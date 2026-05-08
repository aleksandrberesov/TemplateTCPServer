#include "Json.h"

#include <cctype>
#include <cstdio>
#include <locale>
#include <sstream>
#include <string>

namespace tts::json {

namespace {

class Parser {
public:
    explicit Parser(const std::string& src) : src_(src), pos_(0) {}

    Value parse() {
        skipWs();
        Value v = parseValue();
        skipWs();
        if (pos_ != src_.size()) {
            throw JsonError("Trailing content after JSON value at offset " + std::to_string(pos_));
        }
        return v;
    }

private:
    const std::string& src_;
    size_t pos_;

    void skipWs() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    char peek() {
        if (pos_ >= src_.size()) throw JsonError("Unexpected end of input");
        return src_[pos_];
    }

    void expect(char c) {
        if (peek() != c) {
            throw JsonError(std::string("Expected '") + c + "' at offset " + std::to_string(pos_));
        }
        ++pos_;
    }

    Value parseValue() {
        skipWs();
        char c = peek();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseString();
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') return parseNull();
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        throw JsonError(std::string("Unexpected character '") + c + "' at offset " + std::to_string(pos_));
    }

    Value parseObject() {
        Value::Object obj;
        expect('{');
        skipWs();
        if (peek() == '}') { ++pos_; return Value(std::move(obj)); }
        while (true) {
            skipWs();
            if (peek() != '"') {
                throw JsonError("Expected string key at offset " + std::to_string(pos_));
            }
            std::string key = parseStringRaw();
            skipWs();
            expect(':');
            Value v = parseValue();
            obj[std::move(key)] = std::move(v);
            skipWs();
            char c = peek();
            if (c == ',') { ++pos_; continue; }
            if (c == '}') { ++pos_; break; }
            throw JsonError("Expected ',' or '}' at offset " + std::to_string(pos_));
        }
        return Value(std::move(obj));
    }

    Value parseArray() {
        Value::Array arr;
        expect('[');
        skipWs();
        if (peek() == ']') { ++pos_; return Value(std::move(arr)); }
        while (true) {
            arr.push_back(parseValue());
            skipWs();
            char c = peek();
            if (c == ',') { ++pos_; continue; }
            if (c == ']') { ++pos_; break; }
            throw JsonError("Expected ',' or ']' at offset " + std::to_string(pos_));
        }
        return Value(std::move(arr));
    }

    Value parseString() { return Value(parseStringRaw()); }

    std::string parseStringRaw() {
        expect('"');
        std::string out;
        while (pos_ < src_.size()) {
            char c = src_[pos_++];
            if (c == '"') return out;
            if (c == '\\') {
                if (pos_ >= src_.size()) throw JsonError("Truncated escape sequence");
                char e = src_[pos_++];
                switch (e) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'u': {
                        if (pos_ + 4 > src_.size()) throw JsonError("Truncated \\u escape");
                        unsigned cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = src_[pos_++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                            else throw JsonError("Invalid hex in \\u escape");
                        }
                        // Encode as UTF-8 (BMP only; surrogate pairs not handled).
                        if (cp < 0x80) {
                            out += static_cast<char>(cp);
                        } else if (cp < 0x800) {
                            out += static_cast<char>(0xC0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default:
                        throw JsonError(std::string("Invalid escape \\") + e);
                }
            } else {
                out += c;
            }
        }
        throw JsonError("Unterminated string");
    }

    Value parseBool() {
        if (src_.compare(pos_, 4, "true") == 0)  { pos_ += 4; return Value(true);  }
        if (src_.compare(pos_, 5, "false") == 0) { pos_ += 5; return Value(false); }
        throw JsonError("Invalid literal at offset " + std::to_string(pos_));
    }

    Value parseNull() {
        if (src_.compare(pos_, 4, "null") == 0) { pos_ += 4; return Value(nullptr); }
        throw JsonError("Invalid literal at offset " + std::to_string(pos_));
    }

    Value parseNumber() {
        size_t start = pos_;
        bool isReal = false;
        if (src_[pos_] == '-') ++pos_;
        while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) ++pos_;
        if (pos_ < src_.size() && src_[pos_] == '.') {
            isReal = true;
            ++pos_;
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) ++pos_;
        }
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            isReal = true;
            ++pos_;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) ++pos_;
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) ++pos_;
        }
        std::string num = src_.substr(start, pos_ - start);
        try {
            if (isReal) return Value(std::stod(num));
            return Value(static_cast<long long>(std::stoll(num)));
        } catch (const std::exception&) {
            throw JsonError("Invalid number: " + num);
        }
    }
};

void appendEscaped(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void dumpDouble(std::string& out, double d) {
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss.precision(9);
    oss << d;
    out += oss.str();
}

void dumpValue(const Value& v, std::string& out) {
    switch (v.kind()) {
        case Value::Kind::Null:    out += "null";                                 break;
        case Value::Kind::Bool:    out += v.toBool() ? "true" : "false";          break;
        case Value::Kind::Int:     out += std::to_string(v.toInt());              break;
        case Value::Kind::Double:  dumpDouble(out, v.toDouble());                 break;
        case Value::Kind::String:  appendEscaped(out, v.toString());              break;
        case Value::Kind::Array_: {
            out += '[';
            const auto& arr = v.toArray();
            for (size_t i = 0; i < arr.size(); ++i) {
                if (i) out += ',';
                dumpValue(arr[i], out);
            }
            out += ']';
            break;
        }
        case Value::Kind::Object_: {
            out += '{';
            const auto& obj = v.toObject();
            bool first = true;
            for (const auto& kv : obj) {
                if (!first) out += ',';
                first = false;
                appendEscaped(out, kv.first);
                out += ':';
                dumpValue(kv.second, out);
            }
            out += '}';
            break;
        }
    }
}

}  // namespace

bool Value::toBool() const {
    if (!isBool()) throw JsonError("Value is not bool");
    return bool_;
}

long long Value::toInt() const {
    if (kind_ == Kind::Int)    return int_;
    if (kind_ == Kind::Double) return static_cast<long long>(double_);
    throw JsonError("Value is not a number");
}

double Value::toDouble() const {
    if (kind_ == Kind::Double) return double_;
    if (kind_ == Kind::Int)    return static_cast<double>(int_);
    throw JsonError("Value is not a number");
}

const std::string& Value::toString() const {
    if (!isString()) throw JsonError("Value is not a string");
    return str_;
}

const Value::Array& Value::toArray() const {
    if (!isArray()) throw JsonError("Value is not an array");
    return arr_;
}

const Value::Object& Value::toObject() const {
    if (!isObject()) throw JsonError("Value is not an object");
    return obj_;
}

const Value* Value::find(const std::string& key) const {
    if (!isObject()) return nullptr;
    auto it = obj_.find(key);
    return it == obj_.end() ? nullptr : &it->second;
}

Value Value::parse(const std::string& src) {
    return Parser(src).parse();
}

std::string Value::dump() const {
    std::string out;
    dumpValue(*this, out);
    return out;
}

}  // namespace tts::json
