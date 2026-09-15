#include "chiaki_wasm_net.h"
#include "chiaki_wasm_protocol.h"

#include <emscripten/websocket.h>
#include <emscripten/threading.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

ssize_t __wrap_send(int fd, const void *buf, size_t len, int flags);
ssize_t __wrap_recv(int fd, void *buf, size_t len, int flags);
ssize_t __wrap_write(int fd, const void *buf, size_t count);
ssize_t __wrap_read(int fd, void *buf, size_t count);

#define FD_BASE 64
#define FD_MAX 64
#define PKT_QUEUE_MAX 2048
#define PIPE_BUF_SIZE 1024
#define PENDING_MAX 32

#if defined(FD_SETSIZE) && (FD_BASE + FD_MAX > FD_SETSIZE)
#error "Virtual POSIX fds must stay below FD_SETSIZE so select()/FD_SET work."
#endif

extern int __real_close(int fd);
extern ssize_t __real_read(int fd, void *buf, size_t count);
extern ssize_t __real_write(int fd, const void *buf, size_t count);
extern int __real_fcntl(int fd, int cmd, ...);
extern int __real_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);

typedef enum {
	FD_FREE = 0,
	FD_UDP,
	FD_TCP,
	FD_PIPE_R,
	FD_PIPE_W
} FdKind;

typedef struct NetPkt {
	uint8_t *data;
	size_t len;
	uint32_t addr;
	uint16_t port;
	struct NetPkt *next;
} NetPkt;

typedef struct {
	FdKind kind;
	int fd;
	int nonblock;
	int connected;
	int closed;
	int broadcast;
	uint32_t peer_addr;
	uint16_t peer_port;
	int pipe_peer;
	uint8_t pipe_buf[PIPE_BUF_SIZE];
	size_t pipe_r;
	size_t pipe_w;
	size_t pipe_used;
	NetPkt *q_head;
	NetPkt *q_tail;
	int q_count;
} NetFd;

typedef struct {
	uint32_t id;
	int in_use;
	int done;
	int result;
	int err;
	int extra;
	uint8_t *payload;
	uint32_t payload_len;
} Pending;

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static NetFd g_fds[FD_MAX];
static Pending g_pending[PENDING_MAX];
static EMSCRIPTEN_WEBSOCKET_T g_ws = 0;
static int g_ws_open = 0;
static int g_ws_failed = 0;
static uint32_t g_next_id = 1;
static int g_next_fd = 0;
static char g_proxy_url[512];

static int is_ours(int fd)
{
	return fd >= FD_BASE && fd < FD_BASE + FD_MAX;
}

static NetFd *slot(int fd)
{
	if(!is_ours(fd))
		return NULL;
	NetFd *s = &g_fds[fd - FD_BASE];
	if(s->kind == FD_FREE)
		return NULL;
	return s;
}

static uint32_t alloc_id_locked(void)
{
	uint32_t id = g_next_id++;
	if(id == 0)
		id = g_next_id++;
	return id;
}

static int alloc_fd_locked(FdKind kind)
{
	for(int i = 0; i < FD_MAX; i++)
	{
		int idx = (g_next_fd + i) % FD_MAX;
		if(g_fds[idx].kind == FD_FREE)
		{
			memset(&g_fds[idx], 0, sizeof(g_fds[idx]));
			g_fds[idx].kind = kind;
			g_fds[idx].fd = FD_BASE + idx;
			g_next_fd = (idx + 1) % FD_MAX;
			return g_fds[idx].fd;
		}
	}
	return -1;
}

static void pkt_free_all(NetFd *s)
{
	NetPkt *p = s->q_head;
	while(p)
	{
		NetPkt *n = p->next;
		free(p->data);
		free(p);
		p = n;
	}
	s->q_head = s->q_tail = NULL;
	s->q_count = 0;
}

static void enqueue_pkt(NetFd *s, const uint8_t *data, size_t len, uint32_t addr, uint16_t port)
{
	while(s->q_count >= PKT_QUEUE_MAX && s->q_head)
	{
		NetPkt *old = s->q_head;
		s->q_head = old->next;
		if(!s->q_head)
			s->q_tail = NULL;
		s->q_count--;
		free(old->data);
		free(old);
	}
	NetPkt *p = calloc(1, sizeof(NetPkt));
	if(!p)
		return;
	p->data = malloc(len);
	if(!p->data)
	{
		free(p);
		return;
	}
	memcpy(p->data, data, len);
	p->len = len;
	p->addr = addr;
	p->port = port;
	if(s->q_tail)
		s->q_tail->next = p;
	else
		s->q_head = p;
	s->q_tail = p;
	s->q_count++;
}

