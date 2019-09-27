#include "XYSocket.h"
//---------------------------------------------------------------------------
typedef struct win_fd_set {
	u_int fd_count;					/* how many are SET? */
	SOCKET fd_array[1];				/* an array of SOCKETs */
} win_fd_set;
//---------------------------------------------------------------------------
#define XYFD_ISSET(fd, set)													__WSAFDIsSet(fd, set)
//---------------------------------------------------------------------------
#ifndef XYTCP_NODELAY
#define XYTCP_NODELAY
#endif

#define XYSOCKET_STATE_CONNECTED											0x01
#define XYSOCKET_STATE_DISCONNECTED											0x02
#define XYSOCKET_STATE_UNUSED												0x04
#define XYSOCKET_STATE_SEND													0x08
// 调用者关闭
#define XYSOCKET_STATE_CLOSE												0x10
//---------------------------------------------------------------------------
typedef struct tagXYSOCKET_THREAD
{
	PXYSOCKET ps;
	PXYSOCKET_SET pss;

	HANDLE hevent;
}XYSOCKET_THREAD, *PXYSOCKET_THREAD;
//---------------------------------------------------------------------------
DWORD WINAPI XYSocketDatagramProc(LPVOID parameter);
DWORD WINAPI XYSocketListenProc(LPVOID parameter);
DWORD WINAPI XYSocketConnectProc(LPVOID parameter);
DWORD WINAPI XYSocketReceiveProc(LPVOID parameter);
//---------------------------------------------------------------------------
BOOL XYSocketAdd(PXYSOCKET_SET pss, LPVOID *pointer, LPVOID context, SOCKET s, UINT type)
{
	PXYSOCKET_CONTEXT psc = NULL;
	UINT i;

	EnterCriticalSection(&pss->cs);
	if (pss->s_count < pss->s_maximum)
	{
		psc = &pss->s_array[pss->s_count++];
	}
	else
	{
		for (i = 0; i < pss->s_count; i++)
		{
			psc = &pss->s_array[i];
			if (psc->states&XYSOCKET_STATE_UNUSED)
			{
				break;
			}
		}
		if (i == pss->s_count)
		{
			psc = NULL;
		}
	}
	if (psc != NULL)
	{
		if (pointer != NULL)
		{
			*pointer = (LPVOID)psc;
		}

		psc->context = context;
		psc->s = s;
		psc->states = 0;
		psc->type = type;
		psc->flags = 0;
		psc->reserved = 0;
	}
	LeaveCriticalSection(&pss->cs);

	if (psc != NULL)
	{
		SetEvent(pss->hevent);
	}

	return(psc != NULL);
}
int XYSocketRemove(PXYSOCKET_SET pss, PXYSOCKET_CONTEXT psc0, PXYSOCKET_CONTEXT psc1)
{
	UINT index;
	int result = 0;

	EnterCriticalSection(&pss->cs);
	if (pss->s_count)
	{
		if (psc1)
		{
			CopyMemory(psc1, psc0, sizeof(XYSOCKET_CONTEXT));
		}

		//
		index = psc0 - pss->s_array;

		if (index + 1 == pss->s_count)
		{
			pss->s_count--;
		}

		psc0->states = XYSOCKET_STATE_UNUSED;

		result = 1;
		//
	}
	LeaveCriticalSection(&pss->cs);

	return(result);
}

