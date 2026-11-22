#include "net/sse.h"

namespace net {

void SseParser::feed(const char *data, size_t len) {
	buffer_.append(data, len);
	size_t start = 0;
	while (true) {
		size_t nl = buffer_.find('\n', start);
		if (nl == std::string::npos) {
			break;
		}
		size_t end = nl;
		if (end > start && buffer_[end - 1] == '\r') {
			end--;
		}
		line(buffer_.substr(start, end - start));
		start = nl + 1;
	}
	buffer_.erase(0, start);
}

void SseParser::finish() {
	if (!buffer_.empty()) {
		line(buffer_);
		buffer_.clear();
	}
	dispatch();
}

void SseParser::line(const std::string &l) {
	if (l.empty()) {
		dispatch();
		return;
	}
	if (l[0] == ':') {
		return; // comment / keep-alive
	}
	size_t colon = l.find(':');
	std::string field = colon == std::string::npos ? l : l.substr(0, colon);
	std::string value;
	if (colon != std::string::npos) {
		value = l.substr(colon + 1);
		if (!value.empty() && value[0] == ' ') {
			value.erase(0, 1);
		}
	}
	if (field == "event") {
		event_ = value;
	} else if (field == "data") {
		if (hasData_) {
			data_ += '\n';
		}
		data_ += value;
		hasData_ = true;
	}
}

void SseParser::dispatch() {
	if (hasData_ || !event_.empty()) {
		if (!(event_.empty() && data_.empty())) {
			handler_(event_.empty() ? "message" : event_, data_);
		}
	}
	event_.clear();
	data_.clear();
	hasData_ = false;
}

} // namespace net
