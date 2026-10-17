// ---------------------------------------------------------------------------
// GUI de menu sobre GX2 (solo hilo principal).
//
// Pipeline: reutiliza el vertex shader de video (GFD embebido en
// video_gsh.h: atributos a_position/a_color/a_texcoord y uniform
// u_projection) con una proyeccion en coordenadas de píxel, y un pixel
// shader propio (gui_gsh.h) que pinta color_de_vertice con alfa
// enmascarado por la textura: out = (color.rgb, color.a * tex.r).
//
// La textura (atlas de256x136) reparte a la izquierda la fuente8x8
// (dominio publico, dhepper/font8x8) en una rejilla16x16 de glifos
// (Unicode0-255; U+00A0-U+00FF del ext latin permite tildes/ñ reales)
// mas un bloque blanco en las filas inferiores (solidos: texel r=1 =>
// alfa pleno), y a la derecha los assets rasterizados de chiaki-ng
// (wordmark + silueta PS4 con su tira de luz).
//
// El batch se acumula en memoria CPU y cada flush escribe una de tres
// buffers GX2R rotativos, porque el GPU todavia puede estar leyendo la
// del frame anterior (GX2 es asincrono y no hay GX2DrawDone aqui a
// proposito: en el camino de shutdown causo DSI y congelo la consola).
// ---------------------------------------------------------------------------

#include "gui.h"

#include "../shaders/video_gsh.h" // GFD con el VS (se reutiliza tal cual)
#include "../shaders/gui_gsh.h"   // GFD con el PS (color x fuente)
#include "font8x8_basic.h"        // dhepper/font8x8 — dominio publico
#include "font8x8_ext_latin.h"
#include "gui_assets.h"           // logo + PS4 rasterizados de chiaki-ng
#include "gui_radar_assets.h"     // iconos discovery (discover-*-24px)

#include <gfd.h>
#include <gx2/context.h>
#include <gx2/draw.h>
#include <gx2/enum.h>
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

#include <whb/gfx.h> // WHBGfxGetTV/DRCContextState (estado2D fijo)

#include <coreinit/cache.h>

#include <malloc.h>
#include <string.h>

#define FONT_TEX_W 256 // mitad izq: glifos; mitad der: assets
#define FONT_TEX_H 224 // glifos (128) + blanco (8) + assets hasta 224
#define FONT_WHITE_ROW 128 // filas de bloque blanco para solidos
#define FONT_WHITE_H 8

// Assets de chiaki-ng en la mitad derecha del atlas (gui_assets.h).
#define GUI_ASSET_W (FONT_TEX_W / 2)
#define ASSET_LOGO_X (FONT_TEX_W / 2)
#define ASSET_LOGO_Y 0
#define ASSET_LOGO_H 76 // wordmark2044x1215 ->128x76
#define ASSET_BODY_Y 80
#define ASSET_BODY_H 22 // consola PS4 (viewBox135x22.75) ->128x22
#define ASSET_LIGHT_Y 104
#define ASSET_LIGHT_H 22
#define ASSET_RADAR_Y 128
#define ASSET_RADAR_S 64            // discover-24px -> 64x64
#define ASSET_RADAR_ON_X ASSET_LOGO_X
#define ASSET_RADAR_OFF_X (ASSET_LOGO_X + ASSET_RADAR_S)
#define ASSET_DISC_Y 192
#define ASSET_DISC_S 32            // disco de los chips de boton
#define ASSET_DISC_X ASSET_LOGO_X
#define ASSET_OFF_S 32             // icono off (off.svg), junto al disco
#define ASSET_OFF_X (ASSET_DISC_X + ASSET_DISC_S)
#define ASSET_OFF_Y ASSET_DISC_Y
// Superficie del touchpad en la mitad IZQUIERDA: filas 136+ son virgenes
// (los glifos acaban en la 127 y el bloque blanco ocupa 128-135).
#define ASSET_TP_X 0
#define ASSET_TP_Y 136
#define ASSET_TP_W 128
#define ASSET_TP_H 64

