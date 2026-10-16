// WebSocket frames (RFC 6455): encode, decode, and the opening handshake.
//
// Frame handling is separated from the socket so it can be tested without a
// network. [WebSocketFrame] is a pure parser over a byte buffer; the server
// feeds it whatever it has read and sends back whatever it produces.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nova {
namespace net {

enum class Opcode : uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

const char* toString(Opcode op);
bool parseOpcode(const std::string& name, Opcode& out);

struct Frame {
    bool fin = true;
    Opcode opcode = Opcode::Text;
    bool masked = false;   ///< clients must mask; servers must not
    std::vector<uint8_t> payload;

    bool isControl() const {
        return opcode == Opcode::Close || opcode == Opcode::Ping || opcode == Opcode::Pong;
    }
    /// A control frame is capped at 125 bytes and may not be fragmented.
    bool legal() const;
    std::string text() const { return std::string(payload.begin(), payload.end()); }
};

/// Serialises one frame. [mask] applies the client-side masking; a server must
/// pass false, and a client must pass true.
std::vector<uint8_t> encodeFrame(const Frame& frame, bool mask);

/// Builds a close frame carrying [code] and [reason] (RFC 6455 section 7.4).
std::vector<uint8_t> encodeClose(uint16_t code, const std::string& reason);

/// Reassembles a stream of frames into messages, handling continuation. The
/// server pushes bytes in as they arrive and pops complete messages out.
class MessageAssembler {
public:
    /// Appends raw bytes read from the socket. Returns false on a protocol
    /// violation, after which the assembler stays poisoned.
    bool feed(const uint8_t* data, size_t size);

    struct Message {
        Opcode opcode = Opcode::Text;  ///< Text or Binary; the first fragment's
        std::vector<uint8_t> payload;
        std::string text() const { return std::string(payload.begin(), payload.end()); }
    };

    /// Pops the next complete message. Returns false when none is ready yet.
    bool next(Message& out);

    /// A control frame seen while assembling: the caller must answer it even if
    /// the message it interrupts is still incomplete.
    bool takeControl(Frame& out);

    bool failed() const { return failed_; }
    std::string error() const { return error_; }
    void reset();

    /// Bytes fed but not yet consumed by a complete frame, for diagnostics.
    size_t buffered() const { return buffer_.size() - cursor_; }

private:
    bool parse();

    std::vector<uint8_t> buffer_;
    size_t cursor_ = 0;
    bool failed_ = false;
    std::string error_;

    std::vector<Message> messages_;
    std::vector<Frame> controls_;

    Frame assembling_;
    bool assemblingActive_ = false;
};

/// A close code is only meaningful in the ranges the RFC defines.
bool legalCloseCode(uint16_t code);
const char* closeReason(uint16_t code);

}  // namespace net
}  // namespace nova