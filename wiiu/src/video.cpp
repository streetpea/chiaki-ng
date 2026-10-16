// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Wii U — decodificacion H.264 por hardware + render NV12.
//
// Decoder: h264.rpl del sistema (H264DEC*), expuesto por wut en
// <h264/decode.h> y resuelto dentro de libwut.a. Flujo identico al
// probado por GaryOderNichts en FFmpeg-wiiu (libavcodec/h264_wiiu.c):
//   InitParam(mem req. nivel 4.2) -> FPTR_OUTPUT -> OUTPUT_PER_FRAME ->
//   Open -> Begin; por frame: CheckSkipableFrame -> SetBitstream ->
//   Execute (exito = 0xE4) con framebuffer NV12 propio; cierre con
//   Flush/End/Close ignorando errores. El frame sale en NV12 con
//   pitch = align(width, 256) y el plano UV justo detras de height
//   filas (fb + height*pitch) — formula probada en CoffeeFlix.
//
// Render: GX2 directo (patron de carga del driver Wii U de SDL2:
// shaders GFD + fetch shader + GX2R). El pixel shader NV12->RGB BT.709
// esta en wiiu/shaders/video.{vsh,psh}; el .gsh compilado via
// latte-assembler viaja embebido en video_gsh.h. Dos texturas (Y R8 y
// UV R8_G8 a media resolucion) con las mismas coordenadas normalizadas
// hacen la combinacion NV12 -> la GPU convierte a RGB gratis.
//
// Reparto de hilos: GX2 NO es thread-safe. El decode va en un hilo
// interno propio: el hilo de red de chiaki solo encola la trama
// comprimida, porque H264DECExecute tarda ~10-20 ms y bloquearlo en el
// hilo de takion desbordaba el buffer UDP (se perdia ~88% de los
// datagramas en pruebas de hardware). Por eso:
//   - hilo de chiaki: copia el bitstream a la cola (us).
//   - hilo de decode: H264DEC* + malloc + copias CPU (bajo lock).
//   - hilo principal: TODO lo GX2 (init perezoso + subida + draw).
// El handoff de frames va por doble buffer con OSMutex compartido.

#include "video.h"
#include "../shaders/video_gsh.h"

#include <h264/decode.h>
#include <h264/stream.h>

#include <gfd.h>
#include <gx2/context.h>
#include <gx2/draw.h>
#include <gx2/enum.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#include <gx2r/buffer.h>
#include <gx2r/draw.h>
#include <gx2r/resource.h>
#include <gx2r/surface.h>

#include <coreinit/cache.h>
#include <coreinit/mutex.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>

#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Memoria del decoder para el nivel maximo soportado (4.2): el valor
// exacto que usa FFmpeg-wiiu ("Set memRequirement to match the maximum
// supported Level"). ~40 MB.
#define DEC_MEM_REQ (0x2200000u + 0x3ffu + 0x480000u)

// Codigo de exito de H264DECExecute cuando hubo frame de salida
// (provenido del codigo probado en hardware de CoffeeFlix).
#define EXEC_FRAME_OK 0xE4

// Cola de tramas comprimidas entre el hilo de chiaki y el hilo de
// decode: 4 slots x 512 KB (una trama de 720p rara vez pasa de 300 KB;
// si una trama mayor llegara, se trunca — efectivamente se pierde).
#define VQ_SLOTS 8
#define VQ_SLOT_SZ (512u * 1024u)

// Pila del hilo de decode (el decode no usa GX2 ni SDL: sobra con poco).
#define DEC_STACK_SZ (64u * 1024u)

// ---------------------------------------------------------------------------
// Estado
// ---------------------------------------------------------------------------

struct VideoVert
{
	float x, y;
	uint32_t color;
	float u, v;
};

static WiiUVideoLogFn s_logfn;

static OSMutex s_lock;
static volatile int s_lock_inited;

// decoder (solo hilo de video)
static void *s_dec_mem;    // ~40 MB del h264.rpl
static bool s_dec_open;
static bool s_dec_giveup;  // sin memoria / init fallido: no reintentar
static uint8_t *s_dec_fb;  // framebuffer NV12 que recibe Execute
static size_t s_dec_fb_size;

// geometria actual (escrita bajo lock por el hilo de video)
static int s_w, s_h;       // 0 = sin decoder inicializado
static size_t s_pitch;     // align(w, 256)

// handoff de frames (productor: hilo de decode; consumidor: main)
// 3 ranuras de display: con 2, mientras main sube una (reading) y el
// decode publica otra (ready), no queda ranura libre y elegir la
// pendiente romperia la subida concurrente (tearing). Con 3 siempre hay
// una que no toca nadie. Tamano = mismo NV12 con holgura que el fb del
// decoder: H264DECExecute escribe AQUI directo, sin copia intermedia.
static uint8_t *s_disp[3];
static size_t s_disp_size;
static int s_disp_ready = -1;
// Slot que el hilo principal esta SUBIENDO a textura (fuera del lock:
// ver wiiu_video_render). El decode jamas escribe este slot.
static int s_disp_reading = -1;

