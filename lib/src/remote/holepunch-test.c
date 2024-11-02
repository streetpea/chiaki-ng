#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <assert.h>
#include <stdio.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netdb.h>
#endif

#include <chiaki/remote/holepunch.h>
#include "../utils.h"

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
            if(!oauth_token)
                return 1;
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
        format_hex(duid_str, sizeof(duid_str), dev.device_uid, sizeof(dev.device_uid));
        printf(
            "%s %s (%s) rp_enabled=%s\n",
            dev.type == CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5 ? "PS5" : "PS4",
            dev.device_name, duid_str, dev.remoteplay_enabled ? "true" : "false");
    }
    for (size_t i = 0; i < num_devices_ps4; i++)
    {
        ChiakiHolepunchDeviceInfo dev = device_info_ps4[i];
        char duid_str[sizeof(dev.device_uid) * 2 + 1];
        format_hex(duid_str, sizeof(duid_str), dev.device_uid, sizeof(dev.device_uid));
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

    ChiakiHolepunchSession session = chiaki_holepunch_session_init(oauth_token, &log);
    if (!session)
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

	err = holepunch_session_create_offer(session);
	if (err != CHIAKI_ERR_SUCCESS)
	{
		fprintf(stderr, "!! Failed to create offer msg for ctrl connection");
		return err;
	}
    printf(">> Created offer msg for ctrl connection\n");
    err = chiaki_holepunch_session_start(session, device_uid, console_type);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        fprintf(stderr, "!! Failed to start session\n");
        chiaki_holepunch_session_fini(session);
        return -1;
    }
    printf(">> Started session\n");

    err = chiaki_holepunch_session_punch_hole(session, CHIAKI_HOLEPUNCH_PORT_TYPE_CTRL);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        fprintf(stderr, "!! Failed to punch hole for control connection.\n");
        chiaki_holepunch_session_fini(session);
        return -1;
    }
    printf(">> Punched hole for control connection!\n");
	err = holepunch_session_create_offer(session);
	if (err != CHIAKI_ERR_SUCCESS)
	{
		fprintf(stderr, "!! Failed to create offer msg for ctrl connection");
		return err;
	}
    printf(">> Created offer msg for ctrl connection\n");
    err = chiaki_holepunch_session_punch_hole(session, CHIAKI_HOLEPUNCH_PORT_TYPE_DATA);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        fprintf(stderr, "!! Failed to punch hole for data connection.\n");
        chiaki_holepunch_session_fini(session);
        return -1;
    }
    printf(">> Punched hole for data connection!\n");

    printf(">> Successfully punched holes for all neccessary connections!\n");

cleanup:
    chiaki_holepunch_free_device_list(&device_info_ps5);
    chiaki_holepunch_free_device_list(&device_info_ps4);
    chiaki_holepunch_session_fini(session);

    return 0;
}