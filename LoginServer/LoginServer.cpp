#include "LoginServer.h"
#include "CRedisConn.h"

bool LoginServer::Start() {
#if !defined(LOGIN_SERVER_ASSERT)
	m_Logger = new Logger("LoginServerLog.txt");
#endif

#if defined(CONNECT_TO_MONITORING_SERVER)
	m_ServerMont = new LoginServerMont(GetTlsMemPoolManager(), m_SerialBufferSize, MONT_SERVER_PROTOCOL_CODE, MONT_SERVER_PACKET_KEY);
	if (m_ServerMont == NULL) {
		std::cout << "[LoginServer::Start] new LoginServerMont() return NULL" << std::endl;
		return false;
	}
	m_ServerMont->Start();
#endif

	// Redis 커넥션
	bool firstConn = true;
	for (uint16 i = 0; i < m_NumOfIOCPWorkers; i++) {
		RedisCpp::CRedisConn* redisConn = new RedisCpp::CRedisConn();	// 형식 지정자가 필요합니다.
		if (redisConn == NULL) {
			std::cout << "[LoginServer::Start] new RedisCpp::CRedisConn() return NULL" << std::endl;
			return false;
		}

		if (!redisConn->connect(REDIS_TOKEN_SERVER_IP, REDIS_TOKEN_SERVER_PORT)) {
			std::cout << "[LoginServer::Start] m_RedisConn->connect(..) return NULL" << std::endl;
			return false;
		}

		if (!redisConn->ping()) {
			std::cout << "[LoginServer::Start] m_RedisConn->ping() return NULL" << std::endl;
			return false;
		}

		if (firstConn) {
			uint32 ret;
			redisConn->flushall(ret);
			firstConn = false;
		}
		m_RedisConnPool.Enqueue(redisConn);
	}

	if (!CLanOdbcServer::Start()) {
		std::cout << "[LoginServer::Start] CLanOdbcServer::Start() return NULL" << std::endl;
		return false;
	}

	m_ServerStart = true;

	return true;
}
void LoginServer::Stop() {
	if (m_ServerStart) {
		CLanOdbcServer::Stop();

		while (m_RedisConnPool.GetSize() > 0) {
			RedisCpp::CRedisConn* redisConn = NULL;
			m_RedisConnPool.Dequeue(redisConn);
			if (redisConn != NULL) {
				delete redisConn;
			}
		}
	}
}

void LoginServer::OnClientJoin(UINT64 sessionID, const SOCKADDR_IN& clientSockAddr)
{
#if defined(CONNECT_TIMEOUT_CHECK_SET)
	// 접속 현황 맵에 삽입
	AcquireSRWLockExclusive(&m_ConnectionMapSrwLock);
	m_ConnectionMap.insert({ sessionID, time(NULL) });
	ReleaseSRWLockExclusive(&m_ConnectionMapSrwLock);
#endif

	m_ClientHostAddrMapMtx.lock();
	if (m_ClientHostAddrMap.find(sessionID) != m_ClientHostAddrMap.end()) {
#if defined(LOGIN_SERVER_ASSERT)
		DebugBreak();
#else
		m_Logger->log("OnClientJoin, m_ClientHostAddrMap.find(sessionID) != m_ClientHostAddrMap.end()");
#endif
	}
	else {
		m_ClientHostAddrMap.insert({ sessionID, clientSockAddr });
	}

	m_ClientHostAddrMapMtx.unlock();

#if defined(CONNECT_TO_MONITORING_SERVER)
	m_ServerMont->IncrementSessionCount(true);
#endif

#if defined(DELEY_TIME_CHECK)
	m_MsecInServerMapMtx.lock();
	m_MsecInServerMap.insert({ sessionID, clock()});
	m_MsecInServerMapMtx.unlock();
#endif
}

