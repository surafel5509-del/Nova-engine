#include "net/HttpServer.h"

#include <arpa/inet.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <utility>
#include <cstring>
#include <sstream>

#include "base/FileSystem.h"
#include "base/Json.h"
#include "net/Sha1.h"

namespace nova {
namespace net {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

const char* reasonPhrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 413: return "Payload Too Large";
        case 426: return "Upgrade Required";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        default: return "OK";
    }
}

/// Does [pattern] (which may end in '*') match [path]?
bool patternMatches(const std::string& pattern, const std::string& path) {
    if (pattern.empty()) return false;
    if (pattern.back() == '*') {
        const std::string prefix = pattern.substr(0, pattern.size() - 1);
        return path.compare(0, prefix.size(), prefix) == 0;
    }
    return pattern == path;
}

/// How many characters of [pattern] and [path] agree. Used to pick the most
/// specific matching route.
size_t commonPrefix(const std::string& a, const std::string& b) {
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    return i;
}

void setNoDelay(int fd) {
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

}  // namespace

// --- request helpers --------------------------------------------------------

std::string urlDecode(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() &&
            std::isxdigit(static_cast<unsigned char>(text[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            const std::string hex = text.substr(i + 1, 2);
            out += static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            i += 2;
        } else if (text[i] == '+') {
            out += ' ';
        } else {
            out += text[i];
        }
    }
    return out;
}

std::string HttpRequest::header(const std::string& name) const {
    // HTTP header names are case-insensitive, so the lookup key is folded the
    // same way the stored keys were.
    auto it = headers.find(lower(name));
    return it == headers.end() ? std::string() : it->second;
}

std::map<std::string, std::string> HttpRequest::params() const {
    std::map<std::string, std::string> out;
    size_t start = 0;
    while (start <= query.size()) {
        const size_t amp = query.find('&', start);
        const std::string pair =
            query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        if (!pair.empty()) {
            const size_t eq = pair.find('=');
            if (eq == std::string::npos) {
                out[urlDecode(pair)] = "";
            } else {
                out[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
            }
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return out;
}

bool wantsKeepAlive(const HttpRequest& request) {
    auto it = request.headers.find("connection");
    if (it != request.headers.end()) {
        if (lower(it->second).find("close") != std::string::npos) return false;
        if (lower(it->second).find("keep-alive") != std::string::npos) return true;
    }
    return request.version == "HTTP/1.1";
}

// --- responses --------------------------------------------------------------

HttpResponse HttpResponse::text(std::string body, int status) {
    HttpResponse r;
    r.status = status;
    r.body = std::move(body);
    r.contentType = "text/plain; charset=utf-8";
    return r;
}

HttpResponse HttpResponse::json(const std::string& body, int status) {
    HttpResponse r;
    r.status = status;
    r.body = body;
    r.contentType = "application/json; charset=utf-8";
    return r;
}

HttpResponse HttpResponse::bytes(std::vector<uint8_t> body, const std::string& contentType,
                                 int status) {
    HttpResponse r;
    r.status = status;
    r.body.assign(body.begin(), body.end());
    r.contentType = contentType;
    return r;
}

HttpResponse HttpResponse::notFound(const std::string& what) {
    HttpResponse r = text("404 " + what + "\n", 404);
    return r;
}

std::string HttpResponse::serialise(bool keepAlive) const {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << " "
        << (statusText.empty() ? reasonPhrase(status) : statusText) << "\r\n";
    for (const auto& kv : headers) {
        out << kv.first << ": " << kv.second << "\r\n";
    }
    if (!contentType.empty()) out << "Content-Type: " << contentType << "\r\n";
    out << "Content-Length: " << body.size() << "\r\n";
    out << "Connection: " << (keepAlive ? "keep-alive" : "close") << "\r\n";
    out << "\r\n" << body;
    return out.str();
}

// --- parsing ----------------------------------------------------------------

bool parseHttpRequest(const std::string& raw, HttpRequest& out, std::string* error) {
    out = HttpRequest{};
    const size_t headEnd = raw.find("\r\n\r\n");
    if (headEnd == std::string::npos) {
        if (error) *error = "no header terminator";
        return false;
    }
    const std::string head = raw.substr(0, headEnd);
    out.body = raw.substr(headEnd + 4);

    std::istringstream lines(head);
    std::string requestLine;
    if (!std::getline(lines, requestLine)) {
        if (error) *error = "empty request";
        return false;
    }
    if (!requestLine.empty() && requestLine.back() == '\r') requestLine.pop_back();

    std::istringstream rl(requestLine);
    if (!(rl >> out.method >> out.target >> out.version)) {
        if (error) *error = "malformed request line";
        return false;
    }
    if (out.method.empty() || out.target.empty()) {
        if (error) *error = "missing method or target";
        return false;
    }
    // Three whitespace-separated tokens are not enough: the version has to look
    // like a version, or random bytes parse as a request with a nonsense
    // method and get a 405 instead of the 400 they deserve.
    if (out.version.compare(0, 5, "HTTP/") != 0 || out.version.size() < 8 ||
        out.version[6] != '.') {
        if (error) *error = "unsupported HTTP version";
        return false;
    }
    const size_t q = out.target.find('?');
    if (q == std::string::npos) {
        out.path = out.target;
    } else {
        out.path = out.target.substr(0, q);
        out.query = out.target.substr(q + 1);
    }

    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;  // ignore malformed headers
        out.headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
    }
    return true;
}

// --- WebSocketConnection ----------------------------------------------------

WebSocketConnection::WebSocketConnection(int fd, std::string remote)
    : fd_(fd), remote_(std::move(remote)) {
    static std::atomic<uint64_t> counter{0};
    id_ = ++counter;
    open_ = true;
    if (fd_ >= 0) setNoDelay(fd_);
}

WebSocketConnection::~WebSocketConnection() {
    if (fd_ >= 0) ::close(fd_);
}

bool WebSocketConnection::writeAll(const uint8_t* data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        const ssize_t n = ::send(fd_, data + sent, size - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        open_ = false;
        return false;
    }
    return true;
}

bool WebSocketConnection::flush() {
    if (!open_ || pending_.empty()) return open_;
    std::vector<uint8_t> batch;
    batch.swap(pending_);
    if (!writeAll(batch.data(), batch.size())) return false;
    return true;
}

bool WebSocketConnection::sendText(const std::string& text) {
    if (!open_) return false;
    Frame f;
    f.opcode = Opcode::Text;
    f.payload.assign(text.begin(), text.end());
    const std::vector<uint8_t> bytes = encodeFrame(f, false);
    pending_.insert(pending_.end(), bytes.begin(), bytes.end());
    return flush();
}

bool WebSocketConnection::sendBinary(const uint8_t* data, size_t size) {
    if (!open_) return false;
    Frame f;
    f.opcode = Opcode::Binary;
    f.payload.assign(data, data + size);
    const std::vector<uint8_t> bytes = encodeFrame(f, false);
    pending_.insert(pending_.end(), bytes.begin(), bytes.end());
    return flush();
}

bool WebSocketConnection::sendBinary(const std::vector<uint8_t>& data) {
    return sendBinary(data.data(), data.size());
}

bool WebSocketConnection::ping(const std::string& payload) {
    if (!open_) return false;
    Frame f;
    f.opcode = Opcode::Ping;
    f.payload.assign(payload.begin(), payload.end());
    if (f.payload.size() > 125) f.payload.resize(125);
    const std::vector<uint8_t> bytes = encodeFrame(f, false);
    pending_.insert(pending_.end(), bytes.begin(), bytes.end());
    return flush();
}

void WebSocketConnection::close(uint16_t code, const std::string& reason) {
    if (!open_) return;
    const std::vector<uint8_t> bytes = encodeClose(code, reason);
    writeAll(bytes.data(), bytes.size());
    open_ = false;
}

bool WebSocketConnection::readSome() {
    uint8_t buffer[4096];
    const ssize_t n = ::recv(fd_, buffer, sizeof(buffer), 0);
    if (n == 0) return false;  // peer closed
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return true;
        return false;
    }
    if (!assembler_.feed(buffer, static_cast<size_t>(n))) return false;
    return true;
}

bool WebSocketConnection::pump() {
    if (!open_) return false;
    if (!readSome()) {
        open_ = false;
        return false;
    }

    Frame control;
    while (assembler_.takeControl(control)) {
        if (control.opcode == Opcode::Ping) {
            Frame pong;
            pong.opcode = Opcode::Pong;
            pong.payload = control.payload;
            const std::vector<uint8_t> bytes = encodeFrame(pong, false);
            pending_.insert(pending_.end(), bytes.begin(), bytes.end());
        } else if (control.opcode == Opcode::Close) {
            uint16_t code = 1000;
            if (control.payload.size() >= 2) {
                code = static_cast<uint16_t>((control.payload[0] << 8) | control.payload[1]);
            }
            close(code, "peer closed");
            return false;
        }
    }
    if (!flush()) return false;

    MessageAssembler::Message message;
    while (assembler_.next(message)) {
        if (message.opcode == Opcode::Text && onMessage) {
            if (!onMessage(std::string(message.payload.begin(), message.payload.end()))) {
                return false;
            }
        }
    }
    return open_;
}

// --- HttpServer -------------------------------------------------------------

HttpServer::HttpServer() = default;

HttpServer::~HttpServer() { stop(); }

void HttpServer::route(const std::string& method, const std::string& pattern, HttpHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Route& r : routes_) {
        if (r.method == method && r.pattern == pattern) {
            r.handler = std::move(handler);
            return;
        }
    }
    routes_.push_back(Route{method, pattern, std::move(handler)});
}

void HttpServer::websocket(const std::string& pattern, WebSocketHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (WsRoute& r : wsRoutes_) {
        if (r.pattern == pattern) {
            r.handler = std::move(handler);
            return;
        }
    }
    wsRoutes_.push_back(WsRoute{pattern, std::move(handler)});
}

bool HttpServer::start(const ServerOptions& options, std::string* error) {
    if (running_) {
        if (error) *error = "already running";
        return false;
    }
    options_ = options;
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        if (error) *error = std::string("socket: ") + std::strerror(errno);
        return false;
    }
    int one = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(options.port);
    if (options.host.empty() || options.host == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else if (::inet_pton(AF_INET, options.host.c_str(), &addr.sin_addr) != 1) {
        if (error) *error = "bad host";
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (error) *error = std::string("bind: ") + std::strerror(errno);
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }
    if (::listen(listenFd_, 16) != 0) {
        if (error) *error = std::string("listen: ") + std::strerror(errno);
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    socklen_t len = sizeof(addr);
    if (::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
        boundPort_ = ntohs(addr.sin_port);
    } else {
        boundPort_ = options.port;
    }

    ::signal(SIGPIPE, SIG_IGN);
    running_ = true;
    acceptThread_ = std::thread([this] { acceptLoop(); });
    return true;
}

void HttpServer::stop() {
    if (!running_.exchange(false)) {
        if (listenFd_ >= 0) {
            ::close(listenFd_);
            listenFd_ = -1;
        }
        return;
    }
    // Closing the listening socket makes the blocking accept() fail, which ends
    // the loop; joining then joins a thread that is already on its way out.
    if (listenFd_ >= 0) {
        ::shutdown(listenFd_, SHUT_RDWR);
        ::close(listenFd_);
        listenFd_ = -1;
    }
    if (acceptThread_.joinable()) acceptThread_.join();

    std::map<uint64_t, std::shared_ptr<WebSocketConnection>> sockets;
    std::vector<int> fds;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sockets.swap(sockets_);
        fds.swap(liveFds_);
    }
    for (auto& kv : sockets) {
        kv.second->close(1001, "server shutting down");
        if (kv.second->onClose) kv.second->onClose();
    }
    // Wake anything blocked in recv() so its thread can notice and return.
    for (int fd : fds) ::shutdown(fd, SHUT_RDWR);

    std::vector<std::pair<uint64_t, std::thread>> threads;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        threads.swap(connectionThreads_);
    }
    for (auto& entry : threads) {
        if (entry.second.joinable()) entry.second.join();
    }
}

