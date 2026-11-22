// Drawing helpers shared by the console and simulator back ends.
#include "platform/gfx.h"

#include "util/util.h"

#include <cmath>

namespace gfx {

Color mix(Color a, Color b, float t) {
	if (t <= 0) {
		return a;
	}
	if (t >= 1) {
		return b;
	}
	auto ch = [&](int shift) {
		float va = (float)((a >> shift) & 0xFF), vb = (float)((b >> shift) & 0xFF);
		return ((uint32_t)(va + (vb - va) * t + 0.5f) & 0xFF) << shift;
	};
	return ch(0) | ch(8) | ch(16) | ch(24);
}

float textWidth(const std::string &s, float scale) {
	float w = 0, best = 0;
	size_t i = 0;
	while (i < s.size()) {
		uint32_t cp = util::utf8Next(s, i);
		if (cp == '\n') {
			best = std::max(best, w);
			w = 0;
			continue;
		}
		w += glyphAdvance(cp, scale);
	}
	return std::max(best, w);
}

std::string fitText(const std::string &s, float scale, float maxWidth) {
	if (textWidth(s, scale) <= maxWidth) {
		return s;
	}
	static const std::string ell = "\xE2\x80\xA6";
	float ellW = textWidth(ell, scale);
	float w = 0;
	size_t i = 0, lastFit = 0;
	while (i < s.size()) {
		size_t start = i;
		uint32_t cp = util::utf8Next(s, i);
		float adv = glyphAdvance(cp, scale);
		if (w + adv + ellW > maxWidth) {
			lastFit = start;
			break;
		}
		w += adv;
		lastFit = i;
	}
	return s.substr(0, lastFit) + ell;
}

float textFit(float x, float y, float scale, Color c, const std::string &s, float maxWidth, bool bold) {
	std::string fitted = fitText(util::oneLine(s), scale, maxWidth);
	if (bold) {
		textBold(x, y, scale, c, fitted);
	} else {
		text(x, y, scale, c, fitted);
	}
	return textWidth(fitted, scale);
}

void textBold(float x, float y, float scale, Color c, const std::string &s) {
	text(x, y, scale, c, s);
	text(x + 0.6f, y, scale, c, s);
}

void roundRectBorder(float x, float y, float w, float h, float r, Color fill, Color border) {
	roundRect(x, y, w, h, r, border);
	roundRect(x + 1, y + 1, w - 2, h - 2, std::max(0.0f, r - 1), fill);
}

void line(float x1, float y1, float x2, float y2, float thickness, Color c) {
	float dx = x2 - x1, dy = y2 - y1;
	float len = std::sqrt(dx * dx + dy * dy);
	if (len < 0.001f) {
		return;
	}
	float nx = -dy / len * thickness * 0.5f, ny = dx / len * thickness * 0.5f;
	triangle(x1 + nx, y1 + ny, x2 + nx, y2 + ny, x1 - nx, y1 - ny, c);
	triangle(x2 + nx, y2 + ny, x2 - nx, y2 - ny, x1 - nx, y1 - ny, c);
}

void arc(float cx, float cy, float r, float thickness, float a0, float a1, Color c) {
	int segments = std::max(4, (int)(std::fabs(a1 - a0) * r / 3.0f));
	float rin = r - thickness * 0.5f, rout = r + thickness * 0.5f;
	for (int i = 0; i < segments; i++) {
		float t0 = a0 + (a1 - a0) * i / segments;
		float t1 = a0 + (a1 - a0) * (i + 1) / segments;
		float c0 = std::cos(t0), s0 = std::sin(t0), c1 = std::cos(t1), s1 = std::sin(t1);
		triangle(cx + c0 * rin, cy + s0 * rin, cx + c0 * rout, cy + s0 * rout, cx + c1 * rout, cy + s1 * rout, c);
		triangle(cx + c0 * rin, cy + s0 * rin, cx + c1 * rout, cy + s1 * rout, cx + c1 * rin, cy + s1 * rin, c);
	}
}

void roundRect(float x, float y, float w, float h, float r, Color c) {
	if (w <= 0 || h <= 0) {
		return;
	}
	r = std::min(r, std::min(w, h) * 0.5f);
	if (r < 0.5f) {
		rect(x, y, w, h, c);
		return;
	}
	rect(x + r, y, w - 2 * r, h, c);
	rect(x, y + r, r, h - 2 * r, c);
	rect(x + w - r, y + r, r, h - 2 * r, c);
	static float cs[9], sn[9];
	static bool init = false;
	if (!init) {
		for (int i = 0; i <= 8; i++) {
			float a = (float)M_PI * 0.5f * i / 8;
			cs[i] = std::cos(a);
			sn[i] = std::sin(a);
		}
		init = true;
	}
	struct Corner {
		float cx, cy, sx, sy;
	} corners[4] = {{x + r, y + r, -1, -1}, {x + w - r, y + r, 1, -1}, {x + w - r, y + h - r, 1, 1}, {x + r, y + h - r, -1, 1}};
	for (const Corner &k : corners) {
		for (int i = 0; i < 8; i++) {
			triangle(k.cx, k.cy, k.cx + k.sx * cs[i] * r, k.cy + k.sy * sn[i] * r, k.cx + k.sx * cs[i + 1] * r,
			         k.cy + k.sy * sn[i + 1] * r, c);
		}
	}
}

} // namespace gfx
