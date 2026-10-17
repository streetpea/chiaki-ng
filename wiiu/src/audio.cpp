// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Audio de sesion (fase 3b) sobre las voces AX de snd_core2.
//
// Arquitectura copiada del backend de audio del SDL2 de devkitPro para
// Wii U (SDL_wiiuaudio.c), que esta probado en hardware:
//
//  - Las voces AX son MONO: hace falta UNA VOZ POR CANAL (I/D) y los
//    datos desentrelazados [todos los I][todos los D].
//  - Sin AXSetVoiceDeviceMix la voz no sale por NINGUN canal (volumen
//    de bus a 0) => silencio total. SDL configura el mix del TV y del
//    GamePad con una tabla por canal (bus master 0x8000).
//  - Toda la configuracion inicial de AX se hace en un hilo con
//    afinidad CPU1 (el backend lo exige: "AX functions need to run
//    from the same core").
//  - Durante la reproduccion NO se llama a AX desde el hilo
//    principal: el playhead lo lee el callback de frame de AX.
//
// Camino del audio:
//   PS4 -> opus -> sink de chiaki (hilo de sesion) -> ganancia de
//   volumen x6 con recorte duro -> ring de staging (entrelazado)
//   -> ring AX (desentrelazado, I y D, en bucle) -> voces AX ->
//   mixer 48 kHz -> TV/GamePad.
//
// El DSP lee el ring por DMA, asi que tras cada relleno se limpia la
// cache (DCFlushRange).

#include "audio.h"

#include <coreinit/cache.h>
#include <coreinit/mutex.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <sndcore2/core.h>
#include <sndcore2/device.h>
#include <sndcore2/voice.h>

#include <malloc.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <math.h>

// 8192 frames por canal: 170 ms a 48 kHz (misma latencia que el ring
// entrelazado anterior de 32768 bytes).
#define AX_RING_FRAMES 8192u
// Bloque de un canal en el ring: 8192 samples s16 = 16384 bytes.
#define AX_BLOCK_BYTES (AX_RING_FRAMES * 2u)
// 1.4 s de staging (entrelazado): los bursts de opus no se pierden.
#define STAGE_BYTES 262144u
// Hueco minimo entre playhead y zona de relleno (~42 ms): el relleno
// corre a ~60 Hz y unarafaga lenta no debe quedar al alcance del DSP.
#define FILL_GUARD_FRAMES 2048u
// 48000 frames/s = 48 frames por ms, para el playhead por reloj.

// Tono de prueba (fase 3b): 480 Hz durante los primeros 3 s de sesión.
// Validado en gui-36 (hardware): el DSP reproduce → desactivado para
// oír el audio real desde el principio. Se conserva el código por si
// hace falta diagnosticar otra vez la salida AX.
#define TONE_FRAMES 0u
#define TONE_LUT_SAMPLES 100u
#define TONE_AMPLITUDE 8192

// Ganancia de volumen: el PCM opus que manda la PS4 llega bajo y en
// consola el volumen de sistema ya esta al maximo (en el PC se sube
// el mixer del SO, aqui no hay mas escalon que la propia ganancia).
// Mismo arreglo que el puerto Switch de chiaki-ng (switch/src/io.cpp,
// AudioCB: ganancia con recorte duro). Medido en gui-48 (MHW,
// hardware): el pico de la fuente ronda 4000-4500 (-18 dBFS), asi
// que x6 lleva el pico a ~27000 (-1.7 dBFS) sin llegar al recorte;
// si en otro juego el pico pasa de ~5461, ahi empieza el recorte
// (intencionado, igual que en el Switch) y habria que bajar a x4-x5.
#define AUDIO_GAIN_NUM 6
#define AUDIO_GAIN_DEN 1

// Hilo de init de AX: el backend de SDL de devkitPro exige que la
// configuracion de las voces se haga en CPU1.
#define AX_INIT_AFFINITY OS_THREAD_ATTRIB_AFFINITY_CPU1
#define AX_INIT_STACK_SIZE (32 * 1024)

static void (*g_log_fn)(const char *) = nullptr;

static void audio_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void audio_log(const char *fmt, ...)
{
	if(!g_log_fn)
		return;
	char msg[192];
	va_list va;
	va_start(va, fmt);
	vsnprintf(msg, sizeof(msg), fmt, va);
	va_end(va);
	g_log_fn(msg);
}