static void hdr_write(uint8_t *buf, uint8_t type, uint32_t id, int32_t fd, int32_t a, int32_t b, int32_t c, uint32_t len)
{
	memset(buf, 0, CHIAKI_NET_HDR_SIZE);
	buf[0] = type;
	memcpy(buf + 4, &id, 4);
	memcpy(buf + 8, &fd, 4);
	memcpy(buf + 12, &a, 4);
	memcpy(buf + 16, &b, 4);
	memcpy(buf + 20, &c, 4);
	memcpy(buf + 24, &len, 4);
}

static int ws_send_locked(const uint8_t *buf, size_t len)
{
	if(!g_ws_open || !g_ws)
	{
		errno = ENOTCONN;
		return -1;
	}
	EMSCRIPTEN_RESULT r = emscripten_websocket_send_binary(g_ws, (void *)buf, (uint32_t)len);
	if(r != EMSCRIPTEN_RESULT_SUCCESS)
	{
		errno = EIO;
		return -1;
	}
	return 0;
}

static int send_msg(uint8_t type, uint32_t id, int32_t fd, int32_t a, int32_t b, int32_t c, const void *payload, uint32_t len)
{
	uint8_t *buf = malloc(CHIAKI_NET_HDR_SIZE + len);
	if(!buf)
	{
		errno = ENOMEM;
		return -1;
	}
	hdr_write(buf, type, id, fd, a, b, c, len);
	if(len && payload)
		memcpy(buf + CHIAKI_NET_HDR_SIZE, payload, len);
	pthread_mutex_lock(&g_mu);
	int r = ws_send_locked(buf, CHIAKI_NET_HDR_SIZE + len);
	pthread_mutex_unlock(&g_mu);
	free(buf);
	return r;
}

static Pending *pending_alloc_locked(uint32_t id)
{
	for(int i = 0; i < PENDING_MAX; i++)
	{
		if(!g_pending[i].in_use)
		{
			memset(&g_pending[i], 0, sizeof(g_pending[i]));
			g_pending[i].in_use = 1;
			g_pending[i].id = id;
			return &g_pending[i];
		}
	}
	return NULL;
}

static int wait_pending(Pending *p, int timeout_ms)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += timeout_ms / 1000;
	ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
	if(ts.tv_nsec >= 1000000000L)
	{
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000L;
	}
	pthread_mutex_lock(&g_mu);
	int rc = 0;
	while(!p->done && rc == 0)
		rc = pthread_cond_timedwait(&g_cv, &g_mu, &ts);
	int done = p->done;
	int result = p->result;
	int err = p->err;
	pthread_mutex_unlock(&g_mu);
	if(!done)
	{
		pthread_mutex_lock(&g_mu);
		p->in_use = 0;
		free(p->payload);
		p->payload = NULL;
		pthread_mutex_unlock(&g_mu);
		errno = ETIMEDOUT;
		return -1;
	}
	if(result < 0)
	{
		errno = err ? err : EIO;
		pthread_mutex_lock(&g_mu);
		p->in_use = 0;
		free(p->payload);
		p->payload = NULL;
		pthread_mutex_unlock(&g_mu);
		return -1;
	}
	return result;
}

static void pending_release(Pending *p)
{
	pthread_mutex_lock(&g_mu);
	free(p->payload);
	p->payload = NULL;
	p->in_use = 0;
	pthread_mutex_unlock(&g_mu);
}

static int rpc(uint8_t type, int32_t fd, int32_t a, int32_t b, int32_t c, const void *payload, uint32_t len, int timeout_ms)
{
	pthread_mutex_lock(&g_mu);
	uint32_t id = alloc_id_locked();
	Pending *p = pending_alloc_locked(id);
	pthread_mutex_unlock(&g_mu);
	if(!p)
	{
		errno = ENOMEM;
		return -1;
	}
	if(send_msg(type, id, fd, a, b, c, payload, len) < 0)
	{
		pending_release(p);
		return -1;
	}
	int r = wait_pending(p, timeout_ms);
	if(r >= 0)
		pending_release(p);
	return r;
}

