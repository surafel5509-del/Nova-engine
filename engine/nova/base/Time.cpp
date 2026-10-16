#include "base/Time.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <thread>

namespace nova {

double nowSeconds() {
    using clock = std::chrono::steady_clock;
    static const clock::time_point origin = clock::now();
    return std::chrono::duration<double>(clock::now() - origin).count();
}

double unixSeconds() {
    using clock = std::chrono::system_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

std::string isoTimestamp() {
    const std::time_t t = static_cast<std::time_t>(unixSeconds());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

void sleepSeconds(double seconds) {
    if (seconds <= 0.0) return;
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}

void Clock::reset() {
    start_ = last_ = nowSeconds();
    elapsed_ = 0.0;
    delta_ = 0.0;
}

void Clock::tick() {
    const double t = nowSeconds();
    delta_ = t - last_;
    last_ = t;
    elapsed_ = t - start_;
}

void Clock::sampleFps(double dt) {
    if (dt <= 0.0) return;
    const double instant = 1.0 / dt;
    // Exponential average: responsive without flickering every frame.
    fps_ = (fps_ <= 0.0) ? instant : fps_ + (instant - fps_) * 0.1;
}

}  // namespace nova
