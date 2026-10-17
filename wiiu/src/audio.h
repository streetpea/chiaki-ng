// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Fase 3b: audio de sesion por la voz AX nativa (snd_core2). chiaki
// decodifica opus y entrega PCM s16le estereo 48 kHz por el sink de la
// sesion; aqui eso va a un ring de staging (productor: hilo de sesion
// de chiaki) y de ahi a un buffer circular que una voz AX LPCM16
// reproduce en bucle. El main loop rellena por detras del playhead del
// DSP cada frame; si el staging falta, se pone silencio (underrun
// contabilizado) en vez de desincronizar.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Adaptador al log de la app (main instala su logprintf).
void audio_set_log(void (*log_fn)(const char *));

// Inicializa mutex + voz AX (LPCM16 estereo, prioridad alta). El mixer
// ya debe estar a 48 kHz (AXInitWithParams en main).
bool audio_init(void);

// Para la voz y libera todo.
void audio_quit(void);

// Nueva sesion: ring a cero y voz a PLAYING (arranca en silencio hasta
// que llegan frames del stream).
void audio_session_start(void);

// Fin de sesion: voz a STOPPED.
void audio_session_stop(void);

// Llamar cada frame del main loop con sesion activa: rellena el ring
// por detras del playhead del DSP.
void audio_update(void);

// Sink de chiaki (hilo de sesion): PCM ya decodificado (s16le estereo
// 48 kHz). No bloquear: copia a un ring interno aplicando la ganancia
// x6 con recorte duro (ver audio.cpp, estilo puerto Switch).
void audio_push(const uint8_t *buf, size_t size);
