// HTTP over libcurl on background threads. Nothing here ever runs on the UI thread.
#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace net {

using CancelFlag = std::shared_ptr<std::atomic<bool>>;
inline CancelFlag makeCancel() { return std::make_shared<std::atomic<bool>>(false); }

struct Request {
	std::string method = "GET";
	std::string url;
	std::vector<std::string> headers; // "Name: value"
	std::string body;
	long timeoutSec = 30;
	// Streaming responses get no total timeout; they fail only when the link stalls.
	bool streaming = false;
	// Called on the worker thread as body bytes arrive (streaming only). Return false to abort.
	std::function<bool(const char *data, size_t len)> onChunk;
	CancelFlag cancel;
	size_t maxBody = 6 * 1024 * 1024;
};

struct Response {
	long status = 0;
	std::string body;
	std::map<std::string, std::string> headers; // lower-case names
	std::vector<std::string> setCookies;
	std::string error; // transport-level failure, empty when the server answered
	bool cancelled = false;

	bool ok() const { return error.empty() && status >= 200 && status < 300; }
	std::string header(const std::string &lowerName) const {
		auto it = headers.find(lowerName);
		return it == headers.end() ? "" : it->second;
	}
	// Cloudflare bot check (an HTML challenge instead of the API answer).
	bool isChallenge() const;
	// A short, human readable description of what went wrong.
	std::string describeError() const;
};

enum class Lane { Api = 0, Stream = 1, Media = 2 };

void init();
void shutdown();

// Queues a request on a lane's worker; `done` runs on that worker thread when it finishes.
void request(Lane lane, Request req, std::function<void(Response &)> done);

// Number of requests queued or running on a lane.
int pending(Lane lane);
bool busy();

// "name=value" pairs from Set-Cookie headers.
std::map<std::string, std::string> parseSetCookies(const std::vector<std::string> &setCookies);

} // namespace net
