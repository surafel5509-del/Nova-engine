// Nova Engine — logging.
//
// Thread-safe, allocation-light console logger with levels, named channels and
// an optional in-memory ring buffer so the editor can show past log lines
// without a file on the device.
#pragma once

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace nova {

enum class LogLevel { Trace = 0, Debug, Info, Warn, Error, Fatal, Off };

const char* toString(LogLevel level);
LogLevel parseLogLevel(const std::string& text, LogLevel fallback = LogLevel::Info);

struct LogRecord {
    LogLevel level = LogLevel::Info;
    std::string channel;
    std::string message;
    double time = 0.0;
};

/// Singleton console logger. All engine macros funnel through here.
class Log {
public:
    static Log& instance();

    void setLevel(LogLevel level) { level_ = level; }
    LogLevel level() const { return level_; }
    /// When false, records are dropped after the level check (default in tools).
    void setMirrorToStdout(bool on) { mirrorToStdout_ = on; }
    /// Binds the logger to the app clock so records carry a timestamp.
    void setClockSource(double (*fn)()) { clockFn_ = fn; }

    void write(LogLevel level, const char* channel, const char* fmt, ...)
        __attribute__((format(printf, 4, 5)));

    /// Ring-buffer access for the editor console panel.
    std::vector<LogRecord> recent(size_t maxCount) const;
    size_t dropCount() const;
    void clearRecent();

private:
    Log() = default;
    mutable std::mutex mutex_;
    LogLevel level_ = LogLevel::Info;
    bool mirrorToStdout_ = true;
    double (*clockFn_)() = nullptr;
    std::vector<LogRecord> ring_;
    size_t dropped_ = 0;
    static constexpr size_t kRingCapacity = 2048;
};

}  // namespace nova

#define NOVA_LOG(level, channel, ...) \
    ::nova::Log::instance().write(level, channel, __VA_ARGS__)

#define NOVA_TRACE(channel, ...) NOVA_LOG(::nova::LogLevel::Trace, channel, __VA_ARGS__)
#define NOVA_DEBUG(channel, ...) NOVA_LOG(::nova::LogLevel::Debug, channel, __VA_ARGS__)
#define NOVA_INFO(channel, ...)  NOVA_LOG(::nova::LogLevel::Info,  channel, __VA_ARGS__)
#define NOVA_WARN(channel, ...)  NOVA_LOG(::nova::LogLevel::Warn,  channel, __VA_ARGS__)
#define NOVA_ERROR(channel, ...) NOVA_LOG(::nova::LogLevel::Error, channel, __VA_ARGS__)

/// Fatal errors abort after logging (kept out of the header macro for clarity).
[[noreturn]] void fatalAbort(const char* channel, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));