// cola de tramas comprimidas (productor: hilo de chiaki en
// wiiu_video_sample; consumidor: el hilo interno de decode)
static OSMutex s_q_lock;
static uint8_t *s_q_buf[VQ_SLOTS];
static size_t s_q_len[VQ_SLOTS];
static int s_q_head, s_q_tail;
static OSThread *s_dec_thread; // memalign(16): el OS valida la alineacion
static uint8_t *s_dec_stack;
static volatile int s_dec_run;
static volatile int s_dec_alive;

// render (SOLO hilo principal: se crea perezosamente en render())
static GX2VertexShader *s_vs;
static GX2PixelShader *s_ps;
static GX2FetchShader s_fetch;
static void *s_fetch_prog;
static GX2Sampler s_samp;
static GX2Texture s_tex_y;
static GX2Texture s_tex_uv;
static GX2RBuffer s_vbuf;
static bool s_render_ready;
static bool s_tex_ready;
static int s_tex_w, s_tex_h;
static bool s_tex_has_frame;   // ya se subio algun frame (main thread)
static bool s_init_logged;      // "decoder listo" ya impreso
static bool s_render_err_logged;

static WiiUVideoStats s_stats;
static bool s_logged_first_frame;
static bool s_logged_no_sps;
static int s_logged_err_codes;
static uint32_t s_exec_us; // EMA de H264DECExecute (µs, 125 ticks = 1 µs)
static uint32_t s_lock_us; // EMA de espera del mutex de display (µs)
static uint32_t s_mem_us;  // EMA de la copia NV12 al display (µs)

static const float s_identity[16] = {
	1.f, 0.f, 0.f, 0.f,
	0.f, 1.f, 0.f, 0.f,
	0.f, 0.f, 1.f, 0.f,
	0.f, 0.f, 0.f, 1.f,
};

static void vlog(const char *fmt, ...)
{
	char line[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	if(s_logfn)
		s_logfn(line);
}

static void lock_inited()
{
	if(!s_lock_inited)
	{
		OSInitMutex(&s_lock);
		OSInitMutex(&s_q_lock);
		__sync_synchronize();
		s_lock_inited = 1;
	}
}

// ---------------------------------------------------------------------------
// Shaders (GFD) — patron de SDL2-wiiu/SDL_shaders_wiiu.c
// ---------------------------------------------------------------------------

static GX2VertexShader *load_gfd_vs(const void *file)
{
	uint32_t header_size, program_size;
	GX2VertexShader *shader;
	void *program;

	if(!file || GFDGetVertexShaderCount(file) == 0)
		return NULL;
	header_size = GFDGetVertexShaderHeaderSize(0, file);
	program_size = GFDGetVertexShaderProgramSize(0, file);
	if(!header_size || !program_size)
		return NULL;

	shader = (GX2VertexShader *)memalign(0x40, header_size);
	if(!shader)
		return NULL;
	memset(shader, 0, header_size);

	shader->gx2rBuffer.flags = GX2R_RESOURCE_BIND_SHADER_PROGRAM |
				  GX2R_RESOURCE_USAGE_CPU_READ |
				  GX2R_RESOURCE_USAGE_CPU_WRITE |
				  GX2R_RESOURCE_USAGE_GPU_READ;
	shader->gx2rBuffer.elemSize = program_size;
	shader->gx2rBuffer.elemCount = 1;
	shader->gx2rBuffer.buffer = NULL;
	if(!GX2RCreateBuffer(&shader->gx2rBuffer))
	{
		free(shader);
		return NULL;
	}

	program = GX2RLockBufferEx(&shader->gx2rBuffer, GX2R_RESOURCE_BIND_NONE);
	if(!program || !GFDGetVertexShader(shader, program, 0, file))
	{
		GX2RUnlockBufferEx(&shader->gx2rBuffer, GX2R_RESOURCE_BIND_NONE);
		GX2RDestroyBufferEx(&shader->gx2rBuffer, GX2R_RESOURCE_BIND_NONE);
		free(shader);
		return NULL;
	}
	GX2RUnlockBufferEx(&shader->gx2rBuffer, GX2R_RESOURCE_BIND_NONE);
	// Patron SDL-wiiu: invalidar tras cargar el bytecode.
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, shader->program, shader->size);
	return shader;
}

static GX2PixelShader *load_gfd_ps(const void *file)
{
	uint32_t header_size, program_size;
	GX2PixelShader *shader;
	void *program;

	if(!file || GFDGetPixelShaderCount(file) == 0)
		return NULL;
	header_size = GFDGetPixelShaderHeaderSize(0, file);
	program_size = GFDGetPixelShaderProgramSize(0, file);
	if(!header_size || !program_size)
		return NULL;

	shader = (GX2PixelShader *)memalign(0x40, header_size);
	if(!shader)
		return NULL;
	memset(shader, 0, header_size);

	shader->gx2rBuffer.flags = GX2R_RESOURCE_BIND_SHADER_PROGRAM |
				  GX2R_RESOURCE_USAGE_CPU_READ |
				  GX2R_RESOURCE_USAGE_CPU_WRITE |
				  GX2R_RESOURCE_USAGE_GPU_READ;
	shader->gx2rBuffer.elemSize = program_size;
	shader->gx2rBuffer.elemCount = 1;
	shader->gx2rBuffer.buffer = NULL;
	if(!GX2RCreateBuffer(&shader->gx2rBuffer))
	{
		free(shader);
		return NULL;
	}

	program = GX2RLockBufferEx(&shader->gx2rBuffer, GX2R_RESOURCE_BIND_NONE);
	if(!program || !GFDGetPixelShader(shader, program, 0, file))
	{
		GX2RUnlockBufferEx(&shader->gx2rBuffer, GX2R_RESOURCE_BIND_NONE);
		GX2RDestroyBufferEx(&shader->gx2rBuffer, GX2R_RESOURCE_BIND_NONE);
		free(shader);
		return NULL;
	}
	GX2RUnlockBufferEx(&shader->gx2rBuffer, GX2R_RESOURCE_BIND_NONE);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, shader->program, shader->size);
	return shader;
}

