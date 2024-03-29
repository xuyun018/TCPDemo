// server.cpp: 定义控制台应用程序的入口点。
//

#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "../../kakapo.h"

#include <stdio.h>

void WriteBufferToEnd(const wchar_t *filename, const BYTE *buffer, int length)
{
	HANDLE hfile;
	DWORD numberofbytes;

	hfile = CreateFile(filename, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hfile != INVALID_HANDLE_VALUE)
	{
		SetFilePointer(hfile, 0, NULL, FILE_END);

		WriteFile(hfile, buffer, length, &numberofbytes, NULL);

		CloseHandle(hfile);
	}
}

int kkp_procedure(struct kakapo *pkkp, void ***pointer, void *context, kkp_fd fd, uint8_t type, uint8_t number, void *psa, int *sasize, const char *buffer, int length)
{
	struct kkp_ctx *pctx = (struct kkp_ctx *)context;
	int result = 0;
	struct sockaddr_in *psai;

	switch (number)
	{
	case KAKAPO_CLOSE:
		switch (type)
		{
		case KAKAPO_TYPE_TCP:
			break;
		case KAKAPO_TYPE_TCP0:
			break;
		case KAKAPO_TYPE_TCP1:
			printf("Server socket close\r\n");
			break;
		default:
			break;
		}
		break;
	case KAKAPO_CONNECT:
		switch (type)
		{
		case KAKAPO_TYPE_TCP0:
			switch (length)
			{
			case 0:
				// 成功
				break;
			case KAKAPO_ERROR_FAILED:
			case KAKAPO_ERROR_REFUSED:
			case KAKAPO_ERROR_OVERFLOW:
			default:
				break;
			}
			break;
		case KAKAPO_TYPE_TCP1:
			switch (length)
			{
			case KAKAPO_ERROR_ACCEPT:
				psai = (struct sockaddr_in *)psa;

				psai->sin_family = AF_INET;

				*sasize = sizeof(struct sockaddr_in);
				break;
			case KAKAPO_ERROR_ACCEPTED:
				printf("Server accept ok\r\n");
				break;
			case KAKAPO_ERROR_OVERFLOW:
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
		break;
	case KAKAPO_RECV:
		switch (type)
		{
		case KAKAPO_TYPE_TCP0:
			// 这里是 client 的 socket
			break;
		case KAKAPO_TYPE_TCP1:
			// 这里是 server 的 socket
			if (pointer == NULL)
			{
				WriteBufferToEnd(L"C:\\Tools\\1.bmp", (const BYTE *)buffer, length);

				DWORD tickcount0, tickcount1;

				printf("server recv buffer\r\n");

				tickcount0 = GetTickCount();
				//Sleep(1000);
				tickcount1 = GetTickCount();

				printf("sleep cast %d\r\n", tickcount1 - tickcount0);
			}
			break;
		default:
			break;
		}
		break;
	case KAKAPO_SEND:
		break;
	case KAKAPO_TIMEOUT:
		switch (type)
		{
		case KAKAPO_TYPE_TCP:
			printf("listener timeout\r\n");
			break;
		case KAKAPO_TYPE_TCP0:
			break;
		case KAKAPO_TYPE_TCP1:
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return(result);
}

int wmain(int argc, WCHAR* argv[])
{
	struct kakapo pkkp[1];
	kkp_fd fd;
	struct sockaddr_in sai;
	WSADATA wsad;

	WSAStartup(MAKEWORD(2, 2), &wsad);

	kkp_initialize(pkkp, NULL, NULL, kkp_procedure);

	kkp_launch_thread(pkkp, KAKAPO_THREAD_LISTEN, 64);
	kkp_launch_thread(pkkp, KAKAPO_THREAD_SERVER, 64);

	sai.sin_family = AF_INET;
	sai.sin_port = htons(1024);
	sai.sin_addr.s_addr = htonl(INADDR_ANY);

	fd = kkp_listen(pkkp, NULL, (void *)&sai, (const void *)&sai, sizeof(sai));
	if (fd != INVALID_SOCKET)
	{
		printf("server\r\n");
		getchar();
	}

	kkp_uninitialize(pkkp);

	WSACleanup();

	return 0;
}


//---------------------------------------------------------------------------