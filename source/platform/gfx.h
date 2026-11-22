// Minimal 2D drawing API. citro2d on the console (gfx_3ds.cpp), a software rasteriser in the
// desktop simulator (host/gfx_host.cpp). Colours are ABGR like citro2d.
#pragma once

#include <cstdint>
#include <string>

namespace gfx {

using Color = uint32_t;

// 0xRRGGBB plus alpha into citro2d's ABGR.
constexpr Color rgb(uint32_t rrggbb, uint8_t a = 255) {
	return ((uint32_t)a << 24) | ((rrggbb & 0xFF) << 16) | (rrggbb & 0xFF00) | ((rrggbb >> 16) & 0xFF);
}
constexpr Color withAlpha(Color c, uint8_t a) { return (c & 0x00FFFFFF) | ((uint32_t)a << 24); }
Color mix(Color a, Color b, float t);

enum class Screen { Top, Bottom };
constexpr int TOP_W = 400;
constexpr int BOTTOM_W = 320;
constexpr int SCREEN_H = 240;

bool init();
void shutdown();
void beginFrame();
void beginScreen(Screen screen, Color clear);
void endFrame();
Screen current();
int width();

void rect(float x, float y, float w, float h, Color c);
void gradientV(float x, float y, float w, float h, Color top, Color bottom);
void roundRect(float x, float y, float w, float h, float r, Color c);
// Filled rounded rect with a 1px border.
void roundRectBorder(float x, float y, float w, float h, float r, Color fill, Color border);
void circle(float cx, float cy, float r, Color c);
// Arc of a ring from angle a0 to a1 (radians, 0 = right, clockwise on screen).
void arc(float cx, float cy, float r, float thickness, float a0, float a1, Color c);
void triangle(float x1, float y1, float x2, float y2, float x3, float y3, Color c);
void line(float x1, float y1, float x2, float y2, float thickness, Color c);

// Text in the console's system font. y is the top of the line.
float lineHeight(float scale);
float glyphAdvance(uint32_t codepoint, float scale);
float textWidth(const std::string &utf8, float scale);
void text(float x, float y, float scale, Color c, const std::string &utf8);
// Draws twice, offset, for emphasis (the system font has no bold face).
void textBold(float x, float y, float scale, Color c, const std::string &utf8);
// Cuts with an ellipsis to fit maxWidth; returns the width drawn.
float textFit(float x, float y, float scale, Color c, const std::string &utf8, float maxWidth, bool bold = false);
std::string fitText(const std::string &utf8, float scale, float maxWidth);
bool hasGlyph(uint32_t codepoint);

// Limits drawing to a rectangle on the current screen.
void clip(float x, float y, float w, float h);
void noClip();

// Textures, created on the main thread from RGBA8 pixels (at most 1024x1024).
struct Image;
Image *createImage(const uint8_t *rgba, int w, int h);
void destroyImage(Image *img);
int imageWidth(const Image *img);
int imageHeight(const Image *img);
void drawImage(const Image *img, float x, float y, float w, float h, float alpha = 1.0f);

} // namespace gfx
