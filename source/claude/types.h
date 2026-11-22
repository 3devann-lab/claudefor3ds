// What a conversation looks like to the UI, whichever backend it came from.
#pragma once

#include "core/config.h"

#include <cstdint>
#include <string>
#include <vector>

namespace claude {

enum class Role { User, Assistant };

enum class BlockType {
	Text,
	Thinking,
	ToolUse,    // Claude calls a tool (client or server side)
	ToolResult, // what the tool returned
	Image,      // an image attached by the user or produced by Claude
	File,       // a file Claude made or attached (with a link when known)
	Artifact,   // claude.ai artifact (document, code, html, svg...)
	Notice,     // app-generated note: errors, "stopped", unsupported content
	Unknown,    // a block type this build does not understand (shown as a placeholder)
};

// What a tool call is about, for icons and the activity view.
enum class ToolKind {
	Generic,
	WebSearch,
	WebFetch,
	Command,  // bash / shell / code execution
	FileRead,
	FileEdit, // edit / write / create file
	Search,   // grep / glob
	Todo,     // TodoWrite / Task* tools
	Question, // AskUserQuestion
	Agent,    // subagent
	Schedule, // cron / scheduled task / wakeup
	Connector,// MCP connector tool
	Artifact,
	Memory,
	Image,
};

struct Block {
	BlockType type = BlockType::Text;
	std::string text;   // text, thinking, tool result text, notice
	std::string id;     // tool_use id / artifact id
	std::string name;   // tool name, artifact or file title
	std::string server; // connector (MCP server) name
	std::string input;  // tool input JSON (accumulates while streaming)
	std::string mime;   // image / file / artifact type
	std::string url;    // where it lives (link shown + QR code)
	std::string imageKey;   // key for the image cache
	std::string rawType;    // block type as the server sent it
	std::string forToolId;  // tool_result: which tool_use it answers
	bool done = true;
	bool error = false;
	bool serverTool = false;
	uint64_t startedMs = 0;
	uint64_t endedMs = 0;
};

struct Message {
	std::string id;
	Role role = Role::User;
	std::vector<Block> blocks;
	enum class State { Done, Streaming, Stopped, Error } state = State::Done;
	std::string error;
	int64_t created = 0;
	// Bumped whenever content changes, so cached layouts know to rebuild.
	uint32_t version = 1;
	// Hidden plumbing messages (client tool results) are kept but not drawn.
	bool hidden = false;
	// API mode: the content array exactly as the API wants it back next turn.
	std::string raw;

	std::string plainText() const;
};

struct ConversationSummary {
	std::string id;
	std::string title;
	std::string subtitle;
	int64_t updated = 0;
	AppMode mode = AppMode::Chat;
	std::string status; // Claude Code sessions: running / idle / ...
};

struct ModelInfo {
	std::string id;
	std::string name;
	std::string description;
};

struct UsageWindow {
	std::string label;    // "5-hour", "Weekly", "Weekly (Opus)", "Tokens / min"
	float used = 0.0f;    // 0..1
	int64_t resetsAt = 0; // UTC seconds, 0 when unknown
};

struct Usage {
	bool known = false;
	std::vector<UsageWindow> windows;
	std::string note;
	// The window closest to its limit, for the status bar.
	const UsageWindow *primary() const;
};

struct TaskItem {
	std::string id;
	std::string content;
	std::string activeForm;
	std::string status; // pending / in_progress / completed
};

struct QuestionOption {
	std::string label;
	std::string description;
	std::string preview;
};

struct Question {
	std::string header;
	std::string question;
	std::vector<QuestionOption> options;
	bool multiSelect = false;
};

struct PendingQuestion {
	std::string toolUseId;
	std::vector<Question> questions;
};

struct ScheduledTask {
	std::string id;
	std::string name;
	std::string schedule;
	int64_t nextRun = 0;
	bool enabled = true;
};

struct Account {
	std::string email;
	std::string name;
	std::string orgId;
	std::string orgName;
	std::string plan;
};

// Classifies a tool by name (and server, for connectors).
ToolKind classifyTool(const std::string &name, bool isMcp);
// Connector (MCP server) a tool call goes to: Block::server, or parsed from "mcp__server__tool".
std::string mcpServerOf(const Block &toolUse);
const char *toolKindLabel(ToolKind kind);
// A one-line description of a tool call: "Searched the web: 3ds homebrew", "Ran: make -j4".
std::string describeToolCall(const Block &toolUse, bool running);
// Short label of what a tool returned: "12 results", "exit 0", "error".
std::string describeToolResult(const Block &result);

} // namespace claude
