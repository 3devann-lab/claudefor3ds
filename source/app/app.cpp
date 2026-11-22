#include "app/app.h"

#include "claude/local_store.h"
#include "claude/stream.h"
#include "core/log.h"
#include "core/secure_store.h"
#include "platform/system.h"
#include "ui/ui.h"
#include "util/util.h"

#include <algorithm>
#include <cstdio>

namespace app {

using namespace claude;

App &App::get() {
	static App instance;
	return instance;
}

void App::init() {
	conv.mode = config().mode;
	Session session;
	if (SecureStore::importKeyFile(session) || SecureStore::load(session)) {
		backend_ = makeApiBackend(session);
		connect();
	} else {
		phase = Phase::SignIn;
	}
}

void App::shutdown() {
	if (backend_ && conv.busy) {
		backend_->stop(conv);
	}
	config().save();
}

void App::update() {
	if (backend_ && phase == Phase::Main) {
		backend_->tick(conv, this);
	}
}

// ---- signing in ------------------------------------------------------------

void App::signInWithKey(const std::string &raw) {
	std::string key = util::trim(raw);
	if (!SecureStore::looksLikeApiKey(key)) {
		signInStatus = "That doesn't look like an API key. Keys start with \"sk-ant-\".";
		signInError = true;
		return;
	}
	Session session;
	session.fields["api_key"] = key;
	backend_ = makeApiBackend(session);
	connect();
}

bool App::signInFromFile() {
	Session session;
	if (!SecureStore::importKeyFile(session)) {
		signInStatus = "No key found. Save it as sdmc:/3ds/ClaudeFor3DS/api_key.txt";
		signInError = true;
		return false;
	}
	backend_ = makeApiBackend(session);
	connect();
	return true;
}

void App::connect() {
	phase = Phase::Connecting;
	working = true;
	signInError = false;
	signInStatus = "Checking your API key...";
	backend_->connect([this](const Result &r) {
		working = false;
		if (!r.ok) {
			signInStatus = r.error;
			signInError = true;
			if (r.authExpired) {
				SecureStore::clear();
				backend_.reset();
				phase = Phase::SignIn;
			}
			return;
		}
		SecureStore::save(backend_->session());
		config().backend = BackendKind::ApiKey;
		config().accountName = backend_->account().name;
		config().save();
		phase = Phase::Main;
		signInStatus.clear();
		loadModels();
		restoreLastConversation();
		ui::toast("Signed in");
	});
}

void App::retryConnect() {
	if (backend_) {
		connect();
	}
}

void App::signOut() {
	if (backend_ && conv.busy) {
		backend_->stop(conv);
	}
	SecureStore::clear();
	backend_.reset();
	conv.clear();
	queue.clear();
	tasks.clear();
	hasQuestion = false;
	models.clear();
	usage = Usage();
	config().backend = BackendKind::None;
	config().accountName.clear();
	config().save();
	phase = Phase::SignIn;
	signInStatus = "Signed out. The key was removed from this 3DS.";
	signInError = false;
	contentVersion++;
}

void App::loadModels() {
	backend_->fetchModels([this](std::vector<ModelInfo> list) {
		models = list;
		if (models.empty()) {
			return;
		}
		auto found = std::find_if(models.begin(), models.end(), [](const ModelInfo &m) { return m.id == config().model; });
		if (found == models.end()) {
			// Default: the newest Sonnet, else the newest model.
			auto pick = std::find_if(models.begin(), models.end(),
			                         [](const ModelInfo &m) { return util::contains(m.id, "sonnet"); });
			const ModelInfo &m = pick != models.end() ? *pick : models.front();
			config().model = m.id;
			config().modelName = m.name;
			config().save();
		} else {
			config().modelName = found->name;
		}
	});
}

void App::restoreLastConversation() {
	const std::string &id = conv.mode == AppMode::Code ? config().lastCodeId : config().lastChatId;
	if (!id.empty()) {
		openConversation(id);
	}
	refreshConversations();
}

// ---- conversations -------------------------------------------------------------

void App::newConversation() {
	if (conv.busy) {
		backend_->stop(conv);
	}
	AppMode mode = conv.mode;
	conv.clear();
	conv.mode = mode;
	conv.loaded = true;
	tasks.clear();
	hasQuestion = false;
	queuePaused = false;
	contentVersion++;
	(mode == AppMode::Code ? config().lastCodeId : config().lastChatId).clear();
	config().save();
}

void App::openConversation(const std::string &id) {
	if (!backend_) {
		return;
	}
	if (conv.busy) {
		backend_->stop(conv);
	}
	AppMode mode = conv.mode;
	conv.clear();
	conv.mode = mode;
	conv.id = id;
	hasQuestion = false;
	backend_->openConversation(conv, [this, id](const Result &r) {
		if (!r.ok) {
			ui::toast(r.error);
			newConversation();
			return;
		}
		(conv.mode == AppMode::Code ? config().lastCodeId : config().lastChatId) = id;
		config().save();
		rebuildTasks();
		// A question left unanswered at the end of the conversation is still waiting.
		if (!conv.messages.empty()) {
			const Message &last = conv.messages.back();
			for (const Block &b : last.blocks) {
				if (b.type == BlockType::ToolUse && b.name == "ask_user_question" && last.role == Role::Assistant) {
					PendingQuestion q;
					q.toolUseId = b.id;
					if (parseQuestions(b.input, q.questions)) {
						question = q;
						hasQuestion = true;
					}
				}
			}
		}
		contentVersion++;
		scrollToEnd = true;
	});
	contentVersion++;
}

void App::deleteConversation(const std::string &id) {
	LocalStore::remove(conv.mode, id);
	if (conv.id == id) {
		newConversation();
	}
	refreshConversations();
}

void App::refreshConversations() {
	if (!backend_) {
		return;
	}
	backend_->listConversations(conv.mode, [this](const Result &r, std::vector<ConversationSummary> list) {
		if (r.ok) {
			conversations = list;
		}
	});
}

void App::setMode(AppMode mode) {
	if (mode == conv.mode) {
		return;
	}
	if (conv.busy) {
		ui::toast("Stop Claude before switching modes");
		return;
	}
	config().mode = mode;
	config().save();
	conv.clear();
	conv.mode = mode;
	conv.loaded = true;
	queue.clear();
	tasks.clear();
	hasQuestion = false;
	contentVersion++;
	restoreLastConversation();
	ui::toast(mode == AppMode::Code ? "Claude Code mode" : "Chat mode");
}

void App::setModel(const ModelInfo &model) {
	config().model = model.id;
	config().modelName = model.name;
	config().save();
}

void App::setEffort(Effort effort) {
	config().effort = effort;
	config().save();
}

std::string App::shortModelName(const std::string &name) {
	std::string n = name;
	if (util::startsWith(n, "Claude ")) {
		n = n.substr(7);
	}
	return n;
}

std::string App::modelLabel() const {
	if (!config().modelName.empty()) {
		return shortModelName(config().modelName);
	}
	return config().model.empty() ? "No model" : config().model;
}

// ---- talking -------------------------------------------------------------------

SendOptions App::options(const std::string &text) const {
	SendOptions o;
	o.text = text;
	o.model = config().model;
	o.effort = config().effort;
	return o;
}

void App::compose() {
	if (phase != Phase::Main) {
		return;
	}
	sys::KeyboardOptions kb;
	kb.hint = conv.busy ? "Queue a message for Claude" : (conv.mode == AppMode::Code ? "Describe a coding task" : "Message Claude");
	kb.multiline = true;
	kb.maxLength = 4000;
	kb.okLabel = conv.busy ? "Queue" : "Send";
	std::string text;
	if (sys::keyboard(kb, text)) {
		submit(text);
	}
}

void App::submit(const std::string &raw) {
	std::string text = util::trim(raw);
	if (text.empty() || !backend_) {
		return;
	}
	if (hasQuestion) {
		// Typing while a question is open answers it in your own words.
		answerQuestion({text});
		return;
	}
	if (conv.busy) {
		queue.push_back(text);
		ui::toast("Queued (" + std::to_string(queue.size()) + ")");
		return;
	}
	sendNow(text);
}

void App::sendNow(const std::string &text) {
	lastError.clear();
	backend_->send(conv, options(text), this);
	scrollToEnd = true;
	contentVersion++;
	updateSleep();
}

void App::stop() {
	if (backend_ && conv.busy) {
		backend_->stop(conv);
		if (!queue.empty()) {
			queuePaused = true;
		}
	}
}

void App::resumeQueue() {
	queuePaused = false;
	if (!conv.busy && !hasQuestion && !queue.empty()) {
		std::string next = queue.front();
		queue.pop_front();
		sendNow(next);
	}
}

void App::removeQueued(size_t index) {
	if (index < queue.size()) {
		queue.erase(queue.begin() + index);
	}
	if (queue.empty()) {
		queuePaused = false;
	}
}

void App::clearQueue() {
	queue.clear();
	queuePaused = false;
}

void App::answerQuestion(const std::vector<std::string> &answers) {
	if (!hasQuestion || !backend_) {
		return;
	}
	PendingQuestion q = question;
	hasQuestion = false;
	backend_->answer(conv, q, answers, options(""), this);
	scrollToEnd = true;
	contentVersion++;
	updateSleep();
}

void App::dismissQuestion() { hasQuestion = false; }

void App::retryLast() {
	if (conv.busy) {
		return;
	}
	for (auto it = conv.messages.rbegin(); it != conv.messages.rend(); ++it) {
		if (it->role == Role::User && !it->hidden && !it->raw.empty()) {
			std::string text = it->plainText();
			// Drop the failed reply so the retry replaces it.
			while (!conv.messages.empty() && conv.messages.back().role == Role::Assistant &&
			       conv.messages.back().state == Message::State::Error) {
				conv.messages.pop_back();
			}
			if (!conv.messages.empty() && conv.messages.back().role == Role::User &&
			    conv.messages.back().plainText() == text) {
				conv.messages.pop_back();
			}
			sendNow(text);
			return;
		}
	}
}

void App::saveToSd(const Block &block) {
	if (!backend_) {
		return;
	}
	std::string key = block.imageKey.empty() ? block.url : block.imageKey;
	if (key.empty() && block.type == BlockType::Image && !block.text.empty()) {
		std::vector<uint8_t> bytes = util::base64Decode(block.text);
		std::string dir = sys::dataDir() + "/files";
		sys::ensureDir(dir);
		std::string ext = util::contains(block.mime, "png") ? ".png" : util::contains(block.mime, "gif") ? ".gif" : ".jpg";
		std::string path = dir + "/image-" + std::to_string(util::nowUtc()) + ext;
		FILE *f = fopen(path.c_str(), "wb");
		if (f) {
			fwrite(bytes.data(), 1, bytes.size(), f);
			fclose(f);
			ui::toast("Saved to " + path.substr(path.find("/3ds/")));
		}
		return;
	}
	if (key.empty()) {
		ui::toast("Nothing to save");
		return;
	}
	ui::toast("Downloading...");
	backend_->fetchMedia(key, true, [](bool ok, std::string bytes, std::string mime, std::string name) {
		if (!ok || bytes.empty()) {
			ui::toast(name.empty() ? "Download failed" : "Download failed: " + name);
			return;
		}
		std::string dir = sys::dataDir() + "/files";
		sys::ensureDir(dir);
		if (name.empty()) {
			name = "file-" + std::to_string(util::nowUtc());
		}
		for (char &c : name) {
			if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
				c = '_';
			}
		}
		std::string path = dir + "/" + name;
		FILE *f = fopen(path.c_str(), "wb");
		if (!f) {
			ui::toast("Could not write to the SD card");
			return;
		}
		fwrite(bytes.data(), 1, bytes.size(), f);
		fclose(f);
		ui::toast("Saved " + name + " (" + util::formatBytes(bytes.size()) + ")");
	});
}

