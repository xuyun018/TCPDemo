#include "kakapo.h"
//---------------------------------------------------------------------------
typedef struct win_fd_set {
	u_int fd_count;					/* how many are SET? */
	SOCKET fd_array[1];				/* an array of SOCKETs */
} win_fd_set;
//---------------------------------------------------------------------------
#define XYFD_ISSET(fd, set)													__WSAFDIsSet(fd, set)

#define XYFD_SET(fd, set, capacity) do { \
		if (((struct fd_set *)(set))->fd_count < capacity) { \
			((struct fd_set *)(set))->fd_array[((struct fd_set *)(set))->fd_count++] = (fd); \
		} \
	} while (0, 0)
//---------------------------------------------------------------------------
#ifndef XYTCP_NODELAY
#define XYTCP_NODELAY
#endif

#define KAKAPO_STATE_CONNECTED												0x01
#define KAKAPO_STATE_DISCONNECTED											0x02
#define KAKAPO_STATE_UNUSED													0x04
#define KAKAPO_STATE_SEND													0x08
// 调用者关闭
#define KAKAPO_STATE_CLOSE													0x10
//---------------------------------------------------------------------------
struct kakapo_thread_parameter
{
	struct kakapo *pkkp;

	struct kkp_box *pbox;

	HANDLE hevent;
};
//---------------------------------------------------------------------------
DWORD WINAPI kkp_datagram_proc(LPVOID parameter);
DWORD WINAPI kkp_listen_proc(LPVOID parameter);
DWORD WINAPI kkp_connect_proc(LPVOID parameter);
DWORD WINAPI kkp_receive_proc(LPVOID parameter);
//---------------------------------------------------------------------------
int kkp_add(struct kkp_box *pbox, void **pointer, void *context, kkp_fd fd, uint8_t type)
{
	struct kkp_ctx *pctx = NULL;
	uint16_t i;

	EnterCriticalSection(&pbox->cs);
	if (pbox->count < pbox->capacity)
	{
		pctx = &pbox->ctxs[pbox->count++];
	}
	else
	{
		for (i = 0; i < pbox->count; i++)
		{
			pctx = &pbox->ctxs[i];
			if (pctx->states&KAKAPO_STATE_UNUSED)
			{
				break;
			}
		}
		if (i == pbox->count)
		{
			pctx = NULL;
		}
	}
	if (pctx)
	{
		if (pointer)
		{
			*pointer = (void *)pctx;
		}

		pctx->context = context;
		pctx->fd = fd;
		pctx->states = 0;
		pctx->type = type;
		pctx->flags = 0;
		pctx->reserved = 0;
	}
	LeaveCriticalSection(&pbox->cs);

	if (pctx)
	{
		SetEvent(pbox->hevent);
	}

	return(pctx != NULL);
}
int kkp_remove(struct kkp_box *pbox, struct kkp_ctx *pctx, struct kkp_ctx *pret)
{
	uint16_t i;
	int result = 0;

	EnterCriticalSection(&pbox->cs);
	if (pbox->count)
	{
		if (pret)
		{
			memcpy(pret, pctx, sizeof(struct kkp_ctx));
		}

		//
		i = pctx - pbox->ctxs;

		if (i + 1 == pbox->count)
		{
			pbox->count--;
		}

		pctx->states = KAKAPO_STATE_UNUSED;

		result = 1;
		//
	}
	LeaveCriticalSection(&pbox->cs);

	return(result);
}

