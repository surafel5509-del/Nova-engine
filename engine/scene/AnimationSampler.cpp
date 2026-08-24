#include "scene/AnimationSampler.h"

#include <algorithm>
#include <cmath>

namespace nova {

void AnimationSampler::apply(RenderScene& scene, float elapsedSeconds) {
    for (const AnimationTrack& track : scene.animations) {
        SpriteInstance* sprite = scene.findSprite(track.entityId);
        if (!sprite) continue;
        writeProperty(*sprite, track.property, sample(track, elapsedSeconds));
    }
}

float AnimationSampler::sample(const AnimationTrack& track, float t) {
    if (track.keys.empty()) return 0.0f;
    if (track.keys.size() == 1) return track.keys[0].value;

    const float start = track.keys.front().t;
    const float end = track.keys.back().t;
    const float duration = end - start;
    if (duration <= 0.0f) return track.keys.back().value;

    float time = t;
    if (track.loop) {
        float wrapped = std::fmod(t - start, duration);
        if (wrapped < 0.0f) wrapped += duration;
        time = start + wrapped;
    } else {
        time = std::clamp(t, start, end);
    }

    // Find the keyframe segment containing [time].
    for (size_t i = 0; i + 1 < track.keys.size(); ++i) {
        const AnimationKey& k0 = track.keys[i];
        const AnimationKey& k1 = track.keys[i + 1];
        if (time >= k0.t && time <= k1.t) {
            const float span = k1.t - k0.t;
            const float f = span > 0.0f ? (time - k0.t) / span : 0.0f;
            return k0.value + (k1.value - k0.value) * f;
        }
    }
    return track.keys.back().value;
}

void AnimationSampler::writeProperty(SpriteInstance& sprite, const std::string& property, float value) {
    if (property == "x") sprite.x = value;
    else if (property == "y") sprite.y = value;
    else if (property == "rotation") sprite.rotation = value;
    else if (property == "scaleX") sprite.scaleX = value;
    else if (property == "scaleY") sprite.scaleY = value;
}

} // namespace nova
