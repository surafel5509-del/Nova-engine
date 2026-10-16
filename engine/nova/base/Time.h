// Nova Engine — time, clocks and the fixed-timestep accumulator.
#pragma once

#include <cstdint>
#include <string>

namespace nova {

/// Monotonic wall clock in seconds since process start.
double nowSeconds();
/// Wall-clock Unix time in seconds (for file stamps / telemetry).
double unixSeconds();
/// Formats unix time as ISO-8601 UTC, e.g. "2026-09-29T13:39:00Z".
std::string isoTimestamp();
/// Sleeps the calling thread. Engine systems must not busy-wait.
void sleepSeconds(double seconds);

class Clock {
public:
    Clock() { reset(); }
    void reset();
    /// Restarts the delta clock without disturbing [elapsed()].
    void tick();
    double elapsed() const { return elapsed_; }
    double delta() const { return delta_; }
    /// Smoothed frames-per-second, updated by [sampleFps].
    double fps() const { return fps_; }
    double msPerFrame() const { return fps_ > 0.0 ? 1000.0 / fps_ : 0.0; }
    void sampleFps(double dt);

private:
    double start_ = 0.0;
    double last_ = 0.0;
    double elapsed_ = 0.0;
    double delta_ = 0.0;
    double fps_ = 0.0;
};

/// Accumulates real time and emits fixed-size simulation steps.
///
/// A fixed step keeps physics and script behaviour deterministic and
/// frame-rate independent; a max-accumulator clamp stops the "spiral of
/// death" when the machine cannot keep up.
class FixedTimeStepper {
public:
    /// @param fixedDelta seconds of simulation per step (e.g. 1/60)
    /// @param maxSteps  upper bound on catch-up steps per frame (0 = unlimited)
    explicit FixedTimeStepper(float fixedDelta = 1.0f / 60.0f, int maxSteps = 5)
        : fixedDelta_(fixedDelta), maxSteps_(maxSteps) {}

    void setFixedDelta(float d) { fixedDelta_ = d > 0.0f ? d : 1.0f / 60.0f; }
    float fixedDelta() const { return fixedDelta_; }
    void setMaxSteps(int n) { maxSteps_ = n; }
    /// Longest single frame that will be integrated. Anything beyond this
    /// (a debugger pause, a stalled load) is treated as a time jump.
    void setMaxFrameTime(double seconds) { maxFrameTime_ = seconds; }
    double maxFrameTime() const { return maxFrameTime_; }

    double alpha() const { return accumulator_ / fixedDelta_; }  ///< 0..1 interpolation
    void reset() { accumulator_ = 0.0; }
    /// Total real time discarded because the simulation fell behind.
    double droppedTime() const { return droppedTime_; }
    /// One-off: forces the next [advance] to run exactly one step.
    void requestStep() { pendingSteps_ = 1; }
    /// One-off: runs [count] steps regardless of accumulated time.
    int consumePendingSteps() {
        if (pendingSteps_ <= 0) return 0;
        const int n = pendingSteps_;
        pendingSteps_ = 0;
        accumulator_ = 0.0;
        return n;
    }

    /// Adds real time and reports how many fixed steps to run.
    int advance(double frameDelta) {
        if (frameDelta > maxFrameTime_) frameDelta = maxFrameTime_;  // clamp spikes
        accumulator_ += frameDelta;
        int steps = 0;
        // The epsilon keeps an exact multiple (0.1 + 0.1 + 0.1) from losing a
        // step to binary floating point representation error.
        const double eps = fixedDelta_ * 1e-6;
        while (accumulator_ + eps >= fixedDelta_ && (maxSteps_ <= 0 || steps < maxSteps_)) {
            accumulator_ -= fixedDelta_;
            if (accumulator_ < 0.0) accumulator_ = 0.0;
            ++steps;
        }
        if (maxSteps_ > 0 && accumulator_ > fixedDelta_ * static_cast<double>(maxSteps_)) {
            // Discard the backlog: the simulation is behind real time.
            accumulator_ = 0.0;
            droppedTime_ += frameDelta;
        }
        return steps;
    }

private:
    float fixedDelta_;
    int maxSteps_;
    double accumulator_ = 0.0;
    double droppedTime_ = 0.0;
    double maxFrameTime_ = 0.25;  // never integrate more than 250ms at once
    int pendingSteps_ = 0;
};

}  // namespace nova
