#include "LoginServer.h"
#include "LoginServerConfig.h"
#include "CommonProtocol.h"

#include "CRedisConn.h"

bool LoginServer::Start() {
	if (!CLanOdbcServer::Start()) {
		DebugBreak();
		return false;
	}

	// Redis Ŀ�ؼ�
	m_RedisConn = new RedisCpp::CRedisConn();	// ���� �����ڰ� �ʿ��մϴ�.
	if (m_RedisConn == NULL) {
		return false;
	}

	if (!m_RedisConn->connect(REDIS_TOKEN_SERVER_IP, REDIS_TOKEN_SERVER_PORT)) {
		std::cout << "redis connect error " << m_RedisConn->getErrorStr() << std::endl;
		DebugBreak();
		return false;
	}

	if (!m_RedisConn->ping()) {
		std::cout << "redis ping returns false " << m_RedisConn->getErrorStr() << std::endl;
		DebugBreak();
		return false;
	}

	return true;
}
void LoginServer::Stop() {
	if (m_DBConn != NULL) {
		FreeDBConnection(m_DBConn);
	}
	if (m_RedisConn != NULL) {
		m_RedisConn->disConnect();
	}

	CLanOdbcServer::Stop();
}

UINT __stdcall LoginServer::TimeOutCheckThreadFunc(void* arg)
{
	LoginServer* server = (LoginServer*)arg;
	USHORT waitTime;
	time_t nowTime;

	// Ž������ 3�ʰ� ������ ���� ������ �������� ���� �ð����� ��� �� ������ ����.
	while (server->m_TimeOutCheckRunning) {
		waitTime = CONNECT_LOGIN_REQ_TIMEOUT_SEC;
		nowTime = time(NULL);

		std::queue<UINT> timeoutSessionQueue;

		AcquireSRWLockShared(&server->m_ConnectionMapSrwLock);
		//for (const auto& conn : server->m_ConnectionMap) {
		for(auto iter = server->m_ConnectionMap.begin(); iter != server->m_ConnectionMap.end();) {
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

void LoginServer::OnBeforeCreateThread()
{
	m_DBConn = HoldDBConnection();
}

//void LoginServer::OnWorkerThreadStart()
//{
//	//m_DBConn = HoldDBConnection();
//	//if (m_DBConn == nullptr) {
//	//	DebugBreak();
//	//}
//	// => IOCP �۾��� �����尡 ��Ƽ�� ��� ���� ���� DB Ŀ�ؼ��� �����Ѵ�.
//}
//
//void LoginServer::OnWorkerThreadEnd()
//{
//	//if (m_DBConn != NULL) {
//	//	FreeDBConnection(m_DBConn);
//	//}
//}

void LoginServer::OnClientJoin(UINT64 sesionID)
{
	// ���� ��Ȳ �ʿ� ����
	AcquireSRWLockExclusive(&m_ConnectionMapSrwLock);
	m_ConnectionMap.insert({ sesionID, time(NULL) });
	ReleaseSRWLockExclusive(&m_ConnectionMapSrwLock);
}

void LoginServer::OnRecv(UINT64 sessionID, JBuffer& recvBuff)
{	
	while (recvBuff.GetUseSize() >= sizeof(WORD)) {		
		WORD type;
		recvBuff.Peek(&type);

		if (type == en_PACKET_CS_LOGIN_REQ_LOGIN) {
			// �α��� ��û ó��
			stMSG_LOGIN_REQ message;
			recvBuff >> message;
			
			Proc_LOGIN_REQ(sessionID, message);		// 1. ���� DB ���� �� ���� ���� Ȯ��
		}
	}
}

void LoginServer::Proc_LOGIN_REQ(UINT64 sessionID, stMSG_LOGIN_REQ message)
{
	// 1. ���� �� Ÿ�Ծƿ� Ȯ��
	{
		std::lock_guard<std::mutex> lockGuard(m_LoginProcMtx);

		// Ÿ�Ӿƿ� üũ
		auto iter = m_TimeOutSet.find(sessionID);
		if (iter != m_TimeOutSet.end()) {
			m_TimeOutSet.erase(iter);
			return;
		}

		// �α��� �޽��� ���� Ȯ��
		m_LoginPacketRecvedSet.insert(sessionID);
	}

	BYTE status;
	stMSG_LOGIN_RES resMessage;
	memset(&resMessage, 0, sizeof(resMessage));

	// 2. DB ��ȸ
	if (!CheckSessionKey(message.AccountNo, message.SessionKey)) {
		//	���� �� ���� ���� ����
		
		return;
	}
	else {
		// 2. Account ���� ȹ��(stMSG_LOGIN_RES �޽��� Ȱ��)
		if (!GetAccountInfo(message.AccountNo, resMessage)) {
			//	���� �� ���� ���� ����

			return;
		}
	}

	// 3. Redis�� ��ū ����
	//InsertSessionKeyToRedis(message.AccountNo, message.SessionKey);
	// ���� �׽�Ʈ
	InsertSessionKeyToRedis(message.AccountNo, "0");
	
	
	// 3. Ŭ���̾�Ʈ�� ��ū ���� (WSASend)
	Proc_LOGIN_RES(sessionID, resMessage.AccountNo, resMessage.Status, resMessage.ID, resMessage.Nickname, resMessage.GameServerIP, resMessage.GameServerPort, resMessage.ChatServerIP, resMessage.ChatServerPort);
}

void LoginServer::Proc_LOGIN_RES(UINT64 sessionID, INT64 accountNo, BYTE status, const WCHAR* id, const WCHAR* nickName, const WCHAR* gameserverIP, USHORT gameserverPort, const WCHAR* chatserverIP, USHORT chatserverPort)
{
	// �α��� ���� �޽��� ����
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

	//SendPacket(sessionID, sendMessage);
	if (!SendPacket(sessionID, sendMessage)) {
		m_SerialBuffPoolMgr.GetTlsMemPool().FreeMem(sendMessage);
	}
}

bool LoginServer::CheckSessionKey(INT64 accountNo, const char* sessionKey)
{
	// ���� �׽�Ʈ
	const WCHAR* query = L"SELECT accountno FROM accountdb.sessionkey WHERE accountno = ? AND sessionkey IS NULL";
	SQLLEN sqlLen = 0;

	// ���� ���ε� ����
	UnBind(m_DBConn);

	// ù ��° �Ķ���ͷ� ���� ��ȣ ���ε�
	BindParameter(m_DBConn, 1, &accountNo);

	// ���� ����
	ExecQuery(m_DBConn, query);

	//FetchQuery(m_DBConn);
	//// ����� ��ġ�ϰ� ��ȯ
	//return FetchQuery(m_DBConn);

	return GetRowCount(m_DBConn) > 0 ? true : false;
}

bool LoginServer::GetAccountInfo(INT64 accountNo, stMSG_LOGIN_RES& resMessage)
{
	// ���� ������ ���¸� �������� SQL ����
	const WCHAR* query = L"SELECT a.userid, a.usernick, s.status FROM accountdb.account a JOIN accountdb.status s ON a.accountno = s.accountno WHERE a.accountno = ?";

	// ���� ���ε� ����
	UnBind(m_DBConn);
	// ���� ��ȣ�� �Ķ���ͷ� ���ε�
	BindParameter(m_DBConn, 1, &accountNo);
	// ���� ����
	m_DBConn->Execute(query);

	// ����� ������ ������ ����
	WCHAR userid[20];  // ����� ID
	WCHAR usernick[20];  // ����� �г���
	int status;  // ����

	// ��� ���� ���ε�
	//m_DBConn->BindCol(1, SQL_C_CHAR, sizeof(userid), userid, NULL);
	//m_DBConn->BindCol(2, SQL_C_CHAR, sizeof(usernick), usernick, NULL);
	//m_DBConn->BindCol(3, SQL_C_SLONG, sizeof(status), &status, NULL);
	BindColumn(m_DBConn, 1, userid, sizeof(userid), NULL);
	BindColumn(m_DBConn, 2, usernick, sizeof(usernick), NULL);
	BindColumn(m_DBConn, 3, &status);

	// ����� ��ġ�ϰ� ���� ����ü�� �� ����
	if (!m_DBConn->Fetch()) {
		return false;
	}

	resMessage.AccountNo = accountNo;
	resMessage.Status = static_cast<BYTE>(dfLOGIN_STATUS_OK);
	//mbstowcs(resMessage.ID, (char*)userid, 20);
	//mbstowcs(resMessage.Nickname, (char*)usernick, 20);
	memcpy(&resMessage.ID, userid, sizeof(userid));
	memcpy(&resMessage.Nickname, usernick, sizeof(usernick));
	// GameServerIP, GameServerPort, ChatServerIP, ChatServerPort ����
	wcscpy_s(resMessage.GameServerIP, ECHO_GAME_SERVER_IP_WSTR);
	resMessage.GameServerPort = ECHO_GAME_SERVER_POPT;
	wcscpy_s(resMessage.ChatServerIP, CHATTING_SERVER_IP_WSTR);
	resMessage.ChatServerPort = CHATTING_SERVER_POPT;

	return true;
}

void LoginServer::InsertSessionKeyToRedis(INT64 accountNo, const char* sessionKey)
{
	std::string accountNoStr = to_string(accountNo);
	std::string sessionKeyStr(sessionKey);
	uint32 retval;
	m_RedisConn->set(accountNoStr, sessionKeyStr, retval);
}
