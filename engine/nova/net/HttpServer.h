// A small HTTP/1.1 server with WebSocket upgrade.
//
// Enough to host the editor: static files, JSON routes, a few REST endpoints,
// and a WebSocket per connected client. Written directly on sockets so the
// engine has no third-party runtime dependency, and threaded one-connection-per
// -thread because the load is a handful of browser tabs.
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "net/WebSocket.h"

namespace nova {
namespace net {

struct HttpRequest {
    std::string method;
    std::string target;  ///< the raw request target, query string included
    std::string path;    ///< [target] up to the '?'
    std::string query;   ///< everything after the '?'
    std::string version;
    std::map<std::string, std::string> headers;  ///< keys lowercased
    std::string body;
    std::string remoteAddress;

    /// Case-insensitive, as HTTP requires.
    std::string header(const std::string& name) const;
    /// Percent-decoded query parameters, first value wins.
    std::map<std::string, std::string> params() const;
};

struct HttpResponse {
    int status = 200;
    std::string statusText;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string contentType = "text/plain; charset=utf-8";

    static HttpResponse text(std::string body, int status = 200);
    static HttpResponse json(const std::string& body, int status = 200);
    static HttpResponse bytes(std::vector<uint8_t> body, const std::string& contentType,
                              int status = 200);
    static HttpResponse notFound(const std::string& what = "not found");

    /// Serialised with Content-Length and the connection close decided already.
    std::string serialise(bool keepAlive) const;
};

/// A live WebSocket connection. Methods are safe to call from the server's
/// accept loop; [onMessage] is invoked on that loop's thread.
class WebSocketConnection {
public:
    explicit WebSocketConnection(int fd, std::string remote);
    ~WebSocketConnection();

    WebSocketConnection(const WebSocketConnection&) = delete;
    WebSocketConnection& operator=(const WebSocketConnection&) = delete;

    uint64_t id() const { return id_; }
    const std::string& remote() const { return remote_; }
    bool open() const { return open_; }

    bool sendText(const std::string& text);
    bool sendBinary(const uint8_t* data, size_t size);
    bool sendBinary(const std::vector<uint8_t>& data);
    bool ping(const std::string& payload = "");
    /// Sends a close frame and marks the connection for teardown.
    void close(uint16_t code = 1000, const std::string& reason = "");

    /// Set by the server. Returns false to close the connection.
    std::function<bool(const std::string& text)> onMessage;
    std::function<void()> onClose;

    /// Reads whatever is available, dispatches messages, and returns false once
    /// the peer has gone or the protocol broke.
    bool pump();
    /// Writes any queued frames. Called after every send so a slow peer cannot
    /// stall the loop indefinitely. Returns false once the peer is gone.
    bool flush();

    /// Drops the user callbacks.
    ///
    /// The obvious way to write an echo handler is
    ///     socket->onMessage = [socket](const std::string& t) { ... };
    /// which makes the connection own a shared_ptr to itself and leak the
    /// whole cycle once the socket closes. The server calls this once the
    /// connection is finished so that pattern stays safe.
    void detachHandlers() {
        onMessage = nullptr;
        onClose = nullptr;
    }

private:
    bool writeAll(const uint8_t* data, size_t size);
    bool readSome();

    int fd_ = -1;
    uint64_t id_ = 0;
    std::string remote_;
    std::atomic<bool> open_{false};
    MessageAssembler assembler_;
    std::vector<uint8_t> pending_;
};

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;
using WebSocketHandler =
    std::function<void(std::shared_ptr<WebSocketConnection>, const HttpRequest&)>;

struct ServerOptions {
    /// 0 asks the OS for an ephemeral port, which is what tests want.
    uint16_t port = 8080;
    std::string host = "0.0.0.0";
    /// Directory served for GET when no route matches. Empty disables it.
    std::string documentRoot;
    /// Refuses request bodies larger than this.
    size_t maxBodyBytes = 8u * 1024u * 1024u;
    /// Refuses request headers larger than this.
    size_t maxHeaderBytes = 64u * 1024u;
    /// Bound on simultaneous connections.
    int maxConnections = 64;
};

struct ServerStats {
    uint64_t requests = 0;
    uint64_t websocketsOpened = 0;
    uint64_t websocketsClosed = 0;
    uint64_t bytesSent = 0;
    uint64_t connectionsAccepted = 0;
    uint64_t requestsRejected = 0;
};

class HttpServer {
public:
    HttpServer();
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    /// Binds and starts accepting. Returns false with [error] filled in.
    bool start(const ServerOptions& options, std::string* error = nullptr);
    void stop();
    bool running() const { return running_; }
    /// The port actually bound, which matters when [options].port was 0.
    uint16_t port() const { return boundPort_; }

