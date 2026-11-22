#include "ui/screens.h"

#include "app/app.h"
#include "claude/stream.h"
#include "core/log.h"
#include "platform/gfx.h"
#include "platform/input.h"
#include "platform/system.h"
#include "ui/chat_view.h"
#include "ui/images.h"
#include "ui/markdown.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "util/json.h"
#include "util/util.h"

#include <rapidjson/prettywriter.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace screens {

using namespace gfx;
using app::App;
using claude::Block;
using claude::BlockType;
using claude::Message;
using ui::Icon;

namespace {

bool g_exit = false;

// ---------------------------------------------------------------------------------
// Overlays
// ---------------------------------------------------------------------------------

class Overlay {
  public:
	virtual ~Overlay() = default;
	virtual void input(const input::State &in) {}
	// Draws on the top screen; return false to keep the conversation visible.
	virtual bool drawTop() { return false; }
	virtual void drawBottom() = 0;
	// Covers the whole top screen, status bar included.
	virtual bool fullscreen() const { return false; }
	bool closed = false;
};

std::vector<std::unique_ptr<Overlay>> g_overlays;

void push(Overlay *o) { g_overlays.emplace_back(o); }

void sheetHeader(const std::string &title, bool closeButton = true) {
	rect(0, 0, BOTTOM_W, 30, theme::bgDeep);
	rect(0, 30, BOTTOM_W, 1, theme::border);
	textFit(12, 7, theme::label, theme::text, title, BOTTOM_W - 50, true);
}

bool closeButton() { return ui::iconButton(BOTTOM_W - 30, 2, 26, Icon::Close, theme::textDim); }

// A scrolling list of rows with D-pad focus and touch.
struct ListView {
	float scroll = 0;
	int focus = 0;

	// Returns the index tapped (or activated with A), -1 otherwise.
	template <typename DrawRow>
	int run(float y, float h, int count, float rowH, DrawRow drawRow, bool keys = true) {
		const input::State &in = input::get();
		int activated = -1;
		if (keys && count > 0) {
			if (in.repeat & input::UP) {
				focus = (focus - 1 + count) % count;
			}
			if (in.repeat & input::DOWN) {
				focus = (focus + 1) % count;
			}
			if (in.down & input::A) {
				activated = focus;
			}
		}
		focus = std::max(0, std::min(count - 1, focus));
		float content = count * rowH;
		float maxScroll = std::max(0.0f, content - h);
		if (in.dragging && in.touchStartY >= y && in.touchStartY < y + h) {
			scroll -= in.dragDY;
		} else if (count > 0) {
			float fy = focus * rowH;
			if (fy < scroll) {
				scroll += (fy - scroll) * 0.4f;
			} else if (fy + rowH > scroll + h) {
				scroll += (fy + rowH - h - scroll) * 0.4f;
			}
		}
		scroll = std::max(0.0f, std::min(maxScroll, scroll));
		clip(0, y, BOTTOM_W, h);
		for (int i = 0; i < count; i++) {
			float ry = y + i * rowH - scroll;
			if (ry + rowH < y || ry > y + h) {
				continue;
			}
			bool pressed = ui::pressedIn(0, std::max(ry, y), BOTTOM_W - 8, rowH);
			if (i == focus || pressed) {
				roundRect(6, ry + 1, BOTTOM_W - 16, rowH - 2, 7, pressed ? theme::panelHi : theme::selection);
			}
			drawRow(i, ry, i == focus);
			if (ry >= y - rowH / 2 && ui::tappedIn(0, std::max(ry, y), BOTTOM_W - 8, std::min(rowH, y + h - ry))) {
				focus = i;
				activated = i;
			}
		}
		noClip();
		ui::scrollbar(BOTTOM_W - 6, y + 2, h - 4, h, content, scroll);
		return activated;
	}
};

struct PickItem {
	std::string label;
	std::string detail;
	Icon icon = Icon::Dot;
	bool showIcon = false;
	bool checked = false;
	bool danger = false;
	bool keepOpen = false;
	std::function<void()> action;
	std::string topText; // shown on the top screen while focused
};

class Picker : public Overlay {
  public:
	Picker(std::string title, std::vector<PickItem> items, int focus = 0) : title_(std::move(title)), items_(std::move(items)) {
		list_.focus = focus;
	}
	std::function<void(int)> onY;
	std::string yHint;
	std::function<void()> topExtra;

	void input(const input::State &in) override {
		if (in.down & input::B) {
			closed = true;
		}
		if ((in.down & input::Y) && onY && !items_.empty()) {
			onY(list_.focus);
		}
	}

	bool drawTop() override {
		if (items_.empty() || list_.focus >= (int)items_.size()) {
			return false;
		}
		const PickItem &it = items_[list_.focus];
		if (it.topText.empty() && !topExtra) {
			return false;
		}
		rect(0, chat::VIEW_TOP, TOP_W, SCREEN_H - chat::VIEW_TOP, theme::bg);
		if (topExtra) {
			topExtra();
			return true;
		}
		textFit(14, 30, theme::title, theme::text, it.label, TOP_W - 28, true);
		ui::paragraph(14, 54, TOP_W - 28, theme::label, theme::textDim, it.topText, 10);
		return true;
	}

	void drawBottom() override {
		sheetHeader(title_);
		if (closeButton()) {
			closed = true;
		}
		float rowH = 30;
		int hit = list_.run(32, 190, (int)items_.size(), rowH, [&](int i, float y, bool focused) {
			const PickItem &it = items_[i];
			float x = 14;
			if (it.showIcon) {
				ui::icon(it.icon, x + 7, y + rowH / 2, 13, it.danger ? theme::red : theme::textDim);
				x += 22;
			}
			Color c = it.danger ? theme::red : theme::text;
			if (it.detail.empty()) {
				textFit(x, y + (rowH - lineHeight(theme::label)) / 2, theme::label, c, it.label, BOTTOM_W - x - 40);
			} else {
				textFit(x, y + 2, theme::label, c, it.label, BOTTOM_W - x - 40);
				textFit(x, y + 16, theme::tiny, theme::textFaint, it.detail, BOTTOM_W - x - 40);
			}
			if (it.checked) {
				ui::icon(Icon::Check, BOTTOM_W - 26, y + rowH / 2, 12, theme::accent);
			}
		});
		if (items_.empty()) {
			text(14, 44, theme::label, theme::textFaint, "Nothing here yet.");
		}
		if (hit >= 0 && hit < (int)items_.size()) {
			PickItem it = items_[hit];
			if (!it.keepOpen) {
				closed = true;
			}
			if (it.action) {
				it.action();
			}
		}
		ui::hints("{A} Choose  {B} Back" + (yHint.empty() ? std::string() : "  {Y} " + yHint));
	}

	std::vector<PickItem> &items() { return items_; }

  private:
	std::string title_;
	std::vector<PickItem> items_;
	ListView list_;
};

class Confirm : public Overlay {
  public:
	Confirm(std::string title, std::string message, std::string yes, std::function<void()> onYes, bool danger = true)
	    : title_(std::move(title)), message_(std::move(message)), yes_(std::move(yes)), onYes_(std::move(onYes)),
	      danger_(danger) {}
	void input(const input::State &in) override {
		if (in.down & input::B) {
			closed = true;
		}
		if (in.down & input::A) {
			closed = true;
			onYes_();
		}
	}
	void drawBottom() override {
		rect(0, 0, BOTTOM_W, SCREEN_H, theme::bg);
		roundRect(16, 40, BOTTOM_W - 32, 150, 12, theme::panel);
		textFit(30, 52, theme::title, theme::text, title_, BOTTOM_W - 60, true);
		ui::paragraph(30, 80, BOTTOM_W - 60, theme::label, theme::textDim, message_, 4);
		if (ui::button(30, 146, 120, 32, "Cancel", ui::Style::Secondary)) {
			closed = true;
		}
		if (ui::button(170, 146, 120, 32, yes_, danger_ ? ui::Style::Danger : ui::Style::Primary)) {
			closed = true;
			onYes_();
		}
		ui::hints("{A} " + yes_ + "  {B} Cancel");
	}

