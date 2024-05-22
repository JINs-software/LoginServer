#include "LoginServer.h"
#include "LoginServerConfig.h"

#include "CRedisConn.h"

bool LoginServer::Start() {
	if (!CLanOdbcServer::Start()) {
		DebugBreak();
		return false;
	}

	// Redis 커넥션
	m_RedisConn = new RedisCpp::CRedisConn();	// 형식 지정자가 필요합니다.

	if (!m_RedisConn->connect("127.0.0.1", 6379)) {
		std::cout << "redis connect error " << m_RedisConn->getErrorStr() << std::endl;
		DebugBreak();
		return false;
	}

	return true;
}
void LoginServer::Stop() {
	m_RedisConn->disConnect();

	CLanOdbcServer::Stop();
}

UINT __stdcall LoginServer::TimeOutCheckThreadFunc(void* arg)
{
	LoginServer* server = (LoginServer*)arg;
	USHORT waitTime = CONNECT_LOGIN_REQ_TIMEOUT_SEC;

	// 이전 탐색에서 3초가 지나지 않은 세션을 기준으로 남은 시간동안 대기 후 루프를 돈다.
	// 아무런 세션이 없다면 3초이다.
	while (server->m_TimeOutCheckRunning) {
		time_t now = time(NULL);

		// m_ConnectionQueue는 Reader 1 : Writer 1 구조
		// server->m_ConnectionQueue.empty() 확인에 있어서 동기화가 필요 없음을 가정
		while (!server->m_ConnectionQueue.empty()) {
			AcquireSRWLockShared(&server->m_ConnectionQueueLock);
			stConnection& conn = server->m_ConnectionQueue.front();
			ReleaseSRWLockShared(&server->m_ConnectionQueueLock);

			if (now - conn.connTime >= CONNECT_LOGIN_REQ_TIMEOUT_SEC) {
				// 해당 접속이 로그인 완료가 되었는 지 확인
				server->m_LoginProcMtx.lock();
				if (server->m_LoginPacketRecvedSet.find(conn.sessionID) == server->m_LoginPacketRecvedSet.end()) {
					server->m_TimeOutSet.insert(conn.sessionID);
				}
				server->m_LoginProcMtx.unlock();
			}
			else {
				waitTime = now - conn.connTime;
				break;
			}
		}
		
		Sleep(waitTime * 1000);
	}

}

void LoginServer::OnWorkerThreadStart()
{
	if (TlsGetValue(m_DBConnTlsIdx) == NULL) {
		
		DBConnection* dbConn = HoldDBConnection();
		if (dbConn == nullptr) {
			DebugBreak();
		}
		else {
			TlsSetValue(m_DBConnTlsIdx, dbConn);
		}
	}
	else {
		DebugBreak();
	}
}

void LoginServer::OnWorkerThreadEnd()
{
	DBConnection* dbConn = (DBConnection*)TlsGetValue(m_DBConnTlsIdx);
	FreeDBConnection(dbConn);
}

void LoginServer::OnClientJoin(UINT64 sesionID)
{
	// 타임 아웃 큐 삽입
	AcquireSRWLockExclusive(&m_ConnectionQueueLock);
	m_ConnectionQueue.push({ sesionID, time(NULL) });
	ReleaseSRWLockExclusive(&m_ConnectionQueueLock);
}

void LoginServer::OnRecv(UINT64 sessionID, JBuffer& recvBuff)
{
	while (recvBuff.GetUseSize() >= sizeof(stMSG_HDR)) {
		stMSG_HDR msgHdr;
		recvBuff.Peek(&msgHdr);

		if (recvBuff.GetUseSize() < sizeof(stMSG_HDR) + msgHdr.len) {
			// 메시지 미완성
			break;
		}

		UINT dequeueSize = 0;
		while (recvBuff.GetUseSize() > 0) {		
			WORD type;
			recvBuff.Peek(&type);

			switch (type)
			{
			case en_PACKET_CS_LOGIN_REQ:
			{
				// 로그인 요청 처리
				stMSG_LOGIN_REQ message;
				recvBuff.Dequeue((BYTE*)&message, sizeof(stMSG_LOGIN_REQ));
				dequeueSize += sizeof(stMSG_LOGIN_REQ);

				Proc_LOGIN_REQ(sessionID, message);		// 1. 계정 DB 접근 및 계정 정보 확인
												// 2. 토큰 생성
			}									// 3. Memory DB에 토큰 삽입
												// 4. 클라이언트에 토큰 송신
			break;
			case en_PACKET_CS_GET_TOKEN:
			{
				
			}
			break;
			default:
				DebugBreak();
				break;
			}
		}
	}
}