int kkp_launch_thread(struct kakapo *pkkp, uint8_t index, uint8_t capacity)
{
	struct kkp_box *pbox = NULL;
	struct kakapo_thread_parameter ptp[1];
	LPTHREAD_START_ROUTINE startaddress = NULL;

	if (pkkp->boxes[index] == NULL)
	{
		pbox = (struct kkp_box *)MALLOC(sizeof(struct kkp_box) + sizeof(struct kkp_ctx)* capacity);
		if (pbox)
		{
			ptp->hevent = CreateEventW(NULL, TRUE, FALSE, NULL);
			if (ptp->hevent)
			{
				ptp->pkkp = pkkp;
				ptp->pbox = pbox;

				//
				pbox->capacity = capacity;
				pbox->count = 0;

				InitializeCriticalSection(&pbox->cs);

				pbox->hevent = CreateEventW(NULL, TRUE, FALSE, NULL);

				switch (index)
				{
				case 0:
					startaddress = kkp_datagram_proc;
					break;
				case 1:
					startaddress = kkp_listen_proc;
					break;
				case 2:
					startaddress = kkp_connect_proc;
					break;
				case 3:
				case 4:
					startaddress = kkp_receive_proc;
					break;
				default:
					break;
				}
				//

				pbox->hthread = CreateThread(NULL, 0, startaddress, (LPVOID)ptp, 0, NULL);
				if (pbox->hthread)
				{
					WaitForSingleObject(ptp->hevent, INFINITE);

					pkkp->boxes[index] = pbox;
				}
				else
				{
					CloseHandle(pbox->hevent);

					DeleteCriticalSection(&pbox->cs);

					FREE((void *)pbox);
					pbox = NULL;
				}

				CloseHandle(ptp->hevent);
			}
		}
	}

	return(pbox != NULL);
}
uint16_t kkp_fds_close(struct kakapo *pkkp, struct kkp_box *pbox, void **ctxs)
{
	t_kakapo_procedure procedure;
	struct kkp_ctx *pctx;
	kkp_fd fd;
	uint16_t count;
	uint16_t i, j;
	uint8_t type;

	procedure = pkkp->procedure;

	j = 0;

	EnterCriticalSection(&pbox->cs);
	for (i = 0; i < pbox->count; i++)
	{
		pctx = &pbox->ctxs[i];
		if ((pctx->states&KAKAPO_STATE_UNUSED) == 0)
		{
			ctxs[j++] = (void *)pctx;
		}
	}
	LeaveCriticalSection(&pbox->cs);

	count = j;
	while (j)
	{
		j--;

		pctx = (struct kkp_ctx *)ctxs[j];

		fd = pctx->fd;
		type = pctx->type;

		procedure(pkkp, NULL, (void *)pctx, fd, type, KAKAPO_CLOSE, NULL, 0, NULL, 0);

		kkp_remove(pbox, pctx, NULL);
		closesocket(fd);
	}

	return(count);
}
unsigned int kkp_fds_load(struct kakapo *pkkp, struct kkp_box *pbox, void **ctxs, struct fd_set *fds0, struct fd_set *fds1, int *maximum)
{
	struct kkp_ctx *pctx;
	kkp_fd fd;
	uint16_t i;
	uint16_t count = 0;

	*maximum = 0;

	FD_ZERO(fds0);
	FD_ZERO(fds1);

	EnterCriticalSection(&pbox->cs);
	for (i = 0; i < pbox->count; i++)
	{
		pctx = &pbox->ctxs[i];
		if ((pctx->states&KAKAPO_STATE_UNUSED) == 0)
		{
			ctxs[count++] = (void *)pctx;

			fd = pctx->fd;

			*maximum = fd > (*maximum) ? fd : (*maximum);

			XYFD_SET(fd, fds0, pbox->capacity);
			XYFD_SET(fd, fds1, pbox->capacity);
		}
	}
	LeaveCriticalSection(&pbox->cs);

	return(count);
}

void kkp_fds_clear(struct kakapo *pkkp)
{
	struct kkp_box *pbox;
	uint16_t i;

	pkkp->working = 0;

	for (i = 0; i < sizeof(pkkp->boxes) / sizeof(pkkp->boxes[0]); i++)
	{
		pbox = pkkp->boxes[i];
		if (pbox)
		{
			pkkp->boxes[i] = NULL;

			if (pbox->hevent)
			{
				SetEvent(pbox->hevent);
			}
			if (pbox->hthread)
			{
				WaitForSingleObject(pbox->hthread, INFINITE);
				CloseHandle(pbox->hthread);
			}
			if (pbox->hevent)
			{
				CloseHandle(pbox->hevent);
			}
			//
			DeleteCriticalSection(&pbox->cs);

			FREE((LPVOID)pbox);
		}
	}
}

