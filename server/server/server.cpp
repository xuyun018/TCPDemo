// server.cpp: 定义控制台应用程序的入口点。
//

#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "../../XYSocket.h"

#include <tchar.h>

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

int CALLBACK SocketProcedure(LPVOID parameter, LPVOID **pointer, LPVOID context, SOCKET s, BYTE type, BYTE number, SOCKADDR *psa, int *salength, const char *buffer, int length)
{
	PXYSOCKET ps = (PXYSOCKET)parameter;
	PXYSOCKET_CONTEXT psc = (PXYSOCKET_CONTEXT)context;
	int result = 0;
	PSOCKADDR_IN psai;

	switch (number)
	{
	case XYSOCKET_CLOSE:
		switch (type)
		{
		case XYSOCKET_TYPE_TCP:
			break;
		case XYSOCKET_TYPE_TCP0:
			break;
		case XYSOCKET_TYPE_TCP1:
			OutputDebugString(_T("Server socket close\r\n"));
			break;
		default:
			break;
		}
		break;
	case XYSOCKET_CONNECT:
		switch (type)
		{
		case XYSOCKET_TYPE_TCP0:
			switch (length)
			{
			case 0:
				// 成功
				break;
			case XYSOCKET_ERROR_FAILED:
			case XYSOCKET_ERROR_REFUSED:
			case XYSOCKET_ERROR_OVERFLOW:
			default:
				break;
			}
			break;
		case XYSOCKET_TYPE_TCP1:
			switch (length)
			{
			case XYSOCKET_ERROR_ACCEPT:
				psai = (PSOCKADDR_IN)psa;

				psai->sin_family = AF_INET;

				*salength = sizeof(SOCKADDR_IN);
				break;
			case XYSOCKET_ERROR_ACCEPTED:
				OutputDebugString(_T("Server accept ok\r\n"));
				break;
			case XYSOCKET_ERROR_OVERFLOW:
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
		break;
	case XYSOCKET_RECV:
		switch (type)
		{
		case XYSOCKET_TYPE_TCP0:
			// 这里是 client 的 socket
			break;
		case XYSOCKET_TYPE_TCP1:
			// 这里是 server 的 socket
			if (pointer == NULL)
			{
				WriteBufferToEnd(_T("C:\\Tools\\1.bmp"), (const BYTE *)buffer, length);

				DWORD tickcount0, tickcount1;

				OutputDebugString(_T("server recv buffer\r\n"));

				tickcount0 = GetTickCount();
				//Sleep(1000);
				tickcount1 = GetTickCount();

				TCHAR debugtext[256];
				wsprintf(debugtext, _T("sleep cast %d\r\n"), tickcount1 - tickcount0);
				OutputDebugString(debugtext);
			}
			break;
		default:
			break;
		}
		break;
	case XYSOCKET_SEND:
		break;
	case XYSOCKET_TIMEOUT:
		switch (type)
		{
		case XYSOCKET_TYPE_TCP:
			OutputDebugString(_T("listener timeout\r\n"));
			break;
		case XYSOCKET_TYPE_TCP0:
			break;
		case XYSOCKET_TYPE_TCP1:
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

int _tmain(int argc, _TCHAR* argv[])
{
	XYSOCKET ps[1];
	SOCKET s;
	SOCKADDR_IN sai;
	WSADATA wsad;

	WSAStartup(MAKEWORD(2, 2), &wsad);

	XYSocketsStartup(ps, NULL, NULL, SocketProcedure);

	XYSocketLaunchThread(ps, XYSOCKET_THREAD_LISTEN, 64);
	XYSocketLaunchThread(ps, XYSOCKET_THREAD_SERVER, 64);

	sai.sin_family = AF_INET;
	sai.sin_port = htons(1024);
	sai.sin_addr.s_addr = htonl(INADDR_ANY);

	s = XYTCPListen(ps, NULL, (LPVOID)&sai, (const SOCKADDR *)&sai, sizeof(sai));
	if (s != INVALID_SOCKET)
	{
		MessageBox(NULL, _T("server"), _T(""), MB_OK);
	}

	XYSocketsCleanup(ps);

	WSACleanup();

	return 0;
}


//---------------------------------------------------------------------------