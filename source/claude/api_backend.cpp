// The official Anthropic API (api.anthropic.com), signed in with the user's API key.
// Chat mode: web search and web fetch. Code mode adds the code execution sandbox (bash and a file
// editor in a container that persists across turns) and a task list. Questions and task lists are
// client tools that the app answers itself.

#include "claude/backend.h"
#include "claude/local_store.h"
#include "claude/stream_runner.h"
#include "core/log.h"
#include "net/http.h"
#include "util/json.h"
#include "util/util.h"

#include <algorithm>
#include <ctime>

namespace claude {

namespace {

const char *ASK_TOOL = R"({"name":"ask_user_question","description":"Ask the user one to four multiple-choice questions when you need their decision before continuing. The user answers on a Nintendo 3DS with buttons or touch and can always type their own answer instead, so never add an 'Other' option.","input_schema":{"type":"object","properties":{"questions":{"type":"array","minItems":1,"maxItems":4,"items":{"type":"object","properties":{"question":{"type":"string","description":"The full question, ending with a question mark."},"header":{"type":"string","description":"Very short label, at most 12 characters."},"options":{"type":"array","minItems":2,"maxItems":4,"items":{"type":"object","properties":{"label":{"type":"string","description":"1-5 words."},"description":{"type":"string","description":"What picking this means."}},"required":["label","description"]}},"multiSelect":{"type":"boolean"}},"required":["question","header","options","multiSelect"]}}},"required":["questions"]}})";

const char *TODO_TOOL = R"({"name":"todo_write","description":"Create and update the task list for multi-step work so the user can follow your progress. Always send the complete list. Keep exactly one task in_progress while working and mark tasks completed as soon as they are done.","input_schema":{"type":"object","properties":{"todos":{"type":"array","items":{"type":"object","properties":{"content":{"type":"string","description":"Imperative, e.g. 'Run the tests'."},"activeForm":{"type":"string","description":"Present continuous, e.g. 'Running the tests'."},"status":{"type":"string","enum":["pending","in_progress","completed"]}},"required":["content","activeForm","status"]}}},"required":["todos"]}})";

std::string dateLine() {
	time_t t = (time_t)(util::nowUtc() + (int64_t)util::utcOffsetMinutes() * 60);
	struct tm tmv;
	gmtime_r(&t, &tmv);
	char buf[64];
	strftime(buf, sizeof(buf), "%A, %B %d, %Y", &tmv);
	return buf;
}

std::string systemPrompt(AppMode mode) {
	std::string s;
	if (mode == AppMode::Code) {
		s = "You are Claude, working as a coding agent through ClaudeFor3DS on a Nintendo 3DS. You have a "
		    "sandboxed Linux container through the code execution tool (bash and a file editor); files and "
		    "installed packages persist across turns of this conversation. Do the work there: write code, run it, "
		    "test it. Plan anything with more than two steps with todo_write and keep it current. Use "
		    "ask_user_question when a decision is genuinely the user's. The user reads on a 400x240 screen, so keep "
		    "explanations short and put long output in files.";
	} else {
		s = "You are Claude, made by Anthropic, chatting through ClaudeFor3DS on a Nintendo 3DS. The screen is "
		    "400x240 pixels: prefer concise answers, short paragraphs and compact Markdown (avoid wide tables). "
		    "When you need the user to choose between options, use ask_user_question.";
	}
	s += " Today is " + dateLine() + ".";
	if (!config().customInstructions.empty()) {
		s += "\n\n" + config().customInstructions;
	}
	return s;
}

class ApiBackend : public Backend {
  public:
	explicit ApiBackend(const Session &session) {
		session_ = session;
		base_ = config().apiBase.empty() ? "https://api.anthropic.com" : config().apiBase;
		while (!base_.empty() && base_.back() == '/') {
			base_.pop_back();
		}
		std::string key = session.apiKey();
		std::string tail = key.size() > 4 ? key.substr(key.size() - 4) : "";
		account_.name = "API key \xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2" + tail;
		account_.plan = "API";
		account_.orgName = "Anthropic API";
	}

	BackendKind kind() const override { return BackendKind::ApiKey; }
	std::string label() const override { return account_.name; }

