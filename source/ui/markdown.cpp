#include "ui/markdown.h"

#include "ui/theme.h"
#include "util/util.h"

#include <cctype>
#include <cmath>

namespace md {

using gfx::Color;

namespace {

struct Span {
	std::string text;
	uint8_t style;
	int link;
};

bool isCjk(uint32_t cp) {
	return (cp >= 0x3000 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
	       (cp >= 0xFF00 && cp <= 0xFFEF);
}

// Characters the system font lacks are swapped for close ones it has.
std::string normalize(const std::string &s) {
	std::string out;
	out.reserve(s.size());
	size_t i = 0;
	while (i < s.size()) {
		size_t start = i;
		uint32_t cp = util::utf8Next(s, i);
		if (cp == '\t') {
			out += "    ";
		} else if (cp == '\r') {
			continue;
		} else if (cp < 0x80 || gfx::hasGlyph(cp)) {
			out.append(s, start, i - start);
		} else if (cp == 0x2014 || cp == 0x2013) {
			out += "-";
		} else if (cp == 0x2018 || cp == 0x2019) {
			out += "'";
		} else if (cp == 0x201C || cp == 0x201D) {
			out += "\"";
		} else if (cp == 0x2026) {
			out += "...";
		} else if (cp == 0x2192) {
			out += "->";
		} else if (cp == 0x2190) {
			out += "<-";
		} else if (cp == 0x2713 || cp == 0x2714 || cp == 0x2705) {
			out += "[x]";
		} else if (cp == 0x274C) {
			out += "[ ]";
		} else if (cp >= 0x1F000 || (cp >= 0x2600 && cp <= 0x27BF) || cp == 0xFE0F || cp == 0x200D) {
			// Emoji: no glyphs on the console; drop the joiners, show a placeholder for the rest.
			if (cp != 0xFE0F && cp != 0x200D && (cp < 0x1F3FB || cp > 0x1F3FF)) {
				out += "\xE2\x96\xA1"; // white square
			}
		} else {
			out.append(s, start, i - start);
		}
	}
	return out;
}

std::vector<Span> parseInline(const std::string &s, std::vector<std::string> &links, uint8_t baseStyle) {
	std::vector<Span> spans;
	std::string cur;
	uint8_t style = baseStyle;
	auto flush = [&](int link = -1) {
		if (!cur.empty()) {
			spans.push_back(Span{cur, style, link});
			cur.clear();
		}
	};
	size_t i = 0;
	const size_t n = s.size();
	while (i < n) {
		char c = s[i];
		if (c == '\\' && i + 1 < n && ispunct((unsigned char)s[i + 1])) {
			cur += s[i + 1];
			i += 2;
			continue;
		}
		if (c == '`') {
			size_t ticks = 1;
			while (i + ticks < n && s[i + ticks] == '`') {
				ticks++;
			}
			std::string fence(ticks, '`');
			size_t end = s.find(fence, i + ticks);
			if (end != std::string::npos) {
				flush();
				std::string code = s.substr(i + ticks, end - i - ticks);
				if (code.size() >= 2 && code.front() == ' ' && code.back() == ' ') {
					code = code.substr(1, code.size() - 2);
				}
				spans.push_back(Span{code, (uint8_t)(style | Code), -1});
				i = end + ticks;
				continue;
			}
		}
		if ((c == '*' || c == '_') && i + 1 < n && s[i + 1] == c) {
			flush();
			style ^= Bold;
			i += 2;
			continue;
		}
		if (c == '~' && i + 1 < n && s[i + 1] == '~') {
			flush();
			style ^= Strike;
			i += 2;
			continue;
		}
		if (c == '*' || c == '_') {
			bool prevAlnum = i > 0 && isalnum((unsigned char)s[i - 1]);
			bool nextAlnum = i + 1 < n && isalnum((unsigned char)s[i + 1]);
			bool nextSpace = i + 1 >= n || s[i + 1] == ' ';
			bool opening = !(style & Italic) && !nextSpace && !(c == '_' && prevAlnum);
			bool closing = (style & Italic) && !(c == '_' && nextAlnum);
			if (opening || closing) {
				flush();
				style ^= Italic;
				i++;
				continue;
			}
		}
		if (c == '[') {
			size_t close = s.find(']', i + 1);
			if (close != std::string::npos && close + 1 < n && s[close + 1] == '(') {
				size_t paren = s.find(')', close + 2);
				if (paren != std::string::npos) {
					flush();
					std::string label = s.substr(i + 1, close - i - 1);
					std::string url = s.substr(close + 2, paren - close - 2);
					links.push_back(url);
					int idx = (int)links.size() - 1;
					uint8_t saved = style;
					style |= Link;
					for (const Span &inner : parseInline(label, links, style)) {
						spans.push_back(Span{inner.text, inner.style, idx});
					}
					style = saved;
					i = paren + 1;
					continue;
				}
			}
		}
		if (c == 'h' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '(') &&
		    (s.compare(i, 7, "http://") == 0 || s.compare(i, 8, "https://") == 0)) {
			size_t end = i;
			while (end < n && s[end] != ' ' && s[end] != ')' && s[end] != '>') {
				end++;
			}
			while (end > i && (s[end - 1] == '.' || s[end - 1] == ',')) {
				end--;
			}
			flush();
			std::string url = s.substr(i, end - i);
			links.push_back(url);
			spans.push_back(Span{url, (uint8_t)(style | Link), (int)links.size() - 1});
			i = end;
			continue;
		}
		if (c == '<' && (s.compare(i, 8, "<http://") == 0 || s.compare(i, 9, "<https://") == 0)) {
			size_t end = s.find('>', i);
			if (end != std::string::npos) {
				flush();
				std::string url = s.substr(i + 1, end - i - 1);
				links.push_back(url);
				spans.push_back(Span{url, (uint8_t)(style | Link), (int)links.size() - 1});
				i = end + 1;
				continue;
			}
		}
		cur += c;
		i++;
	}
	flush();
	return spans;
}

class Builder {
  public:
	Builder(Layout &out, float width, float scale) : out_(out), width_(width), scale_(scale) {}

