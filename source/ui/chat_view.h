// The conversation on the top screen.
#pragma once

#include "claude/types.h"

namespace chat {

constexpr float VIEW_TOP = 19.0f;
constexpr float VIEW_BOTTOM = 240.0f;

// Buttons / sticks for scrolling and selecting (call when no overlay has focus).
void handleInput();
void draw();

int selected();
void select(int messageIndex);
void clearSelection();
const claude::Message *selectedMessage();

void scrollBy(float dy);
void page(int direction);
void toEnd();
// 0 = top, 1 = bottom.
float position();
void setPosition(float fraction);
bool canScroll();

} // namespace chat
