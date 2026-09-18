#include "Server.h"

#include "CommandDispatcher.h"
#include "ICommandHandler.h"
#include "Protocol.h"
#include "RhythmCache.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
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
  #include <netdb.h>
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

// A `rhythm` line can be megabytes; keep the log readable.
std::string truncateForLog(const std::string& s, size_t limit = 240) {
    if (s.size() <= limit) return s;
    return s.substr(0, limit) + "…(" + std::to_string(s.size()) + " bytes)";
}

std::vector<std::string> getLocalIPs() {
    char hostname[256];
    if (::gethostname(hostname, sizeof(hostname)) != 0) return {};
    addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (::getaddrinfo(hostname, nullptr, &hints, &res) != 0) return {};
    std::vector<std::string> ips;
    for (auto* r = res; r; r = r->ai_next) {
        char buf[INET_ADDRSTRLEN];
        auto* sin = reinterpret_cast<sockaddr_in*>(r->ai_addr);
        if (::inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf)))
            ips.emplace_back(buf);
    }
    ::freeaddrinfo(res);
    return ips;
}

class ClientSession : public tts::IClientContext {
public:
    ClientSession(socket_t s, std::string peer, bool quiet,
                  std::string uploadDir, long long maxUploadBytes,
                  long long maxLineBytes, long long processDelayMs,
                  tts::RhythmCache* cache, tts::CommandDispatcher* dispatcher)
        : sock_(s), peer_(std::move(peer)), quiet_(quiet),
          uploadDir_(std::move(uploadDir)), maxUploadBytes_(maxUploadBytes),
          maxLineBytes_(maxLineBytes), processDelayMs_(processDelayMs),
          cache_(cache), dispatcher_(dispatcher) {}

    ClientSession(const ClientSession&)            = delete;
    ClientSession& operator=(const ClientSession&) = delete;

    ~ClientSession() {
        if (sock_ != kInvalidSock) TTS_CLOSESOCK(sock_);
    }

    void run();

private:
    socket_t    sock_;
    std::string peer_;
    bool        quiet_;
    std::string             uploadDir_;
    long long               maxUploadBytes_;
    long long               maxLineBytes_;
    long long               processDelayMs_;
    tts::RhythmCache*       cache_;
    tts::CommandDispatcher* dispatcher_;

    std::mutex            writeMu_;
    std::atomic<bool>     sessionAlive_{true};

    struct PendingUpload { std::string filename; long long size; std::optional<std::string> id; };

    // The (pathology, hash) of the most recent `query` we answered "no_data",
    // awaiting its single `rhythm` message (§3.3). The `rhythm` message itself
    // carries no hash, so we remember it here to key the cache entry (§6).
    struct PendingQuery { std::string pathology; std::string hash; std::string revision; };
    std::optional<PendingQuery> pendingQuery_;

    // IClientContext
    void sendJson(const tts::json::Value& msg) override { sendLine(msg.dump()); }
    const std::string& peerAddress() const override { return peer_; }

    void log(const std::string& s) const { if (!quiet_) logLine(s); }

    bool sendLine(const std::string& s);
    std::optional<PendingUpload> handleMessage(const protocol::Message& msg);
    void receiveUpload(const std::string& rawFilename, long long size,
                       const std::optional<std::string>& id, std::string& buf);

    // Protocol handling.
    void handleQuery(const protocol::Message& msg);
    void handleRhythm(const protocol::Message& msg);
    void sendVerdict(bool cached, const std::optional<std::string>& id);
    void sendAck(const std::optional<std::string>& id);
    void notifyHandler(const char* type, tts::json::Value payload);
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

// A `rhythm` message (§3.3) is one JSON object on a single, possibly very large
// line, so avoid re-scanning the incomplete prefix on every recv: `searchPos`
// remembers how far into `buf` we have already looked for a newline.
void ClientSession::run() {
    log("[" + peer_ + "] connected");

    std::string buf;
    buf.reserve(1 << 16);
    char   chunk[1 << 16];
    size_t searchPos = 0;

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

        size_t pos;
        while (sessionAlive_ && (pos = buf.find('\n', searchPos)) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);   // drop the line and its '\n'
            searchPos = 0;
            if (!line.empty() && line.back() == '\r') line.pop_back();

            bool blank = true;
            for (char c : line) {
                if (!std::isspace(static_cast<unsigned char>(c))) { blank = false; break; }
            }
            if (blank) continue;

            std::optional<PendingUpload> pending;
            try {
                log("[" + peer_ + "] recv raw: " + truncateForLog(line));
                protocol::Message msg = protocol::decode(line);
                pending = handleMessage(msg);
            } catch (const protocol::ProtocolError& e) {
                log("[" + peer_ + "] decode error: " + e.what() +
                    " (line: " + truncateForLog(line) + ")");
            }

            if (pending) {
                // Whatever is left in `buf` is the start of the raw payload;
                // receiveUpload consumes exactly `size` bytes and leaves any
                // bytes pipelined after it (e.g. the query that follows the
                // manifest, §2) for the loop below to parse.
                receiveUpload(pending->filename, pending->size, pending->id, buf);
                searchPos = 0;
            }
        }
        // No newline in [searchPos, end): everything scanned, resume from here.
        searchPos = buf.size();

