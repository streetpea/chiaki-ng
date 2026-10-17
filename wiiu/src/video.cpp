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
// ver wiiu_video_upload). El decode jamas escribe este slot.
static int s_disp_reading = -1;
// Sello de tiempo (OSGetTime) de cada slot: cuando chiaki entrega la
// muestra. La diferencia al subirla = edad real del frame (latencia).
static uint64_t s_disp_stamp[3];

// cola de tramas comprimidas (productor: hilo de chiaki en
// wiiu_video_sample; consumidor: el hilo interno de decode)
static OSMutex s_q_lock;
static uint8_t *s_q_buf[VQ_SLOTS];
static size_t s_q_len[VQ_SLOTS];
static uint64_t s_q_stamp[VQ_SLOTS]; // sello por slot (ver arriba)
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
// Pares de texturas NV12 doble-buffer: mientras la GPU dibuja el par
// DELANTERO, el hilo principal sube al TRASERO. Con un solo par, el
// Lock/Unlock de GX2R se peleaba con la GPU en dibujo y la subida
// costaba ~7.8 ms/frame (bucle principal a 21 Hz = ~20 fps visibles).
// Con 2 pares la subida seguia en ~7.8 ms (gui-44): el par trasero era
// el que la GPU acababa de dibujar en la vuelta anterior y su Lock
// esperaba al GPU. Con 3 pares el trasero lleva DOS vueltas dibujado
// (comandos ya ejecutados) y el Lock vuelve al instante: la subida
// queda como su mero coste CPU (memcpy + DCFlush).
#define TEX_PAIRS 3
static GX2Texture s_tex_y[TEX_PAIRS];
static GX2Texture s_tex_uv[TEX_PAIRS];
static GX2RBuffer s_vbuf;
static bool s_render_ready;
static bool s_tex_ready;
static int s_tex_w, s_tex_h;
static int s_tex_front = -1;      // par que dibuja la GPU (-1 = ninguno)
static bool s_init_logged;      // "decoder listo" ya impreso
static bool s_render_err_logged;

// --- Subida directa a textura (gui-46) -----------------------------------
// El upload CPU clasico cuesta ~7.8 ms/frame en 720p (mismo ~178 MB/s
// en 540p: es memcpy de 1.38 MB + DCFlush, pura banda) y es la parte
// grande de la latencia que queda de lo nuestro: sin caber en la
// ventana de 16,7 ms del retrace, el bucle se queda en ~50 vueltas/s
// y algunos frames esperan 33 ms en vez de 16,7.
//
// La solucion: cada par de texturas GX2 se crea SOBRE la memoria de un
// "backing" NV12 completo (h + h/2 filas R8, pitch = align(w,256)) con
// GX2RCreateSurfaceUserMemory — Y sobre el inicio, UV sobre
// image + pitch*height, que es EXACTAMENTE donde el decoder HW escribe
// su NV12. El decoder pega ahi directo y al hilo principal solo le
// queda invalidar la caché de textura de la GPU y flipear: cero
// memcpy, cero DCFlush. GX2 NO sale del hilo principal.
//
// SEGURIDAD DE PROPIEDAD: a las superficies de memoria de usuario JAMAS
// se les llama GX2RDestroySurfaceEx (no poseen memoria — la posee el
// backing, que es una superficie GX2R normal y SI se destruye). Asi el
// codigo es correcto bajo cualquier semantica de Destroy. Si la
// creacion falla o el backing trae alineacion < 1024 (lo que usa el
// decoder) o UV no cae alineado, se cae a la copia CPU clasica con un
// unico log: sin riesgo, con medible.
static bool s_tex_usermem;       // los pares son memoria de usuario
static GX2Texture s_bk[TEX_PAIRS];      // backings NV12 (poseen la mem)
static bool s_bk_ready[TEX_PAIRS];
static uint8_t *s_bk_img[TEX_PAIRS];    // image del backing i

