#pragma once

#include "procedural/construct/geometry/mesh.h"
#include "procedural/construct/geometry/curve.h"
#include "procedural/construct/propagation.h"

// Procedural construction: geometry operations. Every operation that creates elements from others reports lineage through the
// given propagator, so attribute columns follow the rules of the pack / policy in effect.
// See documentation/procedural_construction_interface.md §13



namespace GLT::construct::geometry {

    // TYPES ===========================================================================================================

    enum class primitive_kind : u8 { box, cylinder, sphere, capsule, plane };

    struct primitive_params {

        glm::vec3                                       size{ 1.0f };           // box: extent; plane: x / z extent
        f32                                             radius = 0.5f;          // cylinder, sphere, capsule
        f32                                             height = 1.0f;          // cylinder: total; capsule: of the straight part
        u32                                             segments = 16;          // around
        u32                                             rings = 8;              // sphere / capsule: per hemisphere / whole sphere
        glm::uvec2                                      divisions{ 1, 1 };      // box / plane: quads per side
        bool                                            caps = true;            // cylinder
    };


    struct sweep_settings {

        u32                                             sides = 8;
        f32                                             radius = 0.05f;         // used when the curve has no [radius_attribute]
        attribute_id                                    radius_attribute{};     // per control point, interpolated along the curve
        bool                                            caps = true;
        u32                                             segments_per_span = 8;
    };


    // @brief Triangles ready for output: one entry per unique (position, normal, uv) corner
    struct triangle_mesh {

        std::vector<glm::vec3>                          positions{};
        std::vector<glm::vec3>                          normals{};
        std::vector<glm::vec2>                          uvs{};
        std::vector<u32>                                indices{};
        std::vector<name_id>                            triangle_material{};    // per triangle (core.material of its face)
    };


    // @brief Origin of one element of an operation's result (per domain), turned into lineage events
    struct element_origin {

        lineage_kind                                    kind = lineage_kind::copy;
        std::array<lineage_source, 4>                   sources{};
        u8                                              source_count = 0;
        bool                                            is_seam = false;

        static element_origin copy_of(u32 i) { element_origin o{}; o.sources[0] = { i, 1.0f }; o.source_count = 1; return o; }
        static element_origin none() { return {}; }
    };

    // @brief A polyline produced by slicing (closed loops for closed meshes)
    struct contour {

        std::vector<glm::vec3>                          points{};
        bool                                            closed = false;
    };

    // FUNCTION DECLARATION ============================================================================================

    // Primitives carry core.group (face), core.uv (corner) and core.hard (edge: the edges that should stay sharp)
    [[nodiscard]] mesh make_primitive(primitive_kind kind, const primitive_params& params);

    // Positions only (attributes are untouched). Mirroring transforms flip the winding back so faces stay outward
    void transform(mesh& m, const glm::mat4& matrix);

    // @brief The faces of the quad edge ring through the edge of half-edge [h]
    //
    // Each entry is the half-edge where the ring enters a face. All entry half-edges start on the same side of the
    // ring ("rail A"), so a parameter measured from their origin is consistent along the ring. Stops at non-quads and
    // boundaries
    struct edge_ring {
        std::vector<u32>                                entries{};
        bool                                            closed = false;
    };
    [[nodiscard]] edge_ring find_edge_ring(const mesh& m, u32 h);

    // @brief Loop cut through the edge ring of [h]: one new edge loop per value in [positions] (0..1, measured from rail A)
    //
    // Lineage: new vertices are splits of their edge's endpoints; split faces are splits of their face; new corners
    // interpolate the face's corners; edges of the new loops are seams (is_seam) of the ring's rails; cut edge pieces
    // are splits of their edge. Faces next to an open ring end get the new vertices inserted into their loop
    [[nodiscard]] mesh loop_cut(const mesh& m, u32 h, std::span<const f32> positions, const propagator& prop);

    // Mirror copy across a plane (ax + by + cz + d = 0). Winding is reversed so faces stay outward. Lineage: mirror
    [[nodiscard]] mesh mirror(const mesh& m, const glm::vec4& plane, const propagator& prop);

    // Appends [src] to [dst]. Columns missing on one side are created with their default
    void append(mesh& dst, const mesh& src);

    // Circular tube along a curve. Corner core.uv = (around 0..1, arc length). Caps are faces of group "cap_start" /
    // "cap_end" with hard rims; the side is group "side"
    [[nodiscard]] mesh sweep_circle(const curve& c, const sweep_settings& settings);

    // Triangulates (fan per polygon) with normals: smooth across edges sharper than [smooth_angle_degrees] is split,
    // core.hard edges are always split. UVs: core.uv when present, else box projection
    [[nodiscard]] triangle_mesh triangulate(const mesh& m, f32 smooth_angle_degrees = 40.0f);

    // Intersection of [m] with the plane dot(n, p) = d ([plane] = (n, d), n need not be normalized). Loops are oriented
    // counter-clockwise seen from +n for outward-facing meshes; ordered deterministically (by first segment)
    [[nodiscard]] std::vector<contour> slice(const mesh& m, const glm::vec4& plane);

    // Resamples a contour to points [spacing] apart along its length (at least [min_points])
    [[nodiscard]] contour resample(const contour& c, f32 spacing, u32 min_points = 8);

    // Signed area of a closed contour projected on the plane with normal [n] (> 0: counter-clockwise seen from +n)
    [[nodiscard]] f32 signed_area(const contour& c, const glm::vec3& n);

    // Builds the result mesh's columns from [src]'s, element by element, through the propagator
    void propagate_columns(const mesh& src, mesh& dst, attribute_domain domain, std::span<const element_origin> origins,
                           const propagator& prop);

}