PXYSOCKET_SET WINAPI XYSocketLaunchThread(PXYSOCKET ps, UINT thread_index, UINT maximum)
{
	PXYSOCKET_SET pss = NULL;
	XYSOCKET_THREAD st;
	PXYSOCKET_THREAD pst = &st;
	LPTHREAD_START_ROUTINE startaddress = NULL;

	if (ps->boxes[thread_index] == NULL)
	{
		pss = (PXYSOCKET_SET)MALLOC(sizeof(XYSOCKET_SET) + sizeof(XYSOCKET_CONTEXT) * maximum);
		if (pss)
		{
			pst->hevent = CreateEvent(NULL, TRUE, FALSE, NULL);

			pst->ps = ps;
			pst->pss = pss;

			//
			pss->s_maximum = maximum;
			pss->s_count = 0;

			InitializeCriticalSection(&pss->cs);

			pss->hevent = CreateEvent(NULL, TRUE, FALSE, NULL);
			switch (thread_index)
			{
			case 0:
				startaddress = XYSocketDatagramProc;
				break;
			case 1:
				startaddress = XYSocketListenProc;
				break;
			case 2:
				startaddress = XYSocketConnectProc;
				break;
			case 3:
			case 4:
				startaddress = XYSocketReceiveProc;
				break;
			default:
				break;
			}
			//

			pss->hthread = CreateThread(NULL, 0, startaddress, (LPVOID)pst, 0, NULL);
			if (pss->hthread)
			{
				WaitForSingleObject(pst->hevent, INFINITE);

				ps->boxes[thread_index] = pss;
			}
			else
			{
				CloseHandle(pss->hevent);

				DeleteCriticalSection(&pss->cs);

				FREE((LPVOID)pss);
				pss = NULL;
			}

			CloseHandle(pst->hevent);
		}
	}

	return(pss);
}
UINT XYSocketsClose(PXYSOCKET ps, PXYSOCKET_SET pss, LPVOID *scs)
{
	XYSOCKET_PROCEDURE procedure;
	PXYSOCKET_CONTEXT psc;
	SOCKET s;
	UINT type;
	UINT i, j;
	UINT count;

	procedure = ps->procedure;

	j = 0;

	EnterCriticalSection(&pss->cs);
	for (i = 0; i < pss->s_count; i++)
	{
		psc = &pss->s_array[i];
		if ((psc->states&XYSOCKET_STATE_UNUSED) == 0)
		{
			scs[j++] = (LPVOID)psc;
		}
	}
	LeaveCriticalSection(&pss->cs);

	count = j;
	while (j)
	{
		j--;

		psc = (PXYSOCKET_CONTEXT)scs[j];

		s = psc->s;
		type = psc->type;

		procedure((LPVOID)ps, NULL, (LPVOID)psc, s, type, XYSOCKET_CLOSE, NULL, 0, NULL, 0);

		XYSocketRemove(pss, psc, NULL);
		closesocket(s);
	}

	return(count);
}
UINT XYSocketsLoad(PXYSOCKET ps, PXYSOCKET_SET pss, LPVOID *scs, FD_SET *fds0, FD_SET *fds1, int *maximum)
{
	PXYSOCKET_CONTEXT psc;
	SOCKET s;
	UINT i;
	UINT count = 0;

	*maximum = 0;

	FD_ZERO(fds0);
	FD_ZERO(fds1);

	EnterCriticalSection(&pss->cs);
	for (i = 0; i < pss->s_count; i++)
	{
		psc = &pss->s_array[i];
		if ((psc->states&XYSOCKET_STATE_UNUSED) == 0)
		{
			scs[count++] = (LPVOID)psc;

			s = psc->s;

			*maximum = s > (*maximum) ? s : (*maximum);

			FD_SET(s, fds0);
			FD_SET(s, fds1);
		}
	}
	LeaveCriticalSection(&pss->cs);

	return(count);
}

VOID XYSocketsClear(PXYSOCKET ps)
{
	PXYSOCKET_SET pss;
	UINT i;

	ps->working = FALSE;

	for (i = 0; i < 5; i++)
	{
		pss = ps->boxes[i];
		if (pss != NULL)
		{
			ps->boxes[i] = NULL;

			if (pss->hevent)
			{
				SetEvent(pss->hevent);
			}
			if (pss->hthread)
			{
				WaitForSingleObject(pss->hthread, INFINITE);
				CloseHandle(pss->hthread);
			}
			if (pss->hevent)
			{
				CloseHandle(pss->hevent);
			}
			//
			DeleteCriticalSection(&pss->cs);

			FREE((LPVOID)pss);
		}
	}
}

