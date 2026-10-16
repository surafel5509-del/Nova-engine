#include "app/FrameHost.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "base/Json.h"
#include "base/Log.h"
#include "base/Time.h"

namespace nova {
namespace app {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

/// The smallest gap the loop leaves between frames, in seconds.
constexpr double kIdleYield = 0.002;

std::string trim(const std::string& s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

}  // namespace

FrameHost::FrameHost() = default;

FrameHost::~FrameHost() { stop(); }

bool FrameHost::start(const HostOptions& options, std::string* error) {
    if (running_.load()) {
        if (error) *error = "already running";
        return false;
    }
    if (!(options.targetFps > 0.0)) {
        if (error) *error = "targetFps must be positive";
        return false;
    }
    options_ = options;
    stopRequested_ = false;
    framesRendered_ = 0;
    framesDrawn_ = 0;
    framesSent_ = 0;
    framesDropped_ = 0;
    bytesSent_ = 0;
    maxClients_ = 0;
    commands_ = 0;
    lastEncodeMs_ = 0.0;

    player_.reset(new Player());
    Player::Config pc;
    pc.width = options.width;
    pc.height = options.height;
    pc.spawnRate = options.spawnRate;
    if (!player_->start(pc, error)) {
        player_.reset();
        return false;
    }

    // --- HTTP surface ------------------------------------------------------
    // Every route below answers from the published snapshot rather than from
    // the live game. See [snapshot_]: blocking the simulation to answer an HTTP
    // request is how a 20 fps frame becomes a hung editor.
    net::ServerOptions so;
    so.host = options.host;
    so.port = options.port;
    so.documentRoot = options.documentRoot;
    so.maxConnections = 128;

    server_.get("/api/status", [this](const net::HttpRequest&) {
        return net::HttpResponse::json(publishedStatus());
    });
    server_.get("/api/telemetry", [this](const net::HttpRequest&) {
        return net::HttpResponse::json(publishedTelemetry());
    });
    server_.get("/api/frame.png", [this](const net::HttpRequest&) {
        // Ask the loop for a fresh frame before reading, so a poll that arrives
        // between two streams still gets something current.
        frameRequested_.store(true, std::memory_order_relaxed);
        const std::vector<uint8_t> png = lastFrame();
        if (png.empty()) return net::HttpResponse::text("no frame yet", 503);
        return net::HttpResponse::bytes(png, "image/png");
    });
    server_.get("/api/host", [this](const net::HttpRequest&) {
        Json root = Json::object();
        const HostStats s = stats();
        root["framesRendered"] = static_cast<double>(s.framesRendered);
        root["framesDrawn"] = static_cast<double>(s.framesDrawn);
        root["framesSent"] = static_cast<double>(s.framesSent);
        root["framesDropped"] = static_cast<double>(s.framesDropped);
        root["bytesSent"] = static_cast<double>(s.bytesSent);
        root["clients"] = static_cast<double>(s.clients);
        root["commands"] = static_cast<double>(s.commands);
        root["lastEncodeMs"] = s.lastEncodeMs;
        root["port"] = static_cast<int>(port_);
        root["documentRoot"] = options_.documentRoot;
        root["snapshotFrame"] = static_cast<double>(snapshotFrame());
        return net::HttpResponse::json(root.dump(2));
    });
    server_.post("/api/command", [this](const net::HttpRequest& request) {
        std::string error;
        if (player_->applyCommand(trim(request.body), &error)) {
            ++commands_;
            Json root = Json::object();
            root["ok"] = true;
            return net::HttpResponse::json(root.dump(2));
        }
        Json root = Json::object();
        root["ok"] = false;
        root["error"] = error;
        return net::HttpResponse::json(root.dump(2), 400);
    });

    // --- the frame + input socket -----------------------------------------
    server_.websocket("/ws", [this](std::shared_ptr<net::WebSocketConnection> socket,
                                    const net::HttpRequest&) {
        socket->onMessage = [this](const std::string& text) {
            handleClientMessage(text);
            return true;
        };
        // Say hello immediately so the browser can tell a live socket from one
        // that is still connecting, without waiting for the first frame.
        Json hello = Json::object();
        hello["type"] = std::string("hello");
        hello["engine"] = std::string("nova");
        const std::string payload = hello.dump(0);
        socket->sendText(payload);
    });

    if (!server_.start(so, error)) {
        player_->shutdown();
        player_.reset();
        return false;
    }
    port_ = server_.port();
    running_ = true;

    thread_ = std::thread([this] { loop(); });
    return true;
}

void FrameHost::stop() {
    if (!running_.exchange(false)) {
        stopRequested_ = true;
        return;
    }
    stopRequested_ = true;
    if (thread_.joinable()) thread_.join();
    server_.stop();
    if (player_) player_->shutdown();
    player_.reset();
}

void FrameHost::runLoop() {
    loop();
}

void FrameHost::loop() {
    const double target = 1.0 / std::max(1.0, options_.targetFps);
    double lastTick = nowSeconds();
    double nextStream = 0.0;
    const double streamInterval =
        options_.streamFps > 0.0 ? 1.0 / options_.streamFps : 0.0;

    while (!stopRequested_.load()) {
        const double t0 = nowSeconds();
        const double dt = t0 - lastTick;
        lastTick = t0;

        // Forward whatever the clients have told us. The state is persistent,
        // not an event queue: a browser sends "key down" once.
        {
            InputState current;
            {
                std::lock_guard<std::mutex> lock(inputMutex_);
                current = remoteInput_;
            }
            player_->submitInput(current, dt);
        }

        // Rendering is the expensive half of a frame and its only consumers are
        // the WebSocket watchers and the /api/frame.png route. Deciding before
        // the update means an unwatched, not-yet-due frame costs the simulation
        // and nothing else -- which is the difference between a host that idles
        // and one that pins a core.
        const bool due = t0 >= nextStream;
        const size_t watchers = due ? server_.websocketCount() : 0;
        const bool draw = watchers > 0 || pendingPng();
        const RunResult result = player_->update(dt > 0.0 ? dt : target, draw);
        if (due) nextStream = t0 + streamInterval;
        framesRendered_ += 1;
        if (draw) framesDrawn_ += 1;
        publishSnapshot();

        // A paused game cannot change its picture, so re-encoding and shipping
        // the same PNG sixteen times a second is pure waste. Telemetry still
        // goes out, so the editor's numbers stay live.
        const bool paused = player_->stats().paused;
        if (watchers > 0) {
            std::vector<uint8_t> png;
            if (paused) {
                server_.broadcastText(player_->telemetry());
            } else if (player_->capturePng(png) && !png.empty()) {
                {
                    std::lock_guard<std::mutex> lock(frameMutex_);
                    lastFrame_ = png;
                }
                lastEncodeMs_ = player_->stats().encodeMs;
                const size_t sent = server_.broadcastBinary(png.data(), png.size());
                if (sent > 0) {
                    framesSent_ += sent;
                    bytesSent_ += png.size() * sent;
                } else {
                    ++framesDropped_;
                }
                // Telemetry rides along as a text frame; the UI uses it for the
                // numbers rather than guessing from frame arrival.
                server_.broadcastText(player_->telemetry());
            }
        }

        if (result == RunResult::Finished) break;
        if (options_.frameBudget > 0 && framesRendered_ >= options_.frameBudget) break;

        // Sleep out the remainder of the target frame time, but always give up
        // the rest of the machine for a moment even when a frame overran:
        // running flat out with no gap starves the threads that answer HTTP.
        const double spent = nowSeconds() - t0;
        sleepSeconds(spent < target ? (target - spent) + kIdleYield : kIdleYield);
    }
    running_ = false;
}

void FrameHost::publishSnapshot() {
    if (!player_) return;
    Snapshot next;
    next.status = player_->describe();
    next.telemetry = player_->telemetry();
    next.actions = buildActionsJson();
    next.scene = buildSceneJson();
    next.stats = player_->stats();
    next.frame = next.stats.frame;
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    snapshot_ = std::move(next);
}

uint64_t FrameHost::snapshotFrame() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return snapshot_.frame;
}

std::string FrameHost::publishedStatus() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    if (!snapshot_.status.empty()) return snapshot_.status;
    return player_ ? player_->describe() : std::string("{}");
}