void LoginServer::OnClientLeave(UINT64 sessionID)
{
	m_ClientHostAddrMapMtx.lock();
	if (m_ClientHostAddrMap.find(sessionID) == m_ClientHostAddrMap.end()) {
#if defined(LOGIN_SERVER_ASSERT)
		// 로그인 처리 후 클라이언트에서 먼저 끊는 방식이라면, OnClientLeave에서 삭제 처리
		DebugBreak();
#else
		m_Logger->log("OnClientLeave, m_ClientHostAddrMap.find(sessionID) == m_ClientHostAddrMap.end()");
#endif
	}
	else {
		m_ClientHostAddrMap.erase(sessionID);
	}

	m_ClientHostAddrMapMtx.unlock();

#if defined(CONNECT_TO_MONITORING_SERVER)
	m_ServerMont->DecrementSessionCount(true);
#endif

#if defined(DELEY_TIME_CHECK)
	m_MsecInServerMapMtx.lock();
	auto iter = m_MsecInServerMap.find(sessionID);
	if (iter != m_MsecInServerMap.end()) {
		clock_t timeInServer = clock() - iter->second;
		m_TotalMsecInServer += timeInServer;
		m_MaxMsecInServer = max(m_MaxMsecInServer, timeInServer);
		m_MinMsecInServer = min(m_MinMsecInServer, timeInServer);
		m_AvrMsecInServer = m_TotalMsecInServer / (m_TotalLoginCnt + m_TotalLoginFailCnt);

		m_MsecInServerMap.erase(iter);
	}
	else {
#if defined(LOGIN_SERVER_ASSERT)
		DebugBreak();
#else
		m_Logger->log("OnClientLeave, m_MsecInServerMap.find(sessionID) == m_MsecInServerMap.end()");
#endif
	}
	m_MsecInServerMapMtx.unlock();
#endif
}

void LoginServer::OnRecv(UINT64 sessionID, JBuffer& recvBuff)
{	
	while (recvBuff.GetUseSize() >= sizeof(WORD)) {		
		WORD type;
		recvBuff.Peek(&type);

		if (type == en_PACKET_CS_LOGIN_REQ_LOGIN) {
			// 로그인 요청 처리
			stMSG_LOGIN_REQ message;
			recvBuff >> message;
			
#if defined(DELEY_TIME_CHECK)
			clock_t start = clock();
			Proc_LOGIN_REQ(sessionID, message);		// 1. 계정 DB 접근 및 계정 정보 확인
			clock_t end = clock();
			InterlockedAdd((LONG*)&m_TotalLoginDelayMs, end - start);
			m_MaxLoginDelayMs = max(m_MaxLoginDelayMs, end - start);
			m_MinLoginDelayMs = min(m_MinLoginDelayMs, end - start);
			m_AvrLoginDelayMs = m_TotalLoginDelayMs / (m_TotalLoginCnt + m_TotalLoginFailCnt);
#else 
			Proc_LOGIN_REQ(sessionID, message);		// 1. 계정 DB 접근 및 계정 정보 확인
#endif
		}
	}
}