	void connect(ResultCb done) override {
		net::Request req = makeRequest("GET", "/v1/models?limit=100");
		auto w = life_.weak();
		net::request(net::Lane::Api, req, [this, w, done](net::Response &resp) {
			net::Response r = resp;
			MainQueue::post([this, w, done, r]() {
				if (w.expired()) {
					return;
				}
				if (r.status == 401 || r.status == 403) {
					Result res = Result::fail("The API key was not accepted (" + r.describeError() + ").");
					res.authExpired = true;
					done(res);
					return;
				}
				if (!r.ok()) {
					done(Result::fail(r.describeError()));
					return;
				}
				parseModels(r.body);
				done(Result::success());
			});
		});
	}

	void fetchModels(std::function<void(std::vector<ModelInfo>)> done) override {
		if (!models_.empty()) {
			done(models_);
			return;
		}
		net::Request req = makeRequest("GET", "/v1/models?limit=100");
		auto w = life_.weak();
		net::request(net::Lane::Api, req, [this, w, done](net::Response &resp) {
			net::Response r = resp;
			MainQueue::post([this, w, done, r]() {
				if (w.expired()) {
					return;
				}
				if (r.ok()) {
					parseModels(r.body);
				}
				done(models_);
			});
		});
	}

	void fetchUsage(std::function<void(const Usage &)> done) override { done(usage_); }

	void listConversations(AppMode mode,
	                       std::function<void(const Result &, std::vector<ConversationSummary>)> done) override {
		done(Result::success(), LocalStore::list(mode));
	}

	void openConversation(Conversation &conv, ResultCb done) override {
		if (!LocalStore::load(conv)) {
			done(Result::fail("This conversation could not be read from the SD card."));
			return;
		}
		done(Result::success());
	}

	void fetchMedia(const std::string &key, bool anyType,
	                std::function<void(bool, std::string, std::string, std::string)> done) override {
		auto w = life_.weak();
		if (util::startsWith(key, "file:")) {
			std::string id = key.substr(5);
			net::Request meta = makeRequest("GET", "/v1/files/" + id);
			net::request(net::Lane::Api, meta, [this, w, id, anyType, done](net::Response &resp) {
				if (!resp.ok()) {
					std::string err = resp.describeError();
					MainQueue::post([done, err]() { done(false, "", "", err); });
					return;
				}
				json::Document doc;
				json::parse(doc, resp.body);
				std::string mime = doc.IsObject() ? json::str(doc, "mime_type") : "";
				std::string name = doc.IsObject() ? json::str(doc, "filename") : "";
				if (!anyType && !util::startsWith(mime, "image/")) {
					MainQueue::post([done, mime, name]() { done(true, "", mime, name); });
					return;
				}
				MainQueue::post([this, w, id, mime, name, done]() {
					if (w.expired()) {
						return;
					}
					net::Request content = makeRequest("GET", "/v1/files/" + id + "/content");
					content.timeoutSec = 90;
					net::request(net::Lane::Media, content, [done, mime, name](net::Response &r2) {
						bool ok = r2.ok();
						std::string body = std::move(r2.body);
						MainQueue::post([done, ok, body, mime, name]() { done(ok, body, mime, name); });
					});
				});
			});
			return;
		}
		net::Request req;
		req.url = key;
		req.timeoutSec = 45;
		net::request(net::Lane::Media, req, [done](net::Response &resp) {
			bool ok = resp.ok();
			std::string mime = resp.header("content-type");
			std::string body = std::move(resp.body);
			MainQueue::post([done, ok, body, mime]() { done(ok, body, mime, ""); });
		});
	}

	void send(Conversation &conv, const SendOptions &options, TurnListener *listener) override {
		if (conv.id.empty()) {
			conv.id = util::uuid4();
			conv.title = util::ellipsize(options.text, 48);
			listener->onConversationInfo(conv.id, conv.title);
		}
		Message user;
		user.id = util::uuid4();
		user.role = Role::User;
		user.created = util::nowUtc();
		Block b;
		b.type = BlockType::Text;
		b.text = options.text;
		user.blocks.push_back(b);
		json::Writer w;
		w.beginArray().beginObject().kv("type", "text").kv("text", options.text).endObject().endArray();
		user.raw = w.str();
		conv.messages.push_back(std::move(user));
		startTurn(conv, options, listener);
	}

