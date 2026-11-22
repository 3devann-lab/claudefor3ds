#include "ui/chat_view.h"

#include "app/app.h"
#include "platform/gfx.h"
#include "platform/input.h"
#include "ui/images.h"
#include "ui/markdown.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "util/util.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace chat {

using namespace gfx;
using claude::Block;
using claude::BlockType;
using claude::Message;
using claude::Role;

namespace {

constexpr float X = 10.0f;
constexpr float W = 378.0f;
constexpr float GAP = 10.0f;

enum class El { Header, Markdown, Bubble, Thinking, Tool, Image, File, Artifact, Notice, Unknown, Working, Error, Stopped };

struct Element {
	El kind;
	float y = 0, h = 0;
	int block = -1;
	md::Layout md;
	std::string text;
	float bubbleW = 0;
};

struct MsgLayout {
	uint32_t version = 0;
	float scale = 0;
	bool header = false;
	bool showThinking = true;
	float height = 0;
	std::vector<Element> els;
};

std::unordered_map<std::string, MsgLayout> g_cache;
float g_scroll = 0, g_target = 0, g_contentH = 0;
bool g_follow = true;
int g_selected = -1;

ui::Icon toolIcon(const Block &b) {
	using claude::ToolKind;
	switch (claude::classifyTool(b.name, !b.server.empty())) {
	case ToolKind::WebSearch:
		return ui::Icon::Search;
	case ToolKind::WebFetch:
		return ui::Icon::Globe;
	case ToolKind::Command:
		return ui::Icon::Terminal;
	case ToolKind::FileRead:
		return ui::Icon::File;
	case ToolKind::FileEdit:
		return ui::Icon::Pencil;
	case ToolKind::Search:
		return ui::Icon::Search;
	case ToolKind::Todo:
		return ui::Icon::List;
	case ToolKind::Question:
		return ui::Icon::Question;
	case ToolKind::Agent:
		return ui::Icon::Agent;
	case ToolKind::Schedule:
		return ui::Icon::Clock;
	case ToolKind::Connector:
		return ui::Icon::Plug;
	case ToolKind::Artifact:
		return ui::Icon::Sparkle;
	case ToolKind::Memory:
		return ui::Icon::Brain;
	case ToolKind::Image:
		return ui::Icon::Image;
	case ToolKind::Generic:
		break;
	}
	return ui::Icon::Code;
}

float maxLineWidth(const md::Layout &l) {
	float w = 0;
	for (const auto &line : l.lines) {
		if (!line.runs.empty()) {
			const md::Run &r = line.runs.back();
			w = std::max(w, r.x + r.w);
		}
	}
	return w;
}

bool hasVisibleContent(const Message &m) {
	for (const Block &b : m.blocks) {
		if (b.type != BlockType::ToolResult) {
			return true;
		}
	}
	return false;
}

MsgLayout build(const Message &m, bool header) {
	MsgLayout out;
	out.version = m.version;
	out.scale = theme::body();
	out.header = header;
	out.showThinking = config().showThinking;
	float scale = out.scale;
	float y = 0;
	auto add = [&](Element e, float gapAfter = 4) {
		e.y = y;
		y += e.h + gapAfter;
		out.els.push_back(std::move(e));
	};
	if (m.role == Role::User) {
		Element e;
		e.kind = El::Bubble;
		e.md = md::build(m.plainText(), 290, scale);
		e.bubbleW = std::min(306.0f, maxLineWidth(e.md) + 16);
		e.h = e.md.height + 10;
		add(std::move(e), 0);
		out.height = y;
		return out;
	}
	if (header) {
		Element e;
		e.kind = El::Header;
		e.h = 14;
		add(std::move(e), 3);
	}
	bool streaming = m.state == Message::State::Streaming;
	for (size_t i = 0; i < m.blocks.size(); i++) {
		const Block &b = m.blocks[i];
		Element e;
		e.block = (int)i;
		switch (b.type) {
		case BlockType::Text:
			if (util::trim(b.text).empty()) {
				continue;
			}
			e.kind = El::Markdown;
			e.md = md::build(b.text, W, scale, !b.done);
			e.h = e.md.height;
			add(std::move(e), 6);
			break;
		case BlockType::Thinking:
			e.kind = El::Thinking;
			e.h = 15;
			if (!b.done && out.showThinking && !b.text.empty()) {
				// Last two lines of the reasoning as a live preview.
				std::string tail = b.text.size() > 400 ? b.text.substr(util::utf8Floor(b.text, b.text.size() - 400)) : b.text;
				e.text = util::oneLine(tail);
				e.h += lineHeight(theme::small) * 2 + 2;
			}
			add(std::move(e));
			break;
		case BlockType::ToolUse:
			e.kind = El::Tool;
			e.h = 15;
			add(std::move(e), 2);
			break;
		case BlockType::ToolResult:
			continue;
		case BlockType::Image:
			e.kind = El::Image;
			e.h = 116;
			add(std::move(e), 6);
			break;
		case BlockType::File:
			e.kind = El::File;
			e.h = 30;
			add(std::move(e), 6);
			break;
		case BlockType::Artifact: {
			e.kind = El::Artifact;
			std::string preview;
			int lines = 0;
			for (const std::string &l : util::split(b.text, '\n')) {
				if (lines++ >= 6) {
					break;
				}
				preview += l + "\n";
			}
			e.md = md::build("```\n" + preview + "```", W - 12, scale);
			e.h = 20 + std::min(e.md.height, 90.0f) + 4;
			add(std::move(e), 6);
			break;
		}
		case BlockType::Notice:
			e.kind = El::Notice;
			e.md = md::build(b.text, W - 18, theme::small);
			e.h = std::max(14.0f, e.md.height);
			add(std::move(e), 6);
			break;
		case BlockType::Unknown:
			e.kind = El::Unknown;
			e.text = b.rawType.empty() ? "content" : b.rawType;
			e.h = 30;
			add(std::move(e), 6);
			break;
		}
	}
	bool lastIsTool = !out.els.empty() && (out.els.back().kind == El::Tool || out.els.back().kind == El::Thinking);
	if (streaming && (!hasVisibleContent(m) || lastIsTool)) {
		bool thinkingLive = false;
		for (const Block &b : m.blocks) {
			thinkingLive |= b.type == BlockType::Thinking && !b.done;
		}
		if (!thinkingLive) {
			Element e;
			e.kind = El::Working;
			e.h = 18;
			add(std::move(e));
		}
	}
	if (m.state == Message::State::Error) {
		Element e;
		e.kind = El::Error;
		e.md = md::build(m.error.empty() ? "Something went wrong." : m.error, W - 20, theme::small);
		e.h = e.md.height + 16;
		add(std::move(e));
	} else if (m.state == Message::State::Stopped) {
		Element e;
		e.kind = El::Stopped;
		e.h = 12;
		add(std::move(e));
	}
	out.height = std::max(0.0f, y - 2);
	return out;
}

struct Placed {
	int index;
	float y;
	const MsgLayout *layout;
};

std::vector<Placed> g_placed;

void layoutAll() {
	auto &conv = app::App::get().conv;
	float scale = theme::body();
	if (g_cache.size() > 400) {
		g_cache.clear();
	}
	g_placed.clear();
	float y = 8;
	bool prevUser = true;
	for (size_t i = 0; i < conv.messages.size(); i++) {
		const Message &m = conv.messages[i];
		if (m.hidden) {
			continue;
		}
		if (m.role == Role::Assistant && m.blocks.empty() && m.state == Message::State::Done) {
			continue;
		}
		bool header = m.role == Role::Assistant && prevUser;
		MsgLayout &cached = g_cache[m.id];
		if (cached.version != m.version || cached.scale != scale || cached.header != header ||
		    cached.showThinking != config().showThinking || cached.version == 0) {
			cached = build(m, header);
		}
		g_placed.push_back(Placed{(int)i, y, &cached});
		y += cached.height + GAP;
		prevUser = m.role == Role::User;
	}
	g_contentH = y + 4;
}

float maxScroll() { return std::max(0.0f, g_contentH - (VIEW_BOTTOM - VIEW_TOP)); }

void drawElement(const Message &m, const Element &e, float y, float viewTop, float viewBottom) {
	const Block *b = e.block >= 0 && e.block < (int)m.blocks.size() ? &m.blocks[e.block] : nullptr;
	switch (e.kind) {
	case El::Header:
		ui::claudeLogo(X + 5, y + 7, 5.5f, theme::accent);
		text(X + 15, y, theme::small, theme::textDim, "Claude");
		break;
	case El::Bubble: {
		float bx = X + W - e.bubbleW;
		roundRect(bx, y, e.bubbleW, e.h, 10, theme::userBubble);
		md::draw(e.md, bx + 8, y + 5, viewTop, viewBottom, theme::text);
		break;
	}
	case El::Markdown:
		md::draw(e.md, X, y, viewTop, viewBottom, theme::text);
		break;
	case El::Thinking: {
		bool live = b && !b->done;
		if (live) {
			ui::spinner(X + 6, y + 7, 5.5f, theme::accent);
			float secs = b->startedMs ? (util::nowMs() - b->startedMs) / 1000.0f : 0;
			std::string label = secs >= 1 ? util::format("Thinking... %ds", (int)secs) : "Thinking...";
			text(X + 16, y, theme::small, theme::textDim, label);
			if (!e.text.empty()) {
				float lh = lineHeight(theme::small);
				rect(X + 5, y + 16, 1.5f, lh * 2, theme::border);
				std::string t = e.text;
				// Show the tail that fits in two lines.
				std::string l1, l2 = fitText(t, theme::small, W - 16);
				if (textWidth(t, theme::small) > W - 16) {
					size_t cut = t.size();
					float wsum = 0;
					while (cut > 0 && wsum < W - 20) {
						cut = util::utf8Floor(t, cut - 1);
						wsum = textWidth(t.substr(cut), theme::small);
					}
					l2 = t.substr(cut);
					std::string head = t.substr(0, cut);
					size_t cut2 = head.size();
					wsum = 0;
					while (cut2 > 0 && wsum < W - 30) {
						cut2 = util::utf8Floor(head, cut2 - 1);
						wsum = textWidth(head.substr(cut2), theme::small);
					}
					l1 = head.substr(cut2);
				}
				text(X + 12, y + 16, theme::small, theme::textFaint, l1);
				text(X + 12, y + 16 + lh, theme::small, theme::textFaint, l2);
			}
		} else {
			ui::icon(ui::Icon::Brain, X + 6, y + 7, 11, theme::textFaint);
			std::string label = "Thought";
			if (b && b->startedMs && b->endedMs > b->startedMs) {
				int secs = (int)((b->endedMs - b->startedMs + 500) / 1000);
				label = secs < 1 ? "Thought briefly" : util::format("Thought for %ds", secs);
			}
			text(X + 16, y, theme::small, theme::textFaint, label);
		}
		break;
	}
	case El::Tool: {
		if (!b) {
			break;
		}
		bool running = !b->done && m.state == Message::State::Streaming;
		Color c = b->error ? theme::red : theme::textDim;
		ui::icon(toolIcon(*b), X + 6, y + 7, 11, running ? theme::accent : c);
		std::string label = claude::describeToolCall(*b, running);
		float tw = textFit(X + 16, y, theme::small, c, label, W - 40);
		float sx = X + 16 + tw + 8;
		if (running) {
			ui::spinner(sx + 4, y + 7, 4.5f, theme::accent);
		} else if (b->error) {
			ui::icon(ui::Icon::Close, sx + 4, y + 7, 8, theme::red);
		} else {
			ui::icon(ui::Icon::Check, sx + 4, y + 7, 9, theme::green);
		}
		break;
	}
	case El::Image: {
		if (!b) {
			break;
		}
		const images::Entry &img = images::get(*b);
		float bw = 200, bh = e.h;
		if (img.state == images::State::Ready) {
			float iw = (float)imageWidth(img.image), ih = (float)imageHeight(img.image);
			float s = std::min(bw / iw, bh / ih);
			float dw = iw * s, dh = ih * s;
			roundRectBorder(X - 1, y - 1, dw + 2, dh + 2, 3, theme::codeBg, theme::border);
			drawImage(img.image, X, y, dw, dh);
		} else {
			roundRectBorder(X, y, bw, bh, 6, theme::panel, theme::border);
			if (img.state == images::State::Loading) {
				ui::spinner(X + bw / 2, y + bh / 2 - 6, 8, theme::textFaint);
				text(X + bw / 2 - textWidth("Loading image", theme::tiny) / 2, y + bh / 2 + 8, theme::tiny,
				     theme::textFaint, "Loading image");
			} else {
				ui::icon(ui::Icon::Image, X + bw / 2, y + bh / 2 - 8, 18, theme::textFaint);
				std::string msg = img.state == images::State::NotImage ? "Not an image" : img.error;
				textFit(X + 8, y + bh / 2 + 6, theme::tiny, theme::textFaint, msg, bw - 16);
			}
		}
		break;
	}
	case El::File: {
		if (!b) {
			break;
		}
		roundRectBorder(X, y, 240, e.h, 6, theme::panel, theme::border);
		ui::icon(ui::Icon::File, X + 14, y + e.h / 2, 14, theme::textDim);
		const images::Entry &info = images::get(*b);
		std::string name = !info.name.empty() ? info.name : b->name;
		textFit(X + 28, y + 2, theme::small, theme::text, name, 200);
		std::string sub = info.state == images::State::Loading ? "Checking file..." : (info.mime.empty() ? "File" : info.mime);
		textFit(X + 28, y + 15, theme::tiny, theme::textFaint, sub + "  \xC2\xB7  A: save to SD", 200);
		break;
	}
	case El::Artifact: {
		if (!b) {
			break;
		}
		roundRectBorder(X, y, W, e.h, 7, theme::panel, theme::border);
		ui::icon(ui::Icon::Sparkle, X + 10, y + 9, 10, theme::accent);
		textFit(X + 20, y + 2, theme::small, theme::text, b->name, W - 120, true);
		std::string kind = b->mime.empty() ? "artifact" : b->mime;
		float kw = textWidth(kind, theme::tiny);
		text(X + W - kw - 8, y + 4, theme::tiny, theme::textFaint, kind);
		clip(X + 4, std::max(viewTop, y + 18), W - 8, std::min(90.0f, e.h - 22));
		md::draw(e.md, X + 6, y + 18, viewTop, viewBottom, theme::text);
		noClip();
		clip(0, viewTop, TOP_W, viewBottom - viewTop);
		break;
	}
	case El::Notice:
		ui::icon(ui::Icon::Info, X + 6, y + 7, 10, theme::textFaint);
		md::draw(e.md, X + 16, y, viewTop, viewBottom, theme::textDim);
		break;
	case El::Unknown:
		roundRectBorder(X, y, W, e.h, 6, theme::panel, theme::border);
		ui::icon(ui::Icon::Info, X + 12, y + e.h / 2, 12, theme::textFaint);
		textFit(X + 24, y + 3, theme::small, theme::textDim, "This content can't be shown on a 3DS", W - 32);
		textFit(X + 24, y + 16, theme::tiny, theme::textFaint, "Type: " + e.text, W - 32);
		break;
	case El::Working:
		ui::spinner(X + 7, y + 9, 6.5f, theme::accent);
		text(X + 18, y + 2, theme::small, theme::textDim, m.blocks.empty() ? "Claude is thinking..." : "Working...");
		break;
	case El::Error:
		roundRect(X, y, W, e.h, 6, rgb(0x3A2321));
		ui::icon(ui::Icon::Warning, X + 10, y + 10, 11, theme::red);
		md::draw(e.md, X + 20, y + 3, viewTop, viewBottom, rgb(0xF2B8B3));
		text(X + 20, y + e.h - 13, theme::tiny, theme::textFaint, "Select this message and press A to retry");
		break;
	case El::Stopped:
		text(X, y, theme::tiny, theme::textFaint, "Stopped");
		break;
	}
}

void drawEmpty() {
	auto &a = app::App::get();
	bool code = a.conv.mode == AppMode::Code;
	float cy = 92;
	ui::claudeLogo(200, cy, 24, theme::accent, ui::time() * 0.15f);
	std::string title = code ? "Claude Code" : "How can I help you today?";
	float tw = textWidth(title, theme::title);
	textBold(200 - tw / 2, cy + 34, theme::title, theme::text, title);
	std::string sub = code ? "Describe a task. Claude works in a sandbox with bash and files." : app::App::get().modelLabel() + " is ready.";
	tw = textWidth(sub, theme::small);
	text(200 - tw / 2, cy + 56, theme::small, theme::textDim, sub);
	std::string hint = "Press Y or tap the message box below";
	tw = textWidth(hint, theme::tiny);
	text(200 - tw / 2, cy + 74, theme::tiny, theme::textFaint, hint);
}

} // namespace

