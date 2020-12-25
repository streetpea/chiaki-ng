// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/log.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

CHIAKI_EXPORT char chiaki_log_level_char(ChiakiLogLevel level)
{
	switch(level)
	{
		case CHIAKI_LOG_VERBOSE:
			return 'V';
		case CHIAKI_LOG_DEBUG:
			return 'D';
		case CHIAKI_LOG_INFO:
			return 'I';
		case CHIAKI_LOG_WARNING:
			return 'W';
		case CHIAKI_LOG_ERROR:
			return 'E';
		default:
			return '?';
	}
}

CHIAKI_EXPORT void chiaki_log_init(ChiakiLog *log, uint32_t level_mask, ChiakiLogCb cb, void *user)
{
	log->level_mask = level_mask;
	log->cb = cb;
	log->user = user;
}

CHIAKI_EXPORT void chiaki_log_cb_print(ChiakiLogLevel level, const char *msg, void *user)
{
	(void)user;

	char c = chiaki_log_level_char(level);
	const char *color = NULL;
	switch(level)
	{
		case CHIAKI_LOG_DEBUG:
			color = "34";
			break;
		case CHIAKI_LOG_WARNING:
			color = "33";
			break;
		case CHIAKI_LOG_ERROR:
			color = "31";
			break;
		default:
			break;
	}

	if(color)
		printf("\033[38;5;%sm", color);
	printf("[%c] ", c);
	if(color)
		printf("\033[0m");
	printf("%s\n", msg);
}

CHIAKI_EXPORT void chiaki_log(ChiakiLog *log, ChiakiLogLevel level, const char *fmt, ...)
{
	if(log && !(log->level_mask & level))
		return;

	va_list args;
	char buf[0x100];
	char *msg = buf;

	va_start(args, fmt);
	int written = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	if(written < 0)
		return;

	if(written >= sizeof(buf))
	{
		msg = malloc(written + 1);
		if(!msg)
			return;

		va_start(args, fmt);
		written = vsnprintf(msg, written + 1, fmt, args);
		va_end(args);

		if(written < 0)
		{
			free(msg);
			return;
		}
	}

	ChiakiLogCb cb = log && log->cb ? log->cb : chiaki_log_cb_print;
	void *user = log ? log->user : NULL;
	cb(level, msg, user);

	if(msg != buf)
		free(msg);
}

#define HEXDUMP_WIDTH 0x10

static const char hex_char[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };

CHIAKI_EXPORT void chiaki_log_hexdump(ChiakiLog *log, ChiakiLogLevel level, const uint8_t *buf, size_t buf_size)
{
	if(log && !(log->level_mask & level))
		return;

	chiaki_log(log, level, "offset 0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f  0123456789abcdef");

	size_t offset = 0;

	char hex_buf[HEXDUMP_WIDTH * 3 + 1];
	char ascii_buf[HEXDUMP_WIDTH + 1];
	for(size_t i=0; i<HEXDUMP_WIDTH; i++)
		hex_buf[i*3+2] = ' ';
	hex_buf[HEXDUMP_WIDTH*3] = '\0';
	ascii_buf[HEXDUMP_WIDTH] = '\0';

	while(buf_size > 0)
	{
		for(size_t i=0; i<HEXDUMP_WIDTH; i++)
		{
			if(i < buf_size)
			{
				uint8_t b = buf[i];
				hex_buf[i*3] = hex_char[b >> 4];
				hex_buf[i*3+1] = hex_char[b & 0xf];

				if(b > 0x20 && b < 0x7f)
					ascii_buf[i] = (char)b;
				else
					ascii_buf[i] = '.';
			}
			else
			{
				hex_buf[i*3] = ' ';
				hex_buf[i*3+1] = ' ';
				ascii_buf[i] = ' ';
			}

			hex_buf[i*3+2] = ' ';
		}

		chiaki_log(log, level, "%6x %s%s", offset, hex_buf, ascii_buf);

		if(buf_size > HEXDUMP_WIDTH)
		{
			buf_size -= HEXDUMP_WIDTH;
			buf += HEXDUMP_WIDTH;
			offset += HEXDUMP_WIDTH;
		}
		else
			break;
	}
}

CHIAKI_EXPORT void chiaki_log_hexdump_raw(ChiakiLog *log, ChiakiLogLevel level, const uint8_t *buf, size_t buf_size)
{
	if(log && !(log->level_mask & level))
		return;

	char *str = malloc(buf_size * 2 + 1);
	if(!str)
		return;
	for(size_t i=0; i<buf_size; i++)
	{
		uint8_t v = buf[i];
		str[i*2+0] = hex_char[v >> 4];
		str[i*2+1] = hex_char[v & 0xf];
	}
	str[buf_size*2] = 0;
	chiaki_log(log, level, "%s", str);
	free(str);
}

static void log_sniffer_cb(ChiakiLogLevel level, const char *msg, void *user);

CHIAKI_EXPORT void chiaki_log_sniffer_init(ChiakiLogSniffer *sniffer, uint32_t level_mask, ChiakiLog *forward_log)
{
	sniffer->forward_log = forward_log;
	// level_mask is applied later and everything is forwarded unmasked, so use ALL here:
	chiaki_log_init(&sniffer->sniff_log, CHIAKI_LOG_ALL, log_sniffer_cb, sniffer);
	sniffer->sniff_level_mask = level_mask;
	sniffer->buf = calloc(1, 1);
	sniffer->buf_len = '\0';
}

CHIAKI_EXPORT void chiaki_log_sniffer_fini(ChiakiLogSniffer *sniffer)
{
	free(sniffer->buf);
}

static void log_sniffer_push(ChiakiLogSniffer *sniffer, ChiakiLogLevel level, const char *msg)
{
	size_t len = strlen(msg);
	if(!len)
		return;
	bool nl = sniffer->buf_len != 0;
	char *new_buf = realloc(sniffer->buf, sniffer->buf_len + (nl ? 1 : 0) + 4 + len + 1);
	if(!new_buf)
		return;
	sniffer->buf = new_buf;
	if(nl)
		sniffer->buf[sniffer->buf_len++] = '\n';
	sniffer->buf[sniffer->buf_len++] = '[';
	sniffer->buf[sniffer->buf_len++] = chiaki_log_level_char(level);
	sniffer->buf[sniffer->buf_len++] = ']';
	sniffer->buf[sniffer->buf_len++] = ' ';
	memcpy(sniffer->buf + sniffer->buf_len, msg, len);
	sniffer->buf_len += len;
	sniffer->buf[sniffer->buf_len] = '\0';
}

static void log_sniffer_cb(ChiakiLogLevel level, const char *msg, void *user)
{
	ChiakiLogSniffer *sniffer = user;
	if(level & sniffer->sniff_level_mask)
		log_sniffer_push(sniffer, level, msg);
	if(sniffer->forward_log)
		chiaki_log(sniffer->forward_log, level, "%s", msg);
}