	void answer(Conversation &conv, const PendingQuestion &question, const std::vector<std::string> &answers,
	            const SendOptions &options, TurnListener *listener) override {
		std::string text = formatAnswers(question, answers);
		Message result;
		result.id = util::uuid4();
		result.role = Role::User;
		result.hidden = true;
		result.created = util::nowUtc();
		json::Writer w;
		w.beginArray();
		w.beginObject().kv("type", "tool_result").kv("tool_use_id", question.toolUseId).kv("content", text).endObject();
		w.endArray();
		result.raw = w.str();
		markToolDone(conv, question.toolUseId);
		conv.messages.push_back(std::move(result));
		// Show the answer as the user's message so the conversation reads naturally (display only).
		Message shown;
		shown.id = util::uuid4();
		shown.role = Role::User;
		shown.created = util::nowUtc();
		Block b;
		b.type = BlockType::Text;
		b.text = text;
		shown.blocks.push_back(b);
		conv.messages.push_back(std::move(shown));
		startTurn(conv, options, listener);
	}

	void stop(Conversation &conv) override {
		if (turn_) {
			*turn_->cancel = true;
		}
	}

  private:
	net::Request makeRequest(const std::string &method, const std::string &path) {
		net::Request req;
		req.method = method;
		req.url = base_ + path;
		req.headers.push_back("x-api-key: " + session_.apiKey());
		req.headers.push_back("anthropic-version: 2023-06-01");
		req.headers.push_back("anthropic-beta: files-api-2025-04-14");
		req.headers.push_back("Accept: application/json");
		return req;
	}

	void parseModels(const std::string &body) {
		json::Document doc;
		if (!json::parse(doc, body)) {
			return;
		}
		const json::Value *data = json::arr(doc, "data");
		if (!data) {
			return;
		}
		models_.clear();
		for (const auto &m : data->GetArray()) {
			ModelInfo info;
			info.id = json::str(m, "id");
			info.name = json::str(m, "display_name", info.id);
			if (!info.id.empty()) {
				models_.push_back(info);
			}
		}
	}

	void markToolDone(Conversation &conv, const std::string &toolId) {
		for (Message &m : conv.messages) {
			for (Block &b : m.blocks) {
				if (b.type == BlockType::ToolUse && b.id == toolId) {
					b.done = true;
					b.endedMs = util::nowMs();
				}
			}
		}
	}

	// History in API form: alternating roles, every client tool call answered, tool results first.
	std::string buildMessages(const Conversation &conv, size_t upTo) {
		json::Document out(rapidjson::kArrayType);
		auto &a = out.GetAllocator();
		std::vector<std::string> unanswered;
		auto pushOrdered = [&](rapidjson::Value &dst, rapidjson::Value &src) {
			for (int pass = 0; pass < 2; pass++) {
				for (auto &blk : src.GetArray()) {
					bool isResult = json::str(blk, "type") == "tool_result";
					if ((pass == 0) == isResult) {
						rapidjson::Value copy;
						copy.CopyFrom(blk, a);
						dst.PushBack(copy, a);
					}
				}
			}
		};
		for (size_t i = 0; i < upTo && i < conv.messages.size(); i++) {
			const Message &m = conv.messages[i];
			if (m.raw.empty() || m.raw == "[]") {
				continue;
			}
			json::Document parsed;
			if (!json::parse(parsed, m.raw) || !parsed.IsArray() || parsed.Empty()) {
				continue;
			}
			rapidjson::Value content;
			content.CopyFrom(parsed, a);
			const char *role = m.role == Role::Assistant ? "assistant" : "user";
			rapidjson::Value blocks(rapidjson::kArrayType);
			if (m.role == Role::User && !unanswered.empty()) {
				std::vector<std::string> present;
				for (const auto &blk : content.GetArray()) {
					if (json::str(blk, "type") == "tool_result") {
						present.push_back(json::str(blk, "tool_use_id"));
					}
				}
				for (const auto &id : unanswered) {
					if (std::find(present.begin(), present.end(), id) == present.end()) {
						rapidjson::Value tr(rapidjson::kObjectType);
						tr.AddMember("type", "tool_result", a);
						tr.AddMember("tool_use_id", rapidjson::Value(id.c_str(), a), a);
						tr.AddMember("content", "(The user did not answer and moved on.)", a);
						blocks.PushBack(tr, a);
					}
				}
				unanswered.clear();
			}
			pushOrdered(blocks, content);
			if (m.role == Role::Assistant) {
				for (const auto &blk : content.GetArray()) {
					if (json::str(blk, "type") == "tool_use") {
						unanswered.push_back(json::str(blk, "id"));
					}
				}
			}
			if (!out.Empty() && json::str(out[out.Size() - 1], "role") == role) {
				rapidjson::Value &prev = out[out.Size() - 1]["content"];
				rapidjson::Value merged(rapidjson::kArrayType);
				if (m.role == Role::User) {
					rapidjson::Value both(rapidjson::kArrayType);
					for (auto &blk : prev.GetArray()) {
						rapidjson::Value c;
						c.CopyFrom(blk, a);
						both.PushBack(c, a);
					}
					for (auto &blk : blocks.GetArray()) {
						rapidjson::Value c;
						c.CopyFrom(blk, a);
						both.PushBack(c, a);
					}
					pushOrdered(merged, both);
				} else {
					for (auto *src : {&prev, &blocks}) {
						for (auto &blk : src->GetArray()) {
							rapidjson::Value c;
							c.CopyFrom(blk, a);
							merged.PushBack(c, a);
						}
					}
				}
				prev = merged;
				continue;
			}
			rapidjson::Value msg(rapidjson::kObjectType);
			msg.AddMember("role", rapidjson::Value(role, a), a);
			msg.AddMember("content", blocks, a);
			out.PushBack(msg, a);
		}
		while (!out.Empty() && json::str(out[0], "role") != "user") {
			out.Erase(out.Begin());
		}
		return json::stringify(out);
	}

