#include "claude/stream_runner.h"

#include "core/main_queue.h"
#include "net/sse.h"

#include <vector>

namespace claude {

std::shared_ptr<StreamTurn> runStream(net::Request req, Conversation &conv, size_t messageIndex,
                                      TurnListener *listener, StreamEnd onEnd) {
	auto turn = std::make_shared<StreamTurn>();
	turn->generation = conv.generation;
	turn->messageIndex = messageIndex;
	req.streaming = true;
	req.cancel = turn->cancel;
	Conversation *convPtr = &conv;

	// Lives on the network thread only.
	auto parser = std::make_shared<std::vector<SseEvent>>();
	auto sse = std::make_shared<net::SseParser>(
	    [parser](const std::string &event, const std::string &data) { parser->push_back(SseEvent{event, data}); });

	auto deliver = [turn, convPtr, listener](std::vector<SseEvent> events) {
		MainQueue::post([turn, convPtr, listener, events = std::move(events)]() {
			if (turn->ended || convPtr->generation != turn->generation ||
			    turn->messageIndex >= convPtr->messages.size()) {
				return;
			}
			Message &msg = convPtr->messages[turn->messageIndex];
			bool changed = false;
			for (const SseEvent &ev : events) {
				changed |= turn->assembler.apply(msg, ev);
			}
			if (changed && listener) {
				listener->onTurnUpdate();
			}
		});
	};

	req.onChunk = [sse, parser, deliver](const char *data, size_t len) {
		sse->feed(data, len);
		if (!parser->empty()) {
			deliver(std::move(*parser));
			parser->clear();
		}
		return true;
	};

	net::request(net::Lane::Stream, std::move(req),
	             [turn, convPtr, sse, parser, deliver, onEnd](net::Response &resp) {
		             sse->finish();
		             if (!parser->empty()) {
			             deliver(std::move(*parser));
			             parser->clear();
		             }
		             net::Response copy = resp;
		             MainQueue::post([turn, convPtr, onEnd, copy]() {
			             if (turn->ended) {
				             return;
			             }
			             turn->ended = true;
			             bool current = convPtr->generation == turn->generation &&
			                            turn->messageIndex < convPtr->messages.size();
			             onEnd(*turn, copy, current);
		             });
	             });
	return turn;
}

} // namespace claude
