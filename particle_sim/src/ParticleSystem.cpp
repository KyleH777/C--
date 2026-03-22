#include "ParticleSystem.h"

#include <algorithm>
#include <cmath>

namespace {
    // ── Physics constants ─────────────────────────────────────────────────────
    constexpr float kGravity           = 200.f;   // px/s² downward
    constexpr float kUpwardBias        = 320.f;   // subtracted from spawn vy
    constexpr float kRestitution       = 0.65f;   // velocity retained on bounce [0,1]
    constexpr float kAttractionStrength = 500.f;  // max px/s² toward attractor
    constexpr float kAttractionMinDist =  20.f;   // prevents singularity at origin
}

// ── Construction ──────────────────────────────────────────────────────────────
ParticleSystem::ParticleSystem(std::size_t maxParticles,
                               float boundsW, float boundsH)
    : m_vertices(sf::Quads)
    , m_maxParticles(maxParticles)
    , m_boundsW(boundsW)
    , m_boundsH(boundsH)
{
    // Pre-allocate the entire storage block upfront (see previous session for
    // the full reserve() explanation). Zero heap allocations on the hot-path.
    m_particles.reserve(maxParticles);
}

// ── Public interface ──────────────────────────────────────────────────────────

void ParticleSystem::setEmitter(sf::Vector2f pos) noexcept {
    m_emitter = pos;
}

void ParticleSystem::setBoundaryMode(BoundaryMode mode) noexcept {
    m_boundaryMode = mode;
}

void ParticleSystem::setAttractor(sf::Vector2f pos, bool active) noexcept {
    m_attractorPos    = pos;
    m_attractorActive = active;
}

BoundaryMode ParticleSystem::boundaryMode() const noexcept {
    return m_boundaryMode;
}

void ParticleSystem::emit(std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (m_particles.size() < m_maxParticles) {
            m_particles.emplace_back();
            resetParticle(m_particles.back());
        } else {
            for (auto& p : m_particles) {
                if (!p.isAlive()) { resetParticle(p); break; }
            }
        }
    }
}

