// The runtime tests: Player (the game loop) and FrameHost (the stream bridge).
//
// These are the first tests that start a whole engine rather than one
// subsystem, so they cover the seams between the pieces as well as the
// behaviour of each: simulation advancing, rendering producing pixels, input
// reaching the game, commands taking effect, and the HTTP/WebSocket surface
// actually carrying a real PNG.

#include "TestHarness.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "app/FrameHost.h"
#include "app/Player.h"
#include "base/Json.h"
#include "math/Math.h"
#include "net/Base64.h"
#include "net/HttpServer.h"
#include "net/Sha1.h"
#include "net/WebSocket.h"
#include "render/Renderer.h"

// --- tiny socket client, shared with the net suite's approach ---------------

namespace {

using nova::app::Player;

/// Blocks until [fn] returns true or the deadline passes.
template <typename Fn>
bool waitFor(Fn fn, double seconds = 2.0, double step = 0.005) {
    const double deadline = nova::nowSeconds() + seconds;
    while (nova::nowSeconds() < deadline) {
        if (fn()) return true;
        std::this_thread::sleep_for(std::chrono::duration<double>(step));
    }
    return fn();
}

/// A raw socket with a handful of the reads the tests need.
class Client {
public:
    ~Client() { close(); }

    bool connectTo(uint16_t port) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            close();
            return false;
        }
        return true;
    }

    void send(const std::string& data) {
        size_t sent = 0;
        while (sent < data.size()) {
            const ssize_t n = ::send(fd_, data.data() + sent, data.size() - sent, 0);
            if (n <= 0) return;
            sent += static_cast<size_t>(n);
        }
    }

    /// Reads until [marker] appears, the peer closes, or the timeout expires.
    std::string readUntil(const std::string& marker, int timeoutMs = 2000) {
        std::string out;
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            char buf[16384];
            const ssize_t n = ::recv(fd_, buf, sizeof(buf), MSG_DONTWAIT);
            if (n > 0) {
                out.append(buf, static_cast<size_t>(n));
                if (!marker.empty() && out.find(marker) != std::string::npos) return out;
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return out;
    }

    /// Everything still buffered, for reading a length-delimited body.
    std::string readExactly(size_t bytes, int timeoutMs = 3000) {
        std::string out;
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (out.size() < bytes && std::chrono::steady_clock::now() < deadline) {
            char buf[65536];
            const size_t want = std::min(sizeof(buf), bytes - out.size());
            const ssize_t n = ::recv(fd_, buf, want, MSG_DONTWAIT);
            if (n > 0) {
                out.append(buf, static_cast<size_t>(n));
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return out;
    }

    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    /// The descriptor, or -1 once close() has run.
    int fd() const { return fd_; }

private:
    int fd_ = -1;
};

std::string jsonField(const std::string& document, const std::string& key) {
    std::string error;
    const nova::Json doc = nova::Json::parse(document, &error);
    if (!error.empty()) return "";
    return doc[key].dump(0);
}

double jsonNumber(const std::string& document, const std::string& key) {
    std::string error;
    const nova::Json doc = nova::Json::parse(document, &error);
    if (!error.empty()) return 0.0;
    return doc[key].asDouble(0.0);
}

/// Pulls one HTTP response into its status line, headers and body.
struct Response {
    int status = 0;
    std::map<std::string, std::string> headers;
    std::string body;

    std::string header(const std::string& name) const {
        auto it = headers.find(name);
        return it == headers.end() ? std::string() : it->second;
    }
};

Response httpGet(uint16_t port, const std::string& path) {
    Client c;
    Response r;
    if (!c.connectTo(port)) return r;
    c.send("GET " + path + " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    const std::string raw = c.readUntil("", 3000);
    const size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return r;

    const std::string head = raw.substr(0, headerEnd);
    r.body = raw.substr(headerEnd + 4);
    size_t lineEnd = head.find("\r\n");
    const std::string statusLine = head.substr(0, lineEnd);
    r.status = std::atoi(statusLine.c_str() + 9);
    while (lineEnd != std::string::npos) {
        const size_t next = head.find("\r\n", lineEnd + 2);
        const std::string line = head.substr(lineEnd + 2, next - lineEnd - 2);
        const size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            for (char& c : key) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            while (!value.empty() && value[0] == ' ') value.erase(0, 1);
            r.headers[key] = value;
        }
        lineEnd = next;
    }

    // A response with Content-Length may still be arriving.
    auto cl = r.headers.find("content-length");
    if (cl != r.headers.end() && r.body.size() < std::strtoul(cl->second.c_str(), nullptr, 10)) {
        const size_t want = static_cast<size_t>(std::strtoul(cl->second.c_str(), nullptr, 10));
        r.body += c.readExactly(want - r.body.size());
    }
    return r;
}

Response httpPost(uint16_t port, const std::string& path, const std::string& body) {
    Client c;
    Response r;
    if (!c.connectTo(port)) return r;
    c.send("POST " + path + " HTTP/1.1\r\nHost: localhost\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
    const std::string raw = c.readUntil("", 3000);
    const size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return r;
    r.body = raw.substr(headerEnd + 4);
    r.status = std::atoi(raw.substr(9, 3).c_str());
    return r;
}

/// Drives a full WebSocket handshake and returns the socket, already upgraded.
bool openWebSocket(Client& c, uint16_t port, const std::string& path = "/ws") {
    if (!c.connectTo(port)) return false;
    const std::string nonce = "dGhlIHNhbXBsZSBub25jZQ==";
    c.send("GET " + path +
           " HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
           "Sec-WebSocket-Key: " +
           nonce + "\r\nSec-WebSocket-Version: 13\r\n\r\n");
    const std::string handshake = c.readUntil("\r\n\r\n");
    return handshake.find("101 Switching Protocols") != std::string::npos;
}

/// Reads frames until one is binary, and returns its payload.
std::vector<uint8_t> readBinaryFrame(Client& c, int timeoutMs = 6000) {
    std::string buf;
    std::vector<uint8_t> result;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        char chunk[65536];
        const ssize_t n = ::recv(c.fd(), chunk, sizeof(chunk), MSG_DONTWAIT);
        if (n > 0) {
            buf.append(chunk, static_cast<size_t>(n));
            while (buf.size() >= 2) {
                const uint8_t b0 = static_cast<uint8_t>(buf[0]);
                const uint8_t b1 = static_cast<uint8_t>(buf[1]);
                const bool masked = (b1 & 0x80u) != 0;
                uint64_t length = b1 & 0x7Fu;
                size_t header = 2;
                if (length == 126) {
                    if (buf.size() < 4) break;
                    length = (static_cast<uint8_t>(buf[2]) << 8) | static_cast<uint8_t>(buf[3]);
                    header = 4;
                } else if (length == 127) {
                    if (buf.size() < 10) break;
                    length = 0;
                    for (int i = 0; i < 8; ++i) {
                        length = (length << 8) | static_cast<uint8_t>(buf[2 + i]);
                    }
                    header = 10;
                }
                if (masked) header += 4;
                if (buf.size() < header + length) break;
                const nova::net::Opcode op =
                    static_cast<nova::net::Opcode>(b0 & 0x0Fu);
                if (op == nova::net::Opcode::Binary) {
                    result.assign(buf.begin() + static_cast<long>(header),
                                  buf.begin() + static_cast<long>(header + length));
                    return result;
                }
                buf.erase(0, header + length);
            }
            continue;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return result;
}

/// The pusher's x position, read out of the runtime's own telemetry.
float pusherX(Player& player) {
    std::string error;
    const nova::Json telemetry = nova::Json::parse(player.telemetry(), &error);
    if (!error.empty()) return 0.0f;
    const nova::Json& bodies = telemetry["bodies"];
    for (size_t i = 0; i < bodies.size(); ++i) {
        if (bodies.at(i)["tag"].asString() == "Pusher") {
            return static_cast<float>(bodies.at(i)["x"].asDouble(0.0));
        }
    }
    return 0.0f;
}

Player::Config smallConfig() {
    Player::Config c;
    c.width = 160;
    c.height = 90;
    c.spawnRate = 0;
    return c;
}

}  // namespace

NOVA_TEST(Player, StartsAdvancesAndRenders) {
    Player player;
    std::string error;
    CHECK_MSG(player.start(smallConfig(), &error), error);
    CHECK(player.running());

    // start() renders once so there is a frame before the first update.
    CHECK(player.frame() != nullptr);
    CHECK(player.frame()->valid());
    CHECK(player.frame()->color.width == 160);
    CHECK(player.frame()->color.height == 90);

    for (int i = 0; i < 30; ++i) player.update(1.0f / 60.0f);

    const nova::app::PlayerStats s = player.stats();
    CHECK(s.frame == 30);
    CHECK(s.bodies >= 6);
    // Clock measures wall time, so thirty instantaneous updates take almost
    // none; what has to be right is that the frame count tracks the calls.
    CHECK(s.elapsedSeconds < 5.0);
    CHECK(s.drawCalls > 0);
    // Rendering 160x90 with a handful of sprites is microseconds, not
    // milliseconds; a regression that reallocates the framebuffer shows up here.
    CHECK_MSG(s.renderMs < 50.0, "renderMs=" + std::to_string(s.renderMs));

    player.shutdown();
    CHECK(!player.running());
}

NOVA_TEST(Player, RejectsBadConfigurations) {
    Player player;
    std::string error;

    Player::Config bad;
    bad.width = 4;
    CHECK(!player.start(bad, &error));
    CHECK(!error.empty());

    Player::Config noStep;
    noStep.fixedDelta = 0.0f;
    error.clear();
    CHECK(!player.start(noStep, &error));

    CHECK_MSG(player.start(smallConfig(), &error), error);
    error.clear();
    CHECK(!player.start(smallConfig(), &error));
    CHECK(error == "already started");
    player.shutdown();
}

NOVA_TEST(Player, PhysicsActuallyRunsAndStacksSettle) {
    Player player;
    std::string error;
    Player::Config c = smallConfig();
    c.spawnRate = 0;
    CHECK_MSG(player.start(c, &error), error);

    // An awake-body count, measured the same way at two points in time.
    auto awakeCount = [&player]() {
        std::string error;
        const nova::Json telemetry = nova::Json::parse(player.telemetry(), &error);
        CHECK_MSG(error.empty(), error);
        int awake = 0;
        for (size_t i = 0; i < telemetry["bodies"].size(); ++i) {
            if (!telemetry["bodies"].at(i)["sleeping"].asBool(false)) {
                ++awake;
                if (getenv("NOVA_TRACE_AWAKE")) {
                    std::printf("    awake[%d] tag=%s x=%.3f y=%.3f\n", awake,
                                telemetry["bodies"].at(i)["tag"].asString().c_str(),
                                telemetry["bodies"].at(i)["x"].asDouble(0.0),
                                telemetry["bodies"].at(i)["y"].asDouble(0.0));
                }
            }
        }
        return awake;
    };

    for (int i = 0; i < 60; ++i) player.update(1.0f / 60.0f);
    const int awakeWhileFalling = awakeCount();
    for (int i = 0; i < 300; ++i) player.update(1.0f / 60.0f);
    const int awakeAfterSettling = awakeCount();

    std::string error2;
    const nova::Json telemetry = nova::Json::parse(player.telemetry(), &error2);
    CHECK(error2.empty());
    const nova::Json& bodies = telemetry["bodies"];
    CHECK(bodies.isArray());
    CHECK(bodies.size() >= 6);

    float lowest = 1e9f;
    for (size_t i = 0; i < bodies.size(); ++i) {
        lowest = std::min(lowest, static_cast<float>(bodies.at(i)["y"].asDouble(0.0)));
    }
    // The floor's top surface is at y = 0; nothing may end up below it.
    CHECK_MSG(lowest > -0.6, "lowest=" + std::to_string(lowest));
    // Six seconds is long enough for the stack to stop moving entirely.
    CHECK_MSG(awakeWhileFalling > 0, "nothing was ever awake: " + std::to_string(awakeWhileFalling));
    CHECK_MSG(awakeAfterSettling == 0, "still awake: " + std::to_string(awakeAfterSettling));

    player.shutdown();
}

NOVA_TEST(Player, CapturesRealPngFrames) {
    Player player;
    std::string error;
    CHECK_MSG(player.start(smallConfig(), &error), error);
    player.update(1.0f / 60.0f);

    std::vector<uint8_t> png;
    CHECK(player.capturePng(png));
    CHECK(png.size() > 100);
    CHECK(png.size() > 8);
    CHECK(png[0] == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');

    // The encoder's own decoder has to agree with it.
    nova::render::Image decoded;
    CHECK(nova::render::decodePng(png.data(), png.size(), decoded));
    CHECK(decoded.width == 160);
    CHECK(decoded.height == 90);
    // The frame is not a flat colour: something was actually drawn.
    bool varied = false;
    for (int y = 0; y < decoded.height && !varied; ++y) {
        for (int x = 0; x < decoded.width; ++x) {
            if (decoded.get(x, y) != decoded.get(0, 0)) {
                varied = true;
                break;
            }
        }
    }
    CHECK(varied);

    player.shutdown();
    CHECK(!player.capturePng(png));
}

NOVA_TEST(Player, CommandsDriveTheRunningGame) {
    Player player;
    std::string error;
    CHECK_MSG(player.start(smallConfig(), &error), error);
    const int before = player.stats().bodies;

    CHECK(player.applyCommand("spawn", &error));
    CHECK(player.stats().bodies == before + 1);

    CHECK(player.applyCommand("grid off", &error));
    CHECK(!player.config().showGrid);
    CHECK(player.applyCommand("grid on", &error));
    CHECK(player.config().showGrid);

    CHECK(player.applyCommand("pause", &error));
    CHECK(player.config().paused);
    player.update(1.0f / 60.0f);
    CHECK(player.stats().bodies == before + 1);
    CHECK(player.applyCommand("resume", &error));

    CHECK(player.applyCommand("width 200", &error));
    CHECK(player.frame()->color.width == 200);
    CHECK(player.applyCommand("height 120", &error));
    CHECK(player.frame()->color.height == 120);
    std::vector<uint8_t> resized;
    CHECK(player.capturePng(resized));
    CHECK(resized.size() > 100);

    CHECK(player.applyCommand("reset", &error));
    CHECK(player.stats().bodies == before);

    // Bad input is rejected with a reason rather than silently ignored.
    CHECK(!player.applyCommand("grid maybe", &error));
    CHECK(error.find("usage") != std::string::npos);
    CHECK(!player.applyCommand("width 4", &error));
    CHECK(!player.applyCommand("nonsense", &error));

    player.shutdown();
    CHECK(!player.applyCommand("pause", &error));
}

NOVA_TEST(Player, DescribeAndTelemetryAreValidJson) {
    Player player;
    std::string error;
    CHECK_MSG(player.start(smallConfig(), &error), error);
    player.update(1.0f / 60.0f);

    std::string parseError;
    const nova::Json described = nova::Json::parse(player.describe(), &parseError);
    CHECK_MSG(parseError.empty(), parseError);
    CHECK(described["engine"].asString() == "nova");
    CHECK(described["width"].asInt(0) == 160);
    CHECK(described["height"].asInt(0) == 90);
    CHECK(described["running"].asBool(false));
    CHECK(described["actions"].isArray());
    CHECK(described["actions"].size() == 4);

    const nova::Json telemetry = nova::Json::parse(player.telemetry(), &parseError);
    CHECK_MSG(parseError.empty(), parseError);
    CHECK(telemetry["frame"].asInt(0) == 1);
    CHECK(telemetry["bodies"].isArray());

    player.shutdown();
}

NOVA_TEST(Player, InputReachesTheSimulation) {
    Player player;
    std::string error;
    Player::Config c = smallConfig();
    c.spawnRate = 0;
    CHECK_MSG(player.start(c, &error), error);

    nova::input::InputState held;
    held.keys[static_cast<size_t>(nova::input::Key::Right)] = true;
    player.submitInput(held, 1.0 / 60.0);

    // Hold right briefly and the pusher has to move right. It is held for a
    // fifth of a second on purpose: one second at 7 u/s would carry it into the
    // far wall, and then "did it stop" could not be told apart from "it was
    // already against something".
    for (int i = 0; i < 12; ++i) {
        player.submitInput(held, 1.0 / 60.0);
        player.update(1.0 / 60.0);
    }

    bool found = false;
    {
        std::string parseError;
        const nova::Json telemetry = nova::Json::parse(player.telemetry(), &parseError);
        CHECK_MSG(parseError.empty(), parseError);
        for (size_t i = 0; i < telemetry["bodies"].size(); ++i) {
            if (telemetry["bodies"].at(i)["tag"].asString() == "Pusher") found = true;
        }
    }
    CHECK_MSG(found, "no Pusher body in telemetry");

    const float reachedX = pusherX(player);
    CHECK_MSG(reachedX > 0.2f && reachedX < 2.5f, "reachedX=" + std::to_string(reachedX));

    // Release it. It has to stay where it is instead of sliding for another
    // second, which is what a controller with no braking would do.
    nova::input::InputState released;
    for (int i = 0; i < 60; ++i) {
        player.submitInput(released, 1.0 / 60.0);
        player.update(1.0 / 60.0);
    }
    const float stopX = pusherX(player);
    CHECK_MSG(std::fabs(stopX - reachedX) < 0.5f,
              "drift=" + std::to_string(stopX - reachedX));

    player.shutdown();
}

// --- FrameHost --------------------------------------------------------------

NOVA_TEST(FrameHost, StartsAndServesItsRestSurface) {
    nova::app::FrameHost host;
    nova::app::HostOptions options;
    options.host = "127.0.0.1";
    options.port = 0;
    options.documentRoot = "";
    options.targetFps = 120.0;
    options.streamFps = 30.0;
    options.width = 160;
    options.height = 90;
    options.spawnRate = 0;

    std::string error;
    CHECK_MSG(host.start(options, &error), error);
    CHECK(host.running());
    CHECK(host.port() != 0);

    const uint16_t port = host.port();

    const Response status = httpGet(port, "/api/status");
    CHECK_MSG(status.status == 200, "status=" + std::to_string(status.status));
    CHECK(status.header("content-type").find("application/json") != std::string::npos);
    CHECK(jsonField(status.body, "engine") == "\"nova\"");
    CHECK(jsonNumber(status.body, "width") == 160.0);

    const Response telemetry = httpGet(port, "/api/telemetry");
    CHECK(telemetry.status == 200);
    std::string parseError;
    nova::Json::parse(telemetry.body, &parseError);
    CHECK_MSG(parseError.empty(), parseError);

    // Before anything streams, the frame endpoint has nothing to say yet.
    const Response early = httpGet(port, "/api/frame.png");
    CHECK(early.status == 503);

    // Commands go over HTTP too, which is what a script or CI job would use.
    const Response paused = httpPost(port, "/api/command", "pause");
    CHECK(paused.status == 200);
    CHECK(jsonField(paused.body, "ok") == "true");
    CHECK(host.player().config().paused);

    const Response bad = httpPost(port, "/api/command", "not-a-command");
    CHECK(bad.status == 400);
    CHECK(jsonField(bad.body, "ok") == "false");
    CHECK(jsonField(bad.body, "error").find("unknown command") != std::string::npos);

    const Response missing = httpGet(port, "/api/nothing-here");
    CHECK(missing.status == 404);

    host.stop();
    CHECK(!host.running());
}

NOVA_TEST(FrameHost, StreamsRealFramesOverWebSocket) {
    nova::app::FrameHost host;
    nova::app::HostOptions options;
    options.host = "127.0.0.1";
    options.port = 0;
    options.documentRoot = "";
    options.targetFps = 120.0;
    options.streamFps = 60.0;
    options.width = 160;
    options.height = 90;
    options.spawnRate = 4;

    std::string error;
    CHECK_MSG(host.start(options, &error), error);

    Client client;
    CHECK(openWebSocket(client, host.port()));
    CHECK(waitFor([&] { return host.server().websocketCount() == 1; }));

    const std::vector<uint8_t> png = readBinaryFrame(client);
    CHECK_MSG(!png.empty(), "no frame arrived");
    CHECK(png.size() > 8);
    CHECK(png[0] == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');

    nova::render::Image decoded;
    CHECK(nova::render::decodePng(png.data(), png.size(), decoded));
    CHECK(decoded.width == 160);
    CHECK(decoded.height == 90);

    CHECK(waitFor([&] { return host.stats().framesSent > 0; }));
    const auto stats = host.stats();
    CHECK(stats.framesRendered > 0);
    CHECK(stats.clients == 1);
    CHECK(stats.bytesSent > 0);

    // And the same bytes are available over plain HTTP for a still image.
    const Response still = httpGet(host.port(), "/api/frame.png");
    CHECK(still.status == 200);
    CHECK(still.header("content-type") == "image/png");
    CHECK(still.body.size() > 100);

    client.close();
    host.stop();
}

NOVA_TEST(FrameHost, ClientInputReachesTheGame) {
    nova::app::FrameHost host;
    nova::app::HostOptions options;
    options.host = "127.0.0.1";
    options.port = 0;
    options.documentRoot = "";
    options.targetFps = 120.0;
    options.width = 160;
    options.height = 90;
    options.spawnRate = 0;

    std::string error;
    CHECK_MSG(host.start(options, &error), error);

    Client client;
    CHECK(openWebSocket(client, host.port()));
    CHECK(waitFor([&] { return host.server().websocketCount() == 1; }));

    // Send "hold right" as JSON and give the loop time to act on it.
    const std::string keyDown =
        std::string("{\"type\":\"key\",\"key\":\"Right\",\"down\":true}");
    nova::net::Frame f;
    f.opcode = nova::net::Opcode::Text;
    f.payload.assign(keyDown.begin(), keyDown.end());
    const std::vector<uint8_t> wire = nova::net::encodeFrame(f, true);
    client.send(std::string(wire.begin(), wire.end()));

    const float startX = pusherX(host.player());
    CHECK(waitFor([&] { return pusherX(host.player()) > startX + 0.3f; }, 3.0));

    // A bare string is a command, which is how the console talks to the game.
    const std::string pause = "pause";
    nova::net::Frame pf;
    pf.opcode = nova::net::Opcode::Text;
    pf.payload.assign(pause.begin(), pause.end());
    const std::vector<uint8_t> pwire = nova::net::encodeFrame(pf, true);
    client.send(std::string(pwire.begin(), pwire.end()));
    CHECK(waitFor([&] { return host.player().config().paused; }));
    CHECK(host.stats().commands > 0);

    client.close();
    host.stop();
}

NOVA_TEST(FrameHost, GarbageFromClientsDoesNotBreakTheStream) {
    nova::app::FrameHost host;
    nova::app::HostOptions options;
    options.host = "127.0.0.1";
    options.port = 0;
    options.documentRoot = "";
    options.targetFps = 120.0;
    options.streamFps = 60.0;
    options.width = 96;
    options.height = 64;

    std::string error;
    CHECK_MSG(host.start(options, &error), error);

    Client client;
    CHECK(openWebSocket(client, host.port()));
    CHECK(waitFor([&] { return host.server().websocketCount() == 1; }));

    // Unparseable JSON, an unknown message type, an unknown key, and a command
    // that does not exist. None of them may close the socket or stop the game.
    const char* junk[] = {
        "{not json at all",
        "{\"type\":\"something-else\"}",
        "{\"type\":\"key\",\"key\":\"NotAKey\",\"down\":true}",
        "nonsense-command",
    };
    for (const char* text : junk) {
        nova::net::Frame f;
        f.opcode = nova::net::Opcode::Text;
        f.payload.assign(text, text + std::strlen(text));
        const std::vector<uint8_t> wire = nova::net::encodeFrame(f, true);
        client.send(std::string(wire.begin(), wire.end()));
    }

    CHECK(waitFor([&] { return host.stats().framesSent >= 2; }, 4.0));
    CHECK(host.server().websocketCount() == 1);
    CHECK(host.running());

    // The stream is still healthy afterwards.
    const std::vector<uint8_t> png = readBinaryFrame(client);
    CHECK(!png.empty());
    CHECK(png[0] == 0x89);

    client.close();
    host.stop();
}

NOVA_TEST(FrameHost, StopsCleanlyWithClientsAttached) {
    nova::app::FrameHost host;
    nova::app::HostOptions options;
    options.host = "127.0.0.1";
    options.port = 0;
    options.documentRoot = "";
    options.targetFps = 60.0;
    options.streamFps = 30.0;
    options.width = 96;
    options.height = 64;

    std::string error;
    CHECK_MSG(host.start(options, &error), error);

    Client a;
    Client b;
    CHECK(openWebSocket(a, host.port()));
    CHECK(openWebSocket(b, host.port()));
    CHECK(waitFor([&] { return host.server().websocketCount() == 2; }));
    CHECK(waitFor([&] { return host.stats().framesSent > 0; }, 3.0));

    // Both stop() calls have to return even though two threads are mid-broadcast
    // and two clients are connected.
    host.stop();
    host.stop();
    CHECK(!host.running());
    a.close();
    b.close();
}