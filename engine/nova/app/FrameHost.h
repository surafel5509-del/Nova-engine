// FrameHost — the bridge between a running game and whatever is watching it.
//
// The runtime renders into memory. A player needs that memory to become
// something a browser can display, quickly enough to look like motion. FrameHost
// does that: it owns a Player, runs it on its own thread at a fixed target
// rate, encodes each finished frame to PNG, and pushes it to every connected
// WebSocket client as one binary message. It also carries keyboard and pointer
// events the other way.
//
// Everything here is best-effort by design. A slow client must not stall the
// simulation, so a frame that cannot be delivered is dropped rather than
// queued, and the drop is counted.

#ifndef NOVA_APP_FRAMEHOST_H
#define NOVA_APP_FRAMEHOST_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/Player.h"
#include "input/Input.h"
#include "net/HttpServer.h"

namespace nova {
namespace app {

struct HostOptions {
    /// Address the HTTP/WebSocket server binds to.
    std::string host = "0.0.0.0";
    uint16_t port = 8090;
    /// Directory served for GET /. The editor UI lives there.
    std::string documentRoot = "web";
    /// Simulation target rate. Frames are rendered at this rate, not faster.
    double targetFps = 30.0;
    int width = 960;
    int height = 540;
    /// Encode and broadcast a frame at most this often. 0 = every frame.
    double streamFps = 15.0;
    /// Stop the loop after this many rendered frames. 0 = run until stopped.
    uint64_t frameBudget = 0;
    /// Bodies dropped onto the stack per second by the demo scene.
    int spawnRate = 2;
    /// Stop when no client has ever connected. Used by the smoke tests.
    bool exitWhenIdle = false;
};

struct HostStats {
    uint64_t framesRendered = 0;
    /// How many of those actually rasterized a frame. The loop skips the render
    /// on frames nobody will stream, so the two numbers are not the same.
    uint64_t framesDrawn = 0;
    uint64_t framesSent = 0;
    uint64_t framesDropped = 0;
    uint64_t bytesSent = 0;
    uint64_t clients = 0;
    uint64_t commands = 0;
    double lastEncodeMs = 0.0;
};

class FrameHost {
public:
    FrameHost();
    ~FrameHost();

    FrameHost(const FrameHost&) = delete;
    FrameHost& operator=(const FrameHost&) = delete;

    /// Starts the game and the server. Returns false with a reason in [error].
    bool start(const HostOptions& options, std::string* error = nullptr);
    void stop();
    bool running() const { return running_.load(); }

    /// The live game. Exposed so a caller can inspect or command it directly.
    Player& player() { return *player_; }
    const Player& player() const { return *player_; }
    net::HttpServer& server() { return server_; }
    const net::HttpServer& server() const { return server_; }
    uint16_t port() const { return port_; }

    HostStats stats() const;

  public:
    /// The most recently published view of the running game, for a reader that
    /// must not block the simulation. Empty before the first frame.
    std::string publishedStatus() const;
    std::string publishedTelemetry() const;
    std::string publishedActions() const;
    std::string publishedScene() const;
    /// The engine frame the published snapshot came from.
    uint64_t snapshotFrame() const;

  private:

    /// Runs the loop on the calling thread until the frame budget is spent or
    /// stop() is called from elsewhere. start() does this on its own thread;
    /// this is for tests and for the standalone player.
    void runLoop();

    /// The most recently broadcast frame, for GET /api/frame.png. Returns an
    /// empty vector if nothing has been encoded yet.
    std::vector<uint8_t> lastFrame() const;

private:
    void loop();
    void publishSnapshot();
    /// The scene graph as JSON, which is what an editor hierarchy panel needs.
    std::string buildSceneJson() const;
    /// The action map with each action's live state.
    std::string buildActionsJson() const;
    void broadcastFrame(const std::vector<uint8_t>& png);
    /// Claims the pending-frame request: true once, for the reader that asked.
    /// GET /api/frame.png sets the flag so the loop renders a frame specifically
    /// for that caller even when nothing is streaming. Claiming it (rather than
    /// reading it) matters -- a flag nobody clears pins the rasterizer on
    /// forever, and an idle host that never idles is a host that never gets
    /// back the CPU it is not using.
    bool pendingPng() { return frameRequested_.exchange(false, std::memory_order_relaxed); }
    /// Handles one text message from a client: a command or an input event.
    void handleClientMessage(const std::string& text);

    std::unique_ptr<Player> player_;
    net::HttpServer server_;
    HostOptions options_;
    uint16_t port_ = 0;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};

    mutable std::mutex frameMutex_;
    std::vector<uint8_t> lastFrame_;
    std::atomic<bool> frameRequested_{false};

    /// State published by the loop thread for readers to take.
    ///
    /// The player's own mutex is held for a whole frame -- simulation, render,
    /// the lot -- and a frame can be tens of milliseconds at a large
    /// framebuffer. An editor panel that locks it would not just wait for one
    /// frame, it would starve: the loop re-acquires the mutex the instant it
    /// releases it, so the reader can lose that race dozens of times in a row.
    /// The loop therefore copies what readers need into this slot, and they
    /// read a snapshot that is at most one frame old.
    struct Snapshot {
        std::string status;
        std::string telemetry;
        std::string actions;
        std::string scene;
        PlayerStats stats{};
        uint64_t frame = 0;
    };
    mutable std::mutex snapshotMutex_;
    Snapshot snapshot_;

    mutable std::mutex inputMutex_;
    /// The live remote input state. Key and pointer events mutate it in place
    /// and it is submitted every tick, because a browser sends one "key down"
    /// and then nothing — clearing it per tick would make a held key last a
    /// single frame.
    InputState remoteInput_;

    std::atomic<uint64_t> framesRendered_{0};
    std::atomic<uint64_t> framesDrawn_{0};
    std::atomic<uint64_t> framesSent_{0};
    std::atomic<uint64_t> framesDropped_{0};
    std::atomic<uint64_t> bytesSent_{0};
    std::atomic<uint64_t> maxClients_{0};
    std::atomic<uint64_t> commands_{0};
    std::atomic<double> lastEncodeMs_{0.0};
};

}  // namespace app
}  // namespace nova

#endif  // NOVA_APP_FRAMEHOST_H