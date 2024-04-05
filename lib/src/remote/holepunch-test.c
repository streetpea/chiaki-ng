#include <string.h>
#include <time.h>
#include <linux/if_arp.h>
#include <linux/sockios.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <assert.h>

#include <curl/curl.h>
#include <json-c/json_object.h>
#include <json-c/json_tokener.h>
#include <json-c/json_pointer.h>
// TODO: Make UPnP optional
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>

#include <chiaki/remote/holepunch.h>
#include <chiaki/stoppipe.h>
#include <chiaki/thread.h>
#include <chiaki/base64.h>
#include <chiaki/random.h>
#include <chiaki/sock.h>

#include "../utils.h"
#include "stun.h"

#define UUIDV4_STR_LEN 37
typedef enum notification_type_t
{
    NOTIFICATION_TYPE_UNKNOWN = 0,
    // psn:sessionManager:sys:remotePlaySession:created
    NOTIFICATION_TYPE_SESSION_CREATED = 1 << 0,
    // psn:sessionManager:sys:rps:members:created
    NOTIFICATION_TYPE_MEMBER_CREATED = 1 << 1,
    // psn:sessionManager:sys:rps:members:deleted
    NOTIFICATION_TYPE_MEMBER_DELETED = 1 << 2,
    // psn:sessionManager:sys:rps:customData1:updated
    NOTIFICATION_TYPE_CUSTOM_DATA1_UPDATED = 1 << 3,
    // psn:sessionManager:sys:rps:sessionMessage:created
    NOTIFICATION_TYPE_SESSION_MESSAGE_CREATED = 1 << 4
} NotificationType;

typedef struct notification_queue_t
{
    struct notification_queue_t *previous;

    NotificationType type;
    json_object* json;
    char* json_buf;
    size_t json_buf_size;
} Notification;

typedef enum session_state_t
{
    SESSION_STATE_INIT = 0,
    SESSION_STATE_WS_OPEN = 1 << 0,
    SESSION_STATE_CREATED = 1 << 1,
    SESSION_STATE_STARTED = 1 << 2,
    SESSION_STATE_CLIENT_JOINED = 1 << 3,
    SESSION_STATE_DATA_SENT = 1 << 4,
    SESSION_STATE_CONSOLE_JOINED = 1 << 5,
    SESSION_STATE_CUSTOMDATA1_RECEIVED = 1 << 6,
    SESSION_STATE_CTRL_OFFER_RECEIVED = 1 << 7,
    SESSION_STATE_CTRL_OFFER_SENT = 1 << 8,
    SESSION_STATE_CTRL_CONSOLE_ACCEPTED = 1 << 9,
    SESSION_STATE_CTRL_CLIENT_ACCEPTED = 1 << 10,
    SESSION_STATE_CTRL_ESTABLISHED = 1 << 11,
    SESSION_STATE_DATA_OFFER_RECEIVED = 1 << 12,
    SESSION_STATE_DATA_OFFER_SENT = 1 << 13,
    SESSION_STATE_DATA_CONSOLE_ACCEPTED = 1 << 14,
    SESSION_STATE_DATA_CLIENT_ACCEPTED = 1 << 15,
    SESSION_STATE_DATA_ESTABLISHED = 1 << 16
} SessionState;

