#include "Server.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
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
        "TemplateTCPServer — speaks the CardioSimulator JSON protocol.\n"
        "On a `start` message it streams synthetic ECG `points`; `stop` halts.\n"
        "\n"
        "Options:\n"
        "  --host HOST   bind address          (default 0.0.0.0)\n"
        "  --port PORT   listen port           (default 9000)\n"
        "  --rate HZ     default sample rate   (default 250)\n"
        "  --batch N     samples per frame     (default 25)\n"
        "  --quiet       reduce logging\n"
        "  -h, --help    show this help\n",
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
        else if (a == "--host")  opts.host       = need("--host");
        else if (a == "--port")  opts.port       = static_cast<uint16_t>(std::atoi(need("--port")));
        else if (a == "--rate")  opts.sampleRate = std::atoi(need("--rate"));
        else if (a == "--batch") opts.batchSize  = std::atoi(need("--batch"));
        else if (a == "--quiet") opts.quiet      = true;
        else {
            std::fprintf(stderr, "Unknown option: %s\n", a.c_str());
            usage(argv[0]);
            return 2;
        }
    }

    tts::server::Server server(opts);
    g_server = &server;
    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);

    return server.run();
}
