#pragma once

#include "procedural/construct/base.h"

// Procedural construction: identities, paths, frames, attribute values, region selectors, diagnostics, random numbers.
// See documentation/procedural_construction_interface.md §1



namespace GLT::construct {

    // FUNCTION DECLARATION ============================================================================================

    // 64-bit FNV-1a. Stable across runs, compilers and modules: the basis of name ids, path hashes and seeds
    [[nodiscard]] constexpr u64 fnv1a_64(std::string_view text, u64 hash = 0xcbf29ce484222325ull) noexcept {
        for (const char c : text) {
            hash ^= static_cast<u8>(c);
            hash *= 0x100000001b3ull;
        }
        return hash;
    }

    // Mixes two hashes (order matters)
    [[nodiscard]] constexpr u64 hash_combine(u64 a, u64 b) noexcept {
        a ^= b + 0x9e3779b97f4a7c15ull + (a << 12) + (a >> 4);
        return a;
    }

    // TYPES ===========================================================================================================

    // @brief An interned name: "ship.rib", "core.radius", "primary"
    //
    // Compared by its 64-bit hash, so ids made in different modules (the core, every plugin, the tests) are equal
    // without sharing a table. The text lives in a process-wide table of the module that interned it first and stays
    // valid for the lifetime of that module.
    struct name_id {

        u64                                             hash = 0;
        const char*                                     text = nullptr;

        [[nodiscard]] constexpr bool empty() const noexcept { return hash == 0; }
        [[nodiscard]] std::string_view str() const noexcept { return text ? std::string_view(text) : std::string_view(); }

        [[nodiscard]] constexpr bool operator==(const name_id& other) const noexcept { return hash == other.hash; }
        [[nodiscard]] constexpr auto operator<=>(const name_id& other) const noexcept { return hash <=> other.hash; }
    };

    // Thread-safe. The empty string gives the empty id
    [[nodiscard]] name_id intern(std::string_view name);

    using kind_id                                       = name_id;
    using role_id                                       = name_id;
    using attribute_id                                  = name_id;
    using relation_id                                   = name_id;
    using pass_id                                       = name_id;


    struct path_segment {

        name_id                                         name{};
        i64                                             index = -1;         // -1: no index

        [[nodiscard]] bool operator==(const path_segment&) const noexcept = default;
    };


    struct parse_error { std::string message; };


    // @brief Stable identity of an element: the path of what created it. "hull/frames/station[12]/rib"
    //
    // Seeds, caches and overrides key on it. Segments are (name, index); the index is chosen by the emitting code, never
    // by evaluation order
    class element_path {
    public:

        element_path() = default;

        [[nodiscard]] static element_path root(std::string_view name, i64 index = -1);
        [[nodiscard]] element_path child(std::string_view name, i64 index = -1) const;
        [[nodiscard]] element_path child(name_id name, i64 index = -1) const;

        [[nodiscard]] std::span<const path_segment> segments() const noexcept { return m_segments; }
        [[nodiscard]] bool empty() const noexcept { return m_segments.empty(); }
        [[nodiscard]] u64 hash() const noexcept { return m_hash; }
        [[nodiscard]] element_path parent() const;
        [[nodiscard]] bool is_prefix_of(const element_path& other) const noexcept;

        [[nodiscard]] std::string to_string() const;
        [[nodiscard]] static std::expected<element_path, parse_error> parse(std::string_view text);

        [[nodiscard]] bool operator==(const element_path& other) const noexcept { return m_hash == other.m_hash && m_segments == other.m_segments; }
        // Total order, the same in every run: by segment text (not by hash), then index
        [[nodiscard]] std::strong_ordering operator<=>(const element_path& other) const noexcept;

    private:

        void rehash() noexcept;

        std::vector<path_segment>                       m_segments{};
        u64                                             m_hash = 0;
    };


    // @brief Per-evaluation handle (index into the evaluation's element table). NOT stable: store paths, not handles
    struct element_handle {

