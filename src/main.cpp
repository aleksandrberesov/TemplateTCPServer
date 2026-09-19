#include "Server.h"

#include "CommandDispatcher.h"
#include "RhythmCache.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif
#include <fstream>

namespace {

tts::server::Server* g_server = nullptr;

void broadcastToEmulator(const std::string& jsonLine) {
#ifdef _WIN32
    HANDLE hPipe = CreateFileA(
        "\\\\.\\pipe\\CardioEmulatorPipe",
        GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);
    if (hPipe != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(hPipe, jsonLine.data(), static_cast<DWORD>(jsonLine.size()), &written, NULL);
        CloseHandle(hPipe);
    }
#endif
    // Also save as active_rhythm.json as IPC fallback
    std::ofstream out("active_rhythm.json");
    if (out.is_open()) {
        out << jsonLine;
    }
}

void onSignal(int) {
    if (g_server) g_server->stop();
}

void usage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "TemplateTCPServer — CardioSimulator monitor server (JSON over TCP).\n"
        "Receives the rhythm catalog + per-selection rhythm data, answering the\n"
        "OK/no_data cache handshake so unchanged rhythms are never re-sent.\n"
        "\n"
        "Options:\n"
        "  --host HOST   bind address          (default 0.0.0.0)\n"
        "  --port PORT   listen port           (default 9000)\n"
        "  --upload-dir DIR    directory to save uploads  (default uploads)\n"
        "  --max-upload-mb N   reject uploads larger than N MB (default 100)\n"
        "  --max-line-mb N     reject JSON lines larger than N MB (default 64)\n"
        "  --process-delay-ms N  simulated processing time per rhythm before ack\n"
        "                        (default 1000; 0 disables; keep under 4000)\n"
        "  --quiet             reduce logging\n"
        "  -h, --help          show this help\n",
        prog);
}

}  // namespace

int main(int argc, char** argv) {
    tts::server::Options opts;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Missing argument for %s\n", name);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") { usage(argv[0]); return 0; }
        else if (a == "--host")          opts.host           = need("--host");
        else if (a == "--port")          opts.port           = static_cast<uint16_t>(std::atoi(need("--port")));
        else if (a == "--quiet")         opts.quiet          = true;
        else if (a == "--upload-dir")    opts.uploadDir      = need("--upload-dir");
        else if (a == "--max-upload-mb") opts.maxUploadBytes = static_cast<long long>(std::atoi(need("--max-upload-mb"))) * 1024 * 1024;
        else if (a == "--max-line-mb")   opts.maxLineBytes   = static_cast<long long>(std::atoi(need("--max-line-mb"))) * 1024 * 1024;
        else if (a == "--process-delay-ms") opts.processDelayMs = static_cast<long long>(std::atoi(need("--process-delay-ms")));
        else {
            std::fprintf(stderr, "Unknown option: %s\n", a.c_str());
            usage(argv[0]);
            return 2;
        }
    }

    // -----------------------------------------------------------------------
    // Rhythm cache — backs the CardioSimulator cache handshake (§4, §6).
    //
    // The server owns the mandatory OK/no_data reply; this store just remembers
    // which (pathology, hash) rhythms it already holds so an unchanged rhythm is
    // never re-sent, even across reconnects. One instance is shared by every
    // client session for the life of the process.
    // -----------------------------------------------------------------------
    tts::RhythmCache cache;
    opts.cache = &cache;

    // -----------------------------------------------------------------------
    // Optional observation hook.
    //
    // After the server has sent any mandatory reply, it invokes a handler keyed
    // by the message type ("time", "query", "rhythm", "start", "stop") with the
    // decoded fields as its payload. Handlers here are for logging/metrics/etc.
    // — they must NOT send replies themselves (the server already sent the
    // mandatory verdict/ack, and an extra reply would desync the app's FIFO
    // matching, §4).
    //
    // Add your own handlers below with registerHandler(); remove the dispatcher
    // wiring entirely if you don't need it.
    // -----------------------------------------------------------------------
    tts::CommandDispatcher dispatcher;

    dispatcher.registerHandler("time",
        [](const tts::json::Value& payload, tts::IClientContext& ctx) {
            const auto* dt = payload.find("datetime");
            std::string when = (dt && dt->isString()) ? dt->toString() : "<none>";
            std::cout << "[hook] time — client clock: " << when
                      << " | peer: " << ctx.peerAddress() << "\n";
        });

    dispatcher.registerHandler("query",
        [](const tts::json::Value& payload, tts::IClientContext& ctx) {
            const auto* pathology = payload.find("pathology");
            const auto* cached    = payload.find("cached");
            std::string id = (pathology && pathology->isString()) ? pathology->toString() : "<none>";
            bool isCached  = cached && cached->isBool() && cached->toBool();
            std::cout << "[hook] query — rhythm: " << id
                      << " | verdict: " << (isCached ? "OK" : "no_data")
                      << " | peer: " << ctx.peerAddress() << "\n";
        });

    dispatcher.registerHandler("start",
        [&cache](const tts::json::Value& payload, tts::IClientContext& ctx) {
            const auto* pathology = payload.find("pathology");
            std::string id = (pathology && pathology->isString()) ? pathology->toString() : "<none>";
            std::cout << "[hook] start (play) — rhythm: " << id
                      << " | peer: " << ctx.peerAddress() << "\n";

            auto stored = cache.get(id);
            if (stored) {
                std::string json = "{\"type\":\"start\",\"pathology\":\"" + id +
                                   "\",\"sampleRate\":" + std::to_string(stored->sampleRate) +
                                   ",\"leads\":{";
                bool firstLead = true;
                for (const auto& kv : stored->leads) {
                    if (!firstLead) json += ",";
                    firstLead = false;
                    json += "\"" + kv.first + "\":[";
                    for (size_t i = 0; i < kv.second.size(); ++i) {
                        if (i > 0) json += ",";
                        json += std::to_string(kv.second[i]);
                    }
                    json += "]";
                }
                json += "}}\n";
                broadcastToEmulator(json);
                std::cout << "[emulator-ipc] broadcast rhythm '" << id << "' to emulator\n";
            }
        });

    dispatcher.registerHandler("stop",
        [](const tts::json::Value&, tts::IClientContext& ctx) {
            std::cout << "[hook] stop — monitor stopped | peer: " << ctx.peerAddress() << "\n";
            broadcastToEmulator("{\"type\":\"stop\"}\n");
        });

    opts.dispatcher = &dispatcher;

    tts::server::Server server(opts);
    g_server = &server;
    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);

    return server.run();
}
