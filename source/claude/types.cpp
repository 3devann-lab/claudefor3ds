#include "claude/types.h"

#include "util/json.h"
#include "util/util.h"

namespace claude {

std::string Message::plainText() const {
	std::string out;
	for (const Block &b : blocks) {
		if (b.type == BlockType::Text) {
			if (!out.empty()) {
				out += "\n\n";
			}
			out += b.text;
		}
	}
	return out;
}

const UsageWindow *Usage::primary() const {
	const UsageWindow *best = nullptr;
	for (const UsageWindow &w : windows) {
		if (!best || w.used > best->used) {
			best = &w;
		}
	}
	return best;
}

ToolKind classifyTool(const std::string &rawName, bool isMcp) {
	if (isMcp || util::startsWith(rawName, "mcp__")) {
		return ToolKind::Connector;
	}
	std::string n = util::toLower(rawName);
	if (n == "web_search" || n == "websearch" || n == "brave_search") {
		return ToolKind::WebSearch;
	}
	if (n == "web_fetch" || n == "webfetch" || n == "fetch") {
		return ToolKind::WebFetch;
	}
	if (n == "bash" || n == "bash_tool" || n == "bash_code_execution" || n == "code_execution" || n == "repl" ||
	    n == "bashoutput" || n == "killshell" || n == "monitor" || n == "powershell") {
		return ToolKind::Command;
	}
	if (n == "read" || n == "view" || n == "notebookread") {
		return ToolKind::FileRead;
	}
	if (n == "edit" || n == "write" || n == "multiedit" || n == "notebookedit" || n == "create_file" ||
	    n == "str_replace" || n == "text_editor_code_execution" || n == "str_replace_based_edit_tool") {
		return ToolKind::FileEdit;
	}
	if (n == "glob" || n == "grep" || n == "conversation_search" || n == "recent_chats" ||
	    util::contains(n, "drive_search")) {
		return ToolKind::Search;
	}
	if (n == "todowrite" || n == "todo_write" || (util::startsWith(n, "task") && n != "task")) {
		return ToolKind::Todo;
	}
	if (util::contains(n, "ask_user") || n == "askuserquestion") {
		return ToolKind::Question;
	}
	if (n == "agent" || n == "task") {
		return ToolKind::Agent;
	}
	if (util::startsWith(n, "cron") || n == "schedulewakeup" || n == "remotetrigger" || util::contains(n, "schedule")) {
		return ToolKind::Schedule;
	}
	if (n == "artifacts" || n == "artifact" || n == "present_files") {
		return ToolKind::Artifact;
	}
	if (util::contains(n, "memory")) {
		return ToolKind::Memory;
	}
	if (util::contains(n, "image")) {
		return ToolKind::Image;
	}
	return ToolKind::Generic;
}

const char *toolKindLabel(ToolKind kind) {
	switch (kind) {
	case ToolKind::WebSearch:
		return "Web search";
	case ToolKind::WebFetch:
		return "Web fetch";
	case ToolKind::Command:
		return "Command";
	case ToolKind::FileRead:
		return "Read file";
	case ToolKind::FileEdit:
		return "Edit file";
	case ToolKind::Search:
		return "Search";
	case ToolKind::Todo:
		return "Tasks";
	case ToolKind::Question:
		return "Question";
	case ToolKind::Agent:
		return "Subagent";
	case ToolKind::Schedule:
		return "Scheduled task";
	case ToolKind::Connector:
		return "Connector";
	case ToolKind::Artifact:
		return "Artifact";
	case ToolKind::Memory:
		return "Memory";
	case ToolKind::Image:
		return "Image";
	case ToolKind::Generic:
		break;
	}
	return "Tool";
}

namespace {

// Reads a string field from tool input JSON, which may still be incomplete while streaming.
std::string field(const std::string &input, const char *key) {
	json::Document doc;
	if (!input.empty() && json::parse(doc, input) && doc.IsObject()) {
		return json::str(doc, key);
	}
	std::string needle = std::string("\"") + key + "\"";
	size_t p = input.find(needle);
	if (p == std::string::npos) {
		return "";
	}
	p = input.find(':', p + needle.size());
	if (p == std::string::npos) {
		return "";
	}
	p = input.find('"', p);
	if (p == std::string::npos) {
		return "";
	}
	std::string out;
	for (size_t i = p + 1; i < input.size(); i++) {
		char c = input[i];
		if (c == '\\' && i + 1 < input.size()) {
			char e = input[++i];
			out += e == 'n' ? ' ' : e == 't' ? ' ' : e;
			continue;
		}
		if (c == '"') {
			break;
		}
		out += c;
	}
	return out;
}

std::string baseName(const std::string &path) {
	size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string mcpToolName(const Block &b) {
	// mcp__<server>__<tool>
	if (util::startsWith(b.name, "mcp__")) {
		size_t sep = b.name.find("__", 5);
		if (sep != std::string::npos) {
			return b.name.substr(sep + 2);
		}
	}
	return b.name;
}

} // namespace

std::string mcpServerOf(const Block &b) {
	if (!b.server.empty()) {
		return b.server;
	}
	if (util::startsWith(b.name, "mcp__")) {
		size_t sep = b.name.find("__", 5);
		if (sep != std::string::npos) {
			return b.name.substr(5, sep - 5);
		}
	}
	return "";
}

std::string describeToolCall(const Block &b, bool running) {
	const std::string &in = b.input;
	ToolKind kind = classifyTool(b.name, !b.server.empty());
	std::string n = util::toLower(b.name);
	auto verb = [&](const char *now, const char *past) { return std::string(running ? now : past); };
	switch (kind) {
	case ToolKind::WebSearch: {
		std::string q = field(in, "query");
		return verb("Searching the web", "Searched the web") + (q.empty() ? "" : ": " + q);
	}
	case ToolKind::WebFetch: {
		std::string u = field(in, "url");
		return verb("Fetching", "Fetched") + (u.empty() ? " a page" : " " + u);
	}
	case ToolKind::Command: {
		std::string c = field(in, "command");
		if (c.empty()) {
			c = field(in, "code");
		}
		if (c.empty()) {
			c = field(in, "description");
		}
		return verb("Running", "Ran") + (c.empty() ? " a command" : ": " + util::oneLine(c));
	}
	case ToolKind::FileRead: {
		std::string p = field(in, "file_path");
		if (p.empty()) {
			p = field(in, "path");
		}
		return verb("Reading", "Read") + (p.empty() ? " a file" : " " + baseName(p));
	}
	case ToolKind::FileEdit: {
		std::string p = field(in, "file_path");
		if (p.empty()) {
			p = field(in, "path");
		}
		std::string cmd = field(in, "command");
		bool create = n == "write" || n == "create_file" || cmd == "create";
		if (cmd == "view") {
			return verb("Viewing", "Viewed") + (p.empty() ? " a file" : " " + baseName(p));
		}
		return (create ? verb("Writing", "Wrote") : verb("Editing", "Edited")) +
		       (p.empty() ? " a file" : " " + baseName(p));
	}
	case ToolKind::Search: {
		std::string p = field(in, "pattern");
		if (p.empty()) {
			p = field(in, "query");
		}
		return verb("Searching", "Searched") + (p.empty() ? "" : " for " + p);
	}
	case ToolKind::Todo: {
		if (n == "taskcreate") {
			return verb("Adding task", "Added task") + ": " + field(in, "subject");
		}
		if (n == "taskupdate") {
			std::string st = field(in, "status");
			return "Task " + field(in, "taskId") + (st.empty() ? " updated" : " -> " + st);
		}
		return verb("Updating the task list", "Updated the task list");
	}
	case ToolKind::Question:
		return verb("Asking you a question", "Asked you a question");
	case ToolKind::Agent: {
		std::string d = field(in, "description");
		std::string t = field(in, "subagent_type");
		return verb("Running subagent", "Subagent") + (t.empty() ? "" : " (" + t + ")") + (d.empty() ? "" : ": " + d);
	}
	case ToolKind::Schedule: {
		std::string cron = field(in, "cron");
		if (cron.empty()) {
			cron = field(in, "cron_expression");
		}
		std::string p = field(in, "prompt");
		if (n == "crondelete") {
			return "Deleted a scheduled task";
		}
		if (n == "cronlist") {
			return "Listed scheduled tasks";
		}
		if (n == "schedulewakeup") {
			return "Scheduled a check-in";
		}
		return verb("Scheduling", "Scheduled") + (cron.empty() ? " a task" : " (" + cron + ")") +
		       (p.empty() ? "" : ": " + util::ellipsize(p, 40));
	}
	case ToolKind::Connector: {
		std::string server = mcpServerOf(b);
		return verb("Using ", "Used ") + (server.empty() ? std::string("connector") : server) + ": " + mcpToolName(b);
	}
	case ToolKind::Artifact: {
		std::string t = field(in, "title");
		std::string cmd = field(in, "command");
		if (n == "present_files") {
			return "Shared files";
		}
		return (cmd == "update" || cmd == "rewrite" ? verb("Updating", "Updated") : verb("Creating", "Created")) +
		       (t.empty() ? " an artifact" : " " + t);
	}
	case ToolKind::Memory:
		return verb("Updating memory", "Updated memory");
	case ToolKind::Image:
	case ToolKind::Generic:
		break;
	}
	return verb("Using ", "Used ") + b.name;
}

std::string describeToolResult(const Block &r) {
	if (r.error) {
		return "error";
	}
	if (r.text.empty()) {
		return "done";
	}
	size_t lines = 1;
	for (char c : r.text) {
		if (c == '\n') {
			lines++;
		}
	}
	if (lines > 1) {
		return std::to_string(lines) + " lines";
	}
	return util::ellipsize(r.text, 48);
}

} // namespace claude
