// Simulator stand-ins for console services. Deterministic so screenshots are stable.
#include "platform/system.h"

#include <chrono>
#include <cstdlib>
#include <deque>
#include <random>
#include <sys/stat.h>
#include <thread>

namespace sys {

namespace {
std::deque<std::string> g_keyboardScript;
int64_t g_fakeClock = 0;
} // namespace

// Simulator hooks.
void hostQueueKeyboard(const std::string &text) { g_keyboardScript.push_back(text); }
void hostSetClock(int64_t localSeconds) { g_fakeClock = localSeconds; }

void init() {}
void exit() {}
void update() {}
int batteryPercent() { return 76; }
bool isCharging() { return false; }
bool isWifiConnected() { return true; }
int wifiBars() { return 3; }

int64_t wallClockSeconds() {
	if (g_fakeClock) {
		return g_fakeClock;
	}
	return (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
	           std::chrono::system_clock::now().time_since_epoch())
	    .count();
}

uint64_t monotonicMs() {
	static auto start = std::chrono::steady_clock::now();
	return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
	    .count();
}

void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

void randomBytes(uint8_t *out, size_t len) {
	static std::mt19937 rng(1234);
	for (size_t i = 0; i < len; i++) {
		out[i] = (uint8_t)rng();
	}
}

bool isNew3DS() { return true; }

std::vector<uint8_t> consoleSecret() { return std::vector<uint8_t>(32, 0x5A); }

const std::string &dataDir() {
	static const std::string dir = "host/build/data";
	return dir;
}

bool ensureDir(const std::string &path) {
	std::string cur;
	for (char c : path + "/") {
		if (c == '/' && !cur.empty()) {
			mkdir(cur.c_str(), 0777);
		}
		cur += c;
	}
	return true;
}

bool keyboard(const KeyboardOptions &, std::string &out) {
	if (g_keyboardScript.empty()) {
		return false;
	}
	out = g_keyboardScript.front();
	g_keyboardScript.pop_front();
	return true;
}

void startThread(std::function<void()> fn, bool) { std::thread(std::move(fn)).detach(); }

void setSleepAllowed(bool) {}

} // namespace sys
