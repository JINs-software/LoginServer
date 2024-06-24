#pragma once
#include "CLanOdbcServer.h"
#include "JClient.h"
#include "PerformanceCounter.h"
#include "LoginServerConfig.h"
#include "CommonProtocol.h"

namespace RedisCpp {
	class CRedisConn;
}

class LoginServerMont;

class LoginServer : public CLanOdbcServer
{
private:
	uint64							m_TotalLoginCnt;		// �α��� ��û �޽��� ó�� -> ���� -> �α��� ���� ó�� ���� �޽��� �۽� �� ī���� ����
	uint64							m_TotalLoginFailCnt;	// �α��� ��û �޽��� ó�� -> ���� ����(DB ��ȸ/���� ȹ��/Redis ��ū ���� ����) 
															// -> �α��� ���� ���� ���� �޽��� �۽� �� ī���� ����

private:
	bool							m_ServerStart;			// Stop ȣ�� �� �÷��� on, Stop ȣ�� ���� ���� ��ü �Ҹ��� ȣ�� �� Stop �Լ� ȣ��(���� �۾�)
	uint16							m_NumOfIOCPWorkers;		// IOCP �۾��� ������ �� Redis Ŀ�ؼ��� �α� ���� �����ڿ��� �ش� ���� �ʱ�ȭ

	// Ŭ���̾�Ʈ ����ID <->  Ŭ���̾�Ʈ ȣ��Ʈ �ּ� ����
	std::map<UINT64, SOCKADDR_IN>	m_ClientHostAddrMap;
	std::mutex						m_ClientHostAddrMapMtx;

	const char m_Client_CLASS1[16] = "10.0.1.2";
	const char m_Client_CLASS2[16] = "10.0.2.2";

	/*********************************
	* DB
	*********************************/
	//DBConnection*				m_DBConn;
	// => �۾��� �����尡 �ʿ� �ø��� Ŀ�ؼ� Ǯ�κ��� �Ҵ� �޵��� �Ѵ�. (��Ƽ �۾��� ������)

	/*********************************
	* Redis
	*********************************/
	//RedisCpp::CRedisConn*		m_RedisConn;	// 'm_RedisConn' ������, �ҿ����� ������ ����� �� �����ϴ�.
	// => ������ Ŀ�ǵ� ���ڷ� ���޵Ǵ� redisContext�� thread-safe���� �ʴ�. 
	// ���� connect�� ���� �Ҵ�޴� ���ؽ�Ʈ�� ���� �� �����Ͽ� ���� ���� Ǯ�� ����� ���?
	LockFreeQueue<RedisCpp::CRedisConn*>	m_RedisConnPool;

	/*********************************
	* Timeout Check
	*********************************/
#if defined(CONNECT_TIMEOUT_CHECK_SET)
	std::map<UINT64, time_t>		m_ConnectionMap;
	SRWLOCK							m_ConnectionMapSrwLock;
	std::set<UINT64>			m_LoginPacketRecvedSet;
	std::set<UINT64>			m_TimeOutSet;
	std::mutex					m_LoginProcMtx;
	bool m_ConnectionTimeOutCheck;
	bool m_TimeOutCheckRunning;
#endif

#if defined(CONNECT_TO_MONITORING_SERVER)
	/*********************************
	* Monitoring
	*********************************/
	LoginServerMont*			m_ServerMont;		// LoginServer::Start �Լ����� ���� �� Start ȣ��
#endif

public:
	LoginServer(int32 dbConnectionCnt, const WCHAR* odbcConnStr,
		const char* serverIP, uint16 serverPort,
		DWORD numOfIocpConcurrentThrd, uint16 numOfWorkerThreads, uint16 maxOfConnections,
		size_t tlsMemPoolDefaultUnitCnt, size_t tlsMemPoolDefaultUnitCapacity,
		bool tlsMemPoolReferenceFlag, bool tlsMemPoolPlacementNewFlag,
		UINT serialBufferSize,
#if defined(LOCKFREE_SEND_QUEUE)
		uint32 sessionRecvBuffSize,
#else
		uint32 sessionSendBuffSize, uint32 sessionRecvBuffSize,
#endif
		BYTE protocolCode = dfPACKET_CODE, BYTE packetKey = dfPACKET_KEY
	)
		: CLanOdbcServer(dbConnectionCnt, odbcConnStr, serverIP, serverPort, numOfIocpConcurrentThrd, numOfWorkerThreads, maxOfConnections,
			tlsMemPoolDefaultUnitCnt, tlsMemPoolDefaultUnitCapacity, tlsMemPoolReferenceFlag, tlsMemPoolPlacementNewFlag,
			serialBufferSize, sessionRecvBuffSize, protocolCode, packetKey),
		m_ServerStart(false), m_NumOfIOCPWorkers(numOfWorkerThreads),
		m_TotalLoginCnt(0), m_TotalLoginFailCnt(0)
	{
#if defined(CONNECT_TO_MONITORING_SERVER)
		m_ServerMont = NULL;
#endif

#if defined(CONNECT_TIMEOUT_CHECK_SET)
		InitializeSRWLock(&m_ConnectionMapSrwLock);
#endif
	}

