// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Stub PSN holepunch/RUDP helpers for platforms that do not ship curl/json-c/miniupnpc
// (WebAssembly LAN client). Remote-play-over-PSN is not available in this build.

#include <chiaki/remote/holepunch.h>

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static ChiakiErrorCode wasm_holepunch_unsupported(ChiakiLog *log)
{
	if(log)
		CHIAKI_LOGE(log, "PSN holepunch / remote connection is not available in the WASM build");
	return CHIAKI_ERR_UNKNOWN;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_holepunch_list_devices(
	const char *psn_oauth2_token,
	ChiakiHolepunchConsoleType console_type, ChiakiHolepunchDeviceInfo **devices,
	size_t *device_count, ChiakiLog *log)
{
	(void)psn_oauth2_token;
	(void)console_type;
	if(devices)
		*devices = NULL;
	if(device_count)
		*device_count = 0;
	return wasm_holepunch_unsupported(log);
}

CHIAKI_EXPORT void chiaki_holepunch_free_device_list(ChiakiHolepunchDeviceInfo **devices)
{
	if(devices && *devices)
	{
		free(*devices);
		*devices = NULL;
	}
}

CHIAKI_EXPORT ChiakiHolepunchRegistInfo chiaki_get_regist_info(ChiakiHolepunchSession session)
{
	(void)session;
	ChiakiHolepunchRegistInfo info;
	memset(&info, 0, sizeof(info));
	return info;
}

CHIAKI_EXPORT void chiaki_get_ps_selected_addr(ChiakiHolepunchSession session, char *ps_ip)
{
	(void)session;
	if(ps_ip)
		ps_ip[0] = '\0';
}

CHIAKI_EXPORT uint16_t chiaki_get_ps_ctrl_port(ChiakiHolepunchSession session)
{
	(void)session;
	return 0;
}

CHIAKI_EXPORT chiaki_socket_t *chiaki_get_holepunch_sock(ChiakiHolepunchSession session, ChiakiHolepunchPortType type)
{
	(void)session;
	(void)type;
	return NULL;
}

CHIAKI_EXPORT bool chiaki_holepunch_session_get_stun_allocation(
	ChiakiHolepunchSession session, int32_t *allocation_increment, bool *random_allocation)
{
	(void)session;
	if(allocation_increment)
		*allocation_increment = 0;
	if(random_allocation)
		*random_allocation = false;
	return false;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_holepunch_generate_client_device_uid(char *out, size_t *out_size)
{
	if(!out || !out_size || *out_size < 1)
		return CHIAKI_ERR_BUF_TOO_SMALL;
	out[0] = '\0';
	*out_size = 0;
	return CHIAKI_ERR_UNKNOWN;
}

CHIAKI_EXPORT ChiakiHolepunchSession chiaki_holepunch_session_init(const char *psn_oauth2_token, ChiakiLog *log)
{
	(void)psn_oauth2_token;
	wasm_holepunch_unsupported(log);
	return NULL;
}

CHIAKI_EXPORT void chiaki_holepunch_session_force_port_guessing(ChiakiHolepunchSession session, bool enabled)
{
	(void)session;
	(void)enabled;
}

CHIAKI_EXPORT void chiaki_holepunch_session_set_port_guessing_ports(ChiakiHolepunchSession session, int count)
{
	(void)session;
	(void)count;
}

CHIAKI_EXPORT void chiaki_holepunch_session_set_port_guessing_socks(ChiakiHolepunchSession session, int count)
{
	(void)session;
	(void)count;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_holepunch_session_create(ChiakiHolepunchSession session)
{
	(void)session;
	return CHIAKI_ERR_UNKNOWN;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_holepunch_session_start(
	ChiakiHolepunchSession session, const uint8_t *console_uid,
	ChiakiHolepunchConsoleType console_type)
{
	(void)session;
	(void)console_uid;
	(void)console_type;
	return CHIAKI_ERR_UNKNOWN;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_holepunch_upnp_discover(ChiakiHolepunchSession session)
{
	(void)session;
	return CHIAKI_ERR_UNKNOWN;
}

CHIAKI_EXPORT ChiakiErrorCode holepunch_session_create_offer(ChiakiHolepunchSession session)
{
	(void)session;
	return CHIAKI_ERR_UNKNOWN;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_holepunch_session_punch_hole(
	ChiakiHolepunchSession session, ChiakiHolepunchPortType port_type)
{
	(void)session;
	(void)port_type;
	return CHIAKI_ERR_UNKNOWN;
}

CHIAKI_EXPORT void chiaki_holepunch_main_thread_cancel(ChiakiHolepunchSession session, bool stop_thread)
{
	(void)session;
	(void)stop_thread;
}

CHIAKI_EXPORT void chiaki_holepunch_session_fini(ChiakiHolepunchSession session)
{
	(void)session;
}