  private:
	std::string title_, message_, yes_;
	std::function<void()> onYes_;
	bool danger_;
};

// Scrollable Markdown on the top screen.
class TextViewer : public Overlay {
  public:
	TextViewer(std::string title, std::string markdown) : title_(std::move(title)), text_(std::move(markdown)) {}
	void input(const input::State &in) override {
		if (in.down & input::B) {
			closed = true;
		}
		if (in.repeat & input::UP) {
			target_ -= 40;
		}
		if (in.repeat & input::DOWN) {
			target_ += 40;
		}
		if (in.repeat & input::LEFT) {
			target_ -= 180;
		}
		if (in.repeat & input::RIGHT) {
			target_ += 180;
		}
		int dy = std::abs(in.circleY) > 18 ? in.circleY : (std::abs(in.cstickY) > 18 ? in.cstickY : 0);
		target_ -= dy * 0.06f;
		if ((in.down & input::X) && !layout_.links.empty()) {
			openLinks();
		}
	}
	bool drawTop() override {
		if (layoutScale_ != theme::body()) {
			layout_ = md::build(text_, TOP_W - 28, theme::body());
			layoutScale_ = theme::body();
		}
		rect(0, chat::VIEW_TOP, TOP_W, SCREEN_H - chat::VIEW_TOP, theme::bg);
		rect(0, chat::VIEW_TOP, TOP_W, 20, theme::panel);
		textFit(12, chat::VIEW_TOP + 3, theme::small, theme::text, title_, TOP_W - 24, true);
		float top = chat::VIEW_TOP + 22, h = SCREEN_H - top;
		float maxScroll = std::max(0.0f, layout_.height + 12 - h);
		target_ = std::max(0.0f, std::min(maxScroll, target_));
		scroll_ += (target_ - scroll_) * 0.35f;
		clip(0, top, TOP_W, h);
		md::draw(layout_, 14, top + 6 - std::floor(scroll_), top, SCREEN_H, theme::text);
		noClip();
		ui::scrollbar(395, top + 2, h - 4, h, layout_.height + 12, scroll_);
		return true;
	}
	void drawBottom() override {
		sheetHeader(title_);
		if (closeButton()) {
			closed = true;
		}
		const input::State &in = input::get();
		if (in.dragging && in.touchStartY > 32 && in.touchStartY < 180) {
			target_ -= in.dragDY * 1.5f;
		}
		ui::icon(Icon::ChevronUp, 160, 70, 18, theme::textFaint);
		std::string tip = "Drag here to scroll";
		text(160 - textWidth(tip, theme::small) / 2, 98, theme::small, theme::textFaint, tip);
		ui::icon(Icon::ChevronDown, 160, 130, 18, theme::textFaint);
		float bx = 16;
		if (!layout_.links.empty()) {
			if (ui::button(bx, 186, 140, 30, util::format("Links (%d)", (int)layout_.links.size()), ui::Style::Secondary,
			               false, Icon::Link, true)) {
				openLinks();
			}
			bx += 152;
		}
		if (ui::button(bx, 186, 136, 30, "Close", ui::Style::Secondary)) {
			closed = true;
		}
		ui::hints(layout_.links.empty() ? "{B} Close   Up/Down Scroll" : "{X} Links  {B} Close   Up/Down Scroll");
	}

  private:
	void openLinks();
	std::string title_, text_;
	md::Layout layout_;
	float layoutScale_ = 0;
	float scroll_ = 0, target_ = 0;
};

// QR codes for links, so they can be opened on a phone.
class LinksOverlay : public Overlay {
  public:
	explicit LinksOverlay(std::vector<std::string> links) : links_(std::move(links)) {}
	void input(const input::State &in) override {
		if (in.down & input::B) {
			closed = true;
		}
	}
	bool drawTop() override {
		rect(0, chat::VIEW_TOP, TOP_W, SCREEN_H - chat::VIEW_TOP, theme::bg);
		if (links_.empty()) {
			return true;
		}
		const std::string &url = links_[std::min((int)links_.size() - 1, list_.focus)];
		gfx::Image *qr = images::qrCode(url);
		float size = 170;
		if (qr) {
			roundRect(16, 32, size + 8, size + 8, 6, rgb(0xFFFFFF));
			drawImage(qr, 20, 36, size, size);
		} else {
			text(20, 40, theme::small, theme::red, "This link is too long for a QR code.");
		}
		text(206, 36, theme::label, theme::text, "Scan to open");
		text(206, 52, theme::small, theme::textDim, "on your phone");
		ui::paragraph(206, 76, TOP_W - 216, theme::small, theme::link, url, 9);
		return true;
	}
	void drawBottom() override {
		sheetHeader("Links");
		if (closeButton()) {
			closed = true;
		}
		list_.run(32, 190, (int)links_.size(), 28, [&](int i, float y, bool) {
			ui::icon(Icon::Link, 20, y + 14, 12, theme::link);
			textFit(34, y + (28 - lineHeight(theme::small)) / 2, theme::small, theme::text, links_[i], BOTTOM_W - 50);
		});
		ui::hints("Up/Down Pick a link  {B} Close");
	}

  private:
	std::vector<std::string> links_;
	ListView list_;
};

void TextViewer::openLinks() { push(new LinksOverlay(layout_.links)); }

class ImageViewer : public Overlay {
  public:
	explicit ImageViewer(std::vector<Block> blocks, int start = 0) : blocks_(std::move(blocks)), index_(start) {}
	void input(const input::State &in) override {
		if (in.down & input::B) {
			closed = true;
		}
		if (in.down & input::X) {
			zoom_ = std::min(4.0f, zoom_ * 1.4f);
		}
		if (in.down & input::Y) {
			zoom_ = std::max(1.0f, zoom_ / 1.4f);
		}
		if (in.down & input::A) {
			zoom_ = 1;
			panX_ = panY_ = 0;
		}
		if ((in.down & input::R) && index_ + 1 < (int)blocks_.size()) {
			index_++;
			zoom_ = 1;
			panX_ = panY_ = 0;
		}
		if ((in.down & input::L) && index_ > 0) {
			index_--;
			zoom_ = 1;
			panX_ = panY_ = 0;
		}
		if (std::abs(in.circleX) > 18) {
			panX_ -= in.circleX * 0.05f;
		}
		if (std::abs(in.circleY) > 18) {
			panY_ += in.circleY * 0.05f;
		}
		if (in.held & input::DLEFT) {
			panX_ += 3;
		}
		if (in.held & input::DRIGHT) {
			panX_ -= 3;
		}
		if (in.held & input::DUP) {
			panY_ += 3;
		}
		if (in.held & input::DDOWN) {
			panY_ -= 3;
		}
	}
	bool drawTop() override {
		// The whole area under the status bar (which stays visible).
		const float top = chat::VIEW_TOP, areaH = SCREEN_H - top;
		rect(0, top, TOP_W, areaH, rgb(0x0E0E0D));
		if (blocks_.empty()) {
			return true;
		}
		const images::Entry &e = images::get(blocks_[index_]);
		if (e.state == images::State::Ready) {
			float iw = (float)imageWidth(e.image), ih = (float)imageHeight(e.image);
			float fit = std::min(TOP_W / iw, areaH / ih);
			float s = fit * zoom_;
			float dw = iw * s, dh = ih * s;
			float maxPX = std::max(0.0f, (dw - TOP_W) / 2), maxPY = std::max(0.0f, (dh - areaH) / 2);
			panX_ = std::max(-maxPX, std::min(maxPX, panX_));
			panY_ = std::max(-maxPY, std::min(maxPY, panY_));
			clip(0, top, TOP_W, areaH);
			drawImage(e.image, (TOP_W - dw) / 2 + panX_, top + (areaH - dh) / 2 + panY_, dw, dh);
			noClip();
		} else if (e.state == images::State::Loading) {
			ui::spinner(200, 120, 14, theme::textFaint);
		} else {
			ui::icon(Icon::Image, 200, 110, 30, theme::textFaint);
			std::string msg = e.state == images::State::NotImage ? "This file is not an image" : e.error;
			text(200 - textWidth(msg, theme::small) / 2, 134, theme::small, theme::textDim, msg);
		}
		return true;
	}
	void drawBottom() override {
		sheetHeader(util::format("Image %d of %d", index_ + 1, (int)blocks_.size()));
		if (closeButton()) {
			closed = true;
		}
		const input::State &in = input::get();
		if (in.dragging && in.touchStartY > 32 && in.touchStartY < 150) {
			panX_ += in.dragDX * 1.5f;
			panY_ += in.dragDY * 1.5f;
		}
		const images::Entry &e = images::get(blocks_[index_]);
		std::string info = e.name.empty() ? "Image" : e.name;
		if (e.width) {
			info += util::format("  \xC2\xB7  %dx%d", e.width, e.height);
		}
		textFit(14, 40, theme::small, theme::textDim, info, BOTTOM_W - 28);
		text(14, 58, theme::tiny, theme::textFaint, "Drag here to move around the image");
		float y = 150;
		if (ui::button(14, y, 56, 30, "", ui::Style::Secondary, false, Icon::ChevronLeft, true) && index_ > 0) {
			index_--;
			zoom_ = 1;
		}
		if (ui::button(76, y, 56, 30, "-", ui::Style::Secondary)) {
			zoom_ = std::max(1.0f, zoom_ / 1.4f);
		}
		if (ui::button(138, y, 56, 30, "Fit", ui::Style::Secondary)) {
			zoom_ = 1;
			panX_ = panY_ = 0;
		}
		if (ui::button(200, y, 56, 30, "+", ui::Style::Secondary)) {
			zoom_ = std::min(4.0f, zoom_ * 1.4f);
		}
		if (ui::button(262, y, 44, 30, "", ui::Style::Secondary, false, Icon::ChevronRight, true) &&
		    index_ + 1 < (int)blocks_.size()) {
			index_++;
			zoom_ = 1;
		}
		if (ui::button(14, 188, 140, 30, "Save to SD", ui::Style::Secondary, false, Icon::Download, true)) {
			App::get().saveToSd(blocks_[index_]);
		}
		if (ui::button(166, 188, 140, 30, "Close", ui::Style::Secondary)) {
			closed = true;
		}
		ui::hints("{X}{Y} Zoom  {A} Fit  {L}{R} Prev/Next  {B} Close");
	}

