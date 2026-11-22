// Immediate-mode widgets: each call draws and, on the bottom screen, reports taps.
#pragma once

#include "platform/gfx.h"
#include "ui/theme.h"

#include <string>

namespace ui {

enum class Icon {
	Menu,
	Plus,
	Send,
	Stop,
	Close,
	Check,
	ChevronDown,
	ChevronRight,
	ChevronLeft,
	ChevronUp,
	Search,
	Globe,
	Terminal,
	File,
	Pencil,
	List,
	Question,
	Agent,
	Clock,
	Plug,
	Sparkle,
	Image,
	Link,
	Gear,
	Trash,
	Keyboard,
	Download,
	Play,
	Pause,
	Info,
	Warning,
	Brain,
	Code,
	Chat,
	Key,
	SdCard,
	Dot,
};

void icon(Icon which, float cx, float cy, float size, gfx::Color c);

// The Claude spark: rays around a centre. `spin` is a rotation in radians.
void claudeLogo(float cx, float cy, float radius, gfx::Color c, float spin = 0.0f);
// Animated "working" spark (rotates and breathes).
void spinner(float cx, float cy, float radius, gfx::Color c);

// ---- touch (bottom screen) -------------------------------------------------
bool tappedIn(float x, float y, float w, float h);
bool pressedIn(float x, float y, float w, float h);

enum class Style { Primary, Secondary, Ghost, Danger };
// A button; true when tapped this frame (or `activate` is true, for keyboard focus).
bool button(float x, float y, float w, float h, const std::string &label, Style style = Style::Secondary,
            bool focused = false, Icon ic = Icon::Dot, bool showIcon = false);
bool iconButton(float x, float y, float size, Icon ic, gfx::Color c, bool focused = false);
// Small rounded pill with optional chevron; true when tapped.
bool chip(float x, float y, float w, float h, const std::string &label, bool focused = false, bool chevron = true,
          Icon ic = Icon::Dot, bool showIcon = false);
// Two- or more-way segmented control; returns the tapped index or -1.
int segmented(float x, float y, float w, float h, const char *const *labels, int count, int selected);

void progressBar(float x, float y, float w, float h, float value, gfx::Color fill, gfx::Color track);
void scrollbar(float x, float y, float h, float viewH, float contentH, float offset);

// Wraps text into at most maxLines lines of width w; returns the height used.
float paragraph(float x, float y, float w, float scale, gfx::Color c, const std::string &text, int maxLines = 0);
float paragraphHeight(float w, float scale, const std::string &text, int maxLines = 0);

// Bottom-of-screen hint bar: "(A) Send  (B) Stop".
void hints(const std::string &text);
void buttonGlyph(float x, float y, char letter, gfx::Color c);

void drawToast();
void toast(const std::string &message);
bool toastVisible();

// Seconds since start, for animation.
float time();
void tick();

} // namespace ui
