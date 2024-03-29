// client.cpp: 定义控制台应用程序的入口点。
//

#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "../../kakapo.h"

#include <stdio.h>
//---------------------------------------------------------------------------
VOID WriteLog(const TCHAR *filename, const TCHAR *string)
{
	HANDLE hfile;
	DWORD numberofbytes;
	CHAR multibytes[1024];
	UINT k;

	hfile = CreateFile(filename, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hfile != INVALID_HANDLE_VALUE)
	{
		SetFilePointer(hfile, 0, NULL, FILE_END);

		k = WideCharToMultiByte(CP_ACP, 0, string, wcslen(string), multibytes, 1024, NULL, NULL);
		multibytes[k] = '\0';

		WriteFile(hfile, multibytes, k, &numberofbytes, NULL);
		WriteFile(hfile, "\r\n", 2, &numberofbytes, NULL);

		CloseHandle(hfile);
	}
}

BYTE *ReadBuffer(const TCHAR *filename, int *length)
{
	HANDLE hfile;
	DWORD numberofbytes;
	BYTE *buffer = NULL;

	hfile = CreateFile(filename, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hfile != INVALID_HANDLE_VALUE)
	{
		numberofbytes = GetFileSize(hfile, NULL);

		buffer = (BYTE *)MALLOC(numberofbytes);
		if (buffer != NULL)
		{
			ReadFile(hfile, buffer, numberofbytes, &numberofbytes, NULL);

			*length = numberofbytes;
		}

		CloseHandle(hfile);
	}
	return(buffer);
}
//---------------------------------------------------------------------------
typedef struct tagCLIENT_CONTEXT
{
	HANDLE hevent;

	DWORD tickcount;
}CLIENT_CONTEXT, *PCLIENT_CONTEXT;
//---------------------------------------------------------------------------
int kkp_procedure(struct kakapo *pkkp, void ***pointer, void *context, kkp_fd fd, uint8_t type, uint8_t number, void *psa, int *sasize, const char *buffer, int length)
{
	struct kkp_ctx *pctx = (struct kkp_ctx *)context;
	int result = 0;
	struct sockaddr_in *psai;
	PCLIENT_CONTEXT pcc = (PCLIENT_CONTEXT)pctx->context;

	switch (number)
	{
	case KAKAPO_CLOSE:
		switch (type)
		{
		case KAKAPO_TYPE_TCP:
			break;
		case KAKAPO_TYPE_TCP0:
			printf("Client Close\r\n");
			break;
		case KAKAPO_TYPE_TCP1:
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
				if (pointer == NULL)
				{
					if (GetTickCount() - pcc->tickcount > 30000)
					{
						result = KAKAPO_ERROR_TIMEOUT;

						printf("Client Time out\r\n");

						SetEvent(pcc->hevent);
					}
				}
				else
				{
					printf("Client Ok\r\n");

					SetEvent(pcc->hevent);
				}
				break;
			case KAKAPO_ERROR_FAILED:
			case KAKAPO_ERROR_REFUSED:
			case KAKAPO_ERROR_OVERFLOW:
			default:
				if (TRUE)
				{
					printf("Client Failed %d\r\n", length);
				}

				SetEvent(pcc->hevent);
				break;
			}
			break;
		case KAKAPO_TYPE_TCP1:
			switch (length)
			{
			case KAKAPO_ERROR_ACCEPT:
				break;
			case KAKAPO_ERROR_ACCEPTED:
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
			break;
		case KAKAPO_TYPE_TCP1:
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
			break;
		case KAKAPO_TYPE_TCP0:
			//{
			//	static int sss = 0;
			//	sss++;
			//	if (sss == 15)
			//	{
			//		result = KAKAPO_ERROR_FAILED;
			//	}
			//}
			printf("Client time out\r\n");
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
//---------------------------------------------------------------------------
int wmain(int argc, WCHAR* argv[])
{
	struct kakapo pkkp[1];
	kkp_fd fd;
	struct sockaddr_in sai;
	CLIENT_CONTEXT pcc[1];
	WSADATA wsad;
	int i;
	char cp[20];
	WCHAR string[20];

	if (argc > 1)
	{
		i = 0;
		while (argv[1][i] != '\0' && i + 1 < sizeof(cp))
		{
			cp[i] = argv[1][i];
			i++;
		}
		cp[i] = '\0';
	}
	else
	{
		wcscpy(string, L"127.0.0.1");
		//_tcscpy(string, _T("192.168.126.155"));

		wprintf(L"%s\r\n", string);

		i = 0;
		while (string[i] != '\0' && i + 1 < sizeof(cp))
		{
			cp[i] = string[i];
			i++;
		}
		cp[i] = '\0';
	}

	WSAStartup(MAKEWORD(2, 2), &wsad);

	kkp_initialize(pkkp, NULL, NULL, kkp_procedure);

	kkp_launch_thread(pkkp, KAKAPO_THREAD_CONNECT, 64);
	kkp_launch_thread(pkkp, KAKAPO_THREAD_CLIENT, 64);

	pcc->hevent = CreateEvent(NULL, TRUE, FALSE, NULL);
	pcc->tickcount = GetTickCount();

	sai.sin_family = AF_INET;
	sai.sin_port = htons(1024);
	sai.sin_addr.s_addr = inet_addr(cp);
	//sai.sin_addr.s_addr = inet_addr("192.168.132.168");

	int sendbuffersize;
	sendbuffersize = 1024;
	fd = kkp_connect(pkkp, (void *)pcc, (const void *)&sai, sizeof(sai), sendbuffersize);
	if (fd != INVALID_SOCKET)
	{
		WaitForSingleObject(pcc->hevent, INFINITE);

		int sendlength;
		BYTE *sendbuffer = ReadBuffer(L"C:\\Tools\\6012fd737c28a25af0370c2bd02c4ed3.bmp", &sendlength);
		int i;
		int l0, l1;
		if (sendbuffer != NULL)
		{
			l1 = 18000;
			for (i = 0; i < sendlength; i += l1)
			{
				l0 = sendlength - i;
				l0 = l0 < l1 ? l0 : l1;
				kkp_send(fd, (const char *)sendbuffer + i, l0, 5);
			}
		}
	}

	printf("Client quit\r\n");

	getchar();

	CloseHandle(pcc->hevent);

	kkp_uninitialize(pkkp);

	WSACleanup();

	return 0;
}


//---------------------------------------------------------------------------