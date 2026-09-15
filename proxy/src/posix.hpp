#pragma once

#include "common.hpp"

class PosixBridge {
public:
	explicit PosixBridge(std::function<void(const uint8_t *, size_t)> send);
	~PosixBridge();

	void handle(const uint8_t *data, size_t len);
	void closeAll();
	void fillPoll(std::vector<pollfd> &fds) const;
	void dispatchPoll(const pollfd *fds, size_t n);

private:
	struct Rec {
		int type = 0;
		sock_t s = INVALID_SOCK;
		uint16_t remote_port = 0;
		uint32_t remote_addr = 0;
		uint16_t bound_port = 0;
		uint32_t bound_addr = 0;
		bool connected = false;
		bool connecting = false;
		bool has_wait = false;
		NetHdr a_wait{};
	};

	std::function<void(const uint8_t *, size_t)> send_;
	std::map<int, Rec> socks_;

	void emit(const std::vector<uint8_t> &pkt);
	void reply(const NetHdr &h, int32_t result, int32_t err = 0, int32_t extra = 0,
		const uint8_t *payload = nullptr, uint32_t plen = 0);
	void push_data(int fd, uint16_t port, uint32_t addr, const uint8_t *p, size_t n);
	Rec *ensure_udp(int fd);
	void close_rec(Rec &r);
	void op_socket(const NetHdr &h);
	void op_close(const NetHdr &h);
	void op_bind(const NetHdr &h);
	void op_connect(const NetHdr &h);
	void op_send(const NetHdr &h, const uint8_t *payload);
	void op_setsockopt(const NetHdr &h);
	void op_getsockname(const NetHdr &h);
	void op_getaddrinfo(const NetHdr &h, const uint8_t *payload);
	void op_shutdown(const NetHdr &h);
	void on_udp(int fd, Rec &r);
	void on_tcp(int fd, Rec &r);
	void finish_connect(int fd, Rec &r, bool ok);
};