static WiiUVideoStats s_stats;
static bool s_logged_first_frame;
static bool s_logged_no_sps;
static int s_logged_err_codes;
static uint32_t s_exec_us; // EMA de H264DECExecute (µs, 125 ticks = 1 µs)
static uint32_t s_lock_us; // EMA de espera del mutex de display (µs)
static uint32_t s_mem_us;  // EMA de la copia NV12 al display (µs)
static uint32_t s_upload_us; // EMA de la subida CPU a textura GX2 (µs)
static uint32_t s_age_us;    // EMA de la edad del frame al subirlo (µs)
static uint64_t s_dec_stamp; // sello del sample en curso (hilo decode)

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
		for(int i = 0; i < TEX_PAIRS; i++)
		{
			// Pares de memoria de usuario: no poseen nada (su
			// memoria es del backing) — jamas destruirlos.
			if(!s_tex_usermem)
			{
				GX2RDestroySurfaceEx(&s_tex_y[i].surface,
						     GX2R_RESOURCE_BIND_NONE);
				GX2RDestroySurfaceEx(&s_tex_uv[i].surface,
						     GX2R_RESOURCE_BIND_NONE);
			}
		}
		s_tex_ready = false;
		s_tex_w = s_tex_h = 0;
	}
	// Los backings poseen su memoria SIEMPRE (tanto en modo directo
	// como si quedaron a medias de un intento fallido).
	for(int i = 0; i < TEX_PAIRS; i++)
	{
		if(s_bk_ready[i])
		{
			GX2RDestroySurfaceEx(&s_bk[i].surface,
					     GX2R_RESOURCE_BIND_NONE);
			s_bk_ready[i] = false;
			s_bk_img[i] = NULL;
		}
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
	s_tex_front = -1;
	s_tex_usermem = false;
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

// Crea un par de texturas NV12 (Y R8 + UV R8_G8). Con user_y/user_uv
// no NULL las texturas se crean SOBRE esa memoria (gui-46, ver
// declaracion de s_tex_usermem): GX2RCreateSurfaceUserMemory y NUNCA
// destruirlas despues. En fallo destruye lo que hubiera creado de ese
// par (salvo superficies de usuario, que no poseen nada).
static bool tex_pair_create(GX2Texture *tex_y, GX2Texture *tex_uv, int w,
			    int h, GX2RResourceFlags flags,
			    const uint8_t *user_y, const uint8_t *user_uv)
{
	memset(tex_y, 0, sizeof(*tex_y));
	tex_y->surface.width = w;
	tex_y->surface.height = h;
	tex_y->surface.depth = 1;
	tex_y->surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
	tex_y->surface.format = GX2_SURFACE_FORMAT_UNORM_R8;
	tex_y->surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	tex_y->surface.mipLevels = 1;
	tex_y->viewNumMips = 1;
	tex_y->viewNumSlices = 1;
	tex_y->compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
	GX2CalcSurfaceSizeAndAlignment(&tex_y->surface);
	GX2InitTextureRegs(tex_y);
	if(user_y)
	{
		// Las alineaciones que pide cada textura tienen que quedar
		// cubiertas por como caiga la memoria o
		// GX2RCreateSurfaceUserMemory / la GPU no estarian contentos.
		if(tex_y->surface.alignment &&
		   ((uintptr_t)user_y % tex_y->surface.alignment))
		{
			vlog("Video HW: memoria Y no alineada (%u) — sin subida directa",
			     (unsigned)tex_y->surface.alignment);
			return false;
		}
		if(!GX2RCreateSurfaceUserMemory(&tex_y->surface,
						 (uint8_t *)user_y, NULL,
						 flags))
			return false;
	}
	else if(!GX2RCreateSurface(&tex_y->surface, flags))
		return false;

	memset(tex_uv, 0, sizeof(*tex_uv));
	tex_uv->surface.width = w / 2;
	tex_uv->surface.height = h / 2;
	tex_uv->surface.depth = 1;
	tex_uv->surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
	tex_uv->surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8;
	tex_uv->surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	tex_uv->surface.mipLevels = 1;
	tex_uv->viewNumMips = 1;
	tex_uv->viewNumSlices = 1;
	tex_uv->compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
	GX2CalcSurfaceSizeAndAlignment(&tex_uv->surface);
	GX2InitTextureRegs(tex_uv);
	{
		BOOL ok;
		if(user_uv)
		{
			if(tex_uv->surface.alignment &&
			   ((uintptr_t)user_uv % tex_uv->surface.alignment))
			{
				vlog("Video HW: memoria UV no alineada (%u) — sin subida directa",
				     (unsigned)tex_uv->surface.alignment);
				if(!user_y)
					GX2RDestroySurfaceEx(&tex_y->surface,
							     GX2R_RESOURCE_BIND_NONE);
				return false;
			}
			ok = GX2RCreateSurfaceUserMemory(&tex_uv->surface,
							 (uint8_t *)user_uv,
							 NULL, flags);
		}
		else
			ok = GX2RCreateSurface(&tex_uv->surface, flags);
		if(!ok)
		{
			if(!user_y)
				GX2RDestroySurfaceEx(&tex_y->surface,
						     GX2R_RESOURCE_BIND_NONE);
			return false;
		}
	}

	return true;
}

// Crea el backing NV12 completo del par idx: una superficie GX2R NORMAL
// de h + h/2 filas R8 (Y + UV contiguos con el pitch que el decoder
// tambien usa). De aqui saldra la memoria sobre la que apuntaran las
// texturas del par y donde pegara el decoder. Devuelve false con el
// motivo ya logueado.
static bool backing_create(int idx, int w, int h, GX2RResourceFlags flags)
{
	GX2Texture *bk = &s_bk[idx];
	memset(bk, 0, sizeof(*bk));
	bk->surface.width = w;
	bk->surface.height = h + h / 2;
	bk->surface.depth = 1;
	bk->surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
	bk->surface.format = GX2_SURFACE_FORMAT_UNORM_R8;
	bk->surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	bk->surface.mipLevels = 1;
	bk->viewNumMips = 1;
	bk->viewNumSlices = 1;
	bk->compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
	GX2CalcSurfaceSizeAndAlignment(&bk->surface);
	GX2InitTextureRegs(bk);
	if(!GX2RCreateSurface(&bk->surface, flags))
	{
		vlog("Video HW: GX2RCreateSurface del backing fallo");
		return false;
	}
	uint8_t *img =
		(uint8_t *)GX2RLockSurfaceEx(&bk->surface, 0,
					     GX2R_RESOURCE_BIND_NONE);
	if(!img)
	{
		GX2RDestroySurfaceEx(&bk->surface, GX2R_RESOURCE_BIND_NONE);
		vlog("Video HW: Lock del backing fallo");
		return false;
	}
	GX2RUnlockSurfaceEx(&bk->surface, 0, GX2R_RESOURCE_BIND_NONE);
	// El requisito del GPU (surface.alignment) puede ser 256, pero lo
	// que tiene que cumplir el DECODER es la alineacion del puntero
	// que le damos (memalign 1024 en el framebuffer clasico). El pool
	// GX2R reparte por paginas: el puntero suele venir a 4096 aunque
	// el requisito sea 256. Se comprueba EL PUNTERO, no el requisito
	// (gui-46 miro el requisito y desactivo la directa sin motivo).
	// UV = img + pitch*h siempre queda 1024-alineado detras (pitch
	// multiplo de 256 y h multiplo de 16 => producto multiplo de
	// 4096), asi que basta chequear Y.
	if((uintptr_t)img & 1023u)
	{
		vlog("Video HW: backing en %p no alineado a 1024 (lo pide el decoder) — sin subida directa",
		     img);
		GX2RDestroySurfaceEx(&bk->surface, GX2R_RESOURCE_BIND_NONE);
		return false;
	}
	s_bk_img[idx] = img;
	s_bk_ready[idx] = true;
	return true;
}

// Solo desde el hilo principal. Crea (o recrea si cambian las
// dimensiones) LOS DOS pares de texturas GX2 NV12 — o, si se puede,
// los pares sobre backings NV12 enteros (subida directa, gui-46).
// En fallo devuelve false con lo que llegara a crearse ya limpiado.
static bool textures_alloc(int w, int h)
{
	if(s_tex_ready && s_tex_w == w && s_tex_h == h)
		return true;
	if(s_tex_ready)
	{
		for(int i = 0; i < TEX_PAIRS; i++)
		{
			// Pares de memoria de usuario: no poseen nada (su
			// memoria es del backing) — jamas destruirlos.
			if(!s_tex_usermem)
			{
				GX2RDestroySurfaceEx(&s_tex_y[i].surface,
						     GX2R_RESOURCE_BIND_NONE);
				GX2RDestroySurfaceEx(&s_tex_uv[i].surface,
						     GX2R_RESOURCE_BIND_NONE);
			}
		}
		s_tex_ready = false;
		s_tex_w = s_tex_h = 0;
	}
	// Los backings poseen su memoria SIEMPRE: se liberan aqui (y en
	// render_free) aunque s_tex_ready este a 0.
	for(int i = 0; i < TEX_PAIRS; i++)
	{
		if(s_bk_ready[i])
		{
			GX2RDestroySurfaceEx(&s_bk[i].surface,
					     GX2R_RESOURCE_BIND_NONE);
			s_bk_ready[i] = false;
			s_bk_img[i] = NULL;
		}
	}

	const GX2RResourceFlags flags =
		GX2R_RESOURCE_BIND_TEXTURE |
		GX2R_RESOURCE_USAGE_CPU_WRITE |
		GX2R_RESOURCE_USAGE_CPU_READ |
		GX2R_RESOURCE_USAGE_GPU_WRITE |
		GX2R_RESOURCE_USAGE_GPU_READ |
		GX2R_RESOURCE_USAGE_FORCE_MEM2;

	// --- Intento de subida directa (gui-46): 3 backings NV12 y las
	// texturas del par apuntando a su memoria (Y al inicio, UV tras
	// pitch*height). Cualquier golpe (alineacion, creacion) cae a la
	// copia CPU clasica sin mas que un log.
	bool direct = true;
	for(int i = 0; direct && i < TEX_PAIRS; i++)
		direct = backing_create(i, w, h, flags);
	if(direct)
	{
		for(int i = 0; direct && i < TEX_PAIRS; i++)
		{
			const uint8_t *uv =
				s_bk_img[i] +
				(size_t)s_bk[i].surface.pitch * (size_t)h;
			direct = tex_pair_create(&s_tex_y[i], &s_tex_uv[i],
						 w, h, flags, s_bk_img[i],
						 uv);
		}
	}
	if(direct)
		vlog("Video HW: subida directa a textura activa (decoder->GX2R, sin memcpy)");
	if(!direct)
	{
		// Limpieza del intento: backings SI (poseen su memoria),
		// pares de usuario NO; structs a cero para no dejar
		// superficies a medio crear en pie.
		for(int i = 0; i < TEX_PAIRS; i++)
		{
			memset(&s_tex_y[i], 0, sizeof(s_tex_y[i]));
			memset(&s_tex_uv[i], 0, sizeof(s_tex_uv[i]));
			if(s_bk_ready[i])
			{
				GX2RDestroySurfaceEx(&s_bk[i].surface,
						     GX2R_RESOURCE_BIND_NONE);
				s_bk_ready[i] = false;
				s_bk_img[i] = NULL;
			}
		}
		for(int i = 0; i < TEX_PAIRS; i++)
		{
			if(!tex_pair_create(&s_tex_y[i], &s_tex_uv[i], w, h,
					    flags, NULL, NULL))
			{
				// El par i ya se auto-limpio; libera los previos.
				for(int j = 0; j < i; j++)
				{
					GX2RDestroySurfaceEx(&s_tex_y[j].surface,
							     GX2R_RESOURCE_BIND_NONE);
					GX2RDestroySurfaceEx(&s_tex_uv[j].surface,
							     GX2R_RESOURCE_BIND_NONE);
				}
				return false;
			}
		}
	}
	s_tex_usermem = direct;
	// Frame pendiente creado con el modo/geometria anterior: se
	// descarga (sin esto, el primer flip directo mostraria memoria
	// que el decoder jamas escribio — memoria basura en pantalla).
	s_disp_ready = -1;
	s_tex_ready = true;
	s_tex_front = -1;
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
	// Subida directa: la ranura ES la memoria de la textura del mismo
	// indice, asi que hay que esquivar ademas a la que dibuja la GPU
	// (front); con 3 ranuras y solo {front, ready} bloqueando, siempre
	// sobra una. En modo copia vale la logica clasica (reading/ready).
	// El predicado es el mismo (y con los mismos campos, bajo este
	// mismo lock) que el de wiiu_video_upload: ambos lados ven siempre
	// el mismo modo.
	const bool direct = s_tex_usermem && s_tex_ready &&
			    s_tex_w == s_w && s_tex_h == s_h;
	int idx = 0;
	for(int i = 0; i < 3; i++)
	{
		const bool busy = direct
			? (s_tex_front == i || s_disp_ready == i)
			: (s_disp_reading == i || s_disp_ready == i);
		if(!busy)
		{
			idx = i;
			break;
		}
	}
	uint8_t *out = direct ? s_bk_img[idx] : s_disp[idx];
	OSUnlockMutex(&s_lock);
	const uint64_t t1 = OSGetTime();

	const int res = (int)H264DECExecute(s_dec_mem, out);
	const uint64_t t2 = OSGetTime();
	if(res == EXEC_FRAME_OK)
	{
		OSLockMutex(&s_lock);
		const uint64_t t3 = OSGetTime();
		s_disp_ready = idx;
		s_disp_stamp[idx] = s_dec_stamp; // arrastra la edad al publicar
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
			s_dec_stamp = s_q_stamp[slot];
		}
		OSUnlockMutex(&s_q_lock);

		if(slot < 0)
		{
			// Vacia: duerme lo justo para reaccionar rapido al
			// proximo frame (latencia minima del muestreo).
			OSSleepTicks(OSMillisecondsToTicks(1));
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
	// hilo interno. Si la cola esta llena se espera ACOTADO (~24 ms) a
	// que el hilo de decode consuma antes de soltar: descartar un frame
	// a la primera deja un hueco en las referencias H.264 y el decoder
	// se queda congelado hasta el proximo IDR. La red se frena solo un
	// instante y el audio no se entera (el ring AX tiene 170 ms).
	OSLockMutex(&s_q_lock);
	int next = (s_q_tail + 1) % VQ_SLOTS;
	for(int tries = 0; next == s_q_head && tries < 24; tries++)
	{
		OSUnlockMutex(&s_q_lock);
		OSSleepTicks(OSMillisecondsToTicks(1));
		OSLockMutex(&s_q_lock);
		next = (s_q_tail + 1) % VQ_SLOTS;
	}
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
	s_q_stamp[s_q_tail] = OSGetTime(); // origen de la edad del frame
	s_q_tail = next;
	OSUnlockMutex(&s_q_lock);
	return true;
}

// ---------------------------------------------------------------------------
// Presentacion (solo hilo principal)
//
// wiiu_video_upload() se llama UNA vez por vuelta del bucle y FUERA de
// los pases WHBGfx: reclama el frame listo del decoder y lo copia al
// par de texturas TRASERO; al terminar s_tex_front apunta al par nuevo
// y wiiu_video_render() (dentro de cada pass: TV y DRC) solo dibuja.
// Hasta gui-40 la subida estaba dentro de cada pass: el pass TV subia
// uno y, ~7.8 ms despues, el pass DRC subia OTRO (el decoder ya habia
// publicado el siguiente) => 2 copias por vuelta, bucle principal a
// 21 Hz y ~20 fps visibles.
// ---------------------------------------------------------------------------

void wiiu_video_upload()
{
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
	bool direct = false;
	if(s_render_ready && s_tex_ready && s_disp_ready >= 0)
	{
		// Mismo predicado que en el hilo de decode (bajo este lock):
		// ambos lados ven el mismo modo siempre. En directo no se
		// marca lectura — nadie copia nada; en modo copia se marca
		// ANTES de soltar el lock: el decode jamas escribe el slot
		// en lectura.
		direct = s_tex_usermem && s_tex_w == s_w && s_tex_h == s_h;
		up = s_disp_ready;
		s_disp_ready = -1;
		if(!direct)
			s_disp_reading = up;
	}

	OSUnlockMutex(&s_lock);

	if(up < 0)
		return;

	if(direct)
	{
		// Subida directa (gui-46): el decoder ya pego el frame en
		// la memoria de estas texturas — aqui solo invalidar la
		// caché de textura de la GPU (el Unlock, que era quien
		// invalidaba, no se llama nunca en memoria de usuario) y
		// flipear. GX2 se toca SOLO en el hilo principal.
		const uint64_t t_up0 = OSGetTime();
		// Edad del frame: desde que chiaki lo entrego hasta aqui
		// (cola + decode + espera al bucle; EMA x1/8).
		s_age_us = (uint32_t)(((uint64_t)s_age_us * 7u +
				       (uint32_t)((t_up0 - s_disp_stamp[up]) / 125u)) /
				      8u);
		// La GPU podia tener tiles de esta pareja cacheados de su
		// vez como frontal (hace DOS vueltas); su memoria cambio
		// por DMA de decoder sin pasar por CPU.
		GX2RInvalidateSurface(&s_tex_y[up].surface, 0,
				      GX2R_RESOURCE_BIND_NONE);
		GX2RInvalidateSurface(&s_tex_uv[up].surface, 0,
				      GX2R_RESOURCE_BIND_NONE);
		OSLockMutex(&s_lock);
		s_tex_front = up;
		OSUnlockMutex(&s_lock);
		s_stats.presented++;
		s_upload_us = (uint32_t)(((uint64_t)s_upload_us * 7u +
					  (uint32_t)((OSGetTime() - t_up0) / 125u)) /
					 8u);
		return;
	}

	{
		const uint64_t t_up0 = OSGetTime();
		// Par trasero: el siguiente al que dibuja la GPU. Con 3
		// pares ese par fue el frontal hace DOS vueltas. (En
		// gui-45 se creyó que el Lock esperaba a la GPU y que por
		// eso costaba 7.8 ms: FALSO — gui-46 midio la misma banda
		// ~178 MB/s en 540p que en 720p: el coste era la propia
		// copia + DCFlush. Por eso la subida directa.)
		const int back = (s_tex_front + 1) % TEX_PAIRS;
		// Edad del frame: desde que chiaki lo entrego hasta aqui
		// (cola + decode + espera al bucle; EMA x1/8).
		s_age_us = (uint32_t)(((uint64_t)s_age_us * 7u +
				       (uint32_t)((t_up0 - s_disp_stamp[up]) / 125u)) /
				      8u);
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
		const size_t y_dst_pitch = (size_t)s_tex_y[back].surface.pitch;
		const size_t uv_dst_pitch = (size_t)s_tex_uv[back].surface.pitch * 2;

		bool up_ok = true;
		void *img = GX2RLockSurfaceEx(&s_tex_y[back].surface, 0, GX2R_RESOURCE_BIND_NONE);
		if(img)
		{
			if(y_dst_pitch == s_pitch)
				memcpy(img, src, y_bytes);
			else
				for(int r = 0; r < s_h; r++)
					memcpy((uint8_t *)img + (size_t)r * y_dst_pitch,
					       src + (size_t)r * s_pitch, (size_t)s_w);
			DCFlushRange(img, s_tex_y[back].surface.imageSize);
			GX2RUnlockSurfaceEx(&s_tex_y[back].surface, 0, GX2R_RESOURCE_BIND_NONE);
		}
		else
		{
			up_ok = false;
			vlog("Video HW: GX2RLock textura Y falla");
		}

		img = GX2RLockSurfaceEx(&s_tex_uv[back].surface, 0, GX2R_RESOURCE_BIND_NONE);
		if(img)
		{
			if(uv_dst_pitch == s_pitch)
				memcpy(img, src + y_bytes, uv_bytes);
			else
				for(int r = 0; r < s_h / 2; r++)
					memcpy((uint8_t *)img + (size_t)r * uv_dst_pitch,
					       src + y_bytes + (size_t)r * s_pitch,
					       (size_t)s_w * 2);
			DCFlushRange(img, s_tex_uv[back].surface.imageSize);
			GX2RUnlockSurfaceEx(&s_tex_uv[back].surface, 0, GX2R_RESOURCE_BIND_NONE);
		}
		else
		{
			up_ok = false;
			vlog("Video HW: GX2RLock textura UV falla");
		}

		// EMA x1/8 de la subida (memcpy Y+UV + DCFlushRange):
		// es el coste CPU por frame de tener imagen en pantalla.
		s_stats.presented++;
		s_upload_us = (uint32_t)(((uint64_t)s_upload_us * 7u +
					 (uint32_t)((OSGetTime() - t_up0) / 125u)) /
					8u);

		OSLockMutex(&s_lock);
		s_disp_reading = -1;
		// El par trasero solo pasa a delantero si las dos copias
		// salieron bien: si no, la GPU dibujaria una textura a
		// medio escribir.
		if(up_ok)
			s_tex_front = back;
		OSUnlockMutex(&s_lock);
	}
}

// Solo hilo principal. Dibuja el ultimo par publicado (quad fullscreen
// NDC). Llamar dentro de WHBGfxBeginRenderTV/DRC: aqui no hay copias
// ni esperas, solo draw.
void wiiu_video_render(int viewport_w, int viewport_h)
{
	if(!s_lock_inited)
		return;

	OSLockMutex(&s_lock);
	const int front = s_tex_front;
	const bool draw = s_render_ready && s_tex_ready && front >= 0;
	OSUnlockMutex(&s_lock);

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
	GX2SetPixelTexture(&s_tex_y[front], loc0);
	GX2SetPixelSampler(&s_samp, loc0);
	GX2SetPixelTexture(&s_tex_uv[front], loc1);
	GX2SetPixelSampler(&s_samp, loc1);

	GX2DrawEx(GX2_PRIMITIVE_MODE_QUADS, 4, 0, 1);
}

void wiiu_video_reset(bool wait_gpu)
{
	lock_inited();

	// Para el hilo de decode y vacia la cola. La sesion siempre se une
	// (chiaki_session_join) antes de llamar aqui, asi que nadie puede
	// estar encolando mientras paramos.
	decode_thread_stop();

	// El cierre se hace desde el hilo principal (inicio/parada/shutdown)
	// y siempre con el hilo de video ya unido o todavia no creado.
	// wait_gpu=false en el SHUTDOWN (HOME con stream activo): ahi
	// GX2DrawDone explotaba con DSI en el helper de gx2 (contexto en
	// estado background) y el crash handler de wut suspendia el hilo
	// principal = consola congelada. Al salir no hace falta esperar: el
	// GPU termina sus draws sobre memoria que sigue mapeada.
	if(wait_gpu && (s_render_ready || s_tex_ready))
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
	st.upload_us = s_upload_us;
	st.age_us = s_age_us;
	OSUnlockMutex(&s_lock);
	return st;
}
