// Colours and type sizes. Warm dark palette in the spirit of Claude's own apps, laid out the
// TriCord way (content on the top screen, controls on the bottom one).
#pragma once

#include "platform/gfx.h"

namespace theme {

using gfx::Color;
using gfx::rgb;

constexpr Color bg = rgb(0x262624);
constexpr Color bgDeep = rgb(0x1B1A19);
constexpr Color panel = rgb(0x302F2C);
constexpr Color panelHi = rgb(0x3C3A36);
constexpr Color border = rgb(0x4A4843);
constexpr Color text = rgb(0xF4F2EC);
constexpr Color textDim = rgb(0xB9B5AB);
constexpr Color textFaint = rgb(0x878379);
constexpr Color accent = rgb(0xD97757);
constexpr Color accentDeep = rgb(0xB9583B);
constexpr Color logoRed = rgb(0xD9472B);
constexpr Color red = rgb(0xE5534B);
constexpr Color green = rgb(0x72C077);
constexpr Color yellow = rgb(0xE6B450);
constexpr Color blue = rgb(0x82AEF2);
constexpr Color codeBg = rgb(0x1C1B1A);
constexpr Color codeText = rgb(0xE6E3DA);
constexpr Color inlineCode = rgb(0xEFA88B);
constexpr Color link = rgb(0x8DB6F7);
constexpr Color userBubble = rgb(0x3D3B37);
constexpr Color selection = rgb(0x4A3A32);
constexpr Color overlay = rgb(0x000000, 150);

// Type scales for the system font (line height = 30px x scale).
constexpr float tiny = 0.34f;
constexpr float small = 0.40f;
constexpr float label = 0.46f;
constexpr float title = 0.60f;
constexpr float hero = 0.78f;
// Body text in the conversation follows the text size setting.
float body();
float code();

} // namespace theme