DWORD WINAPI XYSocketDatagramProc(LPVOID parameter)
{
	PXYSOCKET_THREAD pst = (PXYSOCKET_THREAD)parameter;
	XYSOCKET_PROCEDURE procedure;
	PXYSOCKET ps;
	PXYSOCKET_SET pss;
	win_fd_set *pfds0;
	win_fd_set *pfds2;
	struct timeval tv;
	int maximum;
	SOCKET s;
	LPVOID *scs;
	UINT count;
	PXYSOCKET_CONTEXT psc;
	char buffer[8192];
	int length;
	struct sockaddr_in6 sai6;
	int addresslength;
	UINT type;
	int errorcode;
	int value;
	UINT i;

	//
	ps = pst->ps;
	pss = pst->pss;
	//

	//
	SetEvent(pst->hevent);
	//

	procedure = ps->procedure;

	scs = (LPVOID *)MALLOC(sizeof(LPVOID) * pss->s_maximum);
	pfds0 = (win_fd_set *)MALLOC(sizeof(win_fd_set) + sizeof(SOCKET) * pss->s_maximum);
	pfds2 = (win_fd_set *)MALLOC(sizeof(win_fd_set) + sizeof(SOCKET) * pss->s_maximum);
	if (scs != NULL && pfds0 != NULL && pfds2 != NULL)
	{
		while (ps->working)
		{
			WaitForSingleObject(pss->hevent, INFINITE);
			ResetEvent(pss->hevent);

			count = XYSocketsLoad(ps, pss, scs, (fd_set *)pfds0, (fd_set *)pfds2, &maximum);

			if (count > 0)
			{
				i = count;

				tv.tv_sec = 5;
				tv.tv_usec = 0;

				value = select(maximum + 1, (fd_set *)pfds0, NULL, (fd_set *)pfds2, &tv);
				switch (value)
				{
				case 0:
					while (i)
					{
						i--;

						psc = (PXYSOCKET_CONTEXT)scs[i];

						s = psc->s;
						type = psc->type;

						errorcode = procedure((LPVOID)ps, NULL, (LPVOID)psc, s, type, XYSOCKET_TIMEOUT, NULL, NULL, NULL, 0);
						if (errorcode)
						{
							procedure((LPVOID)ps, NULL, (LPVOID)psc, s, type, XYSOCKET_CLOSE, NULL, NULL, NULL, 0);

							XYSocketRemove(pss, psc, NULL);
							closesocket(s);

							count--;
						}
					}
					break;
				case SOCKET_ERROR:
				default:
					while (i)
					{
						i--;

						psc = (PXYSOCKET_CONTEXT)scs[i];

						s = psc->s;
						type = psc->type;

						errorcode = 0;

						if (value != SOCKET_ERROR && XYFD_ISSET(s, (fd_set *)pfds0))
						{
							length = 0;
							switch (type)
							{
							case XYSOCKET_TYPE_RAW:
								addresslength = 0;

								length = recv(s, buffer, sizeof(buffer), 0);
								break;
							case XYSOCKET_TYPE_UDP:
								addresslength = sizeof(sai6);
								procedure((LPVOID)ps, NULL, (LPVOID)psc, s, type, XYSOCKET_RECV, (SOCKADDR *)&sai6, &addresslength, NULL, 0);

								length = recvfrom(s, buffer, sizeof(buffer), 0, (SOCKADDR *)&sai6, &addresslength);
								break;
							default:
								break;
							}

							if (length > 0)
							{
								errorcode = procedure((LPVOID)ps, NULL, (LPVOID)psc, s, type, XYSOCKET_RECV, (SOCKADDR *)&sai6, &addresslength, buffer, length);
							}
							else
							{
								if (length == 0)
								{
									errorcode = XYSOCKET_ERROR_FAILED;
								}
								else
								{
									// length == SOCKET_ERROR
									errorcode = WSAGetLastError();
									if (errorcode == WSAEWOULDBLOCK || errorcode == WSAEINTR)
									{
										errorcode = 0;
									}
								}
							}
						}
						else
						{
							if (XYFD_ISSET(s, (fd_set *)pfds2))
							{
								errorcode = XYSOCKET_ERROR_FAILED;
							}
						}

						if (errorcode)
						{
							procedure((LPVOID)ps, NULL, (LPVOID)psc, s, type, XYSOCKET_CLOSE, NULL, NULL, NULL, errorcode);

							XYSocketRemove(pss, psc, NULL);
							closesocket(s);

							count--;
						}
					}
					break;
				}
			}

			if (count)
			{
				SetEvent(pss->hevent);
			}
		}

		XYSocketsClose(ps, pss, scs);
	}

	if (scs != NULL)
	{
		FREE(scs);
	}
	if (pfds0 != NULL)
	{
		FREE(pfds0);
	}
	if (pfds2 != NULL)
	{
		FREE(pfds2);
	}

	return(0);
}

