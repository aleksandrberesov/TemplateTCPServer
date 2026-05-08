#include "Server.h"

#include "EcgGenerator.h"
#include "Protocol.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using socket_t = SOCKET;
  static constexpr socket_t kInvalidSock = INVALID_SOCKET;
  #define TTS_CLOSESOCK closesocket
  #define TTS_LASTERR   WSAGetLastError()
#else
  #include <arpa/inet.h>
  #include <errno.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <unistd.h>
  using socket_t = int;
  static constexpr socket_t kInvalidSock = -1;
  #define TTS_CLOSESOCK ::close
  #define TTS_LASTERR   errno
#endif

namespace tts::server {

namespace {

class WinsockGuard {
public:
    WinsockGuard() {
#ifdef _WIN32
        WSADATA w;
        if (WSAStartup(MAKEWORD(2, 2), &w) != 0) {
            std::fprintf(stderr, "WSAStartup failed\n");
        }
#endif
    }
    ~WinsockGuard() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

void logLine(const std::string& s) {
    static std::mutex m;
    std::lock_guard<std::mutex> g(m);
    std::cout << s << std::endl;
}

class ClientSession {
public:
    ClientSession(socket_t s, std::string peer, int defaultRate, int batchSize, bool quiet)
        : sock_(s), peer_(std::move(peer)),
          defaultRate_(defaultRate), batchSize_(batchSize), quiet_(quiet) {}

    ClientSession(const ClientSession&)            = delete;
    ClientSession& operator=(const ClientSession&) = delete;

    ~ClientSession() {
        stopGenerator();
        if (sock_ != kInvalidSock) TTS_CLOSESOCK(sock_);
    }

    void run();

private:
    socket_t    sock_;
    std::string peer_;
    int         defaultRate_;
    int         batchSize_;
    bool        quiet_;

    std::mutex            writeMu_;
    std::atomic<bool>     sessionAlive_{true};

    std::thread           genThread_;
    std::atomic<bool>     genStop_{false};

    void log(const std::string& s) const { if (!quiet_) logLine(s); }

    bool sendLine(const std::string& s);
    void handleMessage(const protocol::Message& msg);
    void startGenerator(int rate, std::optional<std::string> requestId);
    void stopGenerator();
};

bool ClientSession::sendLine(const std::string& s) {
    std::lock_guard<std::mutex> g(writeMu_);
    std::string buf = s;
    buf.push_back('\n');
    size_t sent = 0;
    while (sent < buf.size()) {
        int n = ::send(sock_, buf.data() + sent,
#ifdef _WIN32
                       static_cast<int>(buf.size() - sent),
#else
                       buf.size() - sent,
#endif
                       0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

void ClientSession::run() {
    log("[" + peer_ + "] connected");

    std::string buf;
    buf.reserve(4096);
    char chunk[4096];

    while (sessionAlive_) {
        int n = ::recv(sock_, chunk,
#ifdef _WIN32
                       static_cast<int>(sizeof(chunk)),
#else
                       sizeof(chunk),
#endif
                       0);
        if (n == 0) break;          // peer closed
        if (n < 0) {
            log("[" + peer_ + "] recv error: " + std::to_string(TTS_LASTERR));
            break;
        }
        buf.append(chunk, static_cast<size_t>(n));

        size_t lineStart = 0;
        for (size_t i = 0; i < buf.size(); ++i) {
            if (buf[i] != '\n') continue;
            std::string line = buf.substr(lineStart, i - lineStart);
            lineStart = i + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();

            bool blank = true;
            for (char c : line) {
                if (!std::isspace(static_cast<unsigned char>(c))) { blank = false; break; }
            }
            if (blank) continue;

            try {
                protocol::Message msg = protocol::decode(line);
                handleMessage(msg);
            } catch (const protocol::ProtocolError& e) {
                log("[" + peer_ + "] decode error: " + e.what() + " (line: " + line + ")");
            }
        }
        if (lineStart > 0) buf.erase(0, lineStart);

        if (buf.size() > 1024 * 1024) {
            log("[" + peer_ + "] line buffer overflow, dropping connection");
            break;
        }
    }

    sessionAlive_ = false;
    stopGenerator();
    log("[" + peer_ + "] disconnected");
}

void ClientSession::handleMessage(const protocol::Message& msg) {
    using protocol::Message;
    log("[" + peer_ + "] recv: " + std::string(msg.typeStr()) +
        (msg.id ? " id=" + *msg.id : ""));

    switch (msg.messageType) {
        case Message::Type::Start: {
            int rate = msg.sampleRate.value_or(defaultRate_);
            if (rate <= 0) rate = defaultRate_ > 0 ? defaultRate_ : 250;
            stopGenerator();
            startGenerator(rate, msg.id);
            break;
        }
        case Message::Type::Stop:
            stopGenerator();
            break;
        case Message::Type::Points:
            // The test server doesn't act on inbound points; logging above
            // is enough.
            break;
    }
}

void ClientSession::startGenerator(int rate, std::optional<std::string> requestId) {
    int batch = batchSize_ > 0 ? batchSize_ : 25;
    genStop_  = false;

    log("[" + peer_ + "] generator starting @ " + std::to_string(rate) +
        " Hz, batch=" + std::to_string(batch));

    genThread_ = std::thread([this, rate, batch, requestId]() {
        EcgGenerator gen(rate);
        int                 offset = 0;
        const std::string   identy = "sim-" + std::to_string(
            static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()) % 100000);
        const auto interval = std::chrono::microseconds(
            static_cast<long long>(1'000'000.0 * batch / rate));
        auto next = std::chrono::steady_clock::now();

        while (!genStop_ && sessionAlive_) {
            std::vector<float> samples = gen.next(batch);
            auto msg = protocol::Message::makePoints(
                requestId, protocol::Lead::II, identy, offset, samples);
            std::string line = protocol::encode(msg);
            if (!sendLine(line)) {
                sessionAlive_ = false;
                break;
            }
            offset += batch;
            next   += interval;
            std::this_thread::sleep_until(next);
        }
    });
}

void ClientSession::stopGenerator() {
    genStop_ = true;
    if (genThread_.joinable()) genThread_.join();
}

}  // namespace

Server::Server(Options opts) : opts_(std::move(opts)) {}

Server::~Server() {
    socket_t s = static_cast<socket_t>(listenSock_.exchange(-1));
    if (s != kInvalidSock && s != static_cast<socket_t>(-1)) TTS_CLOSESOCK(s);
}

int Server::run() {
    WinsockGuard ws;

    socket_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kInvalidSock) {
        std::fprintf(stderr, "socket() failed: %d\n", static_cast<int>(TTS_LASTERR));
        return 1;
    }
    listenSock_.store(static_cast<long long>(s));

    int yes = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&yes), sizeof(yes));

