#include "Server.h"

#include "CommandDispatcher.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

tts::server::Server* g_server = nullptr;

void onSignal(int) {
    if (g_server) g_server->stop();
}

void usage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "TemplateTCPServer — JSON-over-TCP template server.\n"
        "Clients send command and upload messages; extend with your own handlers.\n"
        "\n"
        "Options:\n"
        "  --host HOST   bind address          (default 0.0.0.0)\n"
        "  --port PORT   listen port           (default 9000)\n"
        "  --upload-dir DIR    directory to save uploads  (default uploads)\n"
        "  --max-upload-mb N   reject uploads larger than N MB (default 100)\n"
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
        else {
            std::fprintf(stderr, "Unknown option: %s\n", a.c_str());
            usage(argv[0]);
            return 2;
        }
    }

    // -----------------------------------------------------------------------
    // Register incoming command handlers.
    //
    // Clients send:  {"type":"command","name":"<name>","payload":{...}}
    // The handler receives the payload and can reply via ctx.sendJson().
    //
    // Add your own handlers below using registerHandler().
    // -----------------------------------------------------------------------
    tts::CommandDispatcher dispatcher;

    // --- Example handlers (replace or extend with real business logic) ------

    dispatcher.registerHandler("ping",
        [](const tts::json::Value&, tts::IClientContext& ctx) {
            std::cout << "[command] 'ping' is executed from " << ctx.peerAddress() << "\n";
            tts::json::Value::Object resp;
            resp["type"]   = "response";
            resp["name"]   = "ping";
            resp["status"] = "ok";
            ctx.sendJson(tts::json::Value(std::move(resp)));
        });

    dispatcher.registerHandler("startTask",
        [](const tts::json::Value& payload, tts::IClientContext& ctx) {
            const auto* name = payload.find("name");
            std::string taskName = (name && name->isString()) ? name->toString() : "<unnamed>";
            std::cout << "[command] 'startTask' is executed — task: " << taskName
                      << " | peer: " << ctx.peerAddress() << "\n";
        });

    dispatcher.registerHandler("stopTask",
        [](const tts::json::Value& payload, tts::IClientContext& ctx) {
            const auto* name = payload.find("name");
            std::string taskName = (name && name->isString()) ? name->toString() : "<unnamed>";
            std::cout << "[command] 'stopTask' is executed — task: " << taskName
                      << " | peer: " << ctx.peerAddress() << "\n";
        });

    dispatcher.registerHandler("status",
        [](const tts::json::Value&, tts::IClientContext& ctx) {
            std::cout << "[command] 'status' is executed from " << ctx.peerAddress() << "\n";
        });

    // Add your own handlers above. Client sends:
    //   {"type":"command","name":"startTask","payload":{"name":"myJob"}}

    opts.dispatcher = &dispatcher;

    tts::server::Server server(opts);
    g_server = &server;
    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);

    return server.run();
}