static void parse_hdr(const uint8_t *buf, ChiakiNetHdr *h)
{
	memset(h, 0, sizeof(*h));
	h->type = buf[0];
	memcpy(&h->id, buf + 4, 4);
	memcpy(&h->fd, buf + 8, 4);
	memcpy(&h->a, buf + 12, 4);
	memcpy(&h->b, buf + 16, 4);
	memcpy(&h->c, buf + 20, 4);
	memcpy(&h->len, buf + 24, 4);
}

static EM_BOOL on_open(int eventType, const EmscriptenWebSocketOpenEvent *e, void *user)
{
	(void)eventType;
	(void)e;
	(void)user;
	pthread_mutex_lock(&g_mu);
	g_ws_open = 1;
	g_ws_failed = 0;
	pthread_cond_broadcast(&g_cv);
	pthread_mutex_unlock(&g_mu);
	return EM_TRUE;
}

static EM_BOOL on_error(int eventType, const EmscriptenWebSocketErrorEvent *e, void *user)
{
	(void)eventType;
	(void)e;
	(void)user;
	pthread_mutex_lock(&g_mu);
	g_ws_failed = 1;
	pthread_cond_broadcast(&g_cv);
	pthread_mutex_unlock(&g_mu);
	return EM_TRUE;
}

static EM_BOOL on_close(int eventType, const EmscriptenWebSocketCloseEvent *e, void *user)
{
	(void)eventType;
	(void)e;
	(void)user;
	pthread_mutex_lock(&g_mu);
	g_ws_open = 0;
	g_ws_failed = 1;
	pthread_cond_broadcast(&g_cv);
	pthread_mutex_unlock(&g_mu);
	return EM_TRUE;
}

static EM_BOOL on_message(int eventType, const EmscriptenWebSocketMessageEvent *e, void *user)
{
	(void)eventType;
	(void)user;
	if(e->isText || e->numBytes < CHIAKI_NET_HDR_SIZE)
		return EM_TRUE;

	ChiakiNetHdr h;
	parse_hdr(e->data, &h);
	const uint8_t *payload = e->data + CHIAKI_NET_HDR_SIZE;
	if(h.len > (uint32_t)(e->numBytes - CHIAKI_NET_HDR_SIZE))
		h.len = (uint32_t)(e->numBytes - CHIAKI_NET_HDR_SIZE);

	pthread_mutex_lock(&g_mu);
	if(h.type == CHIAKI_NET_REPLY)
	{
		for(int i = 0; i < PENDING_MAX; i++)
		{
			if(g_pending[i].in_use && g_pending[i].id == h.id)
			{
				g_pending[i].result = h.a;
				g_pending[i].err = h.b;
				g_pending[i].extra = h.c;
				if(h.len)
				{
					g_pending[i].payload = malloc(h.len);
					if(g_pending[i].payload)
					{
						memcpy(g_pending[i].payload, payload, h.len);
						g_pending[i].payload_len = h.len;
					}
				}
				g_pending[i].done = 1;
				pthread_cond_broadcast(&g_cv);
				break;
			}
		}
	}
	else if(h.type == CHIAKI_NET_PUSH_DATA)
	{
		NetFd *s = slot(h.fd);
		if(s)
		{
			enqueue_pkt(s, payload, h.len, (uint32_t)h.c, (uint16_t)h.b);
			pthread_cond_broadcast(&g_cv);
		}
	}
	else if(h.type == CHIAKI_NET_PUSH_CONNECTED)
	{
		NetFd *s = slot(h.fd);
		if(s)
		{
			s->connected = 1;
			pthread_cond_broadcast(&g_cv);
		}
	}
	else if(h.type == CHIAKI_NET_PUSH_CLOSED || h.type == CHIAKI_NET_PUSH_ERROR)
	{
		NetFd *s = slot(h.fd);
		if(s)
		{
			s->closed = 1;
			pthread_cond_broadcast(&g_cv);
		}
	}
	pthread_mutex_unlock(&g_mu);
	return EM_TRUE;
}