std::string FrameHost::publishedTelemetry() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    if (!snapshot_.telemetry.empty()) return snapshot_.telemetry;
    return player_ ? player_->telemetry() : std::string("{}");
}

std::string FrameHost::publishedActions() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    if (!snapshot_.actions.empty()) return snapshot_.actions;
    return buildActionsJson();
}

std::string FrameHost::publishedScene() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    if (!snapshot_.scene.empty()) return snapshot_.scene;
    return buildSceneJson();
}

std::string FrameHost::buildSceneJson() const {
    if (!player_) return "{}";
    Json root = Json::object();
    Json nodes = Json::array();
    for (NodeId id : player_->scene().allNodes()) {
        if (!player_->scene().valid(id)) continue;
        Json n = Json::object();
        n["id"] = static_cast<int>(id);
        n["name"] = player_->scene().name(id);
        n["parent"] = player_->scene().parent(id) == kInvalidNode
                          ? -1
                          : static_cast<int>(player_->scene().parent(id));
        n["depth"] = player_->scene().depth(id);
        n["visible"] = player_->scene().visible(id);
        n["active"] = player_->scene().active(id);
        nodes.push(n);
    }
    root["nodes"] = nodes;
    root["count"] = static_cast<int>(nodes.size());
    return root.dump(2);
}