        if (static_cast<long long>(buf.size()) > maxLineBytes_) {
            log("[" + peer_ + "] line buffer overflow (" + std::to_string(buf.size()) +
                " > " + std::to_string(maxLineBytes_) + " bytes), dropping connection");
            break;
        }
    }

    sessionAlive_ = false;
    log("[" + peer_ + "] disconnected");
}

std::optional<ClientSession::PendingUpload> ClientSession::handleMessage(const protocol::Message& msg) {
    using protocol::Message;
    log("[" + peer_ + "] recv: " + std::string(msg.typeStr()) +
        (msg.id ? " id=" + *msg.id : ""));

    switch (msg.messageType) {
        case Message::Type::Time: {
            // Client clock (§3.7), first line of the connection. Advisory — no
            // reply (an extra reply would desync the first query's verdict).
            const std::string dt = msg.datetime.value_or("");
            log("[" + peer_ + "] time (client clock) " + dt);
            tts::json::Value::Object p;
            p["datetime"] = tts::json::Value(dt);
            notifyHandler("time", tts::json::Value(std::move(p)));
            return std::nullopt;
        }
        case Message::Type::Query:
            handleQuery(msg);
            return std::nullopt;
        case Message::Type::Rhythm:
            handleRhythm(msg);
            return std::nullopt;
        case Message::Type::Start: {
            // Play command (§3.4). The app waits for exactly one acknowledgement
            // before it starts drawing, so ack every start. Data was delivered
            // earlier via the query/rhythm handshake.
            auto it = msg.params.find("pathology");
            const std::string pathology = (it == msg.params.end()) ? std::string{} : it->second;
            const bool held = cache_ && cache_->get(pathology).has_value();
            sendAck(msg.id);
            log("[" + peer_ + "] start (play) pathology='" + pathology + "'" +
                (pathology.empty() ? "" : (held ? " [cached]" : " [NOT cached]")) + " -> ack");
            tts::json::Value::Object p;
            p["pathology"] = tts::json::Value(pathology);
            p["held"]      = tts::json::Value(held);
            notifyHandler("start", tts::json::Value(std::move(p)));
            return std::nullopt;
        }
        case Message::Type::Stop:
            // Advisory (§3.5) — no reply.
            log("[" + peer_ + "] stop (monitor stopped)");
            notifyHandler("stop", tts::json::Value(tts::json::Value::Object{}));
            return std::nullopt;
        case Message::Type::Upload:
            return PendingUpload{
                msg.filename.value_or("upload.bin"),
                msg.size.value_or(0),
                msg.id
            };
        case Message::Type::Points:
            // Deprecated (§3.7): the app no longer streams frames. Ignore.
            log("[" + peer_ + "] points ignored: deprecated message, expect 'rhythm' instead");
            return std::nullopt;
        case Message::Type::Ack:
            return std::nullopt;
    }
    return std::nullopt;
}

// query — the cache probe (§3.1, §4). Reply with exactly one line: "OK" when the
// rhythm is already held, "no_data" when its samples must be sent as a `rhythm`.
void ClientSession::handleQuery(const protocol::Message& msg) {
    const std::string pathology = msg.pathology.value_or("");
    const std::string hash      = msg.hash.value_or("");
    const std::string revision  = msg.revision.value_or("");

    const bool cached = cache_ && cache_->contains(pathology, hash);
    sendVerdict(cached, msg.id);

    log("[" + peer_ + "] query pathology='" + pathology + "' rev='" + revision +
        "' hash='" + hash + "' -> " + (cached ? "OK (cached)" : "no_data (need samples)"));

    // On no_data the app will send exactly one `rhythm` message next. Remember
    // the queried (pathology, hash, revision) so we can key its cache entry —
    // `rhythm` carries no hash (§3.3), and revision is a human-readable label (§6).
    pendingQuery_ = cached ? std::nullopt
                           : std::optional<PendingQuery>(PendingQuery{pathology, hash, revision});

    tts::json::Value::Object p;
    p["pathology"] = tts::json::Value(pathology);
    p["hash"]      = tts::json::Value(hash);
    p["revision"]  = tts::json::Value(revision);
    p["cached"]    = tts::json::Value(cached);
    notifyHandler("query", tts::json::Value(std::move(p)));
}