void HttpServer::acceptLoop() {
    while (running_) {
        sockaddr_in peer{};
        socklen_t len = sizeof(peer);
        const int fd = ::accept(listenFd_, reinterpret_cast<sockaddr*>(&peer), &len);
        if (fd < 0) {
            if (!running_) break;
            if (errno == EINTR) continue;
            break;
        }
        char text[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &peer.sin_addr, text, sizeof(text));
        const std::string remote = std::string(text) + ":" + std::to_string(ntohs(peer.sin_port));
        ++accepted_;

        int live = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& kv : sockets_) {
                if (kv.second->open()) ++live;
            }
        }
        if (options_.maxConnections > 0 && live >= options_.maxConnections) {
            ++rejected_;
            ::close(fd);
            continue;
        }
        // One thread per connection. A browser tab makes a handful and this
        // server never sees hundreds, so the simplicity is worth more than the
        // threading model.
        const uint64_t seq = nextThreadId_++;
        std::thread thread([this, fd, remote, seq] {
            serve(fd, remote);
            std::lock_guard<std::mutex> lock(mutex_);
            for (size_t i = 0; i < liveFds_.size(); ++i) {
                if (liveFds_[i] == fd) {
                    liveFds_.erase(liveFds_.begin() + static_cast<long>(i));
                    break;
                }
            }
            finishedThreads_.insert(seq);
        });
        {
            std::lock_guard<std::mutex> lock(mutex_);
            liveFds_.push_back(fd);
            connectionThreads_.emplace_back(seq, std::move(thread));
        }
        // Reap finished connection threads so the vector does not grow without
        // bound on a long-running server. A thread that has already published
        // its sequence number joins immediately.
        std::vector<std::pair<uint64_t, std::thread>> done;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (size_t i = 0; i < connectionThreads_.size();) {
                if (finishedThreads_.count(connectionThreads_[i].first)) {
                    done.push_back(std::move(connectionThreads_[i]));
                    connectionThreads_.erase(connectionThreads_.begin() +
                                             static_cast<long>(i));
                } else {
                    ++i;
                }
            }
        }
        for (auto& entry : done) {
            if (entry.second.joinable()) entry.second.join();
        }
    }
}