	std::string buildBody(const Conversation &conv, const SendOptions &options, size_t upTo) {
		bool code = conv.mode == AppMode::Code;
		json::Writer w;
		w.beginObject();
		w.kv("model", options.model.empty() ? std::string("claude-sonnet-5-5") : options.model);
		w.kv("max_tokens", code ? 32000 : 16000);
		w.kv("stream", true);
		w.kv("system", systemPrompt(conv.mode));
		if (!noThinking_) {
			w.key("thinking").beginObject().kv("type", "adaptive").endObject();
		}
		if (!noEffort_) {
			w.key("output_config").beginObject().kv("effort", effortApiValue(options.effort)).endObject();
		}
		if (code && !conv.containerId.empty() && !noContainer_) {
			w.kv("container", conv.containerId);
		}
		bool mcp = !config().connectors.empty() && !noMcp_;
		if (mcp) {
			w.key("mcp_servers").beginArray();
			for (const auto &s : config().connectors) {
				w.beginObject().kv("type", "url").kv("url", s.url).kv("name", s.name);
				if (!s.token.empty()) {
					w.kv("authorization_token", s.token);
				}
				w.endObject();
			}
			w.endArray();
		}
		w.key("tools").beginArray();
		if (!noServerTools_ && config().webSearch) {
			w.beginObject().kv("type", "web_search_20250305").kv("name", "web_search").kv("max_uses", 5).endObject();
			w.beginObject().kv("type", "web_fetch_20250910").kv("name", "web_fetch").kv("max_uses", 5).endObject();
		}
		if (!noServerTools_ && code) {
			w.beginObject().kv("type", "code_execution_20250825").kv("name", "code_execution").endObject();
		}
		if (mcp) {
			for (const auto &s : config().connectors) {
				w.beginObject().kv("type", "mcp_toolset").kv("mcp_server_name", s.name).endObject();
			}
		}
		w.raw(ASK_TOOL);
		if (code) {
			w.raw(TODO_TOOL);
		}
		w.endArray();
		w.key("messages").raw(buildMessages(conv, upTo));
		w.endObject();
		return w.str();
	}

	void startTurn(Conversation &conv, const SendOptions &options, TurnListener *listener) {
		lastOptions_ = options;
		Message reply;
		reply.id = util::uuid4();
		reply.role = Role::Assistant;
		reply.state = Message::State::Streaming;
		reply.created = util::nowUtc();
		conv.messages.push_back(std::move(reply));
		conv.busy = true;
		attempts_ = 0;
		listener->onTurnUpdate();
		launch(conv, listener);
	}

