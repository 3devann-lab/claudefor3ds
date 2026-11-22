#include "ui/images.h"

#include "app/app.h"
#include "core/log.h"
#include "core/main_queue.h"
#include "platform/system.h"
#include "util/util.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

extern "C" {
#include "qrcodegen.h"
}

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <memory>

namespace images {

namespace {
constexpr int MAX_SIDE = 512;
constexpr size_t MAX_TEXTURES = 10;
std::map<std::string, Entry> g_entries;
std::map<std::string, gfx::Image *> g_qr;

std::string keyFor(const claude::Block &b) {
	if (!b.imageKey.empty()) {
		return b.imageKey;
	}
	if (!b.url.empty()) {
		return b.url;
	}
	if (!b.text.empty()) {
		// Inline base64: key by a cheap hash of the data.
		size_t h = std::hash<std::string>()(b.text);
		return "b64:" + std::to_string(h) + ":" + std::to_string(b.text.size());
	}
	return "";
}

void decodeAsync(const std::string &key, std::string bytes, std::string mime, std::string name) {
	sys::startThread(
	    [key, bytes = std::move(bytes), mime, name]() {
		    auto rgba = std::make_shared<std::vector<uint8_t>>();
		    int w = 0, h = 0, ow = 0, oh = 0;
		    bool ok = decode(bytes, MAX_SIDE, *rgba, w, h, ow, oh);
		    MainQueue::post([key, rgba, w, h, ow, oh, ok, mime, name]() {
			    auto it = g_entries.find(key);
			    if (it == g_entries.end()) {
				    return;
			    }
			    Entry &e = it->second;
			    e.mime = mime;
			    e.name = name;
			    if (!ok) {
				    e.state = State::Failed;
				    e.error = "Couldn't decode this image";
				    return;
			    }
			    e.image = gfx::createImage(rgba->data(), w, h);
			    e.width = ow;
			    e.height = oh;
			    e.state = e.image ? State::Ready : State::Failed;
			    if (!e.image) {
				    e.error = "Not enough memory for this image";
			    }
		    });
	    },
	    true);
}
} // namespace

bool decode(const std::string &bytes, int maxSide, std::vector<uint8_t> &rgba, int &w, int &h, int &origW,
            int &origH) {
	int n = 0;
	unsigned char *px =
	    stbi_load_from_memory((const unsigned char *)bytes.data(), (int)bytes.size(), &origW, &origH, &n, 4);
	if (!px) {
		return false;
	}
	w = origW;
	h = origH;
	float scale = 1.0f;
	if (w > maxSide || h > maxSide) {
		scale = (float)maxSide / std::max(w, h);
		w = std::max(1, (int)(origW * scale));
		h = std::max(1, (int)(origH * scale));
	}
	rgba.assign((size_t)w * h * 4, 0);
	if (scale == 1.0f) {
		memcpy(rgba.data(), px, rgba.size());
	} else {
		// Box filter: average the source pixels each destination pixel covers.
		for (int y = 0; y < h; y++) {
			int sy0 = (int)(y / scale), sy1 = std::max(sy0 + 1, std::min(origH, (int)((y + 1) / scale)));
			for (int x = 0; x < w; x++) {
				int sx0 = (int)(x / scale), sx1 = std::max(sx0 + 1, std::min(origW, (int)((x + 1) / scale)));
				uint32_t acc[4] = {0, 0, 0, 0};
				int count = 0;
				for (int sy = sy0; sy < sy1; sy++) {
					for (int sx = sx0; sx < sx1; sx++) {
						const unsigned char *s = px + ((size_t)sy * origW + sx) * 4;
						for (int k = 0; k < 4; k++) {
							acc[k] += s[k];
						}
						count++;
					}
				}
				uint8_t *d = &rgba[((size_t)y * w + x) * 4];
				for (int k = 0; k < 4; k++) {
					d[k] = (uint8_t)(acc[k] / std::max(1, count));
				}
			}
		}
	}
	stbi_image_free(px);
	return true;
}

const Entry &get(const claude::Block &b) {
	static Entry missing;
	std::string key = keyFor(b);
	if (key.empty()) {
		missing.state = State::Failed;
		missing.error = "No image data";
		return missing;
	}
	auto it = g_entries.find(key);
	if (it != g_entries.end()) {
		it->second.lastUsed = util::nowMs();
		return it->second;
	}
	Entry &e = g_entries[key];
	e.lastUsed = util::nowMs();
	e.name = b.name;
	if (!b.text.empty() && b.imageKey.empty() && b.url.empty()) {
		std::vector<uint8_t> raw = util::base64Decode(b.text);
		decodeAsync(key, std::string(raw.begin(), raw.end()), b.mime, b.name);
		return e;
	}
	claude::Backend *backend = app::App::get().backend();
	if (!backend) {
		e.state = State::Failed;
		e.error = "Not signed in";
		return e;
	}
	backend->fetchMedia(key, false, [key](bool ok, std::string bytes, std::string mime, std::string name) {
		auto found = g_entries.find(key);
		if (found == g_entries.end()) {
			return;
		}
		Entry &entry = found->second;
		entry.mime = mime;
		if (!name.empty()) {
			entry.name = name;
		}
		if (!ok) {
			entry.state = State::Failed;
			entry.error = name.empty() ? "Download failed" : name;
			return;
		}
		if (bytes.empty()) {
			entry.state = State::NotImage;
			return;
		}
		decodeAsync(key, std::move(bytes), mime, entry.name);
	});
	return e;
}

void update() {
	size_t live = 0;
	for (auto &kv : g_entries) {
		live += kv.second.image ? 1 : 0;
	}
	while (live > MAX_TEXTURES) {
		auto oldest = g_entries.end();
		for (auto it = g_entries.begin(); it != g_entries.end(); ++it) {
			if (it->second.image && (oldest == g_entries.end() || it->second.lastUsed < oldest->second.lastUsed)) {
				oldest = it;
			}
		}
		if (oldest == g_entries.end()) {
			break;
		}
		gfx::destroyImage(oldest->second.image);
		g_entries.erase(oldest); // reloaded on demand
		live--;
	}
}

void clear() {
	for (auto &kv : g_entries) {
		gfx::destroyImage(kv.second.image);
	}
	g_entries.clear();
	for (auto &kv : g_qr) {
		gfx::destroyImage(kv.second);
	}
	g_qr.clear();
}

gfx::Image *qrCode(const std::string &text) {
	auto it = g_qr.find(text);
	if (it != g_qr.end()) {
		return it->second;
	}
	std::vector<uint8_t> qr(qrcodegen_BUFFER_LEN_MAX), tmp(qrcodegen_BUFFER_LEN_MAX);
	if (!qrcodegen_encodeText(text.c_str(), tmp.data(), qr.data(), qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN,
	                          qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
		return nullptr;
	}
	int size = qrcodegen_getSize(qr.data());
	const int border = 2;
	int dim = size + border * 2;
	std::vector<uint8_t> rgba((size_t)dim * dim * 4, 255);
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			if (qrcodegen_getModule(qr.data(), x, y)) {
				uint8_t *p = &rgba[((size_t)(y + border) * dim + (x + border)) * 4];
				p[0] = p[1] = p[2] = 0;
			}
		}
	}
	gfx::Image *img = gfx::createImage(rgba.data(), dim, dim);
	if (g_qr.size() > 8) {
		for (auto &kv : g_qr) {
			gfx::destroyImage(kv.second);
		}
		g_qr.clear();
	}
	g_qr[text] = img;
	return img;
}

} // namespace images