    /// Registers a handler. The most specific match wins: an exact
    /// "/api/scene" beats a "/api/" prefix.
    void route(const std::string& method, const std::string& pattern, HttpHandler handler);
    void get(const std::string& pattern, HttpHandler handler) { route("GET", pattern, std::move(handler)); }
    void post(const std::string& pattern, HttpHandler handler) { route("POST", pattern, std::move(handler)); }
    /// Handles the WebSocket upgrade for [pattern].
    void websocket(const std::string& pattern, WebSocketHandler handler);

    /// The handler every other route falls through to. Replaceable, so a host
    /// can serve its own UI without subclassing.
    HttpHandler fallback = nullptr;

    ServerStats stats() const;
    /// A JSON document describing the running server, for the editor's status bar.
    std::string describe() const;

    size_t websocketCount() const;

    /// Sends one message to every open connection. Returns how many accepted
    /// it; a socket that fails is closed, never queued behind. This is the
    /// publish path for anything that streams to many viewers at once.
    size_t broadcastBinary(const uint8_t* data, size_t size);
    size_t broadcastText(const std::string& text);

private:
    struct Route {
        std::string method;
        std::string pattern;
        HttpHandler handler;
    };
    struct WsRoute {
        std::string pattern;
        WebSocketHandler handler;
    };

    void acceptLoop();
    void serve(int fd, const std::string& remote);
    HttpResponse dispatch(const HttpRequest& request);
    bool handleWebSocket(int fd, const std::string& remote, const HttpRequest& request);
    HttpResponse serveStatic(const HttpRequest& request);
    void dropConnection(int fd);

    ServerOptions options_;
    int listenFd_ = -1;
    uint16_t boundPort_ = 0;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> nextConnectionId_{1};
    std::atomic<uint64_t> nextThreadId_{1};

    std::thread acceptThread_;
    mutable std::mutex mutex_;
    std::vector<Route> routes_;
    std::vector<WsRoute> wsRoutes_;
    std::map<uint64_t, std::shared_ptr<WebSocketConnection>> sockets_;
    /// One thread per live connection. They are joined in stop() rather than
    /// detached, so a connection thread can never outlive the server. Each is
    /// paired with the sequence number its thread adds to finishedThreads_ on
    /// the way out, which lets the accept loop reap completed ones.
    std::vector<std::pair<uint64_t, std::thread>> connectionThreads_;
    std::set<uint64_t> finishedThreads_;
    /// Descriptors of every live connection, so stop() can wake a thread that
    /// is blocked in recv() instead of waiting for the peer to hang up.
    std::vector<int> liveFds_;

    std::atomic<uint64_t> requests_{0};
    std::atomic<uint64_t> wsOpened_{0};
    std::atomic<uint64_t> wsClosed_{0};
    std::atomic<uint64_t> bytesSent_{0};
    std::atomic<uint64_t> accepted_{0};
    std::atomic<uint64_t> rejected_{0};
};

/// Parses a raw request head plus body. Returns false on a malformed request;
/// [error] then explains why. Exposed so the parser can be tested without a
/// socket.
bool parseHttpRequest(const std::string& raw, HttpRequest& out, std::string* error);

/// Percent-decoding, for query strings and paths.
std::string urlDecode(const std::string& text);

/// True when the request wants the connection kept open.
bool wantsKeepAlive(const HttpRequest& request);

}  // namespace net
}  // namespace nova