#define MAX_VERTS 4096 //1024 quads por frame (el menu usa ~500)

struct GuiVert
{
	float x, y;
	uint32_t color;
	float u, v;
};

static bool s_ready;
static GX2VertexShader *s_vs;
static GX2PixelShader *s_ps;
static GX2FetchShader s_fetch;
static void *s_fetch_prog;
static GX2Sampler s_samp;
static GX2Texture s_font;
static GX2RBuffer s_vbuf[3];
static int s_slot;

// Batch CPU (main thread)
static GuiVert s_verts[MAX_VERTS];
static int s_nv;
static bool s_in_begin;
static int s_vw, s_vh;
static float s_proj[16];

// ---------------------------------------------------------------------------
// Carga de shaders GFD — copia de video.cpp (load_gfd_vs/ps) para no
// tocar el pipeline de video validado en hardware.
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

// ---------------------------------------------------------------------------
// Textura de fuente
// ---------------------------------------------------------------------------

static bool font_create(void)
{
	// Mismos flags que las texturas NV12 de video (LINEAR_ALIGNED + R8,
	// patrón probado en hardware).
	const GX2RResourceFlags flags =
		GX2R_RESOURCE_BIND_TEXTURE | GX2R_RESOURCE_USAGE_CPU_WRITE |
		GX2R_RESOURCE_USAGE_CPU_READ | GX2R_RESOURCE_USAGE_GPU_WRITE |
		GX2R_RESOURCE_USAGE_GPU_READ | GX2R_RESOURCE_USAGE_FORCE_MEM2;

	memset(&s_font, 0, sizeof(s_font));
	s_font.surface.width = FONT_TEX_W;
	s_font.surface.height = FONT_TEX_H;
	s_font.surface.depth = 1;
	s_font.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
	s_font.surface.format = GX2_SURFACE_FORMAT_UNORM_R8;
	s_font.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	s_font.surface.mipLevels = 1;
	s_font.viewNumMips = 1;
	s_font.viewNumSlices = 1;
	s_font.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_0, GX2_SQ_SEL_0,
				      GX2_SQ_SEL_1);
	GX2CalcSurfaceSizeAndAlignment(&s_font.surface);
	GX2InitTextureRegs(&s_font);
	if(!GX2RCreateSurface(&s_font.surface, flags))
		return false;

	// GX2Surface.pitch esta en TEXELES (para R8 = bytes), mismo patron
	// de escritura que video.cpp: lock -> escribir -> DCFlush -> unlock.
	void *img = GX2RLockSurfaceEx(&s_font.surface, 0,
				      GX2R_RESOURCE_BIND_NONE);
	if(!img)
		return false;
	uint8_t *p = (uint8_t *)img;
	size_t pitch = s_font.surface.pitch;
	memset(p, 0, pitch * FONT_TEX_H);
	memset(p + (size_t)FONT_WHITE_ROW * pitch, 0xFF,
	       (size_t)FONT_WHITE_H * FONT_TEX_W);

	// Glifo Unicode cp -> celda (cp%16, cp/16). LSB del byte = píxel
	// izquierdo (README de font8x8). U+0080-U+009F se dejan en blanco.
	for(uint32_t cp = 0; cp < 256; cp++)
	{
		const uint8_t *g = NULL;
		if(cp < 128)
			g = (const uint8_t *)font8x8_basic[cp];
		else if(cp >= 160)
			g = (const uint8_t *)font8x8_ext_latin[cp - 160];
		if(!g)
			continue;
		size_t gx = (cp & 15) * 8, gy = (cp >> 4) * 8;
		for(int r = 0; r < 8; r++)
		{
			uint8_t bits = g[r];
			uint8_t *dst = p + (gy + (size_t)r) * pitch + gx;
			for(int c = 0; c < 8; c++)
				dst[c] = (bits >> c) & 1 ? 0xFF : 0x00;
		}
	}

	// Assets de chiaki-ng en la mitad derecha (wordmark; PS4 cuerpo y
	// tira de luz por separado para teñirlos segun el estado).
	for(int r = 0; r < ASSET_LOGO_H; r++)
		memcpy(p + (size_t)(ASSET_LOGO_Y + r) * pitch + ASSET_LOGO_X,
		       gui_logo_mask + (size_t)r * GUI_ASSET_W, GUI_ASSET_W);
	for(int r = 0; r < ASSET_BODY_H; r++)
		memcpy(p + (size_t)(ASSET_BODY_Y + r) * pitch + ASSET_LOGO_X,
		       gui_ps4_body + (size_t)r * GUI_ASSET_W, GUI_ASSET_W);
	for(int r = 0; r < ASSET_LIGHT_H; r++)
		memcpy(p + (size_t)(ASSET_LIGHT_Y + r) * pitch + ASSET_LOGO_X,
		       gui_ps4_light + (size_t)r * GUI_ASSET_W, GUI_ASSET_W);
	for(int r = 0; r < ASSET_RADAR_S; r++)
	{
		memcpy(p + (size_t)(ASSET_RADAR_Y + r) * pitch + ASSET_RADAR_ON_X,
		       gui_radar_on + (size_t)r * ASSET_RADAR_S, ASSET_RADAR_S);
		memcpy(p + (size_t)(ASSET_RADAR_Y + r) * pitch + ASSET_RADAR_OFF_X,
		       gui_radar_off + (size_t)r * ASSET_RADAR_S, ASSET_RADAR_S);
	}
	for(int r = 0; r < ASSET_DISC_S; r++)
		memcpy(p + (size_t)(ASSET_DISC_Y + r) * pitch + ASSET_DISC_X,
		       gui_disc + (size_t)r * ASSET_DISC_S, ASSET_DISC_S);
	for(int r = 0; r < ASSET_OFF_S; r++)
		memcpy(p + (size_t)(ASSET_OFF_Y + r) * pitch + ASSET_OFF_X,
		       gui_off + (size_t)r * ASSET_OFF_S, ASSET_OFF_S);
	for(int r = 0; r < ASSET_TP_H; r++)
		memcpy(p + (size_t)(ASSET_TP_Y + r) * pitch + ASSET_TP_X,
		       gui_touchpad + (size_t)r * ASSET_TP_W, ASSET_TP_W);

	DCFlushRange(img, s_font.surface.imageSize);
	GX2RUnlockSurfaceEx(&s_font.surface, 0, GX2R_RESOURCE_BIND_NONE);
	return true;
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