void LoginServer::Proc_LOGIN_REQ(UINT64 sessionID, stMSG_LOGIN_REQ message)
{
#if defined(CONNECT_TIMEOUT_CHECK_SET)
	// 1. 접속 및 타입아웃 확인
	{
		std::lock_guard<std::mutex> lockGuard(m_LoginProcMtx);

		// 타임아웃 체크
		auto iter = m_TimeOutSet.find(sessionID);
		if (iter != m_TimeOutSet.end()) {
			m_TimeOutSet.erase(iter);
			return;
		}

		// 로그인 메시지 수신 확인
		m_LoginPacketRecvedSet.insert(sessionID);
	}
#endif
	stMSG_LOGIN_RES resMessage;
	memset(&resMessage, 0, sizeof(resMessage));
	resMessage.AccountNo = message.AccountNo;
	resMessage.Status = dfLOGIN_STATUS_OK;

	// 2. DB 조회
	if (!CheckSessionKey(message.AccountNo, message.SessionKey)) {
		resMessage.Status = dfLOGIN_STATUS_FAIL;
		InterlockedIncrement64((int64*)&m_TotalLoginFailCnt);
#if defined(LOGIN_SERVER_ASSERT)
		DebugBreak();
#else
		m_Logger->log("CheckSessionKey(" + to_string(message.AccountNo) + ", " + message.SessionKey + ") returns Fail");
#endif
	}
	else {
		//std::cout << "CheckSessionKey Success!!!" << std::endl;

		// 3. Account 정보 획득(stMSG_LOGIN_RES 메시지 활용)
		if (!GetAccountInfo(message.AccountNo, resMessage.ID, resMessage.Nickname/*, resMessage.GameServerIP, resMessage.GameServerPort, resMessage.ChatServerIP, resMessage.ChatServerPort*/)) {
			resMessage.Status = dfLOGIN_STATUS_FAIL;
			InterlockedIncrement64((int64*)&m_TotalLoginFailCnt);
#if defined(LOGIN_SERVER_ASSERT)
			DebugBreak();
#else
			m_Logger->log("GetAccountInfo(" + to_string(message.AccountNo) + ", ..) returns Fail");
#endif
		}
		else {
			// 4. Redis에 토큰 삽입
			if (!InsertSessionKeyToRedis(message.AccountNo, message.SessionKey)) {
				resMessage.Status = dfLOGIN_STATUS_FAIL;
				InterlockedIncrement64((int64*)&m_TotalLoginFailCnt);
#if defined(LOGIN_SERVER_ASSERT)
				DebugBreak();
#else
				m_Logger->log("InsertSessionKeyToRedis(" + to_string(message.AccountNo) + ", " + message.SessionKey + ") returns Fail");
#endif
			}
		}
	}
	
	// 5. 서버 IP/Port 설정
	m_ClientHostAddrMapMtx.lock();
	auto iter = m_ClientHostAddrMap.find(sessionID);
	if (iter == m_ClientHostAddrMap.end()) {
#if defined(LOGIN_SERVER_ASSERT)
		DebugBreak();
#else
		m_Logger->log("Proc_LOGIN_REQ, (서버 IP/Port 설정) m_ClientHostAddrMap.find(sessionID) == m_ClientHostAddrMap.end()");
#endif
	}
	SOCKADDR_IN clientAddr = iter->second;
	m_ClientHostAddrMapMtx.unlock();
	
	char clientIP[16] = { 0, };
	IN_ADDR_TO_STRING(clientAddr.sin_addr, clientIP);

	if (memcmp(clientIP, m_Client_CLASS1, 16) == 0) {
		memcpy(resMessage.GameServerIP, L"10.0.1.1", sizeof(resMessage.GameServerIP));
		resMessage.GameServerPort = ECHO_GAME_SERVER_POPT;
		memcpy(resMessage.ChatServerIP, L"10.0.1.1", sizeof(resMessage.ChatServerIP));
		resMessage.ChatServerPort = CHATTING_SERVER_POPT;
	}
	else if (memcmp(clientIP, m_Client_CLASS2, 16) == 0) {
		memcpy(resMessage.GameServerIP, L"10.0.2.1", sizeof(resMessage.GameServerIP));
		resMessage.GameServerPort = ECHO_GAME_SERVER_POPT;
		memcpy(resMessage.ChatServerIP, L"10.0.2.1", sizeof(resMessage.ChatServerIP));
		resMessage.ChatServerPort = CHATTING_SERVER_POPT;
	}
	else {
		memcpy(resMessage.GameServerIP, L"127.0.0.1", sizeof(resMessage.GameServerIP));
		resMessage.GameServerPort = ECHO_GAME_SERVER_POPT;
		memcpy(resMessage.ChatServerIP, L"127.0.0.1", sizeof(resMessage.ChatServerIP));
		resMessage.ChatServerPort = CHATTING_SERVER_POPT;
	}
	
	
	// 6. 클라이언트에 토큰 전송 (WSASend)
	Proc_LOGIN_RES(sessionID, resMessage.AccountNo, resMessage.Status, resMessage.ID, resMessage.Nickname, resMessage.GameServerIP, resMessage.GameServerPort, resMessage.ChatServerIP, resMessage.ChatServerPort);
}

