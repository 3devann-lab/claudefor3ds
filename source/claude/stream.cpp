#include "claude/stream.h"

#include "core/log.h"
#include "util/util.h"

#include <cstdlib>

namespace claude {

namespace {

std::string textOfContentArray(const json::Value &arr) {
	std::string out;
	for (const auto &item : arr.GetArray()) {
		if (item.IsString()) {
			out += item.GetString();
			continue;
		}
		std::string t = json::str(item, "type");
		if (t == "text") {
			if (!out.empty()) {
				out += "\n";
			}
			out += json::str(item, "text");
		} else if (t == "web_search_result" || t == "knowledge" || t == "search_result" || t == "document") {
			std::string title = json::str(item, "title");
			std::string url = json::str(item, "url");
			if (url.empty()) {
				url = json::str(item, "source");
			}
			if (!out.empty()) {
				out += "\n";
			}
			out += "- " + (title.empty() ? url : title) + (url.empty() || title.empty() ? "" : "\n  " + url);
		}
	}
	return out;
}

void markToolDone(std::vector<Block> &blocks, const std::string &toolId, bool error) {
	if (toolId.empty()) {
		return;
	}
	for (auto it = blocks.rbegin(); it != blocks.rend(); ++it) {
		if (it->type == BlockType::ToolUse && it->id == toolId) {
			it->done = true;
			it->error = it->error || error;
			if (!it->endedMs) {
				it->endedMs = util::nowMs();
			}
			return;
		}
	}
}

Block imageFromSource(const json::Value &item) {
	Block img;
	img.type = BlockType::Image;
	const json::Value *src = json::obj(item, "source");
	if (src) {
		std::string st = json::str(*src, "type");
		img.mime = json::str(*src, "media_type");
		if (st == "base64") {
			img.text = json::str(*src, "data");
		} else if (st == "url") {
			img.url = json::str(*src, "url");
			img.imageKey = img.url;
		} else if (st == "file") {
			img.imageKey = "file:" + json::str(*src, "file_id");
		}
	}
	img.name = json::str(item, "title", "Image");
	return img;
}

void addFileOutputs(const json::Value &arr, std::vector<Block> &out) {
	for (const auto &f : arr.GetArray()) {
		std::string id = json::str(f, "file_id");
		if (id.empty()) {
			continue;
		}
		Block file;
		file.type = BlockType::File;
		file.id = id;
		file.name = json::str(f, "filename", "Output file");
		file.imageKey = "file:" + id;
		out.push_back(file);
	}
}

// Server tool results come as an object rather than an array.
void fillServerResult(const json::Value &c, Block &r, std::vector<Block> &extra) {
	std::string t = json::str(c, "type");
	if (util::endsWith(t, "_error")) {
		r.error = true;
		r.text = "Error: " + json::str(c, "error_code", "unknown");
		return;
	}
	if (t == "web_fetch_result") {
		std::string url = json::str(c, "url");
		r.text = "Fetched " + url;
		r.url = url;
		return;
	}
	if (t == "bash_code_execution_result" || t == "code_execution_result") {
		std::string out = json::str(c, "stdout");
		std::string err = json::str(c, "stderr");
		int64_t code = json::i64(c, "return_code", 0);
		r.text = out;
		if (!err.empty()) {
			r.text += (r.text.empty() ? "" : "\n") + err;
		}
		if (code != 0) {
			r.error = true;
			r.text += (r.text.empty() ? "" : "\n") + util::format("(exit code %d)", (int)code);
		}
		if (const json::Value *files = json::arr(c, "content")) {
			addFileOutputs(*files, extra);
		}
		return;
	}
	if (t == "text_editor_code_execution_view_result") {
		r.text = json::str(c, "content");
		return;
	}
	if (t == "text_editor_code_execution_create_result") {
		r.text = json::boolean(c, "is_file_update") ? "File updated" : "File created";
		return;
	}
	if (t == "text_editor_code_execution_str_replace_result") {
		const json::Value *lines = json::arr(c, "lines");
		r.text = "Edited";
		if (lines) {
			r.text = "";
			for (const auto &l : lines->GetArray()) {
				if (l.IsString()) {
					r.text += std::string(l.GetString()) + "\n";
				}
			}
		}
		return;
	}
	r.text = json::str(c, "text");
}

std::string excerpt(const json::Value &v) {
	std::string s = json::stringify(v);
	return util::ellipsize(s, 140);
}

} // namespace

void appendContentBlock(const json::Value &block, std::vector<Block> &out) {
	if (block.IsString()) {
		Block b;
		b.type = BlockType::Text;
		b.text = block.GetString();
		out.push_back(b);
		return;
	}
	if (!block.IsObject()) {
		return;
	}
	const std::string type = json::str(block, "type");
	Block b;
	b.rawType = type;

	if (type == "text") {
		b.type = BlockType::Text;
		b.text = json::str(block, "text");
		out.push_back(b);
		return;
	}
	if (type == "thinking") {
		b.type = BlockType::Thinking;
		b.text = json::str(block, "thinking");
		if (b.text.empty()) {
			if (const json::Value *sums = json::arr(block, "summaries")) {
				for (const auto &s : sums->GetArray()) {
					b.text += json::str(s, "summary") + "\n";
				}
			}
		}
		out.push_back(b);
		return;
	}
	if (type == "redacted_thinking") {
		b.type = BlockType::Thinking;
		b.text = "(Part of Claude's reasoning was redacted.)";
		out.push_back(b);
		return;
	}
	if (type == "tool_use" || type == "server_tool_use" || type == "mcp_tool_use") {
		b.type = BlockType::ToolUse;
		b.id = json::str(block, "id");
		b.name = json::str(block, "name");
		b.server = json::str(block, "server_name");
		b.serverTool = type != "tool_use";
		if (const json::Value *in = json::get(block, "input")) {
			b.input = json::stringify(*in);
			if (b.input == "{}") {
				b.input.clear();
			}
		}
		b.done = false;
		b.startedMs = util::nowMs();
		out.push_back(b);
		// claude.ai artifacts: show the artifact itself, not just the tool call.
		if (b.name == "artifacts" && !b.input.empty()) {
			const json::Value *in = json::get(block, "input");
			Block a;
			a.type = BlockType::Artifact;
			a.id = json::str(*in, "id");
			a.name = json::str(*in, "title", "Artifact");
			a.mime = json::str(*in, "type");
			a.text = json::str(*in, "content");
			if (a.text.empty()) {
				a.text = json::str(*in, "new_str");
			}
			out.push_back(a);
		}
		return;
	}
	if (type == "tool_result" || type == "mcp_tool_result" || util::endsWith(type, "_tool_result")) {
		b.type = BlockType::ToolResult;
		b.forToolId = json::str(block, "tool_use_id");
		b.name = json::str(block, "name");
		b.error = json::boolean(block, "is_error");
		std::vector<Block> extra;
		const json::Value *content = json::get(block, "content");
		if (content) {
			if (content->IsString()) {
				b.text = content->GetString();
			} else if (content->IsArray()) {
				b.text = textOfContentArray(*content);
				for (const auto &item : content->GetArray()) {
					std::string t = json::str(item, "type");
					if (t == "image") {
						extra.push_back(imageFromSource(item));
					} else if (t == "web_search_tool_result_error") {
						b.error = true;
						b.text = "Search failed: " + json::str(item, "error_code");
					}
				}
			} else if (content->IsObject()) {
				fillServerResult(*content, b, extra);
			}
		}
		markToolDone(out, b.forToolId, b.error);
		out.push_back(b);
		for (Block &e : extra) {
			out.push_back(e);
		}
		return;
	}
	if (type == "image") {
		out.push_back(imageFromSource(block));
		return;
	}
	if (type == "document" || type == "container_upload" || type == "file") {
		b.type = BlockType::File;
		b.name = json::str(block, "title", type == "container_upload" ? "Uploaded file" : "Document");
		std::string fid = json::str(block, "file_id");
		if (!fid.empty()) {
			b.imageKey = "file:" + fid;
		}
		out.push_back(b);
		return;
	}
	b.type = BlockType::Unknown;
	b.text = excerpt(block);
	out.push_back(b);
}

void appendContent(const json::Value &content, std::vector<Block> &out) {
	if (content.IsString()) {
		Block b;
		b.type = BlockType::Text;
		b.text = content.GetString();
		out.push_back(b);
		return;
	}
	if (content.IsArray()) {
		for (const auto &block : content.GetArray()) {
			appendContentBlock(block, out);
		}
	}
}

// ---------------------------------------------------------------------------

struct StreamAssembler::Raw {
	json::Document doc;
	std::string partialJson;
	int displayIndex = -1;
	std::string type;
	bool stopped = false;
};

StreamAssembler::StreamAssembler() {}
StreamAssembler::~StreamAssembler() {}

int StreamAssembler::displayIndexFor(int index) const {
	if (index < 0 || index >= (int)raw_.size() || !raw_[index]) {
		return -1;
	}
	return raw_[index]->displayIndex;
}

void StreamAssembler::startBlock(Message &msg, int index, const json::Value &cb) {
	if (index < 0 || index > 512) {
		return;
	}
	if ((int)raw_.size() <= index) {
		raw_.resize(index + 1);
	}
	auto raw = std::make_unique<Raw>();
	raw->doc.CopyFrom(cb, raw->doc.GetAllocator());
	raw->type = json::str(cb, "type");
	raw->displayIndex = (int)msg.blocks.size();
	appendContentBlock(cb, msg.blocks);
	if (raw->displayIndex < (int)msg.blocks.size()) {
		Block &b = msg.blocks[raw->displayIndex];
		if (b.type == BlockType::Text || b.type == BlockType::Thinking) {
			b.done = false;
		}
		// Streamed tool input starts as "{}"; the real input arrives as deltas.
		if (b.type == BlockType::ToolUse) {
			b.input.clear();
		}
	} else {
		raw->displayIndex = -1;
	}
	raw_[index] = std::move(raw);
}

void StreamAssembler::deltaBlock(Message &msg, int index, const json::Value &delta) {
	if (index < 0 || index >= (int)raw_.size() || !raw_[index]) {
		return;
	}
	Raw &raw = *raw_[index];
	int di = raw.displayIndex;
	Block *b = (di >= 0 && di < (int)msg.blocks.size()) ? &msg.blocks[di] : nullptr;
	auto &alloc = raw.doc.GetAllocator();
	auto appendField = [&](const char *key, const std::string &add) {
		std::string cur = json::str(raw.doc, key);
		cur += add;
		if (raw.doc.HasMember(key)) {
			raw.doc[key].SetString(cur.c_str(), (rapidjson::SizeType)cur.size(), alloc);
		} else {
			raw.doc.AddMember(rapidjson::Value(key, alloc), rapidjson::Value(cur.c_str(), alloc), alloc);
		}
	};
	std::string t = json::str(delta, "type");
	if (t == "text_delta") {
		std::string s = json::str(delta, "text");
		appendField("text", s);
		if (b) {
			b->text += s;
		}
	} else if (t == "thinking_delta") {
		std::string s = json::str(delta, "thinking");
		appendField("thinking", s);
		if (b) {
			b->text += s;
		}
	} else if (t == "thinking_summary_delta") {
		const json::Value *sum = json::obj(delta, "summary");
		std::string s = sum ? json::str(*sum, "summary") : json::str(delta, "summary");
		if (b && !s.empty()) {
			b->text += (b->text.empty() ? "" : "\n") + s;
		}
	} else if (t == "signature_delta") {
		std::string s = json::str(delta, "signature");
		appendField("signature", s);
	} else if (t == "input_json_delta") {
		raw.partialJson += json::str(delta, "partial_json");
		if (b) {
			b->input = raw.partialJson;
		}
	} else if (t == "citations_delta") {
		const json::Value *c = json::get(delta, "citation");
		if (c) {
			if (!raw.doc.HasMember("citations") || !raw.doc["citations"].IsArray()) {
				raw.doc.RemoveMember("citations");
				raw.doc.AddMember("citations", rapidjson::Value(rapidjson::kArrayType), alloc);
			}
			rapidjson::Value copy;
			copy.CopyFrom(*c, alloc);
			raw.doc["citations"].PushBack(copy, alloc);
		}
	}
}

void StreamAssembler::stopBlock(Message &msg, int index) {
	if (index < 0 || index >= (int)raw_.size() || !raw_[index]) {
		return;
	}
	Raw &raw = *raw_[index];
	raw.stopped = true;
	int di = raw.displayIndex;
	Block *b = (di >= 0 && di < (int)msg.blocks.size()) ? &msg.blocks[di] : nullptr;
	if (!raw.partialJson.empty()) {
		json::Document input;
		if (json::parse(input, raw.partialJson)) {
			auto &alloc = raw.doc.GetAllocator();
			rapidjson::Value v;
			v.CopyFrom(input, alloc);
			raw.doc.RemoveMember("input");
			raw.doc.AddMember("input", v, alloc);
			// Artifacts stream their content as tool input.
			if (b && b->type == BlockType::ToolUse && b->name == "artifacts") {
				Block a;
				a.type = BlockType::Artifact;
				a.id = json::str(input, "id");
				a.name = json::str(input, "title", "Artifact");
				a.mime = json::str(input, "type");
				a.text = json::str(input, "content");
				msg.blocks.insert(msg.blocks.begin() + di + 1, a);
				for (auto &r : raw_) {
					if (r && r.get() != &raw && r->displayIndex > di) {
						r->displayIndex++;
					}
				}
				b = &msg.blocks[di];
			}
		}
	}
	if (b && (b->type == BlockType::Text || b->type == BlockType::Thinking)) {
		b->done = true;
	}
}

bool StreamAssembler::apply(Message &msg, const SseEvent &ev) {
	if (ev.event == "ping") {
		return false;
	}
	json::Document doc;
	if (!json::parse(doc, ev.data) || !doc.IsObject()) {
		if (ev.event == "error") {
			result_.error = util::ellipsize(ev.data, 200);
			result_.finished = true;
			return true;
		}
		return false;
	}
	std::string type = json::str(doc, "type", ev.event);
	if (type == "message_start") {
		if (const json::Value *m = json::obj(doc, "message")) {
			result_.messageId = json::str(*m, "id");
			if (result_.messageId.empty()) {
				result_.messageId = json::str(*m, "uuid");
			}
			if (const json::Value *c = json::obj(*m, "container")) {
				result_.containerId = json::str(*c, "id");
			}
		}
		return false;
	}
	if (type == "content_block_start") {
		const json::Value *cb = json::obj(doc, "content_block");
		if (cb) {
			startBlock(msg, (int)json::i64(doc, "index", (int64_t)raw_.size()), *cb);
			msg.version++;
			return true;
		}
		return false;
	}
	if (type == "content_block_delta") {
		const json::Value *d = json::obj(doc, "delta");
		if (d) {
			deltaBlock(msg, (int)json::i64(doc, "index", 0), *d);
			msg.version++;
			return true;
		}
		return false;
	}
	if (type == "content_block_stop") {
		stopBlock(msg, (int)json::i64(doc, "index", 0));
		msg.version++;
		return true;
	}
	if (type == "message_delta") {
		if (const json::Value *d = json::obj(doc, "delta")) {
			std::string sr = json::str(*d, "stop_reason");
			if (!sr.empty()) {
				result_.stopReason = sr;
			}
			if (const json::Value *c = json::obj(*d, "container")) {
				result_.containerId = json::str(*c, "id");
			}
		}
		return false;
	}
	if (type == "message_stop") {
		result_.finished = true;
		return true;
	}
	if (type == "message_limit") {
		if (const json::Value *l = json::get(doc, "message_limit")) {
			result_.limitJson = json::stringify(*l);
		}
		return false;
	}
	if (type == "error") {
		const json::Value *e = json::obj(doc, "error");
		result_.error = e ? json::str(*e, "message", "Unknown error") : json::str(doc, "message", "Unknown error");
		result_.errorType = e ? json::str(*e, "type") : "";
		result_.finished = true;
		return true;
	}
	if (type == "completion" || doc.HasMember("completion")) {
		// Legacy claude.ai text stream.
		std::string piece = json::str(doc, "completion");
		if (legacyBlock_ < 0) {
			Block b;
			b.type = BlockType::Text;
			b.done = false;
			msg.blocks.push_back(b);
			legacyBlock_ = (int)msg.blocks.size() - 1;
		}
		msg.blocks[legacyBlock_].text += piece;
		std::string sr = json::str(doc, "stop_reason");
		if (!sr.empty()) {
			result_.stopReason = sr;
			msg.blocks[legacyBlock_].done = true;
			result_.finished = true;
		}
		msg.version++;
		return true;
	}
	return false;
}

std::string StreamAssembler::rawContentJson() const {
	// Which tool calls got answers inside this message (server tools answer themselves).
	std::vector<std::string> answered;
	for (const auto &r : raw_) {
		if (r && !json::str(r->doc, "tool_use_id").empty()) {
			answered.push_back(json::str(r->doc, "tool_use_id"));
		}
	}
	json::Writer w;
	w.beginArray();
	for (const auto &r : raw_) {
		if (!r || !r->stopped) {
			continue; // cut off mid-block
		}
		const std::string &t = r->type;
		if (t == "text" && json::str(r->doc, "text").empty()) {
			continue;
		}
		if (t == "thinking" && json::str(r->doc, "signature").empty()) {
			continue;
		}
		if (t == "server_tool_use" || t == "mcp_tool_use") {
			std::string id = json::str(r->doc, "id");
			bool found = false;
			for (const auto &a : answered) {
				found = found || a == id;
			}
			if (!found) {
				continue;
			}
		}
		w.value(r->doc);
	}
	w.endArray();
	return w.str();
}

bool StreamAssembler::hasToolUse(const std::string &toolName) const {
	for (const auto &r : raw_) {
		if (r && r->type == "tool_use" && (toolName.empty() || json::str(r->doc, "name") == toolName)) {
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------

bool parseQuestions(const std::string &inputJson, std::vector<Question> &out) {
	json::Document doc;
	if (!json::parse(doc, inputJson) || !doc.IsObject()) {
		return false;
	}
	const json::Value *qs = json::arr(doc, "questions");
	auto parseOne = [&](const json::Value &q) {
		Question question;
		question.question = json::str(q, "question");
		question.header = json::str(q, "header");
		question.multiSelect = json::boolean(q, "multiSelect") || json::boolean(q, "multi_select") ||
		                       json::str(q, "type") == "multi_select";
		if (const json::Value *opts = json::arr(q, "options")) {
			for (const auto &o : opts->GetArray()) {
				QuestionOption opt;
				if (o.IsString()) {
					opt.label = o.GetString();
				} else {
					opt.label = json::str(o, "label");
					opt.description = json::str(o, "description");
					opt.preview = json::str(o, "preview");
				}
				if (!opt.label.empty()) {
					question.options.push_back(opt);
				}
			}
		}
		if (!question.question.empty()) {
			out.push_back(question);
		}
	};
	if (qs) {
		for (const auto &q : qs->GetArray()) {
			parseOne(q);
		}
	} else if (doc.HasMember("question")) {
		parseOne(doc);
	}
	return !out.empty();
}

void applyTaskTool(const Block &toolUse, const std::string &resultText, std::vector<TaskItem> &tasks) {
	json::Document doc;
	if (!json::parse(doc, toolUse.input) || !doc.IsObject()) {
		return;
	}
	std::string n = util::toLower(toolUse.name);
	if (n == "todowrite" || n == "todo_write") {
		const json::Value *todos = json::arr(doc, "todos");
		if (!todos) {
			return;
		}
		tasks.clear();
		int i = 1;
		for (const auto &t : todos->GetArray()) {
			TaskItem item;
			item.id = std::to_string(i++);
			item.content = json::str(t, "content");
			item.activeForm = json::str(t, "activeForm");
			item.status = json::str(t, "status", "pending");
			tasks.push_back(item);
		}
		return;
	}
	if (n == "taskcreate") {
		TaskItem item;
		size_t hash = resultText.find('#');
		item.id = hash != std::string::npos ? std::to_string(atoi(resultText.c_str() + hash + 1))
		                                    : std::to_string(tasks.size() + 1);
		item.content = json::str(doc, "subject");
		item.activeForm = json::str(doc, "activeForm");
		item.status = "pending";
		tasks.push_back(item);
		return;
	}
	if (n == "taskupdate") {
		std::string id = json::str(doc, "taskId");
		for (size_t i = 0; i < tasks.size(); i++) {
			if (tasks[i].id != id) {
				continue;
			}
			std::string status = json::str(doc, "status");
			if (status == "deleted") {
				tasks.erase(tasks.begin() + i);
				return;
			}
			if (!status.empty()) {
				tasks[i].status = status;
			}
			std::string subject = json::str(doc, "subject");
			if (!subject.empty()) {
				tasks[i].content = subject;
			}
			std::string af = json::str(doc, "activeForm");
			if (!af.empty()) {
				tasks[i].activeForm = af;
			}
			return;
		}
	}
}

} // namespace claude