DWORD WINAPI XYSocketListenProc(LPVOID parameter)
{
	PXYSOCKET_THREAD pst = (PXYSOCKET_THREAD)parameter;
	XYSOCKET_PROCEDURE procedure;
	PXYSOCKET ps;
	PXYSOCKET_SET pss;
	win_fd_set *pfds0;
	win_fd_set *pfds2;
	struct timeval tv;
	int maximum;
	SOCKET s0;
	SOCKET s1;
	LPVOID *scs;
	UINT count;
	LPVOID *pointer;
	XYSOCKET_CONTEXT sc;
	PXYSOCKET_CONTEXT psc;
	struct sockaddr_in6 sai6;
	int addresslength;
	int errorcode;
	int value;
	UINT seconds;
	UINT type;
	UINT i;
	BOOL flag = TRUE;

	//
	ps = pst->ps;
	pss = pst->pss;
	//

	//
	SetEvent(pst->hevent);
	//

	procedure = ps->procedure;

	scs = (LPVOID *)MALLOC(sizeof(LPVOID) * pss->s_maximum);
	pfds0 = (win_fd_set *)MALLOC(sizeof(win_fd_set) + sizeof(SOCKET) * pss->s_maximum);
	pfds2 = (win_fd_set *)MALLOC(sizeof(win_fd_set) + sizeof(SOCKET) * pss->s_maximum);
	if (scs != NULL && pfds0 != NULL && pfds2 != NULL)
	{
		while (ps->working)
		{
			WaitForSingleObject(pss->hevent, INFINITE);
			ResetEvent(pss->hevent);

			seconds = 5;

			count = XYSocketsLoad(ps, pss, scs, (fd_set *)pfds0, (fd_set *)pfds2, &maximum);

			if (count > 0)
			{
				i = count;

				//1秒 = 1000毫秒 = 1000000微妙 = 1000000000纳秒

				tv.tv_sec = seconds;
				tv.tv_usec = 0;

				value = select(maximum + 1, (fd_set *)pfds0, NULL, (fd_set *)pfds2, &tv);
				switch (value)
				{
				case 0:
					while (i)
					{
						i--;

						psc = (PXYSOCKET_CONTEXT)scs[i];

						s0 = psc->s;
						type = psc->type;

						//
						errorcode = procedure((LPVOID)ps, NULL, (LPVOID)psc, s0, type, XYSOCKET_TIMEOUT, NULL, NULL, NULL, 0);
						if (errorcode)
						{
							procedure((LPVOID)ps, NULL, (LPVOID)psc, s0, type, XYSOCKET_CLOSE, NULL, NULL, NULL, 0);

							XYSocketRemove(pss, psc, NULL);
							closesocket(s0);

							count--;
						}
					}
					break;
				case SOCKET_ERROR:
				default:
					while (i)
					{
						i--;

						psc = (PXYSOCKET_CONTEXT)scs[i];

						s0 = psc->s;

						if (value != SOCKET_ERROR && XYFD_ISSET(s0, (fd_set *)pfds0))
						{
							// 这里是server
							type = XYSOCKET_TYPE_TCP1;

							addresslength = 0;

							procedure((LPVOID)ps, NULL, (LPVOID)psc, s0, type, XYSOCKET_CONNECT, (SOCKADDR *)&sai6, &addresslength, NULL, XYSOCKET_ERROR_ACCEPT);

							s1 = accept(s0, (SOCKADDR *)&sai6, &addresslength);
							if (s1 != INVALID_SOCKET)
							{
								sc.context = psc->context;
								sc.s = s1;
								sc.type = type;

								pointer = NULL;
								errorcode = procedure((LPVOID)ps, &pointer, (LPVOID)&sc, s1, type, XYSOCKET_CONNECT, (SOCKADDR *)&sai6, &addresslength, NULL, XYSOCKET_ERROR_ACCEPTED);

								if (errorcode || ps->boxes[XYSOCKET_THREAD_SERVER] == NULL || !XYSocketAdd(ps->boxes[XYSOCKET_THREAD_SERVER], pointer, sc.context, sc.s, sc.type))
								{
									if (errorcode == 0)
									{
										// 这个错误有点特殊,是已经触发连接事件成功之后的,需要注意
										errorcode = XYSOCKET_ERROR_OVERFLOW;
									}

									errorcode = procedure((LPVOID)ps, &pointer, (LPVOID)&sc, s1, type, XYSOCKET_CLOSE, NULL, NULL, NULL, errorcode);
									closesocket(s1);
								}
							}
							else
							{
								errorcode = XYSOCKET_ERROR_FAILED;
							}

							if (errorcode != 0)
							{
								// 这里是通知而不是关闭
								procedure((LPVOID)ps, NULL, (LPVOID)psc, s0, type, XYSOCKET_CONNECT, NULL, NULL, NULL, errorcode);
							}
						}
						else
						{
							if (XYFD_ISSET(s0, (fd_set *)pfds2))
							{
								//
								type = psc->type;

								procedure((LPVOID)ps, NULL, (LPVOID)psc, s0, type, XYSOCKET_CLOSE, NULL, NULL, NULL, XYSOCKET_ERROR_FAILED);
								//

								XYSocketRemove(pss, psc, NULL);
								closesocket(s0);

								count--;
							}
						}
					}
					break;
				}
			}

			if (count)
			{
				SetEvent(pss->hevent);
			}
		}

		XYSocketsClose(ps, pss, scs);
	}

	if (scs != NULL)
	{
		FREE(scs);
	}
	if (pfds0 != NULL)
	{
		FREE(pfds0);
	}
	if (pfds2 != NULL)
	{
		FREE(pfds2);
	}

	return(0);
}
DWORD WINAPI XYSocketConnectProc(LPVOID parameter)
{
	PXYSOCKET_THREAD pst = (PXYSOCKET_THREAD)parameter;
	XYSOCKET_PROCEDURE procedure;
	PXYSOCKET ps;
	PXYSOCKET_SET pss;
	win_fd_set *pfds1;
	win_fd_set *pfds2;
	struct timeval tv;
	int maximum;
	SOCKET s;
	LPVOID *scs;
	UINT count;
	LPVOID *pointer;
	XYSOCKET_CONTEXT sc;
	PXYSOCKET_CONTEXT psc;
	UINT type;
	int error = 0;
	int optval;
	int optlen;
	BOOL connected = FALSE;
	int errorcode0;
	int errorcode1;
	int value;
	UINT i;

	//
	ps = pst->ps;
	pss = pst->pss;
	//

	//
	SetEvent(pst->hevent);
	//

	procedure = ps->procedure;

	scs = (LPVOID *)MALLOC(sizeof(LPVOID) * pss->s_maximum);
	pfds1 = (win_fd_set *)MALLOC(sizeof(win_fd_set) + sizeof(SOCKET) * pss->s_maximum);
	pfds2 = (win_fd_set *)MALLOC(sizeof(win_fd_set) + sizeof(SOCKET) * pss->s_maximum);
	if (scs != NULL && pfds1 != NULL && pfds2 != NULL)
	{
		while (ps->working)
		{
			WaitForSingleObject(pss->hevent, INFINITE);
			ResetEvent(pss->hevent);

			count = XYSocketsLoad(ps, pss, scs, (fd_set *)pfds1, (fd_set *)pfds2, &maximum);

			if (count > 0)
			{
				i = count;

				//1秒 = 1000毫秒 = 1000000微妙 = 1000000000纳秒

				tv.tv_sec = 0;
				tv.tv_usec = 400000000;

				value = select(maximum + 1, NULL, (fd_set *)pfds1, (fd_set *)pfds2, &tv);
				switch (value)
				{
				case 0:
					while (i)
					{
						i--;

						psc = (PXYSOCKET_CONTEXT)scs[i];

						s = psc->s;
						type = psc->type;

						// 这里通过pointer参数是否为空来区分是不是超时
						errorcode1 = procedure((LPVOID)ps, NULL, (LPVOID)psc, s, type, XYSOCKET_CONNECT, NULL, NULL, NULL, 0);
						if (errorcode1)
						{
							// 这里不设计为调用CLOSE回调

							XYSocketRemove(pss, psc, NULL);
							closesocket(s);

							count--;
						}
					}
					break;
				case SOCKET_ERROR:
				default:
					while (i)
					{
						i--;

						psc = (PXYSOCKET_CONTEXT)scs[i];

						s = psc->s;
						type = psc->type;

						// 这里特殊一点
						errorcode1 = 0;
						if (XYFD_ISSET(s, (fd_set *)pfds2))
						{
							errorcode1 = XYSOCKET_ERROR_FAILED;

							sc.context = psc->context;
							sc.s = s;
							sc.type = type;
						}
						else
						{
							if (value != SOCKET_ERROR && XYFD_ISSET(s, (fd_set *)pfds1))
							{
								errorcode0 = 0;
								optlen = sizeof(int);
								if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&optval, &optlen) == 0)
								{
									errorcode0 = optval;
								}
								else
								{
									errorcode0 = XYSOCKET_ERROR_REFUSED;
								}

								pointer = NULL;
								errorcode1 = procedure((LPVOID)ps, &pointer, (LPVOID)psc, s, type, XYSOCKET_CONNECT, NULL, NULL, NULL, errorcode0);

								XYSocketRemove(pss, psc, &sc);
								count--;
								psc = NULL;

								if (errorcode0)
								{
									if (errorcode1 == 0)
									{
										errorcode1 = errorcode0;
									}
								}

								if (errorcode1 || ps->boxes[XYSOCKET_THREAD_CLIENT] == NULL || !XYSocketAdd(ps->boxes[XYSOCKET_THREAD_CLIENT], pointer, sc.context, sc.s, sc.type))
								{
									if (errorcode1 == 0)
									{
										errorcode1 = XYSOCKET_ERROR_OVERFLOW;
									}
									// 在下面关闭
								}
							}
						}

						if (errorcode1)
						{
							procedure((LPVOID)ps, NULL, (LPVOID)&sc, s, type, XYSOCKET_CONNECT, NULL, NULL, NULL, errorcode1);

							if (psc != NULL)
							{
								XYSocketRemove(pss, psc, NULL);
								count--;
							}
							closesocket(s);
						}
					}
					break;
				}
			}

			if (count)
			{
				SetEvent(pss->hevent);
			}
		}

		XYSocketsClose(ps, pss, scs);
	}

	if (scs != NULL)
	{
		FREE(scs);
	}
	if (pfds1 != NULL)
	{
		FREE(pfds1);
	}
	if (pfds2 != NULL)
	{
		FREE(pfds2);
	}

	return(error);
}
DWORD WINAPI XYSocketReceiveProc(LPVOID parameter)
{
	PXYSOCKET_THREAD pst = (PXYSOCKET_THREAD)parameter;
	XYSOCKET_PROCEDURE procedure;
	PXYSOCKET ps;
	PXYSOCKET_SET pss;
	win_fd_set *pfds0;
	win_fd_set *pfds2;
	struct timeval tv;
	int maximum;
	SOCKET s;
	LPVOID *scs;
	UINT count;
	LPVOID *pointer;
	PXYSOCKET_CONTEXT psc;
	char buffer[8192];
	int buffersize;
	int length;
	struct sockaddr_in6 sai6;
	int addresslength;
	UINT type;
	UINT seconds;
	int errorcode;
	int value;
	UINT i;

	//
	ps = pst->ps;
	pss = pst->pss;

	//

	//
	SetEvent(pst->hevent);
	//

	procedure = ps->procedure;

	buffersize = sizeof(buffer);

	scs = (LPVOID *)MALLOC(sizeof(LPVOID) * pss->s_maximum);
	pfds0 = (win_fd_set *)MALLOC(sizeof(win_fd_set) + sizeof(SOCKET) * pss->s_maximum);
	pfds2 = (win_fd_set *)MALLOC(sizeof(win_fd_set) + sizeof(SOCKET) * pss->s_maximum);
	if (scs != NULL && pfds0 != NULL && pfds2 != NULL)
	{
		while (ps->working)
		{
			WaitForSingleObject(pss->hevent, INFINITE);
			ResetEvent(pss->hevent);

			seconds = 5;

			count = XYSocketsLoad(ps, pss, scs, (fd_set *)pfds0, (fd_set *)pfds2, &maximum);

			if (count > 0)
			{
				i = count;

				tv.tv_sec = seconds;
				tv.tv_usec = 0;

				value = select(maximum + 1, (fd_set *)pfds0, NULL, (fd_set *)pfds2, &tv);
				switch (value)
				{
				case 0:
					while (i)
					{
						i--;

						psc = (PXYSOCKET_CONTEXT)scs[i];

						s = psc->s;
						type = psc->type;

						errorcode = procedure((LPVOID)ps, NULL, (LPVOID)psc, s, type, XYSOCKET_TIMEOUT, NULL, NULL, NULL, seconds);
						if (errorcode)
						{
							procedure((LPVOID)ps, NULL, (LPVOID)psc, s, type, XYSOCKET_CLOSE, NULL, NULL, NULL, errorcode);

							XYSocketRemove(pss, psc, NULL);
							closesocket(s);

							count--;
						}
					}
					break;
				case SOCKET_ERROR:
				default:
					while (i)
					{
						i--;

						psc = (PXYSOCKET_CONTEXT)scs[i];

						s = psc->s;
						type = psc->type;

						errorcode = 0;

						if (value != SOCKET_ERROR && XYFD_ISSET(s, (fd_set *)pfds0))
						{
							addresslength = 0;

							pointer = (LPVOID *)buffer;

							length = procedure((LPVOID)ps, &pointer, (LPVOID)psc, s, type, XYSOCKET_RECV, (SOCKADDR *)&sai6, &addresslength, buffer, buffersize);
							if (pointer != NULL)
							{
								length = recv(s, buffer, buffersize, 0);
							}

							if (length > 0)
							{
								if (pointer != NULL)
								{
									errorcode = procedure((LPVOID)ps, NULL, (LPVOID)psc, s, type, XYSOCKET_RECV, (SOCKADDR *)&sai6, &addresslength, buffer, length);
								}
							}
							else
							{
								if (length == 0)
								{
									errorcode = XYSOCKET_ERROR_FAILED;
								}
								else
								{
									// length == SOCKET_ERROR

									errorcode = WSAGetLastError();
									if (errorcode == WSAEWOULDBLOCK || errorcode == WSAEINTR)
									{
										errorcode = 0;
									}
								}
							}
						}
						else
						{
							if (XYFD_ISSET(s, (fd_set *)pfds2))
							{
								errorcode = XYSOCKET_ERROR_FAILED;
							}
						}

						if (errorcode)
						{
							procedure((LPVOID)ps, NULL, (LPVOID)psc, s, type, XYSOCKET_CLOSE, NULL, NULL, NULL, errorcode);

							XYSocketRemove(pss, psc, NULL);
							closesocket(s);

							count--;
						}
					}
					break;
				}
			}

			if (count)
			{
				SetEvent(pss->hevent);
			}
		}

		XYSocketsClose(ps, pss, scs);
	}

	if (scs != NULL)
	{
		FREE(scs);
	}
	if (pfds0 != NULL)
	{
		FREE(pfds0);
	}
	if (pfds2 != NULL)
	{
		FREE(pfds2);
	}

	return(0);
}