void HttpServer::dropConnection(int fd) {
    if (fd >= 0) ::close(fd);
}

void HttpServer::serve(int fd, const std::string& remote) {
    setNoDelay(fd);
    std::string buffer;
    char chunk[8192];

    while (running_) {
        const size_t headEnd = buffer.find("\r\n\r\n");
        if (headEnd == std::string::npos) {
            if (buffer.size() > options_.maxHeaderBytes) {
                ++rejected_;
                const HttpResponse r = HttpResponse::text("431 request header fields too large", 400);
                const std::string raw = r.serialise(false);
                ::send(fd, raw.data(), raw.size(), MSG_NOSIGNAL);
                dropConnection(fd);
                return;
            }
            const ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
            if (n <= 0) {
                dropConnection(fd);
                return;
            }
            buffer.append(chunk, static_cast<size_t>(n));
            continue;
        }

        HttpRequest request;
        std::string error;
        if (!parseHttpRequest(buffer, request, &error)) {
            ++rejected_;
            const HttpResponse r = HttpResponse::text("400 " + error, 400);
            const std::string raw = r.serialise(false);
            ::send(fd, raw.data(), raw.size(), MSG_NOSIGNAL);
            dropConnection(fd);
            return;
        }
        request.remoteAddress = remote;

        const size_t contentLength =
            static_cast<size_t>(std::strtoul(request.header("content-length").c_str(), nullptr, 10));
        if (contentLength > options_.maxBodyBytes) {
            ++rejected_;
            const HttpResponse r = HttpResponse::text("413 payload too large", 413);
            const std::string raw = r.serialise(false);
            ::send(fd, raw.data(), raw.size(), MSG_NOSIGNAL);
            dropConnection(fd);
            return;
        }
        if (buffer.size() < headEnd + 4 + contentLength) {
            const ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
            if (n <= 0) {
                dropConnection(fd);
                return;
            }
            buffer.append(chunk, static_cast<size_t>(n));
            continue;
        }
        request.body = buffer.substr(headEnd + 4, contentLength);
        buffer.erase(0, headEnd + 4 + contentLength);

        // The upgrade check has to happen before anything writes a response.
        // When there is no WebSocket route for the path, or the handshake is
        // incomplete, the request falls through to the ordinary handler and
        // gets a real status code instead of a silently dropped connection.
        const std::string upgrade = lower(request.header("upgrade"));
        if (upgrade.find("websocket") != std::string::npos) {
            if (handleWebSocket(fd, remote, request)) return;  // socket is owned now
        }

        ++requests_;
        const HttpResponse response = dispatch(request);
        const bool keepAlive = wantsKeepAlive(request);
        const std::string raw = response.serialise(keepAlive);
        bytesSent_ += raw.size();
        size_t sent = 0;
        while (sent < raw.size()) {
            const ssize_t n = ::send(fd, raw.data() + sent, raw.size() - sent, MSG_NOSIGNAL);
            if (n > 0) {
                sent += static_cast<size_t>(n);
                continue;
            }
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            dropConnection(fd);
            return;
        }
        if (!keepAlive) {
            dropConnection(fd);
            return;
        }
    }
    dropConnection(fd);
}