int chiaki_wasm_net_connect(const char *proxy_url)
{
	if(!emscripten_websocket_is_supported())
	{
		fprintf(stderr, "WebSocket API not supported\n");
		return -1;
	}
	strncpy(g_proxy_url, proxy_url ? proxy_url : "ws://127.0.0.1:8080/posix-net", sizeof(g_proxy_url) - 1);

	EmscriptenWebSocketCreateAttributes attr;
	emscripten_websocket_init_create_attributes(&attr);
	attr.url = g_proxy_url;
	attr.protocols = "binary";
	attr.createOnMainThread = EM_TRUE;

	pthread_mutex_lock(&g_mu);
	g_ws_open = 0;
	g_ws_failed = 0;
	pthread_mutex_unlock(&g_mu);

	g_ws = emscripten_websocket_new(&attr);
	if(g_ws <= 0)
		return -1;

	emscripten_websocket_set_onopen_callback(g_ws, NULL, on_open);
	emscripten_websocket_set_onerror_callback(g_ws, NULL, on_error);
	emscripten_websocket_set_onclose_callback(g_ws, NULL, on_close);
	emscripten_websocket_set_onmessage_callback(g_ws, NULL, on_message);
	return 0;
}

void chiaki_wasm_net_disconnect(void)
{
	if(g_ws)
	{
		emscripten_websocket_close(g_ws, 1000, "bye");
		emscripten_websocket_delete(g_ws);
		g_ws = 0;
	}
	pthread_mutex_lock(&g_mu);
	g_ws_open = 0;
	pthread_mutex_unlock(&g_mu);
}

int chiaki_wasm_net_is_ready(void)
{
	pthread_mutex_lock(&g_mu);
	int r = g_ws_open;
	pthread_mutex_unlock(&g_mu);
	return r;
}

int __wrap_socket(int domain, int type, int protocol)
{
	if(domain != AF_INET)
	{
		errno = EAFNOSUPPORT;
		return -1;
	}
	int stype = type & 0xf;
	FdKind kind;
	if(stype == SOCK_DGRAM)
		kind = FD_UDP;
	else if(stype == SOCK_STREAM)
		kind = FD_TCP;
	else
	{
		errno = EPROTONOSUPPORT;
		return -1;
	}
	pthread_mutex_lock(&g_mu);
	int fd = alloc_fd_locked(kind);
	pthread_mutex_unlock(&g_mu);
	if(fd < 0)
	{
		errno = EMFILE;
		return -1;
	}
	if(kind == FD_UDP)
	{
		send_msg(CHIAKI_NET_SOCKET, 0, fd, domain, stype, protocol, NULL, 0);
		return fd;
	}
	if(rpc(CHIAKI_NET_SOCKET, fd, domain, stype, protocol, NULL, 0, 5000) < 0)
	{
		pthread_mutex_lock(&g_mu);
		g_fds[fd - FD_BASE].kind = FD_FREE;
		pthread_mutex_unlock(&g_mu);
		return -1;
	}
	return fd;
}

int __wrap_close(int fd)
{
	if(!is_ours(fd))
		return __real_close(fd);
	NetFd *s = slot(fd);
	if(!s)
	{
		errno = EBADF;
		return -1;
	}
	if(s->kind == FD_UDP || s->kind == FD_TCP)
		send_msg(CHIAKI_NET_CLOSE, 0, fd, 0, 0, 0, NULL, 0);
	pthread_mutex_lock(&g_mu);
	pkt_free_all(s);
	s->kind = FD_FREE;
	pthread_cond_broadcast(&g_cv);
	pthread_mutex_unlock(&g_mu);
	return 0;
}

int __wrap_bind(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
	(void)addrlen;
	NetFd *s = slot(fd);
	if(!s)
	{
		errno = EBADF;
		return -1;
	}
	if(!addr || addr->sa_family != AF_INET)
	{
		errno = EAFNOSUPPORT;
		return -1;
	}
	const struct sockaddr_in *in = (const struct sockaddr_in *)addr;
	uint16_t port = ntohs(in->sin_port);
	uint32_t ip = in->sin_addr.s_addr;
	return rpc(CHIAKI_NET_BIND, fd, AF_INET, port, (int32_t)ip, NULL, 0, 5000) < 0 ? -1 : 0;
}

