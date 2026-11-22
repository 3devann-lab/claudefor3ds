#include "net/http.h"

#include "core/log.h"
#include "platform/system.h"
#include "util/util.h"

#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <strings.h>

#if defined(__3DS__) || defined(HAVE_CURL)
#define NET_HAVE_CURL 1
#include <curl/curl.h>
#endif

#ifdef __3DS__
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#endif

namespace net {

static const char *USER_AGENT = "ClaudeFor3DS/1.0 (Nintendo 3DS; homebrew)";

bool Response::isChallenge() const {
	if (status != 403 && status != 503 && status != 429) {
		return false;
	}
	if (!header("cf-mitigated").empty()) {
		return true;
	}
	std::string ct = header("content-type");
	return util::contains(ct, "text/html") &&
	       (util::contains(body, "challenge-platform") || util::contains(body, "Just a moment") ||
	        util::contains(body, "cf-chl") || util::contains(body, "Attention Required"));
}

std::string Response::describeError() const {
	if (cancelled) {
		return "Cancelled";
	}
	if (!error.empty()) {
		return error;
	}
	if (isChallenge()) {
		return "Blocked by Cloudflare's browser check (HTTP " + std::to_string(status) + ")";
	}
	std::string msg = "HTTP " + std::to_string(status);
	// Pull a message out of the usual JSON error shapes without a full parse.
	size_t p = body.find("\"message\"");
	if (p != std::string::npos) {
		size_t q = body.find('"', body.find(':', p) + 1);
		if (q != std::string::npos) {
			std::string m;
			for (size_t i = q + 1; i < body.size() && body[i] != '"'; i++) {
				if (body[i] == '\\' && i + 1 < body.size()) {
					i++;
				}
				m += body[i];
			}
			if (!m.empty()) {
				msg += ": " + util::ellipsize(m, 160);
			}
		}
	} else if (!body.empty() && body[0] != '<') {
		msg += ": " + util::ellipsize(body, 120);
	}
	return msg;
}

std::map<std::string, std::string> parseSetCookies(const std::vector<std::string> &setCookies) {
	std::map<std::string, std::string> out;
	for (const std::string &line : setCookies) {
		std::string pair = line.substr(0, line.find(';'));
		size_t eq = pair.find('=');
		if (eq == std::string::npos) {
			continue;
		}
		std::string name = util::trim(pair.substr(0, eq));
		std::string value = util::trim(pair.substr(eq + 1));
		if (!name.empty()) {
			out[name] = value;
		}
	}
	return out;
}

#ifdef NET_HAVE_CURL

namespace {

struct Job {
	Request req;
	std::function<void(Response &)> done;
};

struct LaneState {
	std::mutex mutex;
	std::condition_variable cv;
	std::deque<Job> queue;
	std::atomic<int> active{0};
	bool stop = false;
	bool running = false;
};

LaneState g_lanes[3];
bool g_initialized = false;

#ifdef __3DS__
std::mutex g_caMutex;
mbedtls_x509_crt g_ca;
bool g_caLoaded = false;

mbedtls_x509_crt *caChain() {
	std::lock_guard<std::mutex> lock(g_caMutex);
	if (!g_caLoaded) {
		mbedtls_x509_crt_init(&g_ca);
		int rc = mbedtls_x509_crt_parse_file(&g_ca, "romfs:/cacert.pem");
		if (rc < 0) {
			Log::write("[net] CA bundle failed to load: -0x%04x", -rc);
		}
		g_caLoaded = true;
	}
	return &g_ca;
}

CURLcode sslContext(CURL *, void *sslConfig, void *) {
	// Parsing 140 certificates per connection is slow on a 3DS; share one parsed chain.
	mbedtls_ssl_conf_ca_chain(static_cast<mbedtls_ssl_config *>(sslConfig), caChain(), nullptr);
	return CURLE_OK;
}
#endif

struct Transfer {
	Request *req;
	Response *resp;
};

size_t onBody(char *data, size_t size, size_t nmemb, void *user) {
	Transfer *t = static_cast<Transfer *>(user);
	size_t len = size * nmemb;
	if (t->req->cancel && t->req->cancel->load()) {
		t->resp->cancelled = true;
		return 0;
	}
	if (t->req->streaming && t->req->onChunk && t->resp->status >= 200 && t->resp->status < 300) {
		if (!t->req->onChunk(data, len)) {
			t->resp->cancelled = true;
			return 0;
		}
		// Keep a tail of the body for error reporting only.
		if (t->resp->body.size() < 4096) {
			t->resp->body.append(data, std::min(len, (size_t)4096));
		}
		return len;
	}
	if (t->resp->body.size() + len > t->req->maxBody) {
		t->resp->error = "Response too large (" + util::formatBytes(t->resp->body.size() + len) + ")";
		return 0;
	}
	t->resp->body.append(data, len);
	return len;
}

size_t onHeader(char *data, size_t size, size_t nitems, void *user) {
	Transfer *t = static_cast<Transfer *>(user);
	size_t len = size * nitems;
	std::string line(data, len);
	while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
		line.pop_back();
	}
	if (util::startsWith(line, "HTTP/")) {
		// A new response (redirect or 100-continue) starts; forget the previous headers.
		t->resp->headers.clear();
		size_t sp = line.find(' ');
		if (sp != std::string::npos) {
			t->resp->status = atol(line.c_str() + sp + 1);
		}
		return len;
	}
	size_t colon = line.find(':');
	if (colon == std::string::npos) {
		return len;
	}
	std::string name = util::toLower(util::trim(line.substr(0, colon)));
	std::string value = util::trim(line.substr(colon + 1));
	if (name == "set-cookie") {
		t->resp->setCookies.push_back(value);
	}
	t->resp->headers[name] = value;
	return len;
}

int onProgress(void *user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
	Transfer *t = static_cast<Transfer *>(user);
	if (t->req->cancel && t->req->cancel->load()) {
		t->resp->cancelled = true;
		return 1;
	}
	return 0;
}

void perform(CURL *curl, Request &req, Response &resp) {
	curl_easy_reset(curl);
	Transfer t{&req, &resp};
	curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
	curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
#ifdef __3DS__
	curl_easy_setopt(curl, CURLOPT_CAINFO, nullptr);
	curl_easy_setopt(curl, CURLOPT_CAPATH, nullptr);
	curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, sslContext);
	curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 16384L);
