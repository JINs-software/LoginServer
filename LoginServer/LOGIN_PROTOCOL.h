#pragma once

#pragma pack(push, 1)
struct stMSG_HDR {
	BYTE	code;
	USHORT	len;
	BYTE	randKey;
	BYTE	checkSum;
};
struct stMSG_LOGIN_REQ
{
	WORD	Type;

	INT64	AccountNo;
	char	SessionKey[64];
};
struct stMSG_LOGIN_RES
{
	WORD	Type;
	
	INT64	AccountNo;
	BYTE	Status;				// 0 (세션오류) / 1 (성공) ...  하단 defines 사용
	
	WCHAR	ID[20];				// 사용자 ID		. null 포함
	WCHAR	Nickname[20];		// 사용자 닉네임	. null 포함
	
	WCHAR	GameServerIP[16];	// 접속대상 게임,채팅 서버 정보
	USHORT	GameServerPort;
	WCHAR	ChatServerIP[16];
	USHORT	ChatServerPort;
};
#pragma pack(pop)