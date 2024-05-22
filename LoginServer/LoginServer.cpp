#include "LoginServer.h"
#include "LoginServerConfig.h"

#include "CRedisConn.h"

bool LoginServer::Start() {
	if (!CLanOdbcServer::Start()) {
		DebugBreak();
		return false;
	}

	// Redis Ŀ�ؼ�
	m_RedisConn = new RedisCpp::CRedisConn();	// ���� �����ڰ� �ʿ��մϴ�.

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

	// ���� Ž������ 3�ʰ� ������ ���� ������ �������� ���� �ð����� ��� �� ������ ����.
	// �ƹ��� ������ ���ٸ� 3���̴�.
	while (server->m_TimeOutCheckRunning) {
		time_t now = time(NULL);

		// m_ConnectionQueue�� Reader 1 : Writer 1 ����
		// server->m_ConnectionQueue.empty() Ȯ�ο� �־ ����ȭ�� �ʿ� ������ ����
		while (!server->m_ConnectionQueue.empty()) {
			AcquireSRWLockShared(&server->m_ConnectionQueueLock);
			stConnection& conn = server->m_ConnectionQueue.front();
			ReleaseSRWLockShared(&server->m_ConnectionQueueLock);

			if (now - conn.connTime >= CONNECT_LOGIN_REQ_TIMEOUT_SEC) {
				// �ش� ������ �α��� �Ϸᰡ �Ǿ��� �� Ȯ��
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
	// Ÿ�� �ƿ� ť ����
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
			// �޽��� �̿ϼ�
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
				// �α��� ��û ó��
				stMSG_LOGIN_REQ message;
				recvBuff.Dequeue((BYTE*)&message, sizeof(stMSG_LOGIN_REQ));
				dequeueSize += sizeof(stMSG_LOGIN_REQ);

				Proc_LOGIN_REQ(sessionID, message);		// 1. ���� DB ���� �� ���� ���� Ȯ��
												// 2. ��ū ����
			}									// 3. Memory DB�� ��ū ����
												// 4. Ŭ���̾�Ʈ�� ��ū �۽�
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
	// 1. �α��� �Ϸ� �� ����
	bool timeOutFlag = false;
	m_LoginProcMtx.lock();
	// 1-1.Ÿ�� �ƿ� Ȯ��
	auto iter = m_TimeOutSet.find(sessionID);
	if (iter != m_TimeOutSet.end()) {
		// Ÿ�� �ƿ�
		m_TimeOutSet.erase(iter);
		timeOutFlag = true;
	}
	// 1-2. �α��� ��Ŷ ���� �� ����
	else {
		m_LoginPacketRecvedSet.insert(sessionID);
	}
	m_LoginProcMtx.unlock();
	
	BYTE status;
	stMSG_LOGIN_RES resMessage;

	if (timeOutFlag) {
		return;
	}

	// 1. DB ��ȸ
	if (!CheckSessionKey(message.AccountNo, message.SessionKey)) {
		//	���� �� ���� ���� ����
		
	}
	else {
		// 2. Account ���� ȹ��(stMSG_LOGIN_RES �޽��� Ȱ��)
		if (!GetAccountInfo(message.AccountNo, resMessage)) {
			//	���� �� ���� ���� ����
		}
		else {
			// 3. Redis�� ��ū ����
			InsertSessionKeyToRedis(message.AccountNo, message.SessionKey);
		}
	}


	// 3. Ŭ���̾�Ʈ�� ��ū ���� (WSASend)
	Proc_LOGIN_RES(sessionID, status, resMessage);
}

void LoginServer::Proc_LOGIN_RES(UINT64 sessionID, BYTE status, stMSG_LOGIN_RES resMessage)
{
	// �α��� ���� �޽��� ����
}

bool LoginServer::CheckSessionKey(INT64 accountNo, const char* sessionKey)
{
	DBConnection* dbConn = (DBConnection*)TlsGetValue(m_DBConnTlsIdx);

	// ���� Ű�� Ȯ���ϴ� SQL ����
	const WCHAR* query = L"SELECT accountno FROM accountdb.sessionkey WHERE accountno = ? AND sessionkey = ?";

	// ���� ���ε� ����
	dbConn->Unbind();
	// ù ��° �Ķ���ͷ� ���� ��ȣ ���ε�
	dbConn->BindParam(1, SQL_C_SBIGINT, SQL_BIGINT, 0, &accountNo, NULL);
	// �� ��° �Ķ���ͷ� ���� Ű ���ε�
	dbConn->BindParam(2, SQL_C_CHAR, SQL_CHAR, 64, (SQLPOINTER)sessionKey, NULL);
	// ���� ����
	dbConn->Execute(query);


	// ����� ��ġ�ϰ� ��ȯ
	return dbConn->Fetch();
}

bool LoginServer::GetAccountInfo(INT64 accountNo, stMSG_LOGIN_RES& resMessage)
{
	DBConnection* dbConn = (DBConnection*)TlsGetValue(m_DBConnTlsIdx);

	// ���� ������ ���¸� �������� SQL ����
	const WCHAR* query = L"SELECT a.userid, a.usernick, s.status FROM accountdb.account a JOIN accountdb.status s ON a.accountno = s.accountno WHERE a.accountno = ?";

	// ���� ���ε� ����
	dbConn->Unbind();
	// ���� ��ȣ�� �Ķ���ͷ� ���ε�
	dbConn->BindParam(1, SQL_C_SBIGINT, SQL_BIGINT, 0, &accountNo, NULL);
	// ���� ����
	dbConn->Execute(query);

	// ����� ������ ������ ����
	SQLCHAR userid[64];  // ����� ID
	SQLCHAR usernick[64];  // ����� �г���
	int status;  // ����

	// ��� ���� ���ε�
	dbConn->BindCol(1, SQL_C_CHAR, sizeof(userid), userid, NULL);
	dbConn->BindCol(2, SQL_C_CHAR, sizeof(usernick), usernick, NULL);
	dbConn->BindCol(3, SQL_C_SLONG, sizeof(status), &status, NULL);

	// ����� ��ġ�ϰ� ���� ����ü�� �� ����
	if (dbConn->Fetch()) {
		resMessage.AccountNo = accountNo;
		resMessage.Status = static_cast<BYTE>(status);
		mbstowcs(resMessage.ID, (char*)userid, 20);
		mbstowcs(resMessage.Nickname, (char*)usernick, 20);
		// GameServerIP, GameServerPort, ChatServerIP, ChatServerPort ����
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