bool gui_init(void)
{
	GX2AttribStream attrs[3];
	uint32_t fetch_size;

	if(s_ready)
		return true;

	s_vs = load_gfd_vs(video_gsh);
	s_ps = load_gfd_ps(gui_gsh);
	if(!s_vs || !s_ps)
		return false;

	// Mismos atributos que VideoVert (mismo layout byte a byte).
	attrs[0] = { 0, 0, (uint32_t)offsetof(GuiVert, x),
		     GX2_ATTRIB_FORMAT_FLOAT_32_32, GX2_ATTRIB_INDEX_PER_VERTEX, 0,
		     GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0,
				  GX2_SQ_SEL_1),
		     GX2_ENDIAN_SWAP_DEFAULT };
	attrs[1] = { 1, 0, (uint32_t)offsetof(GuiVert, color),
		     GX2_ATTRIB_FORMAT_UNORM_8_8_8_8, GX2_ATTRIB_INDEX_PER_VERTEX, 0,
		     GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z,
				  GX2_SQ_SEL_W),
		     GX2_ENDIAN_SWAP_DEFAULT };
	attrs[2] = { 2, 0, (uint32_t)offsetof(GuiVert, u),
		     GX2_ATTRIB_FORMAT_FLOAT_32_32, GX2_ATTRIB_INDEX_PER_VERTEX, 0,
		     GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0,
				  GX2_SQ_SEL_1),
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

	// POINT: fuente pixel-perfect (sin suavizado); CLAMP por seguridad.
	GX2InitSampler(&s_samp, GX2_TEX_CLAMP_MODE_CLAMP,
		       GX2_TEX_XY_FILTER_MODE_POINT);

	if(!font_create())
		return false;

	// Triada de buffers de vertices (cada frame escribe uno distinto).
	for(int i = 0; i < 3; i++)
	{
		memset(&s_vbuf[i], 0, sizeof(s_vbuf[i]));
		s_vbuf[i].flags = GX2R_RESOURCE_BIND_VERTEX_BUFFER |
				  GX2R_RESOURCE_USAGE_CPU_WRITE;
		s_vbuf[i].elemSize = sizeof(GuiVert);
		s_vbuf[i].elemCount = MAX_VERTS;
		if(!GX2RCreateBuffer(&s_vbuf[i]))
			return false;
	}

	s_ready = true;

	// Estado2D fijo del pipeline 2D probado en frio (lo mismo que SDL2-
	// wiiu fija en WIIU_SDL_CreateRenderer sobre su contexto): alpha
	// test > 0, depth off y cull off, en AMBOS contextos de WHBGfx.
	// Sin estos registros explicitos, en frio los quads del menu no
	// producian pixeles (solo se veia el clear azul) hasta que el
	// teclado del sistema reescribia estos registros con sus valores.
	for(int i = 0; i < 2; i++)
	{
		GX2ContextState *cs = i == 0 ? WHBGfxGetTVContextState() :
						   WHBGfxGetDRCContextState();
		if(!cs)
			continue;
		GX2SetContextState(cs);
		GX2SetAlphaTest(TRUE, GX2_COMPARE_FUNC_GREATER, 0.0f);
		GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_NEVER);
		GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
	}
	// Deja el contexto TV activo (wut arranca con el DRC; el loop
	// reata cada pass con WHBGfxBeginRenderTV/DRC igualmente).
	GX2SetContextState(WHBGfxGetTVContextState());

	return true;
}

