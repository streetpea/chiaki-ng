#include "posix.hpp"
#include "ws.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <thread>
#include <unordered_map>

#ifdef _WIN32
#  include <direct.h>
#  define getcwd_x _getcwd
#else
#  include <signal.h>
#  include <sys/stat.h>
#  include <termios.h>
#  define getcwd_x getcwd
#endif

#ifdef CHIAKI_PROXY_APPLE
#  include <mach-o/dyld.h>
#endif

std::string parent_dir(const std::string &p)
{
	auto pos = p.find_last_of("/\\");
	if (pos == std::string::npos || pos == 0)
		return p;
	return p.substr(0, pos);
}

std::string join_path(const std::string &a, const std::string &b)
{
	if (a.empty())
		return b;
	char sep = '/';
#ifdef _WIN32
	sep = '\\';
#endif
	if (a.back() == '/' || a.back() == '\\')
		return a + b;
	return a + sep + b;
}

static std::string cwd_path()
{
	char buf[4096];
	if (!getcwd_x(buf, sizeof(buf)))
		return ".";
	return buf;
}

static std::string exe_dir()
{
#ifdef _WIN32
	char buf[MAX_PATH];
	DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
	if (!n)
		return cwd_path();
	return parent_dir(std::string(buf, n));
#elif defined(CHIAKI_PROXY_APPLE)
	char buf[4096];
	uint32_t sz = sizeof(buf);
	if (_NSGetExecutablePath(buf, &sz) != 0)
		return cwd_path();
	char real[4096];
	if (realpath(buf, real))
		return parent_dir(real);
	return parent_dir(buf);
#else
	char buf[4096];
	ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n <= 0)
		return cwd_path();
	buf[n] = 0;
	return parent_dir(buf);
#endif
}

static std::map<std::string, std::string> parse_env_text(const std::string &text)
{
	std::map<std::string, std::string> out;
	std::string line;
	std::istringstream in(text);
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		size_t i = 0;
		while (i < line.size() && (unsigned char)line[i] <= 32)
			i++;
		if (i >= line.size() || line[i] == '#')
			continue;
		auto eq = line.find('=', i);
		if (eq == std::string::npos || eq == i)
			continue;
		std::string key = line.substr(i, eq - i);
		while (!key.empty() && (unsigned char)key.back() <= 32)
			key.pop_back();
		std::string val = line.substr(eq + 1);
		while (!val.empty() && (unsigned char)val.front() <= 32)
			val.erase(val.begin());
		while (!val.empty() && (unsigned char)val.back() <= 32)
			val.pop_back();
		if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') || (val.front() == '\'' && val.back() == '\'')))
			val = val.substr(1, val.size() - 2);
		out[key] = val;
	}
	return out;
}

static bool file_exists(const std::string &p)
{
	std::ifstream f(p);
	return (bool)f;
}

void load_dotenv()
{
	std::vector<std::string> cands;
	if (const char *forced = std::getenv("CHIAKI_ENV_FILE"))
		cands.push_back(forced);
	std::string cwd = cwd_path();
	std::string ex = exe_dir();
	cands.push_back(join_path(cwd, ".env"));
	cands.push_back(join_path(ex, ".env"));
	std::string d = cwd;
	for (int i = 0; i < 8; i++) {
		d = parent_dir(d);
		cands.push_back(join_path(d, ".env"));
	}
	d = ex;
	for (int i = 0; i < 8; i++) {
		d = parent_dir(d);
		cands.push_back(join_path(d, ".env"));
	}
	std::string found;
	for (const auto &p : cands) {
		if (p.empty())
			continue;
		if (file_exists(p)) {
			found = p;
			break;
		}
	}
	if (found.empty())
		return;
	std::ifstream f(found, std::ios::binary);
	std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	auto parsed = parse_env_text(text);
#ifdef _WIN32
	for (const auto &kv : parsed)
		_putenv_s(kv.first.c_str(), kv.second.c_str());
#else
	for (const auto &kv : parsed)
		setenv(kv.first.c_str(), kv.second.c_str(), 1);
#endif
	static std::string last;
	if (last != found) {
		last = found;
		logf("Config: %s\n", found.c_str());
	}
}

