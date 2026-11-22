#ifdef __3DS__

#include "platform/gfx.h"

#include "core/log.h"
#include "util/util.h"

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include <cstring>
#include <unordered_map>

namespace gfx {

struct Image {
	C3D_Tex tex;
	Tex3DS_SubTexture sub;
	int w, h;
};

namespace {
C3D_RenderTarget *g_top = nullptr;
C3D_RenderTarget *g_bottom = nullptr;
C2D_TextBuf g_textBuf = nullptr;
Screen g_screen = Screen::Top;
float g_lineFeed = 30.0f;
int g_altGlyph = -1;
std::unordered_map<uint32_t, float> g_advance;
constexpr float Z = 0.5f;

float advanceRaw(uint32_t cp) {
	auto it = g_advance.find(cp);
	if (it != g_advance.end()) {
		return it->second;
	}
	int gi = C2D_FontGlyphIndexFromCodePoint(nullptr, cp);
	charWidthInfo_s *info = C2D_FontGetCharWidthInfo(nullptr, gi);
	float w = info ? (float)info->charWidth : 0.0f;
	if (g_advance.size() < 4096) {
		g_advance[cp] = w;
	}
	return w;
}

int pow2(int v) {
	int p = 8;
	while (p < v) {
		p <<= 1;
	}
	return p;
}
} // namespace

bool init() {
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 2);
	C2D_Init(8192);
	C2D_Prepare();
	g_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	g_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	g_textBuf = C2D_TextBufNew(12288);
	FINF_s *info = C2D_FontGetInfo(nullptr);
	if (info) {
		g_lineFeed = info->lineFeed;
		g_altGlyph = info->alterCharIndex;
	}
	return g_top && g_bottom && g_textBuf;
}

void shutdown() {
	if (g_textBuf) {
		C2D_TextBufDelete(g_textBuf);
		g_textBuf = nullptr;
	}
	C2D_Fini();
	C3D_Fini();
}

void beginFrame() {
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
	C2D_TextBufClear(g_textBuf);
}

void beginScreen(Screen screen, Color clear) {
	noClip();
	g_screen = screen;
	C3D_RenderTarget *t = screen == Screen::Top ? g_top : g_bottom;
	C2D_TargetClear(t, clear);
	C2D_SceneBegin(t);
}

void endFrame() {
	noClip();
	C3D_FrameEnd(0);
}

Screen current() { return g_screen; }
int width() { return g_screen == Screen::Top ? TOP_W : BOTTOM_W; }

void rect(float x, float y, float w, float h, Color c) {
	if (w > 0 && h > 0) {
		C2D_DrawRectSolid(x, y, Z, w, h, c);
	}
}

void gradientV(float x, float y, float w, float h, Color top, Color bottom) {
	C2D_DrawRectangle(x, y, Z, w, h, top, top, bottom, bottom);
}

void circle(float cx, float cy, float r, Color c) { C2D_DrawCircleSolid(cx, cy, Z, r, c); }

void triangle(float x1, float y1, float x2, float y2, float x3, float y3, Color c) {
	C2D_DrawTriangle(x1, y1, c, x2, y2, c, x3, y3, c, Z);
}

float lineHeight(float scale) { return g_lineFeed * scale; }

float glyphAdvance(uint32_t cp, float scale) { return advanceRaw(cp) * scale; }

bool hasGlyph(uint32_t cp) { return C2D_FontGlyphIndexFromCodePoint(nullptr, cp) != g_altGlyph; }

void text(float x, float y, float scale, Color c, const std::string &s) {
	if (s.empty()) {
		return;
	}
	C2D_Text t;
	const char *end = C2D_TextParse(&t, g_textBuf, s.c_str());
	if (!end) {
		return; // buffer full this frame
	}
	C2D_TextOptimize(&t);
	C2D_DrawText(&t, C2D_WithColor, x, y, Z, scale, scale, c);
}

void clip(float x, float y, float w, float h) {
	C2D_Flush();
	int sw = g_screen == Screen::Top ? TOP_W : BOTTOM_W;
	int left = (int)(SCREEN_H - (y + h)), top = (int)(sw - (x + w));
	int right = (int)(SCREEN_H - y), bottom = (int)(sw - x);
	if (left < 0) {
		left = 0;
	}
	if (top < 0) {
		top = 0;
	}
	if (right > SCREEN_H) {
		right = SCREEN_H;
	}
	if (bottom > sw) {
		bottom = sw;
	}
	C3D_SetScissor(GPU_SCISSOR_NORMAL, left, top, right, bottom);
}

void noClip() {
	C2D_Flush();
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
}

Image *createImage(const uint8_t *rgba, int w, int h) {
	if (w <= 0 || h <= 0 || w > 1024 || h > 1024) {
		return nullptr;
	}
	Image *img = new Image();
	int tw = pow2(w), th = pow2(h);
	if (!C3D_TexInit(&img->tex, (u16)tw, (u16)th, GPU_RGBA8)) {
		Log::write("[gfx] out of texture memory for %dx%d", w, h);
		delete img;
		return nullptr;
	}
	uint32_t *dst = (uint32_t *)img->tex.data;
	memset(dst, 0, (size_t)tw * th * 4);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const uint8_t *p = rgba + ((size_t)y * w + x) * 4;
			// 8x8 tiles in Morton order.
			uint32_t m = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
			uint32_t idx = (((y >> 3) * (tw >> 3) + (x >> 3)) << 6) + m;
			dst[idx] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
		}
	}
	C3D_TexFlush(&img->tex);
	C3D_TexSetFilter(&img->tex, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(&img->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	img->w = w;
	img->h = h;
	img->sub.width = (u16)w;
	img->sub.height = (u16)h;
	img->sub.left = 0.0f;
	img->sub.top = 1.0f;
	img->sub.right = (float)w / tw;
	img->sub.bottom = 1.0f - (float)h / th;
	return img;
}

void destroyImage(Image *img) {
	if (img) {
		C3D_TexDelete(&img->tex);
		delete img;
	}
}

int imageWidth(const Image *img) { return img ? img->w : 0; }
int imageHeight(const Image *img) { return img ? img->h : 0; }

void drawImage(const Image *img, float x, float y, float w, float h, float alpha) {
	if (!img) {
		return;
	}
	C2D_Image ci = {const_cast<C3D_Tex *>(&img->tex), &img->sub};
	C2D_ImageTint tint;
	C2D_AlphaImageTint(&tint, alpha);
	C2D_DrawImageAt(ci, x, y, Z, alpha < 1.0f ? &tint : nullptr, w / img->w, h / img->h);
}

} // namespace gfx

#endif