// private
SOCKET WINAPI XYUDPSocket(int type, const SOCKADDR *psa, int addresslength)
{
	DWORD buffer0[10];
	DWORD buffer1 = 1;
	DWORD size0;
	DWORD size1;
	DWORD numberofbytes = 0;
	BOOL flag = TRUE;
	SOCKET s;
	int sockettype;
	int protocol;

	switch (type)
	{
	case XYSOCKET_TYPE_RAW:
		sockettype = SOCK_RAW;
		protocol = IPPROTO_IP;
		break;
	case XYSOCKET_TYPE_UDP:
		sockettype = SOCK_DGRAM;
		protocol = IPPROTO_UDP;
		break;
	default:
		break;
	}

	s = socket(psa->sa_family, sockettype, protocol);
	if (s != INVALID_SOCKET)
	{
		if (psa != NULL)
		{
			flag = bind(s, (const SOCKADDR *)psa, addresslength) != SOCKET_ERROR;
			if (flag)
			{
				if (sockettype == SOCK_RAW)
				{
					size0 = sizeof(buffer0);

					buffer1 = 1;
					size1 = sizeof(DWORD);
					flag = WSAIoctl(s, SIO_RCVALL, &buffer1, size1, buffer0, size0, &numberofbytes, NULL, NULL) == 0;
				}
			}
		}
		if (!flag)
		{
			closesocket(s);
			s = INVALID_SOCKET;
		}
	}

	return(s);
}
SOCKET WINAPI XYUDPBind(PXYSOCKET ps, int type, LPVOID *pointer, LPVOID context, const SOCKADDR *psa, int addresslength)
{
	SOCKET s = INVALID_SOCKET;

	if (ps->boxes[XYSOCKET_THREAD_UDP] != NULL)
	{
		s = XYUDPSocket(type, psa, addresslength);
		if (s != INVALID_SOCKET)
		{
			if (!XYSocketAdd(ps->boxes[XYSOCKET_THREAD_UDP], pointer, context, s, type))
			{
				closesocket(s);
				s = INVALID_SOCKET;
			}
		}
	}

	return(s);
}
int WINAPI XYUDPSendTo(SOCKET s, const SOCKADDR *psa, int addresslength, const char *buffer, int length)
{
	return(sendto(s, buffer, length, 0, psa, addresslength));
}

