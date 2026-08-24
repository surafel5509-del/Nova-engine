#pragma once

#include "scene/RenderScene.h"

namespace nova {

/**
 * Keyframe animation sampler (GL-free, host-testable). Samples each track at
 * an elapsed time and writes the value into the matching sprite property.
 */
class AnimationSampler {
public:
    /**
     * Applies all tracks in [scene] at time [elapsedSeconds].
     * [elapsedSeconds] is simulation time since play started.
     */
    static void apply(RenderScene& scene, float elapsedSeconds);

    /** Linear-interpolated value of a track at [t] (handles loop). */
    static float sample(const AnimationTrack& track, float t);

private:
    static void writeProperty(SpriteInstance& sprite, const std::string& property, float value);
};

} // namespace nova
