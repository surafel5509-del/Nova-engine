#include "particles/ParticleSystem.h"

#include <cmath>

namespace nova {

namespace {
constexpr size_t kMaxParticlesPerEmitter = 1024;
}

void ParticleSystem::configure(const std::vector<ParticleEmitterRecord>& emitters) {
    emitters_ = emitters;
    pools_.clear();
    pools_.resize(emitters_.size());
    emissionAccum_.assign(emitters_.size(), 0.0f);
}

void ParticleSystem::clear() {
    emitters_.clear();
    pools_.clear();
    emissionAccum_.clear();
}

size_t ParticleSystem::totalParticles() const {
    size_t n = 0;
    for (const auto& pool : pools_) n += pool.size();
    return n;
}

float ParticleSystem::nextRandom() {
    // xorshift32
    uint32_t x = rngState_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rngState_ = x;
    return static_cast<float>(x & 0xFFFFFF) / static_cast<float>(0x1000000);
}

void ParticleSystem::spawn(size_t emitterIndex) {
    const ParticleEmitterRecord& e = emitters_[emitterIndex];
    auto& pool = pools_[emitterIndex];
    if (pool.size() >= kMaxParticlesPerEmitter) return;

    Particle p;
    p.x = e.x;
    p.y = e.y;
    const float angle = e.direction + (nextRandom() - 0.5f) * e.spread * 2.0f;
    const float speed = e.speed * (0.7f + 0.6f * nextRandom());
    p.vx = std::cos(angle) * speed;
    p.vy = std::sin(angle) * speed;
    p.age = 0.0f;
    p.lifetime = e.lifetime * (0.7f + 0.6f * nextRandom());
    if (p.lifetime < 0.05f) p.lifetime = 0.05f;
    p.startSize = e.startSize;
    p.endSize = e.endSize;
    p.r = e.r;
    p.g = e.g;
    p.b = e.b;
    pool.push_back(p);
}

void ParticleSystem::update(float dt) {
    if (dt <= 0.0f) return;
    for (size_t i = 0; i < emitters_.size(); ++i) {
        const ParticleEmitterRecord& e = emitters_[i];
        auto& pool = pools_[i];

        // Emit.
        float& accum = emissionAccum_[i];
        if (emitting_) {
            accum += e.emissionRate * dt;
            while (accum >= 1.0f) {
                accum -= 1.0f;
                spawn(i);
            }
        } else {
            accum = 0.0f;
        }

        // Integrate + cull.
        for (size_t k = 0; k < pool.size();) {
            Particle& p = pool[k];
            p.age += dt;
            if (p.age >= p.lifetime) {
                pool[k] = pool.back();
                pool.pop_back();
                continue;
            }
            p.vy += e.gravity * dt;
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            ++k;
        }
    }
}

} // namespace nova
