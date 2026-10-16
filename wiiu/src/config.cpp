// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Serializacion/parseo del config.ini (ver config.h). Sin FS aqui: las
// entradas las hace main.cpp por el cliente FS del log.

#include "config.h"

#include <stdio.h>
#include <string.h>

static void hex_encode(char *dst, const uint8_t *src, size_t n)
{
	for(size_t i = 0; i < n; i++)
		snprintf(dst + i * 2, 3, "%02x", src[i]);
	dst[n * 2] = '\0';
}

static int hex_val(char c)
{
	if(c >= '0' && c <= '9')
		return c - '0';
	if(c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if(c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static bool hex_decode(uint8_t *dst, size_t n, const char *hex)
{
	if(strlen(hex) != n * 2)
		return false;
	for(size_t i = 0; i < n; i++)
	{
		int hi = hex_val(hex[i * 2]);
		int lo = hex_val(hex[i * 2 + 1]);
		if(hi < 0 || lo < 0)
			return false;
		dst[i] = (uint8_t)((hi << 4) | lo);
	}
	return true;
}

// '|' o saltos de linea romperian el formato: se sustituyen.
static void sanitize_field(char *dst, size_t dst_size, const char *src)
{
	size_t i = 0;
	if(src)
	{
		for(; src[i] && i + 1 < dst_size; i++)
			dst[i] = (src[i] == '|' || src[i] == '\n' || src[i] == '\r')
					 ? '_'
					 : src[i];
	}
	dst[i] = '\0';
}

// Copia el campo [start, end) acotado; false si queda vacio.
static bool field_copy(char *dst, size_t dst_size, const char *start,
		       const char *end)
{
	size_t len = (size_t)(end - start);
	if(len >= dst_size)
		len = dst_size - 1;
	memcpy(dst, start, len);
	dst[len] = '\0';
	return len > 0;
}

void config_init(Config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
}

int config_find(const Config *cfg, const char *host)
{
	if(!host || !host[0])
		return -1;
	for(int i = 0; i < CONFIG_MAX_CONSOLES; i++)
	{
		if(cfg->consoles[i].used && !strcmp(cfg->consoles[i].host, host))
			return i;
	}
	return -1;
}

int config_console_count(const Config *cfg)
{
	int n = 0;
	for(int i = 0; i < CONFIG_MAX_CONSOLES; i++)
		n += cfg->consoles[i].used ? 1 : 0;
	return n;
}

int config_set_console(Config *cfg, const char *host, const char *name,
		       const uint8_t regist_key[CONFIG_REGIST_KEY_SIZE],
		       const uint8_t morning[CONFIG_REGIST_KEY_SIZE],
		       const uint8_t account_id[CONFIG_ACCOUNT_ID_SIZE])
{
	if(!host || !host[0])
		return -1;

	int idx = config_find(cfg, host);
	if(idx < 0)
	{
		for(int i = 0; i < CONFIG_MAX_CONSOLES; i++)
		{
			if(!cfg->consoles[i].used)
			{
				idx = i;
				break;
			}
		}
	}
	if(idx < 0)
		return -1;

	ConfigConsole *c = &cfg->consoles[idx];
	c->used = true;
	sanitize_field(c->host, sizeof(c->host), host);
	sanitize_field(c->name, sizeof(c->name), name);
	memcpy(c->regist_key, regist_key, CONFIG_REGIST_KEY_SIZE);
	memcpy(c->morning, morning, CONFIG_REGIST_KEY_SIZE);
	memcpy(c->account_id, account_id, CONFIG_ACCOUNT_ID_SIZE);
	return idx;
}

size_t config_serialize(const Config *cfg, char *buf, size_t buf_size)
{
	if(buf_size < 2)
		return 0;

	size_t off = (size_t)snprintf(buf, buf_size, "account_id=%s\nlast_host=%s\n",
				      cfg->account_id_hex, cfg->last_host);
	if(off >= buf_size)
		off = buf_size - 1;

	char key_hex[CONFIG_REGIST_KEY_SIZE * 2 + 1];
	char morning_hex[CONFIG_REGIST_KEY_SIZE * 2 + 1];
	char acc_hex[CONFIG_ACCOUNT_ID_SIZE * 2 + 1];
	for(int i = 0; i < CONFIG_MAX_CONSOLES && off + 1 < buf_size; i++)
	{
		const ConfigConsole *c = &cfg->consoles[i];
		if(!c->used)
			continue;
		hex_encode(key_hex, c->regist_key, CONFIG_REGIST_KEY_SIZE);
		hex_encode(morning_hex, c->morning, CONFIG_REGIST_KEY_SIZE);
		hex_encode(acc_hex, c->account_id, CONFIG_ACCOUNT_ID_SIZE);
		int n = snprintf(buf + off, buf_size - off,
				 "console=%s|%s|%s|%s|%s\n",
				 c->host, c->name, key_hex, morning_hex, acc_hex);
		if(n < 0)
			break;
		off += (size_t)n;
		if(off >= buf_size)
		{
			off = buf_size - 1;
			break;
		}
	}
	return off;
}

// host|nombre|regist hex|morning hex|accountid hex (entre v y vend).
static void parse_console_line(Config *cfg, const char *v, const char *vend)
{
	const char *fields[5];
	const char *ends[5];
	fields[0] = v;
	for(int f = 0; f < 4; f++)
	{
		const char *sep = (const char *)memchr(fields[f], '|',
						       (size_t)(vend - fields[f]));
		if(!sep)
			return; // linea incompleta: se ignora
		ends[f] = sep;
		fields[f + 1] = sep + 1;
	}
	ends[4] = vend;

	char host[CONFIG_HOST_SIZE];
	char name[CONFIG_NAME_SIZE + 1];
	char key_hex[CONFIG_REGIST_KEY_SIZE * 2 + 1];
	char morning_hex[CONFIG_REGIST_KEY_SIZE * 2 + 1];
	char acc_hex[CONFIG_ACCOUNT_ID_SIZE * 2 + 1];
	if(!field_copy(host, sizeof(host), fields[0], ends[0]))
		return;
	field_copy(name, sizeof(name), fields[1], ends[1]);
	if(!field_copy(key_hex, sizeof(key_hex), fields[2], ends[2]) ||
	   !field_copy(morning_hex, sizeof(morning_hex), fields[3], ends[3]))
		return;
	field_copy(acc_hex, sizeof(acc_hex), fields[4], ends[4]);

	uint8_t key[CONFIG_REGIST_KEY_SIZE];
	uint8_t morning[CONFIG_REGIST_KEY_SIZE];
	uint8_t acc[CONFIG_ACCOUNT_ID_SIZE];
	if(!hex_decode(key, sizeof(key), key_hex) ||
	   !hex_decode(morning, sizeof(morning), morning_hex))
		return; // claves corruptas: no guardar basura
	if(!hex_decode(acc, sizeof(acc), acc_hex))
		memset(acc, 0, sizeof(acc));

	config_set_console(cfg, host, name, key, morning, acc);
}

void config_parse(Config *cfg, const char *buf, size_t len)
{
	const char *p = buf;
	const char *end = buf + len;
	while(p < end)
	{
		const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
		const char *line_end = nl ? nl : end;
		// \r y espacios finales.
		while(line_end > p &&
		      (line_end[-1] == '\r' || line_end[-1] == ' ' ||
		       line_end[-1] == '\t'))
			line_end--;

		if(line_end > p && p[0] != '#')
		{
			const char *eq = (const char *)memchr(p, '=',
							      (size_t)(line_end - p));
			if(eq)
			{
				char key[16];
				field_copy(key, sizeof(key), p, eq);
				const char *val = eq + 1;
				if(!strcmp(key, "account_id"))
					field_copy(cfg->account_id_hex,
						   sizeof(cfg->account_id_hex),
						   val, line_end);
				else if(!strcmp(key, "last_host"))
					field_copy(cfg->last_host,
						   sizeof(cfg->last_host),
						   val, line_end);
				else if(!strcmp(key, "console"))
					parse_console_line(cfg, val, line_end);
				// claves desconocidas: se ignoran (compatibilidad
				// futura con versiones con mas opciones)
			}
		}
		p = nl ? nl + 1 : end;
	}
}
