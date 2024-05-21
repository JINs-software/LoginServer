#include "CRedisConn.h"

int main() {
	RedisCpp::CRedisConn redisConn;

	redisConn.connect("127.0.0.1", 6379);
}