void LoginServer::Proc_LOGIN_RES(UINT64 sessionID, INT64 accountNo, BYTE status, const WCHAR* id, const WCHAR* nickName, const WCHAR* gameserverIP, USHORT gameserverPort, const WCHAR* chatserverIP, USHORT chatserverPort)
{
	// 로그인 응답 메시지 전송
	//std::cout << "[Send_RES_SECTOR_MOVE] sessionID: " << sessionID << ", accountNo: " << AccountNo << std::endl;
	// Unicast Reply
	JBuffer* sendMessage = AllocSerialSendBuff(sizeof(WORD) + sizeof(INT64) + sizeof(BYTE) + sizeof(WCHAR[20]) + sizeof(WCHAR[20]) + sizeof(WCHAR[16]) + sizeof(USHORT) + sizeof(WCHAR[16]) + sizeof(USHORT));

	(*sendMessage) << (WORD)en_PACKET_CS_LOGIN_RES_LOGIN << accountNo << status;
	sendMessage->Enqueue((BYTE*)id, sizeof(WCHAR[20]));
	sendMessage->Enqueue((BYTE*)nickName, sizeof(WCHAR[20]));
	sendMessage->Enqueue((BYTE*)gameserverIP, sizeof(WCHAR[16]));
	(*sendMessage) << gameserverPort;
	sendMessage->Enqueue((BYTE*)chatserverIP, sizeof(WCHAR[16]));
	(*sendMessage) << chatserverPort;

	//if (!SendPacket(sessionID, sendMessage)) {
	//	m_SerialBuffPoolMgr.GetTlsMemPool().FreeMem(sendMessage);
	//}

	// blocking send 함수 호출
	if (!SendPacketBlocking(sessionID, sendMessage)) {


	}

#if defined(CONNECT_TO_MONITORING_SERVER)
	m_ServerMont->IncrementAuthTransaction(true);
#endif
	InterlockedIncrement64((int64*)&m_TotalLoginCnt);
}

bool LoginServer::CheckSessionKey(INT64 accountNo, const char* sessionKey)
{
	/***************************************
	* DB 커넥션 타임아웃에 대비한 코드로 변경
	* *************************************/
	bool ret;

	// 더미 테스트
	const WCHAR* query = L"SELECT accountno FROM accountdb.sessionkey WHERE accountno = ? AND sessionkey IS NULL";
	SQLLEN sqlLen = 0;

	DBConnection* dbConn;
	bool dbProcSuccess = false;
	while (!dbProcSuccess) {
		// 1. DB 커넥션 할당
		while ((dbConn = HoldDBConnection()) == NULL);	// DBConnection 획득까지 polling

		// 2. 이전 바인딩 해제
		UnBind(dbConn);

		// 3. 첫 번째 파라미터로 계정 번호 바인딩
		if (BindParameter(dbConn, 1, &accountNo)) {
			// 4. 쿼리 실행
			if (!ExecQuery(dbConn, query)) {
				FreeDBConnection(dbConn, true, true);
				continue;
			}
			else {
				if (GetRowCount(dbConn) > 0) {
					ret = true;
				}
				else {
					ret = false;
				}

				dbProcSuccess = true;
			}

		}

		FreeDBConnection(dbConn);
	}

	return ret;
}

bool LoginServer::GetAccountInfo(INT64 accountNo, WCHAR* ID, WCHAR* Nickname)
{
	bool ret;

	// 계정 정보와 상태를 가져오는 SQL 쿼리
	const WCHAR* query = L"SELECT a.userid, a.usernick, s.status FROM accountdb.account a JOIN accountdb.status s ON a.accountno = s.accountno WHERE a.accountno = ?";

	DBConnection* dbConn;
	bool dbProcSuccess = false;
	while (!dbProcSuccess) {
		while ((dbConn = HoldDBConnection()) == NULL);	// DBConnection 획득까지 polling

		// 이전 바인딩 해제
		UnBind(dbConn);

		// 계정 번호를 파라미터로 바인딩
		if (BindParameter(dbConn, 1, &accountNo)) {
			// 쿼리 실행
			if (!ExecQuery(dbConn, query)) {
				FreeDBConnection(dbConn, true, true);
				continue;
			}
			else {
				// 결과를 저장할 변수들 선언
				WCHAR userid[20];  // 사용자 ID
				WCHAR usernick[20];  // 사용자 닉네임
				int status;  // 상태

				// 결과 열을 바인딩
				BindColumn(dbConn, 1, userid, sizeof(userid), NULL);
				BindColumn(dbConn, 2, usernick, sizeof(usernick), NULL);
				BindColumn(dbConn, 3, &status);

				// 결과를 페치하고 응답 구조체에 값 설정
				if (!dbConn->Fetch()) {
					ret = false;
				}
				else {
					ret = true;

					memcpy(ID, userid, sizeof(userid));
					memcpy(Nickname, usernick, sizeof(usernick));
				}

				dbProcSuccess = true;
			}
		}

		FreeDBConnection(dbConn);

	}

	
	return ret;
}

