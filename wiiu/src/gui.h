#pragma once

#include <stdint.h>

// GUI de menu sobre GX2 (solo hilo principal). Primitivas inmediatas
// que se acumulan en un batch por frame:
//
//   gui_begin(w, h);
//   gui_rect(...); gui_text(...);   // ... composicion del menu
//   gui_flush();                    // sube el batch y dibuja
//
// Blending de alfa; fuente8x8 de dominio publico (dhepper/font8x8)
// embebida en una textura R8 con un bloque blanco para solidos.
// Triada de buffers de vertices: cada flush escribe un buffer
// distinto para no pisar lo que el GPU todavia lee del frame anterior.

// Color RGBA en memoria (atributo UNORM_8_8_8_8): byte0=R,1=G,2=B,3=A
// (little-endian => 0xAABBGGRR).
#define GUI_RGBA(r, g, b, a)                                                    \
	((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) |      \
	 ((uint32_t)(a) << 24))

// Carga shaders (VS reutilizado del GFD de video + PS gui_gsh), fuente
// y buffers. Llamar una vez tras inicializar el log; si falla el menu
// no se dibuja y las demas funciones se vuelven no-op.
bool gui_init(void);

void gui_begin(int viewport_w, int viewport_h);
void gui_rect(float x, float y, float w, float h, uint32_t color);
void gui_text(float x, float y, const char *utf8, uint32_t color, int scale);
float gui_text_width(const char *utf8, int scale);

// Imagen: mask R8 del atlas (rect de pantalla + rect UV normalizado).
// El tinte va por color de vertice; la mask decide la opacidad.
void gui_image(float x, float y, float w, float h,
	       float au, float av, float aw, float ah, uint32_t color);

// Assets embebidos de chiaki-ng en el atlas (gui_assets.h).
typedef enum GuiAsset {
	GUI_ASSET_LOGO,      // wordmark "chiaki-ng"
	GUI_ASSET_PS4_BODY,  // silueta de la PS4
	GUI_ASSET_PS4_LIGHT, // tira de luz (se tiñe segun estado)
	GUI_ASSET_RADAR_ON,  // discovery: consola detectada (discover-24px)
	GUI_ASSET_RADAR_OFF, // discovery: buscando (discover-off-24px)
	GUI_ASSET_DISC,      // disco de los chips de boton del gamepad
} GuiAsset;
void gui_asset_uvs(GuiAsset asset, float *u, float *v, float *w, float *h);

void gui_flush(void);
