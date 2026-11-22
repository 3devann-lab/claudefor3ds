// Turns Messages-API-style content (streamed SSE events, or finished content arrays) into
// display blocks. claude.ai, the Anthropic API and Claude Code sessions all speak this shape.
#pragma once

#include "claude/types.h"
#include "util/json.h"

#include <memory>
#include <string>
#include <vector>

namespace claude {

struct SseEvent {
	std::string event;
	std::string data;
};

// Converts one finished content block (JSON object) to display blocks, appending to `out`.
// Tool results also mark their tool call done in `out`.
void appendContentBlock(const json::Value &block, std::vector<Block> &out);
// A whole content array (or a plain string) into display blocks.
void appendContent(const json::Value &content, std::vector<Block> &out);

struct StreamResult {
	bool finished = false;
	std::string stopReason;
	std::string error;
	std::string errorType;
	std::string messageId;
	std::string containerId;
	// claude.ai "message_limit" or rate-limit details, raw JSON.
	std::string limitJson;
};

class StreamAssembler {
  public:
	StreamAssembler();
	~StreamAssembler();

	// Applies one SSE event to `msg`. Returns true when `msg` changed.
	bool apply(Message &msg, const SseEvent &ev);
	const StreamResult &result() const { return result_; }

	// The assistant content as the API wants it back next turn (JSON array text).
	std::string rawContentJson() const;
	bool hasToolUse(const std::string &toolName = "") const;

  private:
	struct Raw;
	void startBlock(Message &msg, int index, const json::Value &cb);
	void deltaBlock(Message &msg, int index, const json::Value &delta);
	void stopBlock(Message &msg, int index);
	int displayIndexFor(int index) const;

	std::vector<std::unique_ptr<Raw>> raw_;
	StreamResult result_;
	// Legacy claude.ai "completion" events stream plain text into one block.
	int legacyBlock_ = -1;
};

// Pulls the AskUserQuestion-style questions out of a tool call's input.
bool parseQuestions(const std::string &inputJson, std::vector<Question> &out);
// Applies a task tool call (TodoWrite / TaskCreate / TaskUpdate / todo_write) to a task list.
void applyTaskTool(const Block &toolUse, const std::string &resultText, std::vector<TaskItem> &tasks);

} // namespace claude
