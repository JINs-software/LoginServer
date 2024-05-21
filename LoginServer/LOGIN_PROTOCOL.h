#pragma once

enum enPACKET_ENUM {
	en_PACKET_CS_LOGIN_REQ,
	en_PACKET_SC_LOGIN_RES,
	en_PACKET_CS_GET_TOKEN
};

struct  stMSG_HDR
{
	unsigned short len;
};
struct stMSG_LOGIN_REQ
{
	INT64	AccountNo;
	char	SessionKey[64];
};
struct stMSG_LOGIN_RES
{
	INT64	AccountNo;
	BYTE	Status;				// 0 (세션오류) / 1 (성공) ...  하단 defines 사용
	
	WCHAR	ID[20];				// 사용자 ID		. null 포함
	WCHAR	Nickname[20];		// 사용자 닉네임	. null 포함
	
	WCHAR	GameServerIP[16];	// 접속대상 게임,채팅 서버 정보
	USHORT	GameServerPort;
	WCHAR	ChatServerIP[16];
	USHORT	ChatServerPort;
};