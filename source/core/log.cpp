#include "core/log.h"

#include "platform/system.h"

#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>

namespace Log {

namespace {
std::mutex g_mutex;
std::deque<std::string> g_lines;
FILE *g_file = nullptr;
constexpr size_t MAX_LINES = 300;
} // namespace

void init() {
	std::lock_guard<std::mutex> lock(g_mutex);
	sys::ensureDir(sys::dataDir());
	std::string path = sys::dataDir() + "/log.txt";
	// Keep the file small: start fresh each launch, keep the previous one.
	std::string old = sys::dataDir() + "/log.old.txt";
	remove(old.c_str());
	rename(path.c_str(), old.c_str());
	g_file = fopen(path.c_str(), "w");
}

void write(const char *fmt, ...) {
	char buf[768];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	std::lock_guard<std::mutex> lock(g_mutex);
	g_lines.emplace_back(buf);
	while (g_lines.size() > MAX_LINES) {
		g_lines.pop_front();
	}
	if (g_file) {
		fprintf(g_file, "[%llu] %s\n", (unsigned long long)sys::monotonicMs(), buf);
		fflush(g_file);
	}
#ifndef __3DS__
	fprintf(stderr, "%s\n", buf);
#endif
}

std::vector<std::string> recent(size_t maxLines) {
	std::lock_guard<std::mutex> lock(g_mutex);
	size_t start = g_lines.size() > maxLines ? g_lines.size() - maxLines : 0;
	return std::vector<std::string>(g_lines.begin() + start, g_lines.end());
}

} // namespace Log