int selected() { return g_selected; }

void select(int index) {
	g_selected = index;
	for (const Placed &p : g_placed) {
		if (p.index == index) {
			float viewH = VIEW_BOTTOM - VIEW_TOP;
			if (p.y < g_target + 4) {
				g_target = p.y - 4;
			} else if (p.y + p.layout->height > g_target + viewH - 4) {
				g_target = std::min(p.y - 4, p.y + p.layout->height - viewH + 8);
			}
			g_target = std::max(0.0f, std::min(maxScroll(), g_target));
			g_follow = false;
		}
	}
}

void clearSelection() { g_selected = -1; }

const Message *selectedMessage() {
	auto &conv = app::App::get().conv;
	if (g_selected < 0 || g_selected >= (int)conv.messages.size()) {
		return nullptr;
	}
	return &conv.messages[g_selected];
}

void scrollBy(float dy) {
	g_target = std::max(0.0f, std::min(maxScroll(), g_target + dy));
	g_follow = g_target >= maxScroll() - 2;
}

void page(int direction) { scrollBy(direction * (VIEW_BOTTOM - VIEW_TOP - 30)); }

void toEnd() {
	g_target = maxScroll();
	g_follow = true;
}

float position() {
	float ms = maxScroll();
	return ms <= 0 ? 1.0f : g_scroll / ms;
}