void sleep_ms(int ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

std::string machine_name()
{
	char buf[256] = { 0 };
#ifdef _WIN32
	DWORD n = sizeof(buf);
	if (GetComputerNameA(buf, &n))
		return buf;
#else
	if (gethostname(buf, sizeof(buf) - 1) == 0)
		return buf;
#endif
	return "chiaki-proxy";
}

int tcp_probe(const char *ip, uint16_t port, int ms)
{
	sock_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCK)
		return 0;
	set_nonblock(s);
	sockaddr_in a{};
	a.sin_family = AF_INET;
	a.sin_port = htons(port);
	a.sin_addr = u32_to_in_addr(ip_to_u32(ip));
	int rc = connect(s, (sockaddr *)&a, sizeof(a));
	if (rc == 0) {
		sock_close(s);
		return 1;
	}
	if (!sock_would_block() && sock_err() != SOCK_EINPROGRESS) {
		sock_close(s);
		return 2;
	}
	pollfd p{};
	p.fd = s;
	p.events = POLLOUT;
	int pr = poll_fds(&p, 1, ms);
	if (pr <= 0) {
		sock_close(s);
		return 0;
	}
	int err = 0;
	socklen_t el = sizeof(err);
	getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &el);
	sock_close(s);
	return err == 0 ? 1 : 2;
}

#ifdef _WIN32
static BOOL WINAPI on_ctrl(DWORD)
{
	g_run = false;
	return TRUE;
}
#else
static void on_sig(int)
{
	g_run = false;
}
#endif

static void send_agent(CloudWs &ws, uint8_t type, uint32_t sid, uint32_t extra,
	const uint8_t *p, uint32_t n)
{
	auto pkt = encode_agent(type, sid, extra, p, n);
	ws.send_msg(pkt.data(), pkt.size());
}

static bool run_agent_loop(CloudWs &ws)
{
	std::unordered_map<uint32_t, std::unique_ptr<PosixBridge>> sessions;
	auto hello = std::string("{\"v\":1,\"name\":\"") + json_escape(machine_name()) + "\"}";
	send_agent(ws, A_HELLO, 0, 0, (const uint8_t *)hello.data(), (uint32_t)hello.size());

	auto last_ping = std::chrono::steady_clock::now();
	bool hello_ok = false;

	while (g_run && ws.ok()) {
		std::vector<uint8_t> payload;
		int got = ws.recv_msg(payload, 5);
		if (got < 0)
			return hello_ok;
		if (got > 0 && payload.size() >= AGENT_HDR) {
			AgentHdr ah = read_agent_hdr(payload.data());
			const uint8_t *pay = payload.data() + AGENT_HDR;
			if (AGENT_HDR + ah.len > payload.size()) {
				/* ignore truncated */
			} else if (ah.type == A_PING) {
				send_agent(ws, A_PONG, ah.sid, 0, nullptr, 0);
			} else if (ah.type == A_PONG) {
				/* ok */
			} else if (ah.type == A_HELLO_OK) {
				hello_ok = true;
				if (ah.extra)
					logf("Online. Keep this window open, then play on the website.\n");
				else
					logf("Waiting for approval on the website…\n");
			} else if (ah.type == A_APPROVED) {
				logf("Approved. Online. Keep this window open, then play on the website.\n");
			} else if (ah.type == A_REJECTED) {
				if (ah.extra) {
					logf("Disconnected from the website. Restart the proxy to reconnect.\n");
					g_stay_down = true;
				} else
					logf("Connection declined on the website.\n");
				return false;
			} else if (ah.type == A_OPEN) {
				uint32_t sid = ah.sid;
				auto br = std::make_unique<PosixBridge>([&ws, sid](const uint8_t *d, size_t n) {
					send_agent(ws, A_DATA, sid, 0, d, (uint32_t)n);
				});
				sessions[sid] = std::move(br);
				logf("Session #%u opened\n", sid);
			} else if (ah.type == A_CLOSE) {
				auto it = sessions.find(ah.sid);
				if (it != sessions.end()) {
					it->second->closeAll();
					sessions.erase(it);
					logf("Session #%u closed\n", ah.sid);
				}
			} else if (ah.type == A_DATA) {
				auto it = sessions.find(ah.sid);
				if (it != sessions.end())
					it->second->handle(pay, ah.len);
			} else if (ah.type == A_PORTCHECK) {
				std::string host((const char *)pay, ah.len);
				uint16_t port = ah.extra ? (uint16_t)ah.extra : 9295;
				int st = tcp_probe(host.c_str(), port, 2800);
				logf("TCP probe %s:%u → %s\n", host.c_str(), port,
					st == 1 ? "open" : st == 2 ? "closed" : "filtered");
				send_agent(ws, A_PORTCHECK_RES, ah.sid, (uint32_t)st, nullptr, 0);
			}
		}

		std::vector<pollfd> fds;
		for (auto &kv : sessions)
			kv.second->fillPoll(fds);
		if (!fds.empty())
			poll_fds(fds.data(), (unsigned)fds.size(), 0);
		for (auto &kv : sessions)
			kv.second->dispatchPoll(fds.data(), fds.size());

		auto now = std::chrono::steady_clock::now();
		if (now - last_ping > std::chrono::seconds(20)) {
			send_agent(ws, A_PING, 0, 0, nullptr, 0);
			last_ping = now;
		}
	}
	return hello_ok;
}