bool HttpServer::handleWebSocket(int fd, const std::string& remote, const HttpRequest& request) {
    WebSocketHandler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t best = 0;
        bool found = false;
        for (const WsRoute& r : wsRoutes_) {
            if (!patternMatches(r.pattern, request.path)) continue;
            const size_t score = commonPrefix(r.pattern, request.path);
            if (!found || score >= best) {
                best = score;
                handler = r.handler;
                found = true;
            }
        }
    }
    if (!handler) return false;

    const std::string key = request.header("sec-websocket-key");
    if (key.empty()) return false;

    std::ostringstream hs;
    hs << "HTTP/1.1 101 Switching Protocols\r\n"
       << "Upgrade: websocket\r\n"
       << "Connection: Upgrade\r\n"
       << "Sec-WebSocket-Accept: " << websocketAccept(key) << "\r\n\r\n";
    const std::string handshake = hs.str();
    size_t sent = 0;
    while (sent < handshake.size()) {
        const ssize_t n =
            ::send(fd, handshake.data() + sent, handshake.size() - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        return false;
    }

    auto socket = std::make_shared<WebSocketConnection>(fd, remote);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sockets_[socket->id()] = socket;
    }
    ++wsOpened_;

    handler(socket, request);

    while (running_ && socket->open()) {
        if (!socket->pump()) break;
        // A short sleep keeps an idle tab from spinning the CPU at 100%.
        struct timespec ts {
            0, 5 * 1000 * 1000
        };
        ::nanosleep(&ts, nullptr);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        sockets_.erase(socket->id());
    }
    ++wsClosed_;
    if (socket->onClose) socket->onClose();
    // A handler that captures its own socket would otherwise keep the
    // connection alive forever; see detachHandlers().
    socket->detachHandlers();
    return true;
}

