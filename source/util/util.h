// Small helpers shared by every layer: UTF-8, strings, time, ids.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace util {

// ---- UTF-8 ---------------------------------------------------------------
// Decodes one code point at s[i] and advances i. Invalid bytes decode as U+FFFD.
uint32_t utf8Next(const std::string &s, size_t &i);
void utf8Append(std::string &out, uint32_t cp);
size_t utf8Length(const std::string &s);
// Byte offset of the code point that starts at or before `pos`.
size_t utf8Floor(const std::string &s, size_t pos);
// Cuts to at most maxBytes without splitting a code point.
std::string utf8Truncate(const std::string &s, size_t maxBytes);

// ---- strings -------------------------------------------------------------
std::string trim(const std::string &s);
std::string toLower(std::string s);
bool startsWith(const std::string &s, const std::string &prefix);
bool endsWith(const std::string &s, const std::string &suffix);
bool contains(const std::string &s, const std::string &needle);
std::vector<std::string> split(const std::string &s, char sep);
std::string replaceAll(std::string s, const std::string &from, const std::string &to);
// Collapses runs of whitespace (incl. newlines) to single spaces.
std::string oneLine(const std::string &s);
// One line of at most maxChars code points, with an ellipsis when cut.
std::string ellipsize(const std::string &s, size_t maxChars);
std::string urlEncode(const std::string &s);
std::string formatBytes(uint64_t bytes);
std::string format(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// ---- ids / encoding ------------------------------------------------------
std::string uuid4();
std::string base64Encode(const uint8_t *data, size_t len);
std::vector<uint8_t> base64Decode(const std::string &in);
std::string hex(const uint8_t *data, size_t len);

// ---- time ----------------------------------------------------------------
// Milliseconds from a monotonic clock.
uint64_t nowMs();
// Seconds since the epoch in UTC, corrected by the offset learned from the network.
int64_t nowUtc();
// Parses ISO-8601 ("2026-11-22T20:15:00.123Z", "+01:00" offsets) to UTC seconds; 0 on failure.
int64_t parseIso8601(const std::string &s);
// Parses an RFC 1123 HTTP Date header to UTC seconds; 0 on failure.
int64_t parseHttpDate(const std::string &s);
// "in 2h 14m", "in 5m", "now"
std::string relativeFuture(int64_t utcSeconds);
// "3m ago", "2h ago", "Nov 20"
std::string relativePast(int64_t utcSeconds);
// Local HH:MM for a UTC timestamp, using the console's clock offset.
std::string localClock(int64_t utcSeconds);

// The console clock is local time with no zone; we learn the zone from server Date headers.
void learnUtcFromServer(int64_t serverUtc);
int utcOffsetMinutes();
void setUtcOffsetMinutes(int minutes);
bool utcOffsetKnown();

} // namespace util
