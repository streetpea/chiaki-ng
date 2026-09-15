#pragma once

#include "common.hpp"

#include <memory>

struct CloudUrl {
	bool tls = true;
	std::string host;
	uint16_t port = 443;
};

CloudUrl parse_cloud_url(std::string s);

class CloudWs {
public:
	CloudWs();
	~CloudWs();
	CloudWs(const CloudWs &) = delete;
	CloudWs &operator=(const CloudWs &) = delete;

	bool login(const CloudUrl &url, const std::string &email, const std::string &password,
		bool insecure, const std::string &ca_file, std::string &cookie, std::string &err);
	bool open_agent(const CloudUrl &url, const std::string &cookie, bool insecure,
		const std::string &ca_file, std::string &err);
	bool send_msg(const uint8_t *p, size_t n);
	int recv_msg(std::vector<uint8_t> &out, int timeout_ms);
	void close();
	bool ok() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

std::vector<uint8_t> ws_frame(uint8_t opcode, const uint8_t *p, size_t n, bool mask);

struct WsMsg {
	uint8_t opcode = 0;
	std::vector<uint8_t> payload;
	size_t used = 0;
};

bool ws_parse(const uint8_t *b, size_t n, WsMsg &out);
bool ws_server_handshake(sock_t fd, std::string &err);
