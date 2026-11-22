// Thread-safe log: kept in memory for the in-app log viewer and appended to log.txt on the SD card.
#pragma once

#include <string>
#include <vector>

namespace Log {

void init();
void write(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
std::vector<std::string> recent(size_t maxLines);

} // namespace Log