int __wrap_connect(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
	(void)addrlen;
	NetFd *s = slot(fd);
	if(!s)
	{
		errno = EBADF;
		return -1;
	}
	if(!addr || addr->sa_family != AF_INET)
	{
		errno = EAFNOSUPPORT;
		return -1;
	}
	const struct sockaddr_in *in = (const struct sockaddr_in *)addr;
	uint16_t port = ntohs(in->sin_port);
	uint32_t ip = in->sin_addr.s_addr;
	s->peer_addr = ip;
	s->peer_port = port;
	if(s->kind == FD_UDP)
	{
		s->connected = 1;
		return rpc(CHIAKI_NET_CONNECT, fd, AF_INET, port, (int32_t)ip, NULL, 0, 5000) < 0 ? -1 : 0;
	}
	if(s->nonblock)
	{
		if(send_msg(CHIAKI_NET_CONNECT, 0, fd, AF_INET, port, (int32_t)ip, NULL, 0) < 0)
			return -1;
		errno = EINPROGRESS;
		return -1;
	}
	if(rpc(CHIAKI_NET_CONNECT, fd, AF_INET, port, (int32_t)ip, NULL, 0, 10000) < 0)
		return -1;
	pthread_mutex_lock(&g_mu);
	s->connected = 1;
	pthread_mutex_unlock(&g_mu);
	return 0;
}

static ssize_t do_sendto(int fd, const void *buf, size_t len, uint32_t addr, uint16_t port)
{
	NetFd *s = slot(fd);
	if(!s)
	{
		errno = EBADF;
		return -1;
	}
	if(send_msg(CHIAKI_NET_SENDTO, 0, fd, AF_INET, port, (int32_t)addr, buf, (uint32_t)len) < 0)
		return -1;
	return (ssize_t)len;
}

ssize_t __wrap_sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *dest, socklen_t addrlen)
{
	(void)flags;
	(void)addrlen;
	if(!dest)
		return __wrap_send(fd, buf, len, flags);
	if(dest->sa_family != AF_INET)
	{
		errno = EAFNOSUPPORT;
		return -1;
	}
	const struct sockaddr_in *in = (const struct sockaddr_in *)dest;
	return do_sendto(fd, buf, len, in->sin_addr.s_addr, ntohs(in->sin_port));
}

ssize_t __wrap_send(int fd, const void *buf, size_t len, int flags)
{
	(void)flags;
	NetFd *s = slot(fd);
	if(!s)
	{
		errno = EBADF;
		return -1;
	}
	if(s->kind == FD_PIPE_W)
		return __wrap_write(fd, buf, len);
	if(s->kind == FD_UDP)
		return do_sendto(fd, buf, len, s->peer_addr, s->peer_port);
	if(send_msg(CHIAKI_NET_SEND, 0, fd, 0, 0, 0, buf, (uint32_t)len) < 0)
		return -1;
	return (ssize_t)len;
}

static ssize_t dequeue_recv(NetFd *s, void *buf, size_t len, struct sockaddr_in *from, int wait)
{
	for(;;)
	{
		if(s->q_head)
		{
			NetPkt *p = s->q_head;
			size_t n = p->len < len ? p->len : len;
			memcpy(buf, p->data, n);
			if(from)
			{
				memset(from, 0, sizeof(*from));
				from->sin_family = AF_INET;
				from->sin_port = htons(p->port);
				from->sin_addr.s_addr = p->addr;
			}
			s->q_head = p->next;
			if(!s->q_head)
				s->q_tail = NULL;
			s->q_count--;
			free(p->data);
			free(p);
			return (ssize_t)n;
		}
		if(s->closed)
			return 0;
		if(!wait || s->nonblock)
		{
			errno = EAGAIN;
			return -1;
		}
		pthread_cond_wait(&g_cv, &g_mu);
		if(s->kind == FD_FREE)
		{
			errno = EBADF;
			return -1;
		}
	}
}

ssize_t __wrap_recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *src, socklen_t *addrlen)
{
	(void)flags;
	NetFd *s = slot(fd);
	if(!s)
	{
		errno = EBADF;
		return -1;
	}
	pthread_mutex_lock(&g_mu);
	struct sockaddr_in from;
	ssize_t n = dequeue_recv(s, buf, len, &from, 1);
	pthread_mutex_unlock(&g_mu);
	if(n > 0 && src && addrlen && *addrlen >= sizeof(from))
	{
		memcpy(src, &from, sizeof(from));
		*addrlen = sizeof(from);
	}
	return n;
}

ssize_t __wrap_recv(int fd, void *buf, size_t len, int flags)
{
	(void)flags;
	NetFd *s = slot(fd);
	if(!s)
	{
		errno = EBADF;
		return -1;
	}
	if(s->kind == FD_PIPE_R)
		return __wrap_read(fd, buf, len);
	pthread_mutex_lock(&g_mu);
	ssize_t n = dequeue_recv(s, buf, len, NULL, 1);
	pthread_mutex_unlock(&g_mu);
	return n;
}