static void render_free()
{
	if(s_tex_ready)
	{
		GX2RDestroySurfaceEx(&s_tex_y.surface, GX2R_RESOURCE_BIND_NONE);
		GX2RDestroySurfaceEx(&s_tex_uv.surface, GX2R_RESOURCE_BIND_NONE);
		s_tex_ready = false;
		s_tex_w = s_tex_h = 0;
	}
	if(s_vbuf.buffer)
	{
		GX2RDestroyBufferEx(&s_vbuf, GX2R_RESOURCE_BIND_NONE);
		memset(&s_vbuf, 0, sizeof(s_vbuf));
	}
	if(s_vs)
	{
		GX2RDestroyBufferEx(&s_vs->gx2rBuffer, GX2R_RESOURCE_BIND_NONE);
		free(s_vs);
		s_vs = NULL;
	}
	if(s_ps)
	{
		GX2RDestroyBufferEx(&s_ps->gx2rBuffer, GX2R_RESOURCE_BIND_NONE);
		free(s_ps);
		s_ps = NULL;
	}
	if(s_fetch_prog)
	{
		free(s_fetch_prog);
		s_fetch_prog = NULL;
	}
	s_render_ready = false;
	s_tex_has_frame = false;
}

// Solo desde el hilo principal.
static bool render_init()
{
	GX2AttribStream attrs[3];
	VideoVert verts[4];
	uint32_t fetch_size;

	s_vs = load_gfd_vs(video_gsh);
	s_ps = load_gfd_ps(video_gsh);
	if(!s_vs || !s_ps)
		return false;

	// a_position (R1, loc0), a_color (R2, loc1), a_texcoord (R3, loc2)
	attrs[0] = { 0, 0, (uint32_t)offsetof(VideoVert, x),
		     GX2_ATTRIB_FORMAT_FLOAT_32_32, GX2_ATTRIB_INDEX_PER_VERTEX, 0,
		     GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1),
		     GX2_ENDIAN_SWAP_DEFAULT };
	attrs[1] = { 1, 0, (uint32_t)offsetof(VideoVert, color),
		     GX2_ATTRIB_FORMAT_UNORM_8_8_8_8, GX2_ATTRIB_INDEX_PER_VERTEX, 0,
		     GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W),
		     GX2_ENDIAN_SWAP_DEFAULT };
	attrs[2] = { 2, 0, (uint32_t)offsetof(VideoVert, u),
		     GX2_ATTRIB_FORMAT_FLOAT_32_32, GX2_ATTRIB_INDEX_PER_VERTEX, 0,
		     GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1),
		     GX2_ENDIAN_SWAP_DEFAULT };

	fetch_size = GX2CalcFetchShaderSizeEx(3, GX2_FETCH_SHADER_TESSELLATION_NONE,
					      GX2_TESSELLATION_MODE_DISCRETE);
	s_fetch_prog = memalign(GX2_SHADER_PROGRAM_ALIGNMENT, fetch_size);
	if(!s_fetch_prog)
		return false;
	GX2InitFetchShaderEx(&s_fetch, (uint8_t *)s_fetch_prog, 3, attrs,
			     GX2_FETCH_SHADER_TESSELLATION_NONE,
			     GX2_TESSELLATION_MODE_DISCRETE);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, s_fetch_prog, fetch_size);

	GX2InitSampler(&s_samp, GX2_TEX_CLAMP_MODE_CLAMP,
		       GX2_TEX_XY_FILTER_MODE_LINEAR);

	// Quad fullscreen en NDC; v=0 arriba (origen de textura GX2).
	// Orden de PERIMETRO (TL,TR,BR,BL): GX2 triangula el quad en
	// (v0,v1,v2)+(v0,v2,v3), asi que con el orden zigzag (TL,TR,BL,BR)
	// usado antes salian triangulos cruzados y el {TR, centro, BR}
	// quedaba sin cubrir: un triangulo del 25% de pantalla en verde
	// (color de clear = NV12 vacio). Perimetro cierra el quad completo
	// con viento uniforme en ambos triangulos.
	verts[0] = { -1.f, 1.f, 0xffffffffu, 0.f, 0.f };
	verts[1] = { 1.f, 1.f, 0xffffffffu, 1.f, 0.f };
	verts[2] = { 1.f, -1.f, 0xffffffffu, 1.f, 1.f };
	verts[3] = { -1.f, -1.f, 0xffffffffu, 0.f, 1.f };

	memset(&s_vbuf, 0, sizeof(s_vbuf));
	s_vbuf.flags = GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE;
	s_vbuf.elemSize = sizeof(VideoVert);
	s_vbuf.elemCount = 4;
	if(!GX2RCreateBuffer(&s_vbuf))
		return false;
	{
		void *p = GX2RLockBufferEx(&s_vbuf, GX2R_RESOURCE_BIND_NONE);
		if(!p)
			return false;
		memcpy(p, verts, sizeof(verts));
		GX2RUnlockBufferEx(&s_vbuf, GX2R_RESOURCE_BIND_NONE);
	}

	s_render_ready = true;
	return true;
}

