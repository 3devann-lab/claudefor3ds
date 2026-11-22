#include "core/config.h"

#include "core/log.h"
#include "platform/system.h"
#include "util/json.h"
#include "util/util.h"

#include <cstdio>

const char *effortName(Effort e) {
	switch (e) {
	case Effort::Low:
		return "Low";
	case Effort::Medium:
		return "Medium";
	case Effort::High:
		return "High";
	case Effort::Max:
		return "Max";
	}
	return "High";
}

const char *effortApiValue(Effort e) {
	switch (e) {
	case Effort::Low:
		return "low";
	case Effort::Medium:
		return "medium";
	case Effort::High:
		return "high";
	case Effort::Max:
		return "max";
	}
	return "high";
}

const char *modeName(AppMode m) { return m == AppMode::Code ? "Claude Code" : "Chat"; }

Config &config() {
	static Config instance;
	return instance;
}

static std::string readFile(const std::string &path) {
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) {
		return "";
	}
	std::string out;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
		out.append(buf, n);
	}
	fclose(f);
	return out;
}

void Config::load() {
	std::string text = readFile(sys::dataDir() + "/config.json");
	if (text.empty()) {
		return;
	}
	json::Document doc;
	if (!json::parse(doc, text) || !doc.IsObject()) {
		Log::write("[config] config.json is not valid JSON, using defaults");
		return;
	}
	backend = (BackendKind)json::i64(doc, "backend", 0);
	accountEmail = json::str(doc, "account_email");
	accountName = json::str(doc, "account_name");
	orgId = json::str(doc, "org_id");
	orgName = json::str(doc, "org_name");
	plan = json::str(doc, "plan");
	model = json::str(doc, "model");
	modelName = json::str(doc, "model_name");
	effort = (Effort)(json::i64(doc, "effort", 2) & 3);
	mode = json::i64(doc, "mode", 0) == 1 ? AppMode::Code : AppMode::Chat;
	lastChatId = json::str(doc, "last_chat");
	lastCodeId = json::str(doc, "last_code");
	textSize = (int)json::i64(doc, "text_size", 1);
	if (textSize < 0 || textSize > 2) {
		textSize = 1;
	}
	showThinking = json::boolean(doc, "show_thinking", true);
	autoScroll = json::boolean(doc, "auto_scroll", true);
	keepAwake = json::boolean(doc, "keep_awake", true);
	utcOffsetManual = json::boolean(doc, "utc_offset_manual", false);
	utcOffset = (int)json::i64(doc, "utc_offset", 0);
	timezone = json::str(doc, "timezone", "UTC");
	webSearch = json::boolean(doc, "web_search", true);
	apiBase = json::str(doc, "api_base", "https://api.anthropic.com");
	customInstructions = json::str(doc, "custom_instructions");
	connectors.clear();
	if (const json::Value *list = json::arr(doc, "connectors")) {
		for (const auto &c : list->GetArray()) {
			Connector con{json::str(c, "name"), json::str(c, "url"), json::str(c, "authorization_token")};
			if (!con.name.empty() && !con.url.empty()) {
				connectors.push_back(con);
			}
		}
	}
	if (utcOffsetManual) {
		util::setUtcOffsetMinutes(utcOffset);
	}
}

void Config::save() const {
	json::Writer w;
	w.beginObject();
	w.kv("backend", (int)backend);
	w.kv("account_email", accountEmail);
	w.kv("account_name", accountName);
	w.kv("org_id", orgId);
	w.kv("org_name", orgName);
	w.kv("plan", plan);
	w.kv("model", model);
	w.kv("model_name", modelName);
	w.kv("effort", (int)effort);
	w.kv("mode", (int)mode);
	w.kv("last_chat", lastChatId);
	w.kv("last_code", lastCodeId);
	w.kv("text_size", textSize);
	w.kv("show_thinking", showThinking);
	w.kv("auto_scroll", autoScroll);
	w.kv("keep_awake", keepAwake);
	w.kv("utc_offset_manual", utcOffsetManual);
	w.kv("utc_offset", utcOffsetManual ? utcOffset : util::utcOffsetMinutes());
	w.kv("timezone", timezone);
	w.kv("web_search", webSearch);
	w.kv("api_base", apiBase);
	w.kv("custom_instructions", customInstructions);
	w.key("connectors").beginArray();
	for (const auto &c : connectors) {
		w.beginObject().kv("name", c.name).kv("url", c.url).kv("authorization_token", c.token).endObject();
	}
	w.endArray();
	w.endObject();

	sys::ensureDir(sys::dataDir());
	std::string path = sys::dataDir() + "/config.json";
	std::string tmp = path + ".tmp";
	FILE *f = fopen(tmp.c_str(), "wb");
	if (!f) {
		Log::write("[config] cannot write %s", tmp.c_str());
		return;
	}
	std::string text = w.str();
	fwrite(text.data(), 1, text.size(), f);
	fclose(f);
	remove(path.c_str());
	rename(tmp.c_str(), path.c_str());
}
