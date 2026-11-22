// Markdown to wrapped lines for the small screen, built once per content change and drawn
// line by line (only what is visible).
#pragma once

#include "platform/gfx.h"

#include <cstdint>
#include <string>
#include <vector>

namespace md {

enum StyleBits : uint8_t { Plain = 0, Bold = 1, Italic = 2, Code = 4, Link = 8, Strike = 16 };

struct Run {
	std::string text;
	float x = 0, w = 0;
	uint8_t style = Plain;
	int link = -1;
};

enum class LineKind : uint8_t { Text, Code, Heading, Rule };
enum class Bullet : uint8_t { None, Dot, Ring, Square, Number, Unchecked, Checked };

struct Line {
	LineKind kind = LineKind::Text;
	float y = 0, h = 0;
	float scale = 0.5f;
	std::vector<Run> runs;
	Bullet bullet = Bullet::None;
	std::string bulletText;
	float bulletX = 0;
	int quoteDepth = 0;
};

// Backgrounds spanning several lines (code blocks, quotes).
struct Panel {
	float y0 = 0, y1 = 0;
	bool code = true;
	float x = 0;
	std::string label;
};

struct Layout {
	std::vector<Line> lines;
	std::vector<Panel> panels;
	std::vector<std::string> links;
	float height = 0;
	float width = 0;
};

Layout build(const std::string &markdown, float width, float scale, bool streaming = false);
void draw(const Layout &layout, float x, float y, float viewTop, float viewBottom, gfx::Color color);

// Plain text with inline Markdown removed (for previews).
std::string stripInline(const std::string &markdown);

} // namespace md
