// A source of Claude: the official Anthropic API, signed in with the user's API key.
// Everything here is called on the main thread, and every callback arrives on the main thread.
#pragma once

#include "claude/types.h"
#include "core/config.h"
#include "core/main_queue.h"
#include "core/secure_store.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace claude {

struct Result {
	bool ok = true;
	std::string error;
	// The session is no longer valid: go back to the sign-in screen.
	bool authExpired = false;

	static Result success() { return Result(); }
	static Result fail(const std::string &e) {
		Result r;
		r.ok = false;
		r.error = e;
		return r;
	}
};

// The open conversation. One instance lives for the whole app run; switching conversations
// clears it and bumps `generation`, so late network answers for the old one are dropped.
struct Conversation {
	std::string id;
	std::string title;
	AppMode mode = AppMode::Chat;
	std::vector<Message> messages;
	uint32_t generation = 0;
	bool busy = false;
	bool loaded = false;
	// Code mode: the sandbox container reused across turns.
	std::string containerId;

	void clear() {
		id.clear();
		title.clear();
		messages.clear();
		busy = false;
		loaded = false;
		containerId.clear();
		generation++;
	}
};

struct SendOptions {
	std::string text;
	std::string model;
	Effort effort = Effort::High;
};

enum class TurnOutcome { Done, Stopped, Error, NeedsAnswer };

class TurnListener {
  public:
	virtual ~TurnListener() = default;
	// Message content changed (new text, tool progress...).
	virtual void onTurnUpdate() = 0;
	virtual void onQuestion(const PendingQuestion &question) = 0;
	virtual void onUsage(const Usage &usage) = 0;
	virtual void onTurnEnd(TurnOutcome outcome, const std::string &error) = 0;
	// A new conversation got its id / title from the server.
	virtual void onConversationInfo(const std::string &id, const std::string &title) = 0;
};

using ResultCb = std::function<void(const Result &)>;

class Backend {
  public:
	virtual ~Backend() = default;

	virtual BackendKind kind() const = 0;
	virtual std::string label() const = 0;
	virtual bool supportsCodeMode() const { return true; }

	// --- signing in -------------------------------------------------------
	// Checks the key and loads the account.
	virtual void connect(ResultCb done) = 0;
	virtual const Account &account() const { return account_; }
	virtual void logout() {}
	const Session &session() const { return session_; }

	// --- account data -----------------------------------------------------
	virtual void fetchModels(std::function<void(std::vector<ModelInfo>)> done) = 0;
	virtual void fetchUsage(std::function<void(const Usage &)> done) = 0;
	virtual void fetchScheduled(std::function<void(bool supported, std::vector<ScheduledTask>, std::string note)> done) {
		done(false, {}, "Scheduled tasks aren't available with this sign-in.");
	}
	virtual void listConversations(AppMode mode,
	                               std::function<void(const Result &, std::vector<ConversationSummary>)> done) = 0;
	// Loads conv.id's messages into conv.
	virtual void openConversation(Conversation &conv, ResultCb done) = 0;
	// Media referenced by a block (an image URL or "file:<id>"): raw bytes, MIME type, file name.
	// Unless anyType is set, only images are downloaded (others report just type and name).
	virtual void fetchMedia(const std::string &key, bool anyType,
	                        std::function<void(bool ok, std::string bytes, std::string mime, std::string name)> done) {
		done(false, "", "", "");
	}

	// --- talking ----------------------------------------------------------
	virtual void send(Conversation &conv, const SendOptions &options, TurnListener *listener) = 0;
	virtual void answer(Conversation &conv, const PendingQuestion &question, const std::vector<std::string> &answers,
	                    const SendOptions &options, TurnListener *listener) = 0;
	virtual void stop(Conversation &conv) = 0;
	// Called every frame (polling backends use it).
	virtual void tick(Conversation &conv, TurnListener *listener) {}

  protected:
	Session session_;
	Account account_;
	LifeToken life_;
};

std::unique_ptr<Backend> makeApiBackend(const Session &session);

// The user's picks for a question, as the text Claude gets back.
std::string formatAnswers(const PendingQuestion &question, const std::vector<std::string> &answers);

} // namespace claude
