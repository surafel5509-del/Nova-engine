#pragma once

#include <cstdint>
#include <vector>

#include "scene/RenderScene.h"

namespace nova {

/** One live particle. */
struct Particle {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float age = 0.0f;
    float lifetime = 1.0f;
    float startSize = 0.25f;
    float endSize = 0.05f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

/**
 * CPU particle simulation feeding quad instances to the renderer.
 * GL-free and host-testable: update() integrates, particles() exposes state.
 * A fast, deterministic xorshift RNG keeps behavior reproducible in tests.
 */
class ParticleSystem {
public:
    /** Resets all emitters from the scene's emitter records. */
    void configure(const std::vector<ParticleEmitterRecord>& emitters);
    void clear();

    /** Advances emission + integration by [dt] seconds. */
    void update(float dt);

    const std::vector<ParticleEmitterRecord>& emitters() const { return emitters_; }

    /** Live particles for emitter index. */
    const std::vector<Particle>& particles(size_t emitterIndex) const {
        return pools_[emitterIndex];
    }

    size_t emitterCount() const { return emitters_.size(); }
    size_t totalParticles() const;

    /** Toggle emission (integration continues; burst control). */
    void setEmitting(bool emitting) { emitting_ = emitting; }

    /** Deterministic RNG seed (tests). */
    void setSeed(uint32_t seed) { rngState_ = seed ? seed : 0x9E3779B9u; }

private:
    float nextRandom();          // 0..1
    void spawn(size_t emitterIndex);

    std::vector<ParticleEmitterRecord> emitters_;
    std::vector<std::vector<Particle>> pools_;
    std::vector<float> emissionAccum_;
    uint32_t rngState_ = 0x9E3779B9u;
    bool emitting_ = true;
};

} // namespace nova