int __wrap_setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
	NetFd *s = slot(fd);
	if(!s)
	{
		errno = EBADF;
		return -1;
	}
	if(level == SOL_SOCKET && optname == SO_BROADCAST && optval && optlen >= (socklen_t)sizeof(int))
		s->broadcast = *(const int *)optval;
	send_msg(CHIAKI_NET_SETSOCKOPT, 0, fd, level, optname, optval ? *(const int *)optval : 0, NULL, 0);
	return 0;
}

int __wrap_getsockopt(int fd, int level, int optname, void *optval, socklen_t *optlen)
{
	(void)level;
	(void)optname;
	NetFd *s = slot(fd);
	if(!s)
	{
		errno = EBADF;
		return -1;
	}
	if(optval && optlen && *optlen >= sizeof(int))
	{
		*(int *)optval = 0;
		*optlen = sizeof(int);
	}
	return 0;
}

int __wrap_getsockname(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	NetFd *s = slot(fd);
	if(!s)
	{
		errno = EBADF;
		return -1;
	}
	pthread_mutex_lock(&g_mu);
	uint32_t id = alloc_id_locked();
	Pending *p = pending_alloc_locked(id);
	pthread_mutex_unlock(&g_mu);
	if(!p)
	{
		errno = ENOMEM;
		return -1;
	}
	if(send_msg(CHIAKI_NET_GETSOCKNAME, id, fd, 0, 0, 0, NULL, 0) < 0)
	{
		pending_release(p);
		return -1;
	}
	int r = wait_pending(p, 3000);
	if(r < 0)
		return -1;
	pthread_mutex_lock(&g_mu);
	uint16_t port = (uint16_t)p->result;
	uint32_t ip = (uint32_t)p->extra;
	pthread_mutex_unlock(&g_mu);
	pending_release(p);
	if(!addr || !addrlen)
		return 0;
	struct sockaddr_in in;
	memset(&in, 0, sizeof(in));
	in.sin_family = AF_INET;
	in.sin_port = htons(port);
	in.sin_addr.s_addr = ip;
	socklen_t n = *addrlen < sizeof(in) ? *addrlen : sizeof(in);
	memcpy(addr, &in, n);
	*addrlen = sizeof(in);
	return 0;
}

int __wrap_getpeername(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	NetFd *s = slot(fd);
	if(!s)
	{
		errno = EBADF;
		return -1;
	}
	if(!s->connected)
	{
		errno = ENOTCONN;
		return -1;
	}
	struct sockaddr_in in;
	memset(&in, 0, sizeof(in));
	in.sin_family = AF_INET;
	in.sin_port = htons(s->peer_port);
	in.sin_addr.s_addr = s->peer_addr;
	socklen_t n = *addrlen < sizeof(in) ? *addrlen : sizeof(in);
	memcpy(addr, &in, n);
	*addrlen = sizeof(in);
	return 0;
}

int __wrap_shutdown(int fd, int how)
{
	NetFd *s = slot(fd);
	if(!s)
	{
		errno = EBADF;
		return -1;
	}
	send_msg(CHIAKI_NET_SHUTDOWN, 0, fd, how, 0, 0, NULL, 0);
	return 0;
}

int __wrap_listen(int fd, int backlog)
{
	(void)fd;
	(void)backlog;
	errno = EOPNOTSUPP;
	return -1;
}

int __wrap_accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	(void)fd;
	(void)addr;
	(void)addrlen;
	errno = EOPNOTSUPP;
	return -1;
}

int __wrap_pipe(int pipefd[2])
{
	pthread_mutex_lock(&g_mu);
	int r = alloc_fd_locked(FD_PIPE_R);
	int w = alloc_fd_locked(FD_PIPE_W);
	if(r < 0 || w < 0)
	{
		if(r >= 0)
			g_fds[r - FD_BASE].kind = FD_FREE;
		if(w >= 0)
			g_fds[w - FD_BASE].kind = FD_FREE;
		pthread_mutex_unlock(&g_mu);
		errno = EMFILE;
		return -1;
	}
	g_fds[r - FD_BASE].pipe_peer = w;
	g_fds[w - FD_BASE].pipe_peer = r;
	pthread_mutex_unlock(&g_mu);
	pipefd[0] = r;
	pipefd[1] = w;
	return 0;
}

