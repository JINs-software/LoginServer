#pragma once
#include "CLanOdbcServer.h"
#include "LoginServerConfig.h"
#include "LOGIN_PROTOCOL.h"

//#include "CRedisConn.h"



namespace RedisCpp {
	class CRedisConn;
}



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
	* 스레드 제어
	*********************************/
	bool m_TimeOutCheckRunning;

	/*********************************
	* Redis
	*********************************/

	RedisCpp::CRedisConn*		m_RedisConn;	// 'm_RedisConn' 빨간줄, 불완전한 형식은 사용할 수 없습니다.

public:
	//CLanOdbcServer(int32 dbConnectionCnt, WCHAR* odbcConnStr,
	//	const char* serverIP, uint16 serverPort,
	//	DWORD numOfIocpConcurrentThrd, uint16 numOfWorkerThreads,
	//	uint16 maxOfConnections
	//)
	LoginServer(int32 dbConnectionCnt, const WCHAR* odbcConnStr, const char* serverIP, uint16 serverPort,
		DWORD numOfIocpConcurrentThrd, uint16 numOfWorkerThreads,
		uint16 maxOfConnections
		)
		: CLanOdbcServer(dbConnectionCnt, odbcConnStr, serverIP, serverPort, numOfIocpConcurrentThrd, numOfWorkerThreads, maxOfConnections)
	{
		InitializeSRWLock(&m_ConnectionQueueLock);
		m_DBConnTlsIdx = TlsAlloc();
	}

	bool Start();
	void Stop();

protected:
	// 타임아웃 체커 스레드
	static UINT __stdcall TimeOutCheckThreadFunc(void* arg);

	// 개별 IOCP 작업자 스레드의 초기 설정 수행: 부모 클래스(CLanOdbcServer)가 관리하는 DBConnectionPool로 부터 단일 DB 커넥션을 획득
	// (DBConnectionPool이 관리하는 커넥션은 CLanOdbcServer 생성자에서 생성 및 연결됨)
	virtual void OnWorkerThreadStart() override;
	// 개별 IOCP 작업자 스레드의 종료(스레드 수행 함수로부터 return) 전 DBConnectionPool에 스레드 개별로 획득하였던 커넥션 반환
	virtual void OnWorkerThreadEnd() override;

	virtual bool OnConnectionRequest(/*IP, Port*/) override { return true; }
	// 로그인 서버 접속 클라이언트 연결
	virtual void OnClientJoin(UINT64 sesionID) override;
	virtual void OnClientLeave(UINT64 sessionID) override {}
	// 로그인 서버 연결 -> 로그인 요청 패킷 전송 (OnClientJoin부터 최초 OnRecv까지의 타임 아웃 발동 필요)
	virtual void OnRecv(UINT64 sessionID, JBuffer& recvBuff);
	virtual void OnError() override {}

	// 메시지 처리
	void Proc_LOGIN_REQ(UINT64, stMSG_LOGIN_REQ);
	void Proc_LOGIN_RES(UINT64, BYTE, stMSG_LOGIN_RES);


	// DB 접근
	bool CheckSessionKey(INT64 accountNo, const char* sessionKey);
	bool GetAccountInfo(INT64 accountNo, stMSG_LOGIN_RES& resMessage);

	// Redis 접근
	void InsertSessionKeyToRedis(INT64 accountNo, const char* sessionKey);
};