void setPosition(float f) {
	g_target = std::max(0.0f, std::min(1.0f, f)) * maxScroll();
	g_scroll = g_target;
	g_follow = f >= 0.995f;
}

bool canScroll() { return maxScroll() > 0; }

void handleInput() {
	const input::State &in = input::get();
	auto &a = app::App::get();
	// Analog scrolling: circle pad and C-stick.
	int dy = std::abs(in.circleY) > 18 ? in.circleY : (std::abs(in.cstickY) > 18 ? in.cstickY : 0);
	if (dy) {
		scrollBy(-dy * 0.06f);
	}
	if (in.repeat & input::DUP) {
		int idx = g_selected;
		int prev = -1;
		for (const Placed &p : g_placed) {
			if (idx < 0 || p.index < idx) {
				prev = p.index;
			}
		}
		if (idx < 0 && !g_placed.empty()) {
			// Start from the last message on screen.
			prev = g_placed.back().index;
		}
		if (prev >= 0) {
			select(prev);
		}
	}
	if (in.repeat & input::DDOWN) {
		if (g_selected >= 0) {
			int next = -1;
			for (const Placed &p : g_placed) {
				if (p.index > g_selected) {
					next = p.index;
					break;
				}
			}
			if (next >= 0) {
				select(next);
			} else {
				clearSelection();
				toEnd();
			}
		} else {
			scrollBy(40);
		}
	}
	if (in.repeat & input::DLEFT) {
		page(-1);
	}
	if (in.repeat & input::DRIGHT) {
		page(1);
	}
	(void)a;
}

