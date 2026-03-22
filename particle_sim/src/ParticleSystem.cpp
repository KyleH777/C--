// ── Parallel STL ──────────────────────────────────────────────────────────────
// std::execution::par_unseq requires:
//   • GCC/Clang on Linux: link against Intel TBB  (set by CMakeLists.txt)
//   • MSVC on Windows:    ships with the CRT, no extra library needed
//   • AppleClang:         not supported — falls back to sequential
//
// CMakeLists.txt defines PARTICLE_PARALLEL when TBB is available or MSVC is
// used, so this file never calls <execution> on a toolchain that lacks it.
#ifdef PARTICLE_PARALLEL
#  include <execution>
#  define PAR_POLICY std::execution::par_unseq,
#else
#  define PAR_POLICY   // empty — sequential std::for_each
#endif

#include "ParticleSystem.h"

#include <algorithm>
#include <cmath>
#include <numeric>   // std::iota

namespace {
    constexpr float kGravity            = 200.f;
    constexpr float kUpwardBias         = 320.f;
    constexpr float kRestitution        = 0.65f;
    constexpr float kAttractionStrength = 500.f;
    constexpr float kAttractionMinDist  =  20.f;
}

// ── Construction ──────────────────────────────────────────────────────────────
ParticleSystem::ParticleSystem(std::size_t maxParticles,
                               float boundsW, float boundsH)
    : m_maxParticles(maxParticles)
    , m_boundsW(boundsW)
    , m_boundsH(boundsH)
{
    // ── Layer 1: Pre-allocate both buffers ────────────────────────────────────
    //
    // Particles — same reserve() rationale as before: zero reallocs on
    // the hot-path; contiguous memory for cache-friendly linear scans.
    m_particles.reserve(maxParticles);

    // Vertices — THIS is the key fix for the "sf::VertexArray" question.
    //
    // The old code called m_vertices.resize(m_particles.size() * 4) EVERY
    // frame inside rebuildVertices(). Even though sf::VertexArray only grows,
    // the resize() call itself walks the size/capacity check each frame.
    //
    // By switching from sf::VertexArray to std::vector<sf::Vertex> and
    // calling resize() ONCE here, rebuildVertices() becomes a pure write
    // loop — no length checks, no branching, no allocation ever again.
    //
    // We size it to the full maximum immediately: dead particle slots write
    // transparent quads, which the GPU discards with near-zero cost.
    m_vertices.resize(maxParticles * 4);
}

// ── Public interface ──────────────────────────────────────────────────────────
void ParticleSystem::setEmitter(sf::Vector2f pos)              noexcept { m_emitter = pos; }
void ParticleSystem::setBoundaryMode(BoundaryMode mode)        noexcept { m_boundaryMode = mode; }
void ParticleSystem::setAttractor(sf::Vector2f pos, bool active) noexcept {
    m_attractorPos    = pos;
    m_attractorActive = active;
}
BoundaryMode ParticleSystem::boundaryMode() const noexcept { return m_boundaryMode; }

// O(1): atomic load, not a loop.
std::size_t ParticleSystem::activeCount() const noexcept {
    return m_activeCount.load(std::memory_order_relaxed);
}
std::size_t ParticleSystem::maxCapacity() const noexcept { return m_maxParticles; }

void ParticleSystem::emit(std::size_t count) {
    // emit() is called from the main thread — serial, safe to use m_rng.
    for (std::size_t i = 0; i < count; ++i) {
        if (m_particles.size() < m_maxParticles) {
            m_particles.emplace_back();
            resetParticle(m_particles.back());    // increments m_activeCount
        } else {
            for (auto& p : m_particles) {
                if (!p.isAlive()) { resetParticle(p); break; }  // increments m_activeCount
            }
        }
    }
}

