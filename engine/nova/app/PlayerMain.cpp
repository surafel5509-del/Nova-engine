// nova_player — the standalone Nova runtime.
//
// Two ways to run:
//   * headless: simulate, render and write PNG frames to a directory, then
//     report what happened. This is the mode a build agent or a render farm
//     uses, and the one the smoke test drives.
//   * serve: bind HTTP + WebSocket and stream live frames to a browser, with
//     keyboard and pointer input coming back the other way.
//
// There is no windowing system on this host, so the frame path is the honest
// one: the software rasterizer produces a real image and the net layer ships
// it. On a platform with a GL/Vulkan backend the same Player object would feed
// a GPU instead and nothing above this line would change.

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "app/FrameHost.h"
#include "app/Player.h"
#include "base/FileSystem.h"
#include "base/Log.h"
#include "base/Time.h"

using nova::app::FrameHost;
using nova::app::HostOptions;
using nova::app::PlayerStats;
using nova::app::Player;
using nova::app::RunResult;

namespace {

/// Set from a signal handler, so it has to be a plain file-scope object: a
/// handler may not capture, and std::atomic is not guaranteed lock-free enough
/// to touch from one.
volatile std::sig_atomic_t gSignalled = 0;

extern "C" void onSignal(int) { gSignalled = 1; }

void printUsage() {
    std::printf(
        "nova_player — the Nova engine runtime\n"
        "\n"
        "Usage: nova_player [options]\n"
        "\n"
        "  --serve                 stream live frames over HTTP + WebSocket\n"
        "  --frames N              stop after N simulated frames (headless)\n"
        "  --width N --height N    framebuffer size (default 960x540)\n"
        "  --out DIR               write PNG frames here (headless, optional)\n"
        "  --every N               write one PNG every N frames (default 1)\n"
        "  --fps F                 simulation target rate (default 60)\n"
        "  --stream-fps F          frame broadcast rate when serving (default 15)\n"
        "  --port P                listen port when serving (default 8090)\n"
        "  --host ADDR             bind address when serving (default 0.0.0.0)\n"
        "  --web DIR               document root for the browser UI (default web)\n"
        "  --spawn-rate N          crates dropped per second (default 2)\n"
        "  --quiet                 only print the summary line\n"
        "  --help                  this text\n"
        "\n"
        "Examples:\n"
        "  nova_player --frames 120 --out /tmp/frames\n"
        "  nova_player --serve --port 8090 --web web\n");
}

bool parseInt(const char* text, long& out) {
    char* end = nullptr;
    const long v = std::strtol(text, &end, 10);
    if (end == text || *end != '\0') return false;
    out = v;
    return true;
}

bool parseDouble(const char* text, double& out) {
    char* end = nullptr;
    const double v = std::strtod(text, &end);
    if (end == text || *end != '\0') return false;
    out = v;
    return true;
}

/// Runs the game without a network and writes PNG frames as it goes.
int runHeadless(Player::Config config, long frames, const std::string& outDir, long every,
                bool quiet) {
    Player player;
    std::string error;
    if (!player.start(config, &error)) {
        std::fprintf(stderr, "nova_player: %s\n", error.c_str());
        return 1;
    }

    if (!outDir.empty() && !nova::defaultFs().createDirectories(outDir)) {
        std::fprintf(stderr, "nova_player: cannot create output directory %s\n", outDir.c_str());
        player.shutdown();
        return 1;
    }

    const double target = 1.0 / static_cast<double>(config.fixedDelta);
    int written = 0;
    for (long i = 0; i < frames && player.running(); ++i) {
        if (player.update(config.fixedDelta) == RunResult::Finished) break;

        if (!outDir.empty() && every > 0 && (i % every) == 0) {
            std::vector<uint8_t> png;
            if (player.capturePng(png) && !png.empty()) {
                char name[64];
                std::snprintf(name, sizeof(name), "frame_%05ld.png", i);
                if (!writePng(nova::defaultFs().join(outDir, name), player.frame()->color)) {
                    std::fprintf(stderr, "nova_player: failed to write %s\n", name);
                    player.shutdown();
                    return 1;
                }
                ++written;
            }
        }
        // Headless still paces itself, otherwise a 600-frame run reports a
        // meaningless frame time.
        if (!quiet) nova::sleepSeconds(1.0 / target);
    }

    const PlayerStats s = player.stats();
    std::printf(
        "frames=%llu elapsed=%.2fs bodies=%d contacts=%d draws=%d "
        "frameMs=%.3f simulateMs=%.3f renderMs=%.3f encodeMs=%.3f pngs=%d\n",
        static_cast<unsigned long long>(s.frame), s.elapsedSeconds, s.bodies, s.contacts,
        s.drawCalls, s.frameMs, s.simulateMs, s.renderMs, s.encodeMs, written);
    player.shutdown();
    return 0;
}

/// Runs the game behind HTTP + WebSocket and streams frames to a browser.
int runServe(HostOptions options, bool quiet) {
    FrameHost host;
    std::string error;
    if (!host.start(options, &error)) {
        std::fprintf(stderr, "nova_player: %s\n", error.c_str());
        return 1;
    }

    std::printf("nova_player: streaming on http://%s:%u/\n", options.host.c_str(),
                static_cast<unsigned>(host.port()));
    std::printf("nova_player: %dx%d at %.0f fps, broadcasting at %.0f fps\n", options.width,
                options.height, options.targetFps, options.streamFps);
    std::fflush(stdout);

    // Ctrl-C is the normal way out, and a headless container sends SIGTERM.
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    double lastReport = nova::nowSeconds();
    while (host.running() && gSignalled == 0) {
        nova::sleepSeconds(0.25);
        if (quiet) continue;
        const double t = nova::nowSeconds();
        if (t - lastReport < 5.0) continue;
        lastReport = t;
        const auto s = host.stats();
        const auto p = host.player().stats();
        std::printf(
            "frame %llu  %.1f fps  sim %.2fms  render %.2fms  encode %.2fms  "
            "bodies %d  clients %llu  sent %llu  dropped %llu\n",
            static_cast<unsigned long long>(p.frame), p.fps, p.simulateMs, p.renderMs,
            p.encodeMs, p.bodies, static_cast<unsigned long long>(s.clients),
            static_cast<unsigned long long>(s.framesSent),
            static_cast<unsigned long long>(s.framesDropped));
        std::fflush(stdout);
    }

    const auto s = host.stats();
    std::printf("rendered=%llu sent=%llu dropped=%llu bytes=%llu\n",
                static_cast<unsigned long long>(s.framesRendered),
                static_cast<unsigned long long>(s.framesSent),
                static_cast<unsigned long long>(s.framesDropped),
                static_cast<unsigned long long>(s.bytesSent));
    host.stop();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    nova::Log::instance().setLevel(nova::LogLevel::Warn);

    Player::Config config;
    HostOptions host;
    bool serve = false;
    bool quiet = false;
    long frames = 60;
    long every = 1;
    std::string outDir;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "nova_player: %s needs a value\n", what);
                std::exit(2);
            }
            return argv[++i];
        };
        long v = 0;
        double d = 0.0;

        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else if (arg == "--serve") {
            serve = true;
        } else if (arg == "--quiet") {
            quiet = true;
        } else if (arg == "--width") {
            if (!parseInt(next("--width"), v)) {
                std::fprintf(stderr, "nova_player: --width wants a number\n");
                return 2;
            }
            config.width = static_cast<int>(v);
            host.width = config.width;
        } else if (arg == "--height") {
            if (!parseInt(next("--height"), v)) {
                std::fprintf(stderr, "nova_player: --height wants a number\n");
                return 2;
            }
            config.height = static_cast<int>(v);
            host.height = config.height;
        } else if (arg == "--frames") {
            if (!parseInt(next("--frames"), v)) {
                std::fprintf(stderr, "nova_player: --frames wants a number\n");
                return 2;
            }
            frames = v;
            host.frameBudget = static_cast<uint64_t>(v < 0 ? 0 : v);
        } else if (arg == "--every") {
            if (!parseInt(next("--every"), v)) {
                std::fprintf(stderr, "nova_player: --every wants a number\n");
                return 2;
            }
            every = v;
        } else if (arg == "--out") {
            outDir = next("--out");
        } else if (arg == "--fps") {
            if (!parseDouble(next("--fps"), d)) {
                std::fprintf(stderr, "nova_player: --fps wants a number\n");
                return 2;
            }
            host.targetFps = d;
        } else if (arg == "--stream-fps") {
            if (!parseDouble(next("--stream-fps"), d)) {
                std::fprintf(stderr, "nova_player: --stream-fps wants a number\n");
                return 2;
            }
            host.streamFps = d;
        } else if (arg == "--port") {
            if (!parseInt(next("--port"), v)) {
                std::fprintf(stderr, "nova_player: --port wants a number\n");
                return 2;
            }
            host.port = static_cast<uint16_t>(v);
        } else if (arg == "--host") {
            host.host = next("--host");
        } else if (arg == "--web") {
            host.documentRoot = next("--web");
        } else if (arg == "--spawn-rate") {
            if (!parseInt(next("--spawn-rate"), v)) {
                std::fprintf(stderr, "nova_player: --spawn-rate wants a number\n");
                return 2;
            }
            config.spawnRate = static_cast<int>(v);
        } else {
            std::fprintf(stderr, "nova_player: unknown option %s\n", arg.c_str());
            printUsage();
            return 2;
        }
    }

    if (frames <= 0 && !serve) {
        std::fprintf(stderr, "nova_player: --frames must be at least 1\n");
        return 2;
    }

    return serve ? runServe(host, quiet) : runHeadless(config, frames, outDir, every, quiet);
}