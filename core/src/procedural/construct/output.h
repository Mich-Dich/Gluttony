#pragma once

#include "procedural/construct/policy.h"

// Procedural construction: products (what realizers emit) and outputs (what products become).
// A construct may have any number of outputs at once, each with its own topology and purpose.
// See documentation/procedural_construction_interface.md §8, §9



namespace GLT::construct {

    // TYPES ===========================================================================================================

    struct mesh_product {

        geometry::mesh                                  mesh{};             // in the element's frame
        f32                                             smooth_angle_degrees = 40.0f;
    };

    struct instance_product {

        std::string                                     asset{};            // asset reference (content path or uuid string)
        std::vector<frame>                              placements{};       // in the element's frame
    };

    struct marker_product {

        frame                                           at{};               // in the element's frame
    };

    using product_data = std::variant<mesh_product, instance_product, marker_product>;


    // @brief One thing a realizer emitted for one element
    struct product {

        element_path                                    element{};
        kind_id                                         element_kind{};
        std::string                                     name{};             // "web", "flange", "post", "seat"
        frame                                           placement{};        // the element's placement when it was realized
        attribute_set                                   tags{};             // element attributes + what the realizer added
        product_data                                    data{};
        u32                                             stage = 0;          // the element's stage (0: none)
    };


    enum class topology : u8 {

        as_realized,          // one part per product, as emitted
        shells_per_element,   // one part per element (its mesh products together)
        merged_shells,        // everything in one part
        watertight,           // boolean union into one closed 2-manifold (needs a boolean backend: falls back to merged_shells)
        open_surface,         // single-sided surfaces (leaf cards, decals)
    };

    enum class output_purpose : u8 { render, collision, physics_proxy, navigation, occlusion, export_only };


    struct product_filter {

        std::vector<std::string>                        names{};            // product names (empty: any)
        std::vector<kind_id>                            kinds{};            // element kinds (empty: any)
        std::vector<attribute_id>                       require_tags{};     // the product's tags must contain these
        std::vector<attribute_id>                       exclude_tags{};

        [[nodiscard]] bool matches(const product& p) const;
    };


    // @brief One output of a construct. The core.outputs hook returns the list
    struct output_desc {

        std::string                                     name{ "render" };
        output_purpose                                  purpose = output_purpose::render;
        product_filter                                  include{};
        topology                                        topo = topology::merged_shells;
        bool                                            lods = true;
    };


    struct output_group {

        name_id                                         material{};         // core.material of the faces
        geometry::triangle_mesh                         mesh{};             // construct space (relative to the construct origin)
    };

    struct output_part {

        element_path                                    element{};          // empty for merged parts
        std::vector<output_group>                       groups{};
    };

    struct output_instance { std::string asset{}; glm::mat4 transform{ 1.0f }; element_path element{}; };
    struct output_marker   { std::string name{}; frame at{}; element_path element{}; attribute_set tags{}; };


    // @brief What one output became
    struct output_result {

        output_desc                                     desc{};
        std::vector<output_part>                        parts{};
        std::vector<output_instance>                    instances{};
        std::vector<output_marker>                      markers{};

        [[nodiscard]] u32 triangle_count() const noexcept;
        [[nodiscard]] u64 hash() const noexcept;
    };


    // @brief Hook core.outputs: every matching policy adds outputs (composition: all)
    struct output_query {

        std::span<const std::string>                    packs{};            // the construct's packs
    };

    class i_outputs {
    public:
        virtual ~i_outputs() = default;
        virtual void outputs(const output_query& query, std::vector<output_desc>& out) const = 0;
    };

    struct outputs_hook {
        using interface = i_outputs;
        static constexpr std::string_view name = "core.outputs";
        static constexpr composition default_composition = composition::all;
    };


    // FUNCTION DECLARATION ============================================================================================

    // Triangles of a mesh product in the construct's output space (relative to [origin]). Empty for other products
    [[nodiscard]] geometry::triangle_mesh product_triangles(const product& p, const glm::dvec3& origin);

    // Mesh products merged into one part (one group per material), relative to [origin]
    [[nodiscard]] output_part merge_products(std::span<const product* const> products, const glm::dvec3& origin);

}
