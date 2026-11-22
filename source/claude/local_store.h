// Conversations kept on the SD card (the API keeps no history of its own).
#pragma once

#include "claude/backend.h"

#include <string>
#include <vector>

namespace claude {
namespace LocalStore {

std::vector<ConversationSummary> list(AppMode mode);
bool load(Conversation &conv);
void save(const Conversation &conv);
void remove(AppMode mode, const std::string &id);

} // namespace LocalStore
} // namespace claude
