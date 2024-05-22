#include "LoginServer.h"
#include <conio.h>

int main() {
	LoginServer* loginserver = new LoginServer(10, (WCHAR*)ODBC_CONNECTION_STRING, NULL, 300, 0, 10, 1000);

	if (!loginserver->Start()) {
		std::cout << "loginserver->Start() Fail!" << std::endl;
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