// Solo desde el hilo principal. Crea (o recrea si cambian las
// dimensiones) las texturas GX2 NV12.
static bool textures_alloc(int w, int h)
{
	if(s_tex_ready && s_tex_w == w && s_tex_h == h)
		return true;
	if(s_tex_ready)
	{
		GX2RDestroySurfaceEx(&s_tex_y.surface, GX2R_RESOURCE_BIND_NONE);
		GX2RDestroySurfaceEx(&s_tex_uv.surface, GX2R_RESOURCE_BIND_NONE);
		s_tex_ready = false;
		s_tex_w = s_tex_h = 0;
	}

	const GX2RResourceFlags flags =
		GX2R_RESOURCE_BIND_TEXTURE |
		GX2R_RESOURCE_USAGE_CPU_WRITE |
		GX2R_RESOURCE_USAGE_CPU_READ |
		GX2R_RESOURCE_USAGE_GPU_WRITE |
		GX2R_RESOURCE_USAGE_GPU_READ |
		GX2R_RESOURCE_USAGE_FORCE_MEM2;

	memset(&s_tex_y, 0, sizeof(s_tex_y));
	s_tex_y.surface.width = w;
	s_tex_y.surface.height = h;
	s_tex_y.surface.depth = 1;
	s_tex_y.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
	s_tex_y.surface.format = GX2_SURFACE_FORMAT_UNORM_R8;
	s_tex_y.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	s_tex_y.surface.mipLevels = 1;
	s_tex_y.viewNumMips = 1;
	s_tex_y.viewNumSlices = 1;
	s_tex_y.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
	GX2CalcSurfaceSizeAndAlignment(&s_tex_y.surface);
	GX2InitTextureRegs(&s_tex_y);
	if(!GX2RCreateSurface(&s_tex_y.surface, flags))
		return false;

	memset(&s_tex_uv, 0, sizeof(s_tex_uv));
	s_tex_uv.surface.width = w / 2;
	s_tex_uv.surface.height = h / 2;
	s_tex_uv.surface.depth = 1;
	s_tex_uv.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
	s_tex_uv.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8;
	s_tex_uv.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	s_tex_uv.surface.mipLevels = 1;
	s_tex_uv.viewNumMips = 1;
	s_tex_uv.viewNumSlices = 1;
	s_tex_uv.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
	GX2CalcSurfaceSizeAndAlignment(&s_tex_uv.surface);
	GX2InitTextureRegs(&s_tex_uv);
	if(!GX2RCreateSurface(&s_tex_uv.surface, flags))
	{
		GX2RDestroySurfaceEx(&s_tex_y.surface, GX2R_RESOURCE_BIND_NONE);
		return false;
	}

	s_tex_ready = true;
	s_tex_w = w;
	s_tex_h = h;
	return true;
}

// ---------------------------------------------------------------------------
// Buffers de frame (framebuffer del decoder + doble buffer de display)
// Solo malloc: pueden tocarse desde el hilo de video (bajo lock).
// ---------------------------------------------------------------------------

static void frame_bufs_free()
{
	if(s_dec_fb)
	{
		free(s_dec_fb);
		s_dec_fb = NULL;
		s_dec_fb_size = 0;
	}
	for(int i = 0; i < 3; i++)
	{
		if(s_disp[i])
		{
			free(s_disp[i]);
			s_disp[i] = NULL;
		}
	}
	s_disp_size = 0;
	s_disp_ready = -1;
	s_disp_reading = -1;
}

