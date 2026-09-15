#pragma once

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  ifndef SIO_UDP_CONNRESET
#    define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#  endif
using sock_t = SOCKET;
#  define INVALID_SOCK INVALID_SOCKET
#  define sock_close closesocket
#  define sock_err() WSAGetLastError()
#  define SOCK_EINPROGRESS WSAEWOULDBLOCK
inline int poll_fds(pollfd *fds, unsigned n, int ms) { return WSAPoll(fds, n, ms); }
#else
#  include <arpa/inet.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
using sock_t = int;
#  define INVALID_SOCK (-1)
#  define sock_close ::close
#  define sock_err() errno
#  define SOCK_EINPROGRESS EINPROGRESS
inline int poll_fds(pollfd *fds, unsigned n, int ms) { return ::poll(fds, (nfds_t)n, ms); }
#endif

#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <strings.h>
#endif

inline std::atomic<bool> g_run{true};
inline std::atomic<bool> g_stay_down{false};

inline void logf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	std::vprintf(fmt, ap);
	va_end(ap);
	std::fflush(stdout);
}

inline uint32_t rd_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

inline int32_t rd_i32(const uint8_t *p) { return (int32_t)rd_u32(p); }

inline void wr_u32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

inline uint32_t ip_to_u32(const char *ip)
{
	unsigned a = 0, b = 0, c = 0, d = 0;
	if (std::sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d) != 4)
		return 0;
	return a | (b << 8) | (c << 16) | (d << 24);
}

inline void u32_to_ip(uint32_t n, char *out, size_t cap)
{
	std::snprintf(out, cap, "%u.%u.%u.%u", n & 255, (n >> 8) & 255, (n >> 16) & 255, (n >> 24) & 255);
}

inline in_addr u32_to_in_addr(uint32_t n)
{
	in_addr a{};
	uint8_t b[4] = { (uint8_t)n, (uint8_t)(n >> 8), (uint8_t)(n >> 16), (uint8_t)(n >> 24) };
	std::memcpy(&a, b, 4);
	return a;
}

inline uint32_t in_addr_to_u32(in_addr a)
{
	uint8_t b[4];
	std::memcpy(b, &a, 4);
	return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

inline bool sock_would_block()
{
#ifdef _WIN32
	int e = WSAGetLastError();
	return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
	return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS;
#endif
}

inline bool set_nonblock(sock_t s)
{
#ifdef _WIN32
	u_long n = 1;
	return ioctlsocket(s, FIONBIO, &n) == 0;
#else
	int fl = fcntl(s, F_GETFL, 0);
	return fl >= 0 && fcntl(s, F_SETFL, fl | O_NONBLOCK) == 0;
#endif
}

inline void set_udp_opts(sock_t s)
{
	int one = 1;
	int buf = 4 * 1024 * 1024;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one));
	setsockopt(s, SOL_SOCKET, SO_BROADCAST, (char *)&one, sizeof(one));
	setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char *)&buf, sizeof(buf));
	setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char *)&buf, sizeof(buf));
#ifdef _WIN32
	BOOL disable = FALSE;
	DWORD bytes = 0;
	WSAIoctl(s, SIO_UDP_CONNRESET, &disable, sizeof(disable), nullptr, 0, &bytes, nullptr, nullptr);
#endif
}

inline void set_tcp_opts(sock_t s)
{
	int one = 1;
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(one));
}

inline bool net_init()
{
#ifdef _WIN32
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return false;
	SetConsoleOutputCP(65001);
#endif
	return true;
}

inline void net_shutdown()
{
#ifdef _WIN32
	WSACleanup();
#endif
}

constexpr size_t NET_HDR = 28;
constexpr uint8_t NET_SOCKET = 1;
constexpr uint8_t NET_CLOSE = 2;
constexpr uint8_t NET_BIND = 3;
constexpr uint8_t NET_CONNECT = 4;
constexpr uint8_t NET_SEND = 5;
constexpr uint8_t NET_SENDTO = 6;
constexpr uint8_t NET_SETSOCKOPT = 7;
constexpr uint8_t NET_GETSOCKNAME = 8;
constexpr uint8_t NET_GETADDRINFO = 9;
constexpr uint8_t NET_SHUTDOWN = 10;
constexpr uint8_t NET_REPLY = 128;
constexpr uint8_t NET_PUSH_DATA = 129;
constexpr uint8_t NET_PUSH_CONNECTED = 130;
constexpr uint8_t NET_PUSH_CLOSED = 131;
constexpr uint8_t NET_PUSH_ERROR = 132;