// Una voz por canal (0 = izquierdo, 1 = derecho), como SDL.
static AXVoice *g_voice[2] = { nullptr, nullptr };
static OSMutex g_lock;
static bool g_inited = false;
static bool g_playing = false;
// Ring AX desentrelazado: [0..AX_RING_FRAMES) = canal I,
// [AX_RING_FRAMES..2*AX_RING_FRAMES) = canal D.
static uint8_t g_ax_ring[AX_BLOCK_BYTES * 2] __attribute__((aligned(64)));
static uint8_t g_stage[STAGE_BYTES];
static size_t g_stage_r = 0; // bytes consumidos del staging
static size_t g_stage_w = 0; // bytes escritos en el staging
static uint32_t g_fill = 0;  // frames: siguiente posicion a rellenar
static uint32_t g_underruns = 0;
static uint32_t g_underruns_logged = 0;
static uint64_t g_underrun_log_ticks = 0;
// Playhead: lo escribe el callback de frame de AX con el offset real
// del DSP (muestras por canal = frames). Mientras no demuestre que se
// mueve, se estima por reloj (48 frames/ms exactos).
static volatile uint32_t g_dsp_play_frames = 0;
static uint64_t g_voice_start_ticks = 0;
static uint32_t g_last_dsp_off = 0;
static bool g_dsp_off_live = false;
// Tono de prueba y contador monotono de relleno.
static int16_t g_tone_lut[TONE_LUT_SAMPLES];
static uint32_t g_tone_phase = 0;
static uint32_t g_fill_total = 0; // frames rellenados (para el tono/log)
// Pico de |muestra| de la fuente (ANTES de la ganancia) entre logs:
// si el pico llega bajo, la fuente es la que viene corta y hay que
// subir la ganancia; si llega a >=5461, x6 ya esta recortando.
static volatile uint32_t g_peak_src = 0;

void audio_set_log(void (*log_fn)(const char *))
{
	g_log_fn = log_fn;
}

// Si otro cliente con prioridad mas alta roba una voz, AX la libera y
// avisa aqui: se deja de tocar y ya no se toca nada mas.
static void ax_voice_callback(void *user)
{
	(void)user;
	audio_log("Audio: voz AX robada por otro cliente");
	g_playing = false;
}

// Callback de frame de AX (corre en el hilo del mixer cada ~5.3 ms):
// solo lee el offset del DSP de la voz izquierda. Rapido, sin copias.
static void ax_frame_callback(void)
{
	AXVoice *v = g_voice[0];
	if(!v)
		return;
	AXVoiceOffsets offs = {};
	AXGetVoiceOffsets(v, &offs);
	// LPCM16 por canal: currentOffset viene en samples = frames.
	g_dsp_play_frames = offs.currentOffset & (AX_RING_FRAMES - 1);
}

// Configuracion de las dos voces; se ejecuta en un hilo de CPU1.
static int audio_ax_setup(int argc, const char **argv)
{
	(void)argc;
	(void)argv;

	// Tablas de mix por canal (calcadas de SDL_wiiuaudio_mix.h): la
	// voz I se rutea al canal IZQ del TV y del GamePad, la voz D al
	// canal DER. El puntero lleva UNA ENTRADA POR CANAL de salida
	// (6: TV 5.1); bus master a 0x8000 (1.0 Q1.15), resto a 0.
	static AXVoiceDeviceMixData mix[2][6];
	memset(mix, 0, sizeof(mix));
	mix[0][0].bus[0].volume = 0x8000; // voz 0 -> canal IZQ
	mix[1][1].bus[0].volume = 0x8000; // voz 1 -> canal DER

	AXVoiceVeData ve = {};
	ve.volume = 0x8000;

	// Ring a silencio antes de que el DSP lo mire.
	memset(g_ax_ring, 0, sizeof(g_ax_ring));
	DCFlushRange(g_ax_ring, sizeof(g_ax_ring));

	for(int i = 0; i < 2; i++)
	{
		g_voice[i] = AXAcquireVoice(31, ax_voice_callback, nullptr);
		if(!g_voice[i])
		{
			audio_log("Audio: AXAcquireVoice fallo (canal %d)", i);
			return 1;
		}

		AXVoiceBegin(g_voice[i]);
		AXSetVoiceType(g_voice[i], 0);
		AXSetVoiceVe(g_voice[i], &ve);
		AXSetVoiceDeviceMix(g_voice[i], AX_DEVICE_TYPE_TV, 0,
				    mix[i]);
		AXSetVoiceDeviceMix(g_voice[i], AX_DEVICE_TYPE_DRC, 0,
				    mix[i]);
		// SRC a 1.0 con remuestreo lineal (identidad a rate igual),
		// en el mismo orden que SDL: ratio y luego tipo.
		AXSetVoiceSrcRatio(g_voice[i], 1.0f);
		AXSetVoiceSrcType(g_voice[i], AX_VOICE_SRC_TYPE_LINEAR);

		// Bloque circular propio por canal en LPCM16. OJO: en
		// LPCM16 los offsets van en MUESTRAS de 16 bit, no en bytes
		// (asi lo hace el snd_core real y asi los usa SDL).
		AXVoiceOffsets offsets = {};
		offsets.dataType = AX_VOICE_FORMAT_LPCM16;
		offsets.loopingEnabled = AX_VOICE_LOOP_ENABLED;
		offsets.loopOffset = 0;
		offsets.endOffset = AX_RING_FRAMES;
		offsets.currentOffset = 0;
		offsets.data = g_ax_ring + i * AX_BLOCK_BYTES;
		AXSetVoiceOffsets(g_voice[i], &offsets);
		AXSetVoiceLoop(g_voice[i], AX_VOICE_LOOP_ENABLED);
		AXVoiceEnd(g_voice[i]);

		AXSetVoiceState(g_voice[i], AX_VOICE_STATE_STOPPED);
	}

	// Verificacion de la voz 0 tras los setters.
	AXVoiceOffsets rb = {};
	AXGetVoiceOffsets(g_voice[0], &rb);
	audio_log("Audio: offsets rb type=%u loop=%u end=%u data=%d check=%d",
		  (unsigned int)rb.dataType, (unsigned int)rb.loopingEnabled,
		  (unsigned int)rb.endOffset, rb.data != nullptr,
		  (int)AXCheckVoiceOffsets(&rb));

	AXRegisterAppFrameCallback(ax_frame_callback);
	return 0;
}

