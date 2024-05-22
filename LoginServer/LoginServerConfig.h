#pragma once

#define MAX_WORKER_THREAD_CNT				32
#define CONNECT_LOGIN_REQ_TIMEOUT_SEC		3

//#define ODBC_CONNECTION_STRING				L"Driver={ODBC Driver 18 for SQL Server};Server=(localdb)\\MSSQLLocalDB;Database=TestDB;Trusted_Connection=Yes;"
#define ODBC_CONNECTION_STRING				L"Driver={MySQL ODBC 8.4 ANSI Driver};Server=10.0.1.2;Database=chattingserver;User=mainserver;Password=607281;Option=3;"

#define MAX_CLIENT_CONNECTION				1000