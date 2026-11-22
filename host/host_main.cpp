// Desktop simulator: renders ClaudeFor3DS screens to PNG files for review (no networking).
//   make host && host/build/claude3ds-sim shots/
#include "app/app.h"
#include "claude/stream.h"
#include "core/config.h"
#include "core/main_queue.h"
#include "platform/gfx.h"
#include "platform/input.h"
#include "platform/system.h"
#include "ui/chat_view.h"
#include "ui/images.h"
#include "ui/screens.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "util/util.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace gfx {
bool savePng(const std::string &path, int zoom);
}
namespace sys {
void hostQueueKeyboard(const std::string &text);
void hostSetClock(int64_t localSeconds);
} // namespace sys

using namespace claude;

static std::string g_out = "shots";

static void frame(uint32_t held = 0, bool touch = false, int tx = 0, int ty = 0) {
	input::setRaw(held, touch, tx, ty, 0, 0);
	input::scan();
	ui::tick();
	MainQueue::drain();
	images::update();
	screens::handleInput();
	gfx::beginScreen(gfx::Screen::Top, theme::bg);
	screens::drawTop();
	gfx::beginScreen(gfx::Screen::Bottom, theme::bg);
	screens::drawBottom();
}

static void settle(int frames = 30) {
	for (int i = 0; i < frames; i++) {
		frame();
		sys::sleepMs(5);
	}
}

static void press(uint32_t key) {
	frame(key);
	frame(0);
}

static void tap(int x, int y) {
	frame(0, true, x, y);
	frame(0, false, x, y);
}

static void shot(const char *name) {
	settle(24);
	gfx::savePng(g_out + "/" + name + ".png", 2);
	printf("wrote %s/%s.png\n", g_out.c_str(), name);
}

static Block text(const std::string &t) {
	Block b;
	b.type = BlockType::Text;
	b.text = t;
	return b;
}

static Message user(const std::string &t) {
	Message m;
	m.id = util::uuid4();
	m.role = Role::User;
	m.blocks.push_back(text(t));
	m.raw = "[{\"type\":\"text\",\"text\":\"x\"}]";
	return m;
}

static Message assistant(std::vector<Block> blocks, Message::State state = Message::State::Done) {
	Message m;
	m.id = util::uuid4();
	m.role = Role::Assistant;
	m.blocks = std::move(blocks);
	m.state = state;
	return m;
}

static Block tool(const std::string &name, const std::string &input, bool done = true, bool server = true) {
	Block b;
	b.type = BlockType::ToolUse;
	b.id = util::uuid4();
	b.name = name;
	b.input = input;
	b.done = done;
	b.serverTool = server;
	return b;
}

static Block thinking(const std::string &t, bool done, int secs = 0) {
	Block b;
	b.type = BlockType::Thinking;
	b.text = t;
	b.done = done;
	b.startedMs = util::nowMs() - secs * 1000 - 100;
	b.endedMs = done ? b.startedMs + secs * 1000 : 0;
	return b;
}