// ---- listener --------------------------------------------------------------------

void App::stampTimes() {
	uint64_t now = util::nowMs();
	for (Message &m : conv.messages) {
		if (m.state != Message::State::Streaming) {
			continue;
		}
		for (Block &b : m.blocks) {
			if (b.type == BlockType::Thinking) {
				if (!b.startedMs) {
					b.startedMs = now;
				}
				if (b.done && !b.endedMs) {
					b.endedMs = now;
				}
			}
		}
	}
}

void App::rebuildTasks() {
	std::vector<TaskItem> list;
	for (const Message &m : conv.messages) {
		for (size_t i = 0; i < m.blocks.size(); i++) {
			const Block &b = m.blocks[i];
			if (b.type != BlockType::ToolUse || classifyTool(b.name, !b.server.empty()) != ToolKind::Todo) {
				continue;
			}
			if (b.input.empty()) {
				continue;
			}
			std::string result;
			for (const Message &m2 : conv.messages) {
				for (const Block &r : m2.blocks) {
					if (r.type == BlockType::ToolResult && r.forToolId == b.id) {
						result = r.text;
					}
				}
			}
			applyTaskTool(b, result, list);
		}
	}
	tasks = list;
}

void App::onTurnUpdate() {
	stampTimes();
	rebuildTasks();
	contentVersion++;
}

void App::onQuestion(const PendingQuestion &q) {
	question = q;
	hasQuestion = true;
	contentVersion++;
}

void App::onUsage(const Usage &u) { usage = u; }

void App::onTurnEnd(TurnOutcome outcome, const std::string &error) {
	stampTimes();
	rebuildTasks();
	contentVersion++;
	updateSleep();
	if (outcome == TurnOutcome::Error) {
		lastError = error;
	}
	refreshConversations();
	if (outcome == TurnOutcome::Done && !queue.empty() && !queuePaused && !hasQuestion) {
		std::string next = queue.front();
		queue.pop_front();
		sendNow(next);
	}
}

void App::onConversationInfo(const std::string &id, const std::string &title) {
	conv.id = id;
	conv.title = title;
	(conv.mode == AppMode::Code ? config().lastCodeId : config().lastChatId) = id;
	config().save();
}

void App::updateSleep() { sys::setSleepAllowed(!(conv.busy && config().keepAwake)); }

} // namespace app
