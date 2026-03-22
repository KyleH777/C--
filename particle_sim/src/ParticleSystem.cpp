#include "ParticleSystem.h"

#include <algorithm>
#include <cmath>

namespace {
    constexpr float kGravity       = 200.f;   // pixels / s² (downward)
    constexpr float kUpwardBias    = 320.f;   // added to initial velocity.y
}

// ── Construction ──────────────────────────────────────────────────────────────
ParticleSystem::ParticleSystem(std::size_t maxParticles)
    : m_vertices(sf::Quads)     // set primitive type once — never changes
    , m_maxParticles(maxParticles)
{
    // ── Why reserve() matters ─────────────────────────────────────────────────
    //
    // std::vector grows by doubling when its size reaches its capacity. Without
    // an explicit reserve(), the first push_backs trigger a series of:
    //   1. malloc()  — allocate a larger contiguous block
    //   2. move/copy — relocate every existing Particle to the new block
    //   3. free()    — release the old block
    //
    // These reallocations are:
    //   • Slow    — malloc + free in the middle of a game loop stalls rendering.
    //   • Spiky   — latency is unpredictable (a single frame may pay for
    //               N moves when capacity doubles).
    //   • Unsafe  — any pointer or iterator into the vector is invalidated on
    //               reallocation; bugs here are hard to catch.
    //
    // reserve(maxParticles) pre-allocates the entire block upfront:
    //   • push_back / emplace_back become O(1) with no hidden allocation.
    //   • The memory layout stays contiguous: the CPU prefetcher loves this.
    //   • Iterating over m_particles in update() is a linear scan of a single
    //     cache line stream — no pointer chasing, maximum throughput.
    //
    m_particles.reserve(maxParticles);
}

// ── Public interface ──────────────────────────────────────────────────────────

void ParticleSystem::setEmitter(sf::Vector2f pos) noexcept {
    m_emitter = pos;
}

void ParticleSystem::emit(std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (m_particles.size() < m_maxParticles) {
            // Below max: extend the vector. Because we called reserve() in the
            // constructor, size() < capacity(), so emplace_back is O(1) and
            // performs zero heap allocation.
            m_particles.emplace_back();
            resetParticle(m_particles.back());
        } else {
            // At max: scan for the first dead slot and reuse it.
            // This avoids erase() (O(n) shift) or push_back (would exceed max).
            for (auto& p : m_particles) {
                if (!p.isAlive()) {
                    resetParticle(p);
                    break;
                }
            }
        }
    }
}

void ParticleSystem::update(float dt) {
    for (auto& p : m_particles) {
        if (!p.isAlive()) continue;

        // ── Physics ───────────────────────────────────────────────────────────
        p.velocity.y += kGravity * dt;   // gravity accelerates downward
        p.position   += p.velocity * dt;
        p.lifetime   -= dt;

        // ── Visual decay driven by lifeRatio() ───────────────────────────────
        // lifeRatio() returns [1 → 0] over the particle's lifetime.
        // We drive both size and colour from this single normalised value so
        // the two properties stay in sync without extra bookkeeping.
        const float r = std::max(0.f, p.lifeRatio());

        // Size: shrinks linearly from 4 px to 0 px.
        p.size = 4.f * r;

        // Colour ramp: white (r=1) → yellow (r=0.5) → red+transparent (r=0).
        //   R stays 255 throughout.
        //   G goes from 255 → 0 over the first half of the lifetime.
        //   B goes from 255 → 0 over the second half (white flash at birth).
        //   A fades 255 → 0 so particles dissolve rather than pop.
        const auto u8 = [](float v) noexcept -> sf::Uint8 {
            return static_cast<sf::Uint8>(v * 255.f);
        };
        p.color.r = 255;
        p.color.g = u8(std::min(1.f, r * 2.f));
        p.color.b = u8(r > 0.5f ? (r - 0.5f) * 2.f : 0.f);
        p.color.a = u8(r);
    }

    rebuildVertices();
}

std::size_t ParticleSystem::activeCount() const noexcept {
    std::size_t n = 0;
    for (const auto& p : m_particles) {
        n += static_cast<std::size_t>(p.isAlive());
    }
    return n;
}

std::size_t ParticleSystem::maxCapacity() const noexcept {
    return m_maxParticles;
}

// ── Private helpers ───────────────────────────────────────────────────────────

void ParticleSystem::resetParticle(Particle& p) {
    // Spread angle: full circle with a slight upward bias so the burst
    // looks like a fountain rather than an explosion.
    const float angle = m_angleDist(m_rng) + m_angleBias(m_rng);
    const float speed = m_speedDist(m_rng);

    p.position    = m_emitter;
    p.velocity    = { std::cos(angle) * speed,
                      std::sin(angle) * speed - kUpwardBias };
    p.lifetime    = m_lifeDist(m_rng);
    p.maxLifetime = p.lifetime;
    p.size        = 4.f;
    p.color       = sf::Color::White;
}

void ParticleSystem::rebuildVertices() {
    // Each particle maps to exactly 4 vertices (one quad).
    // resize() is O(n) but avoids per-draw allocation; the buffer is
    // reused across frames because std::vector only grows, never shrinks.
    m_vertices.resize(m_particles.size() * 4);

    for (std::size_t i = 0; i < m_particles.size(); ++i) {
        const Particle& p  = m_particles[i];
        const float     hs = p.size;   // half-side length in pixels
        const sf::Color c  = p.isAlive() ? p.color : sf::Color::Transparent;

        // Quad corners (screen-space, Y grows downward in SFML):
        //   0 ── 1
        //   │    │
        //   3 ── 2
        m_vertices[i * 4 + 0].position = { p.position.x - hs, p.position.y - hs };
        m_vertices[i * 4 + 1].position = { p.position.x + hs, p.position.y - hs };
        m_vertices[i * 4 + 2].position = { p.position.x + hs, p.position.y + hs };
        m_vertices[i * 4 + 3].position = { p.position.x - hs, p.position.y + hs };

        // Assign the same colour to all four corners.
        m_vertices[i * 4 + 0].color =
        m_vertices[i * 4 + 1].color =
        m_vertices[i * 4 + 2].color =
        m_vertices[i * 4 + 3].color = c;
    }
}

void ParticleSystem::draw(sf::RenderTarget& target, sf::RenderStates states) const {
    // This is the single most performance-critical line: one draw call submits
    // the entire vertex array to the GPU, regardless of particle count.
    // Compare this to the naive approach of calling target.draw(circle) for
    // every particle — that would be N separate draw calls with N state
    // changes, easily 10–100x slower for large particle counts.
    target.draw(m_vertices, states);
}
