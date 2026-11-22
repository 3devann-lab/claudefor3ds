// Runs one streaming request: SSE is parsed on the network thread, events are applied to the
// conversation on the main thread in batches (one per network read).
#pragma once

#include "claude/backend.h"
#include "claude/stream.h"
#include "net/http.h"

#include <functional>
#include <memory>

namespace claude {

struct StreamTurn {
	net::CancelFlag cancel = net::makeCancel();
	StreamAssembler assembler;
	uint32_t generation = 0;
	size_t messageIndex = 0;
	bool ended = false;
};

// Called on the main thread when the stream ends. `current` is false if the conversation was
// switched meanwhile (then nothing should be touched).
using StreamEnd = std::function<void(StreamTurn &turn, const net::Response &resp, bool current)>;

std::shared_ptr<StreamTurn> runStream(net::Request req, Conversation &conv, size_t messageIndex,
                                      TurnListener *listener, StreamEnd onEnd);

} // namespace claude
