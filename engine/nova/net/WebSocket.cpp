#include "net/WebSocket.h"

#include <cstring>

namespace nova {
namespace net {

namespace {

/// RFC 6455 caps a message at 100 MiB. Anything larger is a hostile or broken
/// peer; refusing it keeps a single frame from exhausting memory.
constexpr size_t kMaxPayload = 100u * 1024u * 1024u;

}  // namespace

const char* toString(Opcode op) {
    switch (op) {
        case Opcode::Continuation: return "continuation";
        case Opcode::Text: return "text";
        case Opcode::Binary: return "binary";
        case Opcode::Close: return "close";
        case Opcode::Ping: return "ping";
        case Opcode::Pong: return "pong";
        default: return "unknown";
    }
}

bool parseOpcode(const std::string& name, Opcode& out) {
    if (name == "text") { out = Opcode::Text; return true; }
    if (name == "binary") { out = Opcode::Binary; return true; }
    if (name == "close") { out = Opcode::Close; return true; }
    if (name == "ping") { out = Opcode::Ping; return true; }
    if (name == "pong") { out = Opcode::Pong; return true; }
    if (name == "continuation") { out = Opcode::Continuation; return true; }
    return false;
}

bool Frame::legal() const {
    if (isControl()) {
        return payload.size() <= 125 && fin;
    }
    return opcode == Opcode::Continuation || opcode == Opcode::Text || opcode == Opcode::Binary;
}

std::vector<uint8_t> encodeFrame(const Frame& frame, bool mask) {
    std::vector<uint8_t> out;
    out.reserve(frame.payload.size() + 14);

    out.push_back(static_cast<uint8_t>((frame.fin ? 0x80u : 0x00u) |
                                       (static_cast<uint8_t>(frame.opcode) & 0x0Fu)));
    const uint8_t maskBit = mask ? 0x80u : 0x00u;
    const size_t len = frame.payload.size();
    if (len < 126) {
        out.push_back(static_cast<uint8_t>(maskBit | static_cast<uint8_t>(len)));
    } else if (len <= 0xFFFF) {
        out.push_back(static_cast<uint8_t>(maskBit | 126u));
        out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        out.push_back(static_cast<uint8_t>(maskBit | 127u));
        for (int i = 7; i >= 0; --i) {
            out.push_back(static_cast<uint8_t>((static_cast<uint64_t>(len) >> (8 * i)) & 0xFF));
        }
    }

    if (mask) {
        // A fixed key is fine for a server: masking exists to defeat proxy
        // cache poisoning, not to provide confidentiality.
        uint8_t key[4] = {0x37, 0xFA, 0x21, 0x3D};
        for (int i = 0; i < 4; ++i) out.push_back(key[i]);
        const size_t bodyStart = out.size();
        out.insert(out.end(), frame.payload.begin(), frame.payload.end());
        for (size_t i = 0; i < len; ++i) out[bodyStart + i] ^= key[i % 4];
    } else {
        out.insert(out.end(), frame.payload.begin(), frame.payload.end());
    }
    return out;
}

std::vector<uint8_t> encodeClose(uint16_t code, const std::string& reason) {
    Frame f;
    f.fin = true;
    f.opcode = Opcode::Close;
    f.payload.resize(2);
    f.payload[0] = static_cast<uint8_t>((code >> 8) & 0xFF);
    f.payload[1] = static_cast<uint8_t>(code & 0xFF);
    // The reason is truncated rather than rejected: a long one is a client bug,
    // not a reason to drop the connection.
    const size_t room = reason.size() < 123 ? reason.size() : 123;
    f.payload.insert(f.payload.end(), reason.begin(), reason.begin() + static_cast<long>(room));
    return encodeFrame(f, false);
}

bool legalCloseCode(uint16_t code) {
    if (code >= 3000 && code <= 4999) return true;  // registered / application
    switch (code) {
        case 1000: case 1001: case 1002: case 1003:
        case 1007: case 1008: case 1009: case 1010: case 1011:
            return true;
        default:
            return false;  // 1004-1006, 1012-1014 and 1015 are reserved
    }
}

const char* closeReason(uint16_t code) {
    switch (code) {
        case 1000: return "normal closure";
        case 1001: return "going away";
        case 1002: return "protocol error";
        case 1003: return "unsupported data";
        case 1007: return "invalid payload";
        case 1008: return "policy violation";
        case 1009: return "message too big";
        case 1010: return "mandatory extension";
        case 1011: return "internal error";
        default: return code >= 3000 ? "application specific" : "reserved";
    }
}

// --- MessageAssembler -------------------------------------------------------

void MessageAssembler::reset() {
    buffer_.clear();
    cursor_ = 0;
    failed_ = false;
    error_.clear();
    messages_.clear();
    controls_.clear();
    assemblingActive_ = false;
    assembling_ = Frame{};
}

bool MessageAssembler::feed(const uint8_t* data, size_t size) {
    if (failed_) return false;
    if (data && size > 0) {
        buffer_.insert(buffer_.end(), data, data + size);
    }
    return parse();
}

bool MessageAssembler::parse() {
    while (!failed_) {
        if (buffer_.size() - cursor_ < 2) return true;

        const uint8_t b0 = buffer_[cursor_];
        const uint8_t b1 = buffer_[cursor_ + 1];
        const bool fin = (b0 & 0x80u) != 0;
        const uint8_t rsv = static_cast<uint8_t>(b0 & 0x70u);
        const uint8_t op = static_cast<uint8_t>(b0 & 0x0Fu);
        const bool masked = (b1 & 0x80u) != 0;
        uint64_t len = static_cast<uint64_t>(b1 & 0x7Fu);
        size_t headerLen = 2;

        if (rsv != 0) {
            failed_ = true;
            error_ = "reserved bits set but no extension negotiated";
            return false;
        }
        if (len == 126) {
            if (buffer_.size() - cursor_ < 4) return true;
            len = (static_cast<uint64_t>(buffer_[cursor_ + 2]) << 8) |
                  static_cast<uint64_t>(buffer_[cursor_ + 3]);
            headerLen = 4;
        } else if (len == 127) {
            if (buffer_.size() - cursor_ < 10) return true;
            len = 0;
            for (int i = 0; i < 8; ++i) {
                len = (len << 8) | static_cast<uint64_t>(buffer_[cursor_ + 2 + static_cast<size_t>(i)]);
            }
            headerLen = 10;
        }
        if (len > kMaxPayload) {
            failed_ = true;
            error_ = "payload exceeds the 100 MiB limit";
            return false;
        }

        uint8_t maskKey[4] = {0, 0, 0, 0};
        if (masked) {
            if (buffer_.size() - cursor_ < headerLen + 4) return true;
            std::memcpy(maskKey, buffer_.data() + cursor_ + headerLen, 4);
            headerLen += 4;
        }

        if (buffer_.size() - cursor_ < headerLen + static_cast<size_t>(len)) return true;

        Frame frame;
        frame.fin = fin;
        frame.masked = masked;
        frame.opcode = static_cast<Opcode>(op);
        frame.payload.resize(static_cast<size_t>(len));
        if (len > 0) {
            std::memcpy(frame.payload.data(), buffer_.data() + cursor_ + headerLen,
                        static_cast<size_t>(len));
            if (masked) {
                for (size_t i = 0; i < frame.payload.size(); ++i) {
                    frame.payload[i] ^= maskKey[i % 4];
                }
            }
        }
        cursor_ += headerLen + static_cast<size_t>(len);

        if (!frame.legal()) {
            failed_ = true;
            error_ = std::string("illegal ") + toString(frame.opcode) + " frame";
            return false;
        }

        if (frame.isControl()) {
            // A control frame may interrupt an in-flight message, so it is kept
            // on its own queue rather than fed into the assembler.
            if (assemblingActive_) {
                controls_.push_back(frame);
            } else {
                controls_.push_back(frame);
            }
            continue;
        }

        if (frame.opcode == Opcode::Continuation) {
            if (!assemblingActive_) {
                failed_ = true;
                error_ = "continuation frame with nothing to continue";
                return false;
            }
            assembling_.payload.insert(assembling_.payload.end(), frame.payload.begin(),
                                      frame.payload.end());
            if (frame.fin) {
                Message m;
                m.opcode = assembling_.opcode;
                m.payload = assembling_.payload;
                messages_.push_back(std::move(m));
                assemblingActive_ = false;
                assembling_ = Frame{};
            }
        } else {
            if (assemblingActive_) {
                failed_ = true;
                error_ = "new message started before the previous one finished";
                return false;
            }
            if (frame.fin) {
                Message m;
                m.opcode = frame.opcode;
                m.payload = frame.payload;
                messages_.push_back(std::move(m));
            } else {
                assemblingActive_ = true;
                assembling_.opcode = frame.opcode;
                assembling_.payload = frame.payload;
            }
        }
    }
    return !failed_;
}

bool MessageAssembler::next(Message& out) {
    if (messages_.empty()) return false;
    out = std::move(messages_.front());
    messages_.erase(messages_.begin());
    return true;
}

bool MessageAssembler::takeControl(Frame& out) {
    if (controls_.empty()) return false;
    out = std::move(controls_.front());
    controls_.erase(controls_.begin());
    return true;
}

}  // namespace net
}  // namespace nova