	void gap(float factor) {
		if (!out_.lines.empty()) {
			y_ += gfx::lineHeight(scale_) * factor;
		}
	}

	// Wraps spans into lines starting at x = indent.
	void flow(const std::vector<Span> &spans, float indent, float scale, LineKind kind, Bullet bullet,
	          const std::string &bulletText, float bulletX, int quoteDepth, float hangIndent = -1) {
		float lh = gfx::lineHeight(scale);
		Line line;
		line.kind = kind;
		line.scale = scale;
		line.y = y_;
		line.h = lh;
		line.bullet = bullet;
		line.bulletText = bulletText;
		line.bulletX = bulletX;
		line.quoteDepth = quoteDepth;
		float x = indent;
		float maxX = width_;
		float contIndent = hangIndent >= 0 ? hangIndent : indent;
		auto newLine = [&]() {
			out_.lines.push_back(line);
			y_ += lh;
			line.runs.clear();
			line.y = y_;
			line.bullet = Bullet::None;
			line.bulletText.clear();
			x = contIndent;
		};
		auto addPiece = [&](const std::string &text, uint8_t style, int link, float w) {
			if (!line.runs.empty() && line.runs.back().style == style && line.runs.back().link == link &&
			    std::fabs(line.runs.back().x + line.runs.back().w - x) < 0.01f) {
				line.runs.back().text += text;
				line.runs.back().w += w;
			} else {
				Run r;
				r.text = text;
				r.x = x;
				r.w = w;
				r.style = style;
				r.link = link;
				line.runs.push_back(r);
			}
			x += w;
		};
		for (const Span &sp : spans) {
			float runScale = (sp.style & Code) && kind != LineKind::Code ? scale * 0.94f : scale;
			const std::string &t = sp.text;
			size_t i = 0;
			while (i < t.size()) {
				// Next breakable piece: a word plus its spaces, or a single CJK character.
				size_t start = i;
				uint32_t cp = util::utf8Next(t, i);
				if (!isCjk(cp) && cp != ' ') {
					while (i < t.size()) {
						size_t save = i;
						uint32_t c2 = util::utf8Next(t, i);
						if (c2 == ' ' || isCjk(c2)) {
							i = save;
							break;
						}
					}
				}
				while (i < t.size() && t[i] == ' ') {
					i++;
				}
				std::string word = t.substr(start, i - start);
				float w = gfx::textWidth(word, runScale) + ((sp.style & Code) ? 2.0f : 0.0f);
				std::string trimmed = word;
				while (!trimmed.empty() && trimmed.back() == ' ') {
					trimmed.pop_back();
				}
				float wTrim = gfx::textWidth(trimmed, runScale);
				if (x + wTrim > maxX && x > contIndent + 0.5f) {
					newLine();
					if (word[0] == ' ') {
						continue;
					}
				}
				// Longer than a whole line: break by character.
				if (contIndent + wTrim > maxX) {
					size_t j = 0;
					std::string piece;
					float pw = 0;
					while (j < word.size()) {
						size_t s0 = j;
						uint32_t ch = util::utf8Next(word, j);
						float a = gfx::glyphAdvance(ch, runScale);
						if (x + pw + a > maxX && !piece.empty()) {
							addPiece(piece, sp.style, sp.link, pw);
							newLine();
							piece.clear();
							pw = 0;
						}
						piece.append(word, s0, j - s0);
						pw += a;
					}
					if (!piece.empty()) {
						addPiece(piece, sp.style, sp.link, pw);
					}
					continue;
				}
				addPiece(word, sp.style, sp.link, w);
			}
		}
		out_.lines.push_back(line);
		y_ += lh;
	}