bool LoginServer::InsertSessionKeyToRedis(INT64 accountNo, const char* sessionKey)
{
	bool ret;
	std::string accountNoStr = to_string(accountNo);
	std::string sessionKeyStr(sessionKey, sizeof(stMSG_LOGIN_REQ::SessionKey));

	RedisCpp::CRedisConn* redisConn = NULL;
	while (true) {	// redisConnect 획득까지 폴링
		m_RedisConnPool.Dequeue(redisConn);
		if (redisConn != NULL) {
			break;
		}
	}
	
	uint32 retval;
	if (!redisConn->set(accountNoStr, sessionKeyStr, retval)) {
		ret = false;
	}
	else {
		ret = true;
	}

	m_RedisConnPool.Enqueue(redisConn);
	return ret;
}

void LoginServer::ServerConsoleLog() {
				//================ SERVER CORE CONSOLE MONT ================
	std::cout << "================       Login Server       ================" << std::endl;
	std::cout << "[Login] Login Session Cnt          : " << m_ServerMont->GetNumOfLoginServerSessions() << std::endl;
	std::cout << "[Login] Login Auth TPS             : " << m_ServerMont->GetLoginServerAuthTPS() << std::endl;
	std::cout << "[Login] Login Total Login Success  : " << m_TotalLoginCnt << std::endl;
	std::cout << "[Login] Login Total Login Fail     : " << m_TotalLoginFailCnt << std::endl;
#if defined(DELEY_TIME_CHECK)
	if (m_TotalLoginCnt > 0) {
		std::cout << "[Login] Total Time in Server   : " << m_TotalMsecInServer << std::endl;
		std::cout << "[Login] Average Time in Server : " << m_AvrMsecInServer << std::endl;
		std::cout << "[Login] Max Time in Server     : " << m_MaxMsecInServer << std::endl;
		std::cout << "[Login] Min Time in Server     : " << m_MinMsecInServer << std::endl;

		std::cout << "[Proc Delay] Total Delay Ms    : " << m_TotalLoginDelayMs << std::endl;
		std::cout << "[Proc Delay] Average Delay Ms  : " << m_AvrLoginDelayMs << std::endl;
		std::cout << "[Proc Delay] Max Delay Ms      : " << m_MaxLoginDelayMs << std::endl;
		std::cout << "[Proc Delay] Min Delay Ms      : " << m_MinLoginDelayMs << std::endl;

		std::cout << std::endl;
		std::cout << "m_MsecInServerMap Size  : " << m_MsecInServerMap.size() << std::endl;
		std::cout << "m_ClientHostAddrMap Size: " << m_ClientHostAddrMap.size() << std::endl;
	}
#endif
}

