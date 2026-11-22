#include "platform/input.h"

#include <cstdlib>

#ifdef __3DS__
#include <3ds.h>
#endif

namespace input {

namespace {
State g_state;
uint32_t g_prevHeld = 0;
bool g_prevTouching = false;
int g_prevTX = 0, g_prevTY = 0;
int g_repeatTimer[32] = {};
bool g_dragActive = false;

uint32_t g_rawHeld = 0;
bool g_rawTouching = false;
int g_rawTX = 0, g_rawTY = 0, g_rawCX = 0, g_rawCY = 0;

constexpr int REPEAT_DELAY = 18;
constexpr int REPEAT_RATE = 5;
constexpr int DRAG_THRESHOLD = 6;
} // namespace

void setRaw(uint32_t held, bool touching, int tx, int ty, int cx, int cy) {
	g_rawHeld = held;
	g_rawTouching = touching;
	g_rawTX = tx;
	g_rawTY = ty;
	g_rawCX = cx;
	g_rawCY = cy;
}

void scan() {
	uint32_t held;
	bool touching;
	int tx, ty, cx, cy, sx = 0, sy = 0;
#ifdef __3DS__
	hidScanInput();
	held = hidKeysHeld();
	touchPosition tp;
	hidTouchRead(&tp);
	touching = (held & KEY_TOUCH) != 0;
	tx = tp.px;
	ty = tp.py;
	circlePosition cp;
	hidCircleRead(&cp);
	cx = cp.dx;
	cy = cp.dy;
	circlePosition cs;
	hidCstickRead(&cs);
	sx = cs.dx;
	sy = cs.dy;
#else
	held = g_rawHeld | (g_rawTouching ? TOUCH : 0);
	touching = g_rawTouching;
	tx = g_rawTX;
	ty = g_rawTY;
	cx = g_rawCX;
	cy = g_rawCY;
#endif
	State s;
	s.held = held;
	s.down = held & ~g_prevHeld;
	s.up = g_prevHeld & ~held;
	s.repeat = s.down;
	for (int i = 0; i < 32; i++) {
		uint32_t bit = 1u << i;
		if (held & bit) {
			if (s.down & bit) {
				g_repeatTimer[i] = REPEAT_DELAY;
			} else if (--g_repeatTimer[i] <= 0) {
				g_repeatTimer[i] = REPEAT_RATE;
				s.repeat |= bit;
			}
		}
	}
	s.touching = touching;
	s.touchX = touching ? tx : g_prevTX;
	s.touchY = touching ? ty : g_prevTY;
	s.touchStarted = touching && !g_prevTouching;
	s.touchEnded = !touching && g_prevTouching;
	if (s.touchStarted) {
		g_state.touchStartX = tx;
		g_state.touchStartY = ty;
		g_dragActive = false;
	}
	s.touchStartX = g_state.touchStartX;
	s.touchStartY = g_state.touchStartY;
	if (touching && !s.touchStarted) {
		if (!g_dragActive &&
		    (std::abs(tx - s.touchStartX) > DRAG_THRESHOLD || std::abs(ty - s.touchStartY) > DRAG_THRESHOLD)) {
			g_dragActive = true;
		}
		if (g_dragActive) {
			s.dragDX = tx - g_prevTX;
			s.dragDY = ty - g_prevTY;
		}
	}
	s.dragging = touching && g_dragActive;
	if (s.touchEnded && !g_dragActive) {
		s.tapped = true;
		s.tapX = g_prevTX;
		s.tapY = g_prevTY;
	}
	s.circleX = cx;
	s.circleY = cy;
	s.cstickX = sx;
	s.cstickY = sy;

	g_prevHeld = held;
	g_prevTouching = touching;
	if (touching) {
		g_prevTX = tx;
		g_prevTY = ty;
	}
	g_state = s;
}

const State &get() { return g_state; }

void consume(uint32_t keys) {
	g_state.down &= ~keys;
	g_state.repeat &= ~keys;
}

void consumeTouch() {
	g_state.tapped = false;
	g_state.touchStarted = false;
	g_state.dragDX = 0;
	g_state.dragDY = 0;
}

} // namespace input
