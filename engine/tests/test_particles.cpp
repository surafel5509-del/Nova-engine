// Host-side tests for the CPU particle system (GL-free).
#include <cmath>
#include <cstdio>

#include "particles/ParticleSystem.h"

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* name) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", name);
    }
}

nova::ParticleEmitterRecord makeEmitter(float emissionRate = 10.0f, float lifetime = 1.0f) {
    nova::ParticleEmitterRecord e;
    e.id = "torch";
    e.x = 2.0f;
    e.y = -1.0f;
    e.emissionRate = emissionRate;
    e.lifetime = lifetime;
    e.speed = 2.0f;
    e.gravity = -1.0f;
    e.startSize = 0.4f;
    e.endSize = 0.1f;
    e.direction = 1.5708f; // up
    e.spread = 0.5f;
    return e;
}

void testIdleEmitsNothing() {
    std::vector<nova::ParticleEmitterRecord> emitters;
    nova::ParticleSystem ps;
    ps.configure(emitters);
    ps.update(1.0f);
    check(ps.totalParticles() == 0, "no emitters, no particles");
}

void testEmissionAccumulates() {
    std::vector<nova::ParticleEmitterRecord> emitters = { makeEmitter(10.0f, 5.0f) };
    nova::ParticleSystem ps;
    ps.configure(emitters);
    ps.setSeed(42);
    // 10/s for one second -> ~10 particles (lifetime keeps them alive).
    for (int i = 0; i < 60; ++i) ps.update(1.0f / 60.0f);
    check(ps.totalParticles() >= 8 && ps.totalParticles() <= 12, "emission rate honored");
}

void testParticlesDieAfterLifetime() {
    std::vector<nova::ParticleEmitterRecord> emitters = { makeEmitter(50.0f, 0.5f) };
    nova::ParticleSystem ps;
    ps.configure(emitters);
    for (int i = 0; i < 30; ++i) ps.update(1.0f / 60.0f); // 0.5 s of emission
    size_t aliveMid = ps.totalParticles();
    check(aliveMid > 0, "particles alive mid-lifetime");
    ps.setEmitting(false);
    for (int i = 0; i < 90; ++i) ps.update(1.0f / 60.0f); // 1.5 more seconds
    check(ps.totalParticles() == 0, "all particles dead after lifetime");
}

void testParticlesMoveWithGravity() {
    std::vector<nova::ParticleEmitterRecord> emitters = { makeEmitter(20.0f, 5.0f) };
    nova::ParticleSystem ps;
    ps.configure(emitters);
    ps.setSeed(7);
    for (int i = 0; i < 30; ++i) ps.update(1.0f / 60.0f);
    const auto& live = ps.particles(0);
    bool movedOffOrigin = false;
    for (const auto& p : live) {
        if (std::fabs(p.x - 2.0f) > 0.001f || std::fabs(p.y - (-1.0f)) > 0.001f) {
            movedOffOrigin = true;
        }
    }
    check(movedOffOrigin, "particles integrate velocity");
    bool fallen = false;
    for (const auto& p : live) {
        if (p.vy < 2.0f) fallen = true;  // gravity pulls vy below initial
    }
    check(fallen, "gravity applies to particles");
}

void testClearResets() {
    std::vector<nova::ParticleEmitterRecord> emitters = { makeEmitter() };
    nova::ParticleSystem ps;
    ps.configure(emitters);
    ps.update(0.5f);
    check(ps.totalParticles() > 0, "particles before clear");
    ps.clear();
    check(ps.totalParticles() == 0, "clear empties pools");
    check(ps.emitterCount() == 0, "clear removes emitters");
}

} // namespace

int runParticleTests() {
    testIdleEmitsNothing();
    testEmissionAccumulates();
    testParticlesDieAfterLifetime();
    testParticlesMoveWithGravity();
    testClearResets();
    std::printf("particles: %d checks, %d failures\n", checks, failures);
    return failures;
}