#endif
	if (req.streaming) {
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 120L);
	} else {
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, req.timeoutSec);
	}
	if (req.method == "POST") {
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)req.body.size());
	} else if (req.method != "GET") {
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req.method.c_str());
		if (!req.body.empty()) {
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)req.body.size());
		}
	}
	curl_slist *headers = nullptr;
	for (const std::string &h : req.headers) {
		headers = curl_slist_append(headers, h.c_str());
	}
	// curl adds "Expect: 100-continue" to big POSTs, which costs a round trip.
	headers = curl_slist_append(headers, "Expect:");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onBody);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &t);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, onHeader);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &t);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onProgress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &t);

	CURLcode rc = curl_easy_perform(curl);
	curl_slist_free_all(headers);
	if (rc != CURLE_OK && !resp.cancelled && resp.error.empty()) {
		resp.error = curl_easy_strerror(rc);
		if (rc == CURLE_COULDNT_RESOLVE_HOST || rc == CURLE_COULDNT_CONNECT) {
			resp.error = sys::isWifiConnected() ? "Can't reach the server" : "No internet connection";
		} else if (rc == CURLE_OPERATION_TIMEDOUT) {
			resp.error = "The connection timed out";
		}
	}
	long code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	if (code) {
		resp.status = code;
	}
	std::string date = resp.header("date");
	if (!date.empty()) {
		util::learnUtcFromServer(util::parseHttpDate(date));
	}
}

void laneLoop(int index) {
	LaneState &lane = g_lanes[index];
	CURL *curl = curl_easy_init();
	while (true) {
		Job job;
		{
			std::unique_lock<std::mutex> lock(lane.mutex);
			lane.cv.wait(lock, [&] { return lane.stop || !lane.queue.empty(); });
			if (lane.stop && lane.queue.empty()) {
				break;
			}
			job = std::move(lane.queue.front());
			lane.queue.pop_front();
			lane.active++;
		}
		Response resp;
		if (job.req.cancel && job.req.cancel->load()) {
			resp.cancelled = true;
		} else if (curl) {
			perform(curl, job.req, resp);
		} else {
			resp.error = "Networking is unavailable";
		}
		if (!resp.ok() && !resp.cancelled) {
			Log::write("[net] %s %s -> %s", job.req.method.c_str(), util::ellipsize(job.req.url, 90).c_str(),
			           resp.describeError().c_str());
		}
		if (job.done) {
			job.done(resp);
		}
		lane.active--;
	}
	if (curl) {
		curl_easy_cleanup(curl);
	}
	std::lock_guard<std::mutex> lock(lane.mutex);
	lane.running = false;
	lane.cv.notify_all();
}

} // namespace

void init() {
	if (g_initialized) {
		return;
	}
	curl_global_init(CURL_GLOBAL_DEFAULT);
	g_initialized = true;
	for (int i = 0; i < 3; i++) {
		g_lanes[i].stop = false;
		g_lanes[i].running = true;
		sys::startThread([i] { laneLoop(i); }, true);
	}
}

void shutdown() {
	if (!g_initialized) {
		return;
	}
	for (auto &lane : g_lanes) {
		std::unique_lock<std::mutex> lock(lane.mutex);
		for (Job &job : lane.queue) {
			if (job.req.cancel) {
				*job.req.cancel = true;
			}
		}
		lane.queue.clear();
		lane.stop = true;
		lane.cv.notify_all();
	}
	// Give running transfers a moment to notice and wind down.
	for (auto &lane : g_lanes) {
		std::unique_lock<std::mutex> lock(lane.mutex);
		lane.cv.wait_for(lock, std::chrono::milliseconds(1500), [&] { return !lane.running; });
	}
	curl_global_cleanup();
	g_initialized = false;
}

void request(Lane lane, Request req, std::function<void(Response &)> done) {
	if (!g_initialized) {
		Response resp;
		resp.error = "Networking is not started";
		done(resp);
		return;
	}
	req.headers.reserve(req.headers.size() + 1);
	LaneState &l = g_lanes[(int)lane];
	std::lock_guard<std::mutex> lock(l.mutex);
	l.queue.push_back(Job{std::move(req), std::move(done)});
	l.cv.notify_one();
}

int pending(Lane lane) {
	LaneState &l = g_lanes[(int)lane];
	std::lock_guard<std::mutex> lock(l.mutex);
	return (int)l.queue.size() + l.active.load();
}

bool busy() { return pending(Lane::Api) + pending(Lane::Stream) + pending(Lane::Media) > 0; }

#else // no curl: simulator without networking

void init() {}
void shutdown() {}
void request(Lane, Request req, std::function<void(Response &)> done) {
	Response resp;
	resp.error = "No network in the simulator";
	(void)req;
	done(resp);
}
int pending(Lane) { return 0; }
bool busy() { return false; }

#endif

} // namespace net