// ── Physics pipeline (parallel) ───────────────────────────────────────────────
//
// std::for_each with par_unseq splits work across all CPU cores. The
// correctness requirement for par_unseq is:
//
//   Each loop iteration must be independent — no shared mutable state
//   that two iterations could race to read AND write simultaneously.
//
// Our loop satisfies this because:
//   • Each particle writes only to its own Particle object.
//   • m_attractorActive, m_attractorPos, m_boundsW/H are read-only in the loop.
//   • m_rng is NOT used in the parallel body (only in resetParticle/emit, serial).
//   • m_activeCount uses fetch_sub(relaxed): atomic, no data race.
//
void ParticleSystem::update(float dt) {
    // Capture loop-invariant values before entering the parallel section.
    // Capturing `this` would work too, but local copies avoid a pointer
    // dereference on every particle when running across multiple cores.
    const sf::Vector2f gravityAccel  { 0.f, kGravity };
    const bool         attracting     = m_attractorActive;
    const sf::Vector2f attractorPos   = m_attractorPos;
    const BoundaryMode boundaryMode   = m_boundaryMode;
    const float        boundsW        = m_boundsW;
    const float        boundsH        = m_boundsH;

    // ── Layer 2: Parallel physics ─────────────────────────────────────────────
    //
    // std::for_each is chosen over a raw for-loop because it accepts an
    // execution policy as its first argument — a single-token change toggles
    // between sequential and parallel without restructuring the loop body.
    //
    //   Sequential:  std::for_each(          begin, end, fn);
    //   Parallel:    std::for_each(par_unseq, begin, end, fn);
    //
    // par_unseq = parallel AND vectorised (SIMD). The runtime (TBB on Linux,
    // ConcRT on Windows) decides how many threads to use based on core count.
    //
    std::for_each(PAR_POLICY
        m_particles.begin(), m_particles.end(),
        [&](Particle& p) {
            if (!p.isAlive()) return;

            // Step 1 — Gravity
            p.acceleration += gravityAccel;

            // Step 2 — Mouse Attraction
            if (attracting) {
                const sf::Vector2f delta    = attractorPos - p.position;
                const float        dist     = std::sqrt(delta.x*delta.x + delta.y*delta.y);
                const float        safeDist = std::max(dist, kAttractionMinDist);
                p.acceleration += (delta / safeDist) * (kAttractionStrength / safeDist);
            }

            // Step 3 — Semi-Implicit Euler (lives in Particle::update)
            const bool wasAlive = p.isAlive();
            p.update(dt);
            // If this step killed the particle, decrement the live counter.
            // fetch_sub with relaxed ordering is sufficient: the counter is only
            // read for display, never used as a synchronisation point.
            if (wasAlive && !p.isAlive())
                m_activeCount.fetch_sub(1, std::memory_order_relaxed);

            // Step 4 — Boundary
            switch (boundaryMode) {
            case BoundaryMode::Wrap:
                if      (p.position.x < 0.f)    p.position.x += boundsW;
                else if (p.position.x > boundsW) p.position.x -= boundsW;
                if      (p.position.y < 0.f)    p.position.y += boundsH;
                else if (p.position.y > boundsH) p.position.y -= boundsH;
                break;
            case BoundaryMode::Bounce:
                if (p.position.x < 0.f) {
                    p.position.x = -p.position.x;
                    p.velocity.x =  std::abs(p.velocity.x) * kRestitution;
                } else if (p.position.x > boundsW) {
                    p.position.x = 2.f * boundsW - p.position.x;
                    p.velocity.x = -std::abs(p.velocity.x) * kRestitution;
                }
                if (p.position.y < 0.f) {
                    p.position.y = -p.position.y;
                    p.velocity.y =  std::abs(p.velocity.y) * kRestitution;
                } else if (p.position.y > boundsH) {
                    p.position.y = 2.f * boundsH - p.position.y;
                    p.velocity.y = -std::abs(p.velocity.y) * kRestitution;
                }
                break;
            case BoundaryMode::None: default: break;
            }

            // Step 5 — Visual decay
            const float r = std::max(0.f, p.lifeRatio());
            p.size = 4.f * r;
            const auto u8 = [](float v) noexcept -> sf::Uint8 {
                return static_cast<sf::Uint8>(v * 255.f);
            };
            p.color.r = 255;
            p.color.g = u8(std::min(1.f, r * 2.f));
            p.color.b = u8(r > 0.5f ? (r - 0.5f) * 2.f : 0.f);
            p.color.a = u8(r);
        }
    );

    rebuildVertices();
}

