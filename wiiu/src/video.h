// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#pragma once

#include <stddef.h>
#include <stdint.h>

// Fase 3a — decodificacion H.264 por hardware (h264.rpl del sistema, la
// misma libreria que usan CoffeeFlix/Netflix) + presentacion NV12 via GX2
// con shader propio BT.709. Sin dependencias nuevas: los simbolos H264DEC*
// estan en libwut.a y las cabeceras en <h264/*.h>.
//
// Hilos: wiiu_video_sample() corre en el hilo de red/video de chiaki y
// solo encola la trama comprimida (µs); el decode (H264DEC*) corre en un
// hilo interno para no bloquear la recepcion UDP. wiiu_video_render()
// SOLO desde el hilo principal dentro del pass TV/DRC de WHBGfx.
// Estado compartido protegido con OSMutex internos.

typedef void (*WiiUVideoLogFn)(const char *line);

// Registrar el sink de log (logprintf del frontend) antes de la primera sesion.
void wiiu_video_set_log(WiiUVideoLogFn fn);

// Alimenta una muestra H.264 Annex-B: primero la cabecera SPS/PPS (entrega
// del video receiver) y despues cada frame. Devuelve true si se consumo.
bool wiiu_video_sample(uint8_t *buf, size_t buf_size);

// Dibuja el ultimo frame disponible a pantalla (quad fullscreen NDC).
// Llamar dentro de WHBGfxBeginRenderTV/DRC con el tamano del scan buffer.
void wiiu_video_render(int viewport_w, int viewport_h);

// Libera decoder (40 MB) y buffers. Idempotente: llamar al iniciar y parar
// sesion, y en el apagado de la app.
void wiiu_video_reset(bool wait_gpu = true);

struct WiiUVideoStats
{
	uint64_t fed;     // muestras alimentadas
	uint64_t decoded; // frames decodificados por el hardware
	uint64_t skipped; // NALs omitibles descartados
	uint64_t errors;  // H264DECExecute con codigo inesperado
	uint64_t dropped; // frames soltados por cola de decode llena
	uint32_t width;   // dimensiones actuales (0 = sin decoder)
	uint32_t height;
	// Tiempos medios del hilo de decode (EMA, microsegundos) para
	// diagnosticar el techo de fps: exec = H264DECExecute;
	// lock = espera del mutex de display; mem = copia NV12 al
	// buffer de display.
	uint32_t exec_us;
	uint32_t lock_us;
	uint32_t mem_us;
};

WiiUVideoStats wiiu_video_stats();
