#include "posix.hpp"

#include <algorithm>

PosixBridge::PosixBridge(std::function<void(const uint8_t *, size_t)> send)
	: send_(std::move(send))
{
}

PosixBridge::~PosixBridge()
{
	closeAll();
}

void PosixBridge::emit(const std::vector<uint8_t> &pkt)
{
	if (send_)
		send_(pkt.data(), pkt.size());
}

void PosixBridge::reply(const NetHdr &h, int32_t result, int32_t err, int32_t extra,
	const uint8_t *payload, uint32_t plen)
{
	if (!h.id)
		return;
	emit(encode_net(NET_REPLY, h.id, h.fd, result, err, extra, payload, plen));
}

void PosixBridge::push_data(int fd, uint16_t port, uint32_t addr, const uint8_t *p, size_t n)
{
	emit(encode_net(NET_PUSH_DATA, 0, fd, 0, (int32_t)port, (int32_t)addr, p, (uint32_t)n));
}

void PosixBridge::close_rec(Rec &r)
{
	if (r.s != INVALID_SOCK) {
		sock_close(r.s);
		r.s = INVALID_SOCK;
	}
	r.connecting = false;
	r.connected = false;
}

void PosixBridge::closeAll()
{
	for (auto &kv : socks_)
		close_rec(kv.second);
	socks_.clear();
}

PosixBridge::Rec *PosixBridge::ensure_udp(int fd)
{
	Rec &r = socks_[fd];
	if (r.s != INVALID_SOCK && r.type == SOCK_DGRAM_T)
		return &r;
	if (r.s != INVALID_SOCK)
		close_rec(r);
	r.type = SOCK_DGRAM_T;
	r.s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (r.s == INVALID_SOCK)
		return nullptr;
	set_udp_opts(r.s);
	set_nonblock(r.s);
	return &r;
}

void PosixBridge::handle(const uint8_t *data, size_t len)
{
	if (len < NET_HDR)
		return;
	NetHdr h = read_net_hdr(data);
	const uint8_t *payload = data + NET_HDR;
	uint32_t plen = h.len;
	if (NET_HDR + plen > len)
		plen = (uint32_t)(len - NET_HDR);
	try {
		switch (h.type) {
		case NET_SOCKET: op_socket(h); break;
		case NET_CLOSE: op_close(h); break;
		case NET_BIND: op_bind(h); break;
		case NET_CONNECT: op_connect(h); break;
		case NET_SEND:
		case NET_SENDTO: op_send(h, payload); break;
		case NET_SETSOCKOPT: op_setsockopt(h); break;
		case NET_GETSOCKNAME: op_getsockname(h); break;
		case NET_GETADDRINFO: op_getaddrinfo(h, payload); break;
		case NET_SHUTDOWN: op_shutdown(h); break;
		default: reply(h, -1, 22); break;
		}
	} catch (...) {
		reply(h, -1, 5);
	}
	(void)plen;
}

void PosixBridge::op_socket(const NetHdr &h)
{
	const int type = h.b;
	if (type == SOCK_DGRAM_T) {
		if (!ensure_udp(h.fd)) {
			reply(h, -1, 5);
			return;
		}
		reply(h, 0);
		return;
	}
	if (type == SOCK_STREAM_T) {
		Rec r;
		r.type = SOCK_STREAM_T;
		socks_[h.fd] = r;
		reply(h, 0);
		return;
	}
	reply(h, -1, 93);
}

void PosixBridge::op_close(const NetHdr &h)
{
	auto it = socks_.find(h.fd);
	if (it != socks_.end()) {
		close_rec(it->second);
		socks_.erase(it);
	}
	reply(h, 0);
}

void PosixBridge::op_bind(const NetHdr &h)
{
	Rec *r = ensure_udp(h.fd);
	if (!r) {
		reply(h, -1, 98);
		return;
	}
	if (r->bound_port) {
		reply(h, 0);
		return;
	}
	sockaddr_in a{};
	a.sin_family = AF_INET;
	a.sin_port = 0;
	a.sin_addr.s_addr = INADDR_ANY;
	if (bind(r->s, (sockaddr *)&a, sizeof(a)) != 0) {
		reply(h, -1, 98);
		return;
	}
	sockaddr_in got{};
	socklen_t gl = sizeof(got);
	if (getsockname(r->s, (sockaddr *)&got, &gl) == 0) {
		r->bound_port = ntohs(got.sin_port);
		r->bound_addr = in_addr_to_u32(got.sin_addr);
	} else {
		r->bound_port = 1;
	}
	reply(h, 0);
}

