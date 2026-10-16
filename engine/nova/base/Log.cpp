#include "base/Log.h"

#include <cstdlib>
#include <cstring>

namespace nova {

const char* toString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        case LogLevel::Off:   return "OFF";
    }
    return "?";
}

LogLevel parseLogLevel(const std::string& text, LogLevel fallback) {
    std::string t;
    for (char c : text) t += static_cast<char>(::toupper(static_cast<unsigned char>(c)));
    if (t == "TRACE") return LogLevel::Trace;
    if (t == "DEBUG") return LogLevel::Debug;
    if (t == "INFO")  return LogLevel::Info;
    if (t == "WARN" || t == "WARNING") return LogLevel::Warn;
    if (t == "ERROR") return LogLevel::Error;
    if (t == "FATAL") return LogLevel::Fatal;
    if (t == "OFF" || t == "NONE") return LogLevel::Off;
    return fallback;
}

Log& Log::instance() {
    static Log logger;
    return logger;
}

void Log::write(LogLevel level, const char* channel, const char* fmt, ...) {
    if (level < level_ || level_ == LogLevel::Off) return;

    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    LogRecord rec;
    rec.level = level;
    rec.channel = channel ? channel : "nova";
    rec.message = buffer;
    rec.time = clockFn_ ? clockFn_() : 0.0;

    std::lock_guard<std::mutex> lock(mutex_);
    if (mirrorToStdout_) {
        const char* color = "";
        switch (level) {
            case LogLevel::Error:
            case LogLevel::Fatal: color = "\033[31m"; break;
            case LogLevel::Warn:  color = "\033[33m"; break;
            case LogLevel::Debug: color = "\033[90m"; break;
            case LogLevel::Info:  color = "\033[36m"; break;
            default: break;
        }
        std::fprintf(stderr, "%s[%s] %-10s %s\033[0m\n", color, toString(level),
                     rec.channel.c_str(), rec.message.c_str());
    }

    if (ring_.size() >= kRingCapacity) {
        // Drop the oldest half in one shift rather than one erase per line.
        ring_.erase(ring_.begin(), ring_.begin() + kRingCapacity / 2);
        dropped_ += kRingCapacity / 2;
    }
    ring_.push_back(std::move(rec));
}

std::vector<LogRecord> Log::recent(size_t maxCount) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (maxCount == 0 || ring_.size() <= maxCount) return ring_;
    return std::vector<LogRecord>(ring_.end() - static_cast<long>(maxCount), ring_.end());
}

size_t Log::dropCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
}

void Log::clearRecent() {
    std::lock_guard<std::mutex> lock(mutex_);
    ring_.clear();
    dropped_ = 0;
}

void fatalAbort(const char* channel, const char* fmt, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    Log::instance().write(LogLevel::Fatal, channel, "%s", buffer);
    std::abort();
}

}  // namespace nova