SOCKET WINAPI XYTCPConnect(PXYSOCKET ps, LPVOID context, const SOCKADDR *psa, int addresslength, int sendbuffersize)
{
	SOCKET s = INVALID_SOCKET;
	u_long argp = 1;
	int optval;
	BOOL flag = FALSE;

	if (ps->boxes[XYSOCKET_THREAD_CONNECT] != NULL)
	{
		s = socket(psa->sa_family, SOCK_STREAM, 0);
		if (s != INVALID_SOCKET)
		{
			optval = 19000;

			//setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&optval, sizeof(int));
			//setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&optval, sizeof(int));

			LINGER l;
			l.l_onoff = 1;
			l.l_linger = 0;
			setsockopt(s, SOL_SOCKET, SO_LINGER, (char *)&l, sizeof(l));

			//
#ifdef XYTCP_NODELAY
			optval = 1;
			setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&optval, sizeof(int));
#endif
			//

			if (sendbuffersize >= 0)
			{
				//optval = 256 * 1024;
				optval = sendbuffersize;
				setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char*)&optval, sizeof(int));
			}

			if (ioctlsocket(s, FIONBIO, (u_long *)&argp) == 0 && connect(s, psa, addresslength) == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
			{
				flag = XYSocketAdd(ps->boxes[XYSOCKET_THREAD_CONNECT], NULL, context, s, XYSOCKET_TYPE_TCP0);
			}

			if (!flag)
			{
				closesocket(s);
				s = INVALID_SOCKET;
			}
		}
	}
	return(s);
}
SOCKET WINAPI XYTCPListen(PXYSOCKET ps, LPVOID *pointer, LPVOID context, const SOCKADDR *psa, int addresslength)
{
	SOCKET s = INVALID_SOCKET;
	u_long argp = 1;
	BOOL flag = FALSE;

	if (ps->boxes[XYSOCKET_THREAD_LISTEN] != NULL)
	{
		s = socket(psa->sa_family, SOCK_STREAM, 0);
		if (s != INVALID_SOCKET)
		{
			if (ioctlsocket(s, FIONBIO, (u_long *)&argp) == 0 && bind(s, (const SOCKADDR *)psa, addresslength) == 0 && listen(s, SOMAXCONN) == 0)
			{
				flag = XYSocketAdd(ps->boxes[XYSOCKET_THREAD_LISTEN], pointer, context, s, XYSOCKET_TYPE_TCP);
			}
			if (!flag)
			{
				closesocket(s);
				s = INVALID_SOCKET;
			}
		}
	}
	return(s);
}
int WINAPI XYTCPSend(SOCKET s, const char *buffer, int length, UINT seconds)
{
	int result;
	int offset = 0;
	FD_SET fds1;
	struct timeval tv;
	struct timeval *lptv;
	int errorcode;

	if (seconds == 0)
	{
		lptv = NULL;
	}
	else
	{
		tv.tv_sec = seconds;
		tv.tv_usec = 0;

		lptv = &tv;
	}

	while (offset < length)
	{
		FD_ZERO(&fds1);
		FD_SET(s, &fds1);

		//TCHAR debugtext[256];
		//DWORD tickcount0, tickcount1;

		//tickcount0 = GetTickCount();

		switch (select(s + 1, NULL, &fds1, NULL, lptv))
		{
		case 0:
			//tickcount1 = GetTickCount();
			//wsprintf(debugtext, _T("time out cast %d, %d"), tickcount1 - tickcount0, pfs->p_WSAGetLastError());
			//OutputDebugString(debugtext);
			//
			//errorcode = WSAGetLastError();
			//if (errorcode != 0 && errorcode != WSAEWOULDBLOCK && errorcode != WSAETIMEDOUT)
			//{
			//	length = 0;
			//}
			break;
		case SOCKET_ERROR:
			length = 0;
			break;
		default:
			//tickcount1 = GetTickCount();
			//wsprintf(debugtext, _T("isset cast %d"), tickcount1 - tickcount0);
			//OutputDebugString(debugtext);

			if (XYFD_ISSET(s, &fds1))
			{
				//tickcount0 = GetTickCount();

				result = send(s, buffer + offset, length - offset, 0);
				if (result > 0 && result != SOCKET_ERROR)
				{
					offset += result;

					//tickcount1 = GetTickCount();

					//wsprintf(debugtext, _T("send success cast %d, length = %d"), tickcount1 - tickcount0, result);
					//OutputDebugString(debugtext);
				}
				else
				{
					//tickcount1 = GetTickCount();

					errorcode = WSAGetLastError();
					//wsprintf(debugtext, _T("send error cast %d, errorcode = %d"), tickcount1 - tickcount0, errorcode);
					//OutputDebugString(debugtext);
					if (errorcode != WSAEWOULDBLOCK && errorcode != WSAETIMEDOUT)
					{
						length = 0;
					}
					else
					{
						Sleep(1);
					}
				}
			}
			break;
		}
	}
	return(offset);
}

BOOL WINAPI XYSocketsStartup(PXYSOCKET ps, LPVOID parameter0, LPVOID parameter1, XYSOCKET_PROCEDURE procedure)
{
	UINT i;
	BOOL result = FALSE;

	ps->parameter0 = parameter0;
	ps->parameter1 = parameter1;

	for (i = 0; i < 5; i++)
	{
		ps->boxes[i] = NULL;
	}

	ps->working = TRUE;
	InitializeCriticalSection(&ps->cs);

	ps->procedure = procedure;

	result = TRUE;

	return(result);
}

VOID WINAPI XYSocketsCleanup(PXYSOCKET ps)
{	
	XYSocketsClear(ps);

	DeleteCriticalSection(&ps->cs);
}
//---------------------------------------------------------------------------
