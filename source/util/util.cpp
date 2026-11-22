#include "util/util.h"

#include "platform/system.h"

#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace util {

// ---- UTF-8 ---------------------------------------------------------------

uint32_t utf8Next(const std::string &s, size_t &i) {
	const unsigned char c = (unsigned char)s[i];
	if (c < 0x80) {
		i += 1;
		return c;
	}
	int extra = 0;
	uint32_t cp = 0;
	if ((c & 0xE0) == 0xC0) {
		extra = 1;
		cp = c & 0x1F;
	} else if ((c & 0xF0) == 0xE0) {
		extra = 2;
		cp = c & 0x0F;
	} else if ((c & 0xF8) == 0xF0) {
		extra = 3;
		cp = c & 0x07;
	} else {
		i += 1;
		return 0xFFFD;
	}
	if (i + extra >= s.size()) {
		i = s.size();
		return 0xFFFD;
	}
	for (int k = 1; k <= extra; k++) {
		const unsigned char cc = (unsigned char)s[i + k];
		if ((cc & 0xC0) != 0x80) {
			i += k;
			return 0xFFFD;
		}
		cp = (cp << 6) | (cc & 0x3F);
	}
	i += extra + 1;
	return cp;
}

void utf8Append(std::string &out, uint32_t cp) {
	if (cp < 0x80) {
		out += (char)cp;
	} else if (cp < 0x800) {
		out += (char)(0xC0 | (cp >> 6));
		out += (char)(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		out += (char)(0xE0 | (cp >> 12));
		out += (char)(0x80 | ((cp >> 6) & 0x3F));
		out += (char)(0x80 | (cp & 0x3F));
	} else {
		out += (char)(0xF0 | (cp >> 18));
		out += (char)(0x80 | ((cp >> 12) & 0x3F));
		out += (char)(0x80 | ((cp >> 6) & 0x3F));
		out += (char)(0x80 | (cp & 0x3F));
	}
}

size_t utf8Length(const std::string &s) {
	size_t n = 0;
	for (unsigned char c : s) {
		if ((c & 0xC0) != 0x80) {
			n++;
		}
	}
	return n;
}

size_t utf8Floor(const std::string &s, size_t pos) {
	if (pos >= s.size()) {
		return s.size();
	}
	while (pos > 0 && ((unsigned char)s[pos] & 0xC0) == 0x80) {
		pos--;
	}
	return pos;
}

std::string utf8Truncate(const std::string &s, size_t maxBytes) {
	if (s.size() <= maxBytes) {
		return s;
	}
	return s.substr(0, utf8Floor(s, maxBytes));
}

// ---- strings -------------------------------------------------------------

std::string trim(const std::string &s) {
	size_t a = 0, b = s.size();
	while (a < b && isspace((unsigned char)s[a])) {
		a++;
	}
	while (b > a && isspace((unsigned char)s[b - 1])) {
		b--;
	}
	return s.substr(a, b - a);
}

std::string toLower(std::string s) {
	for (char &c : s) {
		c = (char)tolower((unsigned char)c);
	}
	return s;
}

bool startsWith(const std::string &s, const std::string &prefix) {
	return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string &s, const std::string &suffix) {
	return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool contains(const std::string &s, const std::string &needle) { return s.find(needle) != std::string::npos; }

std::vector<std::string> split(const std::string &s, char sep) {
	std::vector<std::string> out;
	size_t start = 0;
	while (true) {
		size_t p = s.find(sep, start);
		if (p == std::string::npos) {
			out.push_back(s.substr(start));
			break;
		}
		out.push_back(s.substr(start, p - start));
		start = p + 1;
	}
	return out;
}

std::string replaceAll(std::string s, const std::string &from, const std::string &to) {
	if (from.empty()) {
		return s;
	}
	size_t pos = 0;
	while ((pos = s.find(from, pos)) != std::string::npos) {
		s.replace(pos, from.size(), to);
		pos += to.size();
	}
	return s;
}

std::string oneLine(const std::string &s) {
	std::string out;
	out.reserve(s.size());
	bool space = false;
	for (char c : s) {
		if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
			space = !out.empty();
			continue;
		}
		if (space) {
			out += ' ';
			space = false;
		}
		out += c;
	}
	return out;
}

std::string ellipsize(const std::string &s, size_t maxChars) {
	std::string flat = oneLine(s);
	if (utf8Length(flat) <= maxChars) {
		return flat;
	}
	std::string out;
	size_t i = 0, n = 0;
	while (i < flat.size() && n + 1 < maxChars) {
		size_t start = i;
		utf8Next(flat, i);
		out.append(flat, start, i - start);
		n++;
	}
	return out + "\xE2\x80\xA6";
}

std::string urlEncode(const std::string &s) {
	static const char *hexd = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : s) {
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			out += (char)c;
		} else {
			out += '%';
			out += hexd[c >> 4];
			out += hexd[c & 15];
		}
	}
	return out;
}

