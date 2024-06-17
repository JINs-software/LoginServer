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
	BYTE	Status;				// 0 (���ǿ���) / 1 (����) ...  �ϴ� defines ���
	
	WCHAR	ID[20];				// ����� ID		. null ����
	WCHAR	Nickname[20];		// ����� �г���	. null ����
	
	WCHAR	GameServerIP[16];	// ���Ӵ�� ����,ä�� ���� ����
	USHORT	GameServerPort;
	WCHAR	ChatServerIP[16];
	USHORT	ChatServerPort;
};
#pragma pack(pop)