// ── Physics pipeline ──────────────────────────────────────────────────────────
//
// Each frame, for every alive particle:
//
//   Step 1 — Gravity
//     Gravity is a constant downward acceleration. We add it to the
//     particle's acceleration accumulator. Because Particle::update() resets
//     acceleration to zero after integration, we must re-add it every frame.
//
//   Step 2 — Mouse Attraction Force (when active)
//     Given the mouse position M and particle position P:
//
//       delta = M - P                 ← vector from particle TO mouse
//       dist  = ||delta||             ← Euclidean length
//       dir   = delta / dist          ← unit vector (normalised direction)
//
//     Normalisation:
//       A raw delta vector has both direction AND magnitude. Dividing by its
//       length extracts only the direction (unit length = 1). This lets us
//       apply a force of exactly kAttractionStrength toward the mouse,
//       independent of how far away it is.
//
//     We clamp the distance at kAttractionMinDist to avoid a singularity:
//     if a particle sits exactly on the mouse, dist = 0 → division by zero.
//     The clamp keeps the force finite and prevents velocity explosions.
//
//     Falloff:  force = dir * (kAttractionStrength / max(dist, minDist))
//       • At large distances the force is weak (particles drift inward).
//       • Close to the cursor the force is strong (particles swirl).
//       • 1/r falloff (not 1/r²) is visually satisfying without being too
//         chaotic at close range.
//
//   Step 3 — Particle::update(dt)   [semi-implicit Euler]
//     velocity += acceleration * dt
//     position += velocity     * dt   (uses new velocity — symplectic form)
//     acceleration = {}               (reset for next frame)
//     lifetime -= dt
//
//   Step 4 — Boundary handling
//     Delegates to applyBoundary() based on m_boundaryMode.
//
//   Step 5 — Visual decay
//     Colour and size are driven by lifeRatio() so they stay in sync.
//
void ParticleSystem::update(float dt) {
    const sf::Vector2f gravityAccel{ 0.f, kGravity };

    for (auto& p : m_particles) {
        if (!p.isAlive()) continue;

        // ── Step 1: Gravity ───────────────────────────────────────────────────
        p.acceleration += gravityAccel;

        // ── Step 2: Mouse Attraction ──────────────────────────────────────────
        if (m_attractorActive) {
            const sf::Vector2f delta = m_attractorPos - p.position;

            // Length of delta (Euclidean distance):
            //   dist = √(dx² + dy²)
            const float dist = std::sqrt(delta.x * delta.x + delta.y * delta.y);

            // Clamp to avoid division by zero at the cursor hotspot.
            const float safeDist = std::max(dist, kAttractionMinDist);

            // Normalise: divide each component by the length.
            //   dir = delta / dist  →  ||dir|| == 1
            const sf::Vector2f dir = delta / safeDist;

            // Scale: stronger close up (1/r falloff), bounded by kAttractionMinDist.
            const float strength = kAttractionStrength / safeDist;

            p.acceleration += dir * strength;
        }

        // ── Step 3: Semi-Implicit Euler ───────────────────────────────────────
        p.update(dt);

        // ── Step 4: Boundary ──────────────────────────────────────────────────
        applyBoundary(p);

        // ── Step 5: Visual decay ──────────────────────────────────────────────
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

    rebuildVertices();
}

std::size_t ParticleSystem::activeCount() const noexcept {
    std::size_t n = 0;
    for (const auto& p : m_particles)
        n += static_cast<std::size_t>(p.isAlive());
    return n;
}

std::size_t ParticleSystem::maxCapacity() const noexcept { return m_maxParticles; }

// ── Private helpers ───────────────────────────────────────────────────────────

void ParticleSystem::resetParticle(Particle& p) {
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
}

// ── Boundary: Wrap vs Bounce ──────────────────────────────────────────────────
//
//  WRAP (toroidal):
//    The screen is treated like a torus — leaving the right edge re-enters
//    from the left. No velocity change: particles keep their momentum.
//
//    Implementation: simple modular arithmetic.
//      x < 0     →  x += W
//      x > W     →  x -= W
//
//    Edge case: a particle moving very fast could jump past the entire screen
//    in one frame (tunnelling). For a visual sim this is acceptable, but in a
//    physics sim you would loop until x ∈ [0, W].
//
//  BOUNCE (reflection):
//    The screen is a closed box. When a particle exits, we:
//      1. Reflect its position back inside (prevents sticking to walls).
//      2. Flip the relevant velocity component (reflection).
//      3. Multiply by kRestitution < 1 (energy loss per impact).
//
//    Position reflection for left wall:
//      before:  x = -3         (3 px outside)
//      after:   x = 3          (3 px inside)  →  x = -x  (while x < 0)
//
//    Position reflection for right wall (W = 1280):
//      before:  x = 1283       (3 px outside)
//      after:   x = 1277       →  x = 2*W - x
//
//    Velocity reflection:
//      vx = -vx * kRestitution
//      If restitution = 1.0: perfectly elastic (energy conserved).
//      If restitution = 0.0: perfectly inelastic (stops dead at wall).
//      We use 0.65: particles lose ~35% energy per bounce — feels natural.
//
void ParticleSystem::applyBoundary(Particle& p) const noexcept {
    switch (m_boundaryMode) {

    case BoundaryMode::Wrap:
        if (p.position.x < 0.f)        p.position.x += m_boundsW;
        else if (p.position.x > m_boundsW) p.position.x -= m_boundsW;
        if (p.position.y < 0.f)        p.position.y += m_boundsH;
        else if (p.position.y > m_boundsH) p.position.y -= m_boundsH;
        break;

    case BoundaryMode::Bounce:
        // Left wall
        if (p.position.x < 0.f) {
            p.position.x  = -p.position.x;
            p.velocity.x  = std::abs(p.velocity.x) * kRestitution;
        }
        // Right wall
        else if (p.position.x > m_boundsW) {
            p.position.x  = 2.f * m_boundsW - p.position.x;
            p.velocity.x  = -std::abs(p.velocity.x) * kRestitution;
        }
        // Top wall
        if (p.position.y < 0.f) {
            p.position.y  = -p.position.y;
            p.velocity.y  = std::abs(p.velocity.y) * kRestitution;
        }
        // Bottom wall
        else if (p.position.y > m_boundsH) {
            p.position.y  = 2.f * m_boundsH - p.position.y;
            p.velocity.y  = -std::abs(p.velocity.y) * kRestitution;
        }
        break;

    case BoundaryMode::None:
    default:
        break;
    }
}

void ParticleSystem::rebuildVertices() {
    m_vertices.resize(m_particles.size() * 4);

    for (std::size_t i = 0; i < m_particles.size(); ++i) {
        const Particle& p  = m_particles[i];
        const float     hs = p.size;
        const sf::Color c  = p.isAlive() ? p.color : sf::Color::Transparent;

        m_vertices[i * 4 + 0].position = { p.position.x - hs, p.position.y - hs };
        m_vertices[i * 4 + 1].position = { p.position.x + hs, p.position.y - hs };
        m_vertices[i * 4 + 2].position = { p.position.x + hs, p.position.y + hs };
        m_vertices[i * 4 + 3].position = { p.position.x - hs, p.position.y + hs };

        m_vertices[i * 4 + 0].color =
        m_vertices[i * 4 + 1].color =
        m_vertices[i * 4 + 2].color =
        m_vertices[i * 4 + 3].color = c;
    }
}

void ParticleSystem::draw(sf::RenderTarget& target, sf::RenderStates states) const {
    target.draw(m_vertices, states);
}