// ── Private helpers ───────────────────────────────────────────────────────────

void ParticleSystem::resetParticle(Particle& p) {
    const bool wasDead = !p.isAlive();   // guard: only count net new lives

    const float angle = m_angleDist(m_rng) + m_angleBias(m_rng);
    const float speed = m_speedDist(m_rng);
    p.position     = m_emitter;
    p.velocity     = { std::cos(angle) * speed,
                       std::sin(angle) * speed - kUpwardBias };
    p.acceleration = {};
    p.lifetime     = m_lifeDist(m_rng);
    p.maxLifetime  = p.lifetime;
    p.size         = 4.f;
    p.color        = sf::Color::White;

    if (wasDead)
        m_activeCount.fetch_add(1, std::memory_order_relaxed);
}

// ── Layer 3: Batch rendering ──────────────────────────────────────────────────
//
// WHY sf::VertexArray BEATS sf::CircleShape:
//
//   sf::CircleShape approach (naive):
//     for each particle:
//       shape.setPosition(p.position);   // update shape state
//       shape.setFillColor(p.color);
//       window.draw(shape);              // 1 GPU draw call per particle
//                                        // = N state changes per frame
//     At N=1 000 that's 1 000 draw calls. The GPU driver overhead alone
//     stalls the CPU for milliseconds — frame rate collapses.
//
//   VertexArray approach (this code):
//     Build all vertices into one contiguous buffer (rebuildVertices).
//     Call draw() ONCE — one GPU upload, one draw call, zero state changes.
//
//   rebuildVertices() is also parallelised. Each particle i writes to
//   slots [i*4, i*4+4) — no two particles share a slot, so no data race.
//
void ParticleSystem::rebuildVertices() {
    // Buffer was pre-sized to maxParticles*4 in the constructor.
    // We never call resize() here — that is the entire point of pre-sizing.
    const std::size_t n = m_particles.size();

    // ── Layer 2 (again): Parallel vertex build ────────────────────────────────
    // We need an index (i) to address m_vertices[i*4 .. i*4+3].
    // std::for_each iterates over particle VALUES, not indices. The trick:
    // compute i from the particle's address relative to the vector's base.
    // Pointer arithmetic on a contiguous std::vector is well-defined and fast.
    const Particle* base = m_particles.data();

    std::for_each(PAR_POLICY
        m_particles.begin(), m_particles.begin() + static_cast<std::ptrdiff_t>(n),
        [this, base](const Particle& p) {
            const std::size_t i  = static_cast<std::size_t>(&p - base);
            const float       hs = p.size;
            const sf::Color   c  = p.isAlive() ? p.color : sf::Color::Transparent;

            m_vertices[i*4+0] = {{ p.position.x - hs, p.position.y - hs }, c };
            m_vertices[i*4+1] = {{ p.position.x + hs, p.position.y - hs }, c };
            m_vertices[i*4+2] = {{ p.position.x + hs, p.position.y + hs }, c };
            m_vertices[i*4+3] = {{ p.position.x - hs, p.position.y + hs }, c };
        }
    );
}

void ParticleSystem::draw(sf::RenderTarget& target, sf::RenderStates states) const {
    if (m_particles.empty()) return;

    // Raw sf::Vertex* overload: bypasses sf::VertexArray entirely.
    // Arguments: pointer to first vertex, count of vertices to draw, primitive
    // type, render states (transform, shader, texture — all default here).
    //
    // We draw exactly m_particles.size()*4 vertices — only the allocated slots.
    // Dead-particle quads are transparent; the GPU discards them at rasterisation.
    target.draw(m_vertices.data(),
                m_particles.size() * 4,
                sf::Quads,
                states);
}