  private:
	std::vector<Block> blocks_;
	int index_;
	float zoom_ = 1, panX_ = 0, panY_ = 0;
};

class QuestionSheet : public Overlay {
  public:
	explicit QuestionSheet(claude::PendingQuestion q) : q_(std::move(q)) { answers_.resize(q_.questions.size()); }

	void input(const input::State &in) override {
		const claude::Question &qq = q_.questions[step_];
		int count = (int)qq.options.size() + 1; // + "type my own"
		if (in.repeat & input::UP) {
			focus_ = (focus_ - 1 + count) % count;
		}
		if (in.repeat & input::DOWN) {
			focus_ = (focus_ + 1) % count;
		}
		if (in.down & input::A) {
			choose(focus_);
		}
		if ((in.down & input::X) && qq.multiSelect) {
			submitMulti();
		}
		if (in.down & input::B) {
			if (step_ > 0) {
				step_--;
				focus_ = 0;
			} else {
				closed = true; // the question stays pending; reopen from the composer banner
			}
		}
	}

	bool drawTop() override {
		const claude::Question &qq = q_.questions[step_];
		rect(0, chat::VIEW_TOP, TOP_W, SCREEN_H - chat::VIEW_TOP, theme::bg);
		float y = chat::VIEW_TOP + 10;
		ui::claudeLogo(20, y + 7, 7, theme::accent);
		std::string head = qq.header.empty() ? "Question" : qq.header;
		float hw = textWidth(head, theme::small) + 14;
		roundRect(32, y, hw, 15, 7.5f, theme::selection);
		text(39, y + 1, theme::small, theme::accent, head);
		if (q_.questions.size() > 1) {
			std::string n = util::format("%d of %d", step_ + 1, (int)q_.questions.size());
			text(TOP_W - 14 - textWidth(n, theme::small), y + 1, theme::small, theme::textFaint, n);
		}
		y += 22;
		y += ui::paragraph(14, y, TOP_W - 28, theme::title * 0.85f, theme::text, qq.question, 3) + 8;
		if (focus_ < (int)qq.options.size()) {
			const claude::QuestionOption &opt = qq.options[focus_];
			roundRect(10, y, TOP_W - 20, SCREEN_H - y - 8, 8, theme::panel);
			textFit(20, y + 6, theme::label, theme::text, opt.label, TOP_W - 40, true);
			float py = y + 24;
			py += ui::paragraph(20, py, TOP_W - 40, theme::small, theme::textDim, opt.description, 4);
			if (!opt.preview.empty()) {
				md::Layout l = md::build("```\n" + opt.preview + "\n```", TOP_W - 40, theme::small);
				clip(10, py, TOP_W - 20, SCREEN_H - py - 10);
				md::draw(l, 20, py + 4, py, SCREEN_H, theme::text);
				noClip();
			}
		} else {
			roundRect(10, y, TOP_W - 20, 44, 8, theme::panel);
			text(20, y + 6, theme::label, theme::text, "Type your own answer");
			text(20, y + 24, theme::small, theme::textDim, "Opens the keyboard. Claude gets exactly what you write.");
		}
		return true;
	}

	void drawBottom() override {
		const claude::Question &qq = q_.questions[step_];
		sheetHeader(qq.multiSelect ? "Pick one or more" : "Pick an answer");
		if (closeButton()) {
			closed = true;
		}
		int count = (int)qq.options.size();
		float rowH = std::min(34.0f, 150.0f / (count + 1));
		float y = 36;
		for (int i = 0; i < count; i++) {
			bool on = std::find(multi_.begin(), multi_.end(), i) != multi_.end();
			std::string label = qq.options[i].label;
			float bx = 10, bw = BOTTOM_W - 20;
			if (ui::button(bx, y, bw, rowH - 4, "", i == focus_ ? ui::Style::Primary : ui::Style::Secondary, false)) {
				focus_ = i;
				choose(i);
			}
			Color fg = i == focus_ ? rgb(0xFFFFFF) : theme::text;
			float tx = bx + 12;
			if (qq.multiSelect) {
				roundRectBorder(tx, y + (rowH - 4) / 2 - 6, 12, 12, 3, on ? theme::green : theme::bgDeep, fg);
				if (on) {
					ui::icon(Icon::Check, tx + 6, y + (rowH - 4) / 2, 9, theme::bgDeep);
				}
				tx += 20;
			} else {
				char num[12];
				snprintf(num, sizeof(num), "%d", i + 1);
				text(tx, y + (rowH - 4 - lineHeight(theme::small)) / 2, theme::small, i == focus_ ? fg : theme::textFaint, num);
				tx += 14;
			}
			textFit(tx, y + (rowH - 4 - lineHeight(theme::label)) / 2, theme::label, fg, label, bx + bw - tx - 8);
			y += rowH;
		}
		if (ui::button(10, y, BOTTOM_W - 20, rowH - 4, "Type your own answer",
		               focus_ == count ? ui::Style::Primary : ui::Style::Ghost, false, Icon::Keyboard, true)) {
			focus_ = count;
			choose(count);
		}
		y += rowH;
		if (qq.multiSelect) {
			if (ui::button(BOTTOM_W - 120, 190, 110, 28, util::format("Submit (%d)", (int)multi_.size()),
			               multi_.empty() ? ui::Style::Secondary : ui::Style::Primary)) {
				submitMulti();
			}
		}
		ui::hints(qq.multiSelect ? "{A} Toggle  {X} Submit  {B} Later" : "{A} Choose  {B} Later");
	}

	void resume(const std::vector<std::string> &answers, int step) {
		answers_ = answers;
		step_ = step;
	}

  private:
	void choose(int i) {
		const claude::Question &qq = q_.questions[step_];
		if (i >= (int)qq.options.size()) {
			sys::KeyboardOptions kb;
			kb.hint = util::ellipsize(qq.question, 60);
			kb.okLabel = "Answer";
			kb.multiline = true;
			// The sheet may be gone by the time the keyboard closes; answer through the app.
			claude::PendingQuestion q = q_;
			std::vector<std::string> answers = answers_;
			int step = step_;
			closed = true;
			ui::keyboardLater(kb, [q, answers, step](const std::string &text) mutable {
				std::string t = util::trim(text);
				if (t.empty()) {
					return;
				}
				answers[step] = t;
				if (step + 1 < (int)q.questions.size()) {
					QuestionSheet *next = new QuestionSheet(q);
					next->resume(answers, step + 1);
					push(next);
				} else {
					App::get().answerQuestion(answers);
				}
			});
			return;
		}
		if (qq.multiSelect) {
			auto it = std::find(multi_.begin(), multi_.end(), i);
			if (it == multi_.end()) {
				multi_.push_back(i);
			} else {
				multi_.erase(it);
			}
			return;
		}
		record(qq.options[i].label);
	}

	void submitMulti() {
		const claude::Question &qq = q_.questions[step_];
		if (multi_.empty()) {
			return;
		}
		std::sort(multi_.begin(), multi_.end());
		std::string joined;
		for (int i : multi_) {
			joined += (joined.empty() ? "" : ", ") + qq.options[i].label;
		}
		record(joined);
	}

	void record(const std::string &answer) {
		answers_[step_] = answer;
		multi_.clear();
		focus_ = 0;
		if (step_ + 1 < (int)q_.questions.size()) {
			step_++;
			return;
		}
		closed = true;
		App::get().answerQuestion(answers_);
	}

