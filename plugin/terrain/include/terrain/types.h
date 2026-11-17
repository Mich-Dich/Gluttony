
#pragma once

#include <cmath>

// Terrain node SDK: regions and fields. Header-only, no engine dependencies besides the pch basics (glm, fixed-width
// types), so node packs can include it freely.

namespace GLT::terrain {

    // CONSTANTS =======================================================================================================

    // Largest region a single evaluation accepts (samples per side). Protects against runaway apron growth
    inline constexpr u32                                MAX_REGION_SIDE = 16384;

    // TYPES ===========================================================================================================

    // @brief A rectangle of the world's sample lattice
    //
    // Samples sit on an integer lattice: sample (gx, gy) is at world position (gx * spacing, gy * spacing). Everything is
    // indexed by these GLOBAL lattice coordinates, never by array offsets, so the same world point gets bit-identical
    // inputs no matter which region (chunk, map tile, whole domain) it is evaluated in. That is what makes chunks seamless.
    // World y of the lattice maps to world z (the terrain plane is xz)
    struct region {

        i64                                             x0 = 0;         // first sample, lattice coordinates
        i64                                             y0 = 0;
        u32                                             width = 0;      // samples
        u32                                             height = 0;
        f64                                             spacing = 1.0;  // world units between samples

        [[nodiscard]] constexpr i64 x1() const noexcept { return x0 + static_cast<i64>(width); }    // one past the last
        [[nodiscard]] constexpr i64 y1() const noexcept { return y0 + static_cast<i64>(height); }
        [[nodiscard]] constexpr u64 sample_count() const noexcept { return static_cast<u64>(width) * height; }
        [[nodiscard]] constexpr bool empty() const noexcept { return width == 0 || height == 0; }

        [[nodiscard]] constexpr bool contains(i64 gx, i64 gy) const noexcept { return gx >= x0 && gy >= y0 && gx < x1() && gy < y1(); }

        [[nodiscard]] constexpr bool contains(const region& other) const noexcept {
            return other.x0 >= x0 && other.y0 >= y0 && other.x1() <= x1() && other.y1() <= y1();
        }

        // World position of a lattice sample. Exact for a given (gx, gy, spacing): the basis of seamless chunks
        [[nodiscard]] constexpr glm::dvec2 position(i64 gx, i64 gy) const noexcept {
            return glm::dvec2(static_cast<f64>(gx) * spacing, static_cast<f64>(gy) * spacing);
        }

        // Grown by [samples] on every side
        [[nodiscard]] constexpr region expanded(u32 samples) const noexcept {
            region r = *this;
            r.x0 -= samples;
            r.y0 -= samples;
            r.width += 2 * samples;
            r.height += 2 * samples;
            return r;
        }

        // Smallest region containing both (same spacing assumed)
        [[nodiscard]] constexpr region united(const region& other) const noexcept {
            if (empty())
                return other;
            if (other.empty())
                return *this;
            region r{};
            r.spacing = spacing;
            r.x0 = std::min(x0, other.x0);
            r.y0 = std::min(y0, other.y0);
            r.width = static_cast<u32>(std::max(x1(), other.x1()) - r.x0);
            r.height = static_cast<u32>(std::max(y1(), other.y1()) - r.y0);
            return r;
        }

        [[nodiscard]] constexpr bool operator==(const region&) const noexcept = default;
    };


    // @brief A grid of f32 samples over a region (heightfield, mask, constant: same storage)
    //
    // Accessed by global lattice coordinates. Reads outside the region clamp to the border (a node that reads further than
    // its declared apron gets clamped values, never garbage)
    class field {
    public:

        field() = default;
        explicit field(const region& area, f32 fill = 0.0f) { reset(area, fill); }

        void reset(const region& area, f32 fill = 0.0f) {
            m_area = area;
            m_values.assign(static_cast<size_t>(area.sample_count()), fill);
        }

        [[nodiscard]] const region& area() const noexcept { return m_area; }
        [[nodiscard]] std::span<f32> values() noexcept { return m_values; }
        [[nodiscard]] std::span<const f32> values() const noexcept { return m_values; }

        [[nodiscard]] f32& at(i64 gx, i64 gy) noexcept { return m_values[index(gx, gy)]; }
        [[nodiscard]] f32 at(i64 gx, i64 gy) const noexcept { return m_values[index(gx, gy)]; }

        // Bilinear read at a world position (warp / displacement nodes). Clamped to the region
        [[nodiscard]] f32 sample(glm::dvec2 world) const noexcept {
            const f64 fx = world.x / m_area.spacing;
            const f64 fy = world.y / m_area.spacing;
            const f64 bx = std::floor(fx);
            const f64 by = std::floor(fy);
            const i64 ix = static_cast<i64>(bx);
            const i64 iy = static_cast<i64>(by);
            const f32 tx = static_cast<f32>(fx - bx);
            const f32 ty = static_cast<f32>(fy - by);
            const f32 a = at(ix, iy), b = at(ix + 1, iy), c = at(ix, iy + 1), d = at(ix + 1, iy + 1);
            return glm::mix(glm::mix(a, b, tx), glm::mix(c, d, tx), ty);
        }

    private:

        [[nodiscard]] size_t index(i64 gx, i64 gy) const noexcept {
            const i64 lx = std::clamp<i64>(gx - m_area.x0, 0, static_cast<i64>(m_area.width) - 1);
            const i64 ly = std::clamp<i64>(gy - m_area.y0, 0, static_cast<i64>(m_area.height) - 1);
            return static_cast<size_t>(ly) * m_area.width + static_cast<size_t>(lx);
        }

        region                                          m_area{};
        std::vector<f32>                                m_values{};
    };

    // FUNCTION DECLARATION ============================================================================================

    // @brief Stable 64-bit mixing (splitmix64 finalizer). Used for seeds, lattice hashes and cache keys
    [[nodiscard]] constexpr u64 hash_mix(u64 x) noexcept {
        x += 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }

    [[nodiscard]] constexpr u64 hash_combine(u64 seed, u64 value) noexcept { return hash_mix(seed ^ (value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2))); }

    [[nodiscard]] constexpr u64 hash_string(std::string_view text, u64 seed = 0xCBF29CE484222325ull) noexcept {
        u64 h = seed;
        for (const char c : text)
            h = (h ^ static_cast<u8>(c)) * 0x100000001B3ull;
        return hash_mix(h);
    }

}
