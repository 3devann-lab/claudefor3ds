#include "core/main_queue.h"

#include <mutex>
#include <vector>

namespace MainQueue {

namespace {
std::mutex g_mutex;
std::vector<std::function<void()>> g_pending;
} // namespace

void post(std::function<void()> fn) {
	std::lock_guard<std::mutex> lock(g_mutex);
	g_pending.push_back(std::move(fn));
}

bool drain() {
	std::vector<std::function<void()>> work;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		work.swap(g_pending);
	}
	for (auto &fn : work) {
		fn();
	}
	return !work.empty();
}

} // namespace MainQueue