constexpr int SOCK_STREAM_T = 1;
constexpr int SOCK_DGRAM_T = 2;

constexpr size_t AGENT_HDR = 16;
constexpr uint8_t A_OPEN = 1;
constexpr uint8_t A_CLOSE = 2;
constexpr uint8_t A_DATA = 3;
constexpr uint8_t A_PING = 4;
constexpr uint8_t A_PONG = 5;
constexpr uint8_t A_PORTCHECK = 6;
constexpr uint8_t A_PORTCHECK_RES = 7;
constexpr uint8_t A_HELLO = 8;
constexpr uint8_t A_HELLO_OK = 9;
constexpr uint8_t A_REJECTED = 10;
constexpr uint8_t A_APPROVED = 11;

struct NetHdr {
	uint8_t type = 0;
	uint32_t id = 0;
	int32_t fd = 0;
	int32_t a = 0;
	int32_t b = 0;
	int32_t c = 0;
	uint32_t len = 0;
};

inline NetHdr read_net_hdr(const uint8_t *p)
{
	NetHdr h;
	h.type = p[0];
	h.id = rd_u32(p + 4);
	h.fd = rd_i32(p + 8);
	h.a = rd_i32(p + 12);
	h.b = rd_i32(p + 16);
	h.c = rd_i32(p + 20);
	h.len = rd_u32(p + 24);
	return h;
}

inline std::vector<uint8_t> encode_net(uint8_t type, uint32_t id, int32_t fd, int32_t a, int32_t b, int32_t c,
	const uint8_t *payload, uint32_t plen)
{
	std::vector<uint8_t> buf(NET_HDR + plen);
	std::memset(buf.data(), 0, NET_HDR);
	buf[0] = type;
	wr_u32(buf.data() + 4, id);
	wr_u32(buf.data() + 8, (uint32_t)fd);
	wr_u32(buf.data() + 12, (uint32_t)a);
	wr_u32(buf.data() + 16, (uint32_t)b);
	wr_u32(buf.data() + 20, (uint32_t)c);
	wr_u32(buf.data() + 24, plen);
	if (plen && payload)
		std::memcpy(buf.data() + NET_HDR, payload, plen);
	return buf;
}

struct AgentHdr {
	uint8_t type = 0;
	uint32_t sid = 0;
	uint32_t len = 0;
	uint32_t extra = 0;
};

inline AgentHdr read_agent_hdr(const uint8_t *p)
{
	AgentHdr h;
	h.type = p[0];
	h.sid = rd_u32(p + 4);
	h.len = rd_u32(p + 8);
	h.extra = rd_u32(p + 12);
	return h;
}

inline std::vector<uint8_t> encode_agent(uint8_t type, uint32_t sid, uint32_t extra,
	const uint8_t *payload, uint32_t plen)
{
	std::vector<uint8_t> buf(AGENT_HDR + plen);
	std::memset(buf.data(), 0, AGENT_HDR);
	buf[0] = type;
	wr_u32(buf.data() + 4, sid);
	wr_u32(buf.data() + 8, plen);
	wr_u32(buf.data() + 12, extra);
	if (plen && payload)
		std::memcpy(buf.data() + AGENT_HDR, payload, plen);
	return buf;
}

inline std::string json_escape(const std::string &s)
{
	std::string o;
	o.reserve(s.size() + 8);
	for (unsigned char c : s) {
		if (c == '"' || c == '\\') {
			o += '\\';
			o += (char)c;
		} else if (c < 32) {
			char b[8];
			std::snprintf(b, sizeof(b), "\\u%04x", c);
			o += b;
		} else {
			o += (char)c;
		}
	}
	return o;
}

inline bool env_flag(const char *name, bool fallback = false)
{
	const char *v = std::getenv(name);
	if (!v || !*v)
		return fallback;
#ifdef _WIN32
	return !_stricmp(v, "1") || !_stricmp(v, "true") || !_stricmp(v, "yes") || !_stricmp(v, "on");
#else
	return !strcasecmp(v, "1") || !strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcasecmp(v, "on");
#endif
}

inline std::string env_str(const char *name, const char *fallback = "")
{
	const char *v = std::getenv(name);
	if (v && *v)
		return v;
	return fallback;
}

void load_dotenv();
int tcp_probe(const char *ip, uint16_t port, int ms);
std::string machine_name();
void sleep_ms(int ms);
std::string parent_dir(const std::string &p);
std::string join_path(const std::string &a, const std::string &b);
