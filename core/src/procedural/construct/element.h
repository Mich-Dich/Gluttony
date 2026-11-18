#pragma once

#include "procedural/construct/attributes.h"
#include "procedural/construct/geometry/kernel.h"

// Procedural construction: elements, guides (authored elements) and relations.
// See documentation/procedural_construction_interface.md §3



namespace GLT::construct {

    // CONSTANTS =======================================================================================================

    // Element kinds of the core
    //   guide.volume / guide.curve / guide.surface / guide.point   authored guides (after Interpret)
    [[nodiscard]] kind_id kind_guide_volume();
    [[nodiscard]] kind_id kind_guide_curve();
    [[nodiscard]] kind_id kind_guide_surface();
    [[nodiscard]] kind_id kind_guide_point();

    // Relation kinds of the core
    [[nodiscard]] relation_id relation_child_of();
    [[nodiscard]] relation_id relation_mirror_of();

    // Attributes of the core on elements
    //   core.radius  (curve, f32)   tube radius of curve guides / swept elements
    //   core.hidden  (element, bool) not realized
    [[nodiscard]] attribute_id attr_radius();
    [[nodiscard]] attribute_id attr_hidden();

    // TYPES ===========================================================================================================

    enum class shape_kind : u8 { none, point, frame, curve, surface, volume, graph };
    enum class origin_kind : u8 { authored, derived };
    enum class orientation : u8 { solid, cavity };      // volumes: which side is material

    // Abstract shapes (not output meshes). All in the element's frame
    struct point_shape   { std::vector<glm::vec3> points{}; };
    struct frame_shape   {};                                                // the element's placement is the frame
    struct curve_shape   { geometry::curve curve{}; };
    struct surface_shape { geometry::mesh surface{}; };
    struct volume_shape  { geometry::mesh cage{}; orientation side = orientation::solid; };
    struct graph_shape   { std::vector<glm::vec3> nodes{}; std::vector<std::pair<u32, u32>> edges{}; };
    using shape_data = std::variant<std::monostate, point_shape, frame_shape, curve_shape, surface_shape, volume_shape, graph_shape>;


    // @brief Typed parameter block of an element kind (any copyable struct, usually a reflected params struct)
    //
    // The type is identified by a stable token (hash of the type's name), so blocks made in one module are readable in
    // another as long as both see the same struct
    class param_block {
    public:

        param_block() = default;

        template<typename P>
        [[nodiscard]] static param_block make(P value) {
            param_block b{};
            b.m_data = std::make_shared<const P>(std::move(value));
            b.m_type = type_token<P>();
            return b;
        }

        // Null when empty or of another type
        template<typename P>
        [[nodiscard]] const P* get() const noexcept {
            return m_type == type_token<P>() ? static_cast<const P*>(m_data.get()) : nullptr;
        }

        [[nodiscard]] bool empty() const noexcept { return !m_data; }
        [[nodiscard]] u64 type() const noexcept { return m_type; }

        template<typename P>
        [[nodiscard]] static u64 type_token() noexcept {
            static const u64 token = fnv1a_64(std::meta::display_string_of(^^P));
            return token;
        }

    private:

        std::shared_ptr<const void>                     m_data{};
        u64                                             m_type = 0;
    };


    // @brief The unit of everything. Immutable once committed to an evaluation
    struct element {

        element_path                                    path{};
        kind_id                                         kind{};
        shape_kind                                      shape = shape_kind::none;
        origin_kind                                     origin = origin_kind::derived;
        role_id                                         role{};
        frame                                           placement{};        // world (construct) placement
        shape_data                                      data{};
        attribute_set                                   attributes{};
        param_block                                     params{};
        pass_id                                         pass{};             // pass that produced it ("authored" for guides)
        u32                                             depth = 0;          // production depth (recursion)
        element_handle                                  parent{};
        std::string                                     source{};           // production / stage label / guide name that made it
        u32                                             stage = 0;          // stage (generator_instance::id) that made it; 0: none.
                                                                            // Elements productions derive keep their parent's
    };


    // @brief A typed link between two elements
    struct relation {

        relation_id                                     kind{};             // core.child_of, core.mirror_of, pack kinds
        name_id                                         type{};
        element_handle                                  a{}, b{};
        attribute_set                                   params{};
    };

    // Guides (what the user authors) ---------------------------------------------------------------------------------

    struct volume_guide {

        geometry::primitive_kind                        primitive = geometry::primitive_kind::box;
        geometry::primitive_params                      params{};
        orientation                                     side = orientation::solid;
    };

    struct curve_guide {

        geometry::curve_type                            type = geometry::curve_type::catmull_rom;
        std::vector<glm::vec3>                          points{};
        bool                                            closed = false;
        std::vector<std::pair<attribute_id, std::vector<attribute_value>>> point_attributes{};
    };

    struct surface_guide {

        glm::vec2                                       size{ 1.0f };
        glm::uvec2                                      divisions{ 1, 1 };
    };

    struct point_guide {

        std::vector<glm::vec3>                          points{ glm::vec3(0.0f) };
    };

    using guide_shape = std::variant<volume_guide, curve_guide, surface_guide, point_guide>;


    // @brief Which faces of a guide's cage an edit applies to. Empty selection: all faces
    struct face_selection {

        std::optional<name_id>                          group{};            // core.group equals
        std::optional<glm::vec3>                        normal{};           // face normal within [max_angle] of this
        f32                                             max_angle_degrees = 1.0f;
        std::optional<std::pair<glm::vec3, glm::vec3>>  inside{};           // centroid inside this box (min, max)

        [[nodiscard]] bool matches(const geometry::mesh& m, u32 face) const;
    };

    // Edit steps, replayed in Interpret in order. Every step reports lineage, so attributes follow the active policies
    struct edit_transform      { glm::mat4 matrix{ 1.0f }; };
    struct edit_loop_cut       { glm::vec3 near_point{ 0.0f }; std::vector<f32> positions{ 0.5f }; };   // ring through the edge nearest to near_point
    struct edit_assign_group   { face_selection faces{}; name_id group{}; };
    struct edit_set_attribute  { face_selection faces{}; attribute_id id{}; attribute_value value{}; attribute_domain domain = attribute_domain::face; };
    using edit_step = std::variant<edit_transform, edit_loop_cut, edit_assign_group, edit_set_attribute>;


    struct guide_modifiers {

        std::optional<glm::vec4>                        mirror_plane{};     // in the guide's frame: adds a mirrored copy
    };


    // @brief An authored element: what the user places in the construct editor (or code builds)
    struct guide {

        std::string                                     name{};             // root path segment
        i64                                             index = -1;
        role_id                                         role{};
        frame                                           placement{};
        guide_shape                                     shape{};
        std::vector<edit_step>                          edits{};
        guide_modifiers                                 modifiers{};
        attribute_set                                   attributes{};
    };

    // FUNCTION DECLARATION ============================================================================================

    // Applies edit steps to a cage (volume / surface guides), in order, reporting lineage through [prop]. Steps that
    // cannot apply are skipped with a message in [warnings]
    [[nodiscard]] geometry::mesh apply_guide_edits(geometry::mesh m, std::span<const edit_step> edits, const propagator& prop,
                                                   std::vector<std::string>* warnings = nullptr);

    // The cage of a volume / surface guide after its edits, in the guide's frame (built-in propagation modes only: for
    // previews and tools). Empty for curve / point guides
    [[nodiscard]] std::optional<geometry::mesh> guide_mesh(const guide& g);

}