	void codeBlock(const std::vector<std::string> &lines, const std::string &lang) {
		float scale = theme::code();
		float lh = gfx::lineHeight(scale);
		const float pad = 6;
		Panel panel;
		panel.code = true;
		panel.label = lang;
		y_ += 2;
		panel.y0 = y_;
		y_ += pad + (lang.empty() ? 0 : lh * 0.8f);
		float maxW = width_ - pad * 2;
		for (const std::string &raw : lines) {
			std::string text = raw;
			float x = 0;
			Line line;
			line.kind = LineKind::Code;
			line.scale = scale;
			line.h = lh;
			line.y = y_;
			std::string piece;
			float pw = 0;
			size_t j = 0;
			bool first = true;
			auto emit = [&]() {
				Run r;
				r.text = piece;
				r.x = pad + (first ? 0 : 10);
				r.w = pw;
				line.runs.push_back(r);
				line.y = y_;
				out_.lines.push_back(line);
				line.runs.clear();
				y_ += lh;
				piece.clear();
				pw = 0;
				first = false;
			};
			while (j < text.size()) {
				size_t s0 = j;
				uint32_t ch = util::utf8Next(text, j);
				float a = gfx::glyphAdvance(ch, scale);
				if (pw + a > maxW - (first ? 0 : 10) && !piece.empty()) {
					emit();
				}
				piece.append(text, s0, j - s0);
				pw += a;
			}
			(void)x;
			emit();
		}
		y_ += pad;
		panel.y1 = y_;
		out_.panels.push_back(panel);
		y_ += 2;
	}

	void rule() {
		Line line;
		line.kind = LineKind::Rule;
		line.y = y_;
		line.h = gfx::lineHeight(scale_) * 0.8f;
		out_.lines.push_back(line);
		y_ += line.h;
	}

	float y() const { return y_; }