std::string formatBytes(uint64_t bytes) {
	if (bytes < 1024) {
		return format("%u B", (unsigned)bytes);
	}
	if (bytes < 1024 * 1024) {
		return format("%.1f KB", bytes / 1024.0);
	}
	return format("%.1f MB", bytes / (1024.0 * 1024.0));
}

std::string format(const char *fmt, ...) {
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n < 0) {
		return "";
	}
	if ((size_t)n < sizeof(buf)) {
		return std::string(buf, n);
	}
	std::string big(n + 1, '\0');
	va_start(ap, fmt);
	vsnprintf(&big[0], big.size(), fmt, ap);
	va_end(ap);
	big.resize(n);
	return big;
}

// ---- ids / encoding ------------------------------------------------------

std::string uuid4() {
	uint8_t b[16];
	sys::randomBytes(b, sizeof(b));
	b[6] = (b[6] & 0x0F) | 0x40;
	b[8] = (b[8] & 0x3F) | 0x80;
	std::string h = hex(b, 16);
	return h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" + h.substr(16, 4) + "-" + h.substr(20);
}

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const uint8_t *data, size_t len) {
	std::string out;
	out.reserve((len + 2) / 3 * 4);
	for (size_t i = 0; i < len; i += 3) {
		uint32_t v = data[i] << 16;
		if (i + 1 < len) {
			v |= data[i + 1] << 8;
		}
		if (i + 2 < len) {
			v |= data[i + 2];
		}
		out += B64[(v >> 18) & 63];
		out += B64[(v >> 12) & 63];
		out += i + 1 < len ? B64[(v >> 6) & 63] : '=';
		out += i + 2 < len ? B64[v & 63] : '=';
	}
	return out;
}

std::vector<uint8_t> base64Decode(const std::string &in) {
	static int8_t table[256];
	static bool init = false;
	if (!init) {
		memset(table, -1, sizeof(table));
		for (int i = 0; i < 64; i++) {
			table[(unsigned char)B64[i]] = (int8_t)i;
		}
		table[(unsigned char)'-'] = 62; // url-safe alphabet too
		table[(unsigned char)'_'] = 63;
		init = true;
	}
	std::vector<uint8_t> out;
	out.reserve(in.size() * 3 / 4);
	uint32_t acc = 0;
	int bits = 0;
	for (unsigned char c : in) {
		int8_t v = table[c];
		if (v < 0) {
			continue;
		}
		acc = (acc << 6) | (uint32_t)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back((uint8_t)((acc >> bits) & 0xFF));
		}
	}
	return out;
}

std::string hex(const uint8_t *data, size_t len) {
	static const char *d = "0123456789abcdef";
	std::string out;
	out.reserve(len * 2);
	for (size_t i = 0; i < len; i++) {
		out += d[data[i] >> 4];
		out += d[data[i] & 15];
	}
	return out;
}

// ---- time ----------------------------------------------------------------

static std::atomic<int> g_offsetMinutes{0};
static std::atomic<bool> g_offsetKnown{false};

uint64_t nowMs() { return sys::monotonicMs(); }

int64_t nowUtc() { return sys::wallClockSeconds() - (int64_t)g_offsetMinutes.load() * 60; }

