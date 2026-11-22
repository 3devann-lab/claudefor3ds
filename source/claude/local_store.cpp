#include "claude/local_store.h"

#include "claude/stream.h"
#include "core/log.h"
#include "platform/system.h"
#include "util/json.h"
#include "util/util.h"

#include <algorithm>
#include <cstdio>

namespace claude {
namespace LocalStore {

namespace {

std::string dir(AppMode mode) {
	return sys::dataDir() + (mode == AppMode::Code ? "/conversations/code" : "/conversations/chat");
}

std::string readFile(const std::string &path) {
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) {
		return "";
	}
	std::string out;
	char buf[8192];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
		out.append(buf, n);
	}
	fclose(f);
	return out;
}

bool writeFile(const std::string &path, const std::string &data) {
	std::string tmp = path + ".tmp";
	FILE *f = fopen(tmp.c_str(), "wb");
	if (!f) {
		return false;
	}
	fwrite(data.data(), 1, data.size(), f);
	fclose(f);
	::remove(path.c_str());
	return rename(tmp.c_str(), path.c_str()) == 0;
}

std::vector<ConversationSummary> readIndex(AppMode mode) {
	std::vector<ConversationSummary> out;
	json::Document doc;
	if (!json::parse(doc, readFile(dir(mode) + "/index.json")) || !doc.IsArray()) {
		return out;
	}
	for (const auto &e : doc.GetArray()) {
		ConversationSummary s;
		s.id = json::str(e, "id");
		s.title = json::str(e, "title");
		s.subtitle = json::str(e, "preview");
		s.updated = json::i64(e, "updated");
		s.mode = mode;
		if (!s.id.empty()) {
			out.push_back(s);
		}
	}
	return out;
}

void writeIndex(AppMode mode, const std::vector<ConversationSummary> &items) {
	json::Writer w;
	w.beginArray();
	for (const auto &s : items) {
		w.beginObject().kv("id", s.id).kv("title", s.title).kv("preview", s.subtitle).kv("updated", s.updated).endObject();
	}
	w.endArray();
	writeFile(dir(mode) + "/index.json", w.str());
}

} // namespace

std::vector<ConversationSummary> list(AppMode mode) {
	auto items = readIndex(mode);
	std::sort(items.begin(), items.end(),
	          [](const ConversationSummary &a, const ConversationSummary &b) { return a.updated > b.updated; });
	return items;
}

bool load(Conversation &conv) {
	json::Document doc;
	if (!json::parse(doc, readFile(dir(conv.mode) + "/" + conv.id + ".json")) || !doc.IsObject()) {
		return false;
	}
	conv.title = json::str(doc, "title");
	conv.containerId = json::str(doc, "container_id");
	conv.messages.clear();
	if (const json::Value *msgs = json::arr(doc, "messages")) {
		for (const auto &m : msgs->GetArray()) {
			Message msg;
			msg.id = json::str(m, "id", util::uuid4());
			msg.role = json::str(m, "role") == "assistant" ? Role::Assistant : Role::User;
			msg.hidden = json::boolean(m, "hidden");
			msg.created = json::i64(m, "created");
			std::string state = json::str(m, "state");
			msg.state = state == "stopped" ? Message::State::Stopped
			            : state == "error" ? Message::State::Error
			                               : Message::State::Done;
			msg.error = json::str(m, "error");
			if (const json::Value *content = json::get(m, "content")) {
				if (!json::boolean(m, "display_only")) {
					msg.raw = json::stringify(*content);
				}
				appendContent(*content, msg.blocks);
			}
			conv.messages.push_back(std::move(msg));
		}
	}
	// Everything in a saved conversation has finished running.
	for (Message &m : conv.messages) {
		for (Block &b : m.blocks) {
			if (b.type == BlockType::ToolUse) {
				b.done = true;
			}
		}
	}
	conv.loaded = true;
	return true;
}

void save(const Conversation &conv) {
	if (conv.id.empty()) {
		return;
	}
	sys::ensureDir(dir(conv.mode));
	json::Writer w;
	w.beginObject();
	w.kv("id", conv.id).kv("title", conv.title).kv("container_id", conv.containerId);
	w.key("messages").beginArray();
	std::string preview;
	for (const Message &m : conv.messages) {
		if (m.state == Message::State::Streaming) {
			continue;
		}
		std::string content = m.raw;
		bool displayOnly = false;
		if (content.empty() || content == "[]") {
			// Display-only messages (answers shown as bubbles, errors) keep their text.
			std::string text = m.plainText();
			if (text.empty() && m.error.empty()) {
				continue;
			}
			json::Writer c;
			c.beginArray().beginObject().kv("type", "text").kv("text", text.empty() ? m.error : text).endObject().endArray();
			content = c.str();
			displayOnly = true;
		}
		w.beginObject();
		w.kv("id", m.id);
		w.kv("role", m.role == Role::Assistant ? "assistant" : "user");
		w.kv("hidden", m.hidden);
		w.kv("created", m.created);
		if (displayOnly) {
			w.kv("display_only", true);
		}
		if (m.state == Message::State::Stopped) {
			w.kv("state", "stopped");
		} else if (m.state == Message::State::Error) {
			w.kv("state", "error");
			w.kv("error", m.error);
		}
		w.key("content").raw(content);
		w.endObject();
		if (!m.hidden && m.role == Role::Assistant) {
			std::string t = m.plainText();
			if (!t.empty()) {
				preview = t;
			}
		}
	}
	w.endArray();
	w.endObject();
	if (!writeFile(dir(conv.mode) + "/" + conv.id + ".json", w.str())) {
		Log::write("[store] could not save conversation %s", conv.id.c_str());
		return;
	}
	auto items = readIndex(conv.mode);
	items.erase(std::remove_if(items.begin(), items.end(),
	                           [&](const ConversationSummary &s) { return s.id == conv.id; }),
	            items.end());
	ConversationSummary s;
	s.id = conv.id;
	s.title = conv.title;
	s.subtitle = util::ellipsize(preview, 80);
	s.updated = util::nowUtc();
	items.insert(items.begin(), s);
	writeIndex(conv.mode, items);
}

void remove(AppMode mode, const std::string &id) {
	::remove((dir(mode) + "/" + id + ".json").c_str());
	auto items = readIndex(mode);
	items.erase(std::remove_if(items.begin(), items.end(), [&](const ConversationSummary &s) { return s.id == id; }),
	            items.end());
	writeIndex(mode, items);
}

} // namespace LocalStore
} // namespace claude
