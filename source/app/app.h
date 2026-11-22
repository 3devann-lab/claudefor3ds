// Application state and actions. Lives on the main thread; the UI reads it and calls actions.
#pragma once

#include "claude/backend.h"
#include "core/config.h"

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

enum class Phase { SignIn, Connecting, Main };

class App : public claude::TurnListener {
  public:
	static App &get();

	void init();
	void update();
	void shutdown();

	Phase phase = Phase::SignIn;
	std::string signInStatus;
	bool signInError = false;
	bool working = false; // a sign-in / connect request is running

	claude::Backend *backend() { return backend_.get(); }
	std::vector<claude::ModelInfo> models;
	claude::Usage usage;

	claude::Conversation conv;
	std::deque<std::string> queue;
	bool queuePaused = false;
	std::vector<claude::TaskItem> tasks;
	bool hasQuestion = false;
	claude::PendingQuestion question;
	std::vector<claude::ConversationSummary> conversations;
	// Bumped on any change to the conversation (layout caches and auto-scroll watch it).
	uint32_t contentVersion = 1;
	// Set when the view should jump to the newest message.
	bool scrollToEnd = false;
	std::string lastError;

	// ---- actions ---------------------------------------------------------
	void signInWithKey(const std::string &key);
	bool signInFromFile();
	void signOut();
	void retryConnect();

	// Opens the keyboard, then sends (or queues while Claude is busy).
	void compose();
	void submit(const std::string &text);
	void stop();
	void resumeQueue();
	void removeQueued(size_t index);
	void clearQueue();

	void newConversation();
	void openConversation(const std::string &id);
	void deleteConversation(const std::string &id);
	void refreshConversations();

	void setMode(AppMode mode);
	void setModel(const claude::ModelInfo &model);
	void setEffort(Effort effort);
	std::string modelLabel() const;
	static std::string shortModelName(const std::string &name);

	void answerQuestion(const std::vector<std::string> &answers);
	void dismissQuestion();
	void retryLast();
	void saveToSd(const claude::Block &block);

	bool busy() const { return conv.busy; }

	// ---- TurnListener ----------------------------------------------------
	void onTurnUpdate() override;
	void onQuestion(const claude::PendingQuestion &question) override;
	void onUsage(const claude::Usage &usage) override;
	void onTurnEnd(claude::TurnOutcome outcome, const std::string &error) override;
	void onConversationInfo(const std::string &id, const std::string &title) override;

  private:
	void connect();
	void loadModels();
	void restoreLastConversation();
	void sendNow(const std::string &text);
	void rebuildTasks();
	void stampTimes();
	claude::SendOptions options(const std::string &text) const;
	void updateSleep();

	std::unique_ptr<claude::Backend> backend_;
	uint64_t lastUsagePoll_ = 0;
};

} // namespace app
