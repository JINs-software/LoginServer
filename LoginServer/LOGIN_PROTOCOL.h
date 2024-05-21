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
	BYTE	Status;				// 0 (���ǿ���) / 1 (����) ...  �ϴ� defines ���
	
	WCHAR	ID[20];				// ����� ID		. null ����
	WCHAR	Nickname[20];		// ����� �г���	. null ����
	
	WCHAR	GameServerIP[16];	// ���Ӵ�� ����,ä�� ���� ����
	USHORT	GameServerPort;
	WCHAR	ChatServerIP[16];
	USHORT	ChatServerPort;
};