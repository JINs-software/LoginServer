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
	std::map<UINT64, time_t>		m_ConnectionMap;
	SRWLOCK							m_ConnectionMapSrwLock;

	std::set<UINT64>			m_LoginPacketRecvedSet;
	std::set<UINT64>			m_TimeOutSet;
	std::mutex					m_LoginProcMtx;

	/*********************************
	* ������ ����
	*********************************/
	bool m_ConnectionTimeOutCheck;
	bool m_TimeOutCheckRunning;

	/*********************************
	* DB
	*********************************/
	DBConnection*				m_DBConn;

	/*********************************
	* Redis
	*********************************/

	RedisCpp::CRedisConn*		m_RedisConn;	// 'm_RedisConn' ������, �ҿ����� ������ ����� �� �����ϴ�.

public:
	//CLanOdbcServer(int32 dbConnectionCnt, WCHAR* odbcConnStr,
	//	const char* serverIP, uint16 serverPort,
	//	DWORD numOfIocpConcurrentThrd, uint16 numOfWorkerThreads,
	//	uint16 maxOfConnections
	//)
	LoginServer(int32 dbConnectionCnt, const WCHAR* odbcConnStr, bool connectionTimeOutCheck,
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
		m_ConnectionTimeOutCheck(connectionTimeOutCheck), m_DBConn(NULL), m_RedisConn(NULL)
	{
		InitializeSRWLock(&m_ConnectionMapSrwLock);
	}

	bool Start();
	void Stop();

protected:
	// Ÿ�Ӿƿ� üĿ ������
	static UINT __stdcall TimeOutCheckThreadFunc(void* arg);

	virtual void OnBeforeCreateThread() override;

	//// ���� IOCP �۾��� �������� �ʱ� ���� ����: �θ� Ŭ����(CLanOdbcServer)�� �����ϴ� DBConnectionPool�� ���� ���� DB Ŀ�ؼ��� ȹ��
	//// (DBConnectionPool�� �����ϴ� Ŀ�ؼ��� CLanOdbcServer �����ڿ��� ���� �� �����)
	//virtual void OnWorkerThreadStart() override;
	//// ���� IOCP �۾��� �������� ����(������ ���� �Լ��κ��� return) �� DBConnectionPool�� ������ ������ ȹ���Ͽ��� Ŀ�ؼ� ��ȯ
	//virtual void OnWorkerThreadEnd() override;

	virtual bool OnConnectionRequest(/*IP, Port*/) override { return true; }
	// �α��� ���� ���� Ŭ���̾�Ʈ ����
	virtual void OnClientJoin(UINT64 sesionID) override;
	virtual void OnClientLeave(UINT64 sessionID) override {}
	// �α��� ���� ���� -> �α��� ��û ��Ŷ ���� (OnClientJoin���� ���� OnRecv������ Ÿ�� �ƿ� �ߵ� �ʿ�)
	virtual void OnRecv(UINT64 sessionID, JBuffer& recvBuff);
	virtual void OnError() override {}

	// �޽��� ó��
	void Proc_LOGIN_REQ(UINT64, stMSG_LOGIN_REQ);
	void Proc_LOGIN_RES(UINT64, INT64 accountNo, BYTE status, const WCHAR* id, const WCHAR* nickName, const WCHAR* gameserverIP, USHORT gameserverPort, const WCHAR* chatserverIP, USHORT chatserverPort);


	// DB ����
	bool CheckSessionKey(INT64 accountNo, const char* sessionKey);
	bool GetAccountInfo(INT64 accountNo, stMSG_LOGIN_RES& resMessage);

	// Redis ����
	void InsertSessionKeyToRedis(INT64 accountNo, const char* sessionKey);
};

