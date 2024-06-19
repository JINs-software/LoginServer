#include "LoginServer.h"
#include <conio.h>

int main() {
	//LoginServer* loginserver = new LoginServer(10, (WCHAR*)ODBC_CONNECTION_STRING, NULL, 30000, 0, 10, 1000);
	LoginServer* loginserver = new LoginServer(LOGIN_SERVER_NUM_OF_DB_CONN, ODBC_CONNECTION_STRING, FALSE, 
		LOGIN_SERVER_IP, LOGIN_SERVER_PORT,
		NUM_OF_IOCP_CONCURRENT_THREAD, NUM_OFIOCP_WORKER_THREAD, MAX_CLIENT_CONNECTION,
		NUM_OF_TLSMEMPOOL_INIT_MEM_UNIT, NUM_OF_TLSMEMPOOL_CAPACITY,
		FALSE, FALSE,
		LOGIN_SERVER_SERIAL_BUFFER_SIZE, LOGIN_SERVER_RECV_BUFFER_SIZE,
		LOGIN_SERVER_PROTOCOL_CODE, LOGIN_SERVER_PACKET_CODE);


	if (!loginserver->Start()) {
		std::cout << "loginserver->Start() Fail!" << std::endl;
		loginserver->Stop();
		return 0;
	}

	char ctr;
	while (true) {
		if (_kbhit()) {
			ctr = _getch();

			if (ctr == 'q' || ctr == 'Q') {
				break;
			}
		}
	}

	loginserver->Stop();
	
}