bool audio_init(void)
{
	if(g_inited)
		return true;
	OSInitMutex(&g_lock);

	audio_log("Audio: AX init=%d renderer=%uHz maxvoices=%u",
		  (int)AXIsInit(), (unsigned int)AXGetInputSamplesPerSec(),
		  (unsigned int)AXGetMaxVoices());

	// LUT del tono de prueba: un ciclo exacto de 480 Hz a 48 kHz.
	for(uint32_t i = 0; i < TONE_LUT_SAMPLES; i++)
		g_tone_lut[i] = (int16_t)(TONE_AMPLITUDE *
			sinf(2.0f * 3.14159265f * (float)i /
			     (float)TONE_LUT_SAMPLES));

	// Toda la config de voces en CPU1 (patron del SDL de devkitPro).
	int32_t result = 1;
	if(OSGetThreadAffinity(OSGetCurrentThread()) != AX_INIT_AFFINITY)
	{
		OSThread *thread = (OSThread *)memalign(16, sizeof(OSThread));
		uint8_t *stack =
			(uint8_t *)memalign(16, AX_INIT_STACK_SIZE);
		if(!thread || !stack)
		{
			audio_log("Audio: sin memoria para hilo AX");
			free(thread);
			free(stack);
			return false;
		}
		int32_t prio = OSGetThreadPriority(OSGetCurrentThread());
		if(!OSCreateThread(thread, audio_ax_setup, 0, nullptr,
				    stack + AX_INIT_STACK_SIZE,
				    AX_INIT_STACK_SIZE, prio,
				    AX_INIT_AFFINITY))
		{
			audio_log("Audio: OSCreateThread AX fallo");
			free(thread);
			free(stack);
			return false;
		}
		OSResumeThread(thread);
		OSJoinThread(thread, &result);
		free(thread);
		free(stack);
	}
	else
	{
		result = audio_ax_setup(0, nullptr);
	}
	if(result != 0)
		return false;

	g_inited = true;
	audio_log("Audio: 2 voces AX listas (LPCM16 mono c/u, ring %u ms)",
		  (unsigned int)(AX_RING_FRAMES / 48));
	audio_log("Audio: ganancia de PCM x6 (recorte duro)");
	return true;
}

void audio_quit(void)
{
	if(!g_inited)
		return;
	OSLockMutex(&g_lock);
	g_playing = false;
	AXVoice *vl = g_voice[0];
	AXVoice *vr = g_voice[1];
	g_voice[0] = nullptr;
	g_voice[1] = nullptr;
	OSUnlockMutex(&g_lock);
	AXDeregisterAppFrameCallback(ax_frame_callback);
	if(vl)
	{
		AXSetVoiceState(vl, AX_VOICE_STATE_STOPPED);
		AXFreeVoice(vl);
	}
	if(vr)
	{
		AXSetVoiceState(vr, AX_VOICE_STATE_STOPPED);
		AXFreeVoice(vr);
	}
	// coreinit no tiene OSDestroyMutex: el mutex se queda hasta el exit.
	g_inited = false;
}

