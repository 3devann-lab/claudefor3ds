// The sign-in secret, sealed with AES-256-GCM under a key derived from this console's unique
// IDs, so a copied SD card file is useless on another 3DS. What the secret is depends on the
// sign-in route (still to be decided), so it is kept as named fields.
#pragma once

#include <map>
#include <string>

struct Session {
	std::string kind;
	std::map<std::string, std::string> fields;
	bool valid() const { return !kind.empty() && !fields.empty(); }
};

namespace SecureStore {

bool load(Session &out);
bool save(const Session &session);
void clear();

} // namespace SecureStore