void PosixBridge::op_connect(const NetHdr &h)
{
	auto it = socks_.find(h.fd);
	Rec *r = it == socks_.end() ? nullptr : &it->second;
	if (!r || r->type == SOCK_DGRAM_T)
		r = ensure_udp(h.fd);
	if (!r) {
		reply(h, -1, 5);
		return;
	}
	const uint16_t port = (uint16_t)h.b;
	const uint32_t ip = (uint32_t)h.c;
	r->remote_port = port;
	r->remote_addr = ip;
	if (r->type == SOCK_DGRAM_T) {
		reply(h, 0);
		return;
	}
	if (r->s != INVALID_SOCK)
		close_rec(*r);
	r->type = SOCK_STREAM_T;
	r->s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (r->s == INVALID_SOCK) {
		reply(h, -1, 5);
		return;
	}
	set_tcp_opts(r->s);
	set_nonblock(r->s);
	sockaddr_in a{};
	a.sin_family = AF_INET;
	a.sin_port = htons(port);
	a.sin_addr = u32_to_in_addr(ip);
	int rc = connect(r->s, (sockaddr *)&a, sizeof(a));
	if (rc == 0) {
		finish_connect(h.fd, *r, true);
		reply(h, 0);
		return;
	}
	if (sock_would_block() || sock_err() == SOCK_EINPROGRESS) {
		r->connecting = true;
		r->a_wait = h;
		r->has_wait = true;
		return;
	}
	reply(h, -1, 111);
	emit(encode_net(NET_PUSH_ERROR, 0, h.fd, 0, 0, 0, nullptr, 0));
	close_rec(*r);
}

void PosixBridge::finish_connect(int fd, Rec &r, bool ok)
{
	r.connecting = false;
	if (!ok) {
		if (r.has_wait)
			reply(r.a_wait, -1, 111);
		r.has_wait = false;
		emit(encode_net(NET_PUSH_ERROR, 0, fd, 0, 0, 0, nullptr, 0));
		close_rec(r);
		return;
	}
	r.connected = true;
	emit(encode_net(NET_PUSH_CONNECTED, 0, fd, 0, 0, 0, nullptr, 0));
	if (r.has_wait)
		reply(r.a_wait, 0);
	r.has_wait = false;
}

void PosixBridge::op_send(const NetHdr &h, const uint8_t *payload)
{
	auto it = socks_.find(h.fd);
	Rec *r = it == socks_.end() ? nullptr : &it->second;
	if (!r && h.type == NET_SENDTO)
		r = ensure_udp(h.fd);
	if (!r) {
		reply(h, (int32_t)h.len);
		return;
	}
	if (r->type == SOCK_DGRAM_T && r->s != INVALID_SOCK) {
		uint16_t port = h.b ? (uint16_t)h.b : r->remote_port;
		uint32_t ip = h.c ? (uint32_t)h.c : r->remote_addr;
		sockaddr_in a{};
		a.sin_family = AF_INET;
		a.sin_port = htons(port);
		a.sin_addr = u32_to_in_addr(ip);
		sendto(r->s, (const char *)payload, (int)h.len, 0, (sockaddr *)&a, sizeof(a));
	} else if (r->s != INVALID_SOCK) {
		send(r->s, (const char *)payload, (int)h.len, 0);
	}
	reply(h, (int32_t)h.len);
}

void PosixBridge::op_setsockopt(const NetHdr &h)
{
	auto it = socks_.find(h.fd);
	if (it != socks_.end() && it->second.type == SOCK_DGRAM_T && it->second.s != INVALID_SOCK && h.b == 6) {
		int one = h.c ? 1 : 0;
		setsockopt(it->second.s, SOL_SOCKET, SO_BROADCAST, (char *)&one, sizeof(one));
	}
	reply(h, 0);
}