	Layout &out_;
	float width_;
	float scale_;
	float y_ = 0;
};

bool isFence(const std::string &trimmed) {
	return util::startsWith(trimmed, "```") || util::startsWith(trimmed, "~~~");
}

bool isRule(const std::string &t) {
	if (t.size() < 3) {
		return false;
	}
	char c = t[0];
	if (c != '-' && c != '*' && c != '_') {
		return false;
	}
	int count = 0;
	for (char ch : t) {
		if (ch == c) {
			count++;
		} else if (ch != ' ') {
			return false;
		}
	}
	return count >= 3;
}

int headingLevel(const std::string &t) {
	int level = 0;
	while (level < (int)t.size() && t[level] == '#') {
		level++;
	}
	if (level >= 1 && level <= 6 && level < (int)t.size() && t[level] == ' ') {
		return level;
	}
	return 0;
}

// "- item", "* item", "1. item", "2) item" -> marker kind and content.
bool listItem(const std::string &line, int &indent, Bullet &bullet, std::string &number, std::string &content) {
	size_t i = 0;
	while (i < line.size() && line[i] == ' ') {
		i++;
	}
	indent = (int)i;
	if (i + 1 < line.size() && (line[i] == '-' || line[i] == '*' || line[i] == '+') && line[i + 1] == ' ') {
		bullet = Bullet::Dot;
		content = line.substr(i + 2);
		if (util::startsWith(content, "[ ] ")) {
			bullet = Bullet::Unchecked;
			content = content.substr(4);
		} else if (util::startsWith(content, "[x] ") || util::startsWith(content, "[X] ")) {
			bullet = Bullet::Checked;
			content = content.substr(4);
		}
		return true;
	}
	size_t d = i;
	while (d < line.size() && isdigit((unsigned char)line[d]) && d - i < 4) {
		d++;
	}
	if (d > i && d + 1 < line.size() && (line[d] == '.' || line[d] == ')') && line[d + 1] == ' ') {
		bullet = Bullet::Number;
		number = line.substr(i, d - i) + ".";
		content = line.substr(d + 2);
		return true;
	}
	return false;
}

std::vector<std::string> tableCells(const std::string &row) {
	std::string t = util::trim(row);
	if (!t.empty() && t[0] == '|') {
		t = t.substr(1);
	}
	if (!t.empty() && t.back() == '|') {
		t.pop_back();
	}
	std::vector<std::string> cells;
	for (const std::string &c : util::split(t, '|')) {
		cells.push_back(util::trim(c));
	}
	return cells;
}

bool isTableSeparator(const std::string &row) {
	for (const std::string &c : tableCells(row)) {
		for (char ch : c) {
			if (ch != '-' && ch != ':' && ch != ' ') {
				return false;
			}
		}
	}
	return true;
}

} // namespace

Layout build(const std::string &source, float width, float scale, bool streaming) {
	Layout out;
	out.width = width;
	Builder b(out, width, scale);
	std::vector<std::string> lines = util::split(normalize(source), '\n');
	size_t i = 0;
	bool firstBlock = true;
	auto blockGap = [&]() {
		if (!firstBlock) {
			b.gap(0.45f);
		}
		firstBlock = false;
	};
	while (i < lines.size()) {
		const std::string &line = lines[i];
		std::string t = util::trim(line);
		if (t.empty()) {
			i++;
			continue;
		}
		if (isFence(t)) {
			std::string fence = t.substr(0, 3);
			std::string lang = util::trim(t.substr(3));
			std::vector<std::string> code;
			i++;
			while (i < lines.size() && !util::startsWith(util::trim(lines[i]), fence)) {
				code.push_back(lines[i]);
				i++;
			}
			i++; // closing fence (or end while streaming)
			while (!code.empty() && util::trim(code.back()).empty() && streaming) {
				code.pop_back();
			}
			blockGap();
			b.codeBlock(code, lang);
			continue;
		}
		if (int level = headingLevel(t)) {
			blockGap();
			float hs = scale * (level == 1 ? 1.28f : level == 2 ? 1.16f : 1.06f);
			b.flow(parseInline(t.substr(level + 1), out.links, Bold), 0, hs, LineKind::Heading, Bullet::None, "", 0, 0);
			i++;
			continue;
		}
		if (isRule(t)) {
			blockGap();
			b.rule();
			i++;
			continue;
		}
		if (t[0] == '>') {
			blockGap();
			std::string quote;
			int depth = 1;
			while (i < lines.size()) {
				std::string q = util::trim(lines[i]);
				if (q.empty() || q[0] != '>') {
					break;
				}
				size_t k = 0;
				int d = 0;
				while (k < q.size() && (q[k] == '>' || q[k] == ' ')) {
					d += q[k] == '>';
					k++;
				}
				depth = std::max(depth, std::min(d, 3));
				quote += (quote.empty() ? "" : " ") + q.substr(k);
				i++;
			}
			float indent = 10.0f * depth;
			b.flow(parseInline(quote, out.links, Italic), indent, scale, LineKind::Text, Bullet::None, "", 0, depth);
			continue;
		}
		if (t[0] == '|') {
			blockGap();
			bool header = true;
			while (i < lines.size() && !util::trim(lines[i]).empty() && util::trim(lines[i])[0] == '|') {
				if (isTableSeparator(lines[i])) {
					i++;
					continue;
				}
				auto cells = tableCells(lines[i]);
				std::vector<Span> spans;
				for (size_t c = 0; c < cells.size(); c++) {
					if (c > 0) {
						spans.push_back(Span{"  \xE2\x94\x82  ", Plain, -1}); // box drawing bar
					}
					for (const Span &s : parseInline(cells[c], out.links, header ? Bold : Plain)) {
						spans.push_back(s);
					}
				}
				b.flow(spans, 0, scale * 0.94f, LineKind::Text, Bullet::None, "", 0, 0, 10);
				if (header) {
					Line sep;
					sep.kind = LineKind::Rule;
					sep.y = b.y_;
					sep.h = 4;
					out.lines.push_back(sep);
					b.y_ += 4;
				}
				header = false;
				i++;
			}
			continue;
		}
		int indent;
		Bullet bullet;
		std::string number, content;
		if (listItem(line, indent, bullet, number, content)) {
			blockGap();
			while (i < lines.size() && listItem(lines[i], indent, bullet, number, content)) {
				i++;
				// Lazy continuation lines belong to the item.
				while (i < lines.size()) {
					const std::string &next = lines[i];
					std::string nt = util::trim(next);
					int ind2;
					Bullet b2;
					std::string n2, c2;
					if (nt.empty() || listItem(next, ind2, b2, n2, c2) || isFence(nt) || headingLevel(nt) || nt[0] == '>' ||
					    nt[0] == '|') {
						break;
					}
					content += " " + nt;
					i++;
				}
				int level = std::min(indent / 2, 4);
				float bx = 4 + level * 12.0f;
				float tx = bx + (bullet == Bullet::Number ? gfx::textWidth(number, scale) + 5 : 11);
				Bullet shown = bullet;
				if (bullet == Bullet::Dot) {
					shown = level == 0 ? Bullet::Dot : level == 1 ? Bullet::Ring : Bullet::Square;
				}
				b.flow(parseInline(content, out.links, bullet == Bullet::Checked ? Strike : Plain), tx, scale,
				       LineKind::Text, shown, number, bx, 0);
				b.gap(0.12f);
				while (i < lines.size() && util::trim(lines[i]).empty() && i + 1 < lines.size() &&
				       listItem(lines[i + 1], indent, bullet, number, content)) {
					i++;
				}
			}
			continue;
		}
		// Paragraph: consecutive lines, joined; a line ending in two spaces or a backslash breaks.
		blockGap();
		std::string para;
		while (i < lines.size()) {
			const std::string &p = lines[i];
			std::string pt = util::trim(p);
			int ind2;
			Bullet b2;
			std::string n2, c2;
			if (pt.empty() || isFence(pt) || headingLevel(pt) || (pt[0] == '>') || (pt[0] == '|' && !para.empty()) ||
			    (listItem(p, ind2, b2, n2, c2) && !para.empty())) {
				break;
			}
			if (!para.empty()) {
				para += (util::endsWith(para, "  ") || util::endsWith(para, "\\")) ? "\n" : " ";
			}
			para += pt;
			i++;
		}
		for (const std::string &piece : util::split(para, '\n')) {
			std::string clean = piece;
			if (util::endsWith(clean, "\\")) {
				clean.pop_back();
			}
			b.flow(parseInline(clean, out.links, Plain), 0, scale, LineKind::Text, Bullet::None, "", 0, 0);
		}
	}
	out.height = b.y_;
	return out;
}

void draw(const Layout &layout, float x, float y, float viewTop, float viewBottom, Color color) {
	using namespace gfx;
	for (const Panel &p : layout.panels) {
		float top = y + p.y0, bottom = y + p.y1;
		if (bottom < viewTop || top > viewBottom) {
			continue;
		}
		roundRect(x, top, layout.width, bottom - top, 6, theme::codeBg);
		if (!p.label.empty()) {
			text(x + 6, top + 3, theme::tiny, theme::textFaint, p.label);
		}
	}
	for (const Line &line : layout.lines) {
		float ly = y + line.y;
		if (ly + line.h < viewTop || ly > viewBottom) {
			continue;
		}
		if (line.kind == LineKind::Rule) {
			rect(x, ly + line.h / 2, layout.width, 1, theme::border);
			continue;
		}
		if (line.quoteDepth > 0) {
			for (int d = 0; d < line.quoteDepth; d++) {
				rect(x + 2 + d * 10, ly, 2, line.h, theme::border);
			}
		}
		float cy = ly + line.h / 2;
		Color bulletColor = theme::textDim;
		switch (line.bullet) {
		case Bullet::Dot:
			circle(x + line.bulletX + 2.5f, cy, 2.0f, bulletColor);
			break;
		case Bullet::Ring:
			arc(x + line.bulletX + 2.5f, cy, 2.0f, 1.0f, 0, 6.2832f, bulletColor);
			break;
		case Bullet::Square:
			rect(x + line.bulletX + 1, cy - 1.5f, 3, 3, bulletColor);
			break;
		case Bullet::Number:
			text(x + line.bulletX, ly, line.scale, theme::textDim, line.bulletText);
			break;
		case Bullet::Unchecked:
			roundRectBorder(x + line.bulletX - 1, cy - 4, 8, 8, 2, theme::bg, theme::textDim);
			break;
		case Bullet::Checked:
			roundRect(x + line.bulletX - 1, cy - 4, 8, 8, 2, theme::green);
			gfx::line(x + line.bulletX + 0.5f, cy, x + line.bulletX + 2.5f, cy + 2, 1.2f, theme::bgDeep);
			gfx::line(x + line.bulletX + 2.5f, cy + 2, x + line.bulletX + 6, cy - 2.5f, 1.2f, theme::bgDeep);
			break;
		case Bullet::None:
			break;
		}
		for (const Run &r : line.runs) {
			float scale = line.scale;
			Color c = color;
			if (line.kind == LineKind::Code) {
				c = theme::codeText;
			} else if (r.style & Code) {
				scale *= 0.94f;
				c = theme::inlineCode;
				std::string trimmed = r.text;
				while (!trimmed.empty() && trimmed.back() == ' ') {
					trimmed.pop_back();
				}
				roundRect(x + r.x - 1, ly + 1, textWidth(trimmed, scale) + 3, line.h - 2, 3, theme::panelHi);
			} else if (r.style & Link) {
				c = theme::link;
			} else if (r.style & Italic) {
				c = gfx::mix(color, theme::textDim, 0.35f);
			}
			if (line.kind == LineKind::Heading) {
				c = theme::text;
			}
			float ty = ly + (line.h - lineHeight(scale)) / 2;
			float tx = x + r.x + ((r.style & Code) && line.kind != LineKind::Code ? 1.0f : 0.0f);
			if ((r.style & Bold) || line.kind == LineKind::Heading) {
				textBold(tx, ty, scale, c, r.text);
			} else {
				text(tx, ty, scale, c, r.text);
			}
			if (r.style & (Link | Strike)) {
				std::string trimmed = r.text;
				while (!trimmed.empty() && trimmed.back() == ' ') {
					trimmed.pop_back();
				}
				float w = textWidth(trimmed, scale);
				float yy = (r.style & Strike) ? ly + line.h / 2 : ly + line.h - 2;
				rect(tx, yy, w, 1, gfx::withAlpha(c, 160));
			}
		}
	}
}

std::string stripInline(const std::string &markdown) {
	std::vector<std::string> links;
	std::string out;
	for (const std::string &line : util::split(markdown, '\n')) {
		std::string t = util::trim(line);
		while (!t.empty() && (t[0] == '#' || t[0] == '>')) {
			t = util::trim(t.substr(1));
		}
		if (isFence(t)) {
			continue;
		}
		for (const Span &s : parseInline(t, links, Plain)) {
			out += s.text;
		}
		out += ' ';
	}
	return util::oneLine(out);
}

} // namespace md
