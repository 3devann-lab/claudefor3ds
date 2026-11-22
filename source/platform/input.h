// One snapshot of buttons, touch and sticks per frame. Bit values match libctru's KEY_*.
#pragma once

#include <cstdint>

namespace input {

enum Key : uint32_t {
	A = 1u << 0,
	B = 1u << 1,
	SELECT = 1u << 2,
	START = 1u << 3,
	DRIGHT = 1u << 4,
	DLEFT = 1u << 5,
	DUP = 1u << 6,
	DDOWN = 1u << 7,
	R = 1u << 8,
	L = 1u << 9,
	X = 1u << 10,
	Y = 1u << 11,
	ZL = 1u << 14,
	ZR = 1u << 15,
	TOUCH = 1u << 20,
	CSTICK_RIGHT = 1u << 24,
	CSTICK_LEFT = 1u << 25,
	CSTICK_UP = 1u << 26,
	CSTICK_DOWN = 1u << 27,
	CPAD_RIGHT = 1u << 28,
	CPAD_LEFT = 1u << 29,
	CPAD_UP = 1u << 30,
	CPAD_DOWN = 1u << 31,
	UP = DUP | CPAD_UP,
	DOWN = DDOWN | CPAD_DOWN,
	LEFT = DLEFT | CPAD_LEFT,
	RIGHT = DRIGHT | CPAD_RIGHT,
};

struct State {
	uint32_t down = 0;
	uint32_t held = 0;
	uint32_t up = 0;
	// Repeats while held, like a keyboard (for list navigation).
	uint32_t repeat = 0;
	bool touching = false;
	int touchX = 0, touchY = 0;
	// Touch that just started / ended this frame, and where.
	bool touchStarted = false, touchEnded = false;
	int touchStartX = 0, touchStartY = 0;
	// Finger lifted without moving far: a tap at (tapX, tapY).
	bool tapped = false;
	int tapX = 0, tapY = 0;
	// Movement since last frame while dragging.
	int dragDX = 0, dragDY = 0;
	bool dragging = false;
	int circleX = 0, circleY = 0;
	int cstickX = 0, cstickY = 0;
};

// Fills the snapshot from the hardware (or the simulator's script).
void scan();
const State &get();
// Clears this frame's presses so one widget consuming them hides them from the rest.
void consume(uint32_t keys);
void consumeTouch();

// Used by the simulator to inject input.
void setRaw(uint32_t held, bool touching, int tx, int ty, int cx, int cy);

} // namespace input
