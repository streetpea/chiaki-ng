// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/stoppipe.h>
#include <chiaki/sock.h>

#include <fcntl.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#endif

CHIAKI_EXPORT ChiakiErrorCode chiaki_stop_pipe_init(ChiakiStopPipe *stop_pipe)
{
#ifdef _WIN32
	stop_pipe->event = WSACreateEvent();
	if(stop_pipe->event == WSA_INVALID_EVENT)
		return CHIAKI_ERR_UNKNOWN;
#elif defined(__SWITCH__)
	// currently pipe or socketpare are not available on switch
	// use a custom udp socket as pipe

	// struct sockaddr_in addr;
	int addr_size = sizeof(stop_pipe->addr);

	stop_pipe->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(stop_pipe->fd < 0)
		return CHIAKI_ERR_UNKNOWN;
	stop_pipe->addr.sin_family = AF_INET;
	// bind to localhost
	stop_pipe->addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	// use a random port (dedicate one socket per object)
	stop_pipe->addr.sin_port = htons(0);
	// bind on localhost
	bind(stop_pipe->fd, (struct sockaddr *) &stop_pipe->addr, addr_size);
	// listen
	getsockname(stop_pipe->fd, (struct sockaddr *) &stop_pipe->addr, &addr_size);
	int r = fcntl(stop_pipe->fd, F_SETFL, O_NONBLOCK);
	if(r == -1)
	{
		close(stop_pipe->fd);
		return CHIAKI_ERR_UNKNOWN;
	}
#else
	int r = pipe(stop_pipe->fds);
	if(r < 0)
		return CHIAKI_ERR_UNKNOWN;
	r = fcntl(stop_pipe->fds[0], F_SETFL, O_NONBLOCK);
	if(r == -1)
	{
		close(stop_pipe->fds[0]);
		close(stop_pipe->fds[1]);
		return CHIAKI_ERR_UNKNOWN;
	}
#endif
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT void chiaki_stop_pipe_fini(ChiakiStopPipe *stop_pipe)
{
#ifdef _WIN32
	WSACloseEvent(stop_pipe->event);
#elif defined(__SWITCH__)
	close(stop_pipe->fd);
#else
	close(stop_pipe->fds[0]);
	close(stop_pipe->fds[1]);
#endif
}

CHIAKI_EXPORT void chiaki_stop_pipe_stop(ChiakiStopPipe *stop_pipe)
{
#ifdef _WIN32
	WSASetEvent(stop_pipe->event);
#elif defined(__SWITCH__)
	// send to local socket (FIXME MSG_CONFIRM)
	sendto(stop_pipe->fd, "\x00", 1, 0,
		(struct sockaddr*)&stop_pipe->addr, sizeof(struct sockaddr_in));
#else
	write(stop_pipe->fds[1], "\x00", 1);
#endif
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_stop_pipe_select_single(ChiakiStopPipe *stop_pipe, chiaki_socket_t fd, bool write, uint64_t timeout_ms)
{
#ifdef _WIN32
	WSAEVENT events[2];
	DWORD events_count = 1;
	events[0] = stop_pipe->event;

	if(!CHIAKI_SOCKET_IS_INVALID(fd))
	{
		events_count = 2;
		events[1] = WSACreateEvent();
		if(events[1] == WSA_INVALID_EVENT)
			return CHIAKI_ERR_UNKNOWN;
		WSAEventSelect(fd, events[1], write ? FD_WRITE : FD_READ);
	}

	DWORD r = WSAWaitForMultipleEvents(events_count, events, FALSE, timeout_ms == UINT64_MAX ? WSA_INFINITE : (DWORD)timeout_ms, FALSE);

	if(events_count == 2)
		WSACloseEvent(events[1]);

	switch(r)
	{
		case WSA_WAIT_EVENT_0:
			return CHIAKI_ERR_CANCELED;
		case WSA_WAIT_EVENT_0+1:
			return CHIAKI_ERR_SUCCESS;
		case WSA_WAIT_TIMEOUT:
			return CHIAKI_ERR_TIMEOUT;
		default:
			return CHIAKI_ERR_UNKNOWN;
	}
#else
	fd_set rfds;
	FD_ZERO(&rfds);
#if defined(__SWITCH__)
	// push udp local socket as fd
	int stop_fd = stop_pipe->fd;
#else
	int stop_fd = stop_pipe->fds[0];
#endif
	FD_SET(stop_fd, &rfds);
	int nfds = stop_fd;

	fd_set wfds;
	FD_ZERO(&wfds);
	if(!CHIAKI_SOCKET_IS_INVALID(fd))
	{
		FD_SET(fd, write ? &wfds : &rfds);
		if(fd > nfds)
			nfds = fd;
	}
	nfds++;

	struct timeval timeout_s;
	struct timeval *timeout = NULL;
	if(timeout_ms != UINT64_MAX)
	{
		timeout_s.tv_sec = timeout_ms / 1000;
		timeout_s.tv_usec = (timeout_ms % 1000) * 1000;
		timeout = &timeout_s;
	}

	int r;
	do
	{
		r = select(nfds, &rfds, write ? &wfds : NULL, NULL, timeout);
	} while(r < 0 && errno == EINTR);

	if(r < 0)
		return CHIAKI_ERR_UNKNOWN;

	if(FD_ISSET(stop_fd, &rfds))
		return CHIAKI_ERR_CANCELED;

	if(!CHIAKI_SOCKET_IS_INVALID(fd) && FD_ISSET(fd, write ? &wfds : &rfds))
		return CHIAKI_ERR_SUCCESS;

	return CHIAKI_ERR_TIMEOUT;
#endif
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_stop_pipe_connect(ChiakiStopPipe *stop_pipe, chiaki_socket_t fd, struct sockaddr *addr, size_t addrlen)
{
	int r = connect(fd, addr, (socklen_t)addrlen);
	if(r >= 0)
		return CHIAKI_ERR_SUCCESS;

	if(CHIAKI_SOCKET_EINPROGRESS)
	{
		ChiakiErrorCode err = chiaki_stop_pipe_select_single(stop_pipe, fd, true, UINT64_MAX);
		if(err != CHIAKI_ERR_SUCCESS)
			return err;
	}
	else
	{
#ifdef _WIN32
		int err = WSAGetLastError();
		if(err == WSAECONNREFUSED)
			return CHIAKI_ERR_CONNECTION_REFUSED;
		else
			return CHIAKI_ERR_NETWORK;
#else
		if(errno == ECONNREFUSED)
			return CHIAKI_ERR_CONNECTION_REFUSED;
		else
			return CHIAKI_ERR_NETWORK;
#endif
	}

	struct sockaddr peer;
	socklen_t peerlen = sizeof(peer);
	if(getpeername(fd, &peer, &peerlen) == 0)
		return CHIAKI_ERR_SUCCESS;

	if(errno != ENOTCONN)
		return CHIAKI_ERR_UNKNOWN;

#ifdef _WIN32
	DWORD sockerr;
#else
	int sockerr;
#endif
	socklen_t sockerr_sz = sizeof(sockerr);
	if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockerr, &sockerr_sz) < 0)
		return CHIAKI_ERR_UNKNOWN;

#ifdef _WIN32
	switch(sockerr)
	{
		case WSAETIMEDOUT:
			return CHIAKI_ERR_TIMEOUT;
		case WSAECONNREFUSED:
			return CHIAKI_ERR_CONNECTION_REFUSED;
		case WSAEHOSTDOWN:
			return CHIAKI_ERR_HOST_DOWN;
		case WSAEHOSTUNREACH:
			return CHIAKI_ERR_HOST_UNREACH;
		default:
			return CHIAKI_ERR_UNKNOWN;
	}
#else
	switch(sockerr)
	{
		case ETIMEDOUT:
			return CHIAKI_ERR_TIMEOUT;
		case ECONNREFUSED:
			return CHIAKI_ERR_CONNECTION_REFUSED;
		case EHOSTDOWN:
			return CHIAKI_ERR_HOST_DOWN;
		case EHOSTUNREACH:
			return CHIAKI_ERR_HOST_UNREACH;
		default:
			return CHIAKI_ERR_UNKNOWN;
	}
#endif
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_stop_pipe_reset(ChiakiStopPipe *stop_pipe)
{
#ifdef _WIN32
	BOOL r = WSAResetEvent(stop_pipe->event);
	return r ? CHIAKI_ERR_SUCCESS : CHIAKI_ERR_UNKNOWN;
#elif defined(__SWITCH__)
	//FIXME
	uint8_t v;
	int r;
	while((r = read(stop_pipe->fd, &v, sizeof(v))) > 0);
	return r < 0 ? CHIAKI_ERR_UNKNOWN : CHIAKI_ERR_SUCCESS;
#else
	uint8_t v;
	int r;
	while((r = read(stop_pipe->fds[0], &v, sizeof(v))) > 0);
	return r < 0 ? CHIAKI_ERR_UNKNOWN : CHIAKI_ERR_SUCCESS;
#endif
}