std::string FrameHost::buildActionsJson() const {
    if (!player_) return "[]";
    Json root = Json::array();
    for (const std::string& name : player_->actions().names()) {
        const input::ActionState& a = player_->actions().action(name);
        Json entry = Json::object();
        entry["name"] = name;
        entry["down"] = a.down;
        entry["axis"] = static_cast<double>(a.axis);
        entry["held"] = static_cast<double>(a.heldSeconds);
        root.push(entry);
    }
    return root.dump(2);
}

void FrameHost::broadcastFrame(const std::vector<uint8_t>& png) {
    const size_t sent = server_.broadcastBinary(png.data(), png.size());
    if (sent > 0) {
        framesSent_ += sent;
        bytesSent_ += png.size() * sent;
    } else {
        ++framesDropped_;
    }
}

void FrameHost::handleClientMessage(const std::string& text) {
    const std::string trimmed = trim(text);
    if (trimmed.empty()) return;

    // A bare string is a command; anything that starts with a brace is JSON.
    if (trimmed[0] != '{') {
        std::string error;
        if (!player_->applyCommand(trimmed, &error)) {
            Json reply = Json::object();
            reply["type"] = std::string("error");
            reply["error"] = error;
            server_.broadcastText(reply.dump(0));
        }
        ++commands_;
        return;
    }

    std::string error;
    const Json message = Json::parse(trimmed, &error);
    if (!error.empty()) return;
    const std::string type = lower(message.getString("type", ""));

    if (type == "command") {
        std::string cmdError;
        if (!player_->applyCommand(message.getString("command", ""), &cmdError)) {
            Json reply = Json::object();
            reply["type"] = std::string("error");
            reply["error"] = cmdError;
            server_.broadcastText(reply.dump(0));
        }
        ++commands_;
        return;
    }

    if (type == "input" || type == "key") {
        const std::string name = message.getString("key", "");
        const bool down = message.getBool("down", false);
        input::Key key = input::Key::Unknown;
        if (name.empty() || !input::parseKey(lower(name), key)) return;
        std::lock_guard<std::mutex> lock(inputMutex_);
        remoteInput_.keys[static_cast<size_t>(key)] = down;
        return;
    }

    if (type == "pointer") {
        const int id = static_cast<int>(message["id"].asInt(0));
        const float x = static_cast<float>(message["x"].asDouble(0.0));
        const float y = static_cast<float>(message["y"].asDouble(0.0));
        const bool down = message.getBool("down", false);
        std::lock_guard<std::mutex> lock(inputMutex_);
        if (down) {
            remoteInput_.setPointer(id, Vec2(x, y), true);
        } else {
            remoteInput_.releasePointer(id, Vec2(x, y));
        }
        return;
    }

    if (type == "release-all") {
        std::lock_guard<std::mutex> lock(inputMutex_);
        remoteInput_.endAllPointers();
        for (auto& key : remoteInput_.keys) key = false;
        return;
    }
}

std::vector<uint8_t> FrameHost::lastFrame() const {
    std::lock_guard<std::mutex> lock(frameMutex_);
    return lastFrame_;
}

HostStats FrameHost::stats() const {
    HostStats s;
    s.framesRendered = framesRendered_.load();
    s.framesDrawn = framesDrawn_.load();
    s.framesSent = framesSent_.load();
    s.framesDropped = framesDropped_.load();
    s.bytesSent = bytesSent_.load();
    s.clients = server_.websocketCount();
    s.commands = commands_.load();
    s.lastEncodeMs = lastEncodeMs_.load();
    return s;
}

}  // namespace app
}  // namespace nova