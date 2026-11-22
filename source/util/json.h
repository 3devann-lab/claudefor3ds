// rapidjson conveniences: tolerant getters (servers add, drop and retype fields) and a writer.
#pragma once

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <cstdint>
#include <string>

namespace json {

using Value = rapidjson::Value;
using Document = rapidjson::Document;

inline bool parse(Document &doc, const std::string &text) {
	doc.Parse(text.c_str(), text.size());
	return !doc.HasParseError();
}

inline const Value *get(const Value &v, const char *key) {
	if (!v.IsObject()) {
		return nullptr;
	}
	auto it = v.FindMember(key);
	if (it == v.MemberEnd() || it->value.IsNull()) {
		return nullptr;
	}
	return &it->value;
}

inline std::string str(const Value &v, const char *key, const std::string &def = "") {
	const Value *f = get(v, key);
	if (!f) {
		return def;
	}
	if (f->IsString()) {
		return std::string(f->GetString(), f->GetStringLength());
	}
	if (f->IsInt64()) {
		return std::to_string(f->GetInt64());
	}
	if (f->IsDouble()) {
		return std::to_string(f->GetDouble());
	}
	if (f->IsBool()) {
		return f->GetBool() ? "true" : "false";
	}
	return def;
}

inline double num(const Value &v, const char *key, double def = 0) {
	const Value *f = get(v, key);
	if (!f) {
		return def;
	}
	if (f->IsNumber()) {
		return f->GetDouble();
	}
	if (f->IsString()) {
		return atof(f->GetString());
	}
	return def;
}

inline int64_t i64(const Value &v, const char *key, int64_t def = 0) {
	const Value *f = get(v, key);
	if (!f) {
		return def;
	}
	if (f->IsInt64()) {
		return f->GetInt64();
	}
	if (f->IsNumber()) {
		return (int64_t)f->GetDouble();
	}
	if (f->IsString()) {
		return atoll(f->GetString());
	}
	return def;
}

inline bool boolean(const Value &v, const char *key, bool def = false) {
	const Value *f = get(v, key);
	if (!f) {
		return def;
	}
	if (f->IsBool()) {
		return f->GetBool();
	}
	if (f->IsNumber()) {
		return f->GetDouble() != 0;
	}
	return def;
}

inline const Value *arr(const Value &v, const char *key) {
	const Value *f = get(v, key);
	return f && f->IsArray() ? f : nullptr;
}

inline const Value *obj(const Value &v, const char *key) {
	const Value *f = get(v, key);
	return f && f->IsObject() ? f : nullptr;
}

inline std::string stringify(const Value &v) {
	rapidjson::StringBuffer sb;
	rapidjson::Writer<rapidjson::StringBuffer> w(sb);
	v.Accept(w);
	return std::string(sb.GetString(), sb.GetSize());
}

inline std::string quote(const std::string &s) {
	rapidjson::StringBuffer sb;
	rapidjson::Writer<rapidjson::StringBuffer> w(sb);
	w.String(s.c_str(), (rapidjson::SizeType)s.size());
	return std::string(sb.GetString(), sb.GetSize());
}

// Streaming writer with std::string keys and values.
class Writer {
  public:
	Writer() : w_(sb_) {}
	Writer &beginObject() {
		w_.StartObject();
		return *this;
	}
	Writer &endObject() {
		w_.EndObject();
		return *this;
	}
	Writer &beginArray() {
		w_.StartArray();
		return *this;
	}
	Writer &endArray() {
		w_.EndArray();
		return *this;
	}
	Writer &key(const char *k) {
		w_.Key(k);
		return *this;
	}
	Writer &value(const std::string &s) {
		w_.String(s.c_str(), (rapidjson::SizeType)s.size());
		return *this;
	}
	Writer &value(const char *s) {
		w_.String(s);
		return *this;
	}
	Writer &value(int64_t n) {
		w_.Int64(n);
		return *this;
	}
	Writer &value(int n) {
		w_.Int(n);
		return *this;
	}
	Writer &value(double d) {
		w_.Double(d);
		return *this;
	}
	Writer &value(bool b) {
		w_.Bool(b);
		return *this;
	}
	Writer &null() {
		w_.Null();
		return *this;
	}
	// Inserts already-serialised JSON.
	Writer &raw(const std::string &jsonText) {
		w_.RawValue(jsonText.c_str(), jsonText.size(), rapidjson::kObjectType);
		return *this;
	}
	Writer &value(const Value &v) {
		v.Accept(w_);
		return *this;
	}
	template <typename T> Writer &kv(const char *k, const T &v) {
		key(k);
		return value(v);
	}
	std::string str() const { return std::string(sb_.GetString(), sb_.GetSize()); }

  private:
	rapidjson::StringBuffer sb_;
	rapidjson::Writer<rapidjson::StringBuffer> w_;
};

} // namespace json
