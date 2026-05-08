#pragma once

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace tts::protocol {

class ProtocolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class Lead { I, II, III, aVR, aVL, aVF, V1, V2, V3, V4, V5, V6 };

const char*          leadName(Lead lead);
std::optional<Lead>  parseLead(const std::string& token);

struct Message {
    enum class Type { Start, Stop, Points };

    Type                       messageType = Type::Stop;
    std::optional<std::string> id;

    // Start
    std::optional<int>                  sampleRate;
    std::map<std::string, std::string>  params;

    // Points
    std::optional<Lead>        lead;
    std::optional<std::string> identy;
    int                        offset = 0;
    std::vector<float>         values;

    static Message makeStart(std::optional<std::string> id = std::nullopt,
                             std::optional<int> sampleRate = std::nullopt,
                             std::map<std::string, std::string> params = {});
    static Message makeStop(std::optional<std::string> id = std::nullopt);
    static Message makePoints(std::optional<std::string> id,
                              std::optional<Lead>        lead,
                              std::optional<std::string> identy,
                              int                        offset,
                              std::vector<float>         values);

    const char* typeStr() const;
};

std::string encode(const Message& msg);
Message     decode(const std::string& json);

}  // namespace tts::protocol
