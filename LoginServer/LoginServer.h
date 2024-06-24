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
	uint64							m_TotalLoginCnt;		// 로그인 요청 메시지 처리 -> 인증 -> 로그인 정상 처리 응답 메시지 송신 후 카운터 증가
	uint64							m_TotalLoginFailCnt;	// 로그인 요청 메시지 처리 -> 인증 실패(DB 조회/계정 획득/Redis 토큰 삽입 실패) 
															// -> 로그인 인증 실패 응답 메시지 송신 후 카운터 증가

private:
	bool							m_ServerStart;			// Stop 호출 시 플래그 on, Stop 호출 없이 서버 객체 소멸자 호출 시 Stop 함수 호출(정리 작업)
	uint16							m_NumOfIOCPWorkers;		// IOCP 작업자 스레드 별 Redis 커넥션을 맺기 위해 생성자에서 해당 변수 초기화

	// 클라이언트 세션ID <->  클라이언트 호스트 주소 맵핑
	std::map<UINT64, SOCKADDR_IN>	m_ClientHostAddrMap;
	std::mutex						m_ClientHostAddrMapMtx;

	const char m_Client_CLASS1[16] = "10.0.1.2";
	const char m_Client_CLASS2[16] = "10.0.2.2";

	/*********************************
	* DB
	*********************************/
	//DBConnection*				m_DBConn;
	// => 작업자 스레드가 필요 시마다 커넥션 풀로부터 할당 받도록 한다. (멀티 작업자 스레드)

	/*********************************
	* Redis
	*********************************/
	//RedisCpp::CRedisConn*		m_RedisConn;	// 'm_RedisConn' 빨간줄, 불완전한 형식은 사용할 수 없습니다.
	// => 레디스의 커맨드 인자로 전달되는 redisContext는 thread-safe하지 않다. 
	// 따라서 connect로 부터 할당받는 컨텍스트를 여러 개 생성하여 레디스 연결 풀을 만들면 어떨까?
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
	LoginServerMont*			m_ServerMont;		// LoginServer::Start 함수에서 생성 및 Start 호출
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
	// 로그인 서버 접속 클라이언트 연결
	// - LoginServerMont::IncrementSessionCount() 호출
	virtual void OnClientJoin(UINT64 sessionID, const SOCKADDR_IN& clientSockAddr) override;

	// 로그인 서버 접속 클라이언트 연결 종료
	// - DecrementSessionCount
	virtual void OnClientLeave(UINT64 sessionID) override;

	// 로그인 서버 연결 -> 로그인 요청 패킷 전송 (OnClientJoin부터 최초 OnRecv까지의 타임 아웃 발동 필요)
	virtual void OnRecv(UINT64 sessionID, JBuffer& recvBuff);

	// 메시지 처리
	void Proc_LOGIN_REQ(UINT64, stMSG_LOGIN_REQ);
	void Proc_LOGIN_RES(UINT64, INT64 accountNo, BYTE status, const WCHAR* id, const WCHAR* nickName, const WCHAR* gameserverIP, USHORT gameserverPort, const WCHAR* chatserverIP, USHORT chatserverPort);

private:
	// DB 접근
	bool CheckSessionKey(INT64 accountNo, const char* sessionKey);
	bool GetAccountInfo(INT64 accountNo, WCHAR* ID, WCHAR* Nickname);

	// Redis 접근
	bool InsertSessionKeyToRedis(INT64 accountNo, const char* sessionKey);

#if defined(CONNECT_TO_MONITORING_SERVER)
	virtual void ServerConsoleLog() override;
#endif

#if defined(CONNECT_TIMEOUT_CHECK_SET)
	// 타임아웃 체커 스레드 함수
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
	LONG					m_NumOfLoginServerSessions;		// 로그인서버 세션 수 (컨넥션 수)
	LONG					m_LoginServerAuthTransaction;	
	LONG					m_LoginServerAuthTPS;			// 로그인서버 인증 처리 초당 횟수

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