void audio_session_start(void)
{
	if(!g_inited || !g_voice[0] || !g_voice[1])
		return;
	OSLockMutex(&g_lock);
	memset(g_ax_ring, 0, sizeof(g_ax_ring));
	DCFlushRange(g_ax_ring, sizeof(g_ax_ring));
	g_stage_r = 0;
	g_stage_w = 0;
	g_fill = 0;
	g_underruns = 0;
	g_underruns_logged = 0;
	g_underrun_log_ticks = OSGetTime();
	g_dsp_play_frames = 0;
	AXSetVoiceCurrentOffset(g_voice[0], 0);
	AXSetVoiceCurrentOffset(g_voice[1], 0);
	g_voice_start_ticks = OSGetTime();
	g_last_dsp_off = 0;
	g_dsp_off_live = false;
	g_tone_phase = 0;
	g_fill_total = 0;
	g_playing = true;
	AXSetVoiceState(g_voice[0], AX_VOICE_STATE_PLAYING);
	AXSetVoiceState(g_voice[1], AX_VOICE_STATE_PLAYING);
	OSUnlockMutex(&g_lock);
}

void audio_session_stop(void)
{
	if(!g_inited || !g_voice[0])
		return;
	OSLockMutex(&g_lock);
	g_playing = false;
	OSUnlockMutex(&g_lock);
	AXSetVoiceState(g_voice[0], AX_VOICE_STATE_STOPPED);
	if(g_voice[1])
		AXSetVoiceState(g_voice[1], AX_VOICE_STATE_STOPPED);
}

// Copia PCM al staging aplicando la ganancia de volumen (entero 9/5)
// con recorte duro a int16, y mide de paso el pico de la fuente
// antes de ganar (para el log de diagnostico).
static void stage_gain_copy(int16_t *dst, const int16_t *src,
			    size_t samples)
{
	for(size_t i = 0; i < samples; i++)
	{
		int32_t s = src[i];
		int32_t a = s < 0 ? -s : s;
		if((uint32_t)a > g_peak_src)
			g_peak_src = (uint32_t)a;
		int32_t v = (s * AUDIO_GAIN_NUM) / AUDIO_GAIN_DEN;
		if(v > 32767)
			v = 32767;
		else if(v < -32768)
			v = -32768;
		dst[i] = (int16_t)v;
	}
}

void audio_push(const uint8_t *buf, size_t size)
{
	if(!g_playing)
		return;
	OSLockMutex(&g_lock);
	size_t used = g_stage_w - g_stage_r;
	size_t free = STAGE_BYTES - used;
	if(size > free)
		size = free; // staging lleno: se corta (underrun lo notara)
	size_t w = g_stage_w % STAGE_BYTES;
	size_t first = STAGE_BYTES - w;
	if(first > size)
		first = size;
	stage_gain_copy((int16_t *)(g_stage + w), (const int16_t *)buf,
			first / 2);
	if(size > first)
		stage_gain_copy((int16_t *)g_stage,
				(const int16_t *)(buf + first),
				(size - first) / 2);
	g_stage_w += size;
	OSUnlockMutex(&g_lock);
}

// Tono de prueba: escribe 'frames' en ambos bloques (canal I y D) con
// fase continua entre llamadas. off y frames ya validados dentro del
// ring.
static void fill_tone(uint32_t off, uint32_t frames)
{
	uint32_t left = frames;
	uint32_t o = off;
	while(left)
	{
		uint32_t chunk = AX_RING_FRAMES - o;
		if(chunk > left)
			chunk = left;
		int16_t *l = (int16_t *)g_ax_ring + o;
		int16_t *r = (int16_t *)g_ax_ring + AX_RING_FRAMES + o;
		for(uint32_t i = 0; i < chunk; i++)
		{
			int16_t s = g_tone_lut[g_tone_phase];
			g_tone_phase++;
			if(g_tone_phase >= TONE_LUT_SAMPLES)
				g_tone_phase = 0;
			l[i] = s;
			r[i] = s;
		}
		o = (o + chunk) % AX_RING_FRAMES;
		left -= chunk;
	}
	g_fill = o;
}

// Silencio en ambos bloques.
static void fill_silence(uint32_t off, uint32_t frames)
{
	uint32_t left = frames;
	uint32_t o = off;
	while(left)
	{
		uint32_t chunk = AX_RING_FRAMES - o;
		if(chunk > left)
			chunk = left;
		memset((uint8_t *)g_ax_ring + o * 2, 0, chunk * 2);
		memset((uint8_t *)g_ax_ring + AX_BLOCK_BYTES + o * 2, 0,
		       chunk * 2);
		o = (o + chunk) % AX_RING_FRAMES;
		left -= chunk;
	}
	g_fill = o;
}

