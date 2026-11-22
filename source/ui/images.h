// Images shown in the conversation: fetched and decoded off the main thread, uploaded as
// textures on it, scaled down to fit the console's memory, and evicted when unused.
#pragma once

#include "claude/types.h"
#include "platform/gfx.h"

#include <string>

namespace images {

enum class State { Loading, Ready, NotImage, Failed };

struct Entry {
	State state = State::Loading;
	gfx::Image *image = nullptr;
	int width = 0, height = 0; // original size
	std::string mime;
	std::string name;
	std::string error;
	uint64_t lastUsed = 0;
};

// Starts loading on first use. Never null.
const Entry &get(const claude::Block &block);
// Call once per frame on the main thread: uploads finished decodes, evicts old textures.
void update();
void clear();

// Decodes PNG/JPEG/GIF/BMP bytes to RGBA, scaled so neither side exceeds maxSide.
bool decode(const std::string &bytes, int maxSide, std::vector<uint8_t> &rgba, int &w, int &h, int &origW,
            int &origH);

// A QR code for text, as a texture (cached by text).
gfx::Image *qrCode(const std::string &text);

} // namespace images
