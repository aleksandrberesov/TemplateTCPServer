#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace tts::server {

struct Options {
    std::string host            = "0.0.0.0";
    uint16_t    port            = 9000;
    int         sampleRate      = 250;
    int         batchSize       = 25;
    bool        quiet           = false;
    std::string uploadDir       = "uploads";
    long long   maxUploadBytes  = 100LL * 1024 * 1024;
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