#if defined(CONNECT_TIMEOUT_CHECK_SET)
UINT __stdcall LoginServer::TimeOutCheckThreadFunc(void* arg)
{
	LoginServer* server = (LoginServer*)arg;
	USHORT waitTime;
	time_t nowTime;

	// 탐색에서 3초가 지나지 않은 세션을 기준으로 남은 시간동안 대기 후 루프를 돈다.
	while (server->m_TimeOutCheckRunning) {
		waitTime = CONNECT_LOGIN_REQ_TIMEOUT_SEC;
		nowTime = time(NULL);

		std::queue<UINT> timeoutSessionQueue;

		AcquireSRWLockShared(&server->m_ConnectionMapSrwLock);
		//for (const auto& conn : server->m_ConnectionMap) {
		for (auto iter = server->m_ConnectionMap.begin(); iter != server->m_ConnectionMap.end();) {
			UINT64 sessionID = iter->first;
			time_t connTime = iter->second;

			if (nowTime - connTime >= CONNECT_LOGIN_REQ_TIMEOUT_SEC) {
				std::lock_guard<std::mutex> lockGuard(server->m_LoginProcMtx);
				if (server->m_LoginPacketRecvedSet.find(sessionID) == server->m_LoginPacketRecvedSet.end()) {
					server->m_TimeOutSet.insert(sessionID);
				}

				//iter = server->m_ConnectionMap.erase(iter);
				timeoutSessionQueue.push(sessionID);
			}
			else {
				waitTime = min(waitTime, CONNECT_LOGIN_REQ_TIMEOUT_SEC - (nowTime - connTime));

				iter++;
			}
		}
		ReleaseSRWLockShared(&server->m_ConnectionMapSrwLock);

		if (!timeoutSessionQueue.empty()) {
			AcquireSRWLockExclusive(&server->m_ConnectionMapSrwLock);
			while (!timeoutSessionQueue.empty()) {
				server->m_ConnectionMap.erase(timeoutSessionQueue.front());
				timeoutSessionQueue.pop();
			}
			ReleaseSRWLockExclusive(&server->m_ConnectionMapSrwLock);
		}

		Sleep(waitTime * 1000);
	}
}
#endif

/*****************************************************************************************************
* LoginServerMont
*****************************************************************************************************/

void LoginServerMont::Start() {
#if !defined(LOGIN_SERVER_ASSERT)
	m_Logger = new Logger("LoginServerMontLog.txt");
#endif
	m_CounterThread = (HANDLE)_beginthreadex(NULL, 0, PerformanceCountFunc, this, 0, NULL);
}

void LoginServerMont::OnClientNetworkThreadStart()
{
	AllocTlsMemPool();
}

void LoginServerMont::OnServerConnected()
{
	// 모니터링 서버에 접속 성공 시 로그인 요청 메시지 송신
	JBuffer* loginMsg = AllocSerialSendBuff(sizeof(WORD) + sizeof(int), MONT_SERVER_PROTOCOL_CODE, GetRandomKey());
	*loginMsg << (WORD)en_PACKET_SS_MONITOR_LOGIN;
	*loginMsg << (int)dfSERVER_LOGIN_SERVER;

	stMSG_HDR* hdr = (stMSG_HDR*)loginMsg->GetBeginBufferPtr();
	BYTE* payloads = loginMsg->GetBufferPtr(sizeof(stMSG_HDR));
	Encode(hdr->randKey, hdr->len, hdr->checkSum, payloads, MONT_SERVER_PACKET_KEY);

	if (!SendPacketToServer(loginMsg)) {
		FreeSerialBuff(loginMsg);
	}
	else {
		m_MontConnected = true;
		m_MontConnectting = false;
	}
}

void LoginServerMont::OnServerLeaved()
{
	m_MontConnected = false;
}

void LoginServerMont::OnRecvFromServer(JBuffer& clientRecvRingBuffer)
{
	// 모니터링 서버로부터 수신 받을 패킷 x
}

void LoginServerMont::OnSerialSendBufferFree(JBuffer* serialBuff)
{
	FreeSerialBuff(serialBuff);
}

UINT __stdcall LoginServerMont::PerformanceCountFunc(void* arg)
{
	LoginServerMont* loginServerMont = (LoginServerMont*)arg;

	loginServerMont->AllocTlsMemPool();

	loginServerMont->m_PerfCounter = new PerformanceCounter();
	loginServerMont->m_PerfCounter->SetCpuUsageCounter();
	loginServerMont->m_PerfCounter->SetProcessCounter(dfMONITOR_DATA_TYPE_LOGIN_SERVER_MEM, dfQUERY_PROCESS_USER_VMEMORY_USAGE, L"LoginServer");
	
	clock_t timestamp = clock();
	while (!loginServerMont->m_Stop) {
		clock_t now = clock();
		clock_t interval = now - timestamp;
		if (interval >= CLOCKS_PER_SEC) {
			loginServerMont->m_LoginServerAuthTPS = loginServerMont->m_LoginServerAuthTransaction / (interval / CLOCKS_PER_SEC);
			loginServerMont->m_LoginServerAuthTransaction = 0;
			timestamp = now;
		}

		if (loginServerMont->m_MontConnected) {
			// Connected to Mont Server..
			loginServerMont->SendCounterToMontServer();
		}
		else if(!loginServerMont->m_MontConnectting) {
			// Disonnected to Mont Server..
			if (loginServerMont->ConnectToServer(MONT_SERVER_IP, MONT_SERVER_PORT)) {
				loginServerMont->m_MontConnectting = true;
			}
		}

		Sleep(1000);
	}

	return 0;
}