void draw() {
	auto &a = app::App::get();
	layoutAll();
	if (a.scrollToEnd) {
		a.scrollToEnd = false;
		g_follow = true;
		clearSelection();
	}
	if (g_follow && config().autoScroll) {
		g_target = maxScroll();
	}
	g_target = std::max(0.0f, std::min(maxScroll(), g_target));
	g_scroll += (g_target - g_scroll) * 0.3f;
	if (std::fabs(g_target - g_scroll) < 0.3f) {
		g_scroll = g_target;
	}
	if (g_placed.empty()) {
		drawEmpty();
		return;
	}
	const float viewTop = VIEW_TOP, viewBottom = VIEW_BOTTOM;
	clip(0, viewTop, TOP_W, viewBottom - viewTop);
	float base = viewTop - std::floor(g_scroll);
	for (const Placed &p : g_placed) {
		float y = base + p.y;
		if (y > viewBottom || y + p.layout->height < viewTop) {
			continue;
		}
		const Message &m = a.conv.messages[p.index];
		if (p.index == g_selected) {
			roundRect(4, y - 4, TOP_W - 8, p.layout->height + 8, 8, theme::selection);
			rect(4, y - 2, 2.5f, p.layout->height + 4, theme::accent);
		}
		for (const Element &e : p.layout->els) {
			float ey = y + e.y;
			if (ey > viewBottom || ey + e.h < viewTop) {
				continue;
			}
			drawElement(m, e, ey, viewTop, viewBottom);
		}
	}
	noClip();
	ui::scrollbar(395, viewTop + 2, viewBottom - viewTop - 4, viewBottom - viewTop, g_contentH, g_scroll);
	if (!g_follow && a.busy()) {
		// New content below: a small "jump to latest" marker.
		roundRect(170, 220, 60, 16, 8, theme::panelHi);
		ui::icon(ui::Icon::ChevronDown, 182, 228, 8, theme::text);
		text(190, 221, theme::tiny, theme::text, "Latest");
	}
}

} // namespace chat
