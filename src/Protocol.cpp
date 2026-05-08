#include "Protocol.h"

#include "Json.h"

#include <cctype>
#include <locale>
#include <sstream>
#include <string>

namespace tts::protocol {

const char* leadName(Lead lead) {
    switch (lead) {
        case Lead::I:   return "I";
        case Lead::II:  return "II";
        case Lead::III: return "III";
        case Lead::aVR: return "aVR";
        case Lead::aVL: return "aVL";
        case Lead::aVF: return "aVF";
        case Lead::V1:  return "V1";
        case Lead::V2:  return "V2";
        case Lead::V3:  return "V3";
        case Lead::V4:  return "V4";
        case Lead::V5:  return "V5";
        case Lead::V6:  return "V6";
    }
    return "?";
}

std::optional<Lead> parseLead(const std::string& token) {
    size_t a = 0, b = token.size();
    while (a < b && std::isspace(static_cast<unsigned char>(token[a])))     ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(token[b - 1]))) --b;

    std::string s;
    s.reserve(b - a);
    for (size_t i = a; i < b; ++i) {
        s += static_cast<char>(std::tolower(static_cast<unsigned char>(token[i])));
    }

    if (s == "i")   return Lead::I;
    if (s == "ii")  return Lead::II;
    if (s == "iii") return Lead::III;
    if (s == "avr") return Lead::aVR;
    if (s == "avl") return Lead::aVL;
    if (s == "avf") return Lead::aVF;
    if (s == "v1")  return Lead::V1;
    if (s == "v2")  return Lead::V2;
    if (s == "v3")  return Lead::V3;
    if (s == "v4")  return Lead::V4;
    if (s == "v5")  return Lead::V5;
    if (s == "v6")  return Lead::V6;
    return std::nullopt;
}

Message Message::makeStart(std::optional<std::string> id,
                           std::optional<int>         sampleRate,
                           std::map<std::string, std::string> params) {
    Message m;
    m.messageType = Type::Start;
    m.id          = std::move(id);
    m.sampleRate  = sampleRate;
    m.params      = std::move(params);
    return m;
}

Message Message::makeStop(std::optional<std::string> id) {
    Message m;
    m.messageType = Type::Stop;
    m.id          = std::move(id);
    return m;
}

Message Message::makePoints(std::optional<std::string> id,
                            std::optional<Lead>        lead,
                            std::optional<std::string> identy,
                            int                        offset,
                            std::vector<float>         values) {
    Message m;
    m.messageType = Type::Points;
    m.id          = std::move(id);
    m.lead        = lead;
    m.identy      = std::move(identy);
    m.offset      = offset;
    m.values      = std::move(values);
    return m;
}

const char* Message::typeStr() const {
    switch (messageType) {
        case Type::Start:  return "start";
        case Type::Stop:   return "stop";
        case Type::Points: return "points";
    }
    return "?";
}

std::string encode(const Message& msg) {
    using tts::json::Value;
    Value::Object obj;
    obj["type"] = Value(std::string(msg.typeStr()));
    if (msg.id.has_value()) obj["id"] = Value(*msg.id);

    switch (msg.messageType) {
        case Message::Type::Start: {
            if (msg.sampleRate.has_value()) {
                obj["sampleRate"] = Value(static_cast<long long>(*msg.sampleRate));
            }
            if (!msg.params.empty()) {
                Value::Object p;
                for (const auto& kv : msg.params) p[kv.first] = Value(kv.second);
                obj["params"] = Value(std::move(p));
            }
            break;
        }
        case Message::Type::Stop:
            break;
        case Message::Type::Points: {
            if (msg.lead.has_value()) {
                obj["lead"] = Value(std::string(leadName(*msg.lead)));
            }
            if (msg.identy.has_value()) obj["identy"] = Value(*msg.identy);
            if (msg.offset != 0)        obj["offset"] = Value(static_cast<long long>(msg.offset));
            Value::Array vals;
            vals.reserve(msg.values.size());
            for (float v : msg.values) vals.push_back(Value(static_cast<double>(v)));
            obj["values"] = Value(std::move(vals));
            break;
        }
    }
    return Value(std::move(obj)).dump();
}

namespace {

std::optional<std::string> getOptString(const tts::json::Value& obj, const std::string& key) {
    const auto* v = obj.find(key);
    if (!v || v->isNull()) return std::nullopt;
    if (!v->isString()) throw ProtocolError("Field '" + key + "' must be string");
    return v->toString();
}

std::optional<int> getOptInt(const tts::json::Value& obj, const std::string& key) {
    const auto* v = obj.find(key);
    if (!v || v->isNull()) return std::nullopt;
    if (!v->isNumber()) throw ProtocolError("Field '" + key + "' must be number");
    return static_cast<int>(v->toInt());
}

std::string primitiveToString(const tts::json::Value& v) {
    if (v.isString()) return v.toString();
    if (v.isInt())    return std::to_string(v.toInt());
    if (v.isDouble()) {
        std::ostringstream oss;
        oss.imbue(std::locale::classic());
        oss << v.toDouble();
        return oss.str();
    }
    if (v.isBool())   return v.toBool() ? "true" : "false";
    return "";
}

}  // namespace

Message decode(const std::string& json) {
    using tts::json::Value;
    Value root;
    try {
        root = Value::parse(json);
    } catch (const tts::json::JsonError& e) {
        throw ProtocolError(std::string("Invalid JSON: ") + e.what());
    }
    if (!root.isObject()) throw ProtocolError("Invalid JSON: top-level value must be object");

    auto typeOpt = getOptString(root, "type");
    if (!typeOpt) throw ProtocolError("Missing required field: type");
    auto id = getOptString(root, "id");

    if (*typeOpt == "start") {
        Message m = Message::makeStart(id);
        m.sampleRate = getOptInt(root, "sampleRate");
        const auto* p = root.find("params");
        if (p && !p->isNull()) {
            if (!p->isObject()) throw ProtocolError("Field 'params' must be object");
            for (const auto& kv : p->toObject()) {
                if (kv.second.isNull()) continue;
                m.params[kv.first] = primitiveToString(kv.second);
            }
        }
        return m;
    }
    if (*typeOpt == "stop") {
        return Message::makeStop(id);
    }
    if (*typeOpt == "points") {
        Message m;
        m.messageType = Message::Type::Points;
        m.id          = id;
        if (auto leadStr = getOptString(root, "lead")) {
            auto parsed = parseLead(*leadStr);
            if (!parsed) throw ProtocolError("Unknown lead: " + *leadStr);
            m.lead = *parsed;
        }
        m.identy = getOptString(root, "identy");
        if (auto off = getOptInt(root, "offset")) m.offset = *off;
        const auto* vals = root.find("values");
        if (!vals || vals->isNull()) throw ProtocolError("Missing required field: values");
        if (!vals->isArray())        throw ProtocolError("Field 'values' must be array");
        m.values.reserve(vals->toArray().size());
        size_t idx = 0;
        for (const auto& v : vals->toArray()) {
            if (!v.isNumber()) {
                throw ProtocolError("Invalid number in values at index " + std::to_string(idx));
            }
            m.values.push_back(static_cast<float>(v.toDouble()));
            ++idx;
        }
        return m;
    }
    throw ProtocolError("Unknown message type: " + *typeOpt);
}

}  // namespace tts::protocol
