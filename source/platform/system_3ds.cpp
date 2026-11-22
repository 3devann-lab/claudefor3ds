#ifdef __3DS__

#include "platform/system.h"

#include "core/log.h"

#include <3ds.h>
#include <mbedtls/sha256.h>

#include <cerrno>
#include <cstring>
#include <ctime>
#include <sys/stat.h>

namespace sys {

namespace {
int g_battery = -1;
bool g_charging = false;
bool g_wifi = false;
int g_bars = 0;
uint64_t g_lastPoll = 0;
bool g_mcu = false, g_ptm = false, g_ac = false, g_ps = false, g_cfg = false;
std::vector<uint8_t> g_secret;
s32 g_mainPriority = 0x30;

void poll() {
	if (g_mcu) {
		u8 level = 0;
		if (R_SUCCEEDED(MCUHWC_GetBatteryLevel(&level))) {
			g_battery = level > 100 ? 100 : level;
		}
	}
	if (g_ptm) {
		u8 charging = 0;
		if (R_SUCCEEDED(PTMU_GetBatteryChargeState(&charging))) {
			g_charging = charging != 0;
		}
		if (g_battery < 0) {
			u8 lvl = 0; // 0..5 when mcu::HWC is not available
			if (R_SUCCEEDED(PTMU_GetBatteryLevel(&lvl))) {
				g_battery = lvl * 20;
			}
		}
	}
	u32 wifi = 0;
	g_wifi = g_ac && R_SUCCEEDED(ACU_GetWifiStatus(&wifi)) && wifi != 0;
	g_bars = g_wifi ? osGetWifiStrength() : 0;
}
} // namespace

void init() {
	g_mcu = R_SUCCEEDED(mcuHwcInit());
	g_ptm = R_SUCCEEDED(ptmuInit());
	g_ac = R_SUCCEEDED(acInit());
	g_ps = R_SUCCEEDED(psInit());
	g_cfg = R_SUCCEEDED(cfguInit());
	svcGetThreadPriority(&g_mainPriority, CUR_THREAD_HANDLE);
	poll();
	g_lastPoll = monotonicMs();
}

void exit() {
	if (g_cfg) {
		cfguExit();
	}
	if (g_ps) {
		psExit();
	}
	if (g_ac) {
		acExit();
	}
	if (g_ptm) {
		ptmuExit();
	}
	if (g_mcu) {
		mcuHwcExit();
	}
}

void update() {
	uint64_t now = monotonicMs();
	if (now - g_lastPoll >= 2000) {
		g_lastPoll = now;
		poll();
	}
}

int batteryPercent() { return g_battery; }
bool isCharging() { return g_charging; }
bool isWifiConnected() { return g_wifi; }
int wifiBars() { return g_bars; }

int64_t wallClockSeconds() { return (int64_t)time(nullptr); }

uint64_t monotonicMs() { return svcGetSystemTick() / (uint64_t)CPU_TICKS_PER_MSEC; }

void sleepMs(int ms) { svcSleepThread((s64)ms * 1000000LL); }

void randomBytes(uint8_t *out, size_t len) {
	if (g_ps && R_SUCCEEDED(PS_GenerateRandomBytes(out, len))) {
		return;
	}
	// Fallback: tick-seeded xorshift, only used if ps:ps is unavailable.
	static uint64_t state = svcGetSystemTick() | 1;
	for (size_t i = 0; i < len; i++) {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		out[i] = (uint8_t)state;
	}
}

bool isNew3DS() {
	bool n3ds = false;
	APT_CheckNew3DS(&n3ds);
	return n3ds;
}

std::vector<uint8_t> consoleSecret() {
	if (!g_secret.empty()) {
		return g_secret;
	}
	u64 unique = 0, seed = 0;
	if (g_cfg) {
		CFGU_GenHashConsoleUnique(0xC1A3D, &unique);
	}
	if (g_ps) {
		PS_GetLocalFriendCodeSeed(&seed);
	}
	static const char salt[] = "ClaudeFor3DS/session-seal/v1";
	uint8_t digest[32];
	mbedtls_sha256_context ctx;
	mbedtls_sha256_init(&ctx);
	mbedtls_sha256_starts_ret(&ctx, 0);
	mbedtls_sha256_update_ret(&ctx, (const unsigned char *)salt, sizeof(salt) - 1);
	mbedtls_sha256_update_ret(&ctx, (const unsigned char *)&unique, sizeof(unique));
	mbedtls_sha256_update_ret(&ctx, (const unsigned char *)&seed, sizeof(seed));
	mbedtls_sha256_finish_ret(&ctx, digest);
	mbedtls_sha256_free(&ctx);
	g_secret.assign(digest, digest + 32);
	if (unique == 0 && seed == 0) {
		Log::write("[sys] console id unavailable, session is sealed with a fixed key");
	}
	return g_secret;
}

const std::string &dataDir() {
	static const std::string dir = "sdmc:/3ds/ClaudeFor3DS";
	return dir;
}

bool ensureDir(const std::string &path) {
	std::string cur;
	for (size_t i = 0; i < path.size(); i++) {
		cur += path[i];
		if ((path[i] == '/' && i > 0 && path[i - 1] != ':') || i + 1 == path.size()) {
			if (mkdir(cur.c_str(), 0777) != 0 && errno != EEXIST) {
				// Root components like "sdmc:/" fail harmlessly.
			}
		}
	}
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool keyboard(const KeyboardOptions &options, std::string &out) {
	SwkbdState kb;
	SwkbdType type = SWKBD_TYPE_NORMAL;
	if (options.type == KeyboardType::Code) {
		type = SWKBD_TYPE_NUMPAD;
	} else if (options.type == KeyboardType::Email || options.type == KeyboardType::Url) {
		type = SWKBD_TYPE_WESTERN;
	}
	int max = options.maxLength > 0 ? options.maxLength : 2000;
	swkbdInit(&kb, type, 2, max);
	swkbdSetHintText(&kb, options.hint.c_str());
	if (!options.initial.empty()) {
		swkbdSetInitialText(&kb, options.initial.c_str());
	}
	u32 features = 0;
	if (options.multiline) {
		features |= SWKBD_MULTILINE;
	}
	if (type == SWKBD_TYPE_NORMAL) {
		features |= SWKBD_PREDICTIVE_INPUT;
	}
	if (options.type == KeyboardType::Password) {
		swkbdSetPasswordMode(&kb, SWKBD_PASSWORD_HIDE_DELAY);
	}
	swkbdSetFeatures(&kb, features);
	swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	swkbdSetButton(&kb, SWKBD_BUTTON_LEFT, "Cancel", false);
	swkbdSetButton(&kb, SWKBD_BUTTON_RIGHT, options.okLabel.c_str(), true);

	std::vector<char> buf((size_t)max * 4 + 8, 0);
	SwkbdButton button = swkbdInputText(&kb, buf.data(), buf.size());
	if (button != SWKBD_BUTTON_RIGHT) {
		return false;
	}
	out = buf.data();
	return true;
}

namespace {
struct ThreadBox {
	std::function<void()> fn;
};
void threadEntry(void *arg) {
	ThreadBox *box = static_cast<ThreadBox *>(arg);
	box->fn();
	delete box;
}
} // namespace

void startThread(std::function<void()> fn, bool big) {
	ThreadBox *box = new ThreadBox{std::move(fn)};
	// Just below the UI thread, so drawing always wins while networking waits on sockets.
	s32 prio = g_mainPriority + 1;
	if (prio > 0x3F) {
		prio = 0x3F;
	}
	Thread t = threadCreate(threadEntry, box, big ? 96 * 1024 : 32 * 1024, prio, -2, true);
	if (!t) {
		Log::write("[sys] threadCreate failed");
		delete box;
	}
}

void setSleepAllowed(bool allowed) { aptSetSleepAllowed(allowed); }

} // namespace sys

#endif