	bool Start();
	void Stop();

protected:
	// �α��� ���� ���� Ŭ���̾�Ʈ ����
	// - LoginServerMont::IncrementSessionCount() ȣ��
	virtual void OnClientJoin(UINT64 sessionID, const SOCKADDR_IN& clientSockAddr) override;

	// �α��� ���� ���� Ŭ���̾�Ʈ ���� ����
	// - DecrementSessionCount
	virtual void OnClientLeave(UINT64 sessionID) override;

	// �α��� ���� ���� -> �α��� ��û ��Ŷ ���� (OnClientJoin���� ���� OnRecv������ Ÿ�� �ƿ� �ߵ� �ʿ�)
	virtual void OnRecv(UINT64 sessionID, JBuffer& recvBuff);

	// �޽��� ó��
	void Proc_LOGIN_REQ(UINT64, stMSG_LOGIN_REQ);
	void Proc_LOGIN_RES(UINT64, INT64 accountNo, BYTE status, const WCHAR* id, const WCHAR* nickName, const WCHAR* gameserverIP, USHORT gameserverPort, const WCHAR* chatserverIP, USHORT chatserverPort);

private:
	// DB ����
	bool CheckSessionKey(INT64 accountNo, const char* sessionKey);
	bool GetAccountInfo(INT64 accountNo, WCHAR* ID, WCHAR* Nickname);

	// Redis ����
	bool InsertSessionKeyToRedis(INT64 accountNo, const char* sessionKey);

#if defined(CONNECT_TO_MONITORING_SERVER)
	virtual void ServerConsoleLog() override;
#endif

#if defined(CONNECT_TIMEOUT_CHECK_SET)
	// Ÿ�Ӿƿ� üĿ ������ �Լ�
	static UINT __stdcall TimeOutCheckThreadFunc(void* arg);
#endif
};

class LoginServerMont : public JClient {
public:
	LoginServerMont(TlsMemPoolManager<JBuffer>* tlsMemPoolMgr, UINT serialBuffSize, BYTE protoCode, BYTE packetKey)
		: 
		JClient(tlsMemPoolMgr, serialBuffSize, protoCode, packetKey),
		m_NumOfLoginServerSessions(0), m_LoginServerAuthTransaction(0), m_LoginServerAuthTPS(0),
		m_PerfCounter(NULL),
		m_Stop(false),
		m_MontConnected(false), m_MontConnectting(false)
	{}

	void Start() {
		m_CounterThread = (HANDLE)_beginthreadex(NULL, 0, PerformanceCountFunc, this, 0, NULL);
	}

	inline void IncrementSessionCount(bool threadSafe = false) {
		if (threadSafe) {
			InterlockedIncrement(&m_NumOfLoginServerSessions);
		}
		else {
			m_NumOfLoginServerSessions++;
		}
	}
	inline void DecrementSessionCount(bool threadSafe = false) {
		if (threadSafe) {
			InterlockedDecrement(&m_NumOfLoginServerSessions);
		}
		else {
			m_NumOfLoginServerSessions--;
		}
	}
	inline void IncrementAuthTransaction(bool threadSafe = false) {
		if (threadSafe) {
			InterlockedIncrement(&m_LoginServerAuthTransaction);
		}
		else {
			m_LoginServerAuthTransaction++;
		}
	}

	inline LONG GetNumOfLoginServerSessions() {
		return m_NumOfLoginServerSessions;
	}
	inline LONG GetLoginServerAuthTPS() {
		return m_LoginServerAuthTPS;
	}

private:
	LONG					m_NumOfLoginServerSessions;		// �α��μ��� ���� �� (���ؼ� ��)
	LONG					m_LoginServerAuthTransaction;	
	LONG					m_LoginServerAuthTPS;			// �α��μ��� ���� ó�� �ʴ� Ƚ��

	bool					m_MontConnected;
	bool					m_MontConnectting;
	PerformanceCounter*		m_PerfCounter;

	bool					m_Stop;
	HANDLE					m_CounterThread;

	virtual void OnClientNetworkThreadStart() override;
	virtual void OnServerConnected() override;
	virtual void OnServerLeaved() override;
	virtual void OnRecvFromServer(JBuffer& clientRecvRingBuffer) override;
	virtual void OnSerialSendBufferFree(JBuffer* serialBuff) override;
	
	
	static UINT __stdcall PerformanceCountFunc(void* arg);
	void SendCounterToMontServer();
};