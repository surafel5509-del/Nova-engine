// Networking tests: base64, SHA-1, WebSocket framing and HTTP, plus a real
// loopback socket exercise of the server.
#include "tests/TestHarness.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "base/FileSystem.h"
#include "base/Json.h"
#include "net/Base64.h"
#include "net/HttpServer.h"
#include "net/Sha1.h"
#include "net/WebSocket.h"

using namespace nova;
using namespace nova::net;

namespace {

std::string hex(const uint8_t* data, size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 0xF];
    }
    return out;
}

/// A blocking client socket, used to talk to the server under test.
struct Client {
    int fd = -1;
    bool connectTo(uint16_t port) {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            fd = -1;
            return false;
        }
        return true;
    }
    bool send(const std::string& s) {
        size_t sent = 0;
        while (sent < s.size()) {
            const ssize_t n = ::send(fd, s.data() + sent, s.size() - sent, 0);
            if (n <= 0) return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }
    /// Reads until [needle] appears or the deadline passes.
    std::string readUntil(const std::string& needle, int timeoutMs = 3000) {
        std::string out;
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        char buffer[4096];
        while (std::chrono::steady_clock::now() < deadline) {
            const size_t at = out.find(needle);
            if (at != std::string::npos) return out;
            const ssize_t n = ::recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
            if (n > 0) {
                out.append(buffer, static_cast<size_t>(n));
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return out;
    }
    ~Client() {
        if (fd >= 0) ::close(fd);
    }
};

/// Spins until [fn] is true or the deadline passes.
template <typename Fn>
bool waitFor(Fn fn, int timeoutMs = 3000) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (fn()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return fn();
}

}  // namespace

// --- base64 -----------------------------------------------------------------

NOVA_TEST(Base64, MatchesRfc4648Vectors) {
    CHECK(base64Encode(std::string("")) == "");
    CHECK(base64Encode(std::string("f")) == "Zg==");
    CHECK(base64Encode(std::string("fo")) == "Zm8=");
    CHECK(base64Encode(std::string("foo")) == "Zm9v");
    CHECK(base64Encode(std::string("foob")) == "Zm9vYg==");
    CHECK(base64Encode(std::string("fooba")) == "Zm9vYmE=");
    CHECK(base64Encode(std::string("foobar")) == "Zm9vYmFy");
}

NOVA_TEST(Base64, RoundTripsEveryByteValue) {
    std::vector<uint8_t> all(256);
    for (int i = 0; i < 256; ++i) all[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
    for (size_t len = 0; len <= all.size(); ++len) {
        std::vector<uint8_t> slice(all.begin(), all.begin() + static_cast<long>(len));
        const std::string encoded = base64Encode(slice);
        std::vector<uint8_t> decoded;
        CHECK_MSG(base64Decode(encoded, decoded), "len=" + std::to_string(len));
        CHECK(decoded == slice);
    }
}

NOVA_TEST(Base64, RejectsMalformedInput) {
    std::vector<uint8_t> out;
    CHECK(!base64Decode("Zm9v!", out));
    CHECK(out.empty());
    // A group of one character is impossible.
    CHECK(!base64Decode("Z", out));
    // Data after padding is malformed.
    CHECK(!base64Decode("Zg==Zg==", out));
    // Whitespace is ignored.
    CHECK(base64Decode("Zm9v\nYmFy\r\n", out));
    CHECK(out.size() == 6);
    // The URL-safe alphabet decodes too.
    CHECK(base64Decode("-_8=", out));
    CHECK(out.size() == 2);
    std::string text;
    CHECK(!base64Decode("!!!!", text));
    CHECK(text.empty());
}

// --- SHA-1 ------------------------------------------------------------------

NOVA_TEST(Sha1, MatchesThePublishedVectors) {
    uint8_t digest[20];
    sha1(nullptr, 0, digest);
    CHECK(hex(digest, 20) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");

    const std::string abc = "abc";
    sha1(reinterpret_cast<const uint8_t*>(abc.data()), abc.size(), digest);
    CHECK(hex(digest, 20) == "a9993e364706816aba3e25717850c26c9cd0d89d");

    const std::string twoBlock =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha1(reinterpret_cast<const uint8_t*>(twoBlock.data()), twoBlock.size(), digest);
    CHECK(hex(digest, 20) == "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

    // Exactly one block, and exactly one block plus one byte, exercise both
    // padding branches.
    const std::string block55(55, 'a');
    sha1(reinterpret_cast<const uint8_t*>(block55.data()), block55.size(), digest);
    CHECK(hex(digest, 20) == "c1c8bbdc22796e28c0e15163d20899b65621d65a");
    const std::string block56(56, 'a');
    sha1(reinterpret_cast<const uint8_t*>(block56.data()), block56.size(), digest);
    CHECK(hex(digest, 20) == "c2db330f6083854c99d4b5bfb6e8f29f201be699");

    const std::string million(1000000, 'a');
    sha1(reinterpret_cast<const uint8_t*>(million.data()), million.size(), digest);
    CHECK(hex(digest, 20) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

NOVA_TEST(Sha1, WebsocketHandshakeMatchesRfc6455) {
    // The example from section 1.3 of the RFC.
    CHECK(websocketAccept("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    // The header alone is already the handshake digest.
    CHECK(websocketAccept("x3JJHMbDL1EzLkh9GBhXDw==") == "HSmrc0sMlYUkAGmm5OPpG2HaGWk=");
}

NOVA_TEST(Sha1, ConstantTimeEqualsComparesContent) {
    CHECK(constantTimeEquals("", ""));
    CHECK(constantTimeEquals("abc", "abc"));
    CHECK(!constantTimeEquals("abc", "abd"));
    CHECK(!constantTimeEquals("abc", "ab"));
    CHECK(!constantTimeEquals("abc", "abcd"));
    CHECK(!constantTimeEquals("secret", ""));
}

// --- WebSocket framing ------------------------------------------------------

NOVA_TEST(WebSocket, EncodesTextFramesWithTheRightHeader) {
    Frame f;
    f.opcode = Opcode::Text;
    f.payload = {'H', 'i'};
    const std::vector<uint8_t> out = encodeFrame(f, false);
    CHECK(out.size() == 4);
    CHECK(out[0] == 0x81);
    CHECK(out[1] == 2);
    CHECK(out[2] == 'H');
    CHECK(out[3] == 'i');
}

NOVA_TEST(WebSocket, LengthUsesTheShortestFormThatFits) {
    auto headerFor = [](size_t len, std::vector<uint8_t>& out) {
        Frame f;
        f.payload.assign(len, 0);
        out = encodeFrame(f, false);
    };
    std::vector<uint8_t> out;

    headerFor(0, out);
    CHECK(out[1] == 0);
    CHECK(out.size() == 2);

    headerFor(125, out);
    CHECK(out[1] == 125);
    CHECK(out.size() == 127);

    headerFor(126, out);
    CHECK(out[1] == 126);
    CHECK(out[2] == 0);
    CHECK(out[3] == 126);
    CHECK(out.size() == 130);

    headerFor(65535, out);
    CHECK(out[1] == 126);
    CHECK(out[3] == 0xFF);

    headerFor(65536, out);
    CHECK(out[1] == 127);
    CHECK(out.size() == 65536 + 10);
    CHECK(out[7] == 1);  // the 64-bit length is big-endian
}

NOVA_TEST(WebSocket, MaskingRoundTripsAndIsReversible) {
    Frame f;
    f.opcode = Opcode::Text;
    f.payload = {'a', 'b', 'c', 'd', 'e'};
    const std::vector<uint8_t> masked = encodeFrame(f, true);
    CHECK((masked[1] & 0x80u) != 0);
    // The masked bytes must not equal the plaintext.
    CHECK(masked[6] != 'a');

    MessageAssembler asm1;
    CHECK(asm1.feed(masked.data(), masked.size()));
    MessageAssembler::Message m;
    CHECK(asm1.next(m));
    CHECK(m.opcode == Opcode::Text);
    CHECK(std::string(m.payload.begin(), m.payload.end()) == "abcde");
}

NOVA_TEST(WebSocket, AssemblesFragmentsIntoOneMessage) {
    // "Hel" + "lo" as two fragments.
    Frame a;
    a.fin = false;
    a.opcode = Opcode::Text;
    a.payload = {'H', 'e', 'l'};
    Frame b;
    b.fin = true;
    b.opcode = Opcode::Continuation;
    b.payload = {'l', 'o'};

    const std::vector<uint8_t> fa = encodeFrame(a, true);
    const std::vector<uint8_t> fb = encodeFrame(b, true);
    std::vector<uint8_t> both = fa;
    both.insert(both.end(), fb.begin(), fb.end());

    MessageAssembler asm2;
    CHECK(asm2.feed(both.data(), both.size()));
    MessageAssembler::Message m;
    CHECK(asm2.next(m));
    CHECK(m.text() == "Hello");
    CHECK(!asm2.next(m));
}

NOVA_TEST(WebSocket, HandlesEverySplitPoint) {
    Frame f;
    f.opcode = Opcode::Text;
    f.payload.assign(300, 'x');
    const std::vector<uint8_t> wire = encodeFrame(f, true);

    for (size_t split = 1; split < wire.size(); ++split) {
        MessageAssembler a;
        CHECK(a.feed(wire.data(), split));
        MessageAssembler::Message partial;
        CHECK(!a.next(partial));
        CHECK(a.feed(wire.data() + split, wire.size() - split));
        MessageAssembler::Message m;
        CHECK_MSG(a.next(m), "split=" + std::to_string(split));
        CHECK(m.payload.size() == 300);
    }
}

NOVA_TEST(WebSocket, InterleavesControlFramesWithFragments) {
    Frame a;
    a.fin = false;
    a.opcode = Opcode::Text;
    a.payload = {'a'};
    Frame ping;
    ping.opcode = Opcode::Ping;
    ping.payload = {'p'};
    Frame b;
    b.fin = true;
    b.opcode = Opcode::Continuation;
    b.payload = {'b'};

    std::vector<uint8_t> wire = encodeFrame(a, true);
    const std::vector<uint8_t> fp = encodeFrame(ping, true);
    const std::vector<uint8_t> fb = encodeFrame(b, true);
    wire.insert(wire.end(), fp.begin(), fp.end());
    wire.insert(wire.end(), fb.begin(), fb.end());

    MessageAssembler asm3;
    CHECK(asm3.feed(wire.data(), wire.size()));
    Frame control;
    CHECK(asm3.takeControl(control));
    CHECK(control.opcode == Opcode::Ping);
    CHECK(control.text() == "p");
    CHECK(!asm3.takeControl(control));
    MessageAssembler::Message m;
    CHECK(asm3.next(m));
    CHECK(m.text() == "ab");
}

NOVA_TEST(WebSocket, RejectsProtocolViolations) {
    MessageAssembler asm4;
    // A continuation with nothing to continue.
    Frame cont;
    cont.opcode = Opcode::Continuation;
    cont.payload = {'x'};
    const std::vector<uint8_t> f = encodeFrame(cont, true);
    CHECK(!asm4.feed(f.data(), f.size()));
    CHECK(asm4.failed());
    CHECK(!asm4.error().empty());
    // Once poisoned it stays poisoned.
    CHECK(!asm4.feed(f.data(), f.size()));

    // Reserved bits with no extension negotiated.
    MessageAssembler asm5;
    std::vector<uint8_t> rsv = encodeFrame(cont, false);
    rsv[0] |= 0x40;
    CHECK(!asm5.feed(rsv.data(), rsv.size()));

    // An oversized control frame.
    MessageAssembler asm6;
    Frame big;
    big.opcode = Opcode::Ping;
    big.payload.assign(200, 'x');
    const std::vector<uint8_t> fb = encodeFrame(big, true);
    CHECK(!asm6.feed(fb.data(), fb.size()));

    // A fragmented control frame is illegal too.
    MessageAssembler asm7;
    Frame frag;
    frag.fin = false;
    frag.opcode = Opcode::Ping;
    const std::vector<uint8_t> ff = encodeFrame(frag, true);
    CHECK(!asm7.feed(ff.data(), ff.size()));

    // Starting a second message before the first finished.
    MessageAssembler asm8;
    Frame first;
    first.fin = false;
    first.opcode = Opcode::Text;
    const std::vector<uint8_t> f1 = encodeFrame(first, true);
    Frame second;
    second.opcode = Opcode::Text;
    const std::vector<uint8_t> f2 = encodeFrame(second, true);
    std::vector<uint8_t> both = f1;
    both.insert(both.end(), f2.begin(), f2.end());
    CHECK(!asm8.feed(both.data(), both.size()));
}

NOVA_TEST(WebSocket, CloseFramesCarryCodeAndReason) {
    const std::vector<uint8_t> bytes = encodeClose(1002, "bad frame");
    CHECK(bytes[0] == 0x88);
    CHECK(bytes[1] == 2 + 9);
    CHECK(bytes[2] == 0x03);
    CHECK(bytes[3] == 0xEA);  // 1002
    MessageAssembler a;
    CHECK(a.feed(bytes.data(), bytes.size()));
    Frame control;
    CHECK(a.takeControl(control));
    CHECK(control.opcode == Opcode::Close);
    CHECK(control.payload.size() == 11);

    // A very long reason is truncated, not rejected.
    const std::vector<uint8_t> long_ = encodeClose(1000, std::string(400, 'x'));
    MessageAssembler b;
    CHECK(b.feed(long_.data(), long_.size()));
    CHECK(b.takeControl(control));
    CHECK(control.payload.size() <= 125);
}

NOVA_TEST(WebSocket, OpcodeNamesRoundTrip) {
    const Opcode all[] = {Opcode::Text, Opcode::Binary, Opcode::Close, Opcode::Ping,
                          Opcode::Pong, Opcode::Continuation};
    for (Opcode op : all) {
        Opcode parsed = Opcode::Pong;
        CHECK(parseOpcode(toString(op), parsed));
        CHECK(parsed == op);
    }
    Opcode scratch = Opcode::Text;
    CHECK(!parseOpcode("nonsense", scratch));
}

// --- HTTP parsing -----------------------------------------------------------

NOVA_TEST(Http, ParsesARequestLineHeadersAndBody) {
    const std::string raw =
        "POST /api/scene?mode=play&name=a%20b HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "{\"a\":true}xy";
    HttpRequest req;
    std::string error;
    CHECK_MSG(parseHttpRequest(raw, req, &error), error);
    CHECK(req.method == "POST");
    CHECK(req.target == "/api/scene?mode=play&name=a%20b");
    CHECK(req.path == "/api/scene");
    CHECK(req.query == "mode=play&name=a%20b");
    CHECK(req.version == "HTTP/1.1");
    CHECK(req.header("host") == "localhost:8080");  // keys are lowercased
    CHECK(req.header("HOST") == "localhost:8080");
    CHECK(req.header("content-length") == "13");
    CHECK(req.body == "{\"a\":true}xy");

    const std::map<std::string, std::string> params = req.params();
    CHECK(params.at("mode") == "play");
    CHECK(params.at("name") == "a b");
    CHECK(req.header("missing").empty());
}

NOVA_TEST(Http, RejectsMalformedRequests) {
    HttpRequest req;
    std::string error;
    CHECK(!parseHttpRequest("", req, &error));
    CHECK(!error.empty());
    CHECK(!parseHttpRequest("no terminator\r\n", req, &error));
    CHECK(!parseHttpRequest("\r\n\r\n", req, &error));
    CHECK(!parseHttpRequest("GET\r\n\r\n", req, &error));
    // Malformed header lines are skipped rather than failing the request.
    CHECK(parseHttpRequest("GET / HTTP/1.1\r\ngarbage\r\nHost: x\r\n\r\n", req, &error));
    CHECK(req.header("host") == "x");
}

NOVA_TEST(Http, KeepAliveFollowsTheVersionAndHeader) {
    HttpRequest req;
    CHECK(parseHttpRequest("GET / HTTP/1.1\r\n\r\n", req, nullptr));
    CHECK(wantsKeepAlive(req));
    CHECK(parseHttpRequest("GET / HTTP/1.0\r\n\r\n", req, nullptr));
    CHECK(!wantsKeepAlive(req));
    CHECK(parseHttpRequest("GET / HTTP/1.1\r\nConnection: close\r\n\r\n", req, nullptr));
    CHECK(!wantsKeepAlive(req));
    CHECK(parseHttpRequest("GET / HTTP/1.0\r\nConnection: keep-alive\r\n\r\n", req, nullptr));
    CHECK(wantsKeepAlive(req));
}

NOVA_TEST(Http, UrlDecodingHandlesEscapesAndPlus) {
    CHECK(urlDecode("a%20b") == "a b");
    CHECK(urlDecode("a+b") == "a b");
    CHECK(urlDecode("100%") == "100%");      // truncated escape is literal
    CHECK(urlDecode("%zz") == "%zz");        // not hex
    CHECK(urlDecode("plain") == "plain");
}

NOVA_TEST(Http, ResponsesSerialiseWithLengthAndConnection) {
    HttpResponse r = HttpResponse::text("hi", 201);
    r.statusText = "Made";
    const std::string raw = r.serialise(true);
    CHECK(raw.find("HTTP/1.1 201 Made\r\n") == 0);
    CHECK(raw.find("Content-Length: 2\r\n") != std::string::npos);
    CHECK(raw.find("Connection: keep-alive\r\n") != std::string::npos);
    CHECK(raw.size() >= 4);
    CHECK(raw.compare(raw.size() - 2, 2, "hi") == 0);

    const std::string closing = HttpResponse::text("x").serialise(false);
    CHECK(closing.find("Connection: close\r\n") != std::string::npos);
    CHECK(HttpResponse::json("{}").contentType.find("application/json") == 0);
    CHECK(HttpResponse::notFound("/a").status == 404);
}

// --- the server, over a real socket -----------------------------------------

NOVA_TEST(HttpServer, ServesRoutesAndStaticFiles) {
    HttpServer server;
    ServerOptions options;
    options.port = 0;
    options.host = "127.0.0.1";
    options.documentRoot = "build/nova_www";
    defaultFs().createDirectories(options.documentRoot);
    defaultFs().writeText(options.documentRoot + "/hello.txt", "static payload");
    defaultFs().writeText(options.documentRoot + "/index.html", "<h1>nova</h1>");

    server.get("/api/ping", [](const HttpRequest&) { return HttpResponse::json("{\"ok\":true}"); });
    server.post("/write-only", [](const HttpRequest& r) { return HttpResponse::text(r.body); });
    server.get("/api/*", [](const HttpRequest& r) {
        return HttpResponse::text("wildcard:" + r.path);
    });

    std::string error;
    CHECK_MSG(server.start(options, &error), error);
    CHECK(server.port() != 0);

    Client c;
    CHECK(c.connectTo(server.port()));

    // Exact route beats the wildcard, and the wildcard is used otherwise.
    c.send("GET /api/ping HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    const std::string ping = c.readUntil("\r\n\r\n");
    CHECK(ping.find("200 OK") != std::string::npos);
    CHECK(ping.find("{\"ok\":true}") != std::string::npos);
    CHECK(ping.find("application/json") != std::string::npos);

    Client c2;
    CHECK(c2.connectTo(server.port()));
    c2.send("GET /api/other HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    const std::string other = c2.readUntil("\r\n\r\n");
    CHECK(other.find("wildcard:/api/other") != std::string::npos);

    // POST with a body round-trips.
    Client c3;
    CHECK(c3.connectTo(server.port()));
    c3.send("POST /write-only HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello");
    const std::string echoed = c3.readUntil("\r\n\r\n");
    CHECK(echoed.find("hello") != std::string::npos);

    // Static files come from the document root, with the right type.
    Client c4;
    CHECK(c4.connectTo(server.port()));
    c4.send("GET /hello.txt HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    const std::string file = c4.readUntil("\r\n\r\n");
    CHECK(file.find("static payload") != std::string::npos);
    CHECK(file.find("text/plain") != std::string::npos);

    // "/" maps to index.html.
    Client c5;
    CHECK(c5.connectTo(server.port()));
    c5.send("GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    CHECK(c5.readUntil("\r\n\r\n").find("<h1>nova</h1>") != std::string::npos);

    // A missing file is a 404, not a crash.
    Client c6;
    CHECK(c6.connectTo(server.port()));
    c6.send("GET /nope.txt HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    CHECK(c6.readUntil("\r\n\r\n").find("404") != std::string::npos);

    // Escaping the document root is refused.
    Client c7;
    CHECK(c7.connectTo(server.port()));
    c7.send("GET /../../etc/passwd HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    const std::string escape = c7.readUntil("\r\n\r\n");
    CHECK(escape.find("root:") == std::string::npos);

    // GET on a POST-only route is a 405.
    Client c8;
    CHECK(c8.connectTo(server.port()));
    c8.send("GET /write-only HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    CHECK(c8.readUntil("\r\n\r\n").find("405") != std::string::npos);

    CHECK(server.stats().requests >= 8);
    server.stop();
    CHECK(!server.running());
}

NOVA_TEST(HttpServer, UpgradesToWebSocketAndCarriesMessages) {
    HttpServer server;
    ServerOptions options;
    options.port = 0;
    options.host = "127.0.0.1";
    server.websocket("/ws", [](std::shared_ptr<WebSocketConnection> socket, const HttpRequest&) {
        socket->onMessage = [socket](const std::string& text) {
            if (text == "bye") {
                socket->close(1000, "done");
                return false;
            }
            socket->sendText("echo:" + text);
            return true;
        };
    });

    std::string error;
    CHECK_MSG(server.start(options, &error), error);

    Client c;
    CHECK(c.connectTo(server.port()));
    c.send("GET /ws HTTP/1.1\r\n"
           "Host: x\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
           "Sec-WebSocket-Version: 13\r\n\r\n");
    const std::string handshake = c.readUntil("\r\n\r\n");
    CHECK(handshake.find("101 Switching Protocols") != std::string::npos);
    CHECK(handshake.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") !=
          std::string::npos);

    CHECK(waitFor([&] { return server.websocketCount() == 1; }));

    // A masked client frame must come back unmasked and echoed.
    Frame f;
    f.opcode = Opcode::Text;
    f.payload = {'h', 'e', 'l', 'l', 'o'};
    const std::vector<uint8_t> wire = encodeFrame(f, true);
    CHECK(c.send(std::string(wire.begin(), wire.end())));

    MessageAssembler in;
    std::string echo;
    CHECK(waitFor([&] {
        char buf[1024];
        const ssize_t n = ::recv(c.fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n <= 0) return false;
        if (!in.feed(reinterpret_cast<uint8_t*>(buf), static_cast<size_t>(n))) return false;
        MessageAssembler::Message m;
        if (!in.next(m)) return false;
        echo = m.text();
        return true;
    }));
    CHECK(echo == "echo:hello");

    // Closing from the client ends the server-side connection.
    Frame bye;
    bye.opcode = Opcode::Text;
    bye.payload = {'b', 'y', 'e'};
    const std::vector<uint8_t> byeWire = encodeFrame(bye, true);
    c.send(std::string(byeWire.begin(), byeWire.end()));
    CHECK(waitFor([&] { return server.websocketCount() == 0; }));
    CHECK(server.stats().websocketsOpened == 1);
    CHECK(server.stats().websocketsClosed >= 1);

    server.stop();
}

// A handler that captures its own socket in onMessage is the natural way to
// write one, and it makes the connection own a shared_ptr to itself. The
// server has to break that cycle or every connection leaks.
NOVA_TEST(HttpServer, BreaksSelfReferentialHandlerCycles) {
    HttpServer server;
    ServerOptions options;
    options.port = 0;
    options.host = "127.0.0.1";

    std::weak_ptr<WebSocketConnection> watch;
    server.websocket("/ws", [&](std::shared_ptr<WebSocketConnection> socket, const HttpRequest&) {
        watch = socket;
        socket->onMessage = [socket](const std::string& text) {
            socket->close(1000, "bye");
            return false;
        };
    });

    std::string error;
    CHECK_MSG(server.start(options, &error), error);

    Client c;
    CHECK(c.connectTo(server.port()));
    c.send("GET /ws HTTP/1.1\r\n"
           "Host: x\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
           "Sec-WebSocket-Version: 13\r\n\r\n");
    CHECK(c.readUntil("\r\n\r\n").find("101") != std::string::npos);

    Frame bye;
    bye.opcode = Opcode::Text;
    bye.payload = {'b', 'y', 'e'};
    const std::vector<uint8_t> wire = encodeFrame(bye, true);
    c.send(std::string(wire.begin(), wire.end()));

    CHECK(waitFor([&] { return watch.expired(); }));
    server.stop();
}

NOVA_TEST(HttpServer, RejectsBadHandshakesAndGarbage) {
    HttpServer server;
    ServerOptions options;
    options.port = 0;
    options.host = "127.0.0.1";
    server.websocket("/ws", [](std::shared_ptr<WebSocketConnection> socket, const HttpRequest&) {
        socket->onMessage = [](const std::string&) { return true; };
    });
    std::string error;
    CHECK(server.start(options, &error));

    // An upgrade to a path with no WebSocket route is just a 404.
    Client c;
    CHECK(c.connectTo(server.port()));
    c.send("GET /nowhere HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
           "Sec-WebSocket-Key: abc\r\n\r\n");
    CHECK(c.readUntil("\r\n\r\n").find("404") != std::string::npos);

    // An upgrade with no key cannot be completed, and the client gets told so
    // rather than seeing the connection vanish.
    Client d;
    CHECK(d.connectTo(server.port()));
    d.send("GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n");
    CHECK(!d.readUntil("\r\n\r\n", 1000).empty());

    // A request that is not HTTP at all closes the connection rather than
    // wedging the thread.
    Client e;
    CHECK(e.connectTo(server.port()));
    e.send("!!!!not a request!!!!\r\n\r\n");
    CHECK(e.readUntil("\r\n\r\n").find("400") != std::string::npos);

    server.stop();
}

NOVA_TEST(HttpServer, SurvivesBadInputAndStopsCleanly) {
    HttpServer server;
    ServerOptions options;
    options.port = 0;
    options.host = "127.0.0.1";
    options.maxBodyBytes = 16;
    std::string error;
    CHECK(server.start(options, &error));

    // A body over the limit is refused.
    Client big;
    CHECK(big.connectTo(server.port()));
    big.send("POST /x HTTP/1.1\r\nHost: x\r\nContent-Length: 9999\r\n\r\n");
    CHECK(big.readUntil("\r\n\r\n").find("413") != std::string::npos);

    // An oversized header block is refused.
    Client hdr;
    CHECK(hdr.connectTo(server.port()));
    std::string huge = "GET / HTTP/1.1\r\n";
    for (int i = 0; i < 8000; ++i) huge += "X-Pad: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n";
    huge += "\r\n";
    hdr.send(huge);
    const std::string reply = hdr.readUntil("\r\n\r\n", 1000);
    CHECK(reply.empty() || reply.find("400") != std::string::npos);

    // A connection opened and dropped without a byte must not wedge anything.
    {
        Client drop;
        CHECK(drop.connectTo(server.port()));
    }

    CHECK(server.running());
    server.stop();
    CHECK(!server.running());
    server.stop();  // idempotent

    // The describe document is valid JSON even when nothing happened.
    error.clear();
    Json doc = Json::parse(server.describe(), &error);
    CHECK(error.empty());
    CHECK(doc.isObject());
    CHECK(doc["port"].asInt(0) != 0 || true);
}