#pragma once

#include "procedural/construct/attributes.h"

// Procedural construction: half-edge polygon mesh with attribute columns per domain.
// Faces are polygons (quads stay quads, so edge rings and loop cuts work); triangulation happens at output.
// Domains: vertex (per vertex), face (per face), corner (per half-edge: the corner at the half-edge's origin in its
// face), edge (per half-edge; both half-edges of an edge carry the same value).



namespace GLT::construct::geometry {

    // CONSTANTS =======================================================================================================

    inline constexpr u32                                INVALID_INDEX = ~0u;

    // Attributes the core itself uses on meshes
    //   core.group  (face, name_id)  named face group: "+x", "side", "cap_top", ...
    //   core.uv     (corner, vec2)   surface parameterization / texture coordinates
    //   core.hard   (edge, bool)     hard edge: normals are split across it
    //   core.material (face, name_id) material slot name (output groups faces by it)
    [[nodiscard]] attribute_id attr_group();
    [[nodiscard]] attribute_id attr_uv();
    [[nodiscard]] attribute_id attr_hard();
    [[nodiscard]] attribute_id attr_material();

    // TYPES ===========================================================================================================

    struct halfedge {

        u32                                             vertex = INVALID_INDEX;     // the vertex it points TO
        u32                                             next = INVALID_INDEX;
        u32                                             twin = INVALID_INDEX;       // INVALID_INDEX: boundary
        u32                                             face = INVALID_INDEX;
    };


    struct column {

        attribute_id                                    id{};
        attribute_value                                 default_value{};
        std::vector<attribute_value>                    values{};
    };


    // @brief Polygon mesh, f32 positions in the owning element's frame
    class mesh {
    public:

        // building ----------------------------------------------------------------------------------------------------

        u32 add_vertex(const glm::vec3& position);
        // Counter-clockwise loop (seen from outside). Twins are linked by link_twins() (from_polygons does it)
        u32 add_face(std::span<const u32> loop);
        void link_twins();

        [[nodiscard]] static mesh from_polygons(std::span<const glm::vec3> positions, std::span<const std::vector<u32>> faces);

        // topology ----------------------------------------------------------------------------------------------------

        [[nodiscard]] u32 vertex_count() const noexcept { return static_cast<u32>(m_positions.size()); }
        [[nodiscard]] u32 face_count() const noexcept { return static_cast<u32>(m_face_halfedge.size()); }
        [[nodiscard]] u32 halfedge_count() const noexcept { return static_cast<u32>(m_halfedges.size()); }
        [[nodiscard]] u32 domain_size(attribute_domain d) const noexcept;

        [[nodiscard]] const glm::vec3& position(u32 v) const noexcept { return m_positions[v]; }
        void set_position(u32 v, const glm::vec3& p) noexcept { m_positions[v] = p; }
        [[nodiscard]] std::span<const glm::vec3> positions() const noexcept { return m_positions; }

        [[nodiscard]] const halfedge& he(u32 h) const noexcept { return m_halfedges[h]; }
        [[nodiscard]] u32 from_vertex(u32 h) const noexcept;
        [[nodiscard]] u32 prev(u32 h) const noexcept;
        [[nodiscard]] u32 face_halfedge(u32 f) const noexcept { return m_face_halfedge[f]; }
        [[nodiscard]] u32 face_size(u32 f) const noexcept { return m_face_size[f]; }
        // Half-edges / vertices of a face in loop order, starting at face_halfedge(f)
        void face_halfedges(u32 f, std::vector<u32>& out) const;
        void face_vertices(u32 f, std::vector<u32>& out) const;
        [[nodiscard]] glm::vec3 face_normal(u32 f) const;          // Newell normal, normalized (0 for degenerate faces)
        [[nodiscard]] glm::vec3 face_centroid(u32 f) const;

        // attributes --------------------------------------------------------------------------------------------------

        // Creates the column if missing (filled with [default_value]); returns it
        column& add_column(attribute_domain domain, attribute_id id, attribute_value default_value);
        [[nodiscard]] column* find_column(attribute_domain domain, attribute_id id) noexcept;
        [[nodiscard]] const column* find_column(attribute_domain domain, attribute_id id) const noexcept;
        [[nodiscard]] std::span<const column> columns(attribute_domain domain) const noexcept;
        [[nodiscard]] std::span<column> columns(attribute_domain domain) noexcept;

        // Value of [index] in a column, the column default if [index] has none, monostate without the column
        [[nodiscard]] const attribute_value& get(attribute_domain domain, attribute_id id, u32 index) const noexcept;
        // Sets a value, creating the column (default: monostate) if needed. Edges: both half-edges
        void set(attribute_domain domain, attribute_id id, u32 index, attribute_value value);

        // Resizes every column of every domain to the current element counts (new entries take the column default)
        void sync_columns();

        // Faces whose core.group equals [group]
        [[nodiscard]] std::vector<u32> faces_in_group(name_id group) const;

        // hashing / checks --------------------------------------------------------------------------------------------

        // Hash of topology, positions (bit exact) and attributes. Deterministic across runs
        [[nodiscard]] u64 hash() const noexcept;
        // Empty when the half-edge structure is consistent, else a description of the first problem
        [[nodiscard]] std::string validate() const;

    private:

        [[nodiscard]] std::vector<column>& domain_columns(attribute_domain domain) noexcept;
        [[nodiscard]] const std::vector<column>& domain_columns(attribute_domain domain) const noexcept;

        std::vector<glm::vec3>                          m_positions{};
        std::vector<halfedge>                           m_halfedges{};
        std::vector<u32>                                m_face_halfedge{};
        std::vector<u32>                                m_face_size{};

        std::vector<column>                             m_vertex_columns{};
        std::vector<column>                             m_face_columns{};
        std::vector<column>                             m_corner_columns{};
        std::vector<column>                             m_edge_columns{};
    };

}
