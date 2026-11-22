#include "ui/ui.h"

#include "core/config.h"
#include "platform/input.h"
#include "platform/system.h"
#include "util/util.h"

#include <cmath>
#include <vector>

namespace theme {
float body() {
	switch (config().textSize) {
	case 0:
		return 0.42f;
	case 2:
		return 0.56f;
	default:
		return 0.48f;
	}
}
float code() { return body() * 0.86f; }
} // namespace theme

namespace ui {

using namespace gfx;

namespace {
bool g_kbPending = false;
sys::KeyboardOptions g_kbOptions;
std::function<void(const std::string &)> g_kbDone;
float g_time = 0;
uint64_t g_start = 0;
std::string g_toast;
float g_toastUntil = 0;
} // namespace

float time() { return g_time; }

void keyboardLater(const sys::KeyboardOptions &options, std::function<void(const std::string &)> done) {
	if (g_kbPending) {
		return;
	}
	g_kbPending = true;
	g_kbOptions = options;
	g_kbDone = std::move(done);
}

void runPendingKeyboard() {
	if (!g_kbPending) {
		return;
	}
	g_kbPending = false;
	auto done = std::move(g_kbDone);
	g_kbDone = nullptr;
	std::string text;
	if (sys::keyboard(g_kbOptions, text) && done) {
		done(text);
	}
}

void tick() {
	if (!g_start) {
		g_start = sys::monotonicMs();
	}
	g_time = (float)(sys::monotonicMs() - g_start) / 1000.0f;
}

// ---- icons -------------------------------------------------------------------

void icon(Icon which, float cx, float cy, float s, Color c) {
	const float t = std::max(1.2f, s / 9.0f); // stroke
	const float h = s / 2;
	switch (which) {
	case Icon::Menu:
		for (int i = -1; i <= 1; i++) {
			roundRect(cx - h * 0.8f, cy + i * s * 0.28f - t / 2, s * 0.8f, t, t / 2, c);
		}
		break;
	case Icon::Plus:
		roundRect(cx - h * 0.75f, cy - t / 2, s * 0.75f, t, t / 2, c);
		roundRect(cx - t / 2, cy - h * 0.75f, t, s * 0.75f, t / 2, c);
		break;
	case Icon::Send:
		line(cx, cy + h * 0.75f, cx, cy - h * 0.7f, t * 1.2f, c);
		line(cx - h * 0.6f, cy - h * 0.1f, cx + 0.3f, cy - h * 0.75f, t * 1.2f, c);
		line(cx + h * 0.6f, cy - h * 0.1f, cx - 0.3f, cy - h * 0.75f, t * 1.2f, c);
		break;
	case Icon::Stop:
		roundRect(cx - h * 0.55f, cy - h * 0.55f, s * 0.55f, s * 0.55f, s * 0.08f, c);
		break;
	case Icon::Close:
		line(cx - h * 0.6f, cy - h * 0.6f, cx + h * 0.6f, cy + h * 0.6f, t, c);
		line(cx + h * 0.6f, cy - h * 0.6f, cx - h * 0.6f, cy + h * 0.6f, t, c);
		break;
	case Icon::Check:
		line(cx - h * 0.65f, cy, cx - h * 0.15f, cy + h * 0.5f, t * 1.1f, c);
		line(cx - h * 0.2f, cy + h * 0.5f, cx + h * 0.7f, cy - h * 0.55f, t * 1.1f, c);
		break;
	case Icon::ChevronDown:
		line(cx - h * 0.55f, cy - h * 0.25f, cx + 0.4f, cy + h * 0.3f, t, c);
		line(cx + h * 0.55f, cy - h * 0.25f, cx - 0.4f, cy + h * 0.3f, t, c);
		break;
	case Icon::ChevronUp:
		line(cx - h * 0.55f, cy + h * 0.25f, cx + 0.4f, cy - h * 0.3f, t, c);
		line(cx + h * 0.55f, cy + h * 0.25f, cx - 0.4f, cy - h * 0.3f, t, c);
		break;
	case Icon::ChevronRight:
		line(cx - h * 0.25f, cy - h * 0.55f, cx + h * 0.3f, cy + 0.4f, t, c);
		line(cx - h * 0.25f, cy + h * 0.55f, cx + h * 0.3f, cy - 0.4f, t, c);
		break;
	case Icon::ChevronLeft:
		line(cx + h * 0.25f, cy - h * 0.55f, cx - h * 0.3f, cy + 0.4f, t, c);
		line(cx + h * 0.25f, cy + h * 0.55f, cx - h * 0.3f, cy - 0.4f, t, c);
		break;
	case Icon::Search:
		arc(cx - h * 0.15f, cy - h * 0.15f, h * 0.45f, t, 0, 6.2832f, c);
		line(cx + h * 0.18f, cy + h * 0.18f, cx + h * 0.7f, cy + h * 0.7f, t * 1.2f, c);
		break;
	case Icon::Globe:
		arc(cx, cy, h * 0.7f, t, 0, 6.2832f, c);
		line(cx - h * 0.7f, cy, cx + h * 0.7f, cy, t * 0.8f, c);
		arc(cx, cy, h * 0.7f, t * 0.8f, -1.5708f - 0.5f, -1.5708f + 0.5f, c);
		line(cx, cy - h * 0.7f, cx, cy + h * 0.7f, t * 0.8f, c);
		break;
	case Icon::Terminal:
		roundRect(cx - h * 0.85f, cy - h * 0.7f, s * 0.85f, s * 0.7f, s * 0.08f, c);
		roundRect(cx - h * 0.85f + t, cy - h * 0.7f + t, s * 0.85f - 2 * t, s * 0.7f - 2 * t, s * 0.05f,
		          theme::bgDeep);
		line(cx - h * 0.5f, cy - h * 0.25f, cx - h * 0.15f, cy, t * 0.9f, c);
		line(cx - h * 0.5f, cy + h * 0.25f, cx - h * 0.15f, cy, t * 0.9f, c);
		line(cx, cy + h * 0.3f, cx + h * 0.45f, cy + h * 0.3f, t * 0.9f, c);
		break;
	case Icon::File:
		roundRect(cx - h * 0.6f, cy - h * 0.8f, s * 0.6f, s * 0.8f, s * 0.06f, c);
		roundRect(cx - h * 0.6f + t, cy - h * 0.8f + t, s * 0.6f - 2 * t, s * 0.8f - 2 * t, s * 0.04f, theme::bgDeep);
		line(cx - h * 0.3f, cy - h * 0.2f, cx + h * 0.3f, cy - h * 0.2f, t * 0.8f, c);
		line(cx - h * 0.3f, cy + h * 0.15f, cx + h * 0.3f, cy + h * 0.15f, t * 0.8f, c);
		break;
	case Icon::Pencil:
		line(cx - h * 0.55f, cy + h * 0.55f, cx + h * 0.45f, cy - h * 0.45f, t * 1.6f, c);
		triangle(cx - h * 0.75f, cy + h * 0.75f, cx - h * 0.68f, cy + h * 0.3f, cx - h * 0.3f, cy + h * 0.68f, c);
		break;
	case Icon::List:
		for (int i = -1; i <= 1; i++) {
			circle(cx - h * 0.6f, cy + i * h * 0.55f, t * 0.8f, c);
			roundRect(cx - h * 0.3f, cy + i * h * 0.55f - t / 2, s * 0.45f, t, t / 2, c);
		}
		break;
	case Icon::Question:
		arc(cx, cy - h * 0.2f, h * 0.35f, t, -3.4f, 0.6f, c);
		line(cx + h * 0.28f, cy + h * 0.0f, cx, cy + h * 0.25f, t, c);
		circle(cx, cy + h * 0.62f, t * 0.75f, c);
		break;
	case Icon::Agent:
		roundRect(cx - h * 0.65f, cy - h * 0.45f, s * 0.65f, s * 0.55f, s * 0.12f, c);
		circle(cx - h * 0.25f, cy - h * 0.18f, t * 0.8f, theme::bgDeep);
		circle(cx + h * 0.25f, cy - h * 0.18f, t * 0.8f, theme::bgDeep);
		line(cx, cy - h * 0.45f, cx, cy - h * 0.75f, t * 0.8f, c);
		circle(cx, cy - h * 0.8f, t * 0.8f, c);
		roundRect(cx - h * 0.4f, cy + h * 0.25f, s * 0.4f, t * 1.4f, t * 0.5f, c);
		break;
	case Icon::Clock:
		arc(cx, cy, h * 0.7f, t, 0, 6.2832f, c);
		line(cx, cy, cx, cy - h * 0.42f, t * 0.9f, c);
		line(cx, cy, cx + h * 0.32f, cy + h * 0.1f, t * 0.9f, c);
		break;
	case Icon::Plug:
		roundRect(cx - h * 0.45f, cy - h * 0.25f, s * 0.45f, s * 0.38f, s * 0.08f, c);
		line(cx - h * 0.22f, cy - h * 0.25f, cx - h * 0.22f, cy - h * 0.7f, t, c);
		line(cx + h * 0.22f, cy - h * 0.25f, cx + h * 0.22f, cy - h * 0.7f, t, c);
		line(cx, cy + h * 0.5f, cx, cy + h * 0.85f, t, c);
		break;
	case Icon::Sparkle:
		triangle(cx, cy - h * 0.85f, cx + h * 0.2f, cy, cx - h * 0.2f, cy, c);
		triangle(cx, cy + h * 0.85f, cx + h * 0.2f, cy, cx - h * 0.2f, cy, c);
		triangle(cx - h * 0.85f, cy, cx, cy - h * 0.2f, cx, cy + h * 0.2f, c);
		triangle(cx + h * 0.85f, cy, cx, cy - h * 0.2f, cx, cy + h * 0.2f, c);
		break;
	case Icon::Image:
		roundRect(cx - h * 0.8f, cy - h * 0.6f, s * 0.8f, s * 0.6f, s * 0.06f, c);
		roundRect(cx - h * 0.8f + t, cy - h * 0.6f + t, s * 0.8f - 2 * t, s * 0.6f - 2 * t, s * 0.04f, theme::bgDeep);
		triangle(cx - h * 0.55f, cy + h * 0.4f, cx - h * 0.1f, cy - h * 0.15f, cx + h * 0.3f, cy + h * 0.4f, c);
		circle(cx + h * 0.35f, cy - h * 0.2f, t, c);
		break;
	case Icon::Link:
		arc(cx - h * 0.25f, cy + h * 0.25f, h * 0.3f, t, 0.8f, 0.8f + 4.0f, c);
		arc(cx + h * 0.25f, cy - h * 0.25f, h * 0.3f, t, 3.9f, 3.9f + 4.0f, c);
		line(cx - h * 0.2f, cy + h * 0.2f, cx + h * 0.2f, cy - h * 0.2f, t, c);
		break;
	case Icon::Gear:
		for (int i = 0; i < 8; i++) {
			float a = i * 0.7854f;
			line(cx, cy, cx + std::cos(a) * h * 0.8f, cy + std::sin(a) * h * 0.8f, t * 1.6f, c);
		}
		circle(cx, cy, h * 0.55f, c);
		circle(cx, cy, h * 0.25f, theme::bgDeep);
		break;
	case Icon::Trash:
		roundRect(cx - h * 0.5f, cy - h * 0.35f, s * 0.5f, s * 0.55f, s * 0.05f, c);
		roundRect(cx - h * 0.65f, cy - h * 0.6f, s * 0.65f, t, t / 2, c);
		roundRect(cx - h * 0.2f, cy - h * 0.8f, s * 0.2f, t, t / 2, c);
		break;
	case Icon::Keyboard:
		roundRect(cx - h * 0.85f, cy - h * 0.5f, s * 0.85f, s * 0.5f, s * 0.07f, c);
		for (int r = 0; r < 2; r++) {
			for (int k = 0; k < 4; k++) {
				rect(cx - h * 0.6f + k * h * 0.38f, cy - h * 0.28f + r * h * 0.3f, h * 0.2f, h * 0.18f, theme::bgDeep);
			}
		}
		rect(cx - h * 0.45f, cy + h * 0.28f, h * 0.9f, h * 0.12f, theme::bgDeep);
		break;
	case Icon::Download:
		line(cx, cy - h * 0.75f, cx, cy + h * 0.3f, t, c);
		line(cx - h * 0.45f, cy - h * 0.1f, cx + 0.3f, cy + h * 0.35f, t, c);
		line(cx + h * 0.45f, cy - h * 0.1f, cx - 0.3f, cy + h * 0.35f, t, c);
		roundRect(cx - h * 0.7f, cy + h * 0.6f, s * 0.7f, t, t / 2, c);
		break;
	case Icon::Play:
		triangle(cx - h * 0.4f, cy - h * 0.6f, cx - h * 0.4f, cy + h * 0.6f, cx + h * 0.6f, cy, c);
		break;
	case Icon::Pause:
		roundRect(cx - h * 0.5f, cy - h * 0.55f, s * 0.18f, s * 0.55f, t * 0.4f, c);
		roundRect(cx + h * 0.14f, cy - h * 0.55f, s * 0.18f, s * 0.55f, t * 0.4f, c);
		break;
	case Icon::Info:
		arc(cx, cy, h * 0.7f, t, 0, 6.2832f, c);
		circle(cx, cy - h * 0.32f, t * 0.7f, c);
		roundRect(cx - t / 2, cy - h * 0.1f, t, h * 0.55f, t / 2, c);
		break;
	case Icon::Warning:
		triangle(cx, cy - h * 0.8f, cx + h * 0.85f, cy + h * 0.7f, cx - h * 0.85f, cy + h * 0.7f, c);
		roundRect(cx - t / 2, cy - h * 0.3f, t, h * 0.55f, t / 2, theme::bgDeep);
		circle(cx, cy + h * 0.45f, t * 0.6f, theme::bgDeep);
		break;
	case Icon::Brain:
		arc(cx - h * 0.25f, cy, h * 0.45f, t, 1.7f, 4.6f, c);
		arc(cx + h * 0.25f, cy, h * 0.45f, t, -1.4f, 1.4f, c);
		line(cx, cy - h * 0.45f, cx, cy + h * 0.45f, t * 0.8f, c);
		break;
	case Icon::Code:
		line(cx - h * 0.25f, cy - h * 0.5f, cx - h * 0.7f, cy, t, c);
		line(cx - h * 0.25f, cy + h * 0.5f, cx - h * 0.7f, cy, t, c);
		line(cx + h * 0.25f, cy - h * 0.5f, cx + h * 0.7f, cy, t, c);
		line(cx + h * 0.25f, cy + h * 0.5f, cx + h * 0.7f, cy, t, c);
		break;
	case Icon::Chat:
		roundRect(cx - h * 0.8f, cy - h * 0.6f, s * 0.8f, s * 0.55f, s * 0.15f, c);
		triangle(cx - h * 0.45f, cy + h * 0.45f, cx - h * 0.1f, cy + h * 0.45f, cx - h * 0.5f, cy + h * 0.85f, c);
		break;
	case Icon::Key:
		arc(cx - h * 0.35f, cy, h * 0.3f, t, 0, 6.2832f, c);
		line(cx - h * 0.05f, cy, cx + h * 0.75f, cy, t, c);
		line(cx + h * 0.45f, cy, cx + h * 0.45f, cy + h * 0.3f, t, c);
		line(cx + h * 0.7f, cy, cx + h * 0.7f, cy + h * 0.3f, t, c);
		break;
	case Icon::SdCard:
		roundRect(cx - h * 0.55f, cy - h * 0.75f, s * 0.55f, s * 0.75f, s * 0.06f, c);
		triangle(cx + h * 0.05f, cy - h * 0.76f, cx + h * 0.56f, cy - h * 0.76f, cx + h * 0.56f, cy - h * 0.3f,
		         theme::bgDeep);
		for (int i = 0; i < 3; i++) {
			rect(cx - h * 0.35f + i * h * 0.22f, cy - h * 0.6f, h * 0.1f, h * 0.25f, theme::bgDeep);
		}
		break;
	case Icon::Dot:
		circle(cx, cy, s * 0.18f, c);
		break;
	}
}

void claudeLogo(float cx, float cy, float r, Color c, float spin) {
	// Twelve tapered, slightly irregular rays with rounded tips.
	static const float lengths[12] = {1.0f, 0.86f, 0.95f, 0.8f, 0.98f, 0.84f, 0.93f, 0.82f, 1.0f, 0.88f, 0.9f, 0.83f};
	static const float jitter[12] = {0.0f, 0.05f, -0.03f, 0.04f, -0.02f, 0.03f, 0.0f, -0.04f, 0.02f, -0.03f, 0.04f, 0.0f};
	const float base = r * 0.16f;
	for (int i = 0; i < 12; i++) {
		float a = spin + i * (6.2832f / 12.0f) + jitter[i] - 1.5708f;
		float len = r * lengths[i];
		float ca = std::cos(a), sa = std::sin(a);
		float tipW = r * 0.085f;
		float px = -sa, py = ca;
		float bx = cx + ca * r * 0.12f, by = cy + sa * r * 0.12f;
		float tx = cx + ca * (len - tipW), ty = cy + sa * (len - tipW);
		triangle(bx + px * base, by + py * base, tx + px * tipW, ty + py * tipW, tx - px * tipW, ty - py * tipW, c);
		triangle(bx + px * base, by + py * base, tx - px * tipW, ty - py * tipW, bx - px * base, by - py * base, c);
		circle(tx, ty, tipW, c);
	}
	circle(cx, cy, r * 0.2f, c);
}

void spinner(float cx, float cy, float r, Color c) {
	float t = time();
	float breathe = 0.85f + 0.15f * std::sin(t * 4.0f);
	claudeLogo(cx, cy, r * breathe, c, t * 1.6f);
}

// ---- touch ---------------------------------------------------------------------

bool tappedIn(float x, float y, float w, float h) {
	if (current() != Screen::Bottom) {
		return false;
	}
	const input::State &in = input::get();
	if (in.tapped && in.tapX >= x && in.tapX < x + w && in.tapY >= y && in.tapY < y + h) {
		input::consumeTouch();
		return true;
	}
	return false;
}

bool pressedIn(float x, float y, float w, float h) {
	if (current() != Screen::Bottom) {
		return false;
	}
	const input::State &in = input::get();
	return in.touching && !in.dragging && in.touchX >= x && in.touchX < x + w && in.touchY >= y &&
	       in.touchY < y + h;
}

bool button(float x, float y, float w, float h, const std::string &label, Style style, bool focused, Icon ic,
            bool showIcon) {
	bool down = pressedIn(x, y, w, h);
	Color fill, fg;
	switch (style) {
	case Style::Primary:
		fill = down ? theme::accentDeep : theme::accent;
		fg = rgb(0xFFFFFF);
		break;
	case Style::Danger:
		fill = down ? rgb(0x8E2F2A) : rgb(0x5A2A27);
		fg = rgb(0xFFD9D6);
		break;
	case Style::Ghost:
		fill = down ? theme::panelHi : withAlpha(theme::panel, 0);
		fg = theme::textDim;
		break;
	default:
		fill = down ? theme::panelHi : theme::panel;
		fg = theme::text;
		break;
	}
	float r = std::min(h / 2, 10.0f);
	if (focused) {
		roundRect(x - 2, y - 2, w + 4, h + 4, r + 2, theme::accent);
		roundRect(x - 0.5f, y - 0.5f, w + 1, h + 1, r, theme::bg);
	}
	roundRect(x, y, w, h, r, fill);
	float scale = h >= 30 ? theme::label : theme::small;
	float tw = textWidth(label, scale);
	float iconW = showIcon ? h * 0.5f + 6 : 0;
	float total = std::min(w - 12, tw + iconW);
	float sx = x + (w - total) / 2;
	if (showIcon) {
		icon(ic, sx + h * 0.25f, y + h / 2, h * 0.5f, fg);
	}
	textFit(sx + iconW, y + (h - lineHeight(scale)) / 2, scale, fg, label, w - 12 - iconW);
	return tappedIn(x, y, w, h);
}

bool iconButton(float x, float y, float size, Icon ic, Color c, bool focused) {
	bool down = pressedIn(x, y, size, size);
	if (down || focused) {
		roundRect(x, y, size, size, size * 0.3f, focused ? theme::selection : theme::panelHi);
	}
	icon(ic, x + size / 2, y + size / 2, size * 0.55f, c);
	return tappedIn(x, y, size, size);
}

bool chip(float x, float y, float w, float h, const std::string &label, bool focused, bool chevron, Icon ic,
          bool showIcon) {
	bool down = pressedIn(x, y, w, h);
	roundRectBorder(x, y, w, h, h / 2, down ? theme::panelHi : theme::panel, focused ? theme::accent : theme::border);
	float tx = x + 8;
	if (showIcon) {
		icon(ic, x + 6 + h * 0.3f, y + h / 2, h * 0.55f, theme::textDim);
		tx += h * 0.6f + 2;
	}
	float right = chevron ? 14 : 6;
	textFit(tx, y + (h - lineHeight(theme::small)) / 2, theme::small, theme::text, label, x + w - right - tx);
	if (chevron) {
		icon(Icon::ChevronDown, x + w - 9, y + h / 2, 8, theme::textDim);
	}
	return tappedIn(x, y, w, h);
}

int segmented(float x, float y, float w, float h, const char *const *labels, int count, int selected) {
	roundRect(x, y, w, h, h / 2, theme::bgDeep);
	float segW = w / count;
	int hit = -1;
	for (int i = 0; i < count; i++) {
		float sx = x + i * segW;
		if (i == selected) {
			roundRect(sx + 2, y + 2, segW - 4, h - 4, (h - 4) / 2, theme::accent);
		} else if (pressedIn(sx, y, segW, h)) {
			roundRect(sx + 2, y + 2, segW - 4, h - 4, (h - 4) / 2, theme::panelHi);
		}
		float tw = textWidth(labels[i], theme::small);
		Color fg = i == selected ? rgb(0xFFFFFF) : theme::textDim;
		if (i == selected) {
			textBold(sx + (segW - tw) / 2, y + (h - lineHeight(theme::small)) / 2, theme::small, fg, labels[i]);
		} else {
			text(sx + (segW - tw) / 2, y + (h - lineHeight(theme::small)) / 2, theme::small, fg, labels[i]);
		}
		if (tappedIn(sx, y, segW, h)) {
			hit = i;
		}
	}
	return hit;
}

void progressBar(float x, float y, float w, float h, float value, Color fill, Color track) {
	roundRect(x, y, w, h, h / 2, track);
	value = std::max(0.0f, std::min(1.0f, value));
	if (value > 0) {
		roundRect(x, y, std::max(h, w * value), h, h / 2, fill);
	}
}

void scrollbar(float x, float y, float h, float viewH, float contentH, float offset) {
	if (contentH <= viewH + 1) {
		return;
	}
	float barH = std::max(12.0f, h * viewH / contentH);
	float maxOff = contentH - viewH;
	float by = y + (h - barH) * std::max(0.0f, std::min(1.0f, offset / maxOff));
	roundRect(x, by, 3, barH, 1.5f, withAlpha(theme::textFaint, 170));
}

namespace {
std::vector<std::string> wrapLines(float w, float scale, const std::string &text, int maxLines) {
	std::vector<std::string> lines;
	for (const std::string &para : util::split(text, '\n')) {
		std::string cur;
		float curW = 0;
		size_t i = 0;
		while (i < para.size()) {
			size_t start = i;
			// Next word including its trailing space.
			while (i < para.size() && para[i] != ' ') {
				util::utf8Next(para, i);
			}
			while (i < para.size() && para[i] == ' ') {
				i++;
			}
			std::string word = para.substr(start, i - start);
			float ww = textWidth(word, scale);
			if (curW + ww > w && !cur.empty()) {
				lines.push_back(cur);
				cur.clear();
				curW = 0;
			}
			while (ww > w && word.size() > 1) {
				// A word longer than the line: split it.
				std::string part = fitText(word, scale, w);
				part = part.substr(0, part.size() >= 3 ? part.size() - 3 : 0);
				if (part.empty()) {
					break;
				}
				lines.push_back(part);
				word = word.substr(part.size());
				ww = textWidth(word, scale);
			}
			cur += word;
			curW += ww;
		}
		lines.push_back(cur);
	}
	if (maxLines > 0 && (int)lines.size() > maxLines) {
		lines.resize(maxLines);
		lines.back() = fitText(lines.back() + "\xE2\x80\xA6", scale, w);
	}
	return lines;
}
} // namespace

float paragraph(float x, float y, float w, float scale, Color c, const std::string &s, int maxLines) {
	auto lines = wrapLines(w, scale, s, maxLines);
	float lh = lineHeight(scale);
	for (size_t i = 0; i < lines.size(); i++) {
		text(x, y + i * lh, scale, c, lines[i]);
	}
	return lines.size() * lh;
}

float paragraphHeight(float w, float scale, const std::string &s, int maxLines) {
	return wrapLines(w, scale, s, maxLines).size() * lineHeight(scale);
}

void buttonGlyph(float x, float y, char letter, Color c) {
	float r = 6.0f;
	circle(x + r, y + r, r, c);
	char s[2] = {letter, 0};
	float tw = textWidth(s, 0.34f);
	text(x + r - tw / 2, y + r - lineHeight(0.34f) / 2, 0.34f, theme::bgDeep, s);
}

void hints(const std::string &spec) {
	rect(0, 224, BOTTOM_W, 16, theme::bgDeep);
	float x = 6;
	size_t i = 0;
	while (i < spec.size()) {
		if (spec[i] == '{' && i + 2 < spec.size() && spec[i + 2] == '}') {
			buttonGlyph(x, 226, spec[i + 1], theme::textDim);
			x += 15;
			i += 3;
			continue;
		}
		size_t next = spec.find('{', i);
		std::string piece = spec.substr(i, next == std::string::npos ? std::string::npos : next - i);
		text(x, 227, 0.36f, theme::textDim, piece);
		x += textWidth(piece, 0.36f);
		i = next == std::string::npos ? spec.size() : next;
	}
}

void toast(const std::string &message) {
	g_toast = message;
	g_toastUntil = time() + 2.6f;
}

bool toastVisible() { return time() < g_toastUntil; }

void drawToast() {
	if (!toastVisible()) {
		return;
	}
	float w = std::min(300.0f, textWidth(g_toast, theme::small) + 24);
	float x = (BOTTOM_W - w) / 2, y = 170;
	roundRect(x, y, w, 26, 9, rgb(0x121110, 240));
	textFit(x + 12, y + (26 - lineHeight(theme::small)) / 2, theme::small, theme::text, g_toast, w - 24);
}

} // namespace ui
