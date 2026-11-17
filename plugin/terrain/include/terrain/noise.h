
#pragma once

#include "terrain/types.h"

// Deterministic 2D noise for terrain nodes. Header-only, double precision (world coordinates of large maps stay exact),
// lattice hashed with a seed (no permutation tables: any seed, no repetition). Results depend only on (position, seed),
// never on the evaluated region.

namespace GLT::terrain::noise {

    // TYPES ===========================================================================================================

    enum class basis : i32 { perlin = 0, simplex, value };

    enum class distance_metric : i32 { euclidean = 0, manhattan, chebyshev };

    struct worley_result {

        f64                                             f1 = 0.0;       // distance to the nearest feature point
        f64                                             f2 = 0.0;       // distance to the second nearest
        f64                                             cell_value = 0.0;   // [0, 1) random value of the nearest cell
    };

    // FUNCTION DECLARATION ============================================================================================

    namespace detail {

        [[nodiscard]] inline u64 lattice_hash(i64 x, i64 y, u64 seed) noexcept {
            return hash_combine(hash_combine(seed, static_cast<u64>(x)), static_cast<u64>(y));
        }

        // [0, 1)
        [[nodiscard]] inline f64 unit(u64 h) noexcept { return static_cast<f64>(h >> 11) * (1.0 / 9007199254740992.0); }

        [[nodiscard]] inline f64 fade(f64 t) noexcept { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

        [[nodiscard]] inline glm::dvec2 gradient(u64 h) noexcept {
            constexpr f64 d = 0.70710678118654752;
            static constexpr f64 table_x[8] = { 1.0, -1.0, 0.0, 0.0, d, -d, d, -d };
            static constexpr f64 table_y[8] = { 0.0, 0.0, 1.0, -1.0, d, d, -d, -d };
            return glm::dvec2(table_x[h & 7u], table_y[h & 7u]);
        }

        [[nodiscard]] inline i64 floor_i64(f64 v) noexcept { return static_cast<i64>(std::floor(v)); }

    }


    // Value noise, [-1, 1]
    [[nodiscard]] inline f64 value(glm::dvec2 p, u64 seed) noexcept {

        const i64 ix = detail::floor_i64(p.x), iy = detail::floor_i64(p.y);
        const f64 fx = p.x - static_cast<f64>(ix), fy = p.y - static_cast<f64>(iy);
        const f64 u = detail::fade(fx), v = detail::fade(fy);
        auto corner = [&](i64 dx, i64 dy) { return detail::unit(detail::lattice_hash(ix + dx, iy + dy, seed)) * 2.0 - 1.0; };
        const f64 a = corner(0, 0), b = corner(1, 0), c = corner(0, 1), d = corner(1, 1);
        return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v;
    }


    // Gradient (Perlin) noise, about [-1, 1]
    [[nodiscard]] inline f64 perlin(glm::dvec2 p, u64 seed) noexcept {

        const i64 ix = detail::floor_i64(p.x), iy = detail::floor_i64(p.y);
        const f64 fx = p.x - static_cast<f64>(ix), fy = p.y - static_cast<f64>(iy);
        auto dot_corner = [&](i64 dx, i64 dy) {
            const glm::dvec2 g = detail::gradient(detail::lattice_hash(ix + dx, iy + dy, seed));
            return g.x * (fx - static_cast<f64>(dx)) + g.y * (fy - static_cast<f64>(dy));
        };
        const f64 u = detail::fade(fx), v = detail::fade(fy);
        const f64 x0 = dot_corner(0, 0) + (dot_corner(1, 0) - dot_corner(0, 0)) * u;
        const f64 x1 = dot_corner(0, 1) + (dot_corner(1, 1) - dot_corner(0, 1)) * u;
        return (x0 + (x1 - x0) * v) * 1.41421356237;
    }


    // Simplex noise, about [-1, 1]
    [[nodiscard]] inline f64 simplex(glm::dvec2 p, u64 seed) noexcept {

        constexpr f64 F2 = 0.36602540378443864676;      // (sqrt(3) - 1) / 2
        constexpr f64 G2 = 0.21132486540518711775;      // (3 - sqrt(3)) / 6

        const f64 s = (p.x + p.y) * F2;
        const i64 i = detail::floor_i64(p.x + s), j = detail::floor_i64(p.y + s);
        const f64 t = static_cast<f64>(i + j) * G2;
        const f64 x0 = p.x - (static_cast<f64>(i) - t), y0 = p.y - (static_cast<f64>(j) - t);
        const i64 i1 = x0 > y0 ? 1 : 0, j1 = x0 > y0 ? 0 : 1;
        const f64 x1 = x0 - static_cast<f64>(i1) + G2, y1 = y0 - static_cast<f64>(j1) + G2;
        const f64 x2 = x0 - 1.0 + 2.0 * G2, y2 = y0 - 1.0 + 2.0 * G2;

        auto contribution = [&](f64 x, f64 y, i64 ci, i64 cj) {
            f64 a = 0.5 - x * x - y * y;
            if (a <= 0.0)
                return 0.0;
            a *= a;
            const glm::dvec2 g = detail::gradient(detail::lattice_hash(ci, cj, seed));
            return a * a * (g.x * x + g.y * y);
        };
        return 70.0 * (contribution(x0, y0, i, j) + contribution(x1, y1, i + i1, j + j1) + contribution(x2, y2, i + 1, j + 1));
    }


