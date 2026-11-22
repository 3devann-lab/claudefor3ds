// Non-secret settings, saved as JSON in sdmc:/3ds/ClaudeFor3DS/config.json.
#pragma once

#include <string>
#include <vector>

enum class AppMode { Chat = 0, Code = 1 };
enum class Effort { Low = 0, Medium = 1, High = 2, Max = 3 };
enum class BackendKind { None = 0 };

const char *effortName(Effort e);
const char *effortApiValue(Effort e);
const char *modeName(AppMode m);

struct Config {
	BackendKind backend = BackendKind::None;
	std::string accountEmail;
	std::string accountName;
	std::string orgId;
	std::string orgName;
	std::string plan;

	std::string model;
	std::string modelName;
	Effort effort = Effort::High;
	AppMode mode = AppMode::Chat;
	std::string lastChatId;
	std::string lastCodeId;

	// 0 = small, 1 = normal, 2 = large
	int textSize = 1;
	bool showThinking = true;
	bool autoScroll = true;
	bool keepAwake = true;
	// Minutes east of UTC; learned from the network unless set by hand.
	int utcOffset = 0;
	bool utcOffsetManual = false;
	std::string timezone = "UTC";
	// Let Claude search the web in chats.
	bool webSearch = true;

	void load();
	void save() const;
};

Config &config();
