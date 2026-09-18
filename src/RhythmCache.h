#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tts {

// Server-side store of rhythms received via the CardioSimulator cache handshake
// (see Docs/tcpprotocol.md §4, §6). Shared by every client session for the life
// of the server, so a rhythm received on one connection is still "held" after
// the app reconnects. All methods are thread-safe.
//
// The cache key is the pair (pathology, hash):
//   - same pathology + same hash  -> the server already holds this rhythm  -> "OK"
//   - same pathology + new  hash  -> the rhythm was edited, resend it       -> "no_data"
//   - unknown pathology           -> not held                              -> "no_data"
class RhythmCache {
public:
    // One rhythm's samples, delivered in a single `rhythm` message (§3.3).
    struct Rhythm {
        std::string hash;                                  // content fingerprint (§6, cache key)
        std::string revision;                              // human-readable version label (§6)
        int         sampleRate = 500;                      // samples per second
        std::map<std::string, std::vector<int>> leads;     // lead token -> raw ADC samples
    };

    // True when a rhythm with this pathology is held AND its stored hash equals
    // `hash`. When both hashes are empty the match is by pathology alone (the app
    // may omit the hash, in which case the server falls back to id-only caching).
    bool contains(const std::string& pathology, const std::string& hash) const;

    // Store (or replace) the rhythm held for `pathology`.
    void store(const std::string& pathology, Rhythm rhythm);

    // Copy of the rhythm held for `pathology`, if any (for display / inspection).
    std::optional<Rhythm> get(const std::string& pathology) const;

    // Number of distinct rhythms currently held.
    std::size_t size() const;

private:
    mutable std::mutex                        mu_;
    std::unordered_map<std::string, Rhythm>   byPathology_;
};

}  // namespace tts