static std::string readFile(const std::string &p) {
	std::ifstream f(p, std::ios::binary);
	std::stringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

static void reset() {
	app::App &a = app::App::get();
	a.conv.clear();
	a.queue.clear();
	a.tasks.clear();
	a.hasQuestion = false;
	a.queuePaused = false;
	chat::clearSelection();
	settle(2);
}

int main(int argc, char **argv) {
	if (argc > 1) {
		g_out = argv[1];
	}
	sys::ensureDir(g_out);
	sys::hostSetClock(1795467000 - 5 * 3600); // 14:30 local in a UTC-5 zone
	util::setUtcOffsetMinutes(-5 * 60);
	if (!gfx::init()) {
		return 1;
	}
	app::App &a = app::App::get();
	config().model = "claude-opus-5-5";
	config().modelName = "Claude Opus 5.5";
	config().effort = Effort::High;
	a.models = {{"claude-fable-5-2", "Claude Fable 5.2", ""},
	            {"claude-opus-5-5", "Claude Opus 5.5", ""},
	            {"claude-sonnet-5-5", "Claude Sonnet 5.5", ""},
	            {"claude-haiku-5-5", "Claude Haiku 5.5", ""}};

	// 1. Sign in.
	a.phase = app::Phase::SignIn;
	shot("01-sign-in");

	// 2. Empty chat.
	a.phase = app::Phase::Main;
	claude::Usage u;
	u.known = true;
	u.windows = {{"Requests / min", 0.18f, util::nowUtc() + 40}, {"Output tokens / min", 0.42f, util::nowUtc() + 1500}};
	a.usage = u;
	reset();
	a.conv.mode = AppMode::Chat;
	a.conv.loaded = true;
	shot("02-empty-chat");

	// 3. Chat with markdown, finished and streaming.
	reset();
	a.conv.title = "Homebrew on the 3DS";
	a.conv.messages.push_back(user("What's the difference between a .cia and a .3dsx?"));
	a.conv.messages.push_back(assistant(
	    {thinking("The user asks about 3DS homebrew formats.", true, 4),
	     tool("web_search", "{\"query\":\"3dsx vs cia homebrew format\"}"),
	     text("Both run homebrew, but they load differently:\n\n"
	          "## .3dsx\n- Launched from the **Homebrew Launcher**\n- Lives in `sdmc:/3ds/`\n- No HOME Menu icon\n\n"
	          "## .cia\n1. Installed with *FBI*\n2. Gets its own **HOME Menu** icon and banner\n3. Can use more memory\n\n"
	          "```sh\nmake cia   # builds the installable title\n```\n"
	          "> Tip: keep both, the `.3dsx` is handy for quick tests.\n\nMore at https://3ds.hacks.guide")}));
	a.conv.messages.push_back(user("Can you check the latest devkitARM release?"));
	a.conv.messages.push_back(assistant({thinking("I should search the web for the newest devkitARM release notes and "
	                                              "compare them with what the user has, then summarise the changes "
	                                              "that matter for building 3DS apps.",
	                                              false, 3)},
	                                    Message::State::Streaming));
	a.conv.busy = true;
	a.contentVersion++;
	a.scrollToEnd = true;
	shot("03-chat-streaming");

	// 4. Claude Code mode with tools, tasks and the activity panel.
	reset();
	a.conv.mode = AppMode::Code;
	a.conv.title = "Fix the failing tests";
	a.conv.messages.push_back(user("The tests in parser.py fail. Fix them and run the suite."));
	Block todo = tool("todo_write",
	                  "{\"todos\":[{\"content\":\"Run the test suite\",\"activeForm\":\"Running the test suite\","
	                  "\"status\":\"completed\"},{\"content\":\"Fix the date parser\",\"activeForm\":\"Fixing the date "
	                  "parser\",\"status\":\"in_progress\"},{\"content\":\"Re-run the tests\",\"activeForm\":\"Re-running "
	                  "the tests\",\"status\":\"pending\"}]}",
	                  true, false);
	a.conv.messages.push_back(assistant(
	    {todo, tool("bash_code_execution", "{\"command\":\"python -m pytest -q\"}"),
	     text("Two tests fail in `parse_date`: it ignores the time zone offset. Fixing that now."),
	     tool("text_editor_code_execution", "{\"command\":\"str_replace\",\"path\":\"/tmp/parser.py\"}"),
	     tool("mcp__github__create_issue", "{\"title\":\"Date parser drops offsets\"}", false),
	     tool("bash_code_execution", "{\"command\":\"python -m pytest -q\"}", false)},
	    Message::State::Streaming));
	a.conv.busy = true;
	a.queue = {"Also add a test for leap years", "Then summarise the change"};
	a.onTurnUpdate();
	a.scrollToEnd = true;
	shot("04-code-activity");
	press(input::R);
	shot("05-code-queue");
	press(input::R);
	shot("06-code-tasks");
	press(input::R);

	// 5. Question.
	reset();
	a.conv.mode = AppMode::Chat;
	a.conv.messages.push_back(user("Help me pick a name for my 3DS app"));
	Block ask = tool("ask_user_question",
	                 "{\"questions\":[{\"question\":\"Which style of name do you prefer?\",\"header\":\"Style\","
	                 "\"multiSelect\":false,\"options\":[{\"label\":\"Playful\",\"description\":\"Fun and memorable, "
	                 "like Pocket Claude\"},{\"label\":\"Descriptive\",\"description\":\"Says what it does, like "
	                 "ClaudeFor3DS\"},{\"label\":\"Short\",\"description\":\"One word, easy to type\"}]}]}",
	                 true, false);
	a.conv.messages.push_back(assistant({text("Happy to help! First, one quick question:"), ask}));
	PendingQuestion q;
	q.toolUseId = ask.id;
	parseQuestions(ask.input, q.questions);
	a.onQuestion(q);
	settle(3);
	press(input::DDOWN);
	shot("07-question");
	press(input::B);

	// 6. Menu and pickers.
	reset();
	a.conv.messages.push_back(user("Hi!"));
	a.conv.messages.push_back(assistant({text("Hello! How can I help you today?")}));
	press(input::X);
	shot("08-menu");
	press(input::B);
	tap(160, 45);
	shot("09-model-picker");
	press(input::B);

	// 7. Images and files.
	reset();
	std::string png = readFile("resources/banner.png");
	Block img;
	img.type = BlockType::Image;
	img.mime = "image/png";
	img.text = util::base64Encode((const uint8_t *)png.data(), png.size());
	img.name = "chart.png";
	Block unknown;
	unknown.type = BlockType::Unknown;
	unknown.rawType = "container_upload_v2";
	unknown.text = "{\"type\":\"container_upload_v2\"}";
	Block art;
	art.type = BlockType::Artifact;
	art.name = "Snake game";
	art.mime = "text/html";
	art.text = "<!DOCTYPE html>\n<html>\n<body>\n<canvas id=\"game\"></canvas>\n<script src=\"snake.js\"></script>\n</body>\n</html>";
	a.conv.messages.push_back(user("Make me a chart and a tiny game"));
	a.conv.messages.push_back(assistant({text("Here's the chart I made:"), img, art, unknown}));
	settle(40);
	press(input::DUP);
	shot("10-rich-content");
	press(input::A);
	shot("11-message-actions");
	press(input::DDOWN);
	press(input::A);
	settle(10);
	shot("12-image-viewer");
	press(input::B);
	press(input::B);

	// 8. Error.
	reset();
	a.conv.messages.push_back(user("Hello?"));
	Message err = assistant({}, Message::State::Error);
	err.error = "No internet connection";
	a.conv.messages.push_back(err);
	a.contentVersion++;
	shot("13-error");
	return 0;
}