void PosixBridge::op_getsockname(const NetHdr &h)
{
	auto it = socks_.find(h.fd);
	if (it == socks_.end()) {
		reply(h, -1, 9);
		return;
	}
	Rec &r = it->second;
	uint16_t port = r.bound_port;
	uint32_t addr = r.bound_addr;
	if (r.s != INVALID_SOCK) {
		sockaddr_in a{};
		socklen_t al = sizeof(a);
		if (getsockname(r.s, (sockaddr *)&a, &al) == 0) {
			port = ntohs(a.sin_port);
			addr = in_addr_to_u32(a.sin_addr);
			r.bound_port = port;
			r.bound_addr = addr;
		}
	}
	reply(h, (int32_t)port, 0, (int32_t)addr);
}

void PosixBridge::op_getaddrinfo(const NetHdr &h, const uint8_t *payload)
{
	std::string host((const char *)payload, h.len);
	while (!host.empty() && host.back() == 0)
		host.pop_back();
	addrinfo hints{};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	addrinfo *res = nullptr;
	if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
		reply(h, -1, 0);
		return;
	}
	auto *in = (sockaddr_in *)res->ai_addr;
	uint32_t ip = in_addr_to_u32(in->sin_addr);
	freeaddrinfo(res);
	uint8_t buf[4];
	wr_u32(buf, ip);
	reply(h, (int32_t)ip, 0, 0, buf, 4);
}

void PosixBridge::op_shutdown(const NetHdr &h)
{
	auto it = socks_.find(h.fd);
	if (it != socks_.end() && it->second.s != INVALID_SOCK && it->second.type == SOCK_STREAM_T) {
#ifdef _WIN32
		shutdown(it->second.s, SD_SEND);
#else
		shutdown(it->second.s, SHUT_WR);
#endif
	}
	reply(h, 0);
}

void PosixBridge::fillPoll(std::vector<pollfd> &fds) const
{
	for (const auto &kv : socks_) {
		const Rec &r = kv.second;
		if (r.s == INVALID_SOCK)
			continue;
		pollfd p{};
		p.fd = r.s;
		p.events = POLLIN;
		if (r.connecting)
			p.events = (short)(POLLIN | POLLOUT);
		fds.push_back(p);
	}
}

void PosixBridge::dispatchPoll(const pollfd *fds, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		if (!fds[i].revents)
			continue;
		for (auto &kv : socks_) {
			if (kv.second.s != (sock_t)fds[i].fd)
				continue;
			Rec &r = kv.second;
			if (r.connecting && (fds[i].revents & (POLLOUT | POLLERR | POLLHUP))) {
				int err = 0;
				socklen_t el = sizeof(err);
				getsockopt(r.s, SOL_SOCKET, SO_ERROR, (char *)&err, &el);
				finish_connect(kv.first, r, err == 0);
				break;
			}
			if (fds[i].revents & (POLLIN | POLLERR | POLLHUP)) {
				if (r.type == SOCK_DGRAM_T)
					on_udp(kv.first, r);
				else
					on_tcp(kv.first, r);
			}
			break;
		}
	}
}

void PosixBridge::on_udp(int fd, Rec &r)
{
	uint8_t buf[2048];
	sockaddr_in src{};
	socklen_t sl = sizeof(src);
	for (;;) {
		int n = recvfrom(r.s, (char *)buf, sizeof(buf), 0, (sockaddr *)&src, &sl);
		if (n < 0) {
			int e = sock_err();
#ifdef _WIN32
			if (e == WSAECONNRESET || e == WSAENETRESET || e == WSAECONNREFUSED)
				continue;
#endif
			if (sock_would_block())
				return;
#ifndef _WIN32
			if (e == ECONNREFUSED || e == ECONNRESET || e == ENETUNREACH)
				continue;
#endif
			return;
		}
		push_data(fd, ntohs(src.sin_port), in_addr_to_u32(src.sin_addr), buf, (size_t)n);
		sl = sizeof(src);
	}
}

void PosixBridge::on_tcp(int fd, Rec &r)
{
	uint8_t buf[16384];
	int n = recv(r.s, (char *)buf, sizeof(buf), 0);
	if (n > 0) {
		push_data(fd, r.remote_port, r.remote_addr, buf, (size_t)n);
		return;
	}
	if (n < 0 && sock_would_block())
		return;
	emit(encode_net(NET_PUSH_CLOSED, 0, fd, 0, 0, 0, nullptr, 0));
	close_rec(r);
}
