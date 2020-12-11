// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_REGIST_H
#define CHIAKI_REGIST_H

#include "common.h"
#include "log.h"
#include "thread.h"
#include "stoppipe.h"
#include "rpcrypt.h"
#include "session.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CHIAKI_PSN_ACCOUNT_ID_SIZE 8

typedef struct chiaki_regist_info_t
{
	ChiakiTarget target;
	const char *host;
	bool broadcast;

	/**
	 * may be null, in which case psn_account_id will be used
	 */
	const char *psn_online_id;

	/**
	 * will be used if psn_online_id is null, for PS4 >= 7.0
	 */
	uint8_t psn_account_id[CHIAKI_PSN_ACCOUNT_ID_SIZE];

	uint32_t pin;
} ChiakiRegistInfo;

typedef struct chiaki_registered_host_t
{
	ChiakiTarget target;
	char ap_ssid[0x30];
	char ap_bssid[0x20];
	char ap_key[0x50];
	char ap_name[0x20];
	uint8_t server_mac[6];
	char server_nickname[0x20];
	char rp_regist_key[CHIAKI_SESSION_AUTH_SIZE]; // must be completely filled (pad with \0)
	uint32_t rp_key_type;
	uint8_t rp_key[0x10];
} ChiakiRegisteredHost;

typedef enum chiaki_regist_event_type_t {
	CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED,
	CHIAKI_REGIST_EVENT_TYPE_FINISHED_FAILED,
	CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS
} ChiakiRegistEventType;

typedef struct chiaki_regist_event_t
{
	ChiakiRegistEventType type;
	ChiakiRegisteredHost *registered_host;
} ChiakiRegistEvent;

typedef void (*ChiakiRegistCb)(ChiakiRegistEvent *event, void *user);

typedef struct chiaki_regist_t
{
	ChiakiLog *log;
	ChiakiRegistInfo info;
	ChiakiRegistCb cb;
	void *cb_user;

	ChiakiThread thread;
	ChiakiStopPipe stop_pipe;
} ChiakiRegist;

CHIAKI_EXPORT ChiakiErrorCode chiaki_regist_start(ChiakiRegist *regist, ChiakiLog *log, const ChiakiRegistInfo *info, ChiakiRegistCb cb, void *cb_user);
CHIAKI_EXPORT void chiaki_regist_fini(ChiakiRegist *regist);
CHIAKI_EXPORT void chiaki_regist_stop(ChiakiRegist *regist);

/**
 * @param psn_account_id must be exactly of size CHIAKI_PSN_ACCOUNT_ID_SIZE
 */
CHIAKI_EXPORT ChiakiErrorCode chiaki_regist_request_payload_format(ChiakiTarget target, const uint8_t *ambassador, uint8_t *buf, size_t *buf_size, ChiakiRPCrypt *crypt, const char *psn_online_id, const uint8_t *psn_account_id, uint32_t pin);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_REGIST_H
