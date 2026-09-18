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
    // Time   — client's clock, first line of every connection (app → server); no reply.
    // Query  — cache probe (app → server), replied to with OK/no_data.
    // Rhythm — the whole record's raw samples in one message (app → server); acked.
    // Start  — play command (app → server); acked.
    // Points — DEPRECATED: former streamed frames, still decodable for compat.
    enum class Type { Time, Query, Rhythm, Start, Stop, Points, Upload, Ack };

    Type                       messageType = Type::Stop;
    std::optional<std::string> id;

    // Query / Rhythm — top-level identity of the rhythm.
    std::optional<std::string> pathology;
    std::optional<std::string> hash;        // Query only (cache key, §6)
    std::optional<std::string> revision;    // Query / Rhythm — human-readable label (§6)

    // Time
    std::optional<std::string> datetime;    // ISO-8601 with UTC offset (§3.7)

    // Start / Rhythm
    std::optional<int>                  sampleRate;
    std::map<std::string, std::string>  params;   // Start: {pathology, name}

    // Rhythm — every stored lead's raw .dat integer samples (baseline ~1024).
    std::map<std::string, std::vector<int>> leads;

    // Points (deprecated)
    std::optional<Lead>        lead;
    std::optional<std::string> identy;
    int                        offset = 0;
    std::vector<float>         values;

    std::optional<std::string> filename;
    std::optional<long long>   size;
    std::optional<long long>   bytes;

    static Message makeTime(std::optional<std::string> id, std::string datetime);
    static Message makeQuery(std::optional<std::string> id,
                             std::string                pathology,
                             std::optional<std::string> hash = std::nullopt,
                             std::optional<std::string> revision = std::nullopt);
    static Message makeRhythm(std::optional<std::string> id,
                              std::string                pathology,
                              std::optional<int>         sampleRate,
                              std::map<std::string, std::vector<int>> leads,
                              std::optional<std::string> revision = std::nullopt);
    static Message makeStart(std::optional<std::string> id = std::nullopt,
                             std::optional<int> sampleRate = std::nullopt,
                             std::map<std::string, std::string> params = {});
    static Message makeStop(std::optional<std::string> id = std::nullopt);
    static Message makePoints(std::optional<std::string> id,
                              std::optional<Lead>        lead,
                              std::optional<std::string> identy,
                              int                        offset,
                              std::vector<float>         values);
    static Message makeUpload(std::optional<std::string> id,
                              std::string                filename,
                              long long                  size);
    static Message makeAck(std::optional<std::string> id,
                           std::optional<std::string> filename,
                           std::optional<long long>   bytes);

    const char* typeStr() const;
};

std::string encode(const Message& msg);
Message     decode(const std::string& json);

}  // namespace tts::protocol
