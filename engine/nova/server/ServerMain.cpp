// nova_server — the Nova editor host.
//
// The engine runs in this process. The editor itself is a browser application
// served from ./web, talking to the engine over two channels:
//
//   HTTP   a small REST surface for state the UI needs once, plus a PNG of
//          the current frame for the still image and the thumbnail.
//   WS     a live channel: every frame arrives as one binary message carrying
//          a PNG, telemetry arrives as JSON text, and keyboard and pointer
//          events travel back as JSON.
//
// Splitting it that way is what makes it usable over the open internet. A
// frame that is a self-contained PNG survives a reconnect, proxies that mangle
// binary, and a viewer's laptop that cannot hold the stream — and the editor UI
// can show a still and the live picture side by side.

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "app/FrameHost.h"
#include "app/Player.h"
#include "base/FileSystem.h"
#include "base/Json.h"
#include "base/Log.h"
#include "base/Time.h"
#include "net/HttpServer.h"

using nova::app::FrameHost;
using nova::app::HostOptions;
using nova::net::HttpRequest;
using nova::net::HttpResponse;

namespace {

volatile std::sig_atomic_t gSignalled = 0;

extern "C" void onSignal(int) { gSignalled = 1; }

void printUsage() {
    std::printf(
        "nova_server — the Nova editor host\n"
        "\n"
        "Usage: nova_server [options]\n"
        "\n"
        "  --port P          listen port (default 8090)\n"
        "  --host ADDR       bind address (default 0.0.0.0)\n"
        "  --web DIR         document root for the editor UI (default web)\n"
        "  --width N         framebuffer width (default 960)\n"
        "  --height N        framebuffer height (default 540)\n"
        "  --fps F           simulation rate (default 60)\n"
        "  --stream-fps F    frame broadcast rate (default 15)\n"
        "  --spawn-rate N    crates dropped per second (default 2)\n"
        "  --open-check      report whether the document root has an index\n"
        "  --quiet           only print startup and shutdown lines\n"
        "  --help            this text\n");
}

}  // namespace

int main(int argc, char** argv) {
    nova::Log::instance().setLevel(nova::LogLevel::Warn);

    HostOptions options;
    bool quiet = false;
    bool openCheck = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "nova_server: %s needs a value\n", what);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else if (arg == "--port") {
            options.port = static_cast<uint16_t>(std::strtol(next("--port"), nullptr, 10));
        } else if (arg == "--host") {
            options.host = next("--host");
        } else if (arg == "--web") {
            options.documentRoot = next("--web");
        } else if (arg == "--width") {
            options.width = static_cast<int>(std::strtol(next("--width"), nullptr, 10));
        } else if (arg == "--height") {
            options.height = static_cast<int>(std::strtol(next("--height"), nullptr, 10));
        } else if (arg == "--fps") {
            options.targetFps = std::strtod(next("--fps"), nullptr);
        } else if (arg == "--stream-fps") {
            options.streamFps = std::strtod(next("--stream-fps"), nullptr);
        } else if (arg == "--spawn-rate") {
            options.spawnRate =
                static_cast<int>(std::strtol(next("--spawn-rate"), nullptr, 10));
        } else if (arg == "--quiet") {
            quiet = true;
        } else if (arg == "--open-check") {
            openCheck = true;
        } else {
            std::fprintf(stderr, "nova_server: unknown option %s\n", arg.c_str());
            printUsage();
            return 2;
        }
    }

    // join() treats a leading slash as "absolute" and would throw the root away.
    const std::string indexPath = nova::defaultFs().join(options.documentRoot, "index.html");
    const bool hasUi = nova::defaultFs().exists(indexPath);
    if (openCheck) {
        std::printf("documentRoot=%s index=%s\n", options.documentRoot.c_str(),
                    hasUi ? indexPath.c_str() : "MISSING");
        return hasUi ? 0 : 1;
    }
    if (!hasUi && !quiet) {
        std::fprintf(stderr,
                     "nova_server: no editor UI at %s — the engine will still run and\n"
                     "             /api/status, /api/telemetry and /ws all work.\n",
                     indexPath.c_str());
    }

    FrameHost host;
    std::string error;
    if (!host.start(options, &error)) {
        std::fprintf(stderr, "nova_server: %s\n", error.c_str());
        return 1;
    }
    // The editor front end is served by the same process; these routes sit on
    // top of the ones FrameHost registers. Both answer from the snapshot the
    // loop publishes, so a hierarchy refresh cannot stall the simulation.
    nova::net::HttpServer& server = host.server();
    server.get("/api/scene", [&host](const HttpRequest&) {
        return HttpResponse::json(host.publishedScene());
    });
    server.get("/api/actions", [&host](const HttpRequest&) {
        return HttpResponse::json(host.publishedActions());
    });

    const uint16_t port = host.port();
    std::printf("nova_server: engine running at http://%s:%u\n", options.host.c_str(),
                static_cast<unsigned>(port));
    std::printf("nova_server: editor UI   at http://127.0.0.1:%u/ (%s)\n",
                static_cast<unsigned>(port),
                hasUi ? options.documentRoot.c_str() : "not installed");
    std::printf("nova_server: frame stream ws://127.0.0.1:%u/ws\n", static_cast<unsigned>(port));
    std::fflush(stdout);

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    double lastReport = nova::nowSeconds();
    while (host.running() && gSignalled == 0) {
        nova::sleepSeconds(0.25);
        if (quiet) continue;
        const double t = nova::nowSeconds();
        if (t - lastReport < 10.0) continue;
        lastReport = t;
        const auto s = host.stats();
        const auto p = host.player().stats();
        std::printf("[%.0fs] frame %llu  %.1f fps  sim %.2fms render %.2fms  bodies %d  "
                    "clients %llu  dropped %llu\n",
                    p.elapsedSeconds, static_cast<unsigned long long>(p.frame), p.fps,
                    p.simulateMs, p.renderMs, p.bodies,
                    static_cast<unsigned long long>(s.clients),
                    static_cast<unsigned long long>(s.framesDropped));
        std::fflush(stdout);
    }

    const auto s = host.stats();
    std::printf("nova_server: rendered %llu frames, sent %llu, dropped %llu, %llu bytes\n",
                static_cast<unsigned long long>(s.framesRendered),
                static_cast<unsigned long long>(s.framesSent),
                static_cast<unsigned long long>(s.framesDropped),
                static_cast<unsigned long long>(s.bytesSent));
    host.stop();
    return 0;
}