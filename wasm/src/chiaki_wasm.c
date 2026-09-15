#include "chiaki_wasm_net.h"

#include <chiaki/base64.h>
#include <chiaki/common.h>
#include <chiaki/controller.h>
#include <chiaki/discoveryservice.h>
#include <chiaki/discovery.h>
#include <chiaki/log.h>
#include <chiaki/opusdecoder.h>
#include <chiaki/regist.h>
#include <chiaki/session.h>

#include <emscripten.h>
#include <emscripten/threading.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void chiaki_wasm_session_stop(void);
void chiaki_wasm_discover_stop(void);

static int wasm_spawn(void *(*fn)(void *), void *arg)
{
	if(!emscripten_is_main_browser_thread())
	{
		fn(arg);
		return 0;
	}
	pthread_t t;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	int rc = pthread_create(&t, &attr, fn, arg);
	pthread_attr_destroy(&attr);
	return rc == 0 ? 0 : -1;
}

typedef struct {
	ChiakiLog log;
	ChiakiDiscoveryService discovery;
	int discovery_active;
	ChiakiSession session;
	int session_active;
	ChiakiOpusDecoder opus;
	int opus_ready;
	ChiakiRegist regist;
	int regist_active;
	char host[256];
	pthread_mutex_t mu;
} WasmState;

static WasmState g_state;