// rhythm — the whole record in one message (§3.3). Store every lead's raw ADC
// samples in the shared cache, keyed by the (pathology, hash) from the query
// that requested it, so the next query for that pair answers "OK", then send the
// single required acknowledgement (§3.3, §4).
void ClientSession::handleRhythm(const protocol::Message& msg) {
    const std::string pathology = msg.pathology.value_or("");
    const int sampleRate = msg.sampleRate.value_or(500);

    // The hash comes from the preceding no_data query; fall back to empty
    // (id-only caching) if the rhythm arrived without one (e.g. fail-open).
    // The revision travels with the rhythm; fall back to the query's value.
    std::string hash;
    std::string revision = msg.revision.value_or("");
    if (pendingQuery_ && pendingQuery_->pathology == pathology) {
        hash = pendingQuery_->hash;
        if (revision.empty()) revision = pendingQuery_->revision;
    }
    pendingQuery_.reset();

    size_t total = 0;
    for (const auto& kv : msg.leads) total += kv.second.size();

    // Imitate the time a real server spends ingesting the record before it can
    // acknowledge (on a local loopback the transfer itself is instant). Sleeps
    // this session's thread only, so other clients are unaffected.
    if (processDelayMs_ > 0) {
        log("[" + peer_ + "] rhythm '" + pathology + "' rev='" + revision + "' hash='" + hash +
            "' (" + std::to_string(msg.leads.size()) + " leads, " + std::to_string(total) +
            " samples @ " + std::to_string(sampleRate) + " Hz) — processing " +
            std::to_string(processDelayMs_) + " ms…");
        std::this_thread::sleep_for(std::chrono::milliseconds(processDelayMs_));
    }

    if (cache_ && !pathology.empty()) {
        RhythmCache::Rhythm stored;
        stored.hash       = hash;
        stored.revision   = revision;
        stored.sampleRate = sampleRate;
        stored.leads      = msg.leads;
        cache_->store(pathology, std::move(stored));
    }

    sendAck(msg.id);   // exactly one acknowledgement per rhythm (§4)

    log("[" + peer_ + "] rhythm '" + pathology + "' rev='" + revision + "' hash='" + hash +
        "' (" + std::to_string(msg.leads.size()) + " leads, " + std::to_string(total) +
        " samples @ " + std::to_string(sampleRate) + " Hz) -> ack");

    tts::json::Value::Object p;
    p["pathology"]  = tts::json::Value(pathology);
    p["hash"]       = tts::json::Value(hash);
    p["revision"]   = tts::json::Value(revision);
    p["leads"]      = tts::json::Value(static_cast<long long>(msg.leads.size()));
    p["samples"]    = tts::json::Value(static_cast<long long>(total));
    p["sampleRate"] = tts::json::Value(static_cast<long long>(sampleRate));
    notifyHandler("rhythm", tts::json::Value(std::move(p)));
}

// Reply to a query with the cache verdict (§3.2). Echoes the request id as JSON
// when present (recommended), otherwise sends the bare token; the app accepts
// both. Newline-terminated by sendLine so the app never has to wait out the 4 s
// fail-open timeout (§4).
void ClientSession::sendVerdict(bool cached, const std::optional<std::string>& id) {
    if (id) {
        tts::json::Value::Object o;
        o["id"]     = tts::json::Value(*id);
        o["status"] = tts::json::Value(std::string(cached ? "ok" : "no_data"));
        sendLine(tts::json::Value(std::move(o)).dump());
    } else {
        sendLine(cached ? "OK" : "no_data");
    }
}

// Acknowledge a `rhythm` or `start` with exactly one reply (§3.3, §3.4, §4):
// {"id":…,"status":"ok"} when the request carried an id, else the bare "OK".
void ClientSession::sendAck(const std::optional<std::string>& id) {
    sendVerdict(true, id);
}

// Fire the optional extensibility hook after the mandatory reply has been sent.
void ClientSession::notifyHandler(const char* type, tts::json::Value payload) {
    if (dispatcher_) dispatcher_->dispatch(type, payload, *this);
}