        u32                                             index = ~0u;

        [[nodiscard]] constexpr bool valid() const noexcept { return index != ~0u; }
        [[nodiscard]] constexpr bool operator==(const element_handle&) const noexcept = default;
    };


    // @brief A placement: f64 origin, f32 rotation and scale. Geometry inside an element is f32, relative to it
    struct frame {

        glm::dvec3                                      origin{ 0.0 };
        glm::quat                                       rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3                                       scale{ 1.0f };

        // Local point -> world (f64)
        [[nodiscard]] glm::dvec3 to_world(const glm::vec3& local) const noexcept {
            return origin + glm::dvec3(rotation * (local * scale));
        }
        [[nodiscard]] glm::vec3 to_world_direction(const glm::vec3& local) const noexcept {
            return glm::normalize(rotation * (local / scale));
        }
        // Local -> space of [base] (f32): used to bring element geometry into the construct's output space
        [[nodiscard]] glm::mat4 matrix_relative_to(const glm::dvec3& base) const noexcept;
        // [child] expressed in this frame's parent space (child is local to this frame)
        [[nodiscard]] frame compose(const frame& child) const noexcept;
    };


    // @brief Attribute values. Tags are attributes too (bool or enum typed)
    using attribute_value = std::variant<std::monostate, bool, i32, i64, f32, f64, glm::vec2, glm::vec3, glm::vec4,
                                         glm::quat, name_id, std::string>;

    // Numeric view of a value (bool / ints / floats; vectors: their first component). 0 for everything else
    [[nodiscard]] f64 as_number(const attribute_value& value) noexcept;
    [[nodiscard]] std::string to_string(const attribute_value& value);


    // @brief Which part of an element something applies to
    struct whole_element { [[nodiscard]] bool operator==(const whole_element&) const noexcept = default; };
    struct face_group    { name_id group{};  [[nodiscard]] bool operator==(const face_group&) const noexcept = default; };
    struct curve_span    { f64 t0 = 0.0, t1 = 1.0; [[nodiscard]] bool operator==(const curve_span&) const noexcept = default; };
    struct point_index   { u32 index = 0; [[nodiscard]] bool operator==(const point_index&) const noexcept = default; };
    using region_selector = std::variant<whole_element, face_group, curve_span, point_index>;


    // @brief Problems reported by pack code or the evaluator
    enum class severity : u8 { info, warning, error };

    struct diagnostic {

        construct::severity                             level = severity::info;
        element_path                                    element{};          // empty: the construct as a whole
        std::string                                     source{};           // stage / production / realizer name
        std::string                                     message{};
    };


    // @brief Deterministic random numbers (splitmix64). Seeded from paths, never from time
    class rng {
    public:

        explicit constexpr rng(u64 seed) noexcept : m_state(seed) {}

        [[nodiscard]] constexpr u64 next() noexcept {
            u64 z = (m_state += 0x9e3779b97f4a7c15ull);
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            return z ^ (z >> 31);
        }
        // [0, 1)
        [[nodiscard]] constexpr f64 uniform() noexcept { return static_cast<f64>(next() >> 11) * 0x1.0p-53; }
        [[nodiscard]] constexpr f64 range(f64 lo, f64 hi) noexcept { return lo + (hi - lo) * uniform(); }
        // [lo, hi]
        [[nodiscard]] constexpr i64 integer(i64 lo, i64 hi) noexcept {
            return lo + static_cast<i64>(next() % static_cast<u64>(hi - lo + 1));
        }

    private:

        u64                                             m_state;
    };

}


template<>
struct std::hash<GLT::construct::name_id> {
    [[nodiscard]] size_t operator()(const GLT::construct::name_id& id) const noexcept { return static_cast<size_t>(id.hash); }
};

template<>
struct std::hash<GLT::construct::element_path> {
    [[nodiscard]] size_t operator()(const GLT::construct::element_path& p) const noexcept { return static_cast<size_t>(p.hash()); }
};
