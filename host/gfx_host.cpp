// Software implementation of gfx for the desktop simulator: renders both screens into memory
// and saves them as one PNG laid out like the console (top screen above the bottom one).
#include "platform/gfx.h"

#include "util/util.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

namespace gfx {

struct Image {
	int w, h;
	std::vector<uint8_t> rgba;
};

namespace {
struct Surface {
	int w, h;
	std::vector<float> px; // rgb 0..1
};
Surface g_top{TOP_W, SCREEN_H, {}}, g_bottom{BOTTOM_W, SCREEN_H, {}};
Surface *g_cur = &g_top;
Screen g_screen = Screen::Top;
float g_clip[4] = {0, 0, TOP_W, SCREEN_H};

stbtt_fontinfo g_font;
std::vector<unsigned char> g_fontData;
float g_fontScaleUnit = 0; // stbtt scale for pixel height 1
int g_ascent = 0;
constexpr float LINE_FEED = 30.0f;
constexpr float EM_PX = 24.0f;

struct GlyphKey {
	uint32_t cp;
	int size10;
	bool operator<(const GlyphKey &o) const { return cp != o.cp ? cp < o.cp : size10 < o.size10; }
};
struct Glyph {
	int w = 0, h = 0, xoff = 0, yoff = 0;
	std::vector<unsigned char> bmp;
};
std::map<GlyphKey, Glyph> g_glyphs;

void blend(int x, int y, Color c, float cov) {
	if (cov <= 0 || x < g_clip[0] || y < g_clip[1] || x >= g_clip[2] || y >= g_clip[3] || x < 0 || y < 0 ||
	    x >= g_cur->w || y >= g_cur->h) {
		return;
	}
	float a = ((c >> 24) & 0xFF) / 255.0f * std::min(1.0f, cov);
	float r = (c & 0xFF) / 255.0f, g = ((c >> 8) & 0xFF) / 255.0f, b = ((c >> 16) & 0xFF) / 255.0f;
	float *p = &g_cur->px[((size_t)y * g_cur->w + x) * 3];
	p[0] = p[0] * (1 - a) + r * a;
	p[1] = p[1] * (1 - a) + g * a;
	p[2] = p[2] * (1 - a) + b * a;
}

const Glyph &glyph(uint32_t cp, float scale) {
	GlyphKey key{cp, (int)(scale * 100)};
	auto it = g_glyphs.find(key);
	if (it != g_glyphs.end()) {
		return it->second;
	}
	Glyph gl;
	float s = g_fontScaleUnit * EM_PX * scale;
	unsigned char *bmp = stbtt_GetCodepointBitmap(&g_font, s, s, (int)cp, &gl.w, &gl.h, &gl.xoff, &gl.yoff);
	if (bmp) {
		gl.bmp.assign(bmp, bmp + gl.w * gl.h);
		stbtt_FreeBitmap(bmp, nullptr);
	}
	return g_glyphs[key] = gl;
}
} // namespace

bool init() {
	const char *paths[] = {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
	                       "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"};
	for (const char *p : paths) {
		FILE *f = fopen(p, "rb");
		if (!f) {
			continue;
		}
		fseek(f, 0, SEEK_END);
		long n = ftell(f);
		fseek(f, 0, SEEK_SET);
		g_fontData.resize(n);
		fread(g_fontData.data(), 1, n, f);
		fclose(f);
		break;
	}
	if (g_fontData.empty() || !stbtt_InitFont(&g_font, g_fontData.data(), 0)) {
		fprintf(stderr, "no font\n");
		return false;
	}
	g_fontScaleUnit = stbtt_ScaleForPixelHeight(&g_font, 1.0f);
	int asc, desc, gap;
	stbtt_GetFontVMetrics(&g_font, &asc, &desc, &gap);
	g_ascent = asc;
	g_top.px.assign(TOP_W * SCREEN_H * 3, 0);
	g_bottom.px.assign(BOTTOM_W * SCREEN_H * 3, 0);
	return true;
}

void shutdown() {}
void beginFrame() {}
void endFrame() {}

void beginScreen(Screen screen, Color clear) {
	g_screen = screen;
	g_cur = screen == Screen::Top ? &g_top : &g_bottom;
	noClip();
	float r = (clear & 0xFF) / 255.0f, g = ((clear >> 8) & 0xFF) / 255.0f, b = ((clear >> 16) & 0xFF) / 255.0f;
	for (size_t i = 0; i < g_cur->px.size(); i += 3) {
		g_cur->px[i] = r;
		g_cur->px[i + 1] = g;
		g_cur->px[i + 2] = b;
	}
}

Screen current() { return g_screen; }
int width() { return g_screen == Screen::Top ? TOP_W : BOTTOM_W; }

void clip(float x, float y, float w, float h) {
	g_clip[0] = std::max(0.0f, x);
	g_clip[1] = std::max(0.0f, y);
	g_clip[2] = std::min((float)g_cur->w, x + w);
	g_clip[3] = std::min((float)g_cur->h, y + h);
}

void noClip() {
	g_clip[0] = 0;
	g_clip[1] = 0;
	g_clip[2] = (float)g_cur->w;
	g_clip[3] = (float)g_cur->h;
}

void rect(float x, float y, float w, float h, Color c) {
	if (w <= 0 || h <= 0) {
		return;
	}
	int x0 = (int)std::floor(x), y0 = (int)std::floor(y), x1 = (int)std::ceil(x + w), y1 = (int)std::ceil(y + h);
	for (int py = y0; py < y1; py++) {
		float cy = std::min((float)py + 1, y + h) - std::max((float)py, y);
		for (int px = x0; px < x1; px++) {
			float cx = std::min((float)px + 1, x + w) - std::max((float)px, x);
			blend(px, py, c, cx * cy);
		}
	}
}

void gradientV(float x, float y, float w, float h, Color top, Color bottom) {
	for (int i = 0; i < (int)std::ceil(h); i++) {
		rect(x, y + i, w, std::min(1.0f, h - i), mix(top, bottom, h > 1 ? i / (h - 1) : 0));
	}
}

void circle(float cx, float cy, float r, Color c) {
	int x0 = (int)std::floor(cx - r - 1), x1 = (int)std::ceil(cx + r + 1);
	int y0 = (int)std::floor(cy - r - 1), y1 = (int)std::ceil(cy + r + 1);
	for (int py = y0; py < y1; py++) {
		for (int px = x0; px < x1; px++) {
			float d = std::sqrt((px + 0.5f - cx) * (px + 0.5f - cx) + (py + 0.5f - cy) * (py + 0.5f - cy));
			blend(px, py, c, std::max(0.0f, std::min(1.0f, r - d + 0.5f)));
		}
	}
}

void triangle(float x1, float y1, float x2, float y2, float x3, float y3, Color c) {
	float minx = std::min({x1, x2, x3}), maxx = std::max({x1, x2, x3});
	float miny = std::min({y1, y2, y3}), maxy = std::max({y1, y2, y3});
	auto edge = [](float ax, float ay, float bx, float by, float px, float py) {
		return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
	};
	float area = edge(x1, y1, x2, y2, x3, y3);
	if (std::fabs(area) < 1e-4f) {
		return;
	}
	// One sample at the pixel centre, like the 3DS GPU (no anti-aliasing).
	static const float sx[1] = {0.5f}, sy[1] = {0.5f};
	for (int py = (int)std::floor(miny); py <= (int)std::ceil(maxy); py++) {
		for (int px = (int)std::floor(minx); px <= (int)std::ceil(maxx); px++) {
			int inside = 0;
			for (int k = 0; k < 1; k++) {
				float qx = px + sx[k], qy = py + sy[k];
				float w0 = edge(x2, y2, x3, y3, qx, qy), w1 = edge(x3, y3, x1, y1, qx, qy),
				      w2 = edge(x1, y1, x2, y2, qx, qy);
				if (area > 0 ? (w0 >= 0 && w1 >= 0 && w2 >= 0) : (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
					inside++;
				}
			}
			if (inside) {
				blend(px, py, c, (float)inside);
			}
		}
	}
}

float lineHeight(float scale) { return LINE_FEED * scale; }

float glyphAdvance(uint32_t cp, float scale) {
	int adv, lsb;
	stbtt_GetCodepointHMetrics(&g_font, (int)cp, &adv, &lsb);
	return adv * g_fontScaleUnit * EM_PX * scale;
}

bool hasGlyph(uint32_t cp) { return stbtt_FindGlyphIndex(&g_font, (int)cp) != 0; }

void text(float x, float y, float scale, Color c, const std::string &s) {
	float penX = x;
	float baseline = y + (g_ascent * g_fontScaleUnit * EM_PX * scale) + LINE_FEED * scale * 0.08f;
	size_t i = 0;
	while (i < s.size()) {
		uint32_t cp = util::utf8Next(s, i);
		if (cp == '\n') {
			penX = x;
			baseline += LINE_FEED * scale;
			continue;
		}
		const Glyph &gl = glyph(cp, scale);
		int gx = (int)std::round(penX) + gl.xoff, gy = (int)std::round(baseline) + gl.yoff;
		for (int yy = 0; yy < gl.h; yy++) {
			for (int xx = 0; xx < gl.w; xx++) {
				blend(gx + xx, gy + yy, c, gl.bmp[yy * gl.w + xx] / 255.0f);
			}
		}
		penX += glyphAdvance(cp, scale);
	}
}

Image *createImage(const uint8_t *rgba, int w, int h) {
	Image *img = new Image{w, h, std::vector<uint8_t>(rgba, rgba + (size_t)w * h * 4)};
	return img;
}

void destroyImage(Image *img) { delete img; }
int imageWidth(const Image *img) { return img ? img->w : 0; }
int imageHeight(const Image *img) { return img ? img->h : 0; }

void drawImage(const Image *img, float x, float y, float w, float h, float alpha) {
	if (!img || w <= 0 || h <= 0) {
		return;
	}
	for (int py = (int)y; py < (int)(y + h); py++) {
		for (int px = (int)x; px < (int)(x + w); px++) {
			int sx = std::max(0, std::min(img->w - 1, (int)((px - x) * img->w / w)));
			int sy = std::max(0, std::min(img->h - 1, (int)((py - y) * img->h / h)));
			const uint8_t *p = &img->rgba[((size_t)sy * img->w + sx) * 4];
			Color c = ((uint32_t)(p[3] * alpha) << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
			blend(px, py, c, 1.0f);
		}
	}
}

// Simulator only: writes both screens as one PNG (scaled by `zoom`).
bool savePng(const std::string &path, int zoom) {
	const int W = TOP_W, H = SCREEN_H * 2 + 4;
	std::vector<uint8_t> out((size_t)W * zoom * H * zoom * 3, 12);
	auto put = [&](int x, int y, const float *rgb3) {
		for (int dy = 0; dy < zoom; dy++) {
			for (int dx = 0; dx < zoom; dx++) {
				uint8_t *o = &out[(((size_t)(y * zoom + dy)) * W * zoom + (x * zoom + dx)) * 3];
				for (int k = 0; k < 3; k++) {
					o[k] = (uint8_t)std::max(0.0f, std::min(255.0f, rgb3[k] * 255.0f + 0.5f));
				}
			}
		}
	};
	for (int y = 0; y < SCREEN_H; y++) {
		for (int x = 0; x < TOP_W; x++) {
			put(x, y, &g_top.px[((size_t)y * TOP_W + x) * 3]);
		}
	}
	int ox = (TOP_W - BOTTOM_W) / 2;
	for (int y = 0; y < SCREEN_H; y++) {
		for (int x = 0; x < BOTTOM_W; x++) {
			put(ox + x, SCREEN_H + 4 + y, &g_bottom.px[((size_t)y * BOTTOM_W + x) * 3]);
		}
	}
	return stbi_write_png(path.c_str(), W * zoom, H * zoom, 3, out.data(), W * zoom * 3) != 0;
}

} // namespace gfx