DWORD WINAPI kkp_datagram_proc(LPVOID parameter)
{
	struct kakapo_thread_parameter *ptp = (kakapo_thread_parameter *)parameter;
	t_kakapo_procedure procedure;
	struct kakapo *pkkp;
	struct kkp_box *pbox;
	struct kkp_ctx *pctx;
	win_fd_set *pfds0;
	win_fd_set *pfds2;
	struct timeval tv;
	kkp_fd fd;
	int maximum;
	void **ctxs;
	char buffer[8192];
	int length;
	struct sockaddr_in6 sai6;
	int sasize;
	int errorcode;
	int value;
	unsigned int i;
	int flag0;
	int flag1;
	uint16_t count;
	uint8_t type;

	//
	pkkp = ptp->pkkp;
	pbox = ptp->pbox;
	//

	//
	SetEvent(ptp->hevent);
	//

	procedure = pkkp->procedure;

	ctxs = (void **)MALLOC(sizeof(void *) * pbox->capacity);
	pfds0 = (win_fd_set *)MALLOC(sizeof(win_fd_set) + sizeof(SOCKET) * pbox->capacity);
	pfds2 = (win_fd_set *)MALLOC(sizeof(win_fd_set) + sizeof(SOCKET) * pbox->capacity);
	if (ctxs && pfds0 && pfds2)
	{
		while (pkkp->working)
		{
			WaitForSingleObject(pbox->hevent, INFINITE);
			ResetEvent(pbox->hevent);

			count = kkp_fds_load(pkkp, pbox, ctxs, (struct fd_set *)pfds0, (struct fd_set *)pfds2, &maximum);

			if (count > 0)
			{
				i = count;

				tv.tv_sec = 5;
				tv.tv_usec = 0;

				value = select(maximum + 1, (fd_set *)pfds0, NULL, (fd_set *)pfds2, &tv);
				switch (value)
				{
				case 0:
				case SOCKET_ERROR:
				default:
					while (i)
					{
						i--;

						pctx = (struct kkp_ctx *)ctxs[i];

						fd = pctx->fd;
						type = pctx->type;

						flag1 = 0;

						errorcode = 0;

						if (value == 0 || value != SOCKET_ERROR && XYFD_ISSET(fd, (fd_set *)pfds0))
						{
							do
							{
								flag0 = 0;

								length = 0;
								switch (type)
								{
								case KAKAPO_TYPE_RAW:
									sasize = 0;

									length = recv(fd, buffer, sizeof(buffer), 0);
									break;
								case KAKAPO_TYPE_UDP:
									procedure(pkkp, NULL, (void *)pctx, fd, type, KAKAPO_RECV, (void *)&sai6, &sasize, NULL, 0);

									length = recvfrom(fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&sai6, &sasize);
									break;
								default:
									break;
								}

								if (length > 0)
								{
									errorcode = procedure(pkkp, NULL, (void *)pctx, fd, type, KAKAPO_RECV, (SOCKADDR *)&sai6, &sasize, buffer, length);

									if (errorcode == 0)
									{
										flag0 = 1;

										flag1 = 1;
									}
								}
								else
								{
									if (length == 0)
									{
										errorcode = KAKAPO_ERROR_FAILED;
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
							} while (flag0);
						}
						else
						{
							if (XYFD_ISSET(fd, (fd_set *)pfds2))
							{
								errorcode = KAKAPO_ERROR_FAILED;
							}
						}

						if (errorcode == 0)
						{
							if (value == 0)
							{
								if (flag1 == 0)
								{
									errorcode = procedure(pkkp, NULL, (void *)pctx, fd, type, KAKAPO_TIMEOUT, NULL, NULL, NULL, 0);
								}
							}
						}

						if (errorcode)
						{
							procedure(pkkp, NULL, (void *)pctx, fd, type, KAKAPO_CLOSE, NULL, NULL, NULL, errorcode);

							kkp_remove(pbox, pctx, NULL);
							closesocket(fd);

							count--;
						}
					}
					break;
				}
			}

			if (count)
			{
				SetEvent(pbox->hevent);
			}
		}

		kkp_fds_close(pkkp, pbox, ctxs);
	}

	if (ctxs)
	{
		FREE(ctxs);
	}
	if (pfds0)
	{
		FREE(pfds0);
	}
	if (pfds2)
	{
		FREE(pfds2);
	}

	return(0);
}

DWORD WINAPI kkp_listen_proc(LPVOID parameter)
{
	struct kakapo_thread_parameter *ptp = (kakapo_thread_parameter *)parameter;
	t_kakapo_procedure procedure;
	struct kakapo *pkkp;
	struct kkp_box *pbox;
	struct kkp_ctx *pctx;
	struct kkp_box *pbox1;
	struct kkp_ctx ctx;
	win_fd_set *pfds0;
	win_fd_set *pfds2;
	struct timeval tv;
	int maximum;
	kkp_fd fd0;
	kkp_fd fd1;
	void **ctxs;
	unsigned int count;
	void **pointer;
	struct sockaddr_in6 sai6;
	int sasize;
	int errorcode;
	int value;
	unsigned int seconds;
	unsigned int i;
	int flag = 1;
	uint8_t type;

	//
	pkkp = ptp->pkkp;
	pbox = ptp->pbox;
	//

	//
	SetEvent(ptp->hevent);
	//

	procedure = pkkp->procedure;

	ctxs = (void **)MALLOC(sizeof(LPVOID) * pbox->capacity);
	pfds0 = (win_fd_set *)MALLOC(sizeof(win_fd_set)+sizeof(SOCKET)* pbox->capacity);
	pfds2 = (win_fd_set *)MALLOC(sizeof(win_fd_set)+sizeof(SOCKET)* pbox->capacity);
	if (ctxs && pfds0 && pfds2)
	{
		while (pkkp->working)
		{
			WaitForSingleObject(pbox->hevent, INFINITE);
			ResetEvent(pbox->hevent);

			seconds = 5;

			count = kkp_fds_load(pkkp, pbox, ctxs, (fd_set *)pfds0, (fd_set *)pfds2, &maximum);

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

						pctx = (struct kkp_ctx *)ctxs[i];

						fd0 = pctx->fd;
						type = pctx->type;

						//
						errorcode = procedure(pkkp, NULL, (void *)pctx, fd0, type, KAKAPO_TIMEOUT, NULL, NULL, NULL, 0);
						if (errorcode)
						{
							procedure(pkkp, NULL, (void *)pctx, fd0, type, KAKAPO_CLOSE, NULL, NULL, NULL, 0);

							kkp_remove(pbox, pctx, NULL);
							closesocket(fd0);

							count--;
						}
					}
					break;
				case SOCKET_ERROR:
				default:
					while (i)
					{
						i--;

						pctx = (struct kkp_ctx *)ctxs[i];

						fd0 = pctx->fd;

						if (value != SOCKET_ERROR && XYFD_ISSET(fd0, (fd_set *)pfds0))
						{
							// 这里是server
							type = KAKAPO_TYPE_TCP1;

							sasize = 0;

							procedure(pkkp, NULL, (void *)pctx, fd0, type, KAKAPO_CONNECT, (void *)&sai6, &sasize, NULL, KAKAPO_ERROR_ACCEPT);

							fd1 = accept(fd0, (struct sockaddr *)&sai6, &sasize);
							if (fd1 != INVALID_SOCKET)
							{
								ctx.context = pctx->context;
								ctx.fd = fd1;
								ctx.type = type;

								pointer = NULL;
								errorcode = procedure(pkkp, &pointer, (void *)&ctx, fd1, type, KAKAPO_CONNECT, (void *)&sai6, &sasize, NULL, KAKAPO_ERROR_ACCEPTED);

								u_long argp = 1;
								if (errorcode || ioctlsocket(fd1, FIONBIO, (u_long *)&argp) != 0 || 
									(pbox1 = pkkp->boxes[KAKAPO_THREAD_SERVER]) == NULL || 
									!kkp_add(pbox1, pointer, ctx.context, ctx.fd, ctx.type))
								{
									if (errorcode == 0)
									{
										// 这个错误有点特殊,是已经触发连接事件成功之后的,需要注意
										errorcode = KAKAPO_ERROR_OVERFLOW;
									}

									errorcode = procedure(pkkp, &pointer, (void *)&ctx, fd1, type, KAKAPO_CLOSE, NULL, NULL, NULL, errorcode);
									closesocket(fd1);
								}
							}
							else
							{
								errorcode = KAKAPO_ERROR_FAILED;
							}

							if (errorcode != 0)
							{
								// 这里是通知而不是关闭
								procedure(pkkp, NULL, (void *)pctx, fd0, type, KAKAPO_CONNECT, NULL, NULL, NULL, errorcode);
							}
						}
						else
						{
							if (XYFD_ISSET(fd0, (fd_set *)pfds2))
							{
								//
								type = pctx->type;

								procedure(pkkp, NULL, (void *)pctx, fd0, type, KAKAPO_CLOSE, NULL, NULL, NULL, KAKAPO_ERROR_FAILED);
								//

								kkp_remove(pbox, pctx, NULL);
								closesocket(fd0);

								count--;
							}
						}
					}
					break;
				}
			}

			if (count)
			{
				SetEvent(pbox->hevent);
			}
		}

		kkp_fds_close(pkkp, pbox, ctxs);
	}

	if (ctxs)
	{
		FREE(ctxs);
	}
	if (pfds0)
	{
		FREE(pfds0);
	}
	if (pfds2)
	{
		FREE(pfds2);
	}

	return(0);
}
DWORD WINAPI kkp_connect_proc(LPVOID parameter)
{
	struct kakapo_thread_parameter *ptp = (kakapo_thread_parameter *)parameter;
	t_kakapo_procedure procedure;
	struct kakapo *pkkp;
	struct kkp_box *pbox;
	struct kkp_ctx *pctx;
	struct kkp_box *pbox1;
	struct kkp_ctx ctx;
	win_fd_set *pfds1;
	win_fd_set *pfds2;
	struct timeval tv;
	int maximum;
	SOCKET fd;
	void **ctxs;
	unsigned int count;
	void **pointer;
	int error = 0;
	int optval;
	int optlen;
	int connected = 0;
	int errorcode0;
	int errorcode1;
	int value;
	unsigned int i;
	uint8_t type;

	//
	pkkp = ptp->pkkp;
	pbox = ptp->pbox;
	//

	//
	SetEvent(ptp->hevent);
	//

	procedure = pkkp->procedure;

	ctxs = (void **)MALLOC(sizeof(LPVOID) * pbox->capacity);
	pfds1 = (win_fd_set *)MALLOC(sizeof(win_fd_set)+sizeof(SOCKET)* pbox->capacity);
	pfds2 = (win_fd_set *)MALLOC(sizeof(win_fd_set)+sizeof(SOCKET)* pbox->capacity);
	if (ctxs && pfds1 && pfds2)
	{
		while (pkkp->working)
		{
			WaitForSingleObject(pbox->hevent, INFINITE);
			ResetEvent(pbox->hevent);

			count = kkp_fds_load(pkkp, pbox, ctxs, (struct fd_set *)pfds1, (struct fd_set *)pfds2, &maximum);

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

						pctx = (struct kkp_ctx *)ctxs[i];

						fd = pctx->fd;
						type = pctx->type;

						// 这里通过pointer参数是否为空来区分是不是超时
						errorcode1 = procedure(pkkp, NULL, (void *)pctx, fd, type, KAKAPO_CONNECT, NULL, NULL, NULL, 0);
						if (errorcode1)
						{
							// 这里不设计为调用CLOSE回调

							kkp_remove(pbox, pctx, NULL);
							closesocket(fd);

							count--;
						}
					}
					break;
				case SOCKET_ERROR:
				default:
					while (i)
					{
						i--;

						pctx = (struct kkp_ctx *)ctxs[i];

						fd = pctx->fd;
						type = pctx->type;

						// 这里特殊一点
						errorcode1 = 0;
						if (XYFD_ISSET(fd, (fd_set *)pfds2))
						{
							errorcode1 = KAKAPO_ERROR_FAILED;

							ctx.context = pctx->context;
							ctx.fd = fd;
							ctx.type = type;
						}
						else
						{
							if (value != SOCKET_ERROR && XYFD_ISSET(fd, (fd_set *)pfds1))
							{
								errorcode0 = 0;
								optlen = sizeof(int);
								if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&optval, &optlen) == 0)
								{
									errorcode0 = optval;
								}
								else
								{
									errorcode0 = KAKAPO_ERROR_REFUSED;
								}

								pointer = NULL;
								errorcode1 = procedure(pkkp, &pointer, (void *)pctx, fd, type, KAKAPO_CONNECT, NULL, NULL, NULL, errorcode0);

								kkp_remove(pbox, pctx, &ctx);
								count--;
								pctx = NULL;

								if (errorcode0)
								{
									if (errorcode1 == 0)
									{
										errorcode1 = errorcode0;
									}
								}

								if (errorcode1 || (pbox1 = pkkp->boxes[KAKAPO_THREAD_CLIENT]) == NULL || 
									!kkp_add(pbox1, pointer, ctx.context, ctx.fd, ctx.type))
								{
									if (errorcode1 == 0)
									{
										errorcode1 = KAKAPO_ERROR_OVERFLOW;
									}
									// 在下面关闭
								}
							}
						}

						if (errorcode1)
						{
							procedure(pkkp, NULL, (void *)&ctx, fd, type, KAKAPO_CONNECT, NULL, NULL, NULL, errorcode1);

							if (pctx != NULL)
							{
								kkp_remove(pbox, pctx, NULL);
								count--;
							}
							closesocket(fd);
						}
					}
					break;
				}
			}

			if (count)
			{
				SetEvent(pbox->hevent);
			}
		}

		kkp_fds_close(pkkp, pbox, ctxs);
	}

	if (ctxs)
	{
		FREE(ctxs);
	}
	if (pfds1)
	{
		FREE(pfds1);
	}
	if (pfds2)
	{
		FREE(pfds2);
	}

	return(error);
}
DWORD WINAPI kkp_receive_proc(LPVOID parameter)
{
	struct kakapo_thread_parameter *ptp = (kakapo_thread_parameter *)parameter;
	t_kakapo_procedure procedure;
	struct kakapo *pkkp;
	struct kkp_box *pbox;
	struct kkp_ctx *pctx;
	win_fd_set *pfds0;
	win_fd_set *pfds2;
	struct timeval tv;
	int maximum;
	SOCKET fd;
	void **ctxs;
	unsigned int count;
	void **pointer;
	char buffer[8192];
	int buffersize;
	int length;
	struct sockaddr_in6 sai6;
	int sasize;
	unsigned int seconds;
	int errorcode;
	int value;
	unsigned int i;
	int flag0;
	int flag1;
	uint8_t type;

	//
	pkkp = ptp->pkkp;
	pbox = ptp->pbox;

	//

	//
	SetEvent(ptp->hevent);
	//

	procedure = pkkp->procedure;

	buffersize = sizeof(buffer);

	ctxs = (void **)MALLOC(sizeof(void *) * pbox->capacity);
	pfds0 = (win_fd_set *)MALLOC(sizeof(win_fd_set) + sizeof(SOCKET) * pbox->capacity);
	pfds2 = (win_fd_set *)MALLOC(sizeof(win_fd_set) + sizeof(SOCKET) * pbox->capacity);
	if (ctxs && pfds0 && pfds2)
	{
		while (pkkp->working)
		{
			WaitForSingleObject(pbox->hevent, INFINITE);
			ResetEvent(pbox->hevent);

			seconds = 5;

			count = kkp_fds_load(pkkp, pbox, ctxs, (fd_set *)pfds0, (fd_set *)pfds2, &maximum);

			if (count > 0)
			{
				i = count;

				tv.tv_sec = seconds;
				tv.tv_usec = 0;

				value = select(maximum + 1, (fd_set *)pfds0, NULL, (fd_set *)pfds2, &tv);
				switch (value)
				{
				case 0:
				case SOCKET_ERROR:
				default:
					while (i)
					{
						i--;

						pctx = (struct kkp_ctx *)ctxs[i];

						fd = pctx->fd;
						type = pctx->type;

						flag1 = 0;

						errorcode = 0;

						// 超时也要接收数据, 没有想明白
						if (value == 0 || value != SOCKET_ERROR && XYFD_ISSET(fd, (fd_set *)pfds0))
						{
							do
							{
								flag0 = 0;

								sasize = 0;

								pointer = (void **)buffer;

								length = procedure(pkkp, &pointer, (void *)pctx, fd, type, KAKAPO_RECV, (void *)&sai6, &sasize, buffer, buffersize);
								if (pointer != NULL)
								{
									length = recv(fd, buffer, buffersize, 0);
								}

								if (length > 0)
								{
									if (pointer != NULL)
									{
										errorcode = procedure(pkkp, NULL, (void *)pctx, fd, type, KAKAPO_RECV, (void *)&sai6, &sasize, buffer, length);
									}

									if (errorcode == 0)
									{
										flag0 = 1;

										flag1 = 1;
									}
								}
								else
								{
									if (length == 0)
									{
										errorcode = KAKAPO_ERROR_FAILED;
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
							} while (flag0);
						}
						else
						{
							if (XYFD_ISSET(fd, (fd_set *)pfds2))
							{
								errorcode = KAKAPO_ERROR_FAILED;
							}
						}

						if (errorcode == 0)
						{
							if (value == 0)
							{
								if (flag1 == 0)
								{
									errorcode = procedure(pkkp, NULL, (void *)pctx, fd, type, KAKAPO_TIMEOUT, NULL, NULL, NULL, seconds);
								}
							}
						}

						if (errorcode)
						{
							procedure(pkkp, NULL, (void *)pctx, fd, type, KAKAPO_CLOSE, NULL, NULL, NULL, errorcode);

							kkp_remove(pbox, pctx, NULL);
							closesocket(fd);

							count--;
						}
					}
					break;
				}
			}

			if (count)
			{
				SetEvent(pbox->hevent);
			}
		}

		kkp_fds_close(pkkp, pbox, ctxs);
	}

	if (ctxs)
	{
		FREE(ctxs);
	}
	if (pfds0)
	{
		FREE(pfds0);
	}
	if (pfds2)
	{
		FREE(pfds2);
	}

	return(0);
}

// private
kkp_fd kkp_udp_socket(int type, const void *psa, int sasize)
{
	const struct sockaddr *_psa = (const struct sockaddr *)psa;
	DWORD buffer0[10];
	DWORD buffer1 = 1;
	DWORD size0;
	DWORD size1;
	DWORD numberofbytes = 0;
	BOOL flag = TRUE;
	SOCKET fd;
	int sockettype;
	int protocol;
	u_long argp = 1;

	switch (type)
	{
	case KAKAPO_TYPE_RAW:
		sockettype = SOCK_RAW;
		protocol = IPPROTO_IP;
		break;
	case KAKAPO_TYPE_UDP:
		sockettype = SOCK_DGRAM;
		protocol = IPPROTO_UDP;
		break;
	default:
		break;
	}

	fd = socket(_psa->sa_family, sockettype, protocol);
	if (fd != INVALID_SOCKET)
	{
		if (psa != NULL)
		{
			flag = ioctlsocket(fd, FIONBIO, (u_long *)&argp) == 0 && 
				bind(fd, (const struct sockaddr *)psa, sasize) != SOCKET_ERROR;
			if (flag)
			{
				if (sockettype == SOCK_RAW)
				{
					size0 = sizeof(buffer0);

					buffer1 = 1;
					size1 = sizeof(DWORD);
					flag = WSAIoctl(fd, SIO_RCVALL, &buffer1, size1, buffer0, size0, &numberofbytes, NULL, NULL) == 0;
				}
			}
		}
		if (!flag)
		{
			closesocket(fd);
			fd = INVALID_SOCKET;
		}
	}

	return(fd);
}
kkp_fd kkp_udp_bind(struct kakapo *pkkp, int type, void **pointer, void *context, const void *psa, int sasize)
{
	struct kkp_box *pbox;
	SOCKET fd = INVALID_SOCKET;

	if (pbox = pkkp->boxes[KAKAPO_THREAD_UDP])
	{
		fd = kkp_udp_socket(type, psa, sasize);
		if (fd != INVALID_SOCKET)
		{
			if (!kkp_add(pbox, pointer, context, fd, type))
			{
				closesocket(fd);
				fd = INVALID_SOCKET;
			}
		}
	}

	return((kkp_fd)fd);
}
int kkp_sendto(kkp_fd fd, const void *psa, int sasize, const char *buffer, int length)
{
	return(sendto(fd, buffer, length, 0, (const struct sockaddr *)psa, sasize));
}

kkp_fd kkp_connect(struct kakapo *pkkp, void *context, const void *psa, int sasize, int sendbuffersize)
{
	const struct sockaddr *_psa = (const struct sockaddr *)psa;
	struct kkp_box *pbox;
	SOCKET fd = INVALID_SOCKET;
	u_long argp = 1;
	int optval;
	BOOL flag = FALSE;

	if (pbox = pkkp->boxes[KAKAPO_THREAD_CONNECT])
	{
		fd = socket(_psa->sa_family, SOCK_STREAM, 0);
		if (fd != INVALID_SOCKET)
		{
			optval = 19000;

			//setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&optval, sizeof(int));
			//setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&optval, sizeof(int));

			//
#ifdef XYTCP_NODELAY
			optval = 1;
			setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&optval, sizeof(int));
#endif
			//

			if (sendbuffersize >= 0)
			{
				//optval = 256 * 1024;
				optval = sendbuffersize;
				setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const char*)&optval, sizeof(int));
			}

			if (ioctlsocket(fd, FIONBIO, (u_long *)&argp) == 0 && 
				connect(fd, (const struct sockaddr *)psa, sasize) == SOCKET_ERROR && 
				WSAGetLastError() == WSAEWOULDBLOCK)
			{
				flag = kkp_add(pbox, NULL, context, fd, KAKAPO_TYPE_TCP0);
			}

			if (!flag)
			{
				closesocket(fd);
				fd = INVALID_SOCKET;
			}
		}
	}
	return(fd);
}
kkp_fd kkp_listen(struct kakapo *pkkp, void **pointer, void *context, const void *psa, int sasize)
{
	const struct sockaddr *_psa = (const struct sockaddr *)psa;
	struct kkp_box *pbox;
	SOCKET fd = INVALID_SOCKET;
	u_long argp = 1;
	BOOL flag = FALSE;

	if (pbox = pkkp->boxes[KAKAPO_THREAD_LISTEN])
	{
		fd = socket(_psa->sa_family, SOCK_STREAM, 0);
		if (fd != INVALID_SOCKET)
		{
			if (ioctlsocket(fd, FIONBIO, (u_long *)&argp) == 0 && 
				bind(fd, (const struct sockaddr *)psa, sasize) == 0 && 
				listen(fd, SOMAXCONN) == 0)
			{
				flag = kkp_add(pbox, pointer, context, fd, KAKAPO_TYPE_TCP);
			}
			if (!flag)
			{
				closesocket(fd);
				fd = INVALID_SOCKET;
			}
		}
	}
	return(fd);
}
int kkp_send(kkp_fd fd, const char *buffer, int length, unsigned int seconds)
{
	int result;
	int offset = 0;
	FD_SET fds1;
	struct timeval tv;
	struct timeval *ptv;
	int errorcode;

	if (seconds == 0)
	{
		ptv = NULL;
	}
	else
	{
		tv.tv_sec = seconds;
		tv.tv_usec = 0;

		ptv = &tv;
	}

	while (offset < length)
	{
		FD_ZERO(&fds1);
		FD_SET(fd, &fds1);

		//TCHAR debugtext[256];
		//DWORD tickcount0, tickcount1;

		//tickcount0 = GetTickCount();

		switch (select(fd + 1, NULL, &fds1, NULL, ptv))
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

			if (XYFD_ISSET(fd, &fds1))
			{
				//tickcount0 = GetTickCount();

				result = send(fd, buffer + offset, length - offset, 0);
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
				}
			}
			break;
		}
	}
	return(offset);
}

int kkp_initialize(struct kakapo *pkkp, void *parameter0, void *parameter1, t_kakapo_procedure procedure)
{
	unsigned int i;
	int result = 0;

	pkkp->parameter0 = parameter0;
	pkkp->parameter1 = parameter1;

	for (i = 0; i < sizeof(pkkp->boxes) / sizeof(pkkp->boxes[0]); i++)
	{
		pkkp->boxes[i] = NULL;
	}

	pkkp->working = 1;
	InitializeCriticalSection(&pkkp->cs);

	pkkp->procedure = procedure;

	result = 1;

	return(result);
}
void kkp_uninitialize(struct kakapo *pkkp)
{	
	kkp_fds_clear(pkkp);

	DeleteCriticalSection(&pkkp->cs);
}
//---------------------------------------------------------------------------
