#include "ws.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>

#ifdef _WIN32
#  include <wincrypt.h>
#  include <winhttp.h>
#else
#  include <openssl/err.h>
#  include <openssl/evp.h>
#  include <openssl/rand.h>
#  include <openssl/ssl.h>
#endif

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string b64enc(const uint8_t *d, size_t n)
{
	std::string o;
	o.reserve(((n + 2) / 3) * 4);
	for (size_t i = 0; i < n; i += 3) {
		unsigned v = (unsigned)d[i] << 16;
		if (i + 1 < n)
			v |= (unsigned)d[i + 1] << 8;
		if (i + 2 < n)
			v |= (unsigned)d[i + 2];
		o += B64[(v >> 18) & 63];
		o += B64[(v >> 12) & 63];
		o += (i + 1 < n) ? B64[(v >> 6) & 63] : '=';
		o += (i + 2 < n) ? B64[v & 63] : '=';
	}
	return o;
}

static void sha1_20(const uint8_t *data, size_t n, uint8_t out[20])
{
#ifdef _WIN32
	HCRYPTPROV prov = 0;
	HCRYPTHASH hash = 0;
	DWORD len = 20;
	if (!CryptAcquireContext(&prov, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
		return;
	if (CryptCreateHash(prov, CALG_SHA1, 0, 0, &hash)) {
		CryptHashData(hash, data, (DWORD)n, 0);
		CryptGetHashParam(hash, HP_HASHVAL, out, &len, 0);
		CryptDestroyHash(hash);
	}
	CryptReleaseContext(prov, 0);
#else
	unsigned int mdlen = 20;
	EVP_Digest(data, n, out, &mdlen, EVP_sha1(), nullptr);
#endif
}

static bool rand_bytes(uint8_t *p, int n)
{
#ifdef _WIN32
	HCRYPTPROV prov = 0;
	if (!CryptAcquireContext(&prov, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
		return false;
	BOOL ok = CryptGenRandom(prov, (DWORD)n, p);
	CryptReleaseContext(prov, 0);
	return ok != 0;
#else
	return RAND_bytes(p, n) == 1;
#endif
}

CloudUrl parse_cloud_url(std::string s)
{
	CloudUrl u;
	while (!s.empty() && (s.back() == '/' || (unsigned char)s.back() <= 32))
		s.pop_back();
	if (s.compare(0, 8, "https://") == 0) {
		u.tls = true;
		s = s.substr(8);
		u.port = 443;
	} else if (s.compare(0, 7, "http://") == 0) {
		u.tls = false;
		s = s.substr(7);
		u.port = 80;
	} else if (s.compare(0, 6, "wss://") == 0) {
		u.tls = true;
		s = s.substr(6);
		u.port = 443;
	} else if (s.compare(0, 5, "ws://") == 0) {
		u.tls = false;
		s = s.substr(5);
		u.port = 80;
	} else {
		u.tls = true;
		u.port = 443;
	}
	auto slash = s.find('/');
	if (slash != std::string::npos)
		s = s.substr(0, slash);
	auto colon = s.rfind(':');
	if (colon != std::string::npos && s.find(':') == colon) {
		u.host = s.substr(0, colon);
		int p = std::atoi(s.c_str() + colon + 1);
		if (p > 0 && p < 65536)
			u.port = (uint16_t)p;
	} else {
		u.host = s;
	}
	return u;
}

std::vector<uint8_t> ws_frame(uint8_t opcode, const uint8_t *p, size_t n, bool mask)
{
	std::vector<uint8_t> o;
	o.push_back((uint8_t)(0x80 | (opcode & 0x0f)));
	uint8_t mbit = mask ? 0x80 : 0;
	if (n < 126) {
		o.push_back((uint8_t)(mbit | n));
	} else if (n < 65536) {
		o.push_back((uint8_t)(mbit | 126));
		o.push_back((uint8_t)(n >> 8));
		o.push_back((uint8_t)n);
	} else {
		o.push_back((uint8_t)(mbit | 127));
		for (int i = 7; i >= 0; --i)
			o.push_back((uint8_t)(n >> (i * 8)));
	}
	uint8_t mk[4] = { 0, 0, 0, 0 };
	if (mask)
		rand_bytes(mk, 4);
	if (mask)
		o.insert(o.end(), mk, mk + 4);
	size_t start = o.size();
	if (n && p)
		o.insert(o.end(), p, p + n);
	if (mask) {
		for (size_t i = 0; i < n; i++)
			o[start + i] ^= mk[i % 4];
	}
	return o;
}

bool ws_parse(const uint8_t *b, size_t n, WsMsg &out)
{
	if (n < 2)
		return false;
	out.opcode = b[0] & 0x0f;
	bool masked = (b[1] & 0x80) != 0;
	uint64_t len = b[1] & 0x7f;
	size_t off = 2;
	if (len == 126) {
		if (n < 4)
			return false;
		len = ((uint64_t)b[2] << 8) | b[3];
		off = 4;
	} else if (len == 127) {
		if (n < 10)
			return false;
		len = 0;
		for (int i = 0; i < 8; i++)
			len = (len << 8) | b[2 + i];
		off = 10;
	}
	if (len > 8ull * 1024ull * 1024ull)
		return false;
	uint8_t mk[4] = { 0, 0, 0, 0 };
	if (masked) {
		if (n < off + 4)
			return false;
		std::memcpy(mk, b + off, 4);
		off += 4;
	}
	if (n < off + (size_t)len)
		return false;
	out.payload.assign(b + off, b + off + (size_t)len);
	if (masked) {
		for (size_t i = 0; i < out.payload.size(); i++)
			out.payload[i] ^= mk[i % 4];
	}
	out.used = off + (size_t)len;
	return true;
}

static std::string ws_accept_key(const std::string &key)
{
	std::string src = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	uint8_t md[20];
	sha1_20((const uint8_t *)src.data(), src.size(), md);
	return b64enc(md, 20);
}

bool ws_server_handshake(sock_t fd, std::string &err)
{
	std::string acc;
	uint8_t tmp[1024];
	while (acc.find("\r\n\r\n") == std::string::npos) {
		int n = recv(fd, (char *)tmp, sizeof(tmp), 0);
		if (n <= 0) {
			err = "client closed";
			return false;
		}
		acc.append((char *)tmp, (size_t)n);
		if (acc.size() > 16384) {
			err = "request too large";
			return false;
		}
	}
	std::string low = acc;
	for (char &c : low)
		c = (char)std::tolower((unsigned char)c);
	if (low.find("upgrade: websocket") == std::string::npos) {
		err = "not a WebSocket";
		return false;
	}
	auto line0 = acc.substr(0, acc.find("\r\n"));
	if (line0.find("/posix-net") == std::string::npos) {
		err = "expected path /posix-net";
		return false;
	}
	std::string key;
	auto kpos = low.find("sec-websocket-key:");
	if (kpos != std::string::npos) {
		auto start = acc.find(':', kpos) + 1;
		while (start < acc.size() && (unsigned char)acc[start] <= 32)
			start++;
		auto end = acc.find("\r\n", start);
		key = acc.substr(start, end - start);
		while (!key.empty() && (unsigned char)key.back() <= 32)
			key.pop_back();
	}
	if (key.empty()) {
		err = "missing Sec-WebSocket-Key";
		return false;
	}
	std::string accept = ws_accept_key(key);
	std::string resp =
		"HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Accept: " + accept + "\r\n"
		"Sec-WebSocket-Protocol: binary\r\n"
		"\r\n";
	size_t off = 0;
	while (off < resp.size()) {
		int n = send(fd, resp.data() + off, (int)(resp.size() - off), 0);
		if (n <= 0) {
			err = "failed to send 101";
			return false;
		}
		off += (size_t)n;
	}
	return true;
}

static std::string cookie_from_raw(const std::string &resp)
{
	std::string low = resp;
	for (char &c : low)
		c = (char)std::tolower((unsigned char)c);
	size_t pos = 0;
	while ((pos = low.find("set-cookie:", pos)) != std::string::npos) {
		size_t line = resp.find("\r\n", pos);
		std::string val = resp.substr(pos + 11, line == std::string::npos ? std::string::npos : line - (pos + 11));
		while (!val.empty() && (unsigned char)val.front() <= 32)
			val.erase(val.begin());
		auto sid = val.find("chiaki_sid=");
		if (sid == std::string::npos) {
			pos += 11;
			continue;
		}
		val = val.substr(sid);
		auto sc = val.find(';');
		if (sc != std::string::npos)
			val = val.substr(0, sc);
		return val;
	}
	return {};
}

#ifdef _WIN32

static std::wstring wide(const std::string &s)
{
	if (s.empty())
		return L"";
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
	std::wstring w((size_t)n, 0);
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
	return w;
}

struct CloudWs::Impl {
	HINTERNET session = nullptr;
	HINTERNET conn = nullptr;
	HINTERNET ws = nullptr;
	std::mutex mu;
	std::condition_variable cv;
	std::queue<std::vector<uint8_t>> q;
	std::thread th;
	std::atomic<bool> alive{false};
	std::atomic<bool> dead{false};
	bool ok = false;

	~Impl() { reset(); }

	void reset()
	{
		ok = false;
		alive = false;
		if (ws)
			WinHttpWebSocketClose(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
		if (th.joinable())
			th.join();
		if (ws) {
			WinHttpCloseHandle(ws);
			ws = nullptr;
		}
		if (conn) {
			WinHttpCloseHandle(conn);
			conn = nullptr;
		}
		if (session) {
			WinHttpCloseHandle(session);
			session = nullptr;
		}
		dead = false;
		std::lock_guard<std::mutex> g(mu);
		while (!q.empty())
			q.pop();
	}

	void start_recv()
	{
		alive = true;
		dead = false;
		th = std::thread([this]() {
			std::vector<uint8_t> buf(64 * 1024);
			std::vector<uint8_t> acc;
			while (alive) {
				DWORD got = 0;
				WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
				DWORD e = WinHttpWebSocketReceive(ws, buf.data(), (DWORD)buf.size(), &got, &type);
				if (e != ERROR_SUCCESS) {
					dead = true;
					cv.notify_all();
					break;
				}
				if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
					dead = true;
					cv.notify_all();
					break;
				}
				acc.insert(acc.end(), buf.begin(), buf.begin() + got);
				if (type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE ||
					type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
					{
						std::lock_guard<std::mutex> g(mu);
						q.push(std::move(acc));
					}
					acc.clear();
					cv.notify_all();
				}
			}
		});
	}
};

CloudWs::CloudWs() : impl_(std::make_unique<Impl>()) {}
CloudWs::~CloudWs() = default;

bool CloudWs::ok() const { return impl_ && impl_->ok; }

void CloudWs::close()
{
	if (impl_)
		impl_->reset();
}

bool CloudWs::login(const CloudUrl &url, const std::string &email, const std::string &password,
	bool insecure, const std::string &, std::string &cookie, std::string &err)
{
	std::string body = std::string("{\"email\":\"") + json_escape(email) + "\",\"password\":\"" +
		json_escape(password) + "\"}";
	HINTERNET ses = WinHttpOpen(L"Chiaki-Proxy/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!ses) {
		err = "WinHTTP: cannot open session";
		return false;
	}
	DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
	WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
	if (insecure) {
		DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
			SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
		WinHttpSetOption(ses, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));
	}
	auto whost = wide(url.host);
	HINTERNET con = WinHttpConnect(ses, whost.c_str(), url.port, 0);
	if (!con) {
		WinHttpCloseHandle(ses);
		err = "WinHTTP: cannot connect";
		return false;
	}
	DWORD flags = url.tls ? WINHTTP_FLAG_SECURE : 0;
	HINTERNET req = WinHttpOpenRequest(con, L"POST", L"/api/login", nullptr, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
	if (!req) {
		WinHttpCloseHandle(con);
		WinHttpCloseHandle(ses);
		err = "WinHTTP: cannot create login request";
		return false;
	}
	if (insecure && url.tls) {
		DWORD sec = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
			SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
		WinHttpSetOption(req, WINHTTP_OPTION_SECURITY_FLAGS, &sec, sizeof(sec));
	}
	std::wstring hdrs = L"Content-Type: application/json\r\n";
	BOOL sent = WinHttpSendRequest(req, hdrs.c_str(), (DWORD)-1L, (LPVOID)body.data(),
		(DWORD)body.size(), (DWORD)body.size(), 0);
	if (!sent || !WinHttpReceiveResponse(req, nullptr)) {
		WinHttpCloseHandle(req);
		WinHttpCloseHandle(con);
		WinHttpCloseHandle(ses);
		err = "WinHTTP: no login response";
		return false;
	}
	DWORD status = 0, slen = sizeof(status);
	WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status, &slen, WINHTTP_NO_HEADER_INDEX);
	DWORD hsz = 0;
	WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
		nullptr, &hsz, WINHTTP_NO_HEADER_INDEX);
	std::wstring raw(hsz / sizeof(wchar_t), 0);
	WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
		raw.data(), &hsz, WINHTTP_NO_HEADER_INDEX);
	int nbytes = WideCharToMultiByte(CP_UTF8, 0, raw.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string headers(nbytes > 0 ? nbytes - 1 : 0, 0);
	if (nbytes > 1)
		WideCharToMultiByte(CP_UTF8, 0, raw.c_str(), -1, headers.data(), nbytes, nullptr, nullptr);

	WinHttpCloseHandle(req);
	WinHttpCloseHandle(con);
	WinHttpCloseHandle(ses);

	if (status != 200) {
		if (status == 401)
			err = "Invalid email or password.";
		else
			err = "Login HTTP " + std::to_string(status);
		return false;
	}
	cookie = cookie_from_raw(headers);
	if (cookie.empty()) {
		err = "No session cookie in the response.";
		return false;
	}
	return true;
}

bool CloudWs::open_agent(const CloudUrl &url, const std::string &cookie, bool insecure,
	const std::string &, std::string &err)
{
	impl_->reset();
	impl_->session = WinHttpOpen(L"Chiaki-Proxy/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!impl_->session) {
		err = "WinHTTP: cannot open WebSocket session";
		return false;
	}
	DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
	WinHttpSetOption(impl_->session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
	auto whost = wide(url.host);
	impl_->conn = WinHttpConnect(impl_->session, whost.c_str(), url.port, 0);
	if (!impl_->conn) {
		err = "WinHTTP: cannot connect agent";
		impl_->reset();
		return false;
	}
	DWORD flags = url.tls ? WINHTTP_FLAG_SECURE : 0;
	HINTERNET req = WinHttpOpenRequest(impl_->conn, L"GET", L"/posix-net?agent=1", nullptr,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
	if (!req) {
		err = "WinHTTP: GET /posix-net?agent=1 failed";
		impl_->reset();
		return false;
	}
	if (insecure && url.tls) {
		DWORD sec = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
			SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
		WinHttpSetOption(req, WINHTTP_OPTION_SECURITY_FLAGS, &sec, sizeof(sec));
	}
	if (!WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
		WinHttpCloseHandle(req);
		err = "WinHTTP: WebSocket upgrade rejected";
		impl_->reset();
		return false;
	}
	std::wstring extra = L"Sec-WebSocket-Protocol: binary\r\nCookie: ";
	extra += wide(cookie);
	extra += L"\r\n";
	if (!WinHttpSendRequest(req, extra.c_str(), (DWORD)-1L, nullptr, 0, 0, 0) ||
		!WinHttpReceiveResponse(req, nullptr)) {
		WinHttpCloseHandle(req);
		err = "WebSocket handshake: no response";
		impl_->reset();
		return false;
	}
	DWORD status = 0, slen = sizeof(status);
	WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status, &slen, WINHTTP_NO_HEADER_INDEX);
	if (status != 101) {
		WinHttpCloseHandle(req);
		err = status == 401 ? "WebSocket handshake: 401 (session expired)."
			: status == 404 ? "HTTP 404 tunnel: the reverse proxy must forward /posix-net (same as the site)."
			: ("WebSocket handshake HTTP " + std::to_string(status));
		impl_->reset();
		return false;
	}
	impl_->ws = WinHttpWebSocketCompleteUpgrade(req, 0);
	WinHttpCloseHandle(req);
	if (!impl_->ws) {
		err = "WinHttpWebSocketCompleteUpgrade failed";
		impl_->reset();
		return false;
	}
	impl_->ok = true;
	impl_->start_recv();
	return true;
}

bool CloudWs::send_msg(const uint8_t *p, size_t n)
{
	if (!impl_ || !impl_->ws || !impl_->ok)
		return false;
	DWORD e = WinHttpWebSocketSend(impl_->ws, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
		(PVOID)p, (DWORD)n);
	return e == ERROR_SUCCESS;
}

int CloudWs::recv_msg(std::vector<uint8_t> &out, int timeout_ms)
{
	std::unique_lock<std::mutex> lk(impl_->mu);
	if (!impl_->cv.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&]() {
		return !impl_->q.empty() || impl_->dead || !impl_->alive;
	}))
		return 0;
	if (!impl_->q.empty()) {
		out = std::move(impl_->q.front());
		impl_->q.pop();
		return 1;
	}
	return -1;
}

#else

struct CloudWs::Impl {
	sock_t fd = INVALID_SOCK;
	SSL_CTX *ctx = nullptr;
	SSL *ssl = nullptr;
	bool tls = false;
	bool ok = false;
	std::vector<uint8_t> inbox;

	~Impl() { reset(); }

	void reset()
	{
		ok = false;
		if (ssl) {
			SSL_shutdown(ssl);
			SSL_free(ssl);
			ssl = nullptr;
		}
		if (ctx) {
			SSL_CTX_free(ctx);
			ctx = nullptr;
		}
		if (fd != INVALID_SOCK) {
			sock_close(fd);
			fd = INVALID_SOCK;
		}
		inbox.clear();
	}

	int raw_read(void *buf, int n)
	{
		if (tls && ssl)
			return SSL_read(ssl, buf, n);
		return recv(fd, (char *)buf, n, 0);
	}
	int raw_write(const void *buf, int n)
	{
		if (tls && ssl)
			return SSL_write(ssl, buf, n);
		return send(fd, (const char *)buf, n, 0);
	}

	bool connect_tcp(const CloudUrl &url, bool insecure, const std::string &ca_file, std::string &err)
	{
		reset();
		tls = url.tls;
		addrinfo hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		addrinfo *res = nullptr;
		char pbuf[16];
		std::snprintf(pbuf, sizeof(pbuf), "%u", url.port);
		if (getaddrinfo(url.host.c_str(), pbuf, &hints, &res) != 0 || !res) {
			err = "DNS: cannot resolve " + url.host;
			return false;
		}
		fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (fd == INVALID_SOCK) {
			freeaddrinfo(res);
			err = "socket() failed";
			return false;
		}
		set_tcp_opts(fd);
		if (connect(fd, res->ai_addr, (int)res->ai_addrlen) != 0) {
			err = std::string("TCP connection to ") + url.host + ":" + pbuf + " refused";
			sock_close(fd);
			fd = INVALID_SOCK;
			freeaddrinfo(res);
			return false;
		}
		freeaddrinfo(res);
		if (!tls)
			return true;
		ctx = SSL_CTX_new(TLS_client_method());
		if (!ctx) {
			err = "OpenSSL: SSL_CTX_new failed";
			reset();
			return false;
		}
		SSL_CTX_set_default_verify_paths(ctx);
		if (!ca_file.empty())
			SSL_CTX_load_verify_locations(ctx, ca_file.c_str(), nullptr);
		SSL_CTX_set_verify(ctx, insecure ? SSL_VERIFY_NONE : SSL_VERIFY_PEER, nullptr);
		ssl = SSL_new(ctx);
		SSL_set_tlsext_host_name(ssl, url.host.c_str());
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
		SSL_set1_host(ssl, url.host.c_str());
#endif
		SSL_set_fd(ssl, (int)fd);
		if (SSL_connect(ssl) != 1) {
			char buf[256];
			ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
			err = std::string("TLS handshake failed (") + buf + ")";
			reset();
			return false;
		}
		return true;
	}

	std::string http(const std::string &request, std::string &err)
	{
		size_t off = 0;
		while (off < request.size()) {
			int n = raw_write(request.data() + off, (int)(request.size() - off));
			if (n <= 0) {
				err = "HTTP send failed";
				return {};
			}
			off += (size_t)n;
		}
		std::string acc;
		uint8_t tmp[4096];
		while (acc.find("\r\n\r\n") == std::string::npos) {
			int n = raw_read(tmp, sizeof(tmp));
			if (n <= 0) {
				err = "Connection closed while reading HTTP";
				return {};
			}
			acc.append((char *)tmp, (size_t)n);
		}
		auto hdr_end = acc.find("\r\n\r\n");
		std::string lower = acc.substr(0, hdr_end);
		for (char &ch : lower)
			ch = (char)std::tolower((unsigned char)ch);
		auto cl = lower.find("content-length:");
		if (cl != std::string::npos) {
			int need = std::atoi(lower.c_str() + lower.find(':', cl) + 1);
			while ((int)acc.size() < (int)hdr_end + 4 + std::max(0, need)) {
				int n = raw_read(tmp, sizeof(tmp));
				if (n <= 0)
					break;
				acc.append((char *)tmp, (size_t)n);
			}
		}
		return acc;
	}
};

CloudWs::CloudWs() : impl_(std::make_unique<Impl>()) {}
CloudWs::~CloudWs() = default;

bool CloudWs::ok() const { return impl_ && impl_->ok; }

void CloudWs::close()
{
	if (impl_)
		impl_->reset();
}

bool CloudWs::login(const CloudUrl &url, const std::string &email, const std::string &password,
	bool insecure, const std::string &ca_file, std::string &cookie, std::string &err)
{
	Impl box;
	if (!box.connect_tcp(url, insecure, ca_file, err))
		return false;
	std::string body = std::string("{\"email\":\"") + json_escape(email) + "\",\"password\":\"" +
		json_escape(password) + "\"}";
	std::string host = url.host;
	if ((url.tls && url.port != 443) || (!url.tls && url.port != 80))
		host += ":" + std::to_string(url.port);
	std::ostringstream req;
	req << "POST /api/login HTTP/1.1\r\nHost: " << host << "\r\nContent-Type: application/json\r\n"
		<< "Content-Length: " << body.size() << "\r\nConnection: close\r\n\r\n" << body;
	std::string resp = box.http(req.str(), err);
	if (resp.empty())
		return false;
	if (resp.compare(0, 12, "HTTP/1.1 200") != 0 && resp.compare(0, 12, "HTTP/1.0 200") != 0) {
		if (resp.find("401") != std::string::npos)
			err = "Invalid email or password.";
		else
			err = "Login rejected";
		return false;
	}
	cookie = cookie_from_raw(resp);
	if (cookie.empty()) {
		err = "No session cookie in the response.";
		return false;
	}
	return true;
}

bool CloudWs::open_agent(const CloudUrl &url, const std::string &cookie, bool insecure,
	const std::string &ca_file, std::string &err)
{
	if (!impl_->connect_tcp(url, insecure, ca_file, err))
		return false;
	uint8_t key[16];
	rand_bytes(key, 16);
	std::string key_b64 = b64enc(key, 16);
	std::string host = url.host;
	if ((url.tls && url.port != 443) || (!url.tls && url.port != 80))
		host += ":" + std::to_string(url.port);
	std::ostringstream req;
	req << "GET /posix-net?agent=1 HTTP/1.1\r\nHost: " << host << "\r\nUpgrade: websocket\r\n"
		<< "Connection: Upgrade\r\nSec-WebSocket-Key: " << key_b64
		<< "\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: binary\r\n";
	if (!cookie.empty())
		req << "Cookie: " << cookie << "\r\n";
	req << "\r\n";
	std::string r = req.str();
	size_t off = 0;
	while (off < r.size()) {
		int n = impl_->raw_write(r.data() + off, (int)(r.size() - off));
		if (n <= 0) {
			err = "WebSocket handshake: send failed";
			impl_->reset();
			return false;
		}
		off += (size_t)n;
	}
	std::string acc;
	uint8_t tmp[1024];
	while (acc.find("\r\n\r\n") == std::string::npos) {
		int n = impl_->raw_read(tmp, sizeof(tmp));
		if (n <= 0) {
			err = "WebSocket handshake: no response";
			impl_->reset();
			return false;
		}
		acc.append((char *)tmp, (size_t)n);
	}
	if (acc.find(" 101 ") == std::string::npos && acc.compare(0, 12, "HTTP/1.1 101") != 0) {
		err = acc.find("401") != std::string::npos ? "WebSocket handshake: 401 (session expired)."
			: acc.find("404") != std::string::npos ? "HTTP 404 tunnel: the reverse proxy must forward /posix-net (same as the site)."
			: "WebSocket handshake rejected";
		impl_->reset();
		return false;
	}
	auto hdr_end = acc.find("\r\n\r\n");
	if (hdr_end != std::string::npos && hdr_end + 4 < acc.size()) {
		auto extra = acc.substr(hdr_end + 4);
		impl_->inbox.assign(extra.begin(), extra.end());
	}
	set_nonblock(impl_->fd);
	impl_->ok = true;
	return true;
}

bool CloudWs::send_msg(const uint8_t *p, size_t n)
{
	if (!impl_ || !impl_->ok)
		return false;
	auto fr = ws_frame(0x2, p, n, true);
	size_t off = 0;
	while (off < fr.size()) {
		int w = impl_->raw_write(fr.data() + off, (int)(fr.size() - off));
		if (w > 0) {
			off += (size_t)w;
			continue;
		}
		if (impl_->ssl) {
			int e = SSL_get_error(impl_->ssl, w);
			if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE)
				continue;
		} else if (sock_would_block()) {
			pollfd pf{ impl_->fd, POLLOUT, 0 };
			if (poll_fds(&pf, 1, 2000) <= 0)
				return false;
			continue;
		}
		return false;
	}
	return true;
}

