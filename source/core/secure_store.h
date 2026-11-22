// The sign-in secret, sealed with AES-256-GCM under a key derived from this console's unique
// IDs, so a copied SD card file is useless on another 3DS.
#pragma once

#include <map>
#include <string>

struct Session {
	std::string kind = "api_key";
	std::map<std::string, std::string> fields;
	std::string apiKey() const {
		auto it = fields.find("api_key");
		return it == fields.end() ? "" : it->second;
	}
	bool valid() const { return !apiKey().empty(); }
};

namespace SecureStore {

bool load(Session &out);
bool save(const Session &session);
void clear();

// An API key saved as sdmc:/3ds/ClaudeFor3DS/api_key.txt (easier than typing it): sealed into
// session.bin, then the plain file is deleted. True if one was imported.
bool importKeyFile(Session &out);
// Light sanity check of a typed or loaded key.
bool looksLikeApiKey(const std::string &key);

} // namespace SecureStore
