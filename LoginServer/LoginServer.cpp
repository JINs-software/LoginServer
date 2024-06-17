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

	if (!m_RedisConn->connect("127.0.0.1", 6379)) {
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
		if (msgHdr.code != dfPACKET_CODE) {
			// �ڵ� ����ġ
			// ���� ���� ����!
			DebugBreak();
			break;
		}
		if (recvBuff.GetUseSize() < sizeof(stMSG_HDR) + msgHdr.len) {
			// �޽��� �̿ϼ�
			DebugBreak();
			break;
		}

		recvBuff >> msgHdr;
		if (!Decode(msgHdr.randKey, msgHdr.len, msgHdr.checkSum, recvBuff.GetDequeueBufferPtr())) {
			DebugBreak();
			// ���� ���� ����?
		}

		UINT dequeueSize = 0;
		while (recvBuff.GetUseSize() > 0) {		
			WORD type;
			recvBuff.Peek(&type);

			if (type == en_PACKET_CS_LOGIN_REQ_LOGIN) {
				// �α��� ��û ó��
				stMSG_LOGIN_REQ message;
				recvBuff.Dequeue((BYTE*)&message, sizeof(stMSG_LOGIN_REQ));
				dequeueSize += sizeof(stMSG_LOGIN_REQ);
				
				Proc_LOGIN_REQ(sessionID, message);		// 1. ���� DB ���� �� ���� ���� Ȯ��
			}

			//switch (type)
			//{
			//case (WORD)en_PACKET_CS_LOGIN_SERVER:
			//{
			//	// �α��� ��û ó��
			//	stMSG_LOGIN_REQ message;
			//	recvBuff.Dequeue((BYTE*)&message, sizeof(stMSG_LOGIN_REQ));
			//	dequeueSize += sizeof(stMSG_LOGIN_REQ);
			//
			//	Proc_LOGIN_REQ(sessionID, message);		// 1. ���� DB ���� �� ���� ���� Ȯ��
			//									// 2. ��ū ����
			//}									// 3. Memory DB�� ��ū ����
			//									// 4. Ŭ���̾�Ʈ�� ��ū �۽�
			//break;
			//default:
			//	DebugBreak();
			//	break;
			//}
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
			//InsertSessionKeyToRedis(message.AccountNo, message.SessionKey);
			// ���� �׽�Ʈ
			InsertSessionKeyToRedis(message.AccountNo, "0");
		}
	}


	// 3. Ŭ���̾�Ʈ�� ��ū ���� (WSASend)
	Proc_LOGIN_RES(sessionID, resMessage.AccountNo, resMessage.Status, resMessage.ID, resMessage.Nickname, L"127.0.0.1", 0, L"127.0.0.1", 12001);
}

void LoginServer::Proc_LOGIN_RES(UINT64 sessionID, INT64 accountNo, BYTE status, const WCHAR* id, const WCHAR* nickName, const WCHAR* gameserverIP, USHORT gameserverPort, const WCHAR* chatserverIP, USHORT chatserverPort)
{
	// �α��� ���� �޽��� ����
	//std::cout << "[Send_RES_SECTOR_MOVE] sessionID: " << sessionID << ", accountNo: " << AccountNo << std::endl;
	// Unicast Reply
	JBuffer* sendMessage = m_SerialBuffPoolMgr.GetTlsMemPool().AllocMem(1);
	sendMessage->ClearBuffer();

	stMSG_HDR* hdr = sendMessage->DirectReserve<stMSG_HDR>();
	hdr->code = dfPACKET_CODE;
	hdr->len = sizeof(stMSG_LOGIN_RES);
	hdr->randKey = (BYTE)rand();

	(*sendMessage) << (WORD)en_PACKET_CS_LOGIN_RES_LOGIN << accountNo << status;
	sendMessage->Enqueue((BYTE*)id, sizeof(WCHAR[20]));
	sendMessage->Enqueue((BYTE*)nickName, sizeof(WCHAR[20]));
	sendMessage->Enqueue((BYTE*)gameserverIP, sizeof(WCHAR[16]));
	(*sendMessage) << gameserverPort;
	sendMessage->Enqueue((BYTE*)chatserverIP, sizeof(WCHAR[16]));
	(*sendMessage) << chatserverPort;

	Encode(hdr->randKey, hdr->len, hdr->checkSum, sendMessage->GetBufferPtr(sizeof(stMSG_HDR)));

	//SendPacket(sessionID, sendMessage);
	if (!SendPacket(sessionID, sendMessage)) {
		m_SerialBuffPoolMgr.GetTlsMemPool().FreeMem(sendMessage);
	}
}

bool LoginServer::CheckSessionKey(INT64 accountNo, const char* sessionKey)
{
	DBConnection* dbConn = (DBConnection*)TlsGetValue(m_DBConnTlsIdx);

	//// ���� Ű�� Ȯ���ϴ� SQL ����
	//const WCHAR* query = L"SELECT accountno FROM accountdb.sessionkey WHERE accountno = ? AND sessionkey = ?";
	//SQLLEN sqlLen = 0;
	//
	//// ���� ���ε� ����
	//dbConn->Unbind();
	//// ù ��° �Ķ���ͷ� ���� ��ȣ ���ε�
	//dbConn->BindParam(1, SQL_C_SBIGINT, SQL_BIGINT, sizeof(accountNo), &accountNo, &sqlLen);
	//// �� ��° �Ķ���ͷ� ���� Ű ���ε�
	//dbConn->BindParam(2, SQL_C_CHAR, SQL_CHAR, 64, (SQLPOINTER)sessionKey, &sqlLen);
	
	// ���� �׽�Ʈ
	const WCHAR* query = L"SELECT accountno FROM accountdb.sessionkey WHERE accountno = ? AND sessionkey IS NULL";
	SQLLEN sqlLen = 0;

	// ���� ���ε� ����
	dbConn->Unbind();
	// ù ��° �Ķ���ͷ� ���� ��ȣ ���ε�
	dbConn->BindParam(1, SQL_C_SBIGINT, SQL_BIGINT, sizeof(accountNo), &accountNo, &sqlLen);

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

		//resMessage.Status = static_cast<BYTE>(status);	// ���� �׽�Ʈ
		resMessage.Status = static_cast<BYTE>(dfLOGIN_STATUS_OK);

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