ssize_t __wrap_write(int fd, const void *buf, size_t count)
{
	if(!is_ours(fd))
		return __real_write(fd, buf, count);
	NetFd *s = slot(fd);
	if(!s || s->kind != FD_PIPE_W)
	{
		errno = EBADF;
		return -1;
	}
	pthread_mutex_lock(&g_mu);
	NetFd *r = slot(s->pipe_peer);
	if(!r)
	{
		pthread_mutex_unlock(&g_mu);
		errno = EPIPE;
		return -1;
	}
	size_t n = 0;
	const uint8_t *p = buf;
	while(n < count && r->pipe_used < PIPE_BUF_SIZE)
	{
		r->pipe_buf[r->pipe_w] = p[n];
		r->pipe_w = (r->pipe_w + 1) % PIPE_BUF_SIZE;
		r->pipe_used++;
		n++;
	}
	if(n)
		pthread_cond_broadcast(&g_cv);
	pthread_mutex_unlock(&g_mu);
	if(n == 0)
	{
		errno = EAGAIN;
		return -1;
	}
	return (ssize_t)n;
}

ssize_t __wrap_read(int fd, void *buf, size_t count)
{
	if(!is_ours(fd))
		return __real_read(fd, buf, count);
	NetFd *s = slot(fd);
	if(!s)
	{
		errno = EBADF;
		return -1;
	}
	if(s->kind != FD_PIPE_R)
		return __wrap_recv(fd, buf, count, 0);
	pthread_mutex_lock(&g_mu);
	while(s->pipe_used == 0)
	{
		if(s->nonblock)
		{
			pthread_mutex_unlock(&g_mu);
			errno = EAGAIN;
			return -1;
		}
		pthread_cond_wait(&g_cv, &g_mu);
	}
	size_t n = 0;
	uint8_t *p = buf;
	while(n < count && s->pipe_used)
	{
		p[n++] = s->pipe_buf[s->pipe_r];
		s->pipe_r = (s->pipe_r + 1) % PIPE_BUF_SIZE;
		s->pipe_used--;
	}
	pthread_mutex_unlock(&g_mu);
	return (ssize_t)n;
}

int __wrap_fcntl(int fd, int cmd, ...)
{
	if(!is_ours(fd))
	{
		va_list ap;
		va_start(ap, cmd);
		int arg = va_arg(ap, int);
		va_end(ap);
		return __real_fcntl(fd, cmd, arg);
	}
	NetFd *s = slot(fd);
	if(!s)
	{
		errno = EBADF;
		return -1;
	}
	va_list ap;
	va_start(ap, cmd);
	int arg = 0;
	if(cmd == F_SETFL)
		arg = va_arg(ap, int);
	va_end(ap);
	if(cmd == F_GETFL)
		return s->nonblock ? O_NONBLOCK : 0;
	if(cmd == F_SETFL)
	{
		s->nonblock = (arg & O_NONBLOCK) ? 1 : 0;
		return 0;
	}
	return 0;
}

int __wrap_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
	uint8_t want_r[FD_MAX];
	uint8_t want_w[FD_MAX];
	memset(want_r, 0, sizeof(want_r));
	memset(want_w, 0, sizeof(want_w));
	int has_ours = 0;
	for(int i = 0; i < FD_MAX; i++)
	{
		int fd = FD_BASE + i;
		if(fd >= nfds || fd >= FD_SETSIZE)
			continue;
		if(readfds && FD_ISSET(fd, readfds))
		{
			want_r[i] = 1;
			has_ours = 1;
		}
		if(writefds && FD_ISSET(fd, writefds))
		{
			want_w[i] = 1;
			has_ours = 1;
		}
	}
	if(!has_ours)
		return __real_select(nfds, readfds, writefds, exceptfds, timeout);

	int64_t wait_ms = -1;
	if(timeout)
		wait_ms = (int64_t)timeout->tv_sec * 1000 + timeout->tv_usec / 1000;

	struct timespec deadline;
	clock_gettime(CLOCK_REALTIME, &deadline);
	if(wait_ms >= 0)
	{
		deadline.tv_sec += wait_ms / 1000;
		deadline.tv_nsec += (wait_ms % 1000) * 1000000L;
		if(deadline.tv_nsec >= 1000000000L)
		{
			deadline.tv_sec++;
			deadline.tv_nsec -= 1000000000L;
		}
	}

	pthread_mutex_lock(&g_mu);
	int nready;
	for(;;)
	{
		nready = 0;
		if(readfds)
			FD_ZERO(readfds);
		if(writefds)
			FD_ZERO(writefds);
		if(exceptfds)
			FD_ZERO(exceptfds);

		for(int i = 0; i < FD_MAX; i++)
		{
			int fd = FD_BASE + i;
			if(fd >= nfds || fd >= FD_SETSIZE)
				continue;
			NetFd *s = slot(fd);
			if(!s)
				continue;
			if(want_r[i])
			{
				int readable = 0;
				if(s->kind == FD_PIPE_R && s->pipe_used)
					readable = 1;
				else if((s->kind == FD_UDP || s->kind == FD_TCP) && (s->q_head || s->closed))
					readable = 1;
				if(readable)
				{
					if(readfds)
						FD_SET(fd, readfds);
					nready++;
				}
			}
			if(want_w[i])
			{
				int writable = 0;
				if(s->kind == FD_PIPE_W)
					writable = 1;
				else if(s->kind == FD_UDP)
					writable = 1;
				else if(s->kind == FD_TCP && (s->connected || s->closed))
					writable = 1;
				if(writable)
				{
					if(writefds)
						FD_SET(fd, writefds);
					nready++;
				}
			}
		}
		if(nready > 0 || wait_ms == 0)
			break;
		int rc;
		if(wait_ms < 0)
			rc = pthread_cond_wait(&g_cv, &g_mu);
		else
			rc = pthread_cond_timedwait(&g_cv, &g_mu, &deadline);
		if(rc == ETIMEDOUT)
			break;
	}
	pthread_mutex_unlock(&g_mu);
	return nready;
}