void LoginServerMont::SendCounterToMontServer()
{
	time_t now = time(NULL);
	m_PerfCounter->ResetPerfCounterItems();
	size_t allocMemPoolUnitCnt = GetAllocMemPoolUsageUnitCnt();

	JBuffer* perfMsg = AllocSerialBuff();
	if (perfMsg == NULL) {
#if defined(LOGIN_SERVER_ASSERT)
		DebugBreak();
#else
		m_Logger->log("SendCounterToMontServe, AllocSerialBuff() returns NULL");
#endif
	}

	stMSG_HDR* hdr;
	stMSG_MONITOR_DATA_UPDATE* body;

	hdr = perfMsg->DirectReserve<stMSG_HDR>();
	if (hdr == NULL) {
#if defined(LOGIN_SERVER_ASSERT)
		DebugBreak();
#else
		m_Logger->log("SendCounterToMontServe, hdr = perfMsg->DirectReserve<stMSG_HDR>() == NULL (dfMONITOR_DATA_TYPE_LOGIN_SERVER_RUN)");
#endif
	}
	hdr->code = MONT_SERVER_PROTOCOL_CODE;
	hdr->len = sizeof(WORD) + sizeof(BYTE) + sizeof(int) + sizeof(int);
	hdr->randKey = GetRandomKey();
	body = perfMsg->DirectReserve<stMSG_MONITOR_DATA_UPDATE>();
	body->Type = en_PACKET_SS_MONITOR_DATA_UPDATE;
	body->DataType = dfMONITOR_DATA_TYPE_LOGIN_SERVER_RUN;
	body->DataValue = 1;
	body->TimeStamp = now;
	Encode(hdr->randKey, hdr->len, hdr->checkSum, (BYTE*)body, MONT_SERVER_PACKET_KEY);

	hdr = perfMsg->DirectReserve<stMSG_HDR>();
	if (hdr == NULL) {
#if defined(LOGIN_SERVER_ASSERT)
		DebugBreak();
#else
		m_Logger->log("SendCounterToMontServe, hdr = perfMsg->DirectReserve<stMSG_HDR>() == NULL (dfMONITOR_DATA_TYPE_LOGIN_SERVER_CPU)");
#endif
	}
	hdr->code = MONT_SERVER_PROTOCOL_CODE;
	hdr->len = sizeof(WORD) + sizeof(BYTE) + sizeof(int) + sizeof(int);
	hdr->randKey = GetRandomKey();
	body = perfMsg->DirectReserve<stMSG_MONITOR_DATA_UPDATE>();
	body->Type = en_PACKET_SS_MONITOR_DATA_UPDATE;
	body->DataType = dfMONITOR_DATA_TYPE_LOGIN_SERVER_CPU;
	body->DataValue = m_PerfCounter->ProcessTotal();
	body->TimeStamp = now;
	Encode(hdr->randKey, hdr->len, hdr->checkSum, (BYTE*)body, MONT_SERVER_PACKET_KEY);

	hdr = perfMsg->DirectReserve<stMSG_HDR>();
	if (hdr == NULL) {
#if defined(LOGIN_SERVER_ASSERT)
		DebugBreak();
#else
		m_Logger->log("SendCounterToMontServe, hdr = perfMsg->DirectReserve<stMSG_HDR>() == NULL (dfMONITOR_DATA_TYPE_LOGIN_SERVER_MEM)");
#endif
	}
	hdr->code = MONT_SERVER_PROTOCOL_CODE;
	hdr->len = sizeof(WORD) + sizeof(BYTE) + sizeof(int) + sizeof(int);
	hdr->randKey = GetRandomKey();
	body = perfMsg->DirectReserve<stMSG_MONITOR_DATA_UPDATE>();
	body->Type = en_PACKET_SS_MONITOR_DATA_UPDATE;
	body->DataType = dfMONITOR_DATA_TYPE_LOGIN_SERVER_MEM;
	body->DataValue = m_PerfCounter->GetPerfCounterItem(dfMONITOR_DATA_TYPE_LOGIN_SERVER_MEM) / (1024 * 1024);
	body->TimeStamp = now;
	Encode(hdr->randKey, hdr->len, hdr->checkSum, (BYTE*)body, MONT_SERVER_PACKET_KEY);

	hdr = perfMsg->DirectReserve<stMSG_HDR>();
	if (hdr == NULL) {
#if defined(LOGIN_SERVER_ASSERT)
		DebugBreak();
#else
		m_Logger->log("SendCounterToMontServe, hdr = perfMsg->DirectReserve<stMSG_HDR>() == NULL (dfMONITOR_DATA_TYPE_LOGIN_SESSION)");
#endif
	}
	hdr->code = MONT_SERVER_PROTOCOL_CODE;
	hdr->len = sizeof(WORD) + sizeof(BYTE) + sizeof(int) + sizeof(int);
	hdr->randKey = GetRandomKey();
	body = perfMsg->DirectReserve<stMSG_MONITOR_DATA_UPDATE>();
	body->Type = en_PACKET_SS_MONITOR_DATA_UPDATE;
	body->DataType = dfMONITOR_DATA_TYPE_LOGIN_SESSION;
	body->DataValue = m_NumOfLoginServerSessions;
	body->TimeStamp = now;
	Encode(hdr->randKey, hdr->len, hdr->checkSum, (BYTE*)body, MONT_SERVER_PACKET_KEY);

	hdr = perfMsg->DirectReserve<stMSG_HDR>();
	if (hdr == NULL) {
#if defined(LOGIN_SERVER_ASSERT)
		DebugBreak();
#else
		m_Logger->log("SendCounterToMontServe, hdr = perfMsg->DirectReserve<stMSG_HDR>() == NULL (dfMONITOR_DATA_TYPE_LOGIN_AUTH_TPS)");
#endif
	}
	hdr->code = MONT_SERVER_PROTOCOL_CODE;
	hdr->len = sizeof(WORD) + sizeof(BYTE) + sizeof(int) + sizeof(int);
	hdr->randKey = GetRandomKey();
	body = perfMsg->DirectReserve<stMSG_MONITOR_DATA_UPDATE>();
	body->Type = en_PACKET_SS_MONITOR_DATA_UPDATE;
	body->DataType = dfMONITOR_DATA_TYPE_LOGIN_AUTH_TPS;
	body->DataValue = m_LoginServerAuthTPS;
	body->TimeStamp = now;
	Encode(hdr->randKey, hdr->len, hdr->checkSum, (BYTE*)body, MONT_SERVER_PACKET_KEY);

	hdr = perfMsg->DirectReserve<stMSG_HDR>();
	if (hdr == NULL) {
#if defined(LOGIN_SERVER_ASSERT)
		DebugBreak();
#else
		m_Logger->log("SendCounterToMontServe, hdr = perfMsg->DirectReserve<stMSG_HDR>() == NULL (dfMONITOR_DATA_TYPE_LOGIN_PACKET_POOL)");
#endif
	}
	hdr->code = MONT_SERVER_PROTOCOL_CODE;
	hdr->len = sizeof(WORD) + sizeof(BYTE) + sizeof(int) + sizeof(int);
	hdr->randKey = GetRandomKey();
	body = perfMsg->DirectReserve<stMSG_MONITOR_DATA_UPDATE>();
	body->Type = en_PACKET_SS_MONITOR_DATA_UPDATE;
	body->DataType = dfMONITOR_DATA_TYPE_LOGIN_PACKET_POOL;
	body->DataValue = allocMemPoolUnitCnt;
	body->TimeStamp = now;
	Encode(hdr->randKey, hdr->len, hdr->checkSum, (BYTE*)body, MONT_SERVER_PACKET_KEY);

	SendPacketToServer(perfMsg);
}