static void serve_local_client(sock_t fd)
{
	std::string err;
	if (!ws_server_handshake(fd, err)) {
		logf("Local client rejected: %s\n", err.c_str());
		sock_close(fd);
		return;
	}
	set_nonblock(fd);
	std::vector<uint8_t> inbox, outbox;
	PosixBridge bridge([&outbox](const uint8_t *d, size_t n) {
		auto fr = ws_frame(0x2, d, n, false);
		outbox.insert(outbox.end(), fr.begin(), fr.end());
	});
	logf("Local posix-net client connected\n");
	while (g_run) {
		std::vector<pollfd> fds;
		pollfd ws{};
		ws.fd = fd;
		ws.events = POLLIN;
		if (!outbox.empty())
			ws.events = (short)(POLLIN | POLLOUT);
		fds.push_back(ws);
		bridge.fillPoll(fds);
		int pr = poll_fds(fds.data(), (unsigned)fds.size(), 250);
		if (pr < 0)
			break;
		if (fds[0].revents & (POLLIN | POLLERR | POLLHUP)) {
			uint8_t tmp[16384];
			int n = recv(fd, (char *)tmp, sizeof(tmp), 0);
			if (n <= 0 && !sock_would_block())
				break;
			if (n > 0)
				inbox.insert(inbox.end(), tmp, tmp + n);
		}
		if (!outbox.empty() && (fds[0].revents & POLLOUT || true)) {
			while (!outbox.empty()) {
				int n = send(fd, (char *)outbox.data(), (int)std::min(outbox.size(), (size_t)16384), 0);
				if (n > 0) {
					outbox.erase(outbox.begin(), outbox.begin() + n);
					continue;
				}
				if (sock_would_block())
					break;
				goto done;
			}
		}
		bridge.dispatchPoll(fds.data(), fds.size());
		size_t off = 0;
		while (off < inbox.size()) {
			WsMsg msg;
			if (!ws_parse(inbox.data() + off, inbox.size() - off, msg))
				break;
			off += msg.used;
			if (msg.opcode == 0x8)
				goto done;
			if (msg.opcode == 0x9) {
				auto pong = ws_frame(0xA, msg.payload.data(), msg.payload.size(), false);
				outbox.insert(outbox.end(), pong.begin(), pong.end());
				continue;
			}
			if (msg.opcode == 0x1 || msg.opcode == 0x2)
				bridge.handle(msg.payload.data(), msg.payload.size());
		}
		if (off)
			inbox.erase(inbox.begin(), inbox.begin() + (std::ptrdiff_t)off);
	}
done:
	bridge.closeAll();
	sock_close(fd);
	logf("Local client disconnected\n");
}

static void local_server_thread(std::string bind, uint16_t port)
{
	sock_t ls = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (ls == INVALID_SOCK) {
		logf("Local listen: socket() failed\n");
		return;
	}
	int one = 1;
	setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one));
	sockaddr_in a{};
	a.sin_family = AF_INET;
	a.sin_port = htons(port);
	if (bind.empty() || bind == "127.0.0.1")
		a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	else if (bind == "0.0.0.0")
		a.sin_addr.s_addr = INADDR_ANY;
	else
		a.sin_addr = u32_to_in_addr(ip_to_u32(bind.c_str()));
	if (::bind(ls, (sockaddr *)&a, sizeof(a)) != 0 || listen(ls, 8) != 0) {
		logf("Cannot listen on %s:%u\n", bind.c_str(), port);
		sock_close(ls);
		return;
	}
	set_nonblock(ls);
	logf("Local listen ready.\n");
	while (g_run) {
		pollfd p{};
		p.fd = ls;
		p.events = POLLIN;
		int pr = poll_fds(&p, 1, 250);
		if (pr <= 0)
			continue;
		sockaddr_in cli{};
		socklen_t cl = sizeof(cli);
		sock_t c = accept(ls, (sockaddr *)&cli, &cl);
		if (c == INVALID_SOCK)
			continue;
		std::thread([c]() { serve_local_client(c); }).detach();
	}
	sock_close(ls);
}