// Vuelca 'frames' del staging (entrelazado I/D) al ring desentrelazado
// en la posición 'off', repartiendo entre los bloques I y D.
static void stage_to_ring(uint32_t off, uint32_t frames)
{
	size_t r = g_stage_r % STAGE_BYTES;
	uint32_t left = frames;
	uint32_t o = off;
	while(left)
	{
		uint32_t chunk = AX_RING_FRAMES - o;
		size_t tail = STAGE_BYTES - r;
		uint32_t avail = (uint32_t)(tail / 4); // 4 B por frame
		if(chunk > left)
			chunk = left;
		if(chunk > avail)
			chunk = avail;
		const int16_t *src = (const int16_t *)(g_stage + r);
		int16_t *l = (int16_t *)g_ax_ring + o;
		int16_t *rr = (int16_t *)g_ax_ring + AX_RING_FRAMES + o;
		for(uint32_t i = 0; i < chunk; i++)
		{
			l[i] = src[i * 2];
			rr[i] = src[i * 2 + 1];
		}
		o = (o + chunk) % AX_RING_FRAMES;
		r = (r + chunk * 4) % STAGE_BYTES;
		left -= chunk;
	}
	g_stage_r += (size_t)frames * 4;
	g_fill = o;
}

void audio_update(void)
{
	if(!g_inited)
		return;
	OSLockMutex(&g_lock);
	if(!g_playing || !g_voice[0])
	{
		OSUnlockMutex(&g_lock);
		return;
	}

	// Playhead en frames dentro del ring (va en bucle). Lo escribe el
	// callback de frame de AX con el offset real del DSP; mientras no
	// demuestre que se mueve, se estima por reloj (48 frames/ms).
	uint32_t dsp_off = g_dsp_play_frames;
	if(dsp_off != g_last_dsp_off)
	{
		g_last_dsp_off = dsp_off;
		g_dsp_off_live = true; // el DSP si actualiza: pasa a ser la fe
	}
	uint32_t play;
	if(g_dsp_off_live)
	{
		play = dsp_off;
	}
	else
	{
		uint64_t ticks_per_ms = OSSecondsToTicks(1) / 1000;
		uint64_t elapsed_ms = (OSGetTime() - g_voice_start_ticks) /
				      ticks_per_ms;
		play = (uint32_t)((elapsed_ms * 48u) % AX_RING_FRAMES);
	}

	// Relleno por detras del playhead, dejando el hueco de seguridad.
	uint32_t need = (play + AX_RING_FRAMES - FILL_GUARD_FRAMES -
			 g_fill) % AX_RING_FRAMES;
	if(need)
	{
		if(g_fill_total < TONE_FRAMES)
		{
			// Tono de prueba (primeros 3 s de sesion): si suena,
			// el DSP reproduce y el problema es chiaki.
			fill_tone(g_fill, need);
			g_fill_total += need;
		}
		else
		{
			size_t have = (g_stage_w - g_stage_r) / 4;
			uint32_t copied = (uint32_t)(have < need ? have
							       : need);
			if(copied)
				stage_to_ring(g_fill, copied);
			// Lo que falte hasta 'need' es silencio: la PS4 se
			// calla o los datos llegaron tarde; se cuenta como
			// underrun.
			uint32_t silence = need - copied;
			if(silence)
			{
				fill_silence(g_fill, silence);
				g_underruns += silence;
			}
			g_fill_total += need;
		}

		// El DSP lee por DMA: limpiar toda la cache del ring
		// (32 KB, trivial) para no mandar lineas sucias.
		DCFlushRange(g_ax_ring, sizeof(g_ax_ring));
	}

	// Diagnostico cada 3 s mientras hay sesion: playhead efectivo y
	// el offset crudo del DSP.
	uint64_t now = OSGetTime();
	if(now - g_underrun_log_ticks > OSSecondsToTicks(3))
	{
		g_underrun_log_ticks = now;
		uint32_t peak = g_peak_src;
		g_peak_src = 0;
		audio_log(
			"Audio: play=%u dsp=%u running=%d fill=%u recv=%u underruns=+%u peak=%u",
			(unsigned int)play, (unsigned int)dsp_off,
			(int)AXIsVoiceRunning(g_voice[0]),
			(unsigned int)g_fill, (unsigned int)g_stage_w,
			(unsigned int)(g_underruns - g_underruns_logged),
			(unsigned int)peak);
		g_underruns_logged = g_underruns;
	}
	OSUnlockMutex(&g_lock);
}