static int hex_nibble(char c)
{
	if(c >= '0' && c <= '9')
		return c - '0';
	if(c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if(c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int parse_hex(const char *hex, uint8_t *out, size_t out_len)
{
	size_t n = strlen(hex);
	if(n != out_len * 2)
		return -1;
	for(size_t i = 0; i < out_len; i++)
	{
		int hi = hex_nibble(hex[i * 2]);
		int lo = hex_nibble(hex[i * 2 + 1]);
		if(hi < 0 || lo < 0)
			return -1;
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return 0;
}

static int parse_key16(const char *s, uint8_t *out)
{
	size_t n = strlen(s);
	if(n == 32 && parse_hex(s, out, 16) == 0)
		return 0;
	if(n <= 16)
	{
		memset(out, 0, 16);
		memcpy(out, s, n);
		return 0;
	}
	return -1;
}

static void wasm_log_cb(ChiakiLogLevel level, const char *msg, void *user)
{
	(void)user;
	MAIN_THREAD_EM_ASM({
		if(Module.onLog)
			Module.onLog($0, UTF8ToString($1));
	}, (int)level, msg);
}

static bool video_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user)
{
	(void)user;
	MAIN_THREAD_EM_ASM({
		if(Module.onVideo)
			Module.onVideo($0, $1, $2, $3);
	}, buf, (int)buf_size, frames_lost, frame_recovered ? 1 : 0);
	return true;
}

static void audio_settings_cb(uint32_t channels, uint32_t rate, void *user)
{
	(void)user;
	MAIN_THREAD_EM_ASM({
		if(Module.onAudioSettings)
			Module.onAudioSettings($0, $1);
	}, (int)channels, (int)rate);
}

static void audio_frame_cb(int16_t *buf, size_t samples_count, void *user)
{
	(void)user;
	MAIN_THREAD_EM_ASM({
		if(Module.onAudio)
			Module.onAudio($0, $1);
	}, buf, (int)samples_count);
}

static void event_cb(ChiakiEvent *event, void *user)
{
	(void)user;
	switch(event->type)
	{
		case CHIAKI_EVENT_CONNECTED:
			MAIN_THREAD_EM_ASM({
				if(Module.onEvent)
					Module.onEvent("connected", 0, "");
			});
			break;
		case CHIAKI_EVENT_LOGIN_PIN_REQUEST:
			MAIN_THREAD_EM_ASM({
				if(Module.onEvent)
					Module.onEvent("pin", $0, "");
			}, event->login_pin_request.pin_incorrect ? 1 : 0);
			break;
		case CHIAKI_EVENT_RUMBLE:
			MAIN_THREAD_EM_ASM({
				if(Module.onEvent)
					Module.onEvent("rumble", $0, "" + $1);
			}, event->rumble.left, event->rumble.right);
			break;
		case CHIAKI_EVENT_QUIT:
		{
			const char *detail = event->quit.reason_str;
			if(!detail || !detail[0])
				detail = chiaki_quit_reason_string(event->quit.reason);
			MAIN_THREAD_EM_ASM({
				if(Module.onEvent)
					Module.onEvent("quit", $0, UTF8ToString($1));
			}, (int)event->quit.reason, detail);
			break;
		}
		case CHIAKI_EVENT_NICKNAME_RECEIVED:
			MAIN_THREAD_EM_ASM({
				if(Module.onEvent)
					Module.onEvent("nickname", 0, UTF8ToString($1));
			}, 0, event->server_nickname);
			break;
		default:
			break;
	}
}

static int json_append(char **buf, size_t *len, size_t *cap, const char *s)
{
	size_t n = strlen(s);
	if(*len + n + 1 > *cap)
	{
		size_t nc = *cap ? *cap : 256;
		while(nc < *len + n + 1)
			nc *= 2;
		char *nb = realloc(*buf, nc);
		if(!nb)
			return -1;
		*buf = nb;
		*cap = nc;
	}
	memcpy(*buf + *len, s, n + 1);
	*len += n;
	return 0;
}

static int json_append_escaped(char **buf, size_t *len, size_t *cap, const char *s)
{
	if(json_append(buf, len, cap, "\"") < 0)
		return -1;
	if(!s)
		s = "";
	for(const unsigned char *p = (const unsigned char *)s; *p; p++)
	{
		char tmp[8];
		if(*p == '"' || *p == '\\')
		{
			tmp[0] = '\\';
			tmp[1] = (char)*p;
			tmp[2] = 0;
			if(json_append(buf, len, cap, tmp) < 0)
				return -1;
		}
		else if(*p < 0x20)
		{
			snprintf(tmp, sizeof(tmp), "\\u%04x", *p);
			if(json_append(buf, len, cap, tmp) < 0)
				return -1;
		}
		else
		{
			tmp[0] = (char)*p;
			tmp[1] = 0;
			if(json_append(buf, len, cap, tmp) < 0)
				return -1;
		}
	}
	return json_append(buf, len, cap, "\"");
}

static void discovery_cb(ChiakiDiscoveryHost *hosts, size_t hosts_count, void *user)
{
	(void)user;
	char *json = NULL;
	size_t len = 0;
	size_t cap = 0;
	int ok = json_append(&json, &len, &cap, "[") == 0;
	for(size_t i = 0; ok && i < hosts_count; i++)
	{
		ChiakiDiscoveryHost *h = &hosts[i];
		char port[16];
		snprintf(port, sizeof(port), "%d", (int)h->host_request_port);
		if(i && json_append(&json, &len, &cap, ",") < 0) { ok = 0; break; }
		if(json_append(&json, &len, &cap, "{\"state\":") < 0
			|| json_append_escaped(&json, &len, &cap, chiaki_discovery_host_state_string(h->state)) < 0
			|| json_append(&json, &len, &cap, ",\"addr\":") < 0
			|| json_append_escaped(&json, &len, &cap, h->host_addr ? h->host_addr : "") < 0
			|| json_append(&json, &len, &cap, ",\"name\":") < 0
			|| json_append_escaped(&json, &len, &cap, h->host_name ? h->host_name : "") < 0
			|| json_append(&json, &len, &cap, ",\"id\":") < 0
			|| json_append_escaped(&json, &len, &cap, h->host_id ? h->host_id : "") < 0
			|| json_append(&json, &len, &cap, ",\"type\":") < 0
			|| json_append_escaped(&json, &len, &cap, h->host_type ? h->host_type : "") < 0
			|| json_append(&json, &len, &cap, ",\"systemVersion\":") < 0
			|| json_append_escaped(&json, &len, &cap, h->system_version ? h->system_version : "") < 0
			|| json_append(&json, &len, &cap, ",\"ps5\":") < 0
			|| json_append(&json, &len, &cap, chiaki_discovery_host_is_ps5(h) ? "1" : "0") < 0
			|| json_append(&json, &len, &cap, ",\"requestPort\":") < 0
			|| json_append(&json, &len, &cap, port) < 0
			|| json_append(&json, &len, &cap, ",\"appName\":") < 0
			|| json_append_escaped(&json, &len, &cap, h->running_app_name ? h->running_app_name : "") < 0
			|| json_append(&json, &len, &cap, "}") < 0)
		{
			ok = 0;
			break;
		}
	}
	if(ok && json_append(&json, &len, &cap, "]") < 0)
		ok = 0;
	if(!ok || !json)
	{
		free(json);
		return;
	}
	MAIN_THREAD_EM_ASM({
		const ptr = $0;
		try {
			const hosts = JSON.parse(UTF8ToString(ptr));
			if(Module.onHosts)
				Module.onHosts(hosts);
			else if(Module.onHost)
				hosts.forEach(function(h){ Module.onHost(h); });
		} catch(e) {}
		if(typeof _free === "function")
			_free(ptr);
		else if(Module._free)
			Module._free(ptr);
	}, json);
}

static void regist_cb(ChiakiRegistEvent *event, void *user)
{
	(void)user;
	if(event->type == CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS && event->registered_host)
	{
		ChiakiRegisteredHost *h = event->registered_host;
		char regist_hex[33];
		char morning_hex[33];
		char mac_hex[13];
		for(int i = 0; i < 16; i++)
			sprintf(regist_hex + i * 2, "%02x", (unsigned char)h->rp_regist_key[i]);
		for(int i = 0; i < 16; i++)
			sprintf(morning_hex + i * 2, "%02x", h->rp_key[i]);
		for(int i = 0; i < 6; i++)
			sprintf(mac_hex + i * 2, "%02x", h->server_mac[i]);
		regist_hex[32] = morning_hex[32] = mac_hex[12] = 0;
		MAIN_THREAD_EM_ASM({
			if(Module.onRegist)
				Module.onRegist({
					ok: true,
					nickname: UTF8ToString($0),
					registKey: UTF8ToString($1),
					morning: UTF8ToString($2),
					mac: UTF8ToString($3),
					ps5: $4
				});
		}, h->server_nickname, regist_hex, morning_hex, mac_hex, chiaki_target_is_ps5(h->target) ? 1 : 0);
	}
	else
	{
		const char *reason = event->type == CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED ? "canceled" : "failed";
		MAIN_THREAD_EM_ASM({
			if(Module.onRegist)
				Module.onRegist({ ok: false, error: UTF8ToString($0) });
		}, reason);
	}
}

EMSCRIPTEN_KEEPALIVE
const char *chiaki_wasm_version(void)
{
	return CHIAKI_VERSION;
}

EMSCRIPTEN_KEEPALIVE
int chiaki_wasm_net_ready(void)
{
	return chiaki_wasm_net_is_ready();
}

EMSCRIPTEN_KEEPALIVE
int chiaki_wasm_init(const char *proxy_url)
{
	memset(&g_state, 0, sizeof(g_state));
	pthread_mutex_init(&g_state.mu, NULL);
	chiaki_log_init(&g_state.log, CHIAKI_LOG_ALL & ~CHIAKI_LOG_VERBOSE, wasm_log_cb, NULL);

	if(chiaki_wasm_net_connect(proxy_url) != 0)
	{
		CHIAKI_LOGE(&g_state.log, "Impossible d'ouvrir le WebSocket du proxy POSIX (%s)", proxy_url ? proxy_url : "");
		return -1;
	}

	ChiakiErrorCode err = chiaki_lib_init();
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(&g_state.log, "chiaki_lib_init: %s", chiaki_error_string(err));
		return -1;
	}

	chiaki_opus_decoder_init(&g_state.opus, &g_state.log);
	g_state.opus_ready = 1;
	CHIAKI_LOGI(&g_state.log, "Chiaki WASM %s prêt", CHIAKI_VERSION);
	return 0;
}

EMSCRIPTEN_KEEPALIVE
void chiaki_wasm_fini(void)
{
	chiaki_wasm_session_stop();
	chiaki_wasm_discover_stop();
	if(g_state.opus_ready)
	{
		chiaki_opus_decoder_fini(&g_state.opus);
		g_state.opus_ready = 0;
	}
	chiaki_wasm_net_disconnect();
}

EMSCRIPTEN_KEEPALIVE
void chiaki_wasm_discover_stop(void)
{
	if(!g_state.discovery_active)
		return;
	chiaki_discovery_service_fini(&g_state.discovery);
	g_state.discovery_active = 0;
}

#define WASM_PING_MAX 32

typedef struct {
	char extra[1024];
} DiscoverArgs;

static size_t parse_ping_hosts(const char *csv, struct sockaddr_storage *out, size_t max)
{
	size_t n = 0;
	if(!csv || !csv[0] || !out || max == 0)
		return 0;
	char buf[1024];
	strncpy(buf, csv, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	char *p = buf;
	while(*p && n < max)
	{
		while(*p == ',' || *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
			p++;
		if(!*p)
			break;
		char *start = p;
		while(*p && *p != ',' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
			p++;
		char saved = *p;
		*p = 0;
		char *colon = strrchr(start, ':');
		if(colon && strchr(start, '.'))
			*colon = 0;
		struct in_addr a;
		if(inet_aton(start, &a) && a.s_addr != 0 && a.s_addr != htonl(INADDR_BROADCAST))
		{
			int dup = 0;
			for(size_t i = 0; i < n; i++)
			{
				if(((struct sockaddr_in *)&out[i])->sin_addr.s_addr == a.s_addr)
				{
					dup = 1;
					break;
				}
			}
			if(!dup)
			{
				memset(&out[n], 0, sizeof(out[n]));
				struct sockaddr_in *in = (struct sockaddr_in *)&out[n];
				in->sin_family = AF_INET;
				in->sin_addr = a;
				n++;
			}
		}
		if(!saved)
			break;
		p++;
	}
	return n;
}

static void *discover_start_thread(void *arg)
{
	DiscoverArgs *args = arg;
	if(g_state.discovery_active)
		chiaki_wasm_discover_stop();

	struct sockaddr_storage ping_addrs[WASM_PING_MAX];
	size_t ping_n = parse_ping_hosts(args ? args->extra : NULL, ping_addrs, WASM_PING_MAX);

	ChiakiDiscoveryServiceOptions options;
	memset(&options, 0, sizeof(options));
	options.hosts_max = 32;
	options.host_drop_pings = 12;
	options.ping_ms = 500;
	options.ping_initial_ms = 10;
	options.cb = discovery_cb;
	options.cb_user = &g_state;

	struct sockaddr_in in_addr;
	memset(&in_addr, 0, sizeof(in_addr));
	in_addr.sin_family = AF_INET;
	in_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	struct sockaddr_storage addr;
	memset(&addr, 0, sizeof(addr));
	memcpy(&addr, &in_addr, sizeof(in_addr));
	options.send_addr = &addr;
	options.send_addr_size = sizeof(in_addr);
	if(ping_n)
	{
		options.broadcast_addrs = ping_addrs;
		options.broadcast_num = ping_n;
	}

	ChiakiErrorCode err = chiaki_discovery_service_init(&g_state.discovery, &options, &g_state.log);
	free(args);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(&g_state.log, "Discovery init: %s", chiaki_error_string(err));
		return NULL;
	}
	g_state.discovery_active = 1;
	if(ping_n)
		CHIAKI_LOGI(&g_state.log, "Sondage UDP de %zu console(s) enregistrée(s)", ping_n);
	else
		CHIAKI_LOGI(&g_state.log, "Découverte LAN en cours (UDP broadcast)");
	return NULL;
}

EMSCRIPTEN_KEEPALIVE
int chiaki_wasm_discover_start(const char *extra_hosts)
{
	DiscoverArgs *args = calloc(1, sizeof(*args));
	if(!args)
		return -1;
	if(extra_hosts)
		strncpy(args->extra, extra_hosts, sizeof(args->extra) - 1);
	if(wasm_spawn(discover_start_thread, args) != 0)
	{
		free(args);
		return -1;
	}
	return 0;
}

typedef struct {
	char host[256];
	char regist_key[64];
	int ps5;
} WakeupArgs;

static void *wakeup_thread(void *arg)
{
	WakeupArgs *args = arg;
	uint8_t key[16];
	if(parse_key16(args->regist_key, key) != 0)
	{
		CHIAKI_LOGE(&g_state.log, "Wake-up: regist_key invalide");
		free(args);
		return NULL;
	}
	char cred[17];
	memset(cred, 0, sizeof(cred));
	memcpy(cred, key, 16);
	size_t cred_len = strlen(cred);
	if(cred_len == 0 || cred_len > 8)
	{
		CHIAKI_LOGE(&g_state.log, "Wake-up: regist_key invalide (len=%zu)", cred_len);
		free(args);
		return NULL;
	}
	uint64_t credential = strtoull(cred, NULL, 16);
	ChiakiDiscovery *disc = g_state.discovery_active ? &g_state.discovery.discovery : NULL;
	ChiakiErrorCode err = chiaki_discovery_wakeup(&g_state.log, disc, args->host, credential, args->ps5 != 0);
	if(err != CHIAKI_ERR_SUCCESS)
		CHIAKI_LOGE(&g_state.log, "Wake-up échoué: %s", chiaki_error_string(err));
	else
		CHIAKI_LOGI(&g_state.log, "Wake-up envoyé à %s", args->host);
	free(args);
	return NULL;
}

EMSCRIPTEN_KEEPALIVE
int chiaki_wasm_wakeup(const char *host, const char *regist_key, int ps5)
{
	if(!host || !regist_key)
		return -1;
	WakeupArgs *args = calloc(1, sizeof(*args));
	if(!args)
		return -1;
	strncpy(args->host, host, sizeof(args->host) - 1);
	strncpy(args->regist_key, regist_key, sizeof(args->regist_key) - 1);
	args->ps5 = ps5;
	if(wasm_spawn(wakeup_thread, args) != 0)
	{
		free(args);
		return -1;
	}
	return 0;
}

typedef struct {
	char host[256];
	char psn_id[128];
	uint32_t pin;
	int ps5;
	int broadcast;
} RegistArgs;

static void *regist_thread(void *arg)
{
	RegistArgs *args = arg;
	if(g_state.regist_active)
	{
		chiaki_regist_stop(&g_state.regist);
		chiaki_regist_fini(&g_state.regist);
		g_state.regist_active = 0;
	}

	ChiakiRegistInfo info;
	memset(&info, 0, sizeof(info));
	info.target = args->ps5 ? CHIAKI_TARGET_PS5_1 : CHIAKI_TARGET_PS4_10;
	info.host = args->host;
	info.broadcast = args->broadcast != 0;
	info.pin = args->pin;
	if(args->psn_id[0])
	{
		size_t out_size = sizeof(info.psn_account_id);
		if(chiaki_base64_decode(args->psn_id, strlen(args->psn_id), info.psn_account_id, &out_size) != CHIAKI_ERR_SUCCESS)
		{
			CHIAKI_LOGE(&g_state.log, "Account ID PSN (base64) invalide");
			free(args);
			return NULL;
		}
	}

	ChiakiErrorCode err = chiaki_regist_start(&g_state.regist, &g_state.log, &info, regist_cb, &g_state);
	if(err != CHIAKI_ERR_SUCCESS)
		CHIAKI_LOGE(&g_state.log, "Regist: %s", chiaki_error_string(err));
	else
		g_state.regist_active = 1;
	free(args);
	return NULL;
}

EMSCRIPTEN_KEEPALIVE
int chiaki_wasm_regist(const char *host, uint32_t pin, const char *psn_account_id_b64, int ps5, int broadcast)
{
	if(!host)
		return -1;
	RegistArgs *args = calloc(1, sizeof(*args));
	if(!args)
		return -1;
	strncpy(args->host, host, sizeof(args->host) - 1);
	if(psn_account_id_b64)
		strncpy(args->psn_id, psn_account_id_b64, sizeof(args->psn_id) - 1);
	args->pin = pin;
	args->ps5 = ps5;
	args->broadcast = broadcast;
	if(wasm_spawn(regist_thread, args) != 0)
	{
		free(args);
		return -1;
	}
	return 0;
}

typedef struct {
	char host[256];
	char regist_key[64];
	char morning[64];
	int ps5;
	int resolution;
	int fps;
	int codec;
	int bitrate;
} SessionStartArgs;

static void *session_start_thread(void *arg)
{
	SessionStartArgs *args = arg;
	if(g_state.session_active)
		chiaki_wasm_session_stop();

	ChiakiConnectInfo connect_info;
	memset(&connect_info, 0, sizeof(connect_info));
	connect_info.ps5 = args->ps5 != 0;
	connect_info.host = args->host;
	connect_info.video_profile_auto_downgrade = true;
	connect_info.packet_loss_max = 0.15;
	if(parse_key16(args->regist_key, (uint8_t *)connect_info.regist_key) != 0)
	{
		CHIAKI_LOGE(&g_state.log, "regist_key invalide (hex 32 chars ou ASCII <= 16)");
		free(args);
		return NULL;
	}
	if(parse_key16(args->morning, connect_info.morning) != 0)
	{
		CHIAKI_LOGE(&g_state.log, "morning invalide (hex 32 chars)");
		free(args);
		return NULL;
	}

	ChiakiVideoResolutionPreset res = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
	if(args->resolution >= 1 && args->resolution <= 4)
		res = (ChiakiVideoResolutionPreset)args->resolution;
	ChiakiVideoFPSPreset fps_p = args->fps >= 60 ? CHIAKI_VIDEO_FPS_PRESET_60 : CHIAKI_VIDEO_FPS_PRESET_30;
	chiaki_connect_video_profile_preset(&connect_info.video_profile, res, fps_p);
	if(args->bitrate > 0)
		connect_info.video_profile.bitrate = (unsigned int)args->bitrate;
	connect_info.video_profile.codec = (ChiakiCodec)args->codec;

	ChiakiErrorCode err = chiaki_session_init(&g_state.session, &connect_info, &g_state.log);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(&g_state.log, "session_init: %s", chiaki_error_string(err));
		free(args);
		return NULL;
	}

	chiaki_session_set_event_cb(&g_state.session, event_cb, &g_state);
	chiaki_session_set_video_sample_cb(&g_state.session, video_cb, &g_state);
	chiaki_opus_decoder_set_cb(&g_state.opus, audio_settings_cb, audio_frame_cb, &g_state);
	ChiakiAudioSink sink;
	chiaki_opus_decoder_get_sink(&g_state.opus, &sink);
	chiaki_session_set_audio_sink(&g_state.session, &sink);

	err = chiaki_session_start(&g_state.session);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(&g_state.log, "session_start: %s", chiaki_error_string(err));
		chiaki_session_fini(&g_state.session);
		free(args);
		return NULL;
	}
	strncpy(g_state.host, args->host, sizeof(g_state.host) - 1);
	g_state.session_active = 1;
	free(args);
	return NULL;
}

EMSCRIPTEN_KEEPALIVE
int chiaki_wasm_session_start(const char *host, const char *regist_key, const char *morning,
		int ps5, int resolution, int fps, int codec, int bitrate)
{
	if(!host || !regist_key || !morning)
		return -1;
	SessionStartArgs *args = calloc(1, sizeof(*args));
	if(!args)
		return -1;
	strncpy(args->host, host, sizeof(args->host) - 1);
	strncpy(args->regist_key, regist_key, sizeof(args->regist_key) - 1);
	strncpy(args->morning, morning, sizeof(args->morning) - 1);
	args->ps5 = ps5;
	args->resolution = resolution;
	args->fps = fps;
	args->codec = codec;
	args->bitrate = bitrate;
	if(wasm_spawn(session_start_thread, args) != 0)
	{
		free(args);
		return -1;
	}
	return 0;
}

static int session_stopping;

static void session_stop_join(void)
{
	if(!g_state.session_active)
	{
		session_stopping = 0;
		return;
	}
	chiaki_session_stop(&g_state.session);
	chiaki_session_join(&g_state.session);
	chiaki_session_fini(&g_state.session);
	g_state.session_active = 0;
	session_stopping = 0;
}

static void *session_stop_thread(void *arg)
{
	(void)arg;
	session_stop_join();
	return NULL;
}

EMSCRIPTEN_KEEPALIVE
void chiaki_wasm_session_stop(void)
{
	if(!g_state.session_active || session_stopping)
		return;
	session_stopping = 1;
	if(emscripten_is_main_browser_thread())
	{
		if(wasm_spawn(session_stop_thread, NULL) != 0)
			session_stopping = 0;
		return;
	}
	session_stop_join();
}

EMSCRIPTEN_KEEPALIVE
int chiaki_wasm_session_set_controller(uint32_t buttons, uint8_t l2, uint8_t r2,
		int16_t lx, int16_t ly, int16_t rx, int16_t ry,
		int touch_active, uint16_t touch_x, uint16_t touch_y)
{
	if(!g_state.session_active)
		return -1;
	ChiakiControllerState state;
	chiaki_controller_state_set_idle(&state);
	state.buttons = buttons;
	state.l2_state = l2;
	state.r2_state = r2;
	state.left_x = lx;
	state.left_y = ly;
	state.right_x = rx;
	state.right_y = ry;
	if(touch_active)
		chiaki_controller_state_start_touch(&state, touch_x, touch_y);
	return chiaki_session_set_controller_state(&g_state.session, &state) == CHIAKI_ERR_SUCCESS ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int chiaki_wasm_session_set_pin(const char *pin)
{
	if(!g_state.session_active || !pin)
		return -1;
	return chiaki_session_set_login_pin(&g_state.session, (const uint8_t *)pin, strlen(pin)) == CHIAKI_ERR_SUCCESS ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int chiaki_wasm_session_goto_bed(void)
{
	if(!g_state.session_active)
		return -1;
	return chiaki_session_goto_bed(&g_state.session) == CHIAKI_ERR_SUCCESS ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int chiaki_wasm_session_go_home(void)
{
	if(!g_state.session_active)
		return -1;
	return chiaki_session_go_home(&g_state.session) == CHIAKI_ERR_SUCCESS ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int chiaki_wasm_session_request_idr(void)
{
	if(!g_state.session_active)
		return -1;
	return chiaki_session_request_idr(&g_state.session) == CHIAKI_ERR_SUCCESS ? 0 : -1;
}

int main(void)
{
	return 0;
}