static std::string trim_copy(std::string s)
{
	while (!s.empty() && (unsigned char)s.front() <= 32)
		s.erase(s.begin());
	while (!s.empty() && (unsigned char)s.back() <= 32)
		s.pop_back();
	return s;
}

static std::string prompt_line(const char *label, bool hide)
{
	std::fputs(label, stdout);
	std::fflush(stdout);
#ifdef _WIN32
	HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
	DWORD mode = 0;
	BOOL got = GetConsoleMode(h, &mode);
	if (hide && got)
		SetConsoleMode(h, mode & ~ENABLE_ECHO_INPUT);
	std::string line;
	bool ok = (bool)std::getline(std::cin, line) && g_run;
	if (hide && got) {
		SetConsoleMode(h, mode);
		std::fputc('\n', stdout);
		std::fflush(stdout);
	}
	if (!ok)
		return {};
	if (!line.empty() && line.back() == '\r')
		line.pop_back();
	return hide ? line : trim_copy(line);
#else
	termios oldt{};
	bool restored = false;
	if (hide && isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &oldt) == 0) {
		termios newt = oldt;
		newt.c_lflag &= ~static_cast<tcflag_t>(ECHO);
		tcsetattr(STDIN_FILENO, TCSANOW, &newt);
		restored = true;
	}
	std::string line;
	bool ok = (bool)std::getline(std::cin, line) && g_run;
	if (restored) {
		tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
		std::fputc('\n', stdout);
		std::fflush(stdout);
	}
	if (!ok)
		return {};
	if (!line.empty() && line.back() == '\r')
		line.pop_back();
	return hide ? line : trim_copy(line);
#endif
}

static bool prompt_account(std::string &email, std::string &pass)
{
	while (g_run) {
		logf("Chiaki account (same as on the website)\n");
		email = prompt_line("Email: ", false);
		if (!g_run)
			return false;
		if (email.empty()) {
			logf("Email is required.\n");
			continue;
		}
		pass = prompt_line("Password: ", true);
		if (!g_run)
			return false;
		if (pass.empty()) {
			logf("Password is required.\n");
			continue;
		}
		return true;
	}
	return false;
}

int main()
{
	if (!net_init()) {
		std::fprintf(stderr, "Network init failed\n");
		return 1;
	}
#ifdef _WIN32
	SetConsoleOutputCP(65001);
	SetConsoleCP(65001);
	SetConsoleCtrlHandler(on_ctrl, TRUE);
#else
	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);
#endif

	logf("Chiaki Home Proxy 1.1\n");
	logf("Remote Play relay - no router ports to open.\n\n");

	load_dotenv();
	std::string url_s = env_str("CHIAKI_CLOUD_URL", "https://chiaki.csphere.fr");
	bool insecure = env_flag("CHIAKI_CLOUD_INSECURE", false);
	std::string ca = env_str("CHIAKI_CLOUD_CA", "");
	std::string email, pass;
	if (!prompt_account(email, pass)) {
		net_shutdown();
		return 0;
	}

	int local_port = (int)std::strtol(env_str("CHIAKI_PROXY_PORT", "18790").c_str(), nullptr, 10);
	std::string local_bind = env_str("CHIAKI_PROXY_BIND", "127.0.0.1");
	std::thread local_th;
	if (local_port > 0)
		local_th = std::thread(local_server_thread, local_bind, (uint16_t)local_port);

	int backoff = 1;
	while (g_run) {
		CloudUrl url = parse_cloud_url(url_s);
		logf("Connecting to %s …\n", url.host.c_str());
		std::string cookie, err;
		CloudWs cloud;
		if (!cloud.login(url, email, pass, insecure, ca, cookie, err)) {
			logf("Login: %s\n", err.c_str());
			if (!prompt_account(email, pass))
				break;
			backoff = 1;
			continue;
		}
		logf("Signed in. Opening tunnel…\n");
		if (!cloud.open_agent(url, cookie, insecure, ca, err)) {
			logf("%s\n", err.c_str());
			sleep_ms(3000);
			continue;
		}
		logf("Tunnel open. Approve or decline the connection on the website (same account).\n");
		backoff = 1;
		const bool loop_ok = run_agent_loop(cloud);
		cloud.close();
		if (!g_run || g_stay_down)
			break;
		if (!loop_ok)
			backoff = 20;
		logf("Disconnected. Reconnecting in %d s…\n", backoff);
		for (int i = 0; i < backoff && g_run; i++)
			sleep_ms(1000);
		backoff = std::min(backoff * 2, 15);
	}

	g_run = false;
	if (local_th.joinable())
		local_th.join();
	net_shutdown();
	logf("Stopped.\n");
	return 0;
}