	claude::PendingQuestion q_;
	std::vector<std::string> answers_;
	std::vector<int> multi_;
	int step_ = 0;
	int focus_ = 0;
};

class UsageOverlay : public Overlay {
  public:
	void input(const input::State &in) override {
		if (in.down & (input::B | input::A)) {
			closed = true;
		}
	}
	bool drawTop() override {
		const claude::Usage &u = App::get().usage;
		rect(0, chat::VIEW_TOP, TOP_W, SCREEN_H - chat::VIEW_TOP, theme::bg);
		textBold(14, chat::VIEW_TOP + 8, theme::title, theme::text, "Usage limits");
		float y = chat::VIEW_TOP + 34;
		if (!u.known) {
			ui::paragraph(14, y, TOP_W - 28, theme::label, theme::textDim,
			              "Usage shows up after your first message: it comes from the rate-limit information the API "
			              "sends with every reply.",
			              5);
			return true;
		}
		for (const claude::UsageWindow &w : u.windows) {
			text(14, y, theme::label, theme::text, w.label);
			std::string pct = util::format("%d%% used", (int)std::round(w.used * 100));
			text(TOP_W - 14 - textWidth(pct, theme::label), y, theme::label, theme::textDim, pct);
			ui::progressBar(14, y + 17, TOP_W - 28, 7, w.used, w.used > 0.85f ? theme::red : theme::accent, theme::panelHi);
			if (w.resetsAt) {
				std::string r = "Resets " + util::relativeFuture(w.resetsAt) + " (" + util::localClock(w.resetsAt) + ")";
				text(14, y + 26, theme::small, theme::textFaint, r);
			}
			y += 46;
		}
		if (!u.note.empty()) {
			text(14, y, theme::small, theme::textFaint, u.note);
		}
		return true;
	}
	void drawBottom() override {
		sheetHeader("Usage limits");
		if (closeButton()) {
			closed = true;
		}
		ui::paragraph(14, 44, BOTTOM_W - 28, theme::small, theme::textDim,
		              "The meter in the status bar shows how much of your tightest limit is left, and when it resets.", 4);
		if (ui::button(90, 186, 140, 30, "Done", ui::Style::Primary)) {
			closed = true;
		}
		ui::hints("{B} Close");
	}
};

// ---------------------------------------------------------------------------------
// Builders for pickers
// ---------------------------------------------------------------------------------

void openModelPicker() {
	App &a = App::get();
	std::vector<PickItem> items;
	int focus = 0;
	for (size_t i = 0; i < a.models.size(); i++) {
		const claude::ModelInfo &m = a.models[i];
		PickItem it;
		it.label = App::shortModelName(m.name);
		it.detail = m.id;
		it.checked = m.id == config().model;
		it.topText = "Model ID: " + m.id + "\n\nNew messages use this model. It applies to both Chat and Claude Code modes.";
		claude::ModelInfo copy = m;
		it.action = [copy]() {
			App::get().setModel(copy);
			ui::toast("Model: " + App::shortModelName(copy.name));
		};
		if (it.checked) {
			focus = (int)i;
		}
		items.push_back(it);
	}
	if (items.empty()) {
		PickItem it;
		it.label = "Loading models...";
		it.detail = "Try again in a moment";
		items.push_back(it);
	}
	push(new Picker("Model", items, focus));
}

void openEffortPicker() {
	static const char *details[] = {"Fastest, uses the least of your limits", "Balanced speed and depth",
	                                "Thinks harder on difficult tasks (default)", "Maximum effort for the hardest problems"};
	std::vector<PickItem> items;
	for (int i = 0; i < 4; i++) {
		PickItem it;
		it.label = effortName((Effort)i);
		it.detail = details[i];
		it.checked = (int)config().effort == i;
		it.topText = std::string(details[i]) +
		             ".\n\nEffort controls how much Claude thinks and how thorough it is. Models that don't support it "
		             "ignore it.";
		it.action = [i]() {
			App::get().setEffort((Effort)i);
			ui::toast(std::string("Effort: ") + effortName((Effort)i));
		};
		items.push_back(it);
	}
	push(new Picker("Effort", items, (int)config().effort));
}

void openConversations() {
	App &a = App::get();
	a.refreshConversations();
	std::vector<PickItem> items;
	PickItem fresh;
	fresh.label = a.conv.mode == AppMode::Code ? "New Claude Code session" : "New chat";
	fresh.icon = Icon::Plus;
	fresh.showIcon = true;
	fresh.action = []() { App::get().newConversation(); };
	items.push_back(fresh);
	for (const claude::ConversationSummary &s : a.conversations) {
		PickItem it;
		it.label = s.title.empty() ? "Untitled" : s.title;
		it.detail = util::relativePast(s.updated);
		it.icon = a.conv.mode == AppMode::Code ? Icon::Code : Icon::Chat;
		it.showIcon = true;
		it.checked = s.id == a.conv.id;
		it.topText = s.subtitle.empty() ? "(no preview)" : s.subtitle;
		std::string id = s.id;
		it.action = [id]() { App::get().openConversation(id); };
		items.push_back(it);
	}
	Picker *p = new Picker(a.conv.mode == AppMode::Code ? "Claude Code sessions" : "Chats", items);
	p->yHint = "Delete";
	p->onY = [p](int index) {
		if (index <= 0 || index - 1 >= (int)App::get().conversations.size()) {
			return;
		}
		std::string id = App::get().conversations[index - 1].id;
		std::string title = p->items()[index].label;
		push(new Confirm("Delete conversation?", "\"" + util::ellipsize(title, 60) + "\" will be removed from the SD card.",
		                 "Delete", [p, id]() {
			                 App::get().deleteConversation(id);
			                 p->closed = true;
			                 ui::toast("Deleted");
		                 }));
	};
	push(p);
}

void openQueue() {
	App &a = App::get();
	std::vector<PickItem> items;
	if (a.queuePaused && !a.queue.empty()) {
		PickItem resume;
		resume.label = "Resume sending";
		resume.icon = Icon::Play;
		resume.showIcon = true;
		resume.action = []() { App::get().resumeQueue(); };
		items.push_back(resume);
	}
	for (size_t i = 0; i < a.queue.size(); i++) {
		PickItem it;
		it.label = util::ellipsize(a.queue[i], 60);
		it.detail = "A: remove from the queue";
		it.icon = Icon::Trash;
		it.showIcon = true;
		it.topText = a.queue[i];
		it.action = [i]() {
			App::get().removeQueued(i);
			ui::toast("Removed from the queue");
		};
		items.push_back(it);
	}
	if (!a.queue.empty()) {
		PickItem clear;
		clear.label = "Clear the queue";
		clear.danger = true;
		clear.icon = Icon::Close;
		clear.showIcon = true;
		clear.action = []() { App::get().clearQueue(); };
		items.push_back(clear);
	}
	push(new Picker(util::format("Queue (%d)", (int)a.queue.size()), items));
}

std::string tzLabel() {
	int m = util::utcOffsetMinutes();
	return util::format("UTC%c%d:%02d", m < 0 ? '-' : '+', std::abs(m) / 60, std::abs(m) % 60);
}

std::string aboutText() {
	return "# ClaudeFor3DS " APP_VERSION_STR "\n\nClaude and Claude Code for the Nintendo 3DS, using the official "
	       "Anthropic API with your own API key.\n\n"
	       "## Controls\n- **Y** or tap the message box: type a message (queued while Claude works)\n"
	       "- **B**: stop Claude\n- **X** or the menu button: menu\n- **SELECT**: switch Chat / Claude Code\n"
	       "- **Up/Down**: select messages, **A**: actions for the selected message\n- **Circle Pad / C-Stick**: "
	       "scroll\n- **Left/Right**: page up / down\n- **L/R**: Activity, Queue and Tasks panels\n- **START**: quit\n\n"
	       "## Files\nSettings, the sealed key and conversations live in `sdmc:/3ds/ClaudeFor3DS`.\n\n"
	       "## Licenses\nrapidjson (MIT), stb_image (public domain / MIT), qrcodegen (MIT), libctru, citro2d, "
	       "citro3d (zlib), libcurl (curl), mbedTLS (Apache-2.0), Mozilla CA bundle (MPL-2.0).\n\n"
	       "Not affiliated with or endorsed by Anthropic or Nintendo.";
}

void openSettings();

void openSettings() {
	std::vector<PickItem> items;
	auto add = [&](const std::string &label, const std::string &value, std::function<void()> fn, Icon ic) {
		PickItem it;
		it.label = label;
		it.detail = value;
		it.icon = ic;
		it.showIcon = true;
		it.action = [fn]() {
			fn();
			config().save();
			// Rebuild so the values shown are current.
			for (auto &o : g_overlays) {
				o->closed = true;
			}
			openSettings();
		};
		items.push_back(it);
	};
	static const char *sizes[] = {"Small", "Normal", "Large"};
	add("Text size", sizes[config().textSize], [] { config().textSize = (config().textSize + 1) % 3; }, Icon::Pencil);
	add("Thinking previews", config().showThinking ? "On" : "Off",
	    [] { config().showThinking = !config().showThinking; }, Icon::Brain);
	add("Follow new messages", config().autoScroll ? "On" : "Off", [] { config().autoScroll = !config().autoScroll; },
	    Icon::ChevronDown);
	add("Stay awake while Claude works", config().keepAwake ? "On" : "Off",
	    [] { config().keepAwake = !config().keepAwake; }, Icon::Clock);
	add("Web search and fetch", config().webSearch ? "On" : "Off", [] { config().webSearch = !config().webSearch; },
	    Icon::Globe);
	add("Time zone", tzLabel() + (config().utcOffsetManual ? " (set by you)" : " (from the network)"),
	    [] {
		    int m = util::utcOffsetMinutes() + 30;
		    if (m > 14 * 60) {
			    m = -12 * 60;
		    }
		    util::setUtcOffsetMinutes(m);
		    config().utcOffset = m;
		    config().utcOffsetManual = true;
	    },
	    Icon::Clock);
	PickItem log;
	log.label = "View log";
	log.detail = "Recent network and app messages";
	log.icon = Icon::List;
	log.showIcon = true;
	log.action = []() {
		std::string text = "```\n";
		for (const std::string &l : Log::recent(80)) {
			text += l + "\n";
		}
		text += "```";
		push(new TextViewer("Log", text));
	};
	items.push_back(log);
	PickItem about;
	about.label = "About and controls";
	about.icon = Icon::Info;
	about.showIcon = true;
	about.action = []() { push(new TextViewer("About", aboutText())); };
	items.push_back(about);
	push(new Picker("Settings", items));
}

std::vector<Block> imagesOf(const Message &m) {
	std::vector<Block> out;
	for (const Block &b : m.blocks) {
		if (b.type == BlockType::Image) {
			out.push_back(b);
		} else if (b.type == BlockType::File) {
			const images::Entry &e = images::get(b);
			if (e.state == images::State::Ready || util::startsWith(e.mime, "image/")) {
				out.push_back(b);
			}
		}
	}
	return out;
}

std::string toolDetails(const Message &m, const Block &tool) {
	std::string out = "**" + claude::describeToolCall(tool, false) + "**\n\n";
	std::string server = claude::mcpServerOf(tool);
	out += "Tool: `" + tool.name + "`" + (server.empty() ? "" : " via connector **" + server + "**") + "\n\n";
	if (!tool.input.empty()) {
		json::Document doc;
		std::string pretty = tool.input;
		if (json::parse(doc, tool.input)) {
			rapidjson::StringBuffer sb;
			rapidjson::PrettyWriter<rapidjson::StringBuffer> w(sb);
			w.SetIndent(' ', 2);
			doc.Accept(w);
			pretty = sb.GetString();
		}
		out += "Input:\n```json\n" + pretty + "\n```\n\n";
	}
	for (const Message &mm : App::get().conv.messages) {
		for (const Block &r : mm.blocks) {
			if (r.type == BlockType::ToolResult && r.forToolId == tool.id) {
				out += std::string(r.error ? "Error:" : "Result:") + "\n```\n" + util::utf8Truncate(r.text, 6000) + "\n```\n";
			}
		}
	}
	(void)m;
	return out;
}

void openMessageActions(int index) {
	App &a = App::get();
	if (index < 0 || index >= (int)a.conv.messages.size()) {
		return;
	}
	const Message &m = a.conv.messages[index];
	std::vector<PickItem> items;
	auto add = [&](const std::string &label, Icon ic, std::function<void()> fn, const std::string &detail = "") {
		PickItem it;
		it.label = label;
		it.detail = detail;
		it.icon = ic;
		it.showIcon = true;
		it.action = fn;
		items.push_back(it);
	};
	std::string full = m.plainText();
	if (m.state == Message::State::Error) {
		add("Retry", Icon::Send, []() { App::get().retryLast(); }, m.error);
	}
	if (!full.empty()) {
		add("Read full message", Icon::Chat, [full]() { push(new TextViewer("Message", full)); });
	}
	if (m.role == claude::Role::User && !full.empty()) {
		add("Edit and send again", Icon::Pencil, [full]() {
			sys::KeyboardOptions kb;
			kb.initial = full;
			kb.multiline = true;
			kb.maxLength = 4000;
			ui::keyboardLater(kb, [](const std::string &text) { App::get().submit(text); });
		});
	}
	std::vector<Block> imgs = imagesOf(m);
	if (!imgs.empty()) {
		add(util::format("View images (%d)", (int)imgs.size()), Icon::Image, [imgs]() { push(new ImageViewer(imgs)); });
	}
	std::string thinking;
	for (const Block &b : m.blocks) {
		if (b.type == BlockType::Thinking && !b.text.empty()) {
			thinking += b.text + "\n\n";
		}
	}
	if (!thinking.empty()) {
		add("Show thinking", Icon::Brain, [thinking]() { push(new TextViewer("Thinking", thinking)); });
	}
	std::vector<PickItem> tools;
	for (const Block &b : m.blocks) {
		if (b.type == BlockType::ToolUse) {
			PickItem it;
			it.label = claude::describeToolCall(b, false);
			it.detail = b.name;
			it.icon = Icon::Code;
			it.showIcon = true;
			std::string details = toolDetails(m, b);
			std::string title = b.name;
			it.action = [details, title]() { push(new TextViewer(title, details)); };
			tools.push_back(it);
		} else if (b.type == BlockType::Artifact) {
			std::string body = "**" + b.name + "**  (" + b.mime + ")\n\n";
			bool markdownDoc = util::contains(b.mime, "markdown");
			body += markdownDoc ? b.text : "```\n" + b.text + "\n```";
			if (util::contains(b.mime, "html") || util::contains(b.mime, "svg") || util::contains(b.mime, "react")) {
				body += "\n\n_Interactive and visual artifacts can't run on a 3DS; this is their source._";
			}
			std::string title = b.name;
			add("Artifact: " + b.name, Icon::Sparkle, [body, title]() { push(new TextViewer(title, body)); });
		} else if (b.type == BlockType::File || (b.type == BlockType::Image && !b.text.empty())) {
			Block copy = b;
			std::string name = b.name.empty() ? "file" : b.name;
			add("Save to SD card: " + name, Icon::Download, [copy]() { App::get().saveToSd(copy); });
		} else if (b.type == BlockType::Unknown) {
			std::string raw = "This block type isn't supported by ClaudeFor3DS yet, so it is shown as raw data.\n\n```\n" +
			                  b.text + "\n```";
			add("Unsupported content: " + b.rawType, Icon::Info, [raw]() { push(new TextViewer("Unsupported", raw)); });
		}
	}
	if (!tools.empty()) {
		add(util::format("Tool details (%d)", (int)tools.size()), Icon::Terminal,
		    [tools]() { push(new Picker("Tool calls", tools)); });
	}
	md::Layout links = md::build(full, 300, 0.4f);
	if (!links.links.empty()) {
		std::vector<std::string> list = links.links;
		add(util::format("Links (%d)", (int)list.size()), Icon::Link, [list]() { push(new LinksOverlay(list)); });
	}
	if (items.empty()) {
		return;
	}
	push(new Picker(m.role == claude::Role::User ? "Your message" : "Claude's message", items));
}

void openMenu() {
	App &a = App::get();
	std::vector<PickItem> items;
	auto add = [&](const std::string &label, Icon ic, std::function<void()> fn, const std::string &detail = "",
	               bool danger = false) {
		PickItem it;
		it.label = label;
		it.detail = detail;
		it.icon = ic;
		it.showIcon = true;
		it.action = fn;
		it.danger = danger;
		items.push_back(it);
	};
	bool code = a.conv.mode == AppMode::Code;
	add(code ? "New session" : "New chat", Icon::Plus, []() { App::get().newConversation(); });
	add(code ? "Sessions" : "Chats", Icon::List, []() { openConversations(); });
	add(code ? "Switch to Chat" : "Switch to Claude Code", code ? Icon::Chat : Icon::Code,
	    [code]() { App::get().setMode(code ? AppMode::Chat : AppMode::Code); }, "SELECT");
	add("Model", Icon::Sparkle, []() { openModelPicker(); }, a.modelLabel());
	add("Effort", Icon::Brain, []() { openEffortPicker(); }, effortName(config().effort));
	if (a.hasQuestion) {
		add("Answer Claude's question", Icon::Question, []() { push(new QuestionSheet(App::get().question)); });
	}
	add(util::format("Message queue (%d)", (int)a.queue.size()), Icon::List, []() { openQueue(); },
	    a.queuePaused ? "Paused" : "");
	add("Usage limits", Icon::Clock, []() { push(new UsageOverlay()); });
	add("Settings", Icon::Gear, []() { openSettings(); });
	add("Sign out", Icon::Key,
	    []() {
		    push(new Confirm("Sign out?", "Your API key will be removed from this 3DS. Conversations stay on the SD card.",
		                     "Sign out", []() { App::get().signOut(); }));
	    },
	    a.backend() ? a.backend()->label() : "", true);
	push(new Picker("Menu", items));
}

// ---------------------------------------------------------------------------------
// Status bar (top screen, always visible)
// ---------------------------------------------------------------------------------

void statusBar() {
	App &a = App::get();
	rect(0, 0, TOP_W, 18, theme::bgDeep);
	rect(0, 18, TOP_W, 1, theme::border);
	const float ty = (18 - lineHeight(theme::small)) / 2;
	// Clock.
	int64_t now = util::nowUtc();
	std::string clock = util::utcOffsetKnown() ? util::localClock(now) : util::localClock(sys::wallClockSeconds());
	if (!util::utcOffsetKnown()) {
		// Before the network tells us the zone, the console clock is already local.
		time_t t = (time_t)sys::wallClockSeconds();
		struct tm tmv;
		gmtime_r(&t, &tmv);
		clock = util::format("%02d:%02d", tmv.tm_hour, tmv.tm_min);
	}
	textBold(6, ty, theme::small, theme::text, clock);
	float x = 6 + textWidth(clock, theme::small) + 8;
	// Mode badge.
	bool code = a.conv.mode == AppMode::Code;
	const char *mode = code ? "CODE" : "CHAT";
	float mw = textWidth(mode, theme::tiny) + 10;
	roundRect(x, 3, mw, 12, 6, code ? theme::accent : theme::panelHi);
	text(x + 5, 3 + (12 - lineHeight(theme::tiny)) / 2, theme::tiny, code ? rgb(0xFFFFFF) : theme::textDim, mode);
	x += mw + 6;

	// Right side, laid out right to left.
	float rx = TOP_W - 6;
	int pct = sys::batteryPercent();
	std::string bat = pct >= 0 ? util::format("%d%%", pct) : "--";
	float bw = textWidth(bat, theme::small);
	rx -= bw;
	text(rx, ty, theme::small, theme::textDim, bat);
	rx -= 3 + 18;
	float by = 5;
	rect(rx + 16, by + 2.5f, 2, 3.5f, theme::textDim);
	roundRectBorder(rx, by, 16, 8, 1.5f, theme::bgDeep, theme::textDim);
	if (pct >= 0) {
		Color fill = sys::isCharging() ? theme::yellow : (pct <= 15 ? theme::red : theme::green);
		rect(rx + 2, by + 2, std::max(1.0f, 12.0f * pct / 100.0f), 4, fill);
	}
	if (sys::isCharging()) {
		triangle(rx + 9, by - 1, rx + 5, by + 4.5f, rx + 8, by + 4.5f, theme::text);
		triangle(rx + 7, by + 3.5f, rx + 11, by + 3.5f, rx + 7, by + 9, theme::text);
	}
	rx -= 8;
	bool wifi = sys::isWifiConnected();
	int bars = wifi ? std::max(1, sys::wifiBars()) : 0;
	for (int i = 2; i >= 0; i--) {
		float h = 3 + i * 3.0f;
		rx -= 3;
		rect(rx, 13 - h, 2.5f, h, i < bars ? theme::text : withAlpha(theme::textFaint, 110));
		rx -= 1.5f;
	}
	if (!wifi) {
		ui::icon(Icon::Close, rx + 6, 6, 6, theme::red);
	}
	rx -= 8;
	// Usage meter: how much of the tightest limit is left, and when it resets.
	const claude::UsageWindow *w = a.usage.known ? a.usage.primary() : nullptr;
	std::string usageLabel;
	if (w) {
		int left = (int)std::round((1.0f - w->used) * 100);
		usageLabel = util::format("%d%% left", left);
		if (w->resetsAt) {
			usageLabel += " \xC2\xB7 " + util::localClock(w->resetsAt);
		}
	} else {
		usageLabel = a.phase == app::Phase::Main ? "usage --" : "";
	}
	if (!usageLabel.empty()) {
		float uw = textWidth(usageLabel, theme::tiny);
		rx -= uw;
		text(rx, (18 - lineHeight(theme::tiny)) / 2, theme::tiny, theme::textDim, usageLabel);
		rx -= 34 + 4;
		float used = w ? w->used : 0;
		ui::progressBar(rx, 6.5f, 34, 5, 1.0f - used, used > 0.85f ? theme::red : theme::accent, theme::panelHi);
		rx -= 8;
	}
	// Model and effort in the middle.
	if (a.phase == app::Phase::Main) {
		if (a.busy()) {
			ui::spinner(x + 5, 9, 4.5f, theme::accent);
			x += 13;
		}
		std::string model = a.modelLabel() + " \xC2\xB7 " + effortName(config().effort);
		textFit(x, ty, theme::small, theme::text, model, rx - x - 4);
	}
}

// ---------------------------------------------------------------------------------
// Sign-in
// ---------------------------------------------------------------------------------

void signInTop() {
	App &a = App::get();
	gradientV(0, 19, TOP_W, SCREEN_H - 19, theme::bg, theme::bgDeep);
	bool spin = a.working;
	ui::claudeLogo(200, 84, 36, theme::logoRed, ui::time() * (spin ? 1.5f : 0.2f));
	std::string title = "ClaudeFor3DS";
	textBold(200 - textWidth(title, theme::hero) / 2, 132, theme::hero, theme::text, title);
	std::string sub = "Claude and Claude Code on your Nintendo 3DS";
	text(200 - textWidth(sub, theme::label) / 2, 162, theme::label, theme::textDim, sub);
	std::string foot = "Uses the official Anthropic API with your own key";
	text(200 - textWidth(foot, theme::small) / 2, 212, theme::small, theme::textFaint, foot);
}

void signInBottom() {
	App &a = App::get();
	rect(0, 0, BOTTOM_W, SCREEN_H, theme::bg);
	textBold(16, 12, theme::title, theme::text, a.phase == app::Phase::Connecting ? "Connecting" : "Sign in");
	if (a.phase == app::Phase::Connecting) {
		if (a.working) {
			ui::spinner(30, 62, 9, theme::accent);
			text(46, 54, theme::label, theme::textDim, a.signInStatus);
		} else {
			ui::icon(Icon::Warning, 26, 60, 14, theme::red);
			ui::paragraph(42, 50, BOTTOM_W - 58, theme::small, rgb(0xF2B8B3), a.signInStatus, 5);
			if (ui::button(16, 150, 138, 32, "Try again", ui::Style::Primary)) {
				a.retryConnect();
			}
			if (ui::button(166, 150, 138, 32, "Sign out", ui::Style::Secondary)) {
				a.signOut();
			}
		}
		ui::hints(a.working ? "Checking..." : "{A} Try again  {X} Sign out");
		return;
	}
	ui::paragraph(16, 40, BOTTOM_W - 32, theme::small, theme::textDim,
	              "Enter your Anthropic API key. Create one at console.anthropic.com under API Keys.", 3);
	auto typeKey = [&a]() {
		sys::KeyboardOptions kb;
		kb.hint = "sk-ant-...";
		kb.type = sys::KeyboardType::Password;
		kb.maxLength = 256;
		kb.okLabel = "Sign in";
		ui::keyboardLater(kb, [&a](const std::string &key) { a.signInWithKey(key); });
	};
	if (ui::button(16, 84, BOTTOM_W - 32, 34, "Type API key", ui::Style::Primary, false, Icon::Keyboard, true)) {
		typeKey();
	}
	if (ui::button(16, 124, BOTTOM_W - 32, 34, "Load api_key.txt from SD card", ui::Style::Secondary, false,
	               Icon::SdCard, true)) {
		a.signInFromFile();
	}
	if (!a.signInStatus.empty()) {
		Color c = a.signInError ? theme::red : theme::textDim;
		ui::paragraph(16, 166, BOTTOM_W - 32, theme::small, c, a.signInStatus, 2);
	} else {
		ui::paragraph(16, 166, BOTTOM_W - 32, theme::tiny, theme::textFaint,
		              "Tip: save the key as sdmc:/3ds/ClaudeFor3DS/api_key.txt to skip typing. It is encrypted for "
		              "this 3DS and the file is deleted.",
		              3);
	}
	const input::State &in = input::get();
	if (in.down & input::A) {
		typeKey();
	} else if (in.down & input::Y) {
		a.signInFromFile();
	}
	ui::hints("{A} Type key  {Y} Load from SD  START Quit");
}

// ---------------------------------------------------------------------------------
// Main bottom screen
// ---------------------------------------------------------------------------------

enum class Tab { Activity = 0, Queue = 1, Tasks = 2 };
Tab g_tab = Tab::Activity;
ListView g_panelList;

struct ActivityRow {
	Icon icon;
	std::string text;
	std::string sub;
	int state; // 0 running, 1 done, 2 error, 3 info
	std::function<void()> open;
};

std::vector<ActivityRow> buildActivity() {
	App &a = App::get();
	std::vector<ActivityRow> rows;
	const auto &msgs = a.conv.messages;
	// The current turn: everything after the last message the user typed.
	int start = 0;
	for (int i = (int)msgs.size() - 1; i >= 0; i--) {
		if (msgs[i].role == claude::Role::User && !msgs[i].hidden) {
			start = i;
			break;
		}
	}
	for (int i = start; i < (int)msgs.size(); i++) {
		const Message &m = msgs[i];
		if (m.role != claude::Role::Assistant) {
			continue;
		}
		bool streaming = m.state == Message::State::Streaming;
		for (const Block &b : m.blocks) {
			ActivityRow r;
			switch (b.type) {
			case BlockType::Thinking: {
				r.icon = Icon::Brain;
				bool live = !b.done && streaming;
				r.text = live ? "Thinking" : "Thought";
				if (b.startedMs) {
					uint64_t end = b.endedMs ? b.endedMs : util::nowMs();
					r.sub = util::format("%ds", (int)((end - b.startedMs) / 1000));
				}
				r.state = live ? 0 : 1;
				std::string t = b.text;
				r.open = [t]() { push(new TextViewer("Thinking", t.empty() ? "_(no summary)_" : t)); };
				break;
			}
			case BlockType::ToolUse: {
				claude::ToolKind kind = claude::classifyTool(b.name, !b.server.empty());
				r.icon = kind == claude::ToolKind::Connector ? Icon::Plug
				         : kind == claude::ToolKind::WebSearch ? Icon::Search
				         : kind == claude::ToolKind::WebFetch  ? Icon::Globe
				         : kind == claude::ToolKind::Command   ? Icon::Terminal
				         : kind == claude::ToolKind::FileEdit  ? Icon::Pencil
				         : kind == claude::ToolKind::FileRead  ? Icon::File
				         : kind == claude::ToolKind::Todo      ? Icon::List
				         : kind == claude::ToolKind::Question  ? Icon::Question
				         : kind == claude::ToolKind::Agent     ? Icon::Agent
				         : kind == claude::ToolKind::Schedule  ? Icon::Clock
				                                               : Icon::Code;
				bool running = !b.done && streaming;
				r.text = claude::describeToolCall(b, running);
				r.sub = claude::toolKindLabel(kind);
				if (kind == claude::ToolKind::Connector) {
					r.sub = "Connector: " + claude::mcpServerOf(b);
				}
				r.state = running ? 0 : (b.error ? 2 : 1);
				std::string details = toolDetails(m, b);
				std::string title = b.name;
				r.open = [details, title]() { push(new TextViewer(title, details)); };
				break;
			}
			case BlockType::Image:
			case BlockType::File: {
				r.icon = b.type == BlockType::Image ? Icon::Image : Icon::File;
				r.text = b.type == BlockType::Image ? "Image" : "File: " + (b.name.empty() ? std::string("output") : b.name);
				r.sub = "Created";
				r.state = 1;
				Message copy = m;
				int idx = i;
				r.open = [idx]() { openMessageActions(idx); };
				break;
			}
			case BlockType::Artifact:
				r.icon = Icon::Sparkle;
				r.text = "Artifact: " + b.name;
				r.state = 1;
				break;
			default:
				continue;
			}
			rows.push_back(r);
		}
	}
	return rows;
}

void drawActivityPanel(float y, float h) {
	App &a = App::get();
	std::vector<ActivityRow> rows = buildActivity();
	// Status first.
	ActivityRow status;
	if (a.busy()) {
		status.icon = Icon::Sparkle;
		status.text = a.conv.mode == AppMode::Code ? "Claude is working" : "Claude is responding";
		status.state = 0;
	} else if (a.hasQuestion) {
		status.icon = Icon::Question;
		status.text = "Waiting for your answer";
		status.state = 3;
		status.open = []() { push(new QuestionSheet(App::get().question)); };
	} else {
		status.icon = Icon::Check;
		status.text = rows.empty() ? "Nothing running" : "Done";
		status.state = 3;
	}
	rows.insert(rows.begin(), status);
	ActivityRow sched;
	sched.icon = Icon::Clock;
	sched.text = "Scheduled tasks";
	sched.sub = "Not available with an API key";
	sched.state = 3;
	rows.push_back(sched);
	if (!config().connectors.empty()) {
		ActivityRow con;
		con.icon = Icon::Plug;
		std::string names;
		for (const auto &c : config().connectors) {
			names += (names.empty() ? "" : ", ") + c.name;
		}
		con.text = "Connectors: " + names;
		con.sub = "From config.json";
		con.state = 3;
		rows.push_back(con);
	}
	int hit = g_panelList.run(y, h, (int)rows.size(), 26, [&](int i, float ry, bool) {
		const ActivityRow &r = rows[i];
		Color c = r.state == 2 ? theme::red : (r.state == 3 ? theme::textDim : theme::text);
		if (r.state == 0) {
			ui::spinner(18, ry + 13, 5.5f, theme::accent);
		} else {
			ui::icon(r.icon, 18, ry + 13, 12, r.state == 2 ? theme::red : theme::textDim);
		}
		float textW = BOTTOM_W - 60;
		if (r.sub.empty()) {
			textFit(32, ry + (26 - lineHeight(theme::small)) / 2, theme::small, c, r.text, textW);
		} else {
			textFit(32, ry + 1, theme::small, c, r.text, textW);
			textFit(32, ry + 13, theme::tiny, theme::textFaint, r.sub, textW);
		}
		if (r.state == 1) {
			ui::icon(Icon::Check, BOTTOM_W - 28, ry + 13, 9, theme::green);
		} else if (r.state == 2) {
			ui::icon(Icon::Close, BOTTOM_W - 28, ry + 13, 8, theme::red);
		}
	}, false);
	if (hit >= 0 && hit < (int)rows.size() && rows[hit].open) {
		rows[hit].open();
	}
}

void drawQueuePanel(float y, float h) {
	App &a = App::get();
	if (a.queue.empty()) {
		ui::icon(Icon::List, 160, y + 34, 18, theme::textFaint);
		std::string m = "No queued messages";
		text(160 - textWidth(m, theme::small) / 2, y + 50, theme::small, theme::textDim, m);
		std::string m2 = "Type while Claude works and messages wait here.";
		text(160 - textWidth(m2, theme::tiny) / 2, y + 66, theme::tiny, theme::textFaint, m2);
		return;
	}
	float listY = y;
	if (a.queuePaused) {
		roundRect(8, y + 2, BOTTOM_W - 16, 24, 7, theme::panel);
		ui::icon(Icon::Pause, 20, y + 14, 10, theme::yellow);
		text(32, y + 7, theme::small, theme::text, "Paused after Stop");
		if (ui::button(BOTTOM_W - 92, y + 4, 80, 20, "Resume", ui::Style::Primary)) {
			a.resumeQueue();
		}
		listY += 28;
		h -= 28;
	}
	int removeAt = -1;
	g_panelList.run(listY, h, (int)a.queue.size(), 26, [&](int i, float ry, bool) {
		char n[12];
		snprintf(n, sizeof(n), "%d", i + 1);
		text(14, ry + (26 - lineHeight(theme::small)) / 2, theme::small, theme::textFaint, n);
		textFit(30, ry + (26 - lineHeight(theme::small)) / 2, theme::small, theme::text, a.queue[i], BOTTOM_W - 76);
		if (ui::iconButton(BOTTOM_W - 40, ry + 2, 22, Icon::Close, theme::textDim)) {
			removeAt = i;
		}
	}, false);
	if (removeAt >= 0) {
		a.removeQueued(removeAt);
		ui::toast("Removed from the queue");
	}
}

void drawTasksPanel(float y, float h) {
	App &a = App::get();
	if (a.tasks.empty()) {
		ui::icon(Icon::List, 160, y + 34, 18, theme::textFaint);
		std::string m = "No task list yet";
		text(160 - textWidth(m, theme::small) / 2, y + 50, theme::small, theme::textDim, m);
		std::string m2 = "Claude makes one for multi-step work in Claude Code mode.";
		text(160 - textWidth(m2, theme::tiny) / 2, y + 66, theme::tiny, theme::textFaint, m2);
		return;
	}
	int done = 0;
	for (const auto &t : a.tasks) {
		done += t.status == "completed";
	}
	std::string head = util::format("%d of %d done", done, (int)a.tasks.size());
	text(12, y + 2, theme::small, theme::textDim, head);
	ui::progressBar(120, y + 7, BOTTOM_W - 136, 5, (float)done / a.tasks.size(), theme::green, theme::panelHi);
	g_panelList.run(y + 18, h - 18, (int)a.tasks.size(), 22, [&](int i, float ry, bool) {
		const claude::TaskItem &t = a.tasks[i];
		float cy = ry + 11;
		if (t.status == "completed") {
			circle(18, cy, 5.5f, theme::green);
			ui::icon(Icon::Check, 18, cy, 7, theme::bgDeep);
		} else if (t.status == "in_progress") {
			ui::spinner(18, cy, 5.5f, theme::accent);
		} else {
			arc(18, cy, 5, 1.2f, 0, 6.2832f, theme::textFaint);
		}
		std::string label = t.status == "in_progress" && !t.activeForm.empty() ? t.activeForm : t.content;
		Color c = t.status == "completed" ? theme::textFaint : theme::text;
		float w = textFit(30, ry + (22 - lineHeight(theme::small)) / 2, theme::small, c, label, BOTTOM_W - 50);
		if (t.status == "completed") {
			rect(30, cy, w, 1, theme::textFaint);
		}
	}, false);
}

void mainBottom() {
	App &a = App::get();
	rect(0, 0, BOTTOM_W, SCREEN_H, theme::bg);
	// Header: menu, title, new conversation.
	rect(0, 0, BOTTOM_W, 30, theme::bgDeep);
	if (ui::iconButton(2, 2, 26, Icon::Menu, theme::text)) {
		openMenu();
	}
	std::string title = a.conv.title.empty() ? (a.conv.mode == AppMode::Code ? "New session" : "New chat") : a.conv.title;
	textFit(34, (30 - lineHeight(theme::label)) / 2, theme::label, theme::text, title, BOTTOM_W - 100, true);
	if (ui::iconButton(BOTTOM_W - 58, 2, 26, Icon::List, theme::textDim)) {
		openConversations();
	}
	if (ui::iconButton(BOTTOM_W - 30, 2, 26, Icon::Plus, theme::text)) {
		a.newConversation();
	}
	// Mode, model and effort.
	static const char *modes[] = {"Chat", "Code"};
	int m = ui::segmented(6, 34, 96, 22, modes, 2, a.conv.mode == AppMode::Code ? 1 : 0);
	if (m >= 0) {
		a.setMode(m == 1 ? AppMode::Code : AppMode::Chat);
	}
	if (ui::chip(108, 34, 120, 22, a.modelLabel(), false, true, Icon::Sparkle, true)) {
		openModelPicker();
	}
	if (ui::chip(232, 34, 82, 22, effortName(config().effort), false, true, Icon::Brain, true)) {
		openEffortPicker();
	}
	// Panel tabs.
	std::string labels[3] = {"Activity", a.queue.empty() ? "Queue" : util::format("Queue (%d)", (int)a.queue.size()),
	                         a.tasks.empty() ? "Tasks" : util::format("Tasks (%d)", (int)a.tasks.size())};
	float tabW = (BOTTOM_W - 12) / 3.0f;
	for (int i = 0; i < 3; i++) {
		float tx = 6 + i * tabW;
		bool on = (int)g_tab == i;
		float tw = textWidth(labels[i], theme::small);
		Color c = on ? theme::text : theme::textFaint;
		if (on) {
			textBold(tx + (tabW - tw) / 2, 61, theme::small, c, labels[i]);
			roundRect(tx + 10, 76, tabW - 20, 2, 1, theme::accent);
		} else {
			text(tx + (tabW - tw) / 2, 61, theme::small, c, labels[i]);
		}
		if (ui::tappedIn(tx, 58, tabW, 22)) {
			g_tab = (Tab)i;
			g_panelList = ListView();
		}
	}
	rect(6, 78, BOTTOM_W - 12, 1, theme::border);
	// Panel, with a scroll rail for the conversation on the right edge.
	float py = 80, ph = 106;
	switch (g_tab) {
	case Tab::Activity:
		drawActivityPanel(py, ph);
		break;
	case Tab::Queue:
		drawQueuePanel(py, ph);
		break;
	case Tab::Tasks:
		drawTasksPanel(py, ph);
		break;
	}
	// Composer.
	float cy = 190;
	if (a.hasQuestion) {
		if (ui::button(6, cy, BOTTOM_W - 12, 30, "Claude asked you a question: tap to answer", ui::Style::Primary, false,
		               Icon::Question, true)) {
			push(new QuestionSheet(a.question));
		}
	} else {
		bool down = ui::pressedIn(6, cy, 250, 30);
		roundRectBorder(6, cy, 250, 30, 15, down ? theme::panelHi : theme::panel, theme::border);
		std::string hint = a.busy() ? "Claude is working: tap to queue" : (a.conv.mode == AppMode::Code ? "Describe a coding task" : "Message Claude");
		ui::icon(Icon::Keyboard, 22, cy + 15, 13, theme::textFaint);
		textFit(34, cy + (30 - lineHeight(theme::label)) / 2, theme::label, theme::textFaint, hint, 214);
		if (ui::tappedIn(6, cy, 250, 30)) {
			a.compose();
		}
		if (a.busy()) {
			bool pressed = ui::pressedIn(262, cy, 52, 30);
			roundRect(262, cy, 52, 30, 15, pressed ? rgb(0x8E2F2A) : theme::red);
			ui::icon(Icon::Stop, 288, cy + 15, 14, rgb(0xFFFFFF));
			if (ui::tappedIn(262, cy, 52, 30)) {
				a.stop();
			}
		} else {
			bool pressed = ui::pressedIn(262, cy, 52, 30);
			roundRect(262, cy, 52, 30, 15, pressed ? theme::accentDeep : theme::accent);
			ui::icon(Icon::Send, 288, cy + 15, 14, rgb(0xFFFFFF));
			if (ui::tappedIn(262, cy, 52, 30)) {
				a.compose();
			}
		}
	}
	if (a.busy()) {
		ui::hints("{Y} Queue  {B} Stop  {X} Menu  {L}{R} Panels");
	} else if (chat::selected() >= 0) {
		ui::hints("{A} Actions  {Y} Type  {B} Deselect  {X} Menu");
	} else {
		ui::hints("{Y} Type  {X} Menu  SELECT Mode  Up/Down Select");
	}
}

} // namespace

