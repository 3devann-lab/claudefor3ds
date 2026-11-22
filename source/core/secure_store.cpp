#include "core/secure_store.h"

#include "core/log.h"
#include "platform/system.h"
#include "util/json.h"
#include "util/util.h"

#include <cstdio>
#include <cstring>
#include <vector>

#ifdef __3DS__
#include <mbedtls/gcm.h>
#endif

namespace SecureStore {

namespace {
const char MAGIC[4] = {'C', '3', 'D', 'S'};
constexpr uint8_t VERSION = 1;
constexpr size_t IV_LEN = 12, TAG_LEN = 16;

std::string path() { return sys::dataDir() + "/session.bin"; }

bool readAll(const std::string &p, std::vector<uint8_t> &out) {
	FILE *f = fopen(p.c_str(), "rb");
	if (!f) {
		return false;
	}
	uint8_t buf[2048];
	size_t n;
	out.clear();
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
		out.insert(out.end(), buf, buf + n);
	}
	fclose(f);
	return true;
}

bool seal(const std::string &plain, std::vector<uint8_t> &out) {
	std::vector<uint8_t> key = sys::consoleSecret();
	uint8_t iv[IV_LEN];
	sys::randomBytes(iv, IV_LEN);
	std::vector<uint8_t> cipher(plain.size());
	uint8_t tag[TAG_LEN];
#ifdef __3DS__
	mbedtls_gcm_context gcm;
	mbedtls_gcm_init(&gcm);
	int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key.data(), 256);
	if (rc == 0) {
		rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, plain.size(), iv, IV_LEN, (const uint8_t *)MAGIC, 4,
		                               (const uint8_t *)plain.data(), cipher.data(), TAG_LEN, tag);
	}
	mbedtls_gcm_free(&gcm);
	if (rc != 0) {
		Log::write("[store] seal failed: -0x%04x", -rc);
		return false;
	}
#else
	// Simulator only: obfuscation, not encryption.
	for (size_t i = 0; i < plain.size(); i++) {
		cipher[i] = (uint8_t)plain[i] ^ key[i % key.size()] ^ iv[i % IV_LEN];
	}
	memset(tag, 0, TAG_LEN);
#endif
	out.assign(MAGIC, MAGIC + 4);
	out.push_back(VERSION);
	out.insert(out.end(), iv, iv + IV_LEN);
	out.insert(out.end(), tag, tag + TAG_LEN);
	out.insert(out.end(), cipher.begin(), cipher.end());
	return true;
}

bool unseal(const std::vector<uint8_t> &in, std::string &plain) {
	const size_t header = 4 + 1 + IV_LEN + TAG_LEN;
	if (in.size() < header || memcmp(in.data(), MAGIC, 4) != 0 || in[4] != VERSION) {
		return false;
	}
	const uint8_t *iv = in.data() + 5;
	const uint8_t *tag = iv + IV_LEN;
	const uint8_t *cipher = tag + TAG_LEN;
	size_t len = in.size() - header;
	std::vector<uint8_t> key = sys::consoleSecret();
	plain.assign(len, '\0');
#ifdef __3DS__
	mbedtls_gcm_context gcm;
	mbedtls_gcm_init(&gcm);
	int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key.data(), 256);
	if (rc == 0) {
		rc = mbedtls_gcm_auth_decrypt(&gcm, len, iv, IV_LEN, (const uint8_t *)MAGIC, 4, tag, TAG_LEN, cipher,
		                              (uint8_t *)&plain[0]);
	}
	mbedtls_gcm_free(&gcm);
	if (rc != 0) {
		Log::write("[store] session.bin does not belong to this console or is damaged");
		plain.clear();
		return false;
	}
#else
	for (size_t i = 0; i < len; i++) {
		plain[i] = (char)(cipher[i] ^ key[i % key.size()] ^ iv[i % IV_LEN]);
	}
#endif
	return true;
}

std::string serialize(const Session &s) {
	json::Writer w;
	w.beginObject();
	w.kv("kind", s.kind);
	w.key("fields").beginObject();
	for (const auto &c : s.fields) {
		w.kv(c.first.c_str(), c.second);
	}
	w.endObject();
	w.endObject();
	return w.str();
}

bool deserialize(const std::string &text, Session &s) {
	json::Document doc;
	if (!json::parse(doc, text) || !doc.IsObject()) {
		return false;
	}
	s.kind = json::str(doc, "kind");
	s.fields.clear();
	if (const json::Value *c = json::obj(doc, "fields")) {
		for (auto it = c->MemberBegin(); it != c->MemberEnd(); ++it) {
			if (it->value.IsString()) {
				s.fields[it->name.GetString()] = it->value.GetString();
			}
		}
	}
	return true;
}

} // namespace

bool load(Session &out) {
	std::vector<uint8_t> data;
	if (!readAll(path(), data)) {
		return false;
	}
	std::string plain;
	if (!unseal(data, plain)) {
		return false;
	}
	return deserialize(plain, out) && out.valid();
}

bool save(const Session &session) {
	std::vector<uint8_t> sealed;
	if (!seal(serialize(session), sealed)) {
		return false;
	}
	sys::ensureDir(sys::dataDir());
	std::string tmp = path() + ".tmp";
	FILE *f = fopen(tmp.c_str(), "wb");
	if (!f) {
		Log::write("[store] cannot write session");
		return false;
	}
	fwrite(sealed.data(), 1, sealed.size(), f);
	fclose(f);
	remove(path().c_str());
	return rename(tmp.c_str(), path().c_str()) == 0;
}

void clear() { remove(path().c_str()); }

bool looksLikeApiKey(const std::string &key) {
	if (key.size() < 20 || key.size() > 400) {
		return false;
	}
	for (char c : key) {
		if (c <= ' ' || c > '~') {
			return false;
		}
	}
	return true;
}

bool importKeyFile(Session &out) {
	const std::string file = sys::dataDir() + "/api_key.txt";
	std::vector<uint8_t> data;
	if (!readAll(file, data)) {
		return false;
	}
	std::string key = util::trim(std::string(data.begin(), data.end()));
	key = key.substr(0, key.find_first_of(" \r\n\t"));
	if (!looksLikeApiKey(key)) {
		Log::write("[store] api_key.txt does not contain a key");
		return false;
	}
	out = Session();
	out.fields["api_key"] = key;
	if (!save(out)) {
		return false;
	}
	remove(file.c_str());
	Log::write("[store] imported api_key.txt");
	return true;
}

} // namespace SecureStore
