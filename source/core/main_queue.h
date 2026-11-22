// Work posted from background threads, run on the main thread at the start of each frame.
// All app state is owned by the main thread; network threads only ever post into here.
#pragma once

#include <functional>
#include <memory>

namespace MainQueue {

void post(std::function<void()> fn);
// Runs everything posted so far. Returns true if anything ran (the frame must redraw).
bool drain();

} // namespace MainQueue

// Lets an object's posted callbacks notice that it has been destroyed meanwhile.
class LifeToken {
  public:
	LifeToken() : token_(std::make_shared<int>(0)) {}
	std::weak_ptr<int> weak() const { return token_; }
	void reset() { token_ = std::make_shared<int>(0); }

  private:
	std::shared_ptr<int> token_;
};