void LoginServer::Proc_LOGIN_REQ(UINT64 sessionID, stMSG_LOGIN_REQ message)
{
	// 1. 로그인 완료 셋 삽입
	bool timeOutFlag = false;
	m_LoginProcMtx.lock();
	// 1-1.타임 아웃 확인
	auto iter = m_TimeOutSet.find(sessionID);
	if (iter != m_TimeOutSet.end()) {
		// 타임 아웃
		m_TimeOutSet.erase(iter);
		timeOutFlag = true;
	}
	// 1-2. 로그인 패킷 수신 셋 삽입
	else {
		m_LoginPacketRecvedSet.insert(sessionID);
	}
	m_LoginProcMtx.unlock();
	
	BYTE status;
	stMSG_LOGIN_RES resMessage;

	if (timeOutFlag) {
		return;
	}

	// 1. DB 조회
	if (!CheckSessionKey(message.AccountNo, message.SessionKey)) {
		//	실패 시 세션 연결 종료
		
	}
	else {
		// 2. Account 정보 획득(stMSG_LOGIN_RES 메시지 활용)
		if (!GetAccountInfo(message.AccountNo, resMessage)) {
			//	실패 시 세션 연결 종료
		}
		else {
			// 3. Redis에 토큰 삽입
			InsertSessionKeyToRedis(message.AccountNo, message.SessionKey);
		}
	}


	// 3. 클라이언트에 토큰 전송 (WSASend)
	Proc_LOGIN_RES(sessionID, status, resMessage);
}

void LoginServer::Proc_LOGIN_RES(UINT64 sessionID, BYTE status, stMSG_LOGIN_RES resMessage)
{
	// 로그인 응답 메시지 전송
}

bool LoginServer::CheckSessionKey(INT64 accountNo, const char* sessionKey)
{
	DBConnection* dbConn = (DBConnection*)TlsGetValue(m_DBConnTlsIdx);

	// 세션 키를 확인하는 SQL 쿼리
	const WCHAR* query = L"SELECT accountno FROM accountdb.sessionkey WHERE accountno = ? AND sessionkey = ?";

	// 이전 바인딩 해제
	dbConn->Unbind();
	// 첫 번째 파라미터로 계정 번호 바인딩
	dbConn->BindParam(1, SQL_C_SBIGINT, SQL_BIGINT, 0, &accountNo, NULL);
	// 두 번째 파라미터로 세션 키 바인딩
	dbConn->BindParam(2, SQL_C_CHAR, SQL_CHAR, 64, (SQLPOINTER)sessionKey, NULL);
	// 쿼리 실행
	dbConn->Execute(query);


	// 결과를 페치하고 반환
	return dbConn->Fetch();
}

bool LoginServer::GetAccountInfo(INT64 accountNo, stMSG_LOGIN_RES& resMessage)
{
	DBConnection* dbConn = (DBConnection*)TlsGetValue(m_DBConnTlsIdx);

	// 계정 정보와 상태를 가져오는 SQL 쿼리
	const WCHAR* query = L"SELECT a.userid, a.usernick, s.status FROM accountdb.account a JOIN accountdb.status s ON a.accountno = s.accountno WHERE a.accountno = ?";

	// 이전 바인딩 해제
	dbConn->Unbind();
	// 계정 번호를 파라미터로 바인딩
	dbConn->BindParam(1, SQL_C_SBIGINT, SQL_BIGINT, 0, &accountNo, NULL);
	// 쿼리 실행
	dbConn->Execute(query);

	// 결과를 저장할 변수들 선언
	SQLCHAR userid[64];  // 사용자 ID
	SQLCHAR usernick[64];  // 사용자 닉네임
	int status;  // 상태

	// 결과 열을 바인딩
	dbConn->BindCol(1, SQL_C_CHAR, sizeof(userid), userid, NULL);
	dbConn->BindCol(2, SQL_C_CHAR, sizeof(usernick), usernick, NULL);
	dbConn->BindCol(3, SQL_C_SLONG, sizeof(status), &status, NULL);

	// 결과를 페치하고 응답 구조체에 값 설정
	if (dbConn->Fetch()) {
		resMessage.AccountNo = accountNo;
		resMessage.Status = static_cast<BYTE>(status);
		mbstowcs(resMessage.ID, (char*)userid, 20);
		mbstowcs(resMessage.Nickname, (char*)usernick, 20);
		// GameServerIP, GameServerPort, ChatServerIP, ChatServerPort 설정
		wcscpy_s(resMessage.GameServerIP, L"127.0.0.1");
		resMessage.GameServerPort = 12345;
		wcscpy_s(resMessage.ChatServerIP, L"127.0.0.1");
		resMessage.ChatServerPort = 54321;

		return true;
	}

	return false;
}

void LoginServer::InsertSessionKeyToRedis(INT64 accountNo, const char* sessionKey)
{
	std::string accountNoStr = to_string(accountNo);
	std::string sessionKeyStr(sessionKey);
	uint32 retval;
	m_RedisConn->set(accountNoStr, sessionKeyStr, retval);
}