HttpResponse HttpServer::serveStatic(const HttpRequest& request) {
    if (options_.documentRoot.empty()) return HttpResponse::notFound(request.path);

    std::string rel = urlDecode(request.path);
    if (rel.empty() || rel[0] != '/') rel = "/" + rel;
    if (rel == "/") rel = "/index.html";
    // Join treats a leading slash as "absolute", which would throw the document
    // root away entirely.
    if (!rel.empty() && rel[0] == '/') rel.erase(0, 1);

    const std::string root = options_.documentRoot;
    const std::string requested = defaultFs().join(root, rel);
    // Refuse anything that could climb out of the document root. Checking the
    // resolved path matters more than rejecting "..": a symlink can point
    // outside just as easily.
    std::vector<char> resolvedBuf(4096), targetBuf(4096);
    const char* base = ::realpath(root.c_str(), resolvedBuf.data())
                           ? resolvedBuf.data()
                           : root.c_str();
    if (::realpath(requested.c_str(), targetBuf.data()) == nullptr) {
        return HttpResponse::notFound(request.path);
    }
    const std::string target = targetBuf.data();
    std::string baseStr = std::string(base);
    if (baseStr.empty() || baseStr.back() != '/') baseStr += '/';
    if (target.compare(0, baseStr.size(), baseStr) != 0) {
        return HttpResponse::text("403 forbidden", 403);
    }
    const std::string full = target;

    std::vector<uint8_t> data;
    if (!defaultFs().readBinary(full, data) || data.empty()) {
        return HttpResponse::notFound(request.path);
    }
    const std::string ext = defaultFs().extensionOf(full);
    std::string type = "application/octet-stream";
    if (ext == "html" || ext == "htm") type = "text/html; charset=utf-8";
    else if (ext == "js" || ext == "mjs") type = "text/javascript; charset=utf-8";
    else if (ext == "css") type = "text/css; charset=utf-8";
    else if (ext == "txt" || ext == "log" || ext == "md") type = "text/plain; charset=utf-8";
    else if (ext == "wasm") type = "application/wasm";
    else if (ext == "glsl" || ext == "vert" || ext == "frag") type = "text/plain; charset=utf-8";
    else if (ext == "json") type = "application/json; charset=utf-8";
    else if (ext == "png") type = "image/png";
    else if (ext == "svg") type = "image/svg+xml";
    else if (ext == "ico") type = "image/x-icon";
    return HttpResponse::bytes(std::move(data), type);
}