// ---------------------------------------------------------------------------
// Batch
// ---------------------------------------------------------------------------

// Proyeccion en coordenadas de píxel (y hacia abajo) a NDC: los shaders
// calculan out = x*C0 + y*C1 + C3 (el VS de video es2D: sin termino z).
static void set_proj(int w, int h)
{
	s_proj[0] = 2.f / (float)w;
	s_proj[1] = 0.f;
	s_proj[2] = 0.f;
	s_proj[3] = 0.f;
	s_proj[4] = 0.f;
	s_proj[5] = -2.f / (float)h;
	s_proj[6] = 0.f;
	s_proj[7] = 0.f;
	s_proj[8] = 0.f;
	s_proj[9] = 0.f;
	s_proj[10] = 1.f;
	s_proj[11] = 0.f;
	s_proj[12] = -1.f;
	s_proj[13] = 1.f;
	s_proj[14] = 0.f;
	s_proj[15] = 1.f;
}

// Centro del texel blanco para solidos (fila132, columna0).
#define WHITE_U (0.5f / (float)FONT_TEX_W)
#define WHITE_V ((FONT_WHITE_ROW + 4.f) / (float)FONT_TEX_H)

static void push_quad(float x, float y, float w, float h, uint32_t color,
		      float u0, float v0, float u1, float v1)
{
	if(!s_in_begin || s_nv + 4 > MAX_VERTS)
		return;
	// Orden de PERIMETRO (TL,TR,BR,BL): GX2 triangula el quad en
	// (v0,v1,v2)+(v0,v2,v3) y el orden zigzag deja un triángulo
	// sin cubrir (mismo hallazgo que en el blit de video).
	GuiVert *v = &s_verts[s_nv];
	v[0] = { x, y, color, u0, v0 };
	v[1] = { x + w, y, color, u1, v0 };
	v[2] = { x + w, y + h, color, u1, v1 };
	v[3] = { x, y + h, color, u0, v1 };
	s_nv += 4;
}

void gui_begin(int viewport_w, int viewport_h)
{
	if(!s_ready || viewport_w <= 0 || viewport_h <= 0)
	{
		s_in_begin = false;
		return;
	}
	s_vw = viewport_w;
	s_vh = viewport_h;
	set_proj(viewport_w, viewport_h);
	s_nv = 0;
	s_in_begin = true;
}

