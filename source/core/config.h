// Non-secret settings, saved as JSON in sdmc:/3ds/ClaudeFor3DS/config.json.
#pragma once

#include <string>
#include <vector>

#ifndef APP_VERSION_MAJOR
#define APP_VERSION_MAJOR 1
#define APP_VERSION_MINOR 0
#define APP_VERSION_MICRO 0
#endif
#define APP_STRINGIFY_(x) #x
#define APP_STRINGIFY(x) APP_STRINGIFY_(x)
#define APP_VERSION_STR APP_STRINGIFY(APP_VERSION_MAJOR) "." APP_STRINGIFY(APP_VERSION_MINOR) "." APP_STRINGIFY(APP_VERSION_MICRO)

enum class AppMode { Chat = 0, Code = 1 };
enum class Effort { Low = 0, Medium = 1, High = 2, Max = 3 };
enum class BackendKind { None = 0, ApiKey = 1 };

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
	// Let Claude search and fetch web pages.
	bool webSearch = true;
	std::string apiBase = "https://api.anthropic.com";
	// Extra instructions added to the system prompt.
	std::string customInstructions;
	// Remote MCP servers ("connectors") passed to the API, from config.json.
	struct Connector {
		std::string name, url, token;
	};
	std::vector<Connector> connectors;

	void load();
	void save() const;
};

Config &config();