static bool frame_bufs_alloc(int w, int h)
{
	frame_bufs_free();

	s_pitch = ((size_t)w + 255u) & ~(size_t)255u;
	// Filas de holgura por si el decoder alinea la altura a macrobloques,
	// mas margen: nunca debe escribir fuera.
	s_dec_fb_size = s_pitch * ((size_t)h + 32u) * 3u / 2u + 4096u;
	s_dec_fb = (uint8_t *)memalign(1024, s_dec_fb_size);
	if(!s_dec_fb)
		return false;

	// Mismo tamano (con holgura) que s_dec_fb: el decoder va a escribir
	// aqui directamente y nunca debe pasarse del buffer.
	s_disp_size = s_dec_fb_size;
	for(int i = 0; i < 3; i++)
	{
		// 1024 como Gary: alineacion por si el decoder escribe por DMA.
		s_disp[i] = (uint8_t *)memalign(1024, s_disp_size);
		if(!s_disp[i])
			return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Decoder (h264.rpl — llamadas desde el hilo de video)
// ---------------------------------------------------------------------------

static void decoder_close()
{
	if(s_dec_mem && s_dec_open)
	{
		// Ignorar errores al cerrar (commit "Ignore errors when
		// closing decoder" de FFmpeg-wiiu).
		H264DECFlush(s_dec_mem);
		H264DECEnd(s_dec_mem);
		H264DECClose(s_dec_mem);
	}
	s_dec_open = false;
	if(s_dec_mem)
	{
		free(s_dec_mem);
		s_dec_mem = NULL;
	}
}

// Callback informativo: el frame se lee del framebuffer que se le pasa a
// Execute (probado en CoffeeFlix/FFmpeg-wiiu); aqui solo registramos que
// el decoder emitiio algo.
static void hw_output_cb(H264DecodeOutput *output)
{
	(void)output;
}

static bool decoder_open(int w, int h)
{
	s_dec_mem = memalign(64, DEC_MEM_REQ);
	if(!s_dec_mem)
	{
		s_dec_giveup = true;
		return false;
	}

	if(H264DECInitParam(DEC_MEM_REQ, s_dec_mem) != H264_ERROR_OK ||
	   H264DECSetParam_FPTR_OUTPUT(s_dec_mem, hw_output_cb) != H264_ERROR_OK ||
	   H264DECSetParam_OUTPUT_PER_FRAME(s_dec_mem, 1) != H264_ERROR_OK ||
	   H264DECOpen(s_dec_mem) != H264_ERROR_OK ||
	   H264DECBegin(s_dec_mem) != H264_ERROR_OK)
	{
		vlog("Video HW: fallo inicializando h264.rpl (0x%X mem req)",
		     (unsigned)DEC_MEM_REQ);
		free(s_dec_mem);
		s_dec_mem = NULL;
		s_dec_giveup = true;
		return false;
	}
	s_dec_open = true;

	if(!frame_bufs_alloc(w, h))
	{
		vlog("Video HW: sin memoria para framebuffers (%dx%d)", w, h);
		decoder_close();
		s_dec_giveup = true;
		return false;
	}
	s_w = w;
	s_h = h;
	return true;
}

// ---------------------------------------------------------------------------
// API publica
// ---------------------------------------------------------------------------

void wiiu_video_set_log(WiiUVideoLogFn fn)
{
	s_logfn = fn;
}

// Escaneo Annex-B propio: ¿contiene la muestra un NAL de tipo 7 (SPS)?
// H264DECGetImageSize promete "el primer SPS encontrado", pero no nos
// fiamos de su comportamiento con buffers sin SPS (P-frames): un falso
// positivo ahi reabriria el decoder (40 MB) en cada frame y rellenaria
// el log. Asi solo se consulta dimensions cuando hay un SPS real.
static bool buf_has_sps_nal(const uint8_t *buf, size_t size)
{
	if(size < 5)
		return false;
	for(size_t i = 0; i + 3 < size; i++)
	{
		if(buf[i] != 0 || buf[i + 1] != 0)
			continue;
		size_t pos;
		if(buf[i + 2] == 1)
			pos = i + 3; // startcode 3 bytes
		else if(i + 4 < size && buf[i + 2] == 0 && buf[i + 3] == 1)
			pos = i + 4; // startcode 4 bytes
		else
			continue;
		if((buf[pos] & 0x1f) == 7)
			return true; // SPS
	}
	return false;
}

// ---------------------------------------------------------------------------
// Hilo de decode: aqui se hace todo el trabajo pesado (SPS, reaperturas,
// H264DECExecute, copia NV12) fuera del hilo de red de chiaki.
// ---------------------------------------------------------------------------

static void decode_sample(uint8_t *buf, size_t buf_size)
{
	if(s_dec_giveup)
		return;

	int32_t w = 0, h = 0;
	const bool has_sps =
		buf_has_sps_nal(buf, buf_size) &&
		H264DECGetImageSize(buf, (int32_t)buf_size, 0, &w, &h) == H264_ERROR_OK &&
		w > 0 && h > 0;

	if(!s_dec_open)
	{
		if(!has_sps)
		{
			// Una sola advertencia: sin SPS no hay decoder.
			if(!s_logged_no_sps)
			{
				s_logged_no_sps = true;
				vlog("Video HW: muestra sin SPS valido (esperando cabecera)");
			}
			return;
		}
		OSLockMutex(&s_lock);
		bool ok = decoder_open(w, h);
		int aw = s_w, ah = s_h;
		size_t ap = s_pitch;
		OSUnlockMutex(&s_lock);
		if(!ok)
			return;
		if(!s_init_logged)
		{
			s_init_logged = true;
			vlog("Video HW: decoder h264.rpl listo %dx%d pitch=%u",
			     aw, ah, (unsigned)ap);
		}
	}
	else if(has_sps && (w != s_w || h != s_h))
	{
		// Cambio de resolucion en caliente: reabrir decoder y buffers.
		OSLockMutex(&s_lock);
		int ow = s_w, oh = s_h;
		decoder_close();
		bool ok = decoder_open(w, h);
		OSUnlockMutex(&s_lock);
		if(ok)
			vlog("Video HW: resolucion %dx%d -> %dx%d", ow, oh, w, h);
		if(!ok)
			return;
	}

	// NALs que el decoder marca como omitibles (SEI etc.)
	BOOL skip = FALSE;
	if(H264DECCheckSkipableFrame(buf, (int32_t)buf_size, &skip) == H264_ERROR_OK && skip)
	{
		s_stats.skipped++;
		return;
	}

	if(H264DECSetBitstream(s_dec_mem, buf, (uint32_t)buf_size, 0.0) != H264_ERROR_OK)
	{
		s_stats.errors++;
		if(s_logged_err_codes < 3)
		{
			s_logged_err_codes++;
			vlog("Video HW: SetBitstream fallo (%u bytes)",
			     (unsigned)buf_size);
		}
		return;
	}

	// Elegimos la ranura de display ANTES de Execute: el decoder escribe
	// DIRECTAMENTE en ella, sin copia intermedia (el memcpy NV12 de
	// 1.4 MB costaba ~5.6 ms: nos dejaba en ~102 fps frente a los ~120
	// que entran, caian ~19 frames/s por la cola y las referencias
	// H.264 se rompian => ghosting, restos del frame anterior pegados).
	// Con 3 ranuras siempre hay una que no lee main (reading) ni esta
	// pendiente (ready). Execute es sincrono: al volver, el frame esta
	// completo en la ranura y solo entonces se publica.
	OSLockMutex(&s_lock);
	const uint64_t t0 = OSGetTime();
	int idx = 0;
	for(int i = 0; i < 3; i++)
	{
		if(s_disp_reading != i && s_disp_ready != i)
		{
			idx = i;
			break;
		}
	}
	uint8_t *out = s_disp[idx];
	OSUnlockMutex(&s_lock);
	const uint64_t t1 = OSGetTime();

	const int res = (int)H264DECExecute(s_dec_mem, out);
	const uint64_t t2 = OSGetTime();
	if(res == EXEC_FRAME_OK)
	{
		OSLockMutex(&s_lock);
		const uint64_t t3 = OSGetTime();
		s_disp_ready = idx;
		OSUnlockMutex(&s_lock);

		// EMA x1/8: casi sin coste y estable entre stats.
		// lock = espera del pick; exec = H264DECExecute; mem = publicar
		// (ya NO hay copia: mem debe quedar ~0).
		s_lock_us = (uint32_t)(((uint64_t)s_lock_us * 7u +
					(uint32_t)((t1 - t0) / 125u)) / 8u);
		s_exec_us = (uint32_t)(((uint64_t)s_exec_us * 7u +
					(uint32_t)((t2 - t1) / 125u)) / 8u);
		s_mem_us = (uint32_t)(((uint64_t)s_mem_us * 7u +
					(uint32_t)((t3 - t2) / 125u)) / 8u);

		s_stats.decoded++;
		if(!s_logged_first_frame)
		{
			s_logged_first_frame = true;
			vlog("Video HW: primer frame decodificado");
		}
	}
	else
	{
		// Codigos fuera de 0xE4 esperables (la muestra SPS/PPS pura
		// no produce frame): se cuentan y solo se registran los
		// primeros distintos para no llenar el log.
		s_stats.errors++;
		if(s_logged_err_codes < 3)
		{
			s_logged_err_codes++;
			vlog("Video HW: Execute=0x%07X", (unsigned)res);
		}
	}
}

static int decode_thread_entry(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	while(s_dec_run)
	{
		int slot = -1;
		size_t len = 0;
		OSLockMutex(&s_q_lock);
		if(s_q_head != s_q_tail)
		{
			slot = s_q_head;
			len = s_q_len[slot];
		}
		OSUnlockMutex(&s_q_lock);

		if(slot < 0)
		{
			// Vacia: duerme poco para reaccionar rapido al proximo frame.
			OSSleepTicks(OSMillisecondsToTicks(2));
			continue;
		}

		// El slot es estable mientras head no avance: solo nosotros
		// lo leemos y el productor no escribe entre head y tail.
		decode_sample(s_q_buf[slot], len);

		OSLockMutex(&s_q_lock);
		s_q_head = (s_q_head + 1) % VQ_SLOTS;
		OSUnlockMutex(&s_q_lock);
	}
	return 0;
}

static void decode_thread_free_bufs()
{
	for(int i = 0; i < VQ_SLOTS; i++)
	{
		free(s_q_buf[i]);
		s_q_buf[i] = NULL;
	}
	free(s_dec_stack);
	s_dec_stack = NULL;
}

static void decode_thread_stop()
{
	if(!s_dec_alive)
		return;
	s_dec_run = 0;
	int ret = 0;
	OSJoinThread(s_dec_thread, &ret);
	s_dec_alive = 0;
	free(s_dec_thread);
	s_dec_thread = NULL;
	OSLockMutex(&s_q_lock);
	s_q_head = s_q_tail = 0;
	OSUnlockMutex(&s_q_lock);
	decode_thread_free_bufs();
}

static bool decode_thread_start()
{
	if(s_dec_alive)
		return true;

	s_q_head = s_q_tail = 0;
	for(int i = 0; i < VQ_SLOTS; i++)
	{
		s_q_buf[i] = (uint8_t *)malloc(VQ_SLOT_SZ);
		if(!s_q_buf[i])
		{
			vlog("Video HW: sin memoria para cola de decode");
			decode_thread_free_bufs();
			return false;
		}
	}
	// Patron identico al de SDL2-wiiu (SDL_systhread.c): OSThread y pila
	// alineados a 16 bytes, y el puntero que recibe OSCreateThread es la
	// CIMA de la pila (base + tamano), no la base. Con la base tal cual
	// (v1) el OS rechazaba la creacion y no se decodificaba nada.
	s_dec_thread = (OSThread *)memalign(16, sizeof(OSThread));
	s_dec_stack = (uint8_t *)memalign(16, DEC_STACK_SZ);
	if(!s_dec_thread || !s_dec_stack)
	{
		vlog("Video HW: sin memoria para el hilo de decode");
		free(s_dec_thread);
		s_dec_thread = NULL;
		decode_thread_free_bufs();
		return false;
	}

	s_dec_run = 1;
	// Heredamos la prioridad del hilo llamador, exactamente igual que
	// SDL2-wiiu: es un valor valido por construccion (el hilo que llama
	// esta corriendo con el). Pasar 100 a secas hacia que OSCreateThread
	// devolviera FALSE (las v3/v4: "fallo creando el hilo de decode").
	const int32_t prio = OSGetThreadPriority(OSGetCurrentThread());
	if(!OSCreateThread(s_dec_thread, decode_thread_entry, 0, NULL,
	                   s_dec_stack + DEC_STACK_SZ, DEC_STACK_SZ,
	                   prio, OS_THREAD_ATTRIB_AFFINITY_ANY))
	{
		vlog("Video HW: fallo creando el hilo de decode (prio=%d)",
		     (int)prio);
		s_dec_run = 0;
		free(s_dec_thread);
		s_dec_thread = NULL;
		decode_thread_free_bufs();
		return false;
	}
	OSResumeThread(s_dec_thread);
	s_dec_alive = 1;
	return true;
}

bool wiiu_video_sample(uint8_t *buf, size_t buf_size)
{
	if(!buf || !buf_size)
		return true;

	lock_inited();
	s_stats.fed++;

	if(s_dec_giveup)
		return true;

	if(!decode_thread_start())
		return true;

	// Copia del bitstream comprimido en la cola (us); el decode va en el
	// hilo interno. Si la cola esta llena se suelta el frame en vez de
	// bloquear la red: nunca podemos frenar la recepcion UDP.
	OSLockMutex(&s_q_lock);
	const int next = (s_q_tail + 1) % VQ_SLOTS;
	if(next == s_q_head)
	{
		s_stats.dropped++;
		OSUnlockMutex(&s_q_lock);
		if(s_stats.dropped == 1)
			vlog("Video HW: cola de decode llena, frame descartado");
		return true;
	}
	const size_t n = (buf_size > VQ_SLOT_SZ) ? (size_t)VQ_SLOT_SZ : buf_size;
	memcpy(s_q_buf[s_q_tail], buf, n);
	s_q_len[s_q_tail] = n;
	s_q_tail = next;
	OSUnlockMutex(&s_q_lock);
	return true;
}

// Solo hilo principal. Init perezoso de GX2 (aqui o en reset) + subida +
// draw. GX2RLock/Unlock ocurren bajo lock; el draw va fuera porque el
// hilo de video ya no toca GX2.
void wiiu_video_render(int viewport_w, int viewport_h)
{
	bool draw = false;

	if(!s_lock_inited)
		return;

	OSLockMutex(&s_lock);

	if(s_dec_open && s_w > 0)
	{
		if(!s_render_ready && !s_render_err_logged)
		{
			if(!render_init())
			{
				s_render_err_logged = true;
				vlog("Video HW: fallo cargando shaders GX2");
			}
		}
		if(s_render_ready && !textures_alloc(s_w, s_h) && !s_render_err_logged)
		{
			s_render_err_logged = true;
			vlog("Video HW: sin memoria para texturas (%dx%d)", s_w, s_h);
		}
	}

	int up = -1;
	if(s_render_ready && s_tex_ready && s_disp_ready >= 0)
	{
		// Reclamamos el frame y marcamos el slot en lectura ANTES de
		// soltar el lock: la subida a textura (2.1 MB) sale fuera del
		// lock para no bloquear al hilo de decode (lock= medido hasta
		// 3.9 ms/frame); el decode jamas escribe el slot en lectura.
		up = s_disp_ready;
		s_disp_ready = -1;
		s_disp_reading = up;
	}
	// Retener el ultimo frame: una vez subido a textura se redibuja en
	// cada pasada; si no, la pantalla volvia al color de clear entre
	// frames y se veia un parpadeo (el pass DRC ademas "consumia" el
	// frame que el pass TV ya habia subido y quedaba sin dibujar).
	draw = s_render_ready && s_tex_ready && (up >= 0 || s_tex_has_frame);

	OSUnlockMutex(&s_lock);

	if(up >= 0)
	{
		const uint8_t *src = s_disp[up];
		size_t y_bytes = s_pitch * (size_t)s_h;
		size_t uv_bytes = y_bytes / 2;

		// GX2Surface.pitch esta en TEXELES, no en bytes (lo confirma
		// SDL2-wiiu: `surface.pitch * BytesPerPixel`). Y es R8
		// (1 byte/texel): pitch=1280 = s_pitch y andaba bien por ese
		// coincidecias; UV es R8_G8 (2 bytes/texel): pitch=640 y se
		// usaba 640 como stride de BYTES -> filas solapadas escribiendo
		// solo ~la mitad del plano de croma, y la mitad inferior de la
		// textura UV quedaba con la basura inicial de MEM2: croma cero
		// = tinte verde transparente con la luminancia visible debajo
		// (exactamente el simptoma observado).
		const size_t y_dst_pitch = (size_t)s_tex_y.surface.pitch;
		const size_t uv_dst_pitch = (size_t)s_tex_uv.surface.pitch * 2;

		void *img = GX2RLockSurfaceEx(&s_tex_y.surface, 0, GX2R_RESOURCE_BIND_NONE);
		if(img)
		{
			if(y_dst_pitch == s_pitch)
				memcpy(img, src, y_bytes);
			else
				for(int r = 0; r < s_h; r++)
					memcpy((uint8_t *)img + (size_t)r * y_dst_pitch,
					       src + (size_t)r * s_pitch, (size_t)s_w);
			DCFlushRange(img, s_tex_y.surface.imageSize);
			GX2RUnlockSurfaceEx(&s_tex_y.surface, 0, GX2R_RESOURCE_BIND_NONE);
		}
		else
			vlog("Video HW: GX2RLock textura Y falla");

		img = GX2RLockSurfaceEx(&s_tex_uv.surface, 0, GX2R_RESOURCE_BIND_NONE);
		if(img)
		{
			if(uv_dst_pitch == s_pitch)
				memcpy(img, src + y_bytes, uv_bytes);
			else
				for(int r = 0; r < s_h / 2; r++)
					memcpy((uint8_t *)img + (size_t)r * uv_dst_pitch,
					       src + y_bytes + (size_t)r * s_pitch,
					       (size_t)s_w * 2);
			DCFlushRange(img, s_tex_uv.surface.imageSize);
			GX2RUnlockSurfaceEx(&s_tex_uv.surface, 0, GX2R_RESOURCE_BIND_NONE);
		}
		else
			vlog("Video HW: GX2RLock textura UV falla");

		OSLockMutex(&s_lock);
		s_disp_reading = -1;
		s_tex_has_frame = true;
		OSUnlockMutex(&s_lock);
	}

	if(!draw)
		return;

	// Estado de dibujo (mismas llamadas que SDL2-wiiu SetDrawState).
	GX2SetViewport(0.f, 0.f, (float)viewport_w, (float)viewport_h, 0.f, 1.f);
	GX2SetScissor(0, 0, (uint32_t)viewport_w, (uint32_t)viewport_h);
	GX2SetColorControl(GX2_LOGIC_OP_COPY, 0x00, FALSE, TRUE);

	GX2SetVertexUniformReg(s_vs->uniformVars[0].offset, 16, s_identity);
	GX2SetFetchShader(&s_fetch);
	GX2SetVertexShader(s_vs);
	GX2SetPixelShader(s_ps);
	GX2RSetAttributeBuffer(&s_vbuf, 0, sizeof(VideoVert), 0);

	const uint32_t loc0 = s_ps->samplerVars[0].location;
	const uint32_t loc1 = s_ps->samplerVars[1].location;
	GX2SetPixelTexture(&s_tex_y, loc0);
	GX2SetPixelSampler(&s_samp, loc0);
	GX2SetPixelTexture(&s_tex_uv, loc1);
	GX2SetPixelSampler(&s_samp, loc1);

	GX2DrawEx(GX2_PRIMITIVE_MODE_QUADS, 4, 0, 1);
}

void wiiu_video_reset()
{
	lock_inited();

	// Para el hilo de decode y vacia la cola. La sesion siempre se une
	// (chiaki_session_join) antes de llamar aqui, asi que nadie puede
	// estar encolando mientras paramos.
	decode_thread_stop();

	// El cierre se hace desde el hilo principal (inicio/parada/shutdown)
	// y siempre con el hilo de video ya unido o todavia no creado.
	if(s_render_ready || s_tex_ready)
		GX2DrawDone();

	OSLockMutex(&s_lock);
	decoder_close();
	frame_bufs_free();
	render_free();
	s_w = 0;
	s_h = 0;
	s_disp_ready = -1;
	s_disp_reading = -1;
	s_dec_giveup = false;
	s_init_logged = false;
	s_render_err_logged = false;
	s_logged_first_frame = false;
	s_logged_no_sps = false;
	s_logged_err_codes = 0;
	memset(&s_stats, 0, sizeof(s_stats));
	OSUnlockMutex(&s_lock);
}

WiiUVideoStats wiiu_video_stats()
{
	WiiUVideoStats st;
	lock_inited();
	OSLockMutex(&s_lock);
	st = s_stats;
	st.width = (uint32_t)s_w;
	st.height = (uint32_t)s_h;
	st.exec_us = s_exec_us;
	st.lock_us = s_lock_us;
	st.mem_us = s_mem_us;
	OSUnlockMutex(&s_lock);
	return st;
}
