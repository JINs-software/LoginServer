#pragma once

#define LOGIN_SERVER_NUM_OF_DB_CONN			1

//#define ODBC_CONNECTION_STRING				L"Driver={ODBC Driver 18 for SQL Server};Server=(localdb)\\MSSQLLocalDB;Database=TestDB;Trusted_Connection=Yes;"
#define ODBC_CONNECTION_STRING				L"Driver={MySQL ODBC 8.4 ANSI Driver};Server=10.0.1.2;Database=accountdb;User=mainserver;Password=607281;Option=3;"
																				// MySQL DB Server: 10.0.1.2

#define LOGIN_SERVER_IP						"127.0.0.1"
#define LOGIN_SERVER_PORT					10910

#define NUM_OF_IOCP_CONCURRENT_THREAD		0		// default
#define NUM_OFIOCP_WORKER_THREAD			2
#define MAX_CLIENT_CONNECTION				8000

#define NUM_OF_TLSMEMPOOL_INIT_MEM_UNIT		1000
#define NUM_OF_TLSMEMPOOL_CAPACITY			1000

#define LOGIN_SERVER_SERIAL_BUFFER_SIZE		300

#define	LOGIN_SERVER_RECV_BUFFER_SIZE		1000

#define LOGIN_SERVER_PROTOCOL_CODE			119
#define LOGIN_SERVER_PACKET_CODE			50

#define CONNECT_LOGIN_REQ_TIMEOUT_SEC		3

#define REDIS_TOKEN_SERVER_IP				"10.0.2.2"
#define REDIS_TOKEN_SERVER_PORT				6379

#define ECHO_GAME_SERVER_IP_WSTR			L"127.0.0.1"
#define ECHO_GAME_SERVER_POPT				10920
#define CHATTING_SERVER_IP_WSTR				L"127.0.0.1"
#define CHATTING_SERVER_POPT				10930