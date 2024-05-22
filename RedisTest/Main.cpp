#include "CRedisConn.h"

int main() {
	RedisCpp::CRedisConn redisConn;

	bool connected = false; 

	if (!redisConn.connect("127.0.0.1", 6379)) {
		std::cout << "connect error " << redisConn.getErrorStr() << std::endl;
	}
	else {
		connected = true;
		UINT32 retval;
		if (!redisConn.set("Hello", "My Name is Jin", retval)) {
			std::cout << "set error " << redisConn.getErrorStr() << std::endl;
		}
		else {
			std::cout << retval << std::endl;
		}

	}

	redisConn.disConnect();

}