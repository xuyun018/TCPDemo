#ifndef KAKAPO_H
#define KAKAPO_H
//---------------------------------------------------------------------------
#include <WinSock2.h>
#include <ws2ipdef.h>
#include <MSWSock.h>
#include <mstcpip.h>
#include <IPHlpApi.h>

#include <stdint.h>
//---------------------------------------------------------------------------
#pragma comment(lib, "ws2_32.lib")
//---------------------------------------------------------------------------
#define MALLOC(x) HeapAlloc(GetProcessHeap(), 0, (x))
#define FREE(x) HeapFree(GetProcessHeap(), 0, (x))
//---------------------------------------------------------------------------
#define KAKAPO_TYPE_RAW														0
#define KAKAPO_TYPE_UDP														1
// listen
#define KAKAPO_TYPE_TCP														2
// client
#define KAKAPO_TYPE_TCP0													3
// server
#define KAKAPO_TYPE_TCP1													4

#define KAKAPO_CLOSE														0
#define KAKAPO_CONNECT														1
#define KAKAPO_RECV															2
#define KAKAPO_SEND															3
#define KAKAPO_TIMEOUT														4

#define KAKAPO_ERROR_ABORT0													1
#define KAKAPO_ERROR_ABORT1													2
#define KAKAPO_ERROR_TIMEOUT												3
#define KAKAPO_ERROR_REFUSED												4
#define KAKAPO_ERROR_FAILED													5
#define KAKAPO_ERROR_ACCEPT													6
#define KAKAPO_ERROR_ACCEPTED												7
#define KAKAPO_ERROR_OVERFLOW												8

#define KAKAPO_THREAD_UDP													0
#define KAKAPO_THREAD_LISTEN												1
#define KAKAPO_THREAD_CONNECT												2
#define KAKAPO_THREAD_SERVER												3
#define KAKAPO_THREAD_CLIENT												4
//---------------------------------------------------------------------------
typedef uintptr_t kkp_fd;
//---------------------------------------------------------------------------
struct kakapo;

typedef int(*t_kakapo_procedure)(struct kakapo *, void ***, void *, kkp_fd, uint8_t, uint8_t, void *, int *, const char *, int);
//---------------------------------------------------------------------------
struct kkp_ctx
{
	void *context;

	kkp_fd fd;

	uint8_t states;
	uint8_t type;
	uint8_t flags;
	uint8_t reserved;
};

struct kkp_box
{
	CRITICAL_SECTION cs;
	void *hevent;
	void *hthread;

	uint16_t capacity;
	uint16_t count;
	struct kkp_ctx ctxs[1];
};

struct kakapo
{
	struct kkp_box *boxes[5];	// udp, raw;listen;connect;tcp (server);tcp (client)

	void *parameter0;
	void *parameter1;

	int working;

	CRITICAL_SECTION cs;

	t_kakapo_procedure procedure;
};
//---------------------------------------------------------------------------
int kkp_initialize(struct kakapo *pkkp, void *parameter0, void *parameter1, t_kakapo_procedure procedure);
void kkp_uninitialize(struct kakapo *pkkp);

int kkp_launch_thread(struct kakapo *pkkp, uint8_t index, uint8_t capacity);

kkp_fd kkp_udp_bind(struct kakapo *pkkp, int type, void **pointer, void *context, const void *psa, int sasize);
int kkp_sendto(kkp_fd fd, const void *psa, int sasize, const char *buffer, int length);

kkp_fd kkp_connect(struct kakapo *pkkp, void *context, const void *psa, int sasize, int sendbuffersize);
kkp_fd kkp_listen(struct kakapo *pkkp, void **pointer, void *context, const void *psa, int sasize);
int kkp_send(kkp_fd fd, const char *buffer, int length, unsigned int seconds);
//---------------------------------------------------------------------------
#endif