HttpResponse HttpServer::dispatch(const HttpRequest& request) {
    HttpHandler handler;
    HttpHandler otherMethod;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t best = 0;
        bool found = false;
        for (const Route& r : routes_) {
            if (!patternMatches(r.pattern, request.path)) continue;
            const size_t score = commonPrefix(r.pattern, request.path);
            if (r.method != request.method) {
                if (!otherMethod) otherMethod = r.handler;
                continue;
            }
            if (!found || score >= best) {
                best = score;
                handler = r.handler;
                found = true;
            }
        }
    }
    if (!handler) {
        if (otherMethod) return HttpResponse::text("405 method not allowed", 405);
        if (fallback) {
            HttpResponse r = fallback(request);
            if (r.status != 404) return r;
        }
        if (request.method != "GET" && request.method != "HEAD") {
            return HttpResponse::text("405 method not allowed", 405);
        }
        return serveStatic(request);
    }
    HttpResponse response = handler(request);
    if (request.method == "HEAD") response.body.clear();
    return response;
}

ServerStats HttpServer::stats() const {
    ServerStats s;
    s.requests = requests_;
    s.websocketsOpened = wsOpened_;
    s.websocketsClosed = wsClosed_;
    s.bytesSent = bytesSent_;
    s.connectionsAccepted = accepted_;
    s.requestsRejected = rejected_;
    return s;
}

size_t HttpServer::broadcastBinary(const uint8_t* data, size_t size) {
    std::map<uint64_t, std::shared_ptr<WebSocketConnection>> sockets;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sockets = sockets_;
    }
    size_t sent = 0;
    for (auto& kv : sockets) {
        // The copy above keeps the connection alive if the pump is closing it
        // on another thread at this very moment.
        if (kv.second->sendBinary(data, size)) ++sent;
    }
    return sent;
}

size_t HttpServer::broadcastText(const std::string& text) {
    std::map<uint64_t, std::shared_ptr<WebSocketConnection>> sockets;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sockets = sockets_;
    }
    size_t sent = 0;
    for (auto& kv : sockets) {
        if (kv.second->sendText(text)) ++sent;
    }
    return sent;
}

size_t HttpServer::websocketCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t n = 0;
    for (const auto& kv : sockets_) {
        if (kv.second->open()) ++n;
    }
    return n;
}

std::string HttpServer::describe() const {
    Json doc = Json::object();
    doc.set("running", running_.load());
    doc.set("port", static_cast<int>(boundPort_));
    doc.set("host", options_.host);
    const ServerStats s = stats();
    doc.set("requests", static_cast<int>(s.requests));
    doc.set("connections", static_cast<int>(s.connectionsAccepted));
    doc.set("websockets", static_cast<int>(websocketCount()));
    doc.set("bytesSent", static_cast<int>(s.bytesSent));
    return doc.dump(0);
}

}  // namespace net
}  // namespace nova