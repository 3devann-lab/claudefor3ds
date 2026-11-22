// Incremental Server-Sent Events parser: feed raw bytes, get (event, data) pairs.
#pragma once

#include <functional>
#include <string>

namespace net {

class SseParser {
  public:
	using Handler = std::function<void(const std::string &event, const std::string &data)>;
	explicit SseParser(Handler handler) : handler_(std::move(handler)) {}

	void feed(const char *data, size_t len);
	// Flushes a final event that was not followed by a blank line.
	void finish();

  private:
	void line(const std::string &l);
	void dispatch();

	Handler handler_;
	std::string buffer_;
	std::string event_;
	std::string data_;
	bool hasData_ = false;
};

} // namespace net
