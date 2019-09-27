#ifndef XYSocket_H
#define XYSocket_H
//---------------------------------------------------------------------------
#include <WinSock2.h>
#include <ws2ipdef.h>
#include <MSWSock.h>
#include <mstcpip.h>
#include <IPHlpApi.h>
//---------------------------------------------------------------------------
#pragma comment(lib, "ws2_32.lib")
//---------------------------------------------------------------------------
#define MALLOC(x) HeapAlloc(GetProcessHeap(), 0, (x))
#define FREE(x) HeapFree(GetProcessHeap(), 0, (x))
//---------------------------------------------------------------------------
#define XYSOCKET_TYPE_RAW													0
#define XYSOCKET_TYPE_UDP													1
// listen
#define XYSOCKET_TYPE_TCP													2
// client
#define XYSOCKET_TYPE_TCP0													3
// server
#define XYSOCKET_TYPE_TCP1													4

#define XYSOCKET_CLOSE														0
#define XYSOCKET_CONNECT													1
#define XYSOCKET_RECV														2
#define XYSOCKET_SEND														3
#define XYSOCKET_TIMEOUT													4

#define XYSOCKET_ERROR_ABORT0												1
#define XYSOCKET_ERROR_ABORT1												2
#define XYSOCKET_ERROR_TIMEOUT												3
#define XYSOCKET_ERROR_REFUSED												4
#define XYSOCKET_ERROR_FAILED												5
#define XYSOCKET_ERROR_ACCEPT												6
#define XYSOCKET_ERROR_ACCEPTED												7
#define XYSOCKET_ERROR_OVERFLOW												8

#define XYSOCKET_THREAD_UDP													0
#define XYSOCKET_THREAD_LISTEN												1
#define XYSOCKET_THREAD_CONNECT												2
#define XYSOCKET_THREAD_SERVER												3
#define XYSOCKET_THREAD_CLIENT												4
//---------------------------------------------------------------------------
typedef int (CALLBACK *XYSOCKET_PROCEDURE)(LPVOID, LPVOID **, LPVOID, SOCKET, BYTE, BYTE, SOCKADDR *, int *, const char *, int);
//---------------------------------------------------------------------------
typedef struct tagXYSOCKET_CONTEXT
{
	LPVOID context;
	SOCKET s;
	BYTE states;
	BYTE type;
	BYTE flags;
	BYTE reserved;
}XYSOCKET_CONTEXT, *PXYSOCKET_CONTEXT;

typedef struct tagXYSOCKET_SET
{
	CRITICAL_SECTION cs;
	HANDLE hevent;
	HANDLE hthread;

	UINT s_maximum;
	UINT s_count;
	XYSOCKET_CONTEXT s_array[1];
}XYSOCKET_SET, *PXYSOCKET_SET;

typedef struct tagXYSOCKET
{
	PXYSOCKET_SET boxes[5];	// udp, raw;listen;connect;tcp (server);tcp (client)

	LPVOID parameter0;
	LPVOID parameter1;

	BOOL working;
	CRITICAL_SECTION cs;

	XYSOCKET_PROCEDURE procedure;
}XYSOCKET, *PXYSOCKET;
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
BOOL WINAPI XYSocketsStartup(PXYSOCKET ps, LPVOID parameter0, LPVOID parameter1, XYSOCKET_PROCEDURE procedure);
VOID WINAPI XYSocketsCleanup(PXYSOCKET ps);

PXYSOCKET_SET WINAPI XYSocketLaunchThread(PXYSOCKET ps, UINT thread_index, UINT maximum);

SOCKET WINAPI XYUDPBind(PXYSOCKET ps, int type, LPVOID *pointer, LPVOID context, const SOCKADDR *psa, int addresslength);
int WINAPI XYUDPSendTo(SOCKET s, const SOCKADDR *psa, int addresslength, const char *buffer, int length);

SOCKET WINAPI XYTCPConnect(PXYSOCKET ps, LPVOID context, const SOCKADDR *psa, int addresslength, int sendbuffersize);
SOCKET WINAPI XYTCPListen(PXYSOCKET ps, LPVOID *pointer, LPVOID context, const SOCKADDR *psa, int addresslength);
int WINAPI XYTCPSend(SOCKET s, const char *buffer, int length, UINT seconds);
//---------------------------------------------------------------------------
#endif