int CloudWs::recv_msg(std::vector<uint8_t> &out, int timeout_ms)
{
	if (!impl_ || !impl_->ok)
		return -1;
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
	for (;;) {
		WsMsg msg;
		if (ws_parse(impl_->inbox.data(), impl_->inbox.size(), msg)) {
			impl_->inbox.erase(impl_->inbox.begin(), impl_->inbox.begin() + (std::ptrdiff_t)msg.used);
			if (msg.opcode == 0x8)
				return -1;
			if (msg.opcode == 0x9) {
				auto pong = ws_frame(0xA, msg.payload.data(), msg.payload.size(), true);
				size_t off = 0;
				while (off < pong.size()) {
					int w = impl_->raw_write(pong.data() + off, (int)(pong.size() - off));
					if (w > 0)
						off += (size_t)w;
					else
						break;
				}
				continue;
			}
			if (msg.opcode == 0x1 || msg.opcode == 0x2) {
				out = std::move(msg.payload);
				return 1;
			}
			continue;
		}
		auto now = std::chrono::steady_clock::now();
		if (now >= deadline)
			return 0;
		int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
		pollfd pf{ impl_->fd, POLLIN, 0 };
		int pr = poll_fds(&pf, 1, std::max(1, left));
		if (pr <= 0)
			return 0;
		uint8_t tmp[16384];
		int n = impl_->raw_read(tmp, sizeof(tmp));
		if (n > 0) {
			impl_->inbox.insert(impl_->inbox.end(), tmp, tmp + n);
			continue;
		}
		if (impl_->ssl) {
			int e = SSL_get_error(impl_->ssl, n);
			if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE)
				continue;
		} else if (sock_would_block()) {
			return 0;
		}
		return -1;
	}
}

#endif
