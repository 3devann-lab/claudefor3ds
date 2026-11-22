// ClaudeFor3DS entry point (console). The simulator has its own main in host/.
#ifdef __3DS__

#include "app/app.h"
#include "core/config.h"
#include "core/log.h"
#include "core/main_queue.h"
#include "net/http.h"
#include "platform/gfx.h"
#include "platform/input.h"
#include "platform/system.h"
#include "ui/images.h"
#include "ui/screens.h"
#include "ui/theme.h"
#include "ui/ui.h"

#include <3ds.h>
#include <malloc.h>

static u32 *g_socBuffer = nullptr;
static const u32 SOC_BUFFER_SIZE = 0x100000;

int main(int argc, char **argv) {
	osSetSpeedupEnable(true);
	gfxInitDefault();
	romfsInit();
	sys::init();
	Log::init();
	Log::write("ClaudeFor3DS %s starting (%s)", APP_VERSION_STR, sys::isNew3DS() ? "New 3DS" : "Old 3DS");

	g_socBuffer = (u32 *)memalign(0x1000, SOC_BUFFER_SIZE);
	if (!g_socBuffer || R_FAILED(socInit(g_socBuffer, SOC_BUFFER_SIZE))) {
		Log::write("socInit failed");
	}
	// mbedTLS takes its entropy from sslc.
	if (R_FAILED(sslcInit(0))) {
		Log::write("sslcInit failed");
	}

	gfx::init();
	config().load();
	net::init();
	app::App::get().init();

	while (aptMainLoop()) {
		input::scan();
		sys::update();
		ui::tick();
		MainQueue::drain();
		images::update();
		app::App::get().update();
		screens::handleInput();
		if (screens::exitRequested()) {
			break;
		}

		gfx::beginFrame();
		gfx::beginScreen(gfx::Screen::Top, theme::bg);
		screens::drawTop();
		gfx::beginScreen(gfx::Screen::Bottom, theme::bg);
		screens::drawBottom();
		gfx::endFrame();
	}

	app::App::get().shutdown();
	net::shutdown();
	MainQueue::drain();
	images::clear();
	gfx::shutdown();
	sslcExit();
	socExit();
	sys::exit();
	romfsExit();
	gfxExit();
	return 0;
}

#endif