	void launch(Conversation &conv, TurnListener *listener) {
		size_t index = conv.messages.size() - 1;
		bool code = conv.mode == AppMode::Code;
		net::Request req = makeRequest("POST", "/v1/messages");
		req.headers.erase(std::remove_if(req.headers.begin(), req.headers.end(),
		                                 [](const std::string &h) { return util::startsWith(h, "anthropic-beta:") ||
		                                                                   util::startsWith(h, "Accept:"); }),
		                  req.headers.end());
		std::string betas = "files-api-2025-04-14";
		if (code && !noServerTools_) {
			betas += ",code-execution-2025-08-25";
		}
		if (!config().connectors.empty() && !noMcp_) {
			betas += ",mcp-client-2025-11-20";
		}
		req.headers.push_back("anthropic-beta: " + betas);
		req.headers.push_back("Content-Type: application/json");
		req.headers.push_back("Accept: text/event-stream");
		req.body = buildBody(conv, lastOptions_, index);
		auto w = life_.weak();
		Conversation *convPtr = &conv;
		turn_ = runStream(req, conv, index, listener,
		                  [this, w, convPtr, listener](StreamTurn &turn, const net::Response &resp, bool current) {
			                  if (w.expired()) {
				                  return;
			                  }
			                  readRateLimits(resp, listener);
			                  if (!current) {
				                  return;
			                  }
			                  finishTurn(*convPtr, turn, resp, listener);
		                  });
	}

	bool degradeFor(const net::Response &resp) {
		if (resp.status != 400 || attempts_ >= 5) {
			return false;
		}
		std::string msg = util::toLower(resp.body);
		bool changed = false;
		if (!noEffort_ && (util::contains(msg, "effort") || util::contains(msg, "output_config"))) {
			noEffort_ = changed = true;
		} else if (!noThinking_ && util::contains(msg, "thinking")) {
			noThinking_ = changed = true;
		} else if (!noMcp_ && util::contains(msg, "mcp")) {
			noMcp_ = changed = true;
		} else if (!noContainer_ && util::contains(msg, "container")) {
			noContainer_ = changed = true;
		} else if (!noServerTools_ && (util::contains(msg, "tool") || util::contains(msg, "beta"))) {
			noServerTools_ = changed = true;
		}
		if (changed) {
			attempts_++;
			Log::write("[api] retrying with fewer features: %s", util::ellipsize(resp.body, 160).c_str());
		}
		return changed;
	}

	void finishTurn(Conversation &conv, StreamTurn &turn, const net::Response &resp, TurnListener *listener) {
		Message &msg = conv.messages[turn.messageIndex];
		const StreamResult &res = turn.assembler.result();
		if (!res.containerId.empty()) {
			conv.containerId = res.containerId;
		}
		if (resp.cancelled) {
			msg.raw = turn.assembler.rawContentJson();
			msg.state = Message::State::Stopped;
			finalizeBlocks(msg);
			conv.busy = false;
			msg.version++;
			LocalStore::save(conv);
			listener->onTurnEnd(TurnOutcome::Stopped, "");
			return;
		}
		if (!resp.ok() || !res.error.empty()) {
			if (degradeFor(resp)) {
				msg.blocks.clear();
				msg.version++;
				launch(conv, listener);
				return;
			}
			std::string err = !res.error.empty() ? res.error : resp.describeError();
			if (resp.status == 401) {
				err = "The API key was not accepted. Sign out and enter it again.";
			} else if (resp.status == 429) {
				err = "Rate limited. " + resp.describeError();
			} else if (resp.status == 529 || res.errorType == "overloaded_error") {
				err = "Claude is overloaded right now. Try again in a moment.";
			} else if (resp.status == 400 && util::contains(util::toLower(resp.body), "credit")) {
				err = "Your API account is out of credits.";
			}
			msg.raw = turn.assembler.rawContentJson();
			msg.state = Message::State::Error;
			msg.error = err;
			finalizeBlocks(msg);
			conv.busy = false;
			msg.version++;
			LocalStore::save(conv);
			listener->onTurnEnd(TurnOutcome::Error, err);
			return;
		}
		msg.raw = turn.assembler.rawContentJson();
		msg.state = Message::State::Done;
		msg.version++;
		const std::string &stop = res.stopReason;
		if (stop == "pause_turn") {
			// A long server-side tool run: send the conversation back so Claude carries on.
			startTurn(conv, lastOptions_, listener);
			return;
		}
		if (stop == "tool_use" && handleClientTools(conv, msg, listener)) {
			return;
		}
		if (stop == "max_tokens") {
			addNotice(msg, "The reply hit the length limit. Send \"continue\" for the rest.");
		} else if (stop == "refusal") {
			addNotice(msg, "Claude declined to continue this reply.");
		}
		finalizeBlocks(msg);
		conv.busy = false;
		LocalStore::save(conv);
		listener->onTurnEnd(TurnOutcome::Done, "");
	}