    // Best-effort send timeout so a stuck peer eventually unblocks the
    // generator thread (5 seconds).
#ifdef _WIN32
    DWORD sndto = 5000;
    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO,
                 reinterpret_cast<const char*>(&sndto), sizeof(sndto));
#else
    struct timeval tv;
    tv.tv_sec  = 5;
    tv.tv_usec = 0;
    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(opts_.port);
    if (opts_.host == "0.0.0.0" || opts_.host.empty()) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        if (::inet_pton(AF_INET, opts_.host.c_str(), &addr.sin_addr) != 1) {
            std::fprintf(stderr, "invalid host: %s\n", opts_.host.c_str());
            TTS_CLOSESOCK(s);
            return 1;
        }
    }

    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::fprintf(stderr, "bind() failed: %d\n", static_cast<int>(TTS_LASTERR));
        TTS_CLOSESOCK(s);
        return 1;
    }
    if (::listen(s, 16) != 0) {
        std::fprintf(stderr, "listen() failed: %d\n", static_cast<int>(TTS_LASTERR));
        TTS_CLOSESOCK(s);
        return 1;
    }

    logLine("TemplateTCPServer listening on " + opts_.host +
            ":" + std::to_string(opts_.port) +
            " (rate=" + std::to_string(opts_.sampleRate) +
            ", batch=" + std::to_string(opts_.batchSize) + ")");

    while (!stopping_) {
        sockaddr_in peer{};
#ifdef _WIN32
        int peerLen = static_cast<int>(sizeof(peer));
#else
        socklen_t peerLen = sizeof(peer);
#endif
        socket_t cs = ::accept(s, reinterpret_cast<sockaddr*>(&peer), &peerLen);
        if (cs == kInvalidSock) {
            if (stopping_) break;
            logLine("accept() failed: " + std::to_string(TTS_LASTERR));
            continue;
        }
        char ip[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        std::string peerStr = std::string(ip) + ":" + std::to_string(ntohs(peer.sin_port));

        std::thread([cs, peerStr, this]() {
            ClientSession sess(cs, peerStr, opts_.sampleRate, opts_.batchSize, opts_.quiet);
            sess.run();
        }).detach();
    }

    TTS_CLOSESOCK(s);
    listenSock_.store(-1);
    return 0;
}

void Server::stop() {
    stopping_ = true;
    socket_t s = static_cast<socket_t>(listenSock_.exchange(-1));
    if (s != kInvalidSock && s != static_cast<socket_t>(-1)) TTS_CLOSESOCK(s);
}

}  // namespace tts::server
