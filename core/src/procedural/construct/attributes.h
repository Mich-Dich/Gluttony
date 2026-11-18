#pragma once

#include "procedural/construct/types.h"

// Procedural construction: attribute descriptors, tag schemas and per-element attribute sets.
// See documentation/procedural_construction_interface.md §2



namespace GLT::construct {

    // TYPES ===========================================================================================================

    // @brief Where an attribute may be stored
    enum class attribute_domain : u16 {

        none                                            = 0,
        element                                         = 1 << 0,       // the element as a whole
        face                                            = 1 << 1,       // per face / face group
        edge                                            = 1 << 2,
        vertex                                          = 1 << 3,
        corner                                          = 1 << 4,       // per face corner (UVs, split normals)
        curve                                           = 1 << 5,       // along a curve, per control point, interpolated by parameter
        point                                           = 1 << 6,       // per point of a point set
        all                                             = 0x7f,
    };

    [[nodiscard]] constexpr attribute_domain operator|(attribute_domain a, attribute_domain b) noexcept {
        return static_cast<attribute_domain>(static_cast<u16>(a) | static_cast<u16>(b));
    }
    [[nodiscard]] constexpr attribute_domain operator&(attribute_domain a, attribute_domain b) noexcept {
        return static_cast<attribute_domain>(static_cast<u16>(a) & static_cast<u16>(b));
    }
    [[nodiscard]] constexpr bool has_domain(attribute_domain set, attribute_domain d) noexcept {
        return (static_cast<u16>(set) & static_cast<u16>(d)) != 0;
    }


    // @brief Runtime descriptor of one attribute (what reflection produces, what builders produce by hand)
    struct attribute_desc {

        attribute_id                                    id{};
        std::string                                     display{};
        std::string                                     tooltip{};
        attribute_value                                 default_value{};
        attribute_domain                                domains = attribute_domain::element;
        name_id                                         propagation{};      // mode name ("copy", "interpolate", ...); empty: pack default
        f32                                             rate = 1.0f;        // parameter of rate-based modes (decay)
        f32                                             min = 0.0f;
        f32                                             max = 0.0f;         // min == max: unbounded
        bool                                            user_editable = true;
        bool                                            affects_topology = false;   // editor: re-run Expand, not only Realize
        std::vector<name_id>                            enum_values{};      // closed set for name_id attributes
    };


    struct tag_schema_desc {

        std::string                                     name{};             // attribute prefix: "ship"
        std::vector<attribute_desc>                     attributes{};
    };


    // @brief Builder for schemas only known at runtime (data-driven packs)
    class tag_schema_builder {
    public:

        explicit tag_schema_builder(std::string name) { m_desc.name = std::move(name); }

        // [member] without the schema prefix: add("armor", ...) declares "ship.armor"
        tag_schema_builder& add(std::string_view member, attribute_value default_value, attribute_domain domains = attribute_domain::element,
                                std::string_view propagation = {}) {
            attribute_desc d{};
            d.id = intern(std::format("{}.{}", m_desc.name, member));
            d.display = std::string(member);
            d.default_value = std::move(default_value);
            d.domains = domains;
            d.propagation = intern(propagation);
            m_desc.attributes.push_back(std::move(d));
            return *this;
        }

        tag_schema_builder& add(attribute_desc desc) { m_desc.attributes.push_back(std::move(desc)); return *this; }

        [[nodiscard]] tag_schema_desc build() { return std::move(m_desc); }

    private:

        tag_schema_desc                                 m_desc{};
    };


    // @brief Attribute values of one element, optionally per region (face group, curve span, point)
    //
    // Lookup: an entry for exactly the asked region first, then the whole-element entry. Small and flat on purpose:
    // elements carry few attributes; mesh-domain data lives in geometry::mesh columns
    class attribute_set {
    public:

        struct entry {

            attribute_id                                id{};
            region_selector                             region{};
            attribute_value                             value{};
        };

        void set(attribute_id id, attribute_value value, const region_selector& region = whole_element{});
        void erase(attribute_id id, const region_selector& region = whole_element{});

        // Exact region, then whole element. Null when neither is set
        [[nodiscard]] const attribute_value* find(attribute_id id, const region_selector& region = whole_element{}) const noexcept;
        [[nodiscard]] bool has(attribute_id id) const noexcept;
        [[nodiscard]] std::span<const entry> entries() const noexcept { return m_entries; }
        [[nodiscard]] bool empty() const noexcept { return m_entries.empty(); }

        // Hash of every entry (cache keys, determinism checks)
        [[nodiscard]] u64 hash() const noexcept;

    private:

        std::vector<entry>                              m_entries{};
    };

    // Hash of one value (stable across runs)
    [[nodiscard]] u64 hash_value(const attribute_value& value) noexcept;
    [[nodiscard]] u64 hash_region(const region_selector& region) noexcept;

}
