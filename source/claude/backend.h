// A source of Claude. The concrete backend depends on the sign-in route, which is not chosen yet
// (a companion bridge to the official Claude Code on the user's computer, or the Anthropic API).
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
	// Claude Code session state ("running", "idle", ...), when known.
	std::string sessionStatus;
	// Connectors the session reported (Claude Code init event).
	std::vector<std::string> connectors;

	void clear() {
		id.clear();
		title.clear();
		messages.clear();
		busy = false;
		loaded = false;
		sessionStatus.clear();
		connectors.clear();
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
	virtual void requestCode(const std::string &email, ResultCb done) { done(Result::fail("Not supported")); }
	virtual void verifyCode(const std::string &email, const std::string &code, ResultCb done) {
		done(Result::fail("Not supported"));
	}
	// Checks the stored session and loads the account.
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
	// Media referenced by a block (an image URL or "file:<id>"): raw bytes and MIME type.
	virtual void fetchMedia(const std::string &key,
	                        std::function<void(bool ok, std::string bytes, std::string mime, std::string name)> done) {
		done(false, "", "", "");
	}
	// A link the user can open on another device for a conversation or file.
	virtual std::string webLink(const Conversation &conv) const { return ""; }

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


// Answer text for a question when it has to go back as a normal message.
std::string formatAnswers(const PendingQuestion &question, const std::vector<std::string> &answers);

} // namespace claude