    [[nodiscard]] inline f64 evaluate(basis b, glm::dvec2 p, u64 seed) noexcept {
        switch (b) {
            case basis::simplex:    return simplex(p, seed);
            case basis::value:      return value(p, seed);
            case basis::perlin:
            default:                return perlin(p, seed);
        }
    }


    // Fractal sum of [octaves] layers, normalized to about [-1, 1]
    [[nodiscard]] inline f64 fbm(basis b, glm::dvec2 p, u64 seed, i32 octaves, f64 lacunarity, f64 gain) noexcept {

        f64 sum = 0.0, amplitude = 1.0, norm = 0.0;
        for (i32 o = 0; o < octaves; ++o) {
            sum += amplitude * evaluate(b, p, hash_combine(seed, static_cast<u64>(o)));
            norm += amplitude;
            amplitude *= gain;
            p *= lacunarity;
        }
        return norm > 0.0 ? sum / norm : 0.0;
    }


    // Ridged multifractal, [0, 1]: sharp crests where the basis crosses zero
    [[nodiscard]] inline f64 ridged(basis b, glm::dvec2 p, u64 seed, i32 octaves, f64 lacunarity, f64 gain) noexcept {

        f64 sum = 0.0, amplitude = 1.0, norm = 0.0, weight = 1.0;
        for (i32 o = 0; o < octaves; ++o) {
            f64 n = 1.0 - std::abs(evaluate(b, p, hash_combine(seed, static_cast<u64>(o))));
            n *= n;
            n *= weight;
            weight = std::clamp(n * 2.0, 0.0, 1.0);     // later octaves only add detail on the ridges
            sum += amplitude * n;
            norm += amplitude;
            amplitude *= gain;
            p *= lacunarity;
        }
        return norm > 0.0 ? sum / norm : 0.0;
    }


    // Billow, [0, 1]: rounded hills (absolute value of each octave)
    [[nodiscard]] inline f64 billow(basis b, glm::dvec2 p, u64 seed, i32 octaves, f64 lacunarity, f64 gain) noexcept {

        f64 sum = 0.0, amplitude = 1.0, norm = 0.0;
        for (i32 o = 0; o < octaves; ++o) {
            sum += amplitude * std::abs(evaluate(b, p, hash_combine(seed, static_cast<u64>(o))));
            norm += amplitude;
            amplitude *= gain;
            p *= lacunarity;
        }
        return norm > 0.0 ? sum / norm : 0.0;
    }


    // Swiss turbulence (de Carpentier): octaves displaced by the derivative of the sum so far, eroded-looking ridges.
    // Derivatives by central differences, about [-1, 1]
    [[nodiscard]] inline f64 swiss(basis b, glm::dvec2 p, u64 seed, i32 octaves, f64 lacunarity, f64 gain, f64 warp) noexcept {

        constexpr f64 h = 1e-3;
        f64 sum = 0.0, amplitude = 1.0, norm = 0.0;
        glm::dvec2 derivative(0.0);
        for (i32 o = 0; o < octaves; ++o) {
            const u64 s = hash_combine(seed, static_cast<u64>(o));
            const glm::dvec2 q = p + warp * derivative;
            const f64 n = 1.0 - std::abs(evaluate(b, q, s));
            const f64 nx = 1.0 - std::abs(evaluate(b, q + glm::dvec2(h, 0.0), s));
            const f64 ny = 1.0 - std::abs(evaluate(b, q + glm::dvec2(0.0, h), s));
            sum += amplitude * n;
            norm += amplitude;
            derivative += amplitude * glm::dvec2(nx - n, ny - n) / h * -n;
            amplitude *= gain * std::clamp(sum / std::max(norm, 1e-9), 0.0, 1.0);
            p *= lacunarity;
        }
        return norm > 0.0 ? (sum / norm) * 2.0 - 1.0 : 0.0;
    }


    // Worley / Voronoi: distances to the two nearest feature points (one jittered point per unit cell)
    [[nodiscard]] inline worley_result worley(glm::dvec2 p, u64 seed, f64 jitter, distance_metric metric) noexcept {

        const i64 ix = detail::floor_i64(p.x), iy = detail::floor_i64(p.y);
        worley_result r{ 1e30, 1e30, 0.0 };
        for (i64 dy = -1; dy <= 1; ++dy)
            for (i64 dx = -1; dx <= 1; ++dx) {
                const i64 cx = ix + dx, cy = iy + dy;
                const u64 h = detail::lattice_hash(cx, cy, seed);
                const glm::dvec2 feature(static_cast<f64>(cx) + 0.5 + (detail::unit(h) - 0.5) * jitter,
                                         static_cast<f64>(cy) + 0.5 + (detail::unit(hash_mix(h)) - 0.5) * jitter);
                const glm::dvec2 d = feature - p;
                f64 dist = 0.0;
                switch (metric) {
                    case distance_metric::manhattan:    dist = std::abs(d.x) + std::abs(d.y); break;
                    case distance_metric::chebyshev:    dist = std::max(std::abs(d.x), std::abs(d.y)); break;
                    case distance_metric::euclidean:
                    default:                            dist = std::sqrt(d.x * d.x + d.y * d.y); break;
                }
                if (dist < r.f1) {
                    r.f2 = r.f1;
                    r.f1 = dist;
                    r.cell_value = detail::unit(hash_mix(h ^ 0x5bd1e995ull));
                } else if (dist < r.f2) {
                    r.f2 = dist;
                }
            }
        return r;
    }

}
