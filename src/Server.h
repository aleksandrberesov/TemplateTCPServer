#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace tts { class CommandDispatcher; }
namespace tts { class RhythmCache; }

namespace tts::server {

struct Options {
    std::string host            = "0.0.0.0";
    uint16_t    port            = 9000;
    bool        quiet           = false;
    std::string uploadDir       = "uploads";
    long long   maxUploadBytes  = 100LL * 1024 * 1024;
    // Max size of a single JSON line. A `rhythm` message carries every lead's
    // raw samples on one line and can be large, so this is generous.
    long long   maxLineBytes    = 64LL * 1024 * 1024;

    // Shared rhythm cache backing the §4 handshake — not owned by Options.
    // Set this before calling Server::run() so every client session shares one
    // store. When null the server treats every rhythm as un-cached (always
    // replies "no_data").
    tts::RhythmCache* cache = nullptr;

    // Optional extensibility hook — not owned by Options. When set, the server
    // invokes it (by message type: "start", "stop", "points") AFTER it has sent
    // any mandatory handshake reply, so custom handlers can observe traffic.
    // Handlers must not send handshake verdicts themselves (see main.cpp).
    tts::CommandDispatcher* dispatcher = nullptr;
};

class Server {
public:
    explicit Server(Options opts);
    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;
    ~Server();

    int  run();
    void stop();

private:
    Options             opts_;
    std::atomic<bool>   stopping_{false};
    std::atomic<long long> listenSock_{-1};
};

}  // namespace tts::server