typedef struct session_t
{
    // TODO: Clean this up, how much of this stuff do we really need?
    char* oauth_header;
    uint8_t console_uid[32];
    ChiakiHolepunchConsoleType console_type;

    chiaki_socket_t sock;

    uint64_t account_id;
    char session_id[UUIDV4_STR_LEN];
    char pushctx_id[UUIDV4_STR_LEN];

    uint16_t sid_local;
    uint16_t sid_console;
    uint8_t hashed_id_local[20];
    uint8_t hashed_id_console[20];

    uint8_t data1[16];
    uint8_t data2[16];
    uint8_t custom_data1[16];

    CURLSH* curl_share;

    char* ws_fqdn;
    ChiakiThread ws_thread;
    Notification* ws_notification_queue;
    bool ws_thread_should_stop;

    ChiakiStopPipe notif_pipe;
    ChiakiMutex notif_mutex;
    ChiakiCond notif_cond;

    SessionState state;
    ChiakiMutex state_mutex;
    ChiakiCond state_cond;

    char* client_addr_static;
    char* client_addr_local;
    chiaki_socket_t client_sock;
    chiaki_socket_t ctrl_sock;
    chiaki_socket_t data_sock;

    ChiakiLog *log;
} Session;
// ================================================================================================
// ================================================================================================
// ================================================================================================
int main(int argc, char **argv)
{
    printf("libchiaki UDP Holepunching Test\n");

    char *oauth_token = NULL;
    FILE *token_file = fopen("/tmp/token.txt", "r");
    if (token_file != NULL)
    {
        char token_buf[256];
        size_t token_len = fread(token_buf, 1, sizeof(token_buf), token_file);
        if (token_len > 0)
        {
            oauth_token = calloc(1, token_len + 1);
            memcpy(oauth_token, token_buf, token_len);
            oauth_token[token_len] = '\0';
        }
        fclose(token_file);
    }
    if (oauth_token == NULL)
    {
        if (argc == 2)
            oauth_token = argv[1];
        else
        {
            fprintf(stderr, "Usage: %s <oauth_token>\n", argv[0]);
            return 1;
        }
    }
    printf("Using OAuth token: %s\n", oauth_token);

    ChiakiLog log;
	chiaki_log_init(&log, CHIAKI_LOG_ALL, chiaki_log_cb_print, NULL);

    // List available devices
    ChiakiHolepunchDeviceInfo *device_info_ps5;
    size_t num_devices_ps5;
    ChiakiHolepunchDeviceInfo *device_info_ps4;
    size_t num_devices_ps4;

    ChiakiErrorCode err = chiaki_holepunch_list_devices(oauth_token, CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, &device_info_ps5, &num_devices_ps5, &log);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        fprintf(stderr, "!! Failed to get PS5 devices\n");
        return 1;
    }
    err = chiaki_holepunch_list_devices(oauth_token, CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS4, &device_info_ps4, &num_devices_ps4, &log);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        fprintf(stderr, "!! Failed to get PS4 devices\n");
        return 1;
    }
    printf(">> Found %ld devices\n", num_devices_ps5 + num_devices_ps4);
    for (size_t i = 0; i < num_devices_ps5; i++)
    {
        ChiakiHolepunchDeviceInfo dev = device_info_ps5[i];
        char duid_str[sizeof(dev.device_uid) * 2 + 1];
        bytes_to_hex(dev.device_uid, sizeof(dev.device_uid), duid_str, sizeof(duid_str));
        printf(
            "%s %s (%s) rp_enabled=%s\n",
            dev.type == CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5 ? "PS5" : "PS4",
            dev.device_name, duid_str, dev.remoteplay_enabled ? "true" : "false");
    }
    for (size_t i = 0; i < num_devices_ps4; i++)
    {
        ChiakiHolepunchDeviceInfo dev = device_info_ps4[i];
        char duid_str[sizeof(dev.device_uid) * 2 + 1];
        bytes_to_hex(dev.device_uid, sizeof(dev.device_uid), duid_str, sizeof(duid_str));
        printf(
            "%s %s (%s) rp_enabled=%s\n",
            dev.type == CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5 ? "PS5" : "PS4",
            dev.device_name, duid_str, dev.remoteplay_enabled ? "true" : "false");
    }

    // Pick device
    uint8_t device_uid[32];
    ChiakiHolepunchConsoleType console_type;
    if (num_devices_ps5 > 0)
    {
        memcpy(device_uid, device_info_ps5[0].device_uid, sizeof(device_uid));
        console_type = CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5;
        printf(">> Using PS5 device %s for session\n", device_info_ps5[0].device_name);
    }
    else if (num_devices_ps4 > 0)
    {
        memcpy(device_uid, device_info_ps4[0].device_uid, sizeof(device_uid));
        console_type = CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS4;
        printf(">> Using PS4 device %s for session\n", device_info_ps5[0].device_name);
    }
    else
    {
        fprintf(stderr, "!! No devices found\n");
        return -1;
    }

    Session *session = chiaki_holepunch_session_init(oauth_token, &log);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        fprintf(stderr, "!! Failed to initialize session\n");
        return -1;
    }
    printf(">> Initialized session\n");

    err = chiaki_holepunch_session_create(session);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        fprintf(stderr, "!! Failed to create session\n");
        return -1;
    }
    printf(">> Created session\n");

    err = chiaki_holepunch_session_start(session, device_uid, console_type);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        fprintf(stderr, "!! Failed to start session\n");
        return -1;
    }
    printf(">> Started session\n");

    chiaki_socket_t ctrl_sock = -1;
    chiaki_socket_t data_sock = -1;
    err = chiaki_holepunch_session_punch_hole(session, CHIAKI_HOLEPUNCH_PORT_TYPE_CTRL, &ctrl_sock);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        fprintf(stderr, "!! Failed to punch hole for control connection.\n");
        return -1;
    }
    printf(">> Punched hole for control connection!\n");

    err = chiaki_holepunch_session_punch_hole(session, CHIAKI_HOLEPUNCH_PORT_TYPE_DATA, &data_sock);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        fprintf(stderr, "!! Failed to punch hole for data connection.\n");
        return -1;
    }
    printf(">> Punched hole for data connection!\n");

    printf(">> Successfully punched holes for all neccessary connections!\n");

cleanup:
    chiaki_holepunch_free_device_list(device_info_ps5);
    chiaki_holepunch_free_device_list(device_info_ps4);

    return 0;
}