	// Answers todo_write at once; stops for ask_user_question. True when the turn goes on or waits.
	bool handleClientTools(Conversation &conv, Message &msg, TurnListener *listener) {
		std::vector<Block> calls;
		for (const Block &b : msg.blocks) {
			if (b.type == BlockType::ToolUse && !b.serverTool && !b.done) {
				calls.push_back(b);
			}
		}
		if (calls.empty()) {
			return false;
		}
		for (const Block &b : calls) {
			if (b.name == "ask_user_question") {
				PendingQuestion q;
				q.toolUseId = b.id;
				if (parseQuestions(b.input, q.questions)) {
					finalizeBlocks(msg);
					conv.busy = false;
					LocalStore::save(conv);
					listener->onQuestion(q);
					listener->onTurnEnd(TurnOutcome::NeedsAnswer, "");
					return true;
				}
			}
		}
		json::Writer w;
		w.beginArray();
		for (const Block &b : calls) {
			std::string reply = b.name == "todo_write" ? "Task list updated and shown to the user."
			                                           : "This tool is not available in this client.";
			w.beginObject().kv("type", "tool_result").kv("tool_use_id", b.id).kv("content", reply).endObject();
			markToolDone(conv, b.id);
		}
		w.endArray();
		finalizeBlocks(msg);
		Message results;
		results.id = util::uuid4();
		results.role = Role::User;
		results.hidden = true;
		results.raw = w.str();
		results.created = util::nowUtc();
		conv.messages.push_back(std::move(results));
		startTurn(conv, lastOptions_, listener);
		return true;
	}

	void finalizeBlocks(Message &msg) {
		for (Block &b : msg.blocks) {
			if (b.type == BlockType::Text || b.type == BlockType::Thinking) {
				b.done = true;
			}
			if (b.type == BlockType::ToolUse && b.serverTool && !b.done) {
				b.done = true;
				b.error = msg.state != Message::State::Done;
			}
		}
	}

	void addNotice(Message &msg, const std::string &text) {
		Block n;
		n.type = BlockType::Notice;
		n.text = text;
		msg.blocks.push_back(n);
		msg.version++;
	}

	void readRateLimits(const net::Response &resp, TurnListener *listener) {
		Usage u;
		auto window = [&](const char *prefix, const char *label) {
			std::string lim = resp.header(std::string("anthropic-ratelimit-") + prefix + "-limit");
			std::string rem = resp.header(std::string("anthropic-ratelimit-") + prefix + "-remaining");
			if (lim.empty() || rem.empty()) {
				return;
			}
			double l = atof(lim.c_str()), r = atof(rem.c_str());
			if (l <= 0) {
				return;
			}
			UsageWindow win;
			win.label = label;
			win.used = (float)std::max(0.0, std::min(1.0, 1.0 - r / l));
			win.resetsAt = util::parseIso8601(resp.header(std::string("anthropic-ratelimit-") + prefix + "-reset"));
			u.windows.push_back(win);
		};
		window("requests", "Requests / min");
		window("input-tokens", "Input tokens / min");
		window("output-tokens", "Output tokens / min");
		if (u.windows.empty()) {
			return;
		}
		u.known = true;
		u.note = "Rate limits for this API key";
		usage_ = u;
		listener->onUsage(u);
	}

	std::string base_;
	std::vector<ModelInfo> models_;
	Usage usage_;
	std::shared_ptr<StreamTurn> turn_;
	SendOptions lastOptions_;
	int attempts_ = 0;
	bool noEffort_ = false, noThinking_ = false, noServerTools_ = false, noMcp_ = false, noContainer_ = false;
};

} // namespace

std::unique_ptr<Backend> makeApiBackend(const Session &session) { return std::make_unique<ApiBackend>(session); }

std::string formatAnswers(const PendingQuestion &question, const std::vector<std::string> &answers) {
	if (question.questions.size() == 1) {
		return answers.empty() ? "(no answer)" : answers[0];
	}
	std::string out;
	for (size_t i = 0; i < question.questions.size(); i++) {
		const std::string a = i < answers.size() ? answers[i] : "(no answer)";
		const Question &q = question.questions[i];
		if (!out.empty()) {
			out += "\n";
		}
		out += (q.header.empty() ? q.question : q.header) + ": " + a;
	}
	return out;
}

} // namespace claude