// ---------------------------------------------------------------------------------

bool exitRequested() { return g_exit; }

void handleInputInner();

void handleInput() {
	size_t before = g_overlays.size();
	Overlay *top = g_overlays.empty() ? nullptr : g_overlays.back().get();
	handleInputInner();
	// A press that opened (or switched) an overlay must not also act inside it this frame.
	if (g_overlays.size() != before || (!g_overlays.empty() && g_overlays.back().get() != top)) {
		input::consume(0xFFFFFFFFu);
		input::consumeTouch();
	}
}

void handleInputInner() {
	App &a = App::get();
	const input::State &in = input::get();
	if (in.down & input::START) {
		if (a.busy()) {
			push(new Confirm("Quit ClaudeFor3DS?", "Claude is still responding. Quitting stops the reply.", "Quit",
			                 []() { g_exit = true; }));
		} else {
			g_exit = true;
		}
		input::consume(input::START);
		return;
	}
	if (a.phase != app::Phase::Main) {
		g_overlays.clear();
		if (a.phase == app::Phase::Connecting && !a.working) {
			if (in.down & input::A) {
				a.retryConnect();
			} else if (in.down & input::X) {
				a.signOut();
			}
		}
		return;
	}
	// A new question pops up its sheet once.
	static bool questionShown = false;
	if (a.hasQuestion && !questionShown) {
		questionShown = true;
		push(new QuestionSheet(a.question));
		return;
	}
	if (!a.hasQuestion) {
		questionShown = false;
	}
	if (!g_overlays.empty()) {
		g_overlays.back()->input(in);
		return;
	}
	chat::handleInput();
	if (in.down & input::Y) {
		a.compose();
	} else if (in.down & input::A) {
		if (chat::selected() >= 0) {
			openMessageActions(chat::selected());
		} else if (a.hasQuestion) {
			push(new QuestionSheet(a.question));
		} else {
			a.compose();
		}
	} else if (in.down & input::B) {
		if (a.busy()) {
			a.stop();
			ui::toast("Stopping");
		} else {
			chat::clearSelection();
		}
	} else if (in.down & input::X) {
		openMenu();
	} else if (in.down & input::SELECT) {
		a.setMode(a.conv.mode == AppMode::Code ? AppMode::Chat : AppMode::Code);
	} else if (in.down & input::L) {
		g_tab = (Tab)(((int)g_tab + 2) % 3);
		g_panelList = ListView();
	} else if (in.down & input::R) {
		g_tab = (Tab)(((int)g_tab + 1) % 3);
		g_panelList = ListView();
	}
}

void drawTop() {
	App &a = App::get();
	bool covered = false;
	if (a.phase == app::Phase::Main) {
		for (auto it = g_overlays.rbegin(); it != g_overlays.rend() && !covered; ++it) {
			covered = (*it)->drawTop();
		}
		if (!covered) {
			chat::draw();
		}
	} else {
		signInTop();
	}
	// Full-screen viewers draw over the status bar; everything else keeps it.
	bool fullscreen = !g_overlays.empty() && g_overlays.back()->fullscreen();
	if (!fullscreen) {
		statusBar();
	}
}

void drawBottom() {
	App &a = App::get();
	if (a.phase != app::Phase::Main) {
		signInBottom();
	} else if (!g_overlays.empty()) {
		g_overlays.back()->drawBottom();
	} else {
		mainBottom();
	}
	ui::drawToast();
	// Drop closed overlays (any depth: settings rebuilds itself).
	g_overlays.erase(std::remove_if(g_overlays.begin(), g_overlays.end(),
	                                [](const std::unique_ptr<Overlay> &o) { return o->closed; }),
	                 g_overlays.end());
}

} // namespace screens
