// Console services: battery, Wi-Fi, clocks, randomness, keyboard, threads.
// Implemented by system_3ds.cpp on hardware and host/system_host.cpp in the simulator.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sys {

void init();
void exit();
// Main thread, once per frame. Polls slow sources (battery, Wi-Fi) every couple of seconds.
void update();

// -1 while unknown.
int batteryPercent();
bool isCharging();
bool isWifiConnected();
// 0..3
int wifiBars();

// Local wall-clock seconds (the 3DS RTC has no time zone).
int64_t wallClockSeconds();
uint64_t monotonicMs();
void sleepMs(int ms);

void randomBytes(uint8_t *out, size_t len);
bool isNew3DS();

// 32 bytes unique to this console (and this app), for sealing secrets on the SD card.
std::vector<uint8_t> consoleSecret();

// Where settings, the session and conversations live: sdmc:/3ds/ClaudeFor3DS
const std::string &dataDir();
bool ensureDir(const std::string &path);

enum class KeyboardType { Text, Email, Code, Password, Url };
struct KeyboardOptions {
	std::string hint;
	std::string initial;
	int maxLength = 2000;
	KeyboardType type = KeyboardType::Text;
	bool multiline = false;
	std::string okLabel = "Send";
};
// Shows the system keyboard; false when cancelled. Blocks the main thread while open,
// background threads (networking) keep running.
bool keyboard(const KeyboardOptions &options, std::string &out);

// Runs fn on a background thread. `big` gives it a larger stack (TLS, JSON).
void startThread(std::function<void()> fn, bool big = true);

// Keeps the console awake with the lid shut (used while Claude is responding).
void setSleepAllowed(bool allowed);

} // namespace sys