namespace {

std::string sanitizeFilename(const std::string& raw) {
    size_t slash = raw.find_last_of("/\\");
    std::string name = (slash == std::string::npos) ? raw : raw.substr(slash + 1);
    if (name.empty() || name == "." || name == "..") return "";
    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        if (c < 32 || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|' || c == '\\' || c == '/')
            out += '_';
        else
            out += static_cast<char>(c);
    }
    for (char c : out) if (c != '_') return out;
    return "";
}

}  // namespace

void ClientSession::receiveUpload(const std::string& rawFilename, long long size,
                                  const std::optional<std::string>& id, std::string& buf) {
    std::string filename = sanitizeFilename(rawFilename);
    if (filename.empty()) {
        log("[" + peer_ + "] upload rejected: invalid filename '" + rawFilename + "'");
        sessionAlive_ = false;
        return;
    }
    if (size > maxUploadBytes_) {
        log("[" + peer_ + "] upload rejected: size " + std::to_string(size) +
            " exceeds limit " + std::to_string(maxUploadBytes_));
        sessionAlive_ = false;
        return;
    }

    std::filesystem::path basePath = std::filesystem::path(uploadDir_) / filename;
    auto stem = basePath.stem().string();
    auto ext  = basePath.extension().string();

    std::FILE* f = nullptr;
    std::filesystem::path path;
    for (int n = 0; n <= 9999 && !f; ++n) {
        path = (n == 0) ? basePath
                        : std::filesystem::path(uploadDir_) / (stem + "_" + std::to_string(n) + ext);
#ifdef _WIN32
        fopen_s(&f, path.string().c_str(), "wbx");
#else
        f = std::fopen(path.string().c_str(), "wbx");
#endif
        if (!f && errno != EEXIST) break;
    }
    if (!f) {
        log("[" + peer_ + "] upload failed: cannot open '" + path.string() + "'");
        sessionAlive_ = false;
        return;
    }
    filename = path.filename().string();

    log("[" + peer_ + "] upload started: " + filename + " (" + std::to_string(size) + " bytes)");

    long long remaining = size;

    if (!buf.empty()) {
        long long avail = std::min(static_cast<long long>(buf.size()), remaining);
        std::fwrite(buf.data(), 1, static_cast<size_t>(avail), f);
        remaining -= avail;
        buf.erase(0, static_cast<size_t>(avail));
    }

    char chunk[65536];
    while (remaining > 0 && sessionAlive_) {
        int toRead = static_cast<int>(std::min(static_cast<long long>(sizeof(chunk)), remaining));
        int n = ::recv(sock_, chunk,
#ifdef _WIN32
                       toRead,
#else
                       static_cast<size_t>(toRead),
#endif
                       0);
        if (n <= 0) {
            log("[" + peer_ + "] upload interrupted after " +
                std::to_string(size - remaining) + " bytes");
            std::fclose(f);
            sessionAlive_ = false;
            return;
        }
        std::fwrite(chunk, 1, static_cast<size_t>(n), f);
        remaining -= n;
    }

    std::fclose(f);
    log("[" + peer_ + "] upload complete: " + filename + " (" + std::to_string(size) + " bytes)");
    sendLine(protocol::encode(protocol::Message::makeAck(id, filename, size)));
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

    // Best-effort send timeout so a stuck peer eventually unblocks (5 seconds).
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

    {
        std::error_code ec;
        std::filesystem::create_directories(opts_.uploadDir, ec);
        if (ec) std::fprintf(stderr, "Warning: cannot create upload dir '%s': %s\n",
                             opts_.uploadDir.c_str(), ec.message().c_str());
    }

    {
        std::string msg = "TemplateTCPServer listening on " + opts_.host +
                          ":" + std::to_string(opts_.port);

        if (opts_.host == "0.0.0.0" || opts_.host.empty()) {
            auto ips = getLocalIPs();
            if (!ips.empty()) {
                msg += "\nConnect clients to:";
                for (const auto& ip : ips)
                    msg += "\n  " + ip + ":" + std::to_string(opts_.port);
            }
        } else {
            msg += "\nConnect clients to: " + opts_.host + ":" + std::to_string(opts_.port);
        }
        logLine(msg);
    }

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
            ClientSession sess(cs, peerStr, opts_.quiet,
                               opts_.uploadDir, opts_.maxUploadBytes, opts_.maxLineBytes,
                               opts_.processDelayMs, opts_.cache, opts_.dispatcher);
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