void learnUtcFromServer(int64_t serverUtc) {
	if (serverUtc <= 0 || g_offsetKnown.load()) {
		return;
	}
	int64_t diff = sys::wallClockSeconds() - serverUtc;
	// Time zones are whole quarter hours; anything beyond +-14h means the clock is just wrong.
	int64_t quarters = (diff >= 0 ? diff + 450 : diff - 450) / 900;
	if (quarters < -14 * 4 || quarters > 14 * 4) {
		return;
	}
	g_offsetMinutes = (int)(quarters * 15);
	g_offsetKnown = true;
}

int utcOffsetMinutes() { return g_offsetMinutes.load(); }

void setUtcOffsetMinutes(int minutes) {
	g_offsetMinutes = minutes;
	g_offsetKnown = true;
}

bool utcOffsetKnown() { return g_offsetKnown.load(); }

// Days since the epoch for a civil date (Howard Hinnant's algorithm).
static int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
	y -= m <= 2;
	const int64_t era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = (unsigned)(y - era * 400);
	const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int64_t)doe - 719468;
}

int64_t parseIso8601(const std::string &s) {
	int Y, M, D, h = 0, m = 0, sec = 0;
	if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) < 3) {
		if (sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &Y, &M, &D, &h, &m, &sec) < 3) {
			return 0;
		}
	}
	int64_t t = daysFromCivil(Y, (unsigned)M, (unsigned)D) * 86400 + h * 3600 + m * 60 + sec;
	// Offset after the time part: Z, +hh:mm or -hh:mm.
	size_t tpos = s.find_first_of("Tt ");
	if (tpos != std::string::npos) {
		size_t z = s.find_first_of("Zz+-", tpos + 1);
		if (z != std::string::npos && s[z] != 'Z' && s[z] != 'z') {
			int oh = 0, om = 0;
			if (sscanf(s.c_str() + z + 1, "%d:%d", &oh, &om) >= 1) {
				int off = oh * 3600 + om * 60;
				t += s[z] == '+' ? -off : off;
			}
		}
	}
	return t;
}

int64_t parseHttpDate(const std::string &s) {
	static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	char wd[8] = {0}, mon[8] = {0};
	int d, y, h, m, sec;
	if (sscanf(s.c_str(), "%3s, %d %3s %d %d:%d:%d", wd, &d, mon, &y, &h, &m, &sec) != 7) {
		return 0;
	}
	int mi = -1;
	for (int i = 0; i < 12; i++) {
		if (strcmp(mon, months[i]) == 0) {
			mi = i;
		}
	}
	if (mi < 0) {
		return 0;
	}
	return daysFromCivil(y, (unsigned)(mi + 1), (unsigned)d) * 86400 + h * 3600 + m * 60 + sec;
}

std::string relativeFuture(int64_t utcSeconds) {
	int64_t d = utcSeconds - nowUtc();
	if (d <= 30) {
		return "now";
	}
	int64_t mins = (d + 59) / 60;
	if (mins < 60) {
		return format("in %dm", (int)mins);
	}
	int64_t hours = mins / 60;
	if (hours < 24) {
		return format("in %dh %dm", (int)hours, (int)(mins % 60));
	}
	return format("in %dd %dh", (int)(hours / 24), (int)(hours % 24));
}

std::string relativePast(int64_t utcSeconds) {
	if (utcSeconds <= 0) {
		return "";
	}
	int64_t d = nowUtc() - utcSeconds;
	if (d < 60) {
		return "just now";
	}
	if (d < 3600) {
		return format("%dm ago", (int)(d / 60));
	}
	if (d < 86400) {
		return format("%dh ago", (int)(d / 3600));
	}
	if (d < 7 * 86400) {
		return format("%dd ago", (int)(d / 86400));
	}
	time_t local = (time_t)(utcSeconds + (int64_t)utcOffsetMinutes() * 60);
	struct tm tmv;
	gmtime_r(&local, &tmv);
	static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	return format("%s %d", months[tmv.tm_mon], tmv.tm_mday);
}

std::string localClock(int64_t utcSeconds) {
	time_t local = (time_t)(utcSeconds + (int64_t)utcOffsetMinutes() * 60);
	struct tm tmv;
	gmtime_r(&local, &tmv);
	return format("%02d:%02d", tmv.tm_hour, tmv.tm_min);
}

} // namespace util