int __wrap_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
	if(!res)
	{
		errno = EINVAL;
		return EAI_FAIL;
	}
	*res = NULL;
	if(!node)
		return EAI_NONAME;

	struct in_addr addr;
	if(inet_aton(node, &addr))
	{
		struct addrinfo *ai = calloc(1, sizeof(struct addrinfo));
		struct sockaddr_in *sa = calloc(1, sizeof(struct sockaddr_in));
		if(!ai || !sa)
		{
			free(ai);
			free(sa);
			return EAI_MEMORY;
		}
		sa->sin_family = AF_INET;
		sa->sin_addr = addr;
		if(service)
			sa->sin_port = htons((uint16_t)atoi(service));
		ai->ai_family = AF_INET;
		ai->ai_socktype = hints ? hints->ai_socktype : SOCK_DGRAM;
		ai->ai_protocol = hints ? hints->ai_protocol : 0;
		ai->ai_addr = (struct sockaddr *)sa;
		ai->ai_addrlen = sizeof(*sa);
		*res = ai;
		return 0;
	}

	pthread_mutex_lock(&g_mu);
	uint32_t id = alloc_id_locked();
	Pending *p = pending_alloc_locked(id);
	pthread_mutex_unlock(&g_mu);
	if(!p)
		return EAI_MEMORY;
	if(send_msg(CHIAKI_NET_GETADDRINFO, id, 0, 0, 0, 0, node, (uint32_t)(strlen(node) + 1)) < 0)
	{
		pending_release(p);
		return EAI_FAIL;
	}
	int r = wait_pending(p, 8000);
	if(r < 0)
		return EAI_NONAME;
	pthread_mutex_lock(&g_mu);
	uint32_t ip = p->payload_len >= 4 ? 0 : (uint32_t)p->result;
	if(p->payload_len >= 4)
		memcpy(&ip, p->payload, 4);
	else
		ip = (uint32_t)p->result;
	pthread_mutex_unlock(&g_mu);
	pending_release(p);

	struct addrinfo *ai = calloc(1, sizeof(struct addrinfo));
	struct sockaddr_in *sa = calloc(1, sizeof(struct sockaddr_in));
	if(!ai || !sa)
	{
		free(ai);
		free(sa);
		return EAI_MEMORY;
	}
	sa->sin_family = AF_INET;
	sa->sin_addr.s_addr = ip;
	if(service)
		sa->sin_port = htons((uint16_t)atoi(service));
	ai->ai_family = AF_INET;
	ai->ai_socktype = hints ? hints->ai_socktype : SOCK_STREAM;
	ai->ai_addr = (struct sockaddr *)sa;
	ai->ai_addrlen = sizeof(*sa);
	*res = ai;
	return 0;
}

void __wrap_freeaddrinfo(struct addrinfo *res)
{
	while(res)
	{
		struct addrinfo *n = res->ai_next;
		free(res->ai_addr);
		free(res);
		res = n;
	}
}
