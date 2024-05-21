#pragma once
#include "CLanOdbcServer.h"
#include "LoginServerConfig.h"
#include "LOGIN_PROTOCOL.h"

#include "CRedisConn.h"

class LoginServer : public CLanOdbcServer
{
private:
	struct stConnection
	{
		UINT64 sessionID;
		time_t connTime;
	};
	std::queue<stConnection>	m_ConnectionQueue;
	SRWLOCK						m_ConnectionQueueLock;

	std::set<UINT64>			m_LoginPacketRecvedSet;
	std::set<UINT64>			m_TimeOutSet;
	std::mutex					m_LoginProcMtx;

	DWORD						m_DBConnTlsIdx;

	/*********************************
	* ������ ����
	*********************************/
	bool m_TimeOutCheckRunning;

	/*********************************
	* Redis
	*********************************/
	RedisCpp::CRedisConn		m_RedisConn;

public:
	//CLanOdbcServer(int32 dbConnectionCnt, WCHAR* odbcConnStr,
	//	const char* serverIP, uint16 serverPort,
	//	DWORD numOfIocpConcurrentThrd, uint16 numOfWorkerThreads,
	//	uint16 maxOfConnections
	//)
	LoginServer(int32 dbConnectionCnt, WCHAR* odbcConnStr, 
		const char* serverIP, uint16 serverPort,
		DWORD numOfIocpConcurrentThrd, uint16 numOfWorkerThreads,
		uint16 maxOfConnections
		)
		: CLanOdbcServer(dbConnectionCnt, odbcConnStr, serverIP, serverPort, numOfIocpConcurrentThrd, numOfWorkerThreads, maxOfConnections)
	{
		InitializeSRWLock(&m_ConnectionQueueLock);
		m_DBConnTlsIdx = TlsAlloc();
	}

	bool Start() {
		if (!CLanOdbcServer::Start(ODBC_CONNECTION_STRING)) {
			DebugBreak();
			return false;
		}

		// Redis Ŀ�ؼ�
		if (!m_RedisConn.connect("127.0.0.1", 6379)) {
			std::cout << "redis connect error " << m_RedisConn.getErrorStr() << std::endl;
			DebugBreak();
			return false;
		}

		return true;
	}
	void Stop() {

	}

private:
	// Ÿ�Ӿƿ� üĿ ������
	static UINT __stdcall TimeOutCheckThreadFunc(void* arg);

	virtual bool OnWorkerThreadCreate(HANDLE thHnd) override;
	virtual void OnWorkerThreadStart() override;
	virtual bool OnConnectionRequest(/*IP, Port*/) override;
	// �α��� ���� ���� Ŭ���̾�Ʈ ����
	virtual void OnClientJoin(UINT64 sesionID) override;
	// �α��� ���� ���� -> �α��� ��û ��Ŷ ���� (OnClientJoin���� ���� OnRecv������ Ÿ�� �ƿ� �ߵ� �ʿ�)
	virtual void OnRecv(UINT64 sessionID, JBuffer& recvBuff);

	// �޽��� ó��
	void Proc_LOGIN_REQ(UINT64, stMSG_LOGIN_REQ);
	void Proc_LOGIN_RES(UINT64, BYTE, stMSG_LOGIN_RES);


	// DB ����
	bool CheckSessionKey(INT64 accountNo, const char* sessionKey);
	bool GetAccountInfo(INT64 accountNo, stMSG_LOGIN_RES& resMessage);

	// Redis ����
	void InsertSessionKeyToRedis(INT64 accountNo, const char* sessionKey);
};