void gui_rect(float x, float y, float w, float h, uint32_t color)
{
	// Mismo texel blanco en las cuatro esquinas (POINT => constante).
	push_quad(x, y, w, h, color, WHITE_U, WHITE_V, WHITE_U, WHITE_V);
}

static const uint8_t *next_cp(const uint8_t *p, uint32_t *cp)
{
	if(p[0] < 0x80)
	{
		*cp = p[0];
		return p + 1;
	}
	if((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80)
	{
		*cp = ((uint32_t)(p[0] & 0x1F) << 6) | (uint32_t)(p[1] & 0x3F);
		return p + 2;
	}
	if((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 &&
	   (p[2] & 0xC0) == 0x80)
	{
		*cp = ((uint32_t)(p[0] & 0x0F) << 12) |
		      ((uint32_t)(p[1] & 0x3F) << 6) | (uint32_t)(p[2] & 0x3F);
		return p + 3;
	}
	*cp = '?';
	return p + 1;
}

void gui_text(float x, float y, const char *utf8, uint32_t color, int scale)
{
	if(!s_in_begin)
		return;
	if(scale < 1)
		scale = 1;
	const float dx = 8.f * (float)scale;
	const float du = 8.f / (float)FONT_TEX_W;       //8 px /256 texels
	const float dv = 8.f / (float)FONT_TEX_H;       //8 px /136 texels
	float cx = x;
	const uint8_t *p = (const uint8_t *)utf8;
	while(*p)
	{
		uint32_t cp;
		p = next_cp(p, &cp);
		if(cp == '\n')
		{
			cx = x;
			y += dx;
			continue;
		}
		// Fuente: U+0000-U+007F + U+00A0-U+00FF. El resto: '?'.
		if(cp > 0xFF || (cp >= 0x80 && cp < 0xA0))
			cp = '?';
		float u0 = (float)(cp & 15) * du;
		float v0 = (float)(cp >> 4) * dv;
		push_quad(cx, y, dx, dx, color, u0, v0, u0 + du, v0 + dv);
		cx += dx;
	}
}

float gui_text_width(const char *utf8, int scale)
{
	if(scale < 1)
		scale = 1;
	int n = 0;
	const uint8_t *p = (const uint8_t *)utf8;
	while(*p)
	{
		uint32_t cp;
		p = next_cp(p, &cp);
		if(cp == '\n')
			break;
		n++;
	}
	return (float)n * 8.f * (float)scale;
}

void gui_image(float x, float y, float w, float h,
	       float au, float av, float aw, float ah, uint32_t color)
{
	push_quad(x, y, w, h, color, au, av, au + aw, av + ah);
}

void gui_asset_uvs(GuiAsset asset, float *u, float *v, float *w, float *h)
{
	const float aw = (float)FONT_TEX_W, ah = (float)FONT_TEX_H;
	switch(asset)
	{
	case GUI_ASSET_LOGO:
		*u = (float)ASSET_LOGO_X / aw;
		*v = (float)ASSET_LOGO_Y / ah;
		*w = (float)GUI_ASSET_W / aw;
		*h = (float)ASSET_LOGO_H / ah;
		break;
	case GUI_ASSET_PS4_BODY:
		*u = (float)ASSET_LOGO_X / aw;
		*v = (float)ASSET_BODY_Y / ah;
		*w = (float)GUI_ASSET_W / aw;
		*h = (float)ASSET_BODY_H / ah;
		break;
	case GUI_ASSET_PS4_LIGHT:
		*u = (float)ASSET_LOGO_X / aw;
		*v = (float)ASSET_LIGHT_Y / ah;
		*w = (float)GUI_ASSET_W / aw;
		*h = (float)ASSET_LIGHT_H / ah;
		break;
	case GUI_ASSET_RADAR_ON:
		*u = (float)ASSET_RADAR_ON_X / aw;
		*v = (float)ASSET_RADAR_Y / ah;
		*w = (float)ASSET_RADAR_S / aw;
		*h = (float)ASSET_RADAR_S / ah;
		break;
	case GUI_ASSET_RADAR_OFF:
		*u = (float)ASSET_RADAR_OFF_X / aw;
		*v = (float)ASSET_RADAR_Y / ah;
		*w = (float)ASSET_RADAR_S / aw;
		*h = (float)ASSET_RADAR_S / ah;
		break;
	case GUI_ASSET_DISC:
		*u = (float)ASSET_DISC_X / aw;
		*v = (float)ASSET_DISC_Y / ah;
		*w = (float)ASSET_DISC_S / aw;
		*h = (float)ASSET_DISC_S / ah;
		break;
	case GUI_ASSET_OFF:
		*u = (float)ASSET_OFF_X / aw;
		*v = (float)ASSET_OFF_Y / ah;
		*w = (float)ASSET_OFF_S / aw;
		*h = (float)ASSET_OFF_S / ah;
		break;
	case GUI_ASSET_TOUCHPAD:
		*u = (float)ASSET_TP_X / aw;
		*v = (float)ASSET_TP_Y / ah;
		*w = (float)ASSET_TP_W / aw;
		*h = (float)ASSET_TP_H / ah;
		break;
	}
}

void gui_flush(void)
{
	if(!s_ready || !s_in_begin)
	{
		s_in_begin = false;
		return;
	}
	s_in_begin = false;
	if(s_nv == 0)
		return;
	if(s_nv & 3)
		return; // por construccion son multiplos de4

	// Escribimos un buffer rotativo distinto cada frame: el anterior
	// puede seguir en lectura por el GPU (GX2 asincrono, sin DrawDone).
	int slot = s_slot;
	s_slot = (s_slot + 1) % 3;
	void *p = GX2RLockBufferEx(&s_vbuf[slot], GX2R_RESOURCE_BIND_NONE);
	if(!p)
		return;
	memcpy(p, s_verts, (size_t)s_nv * sizeof(GuiVert));
	DCFlushRange(p, (size_t)s_nv * sizeof(GuiVert));
	GX2RUnlockBufferEx(&s_vbuf[slot], GX2R_RESOURCE_BIND_NONE);

	// Estado de dibujo (mismo patron que wiiu_video_render + blending
	// alpha SRC_ALPHA/INV_SRC_ALPHA con mascara 0xFF, el valor que usa
	// SDL2-wiiu). Los registros alpha/depth/cull se re-aplican en cada
	// flush: el overlay del teclado del sistema puede escribir sobre
	// estos registros del contexto mientras dibuja.
	GX2SetViewport(0.f, 0.f, (float)s_vw, (float)s_vh, 0.f, 1.f);
	GX2SetScissor(0, 0, (uint32_t)s_vw, (uint32_t)s_vh);
	GX2SetAlphaTest(TRUE, GX2_COMPARE_FUNC_GREATER, 0.0f);
	GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_NEVER);
	GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
	GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xFF, FALSE, TRUE);
	GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA,
			   GX2_BLEND_MODE_INV_SRC_ALPHA,
			   GX2_BLEND_COMBINE_MODE_ADD, TRUE, GX2_BLEND_MODE_ONE,
			   GX2_BLEND_MODE_ONE, GX2_BLEND_COMBINE_MODE_ADD);

	GX2SetVertexUniformReg(s_vs->uniformVars[0].offset, 16, s_proj);
	GX2SetFetchShader(&s_fetch);
	GX2SetVertexShader(s_vs);
	GX2SetPixelShader(s_ps);
	GX2RSetAttributeBuffer(&s_vbuf[slot], 0, sizeof(GuiVert), 0);

	const uint32_t loc0 = s_ps->samplerVars[0].location;
	GX2SetPixelTexture(&s_font, loc0);
	GX2SetPixelSampler(&s_samp, loc0);

	GX2DrawEx(GX2_PRIMITIVE_MODE_QUADS, (uint32_t)s_nv, 0, 1);
}
