// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// ---------------------------------------------------------------------------
// Persistencia de consolas registradas (fase 4a).
//
// Fichero: sd:/wiiu/apps/chiaki/config.ini (lineas "clave=valor"):
//
//   account_id=<16 hex>                       cuenta PSN global
//   last_host=<ip>                            ultima consola conectada
//   console=<host>|<nombre>|<regist hex>|<morning hex>|<accountid hex>
//
// Una linea "console=" por consola guardada. El modulo solo serializa/
// parsea (sin FS): las entradas las hace main.cpp por el mismo cliente FS
// que el log. Gracias a esto el registro (PIN) se hace UNA vez por consola
// y sobrevive a reinicios y reconstrucciones del binario.
// ---------------------------------------------------------------------------
#pragma once

#include <stddef.h>
#include <stdint.h>

#define CONFIG_REGIST_KEY_SIZE 0x10
#define CONFIG_ACCOUNT_ID_SIZE 8
#define CONFIG_NAME_SIZE 0x20
#define CONFIG_HOST_SIZE 48
#define CONFIG_MAX_CONSOLES 8

struct ConfigConsole
{
	bool used;
	char host[CONFIG_HOST_SIZE];
	char name[CONFIG_NAME_SIZE + 1];
	uint8_t regist_key[CONFIG_REGIST_KEY_SIZE];
	uint8_t morning[CONFIG_REGIST_KEY_SIZE];
	uint8_t account_id[CONFIG_ACCOUNT_ID_SIZE];
};

struct Config
{
	// Cuenta PSN global en hex (para registrar consolas nuevas sin
	// volver a teclearla).
	char account_id_hex[CONFIG_ACCOUNT_ID_SIZE * 2 + 1];
	// Ultima consola conectada: al arrancar se preselecciona su IP,
	// claves y cuenta para conectar con A sin registrar de nuevo.
	char last_host[CONFIG_HOST_SIZE];
	ConfigConsole consoles[CONFIG_MAX_CONSOLES];
};

// Vacia todo (sin cuenta, sin consolas).
void config_init(Config *cfg);

// Serializa a "clave=valor\n"; devuelve los bytes utiles (0 = no cabe).
size_t config_serialize(const Config *cfg, char *buf, size_t buf_size);

// Parsea sobre cfg (sin limpiar lo que no aparezca). len = bytes validos.
void config_parse(Config *cfg, const char *buf, size_t len);

// Indice de la consola con esa IP, o -1.
int config_find(const Config *cfg, const char *host);

// Anade o reemplaza (por host) una consola. Devuelve su indice o -1 si
// esta llena. '|' en nombre/host se sanitiza para no romper el formato.
int config_set_console(Config *cfg, const char *host, const char *name,
		       const uint8_t regist_key[CONFIG_REGIST_KEY_SIZE],
		       const uint8_t morning[CONFIG_REGIST_KEY_SIZE],
		       const uint8_t account_id[CONFIG_ACCOUNT_ID_SIZE]);

// Elimina la consola idx y COMPACTA el resto (los indices del menu son
// slots consecutivos sin huecos). Si last_host apuntaba a esa consola,
// se limpia para no preseleccionar un fantasma. false si idx no vale.
bool config_del_console(Config *cfg, int idx);

int config_console_count(const Config *cfg);
