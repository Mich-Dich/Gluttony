#pragma once

#include "asset/mesh.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::mesh {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief Output of build_clusters(): what ends up in mesh_asset::clusters / cluster_indices / cluster_submeshes
    struct cluster_build_output {

        std::vector<cluster>                            clusters;               // submeshes back to back, levels ascending per submesh
        std::vector<u32>                                indices;                // triangle lists, into [source vertices ++ vertices]
        std::vector<cluster_submesh>                    submeshes;              // one per source submesh
        std::vector<vertex>                             vertices;               // extra vertices of simplified clusters (recomputed normals)
        u32                                             level_count = 0;        // deepest hierarchy of all submeshes
        u32                                             source_triangles = 0;   // triangles of the level 0 clusters
        u32                                             coarsest_triangles = 0; // triangles drawn when every cluster is as coarse as it gets
        f64                                             build_ms = 0.0;
    };


    // @brief Everything the cluster selection depends on for one instance and one view (CPU twin of the renderer's
    // virtual_geometry_cull.comp, used by the editor statistics and by tests)
    struct cluster_view {

        glm::mat4                                       model{ 1.0f };          // instance transform (mesh -> world)
        glm::vec3                                       camera_position{ 0.0f };// world space
        f32                                             projection = 1.0f;      // |P11| of the projection = cot(fov_y / 2)
        f32                                             near_plane = 0.05f;
        f32                                             viewport_height = 1080.0f;
        f32                                             pixel_error = 1.0f;     // allowed deviation in pixels
        f32                                             error_scale = 1.0f;     // cluster_settings::error_scale
    };


    // @brief What a view draws of one mesh: result of measure_cut()
    struct cluster_cut {

        u32                                             clusters = 0;
        u32                                             triangles = 0;
        u32                                             deepest_level = 0;      // coarsest level in use
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // @brief Build the cluster hierarchy of a mesh with meshoptimizer (clusterlod): split every submesh into clusters,
    // then repeatedly group neighbouring clusters, simplify each group with its outline locked and split the result
    // into new clusters, until nothing can be merged any more
    //
    // Clusters reference the source vertices (simplification only removes triangles), plus, depending on
    // cluster_settings' normals method, extra vertices that differ from a source vertex only in normal and tangent
    // (cluster_build_output::vertices, see cluster_normals_mode). Submeshes are processed independently and in
    // parallel. Pure function: safe to call from import tools and the editor alike
    [[nodiscard]] cluster_build_output build_clusters(std::span<const vertex> vertices, std::span<const u32> indices,
        std::span<const submesh> submeshes, const cluster_settings& settings);


    // @brief build_clusters() on [mesh] with [mesh.virtual_geometry], stores the result and bumps [mesh.revision] so
    // renderers re-upload. Returns the build time in milliseconds
    f64 generate_clusters(mesh_asset& mesh);


    // @brief Remove the cluster hierarchy (the mesh renders with its discrete LODs again)
    void clear_clusters(mesh_asset& mesh);


    // @brief Switch [mesh] between discrete LODs and virtual geometry (a mesh uses exactly one, see detail_mode) and
    // bump [mesh.revision]. Switching to discrete LODs removes the cluster hierarchy. Switching to virtual geometry
    // only records the choice: build the clusters with generate_clusters() / build_clusters(); until they exist the
    // mesh keeps rendering with its discrete levels. Returns false if the mesh already was in [mode]
    bool set_detail_mode(mesh_asset& mesh, detail_mode mode);


    // @brief Sanitize user input: cluster size in range, weights, error scale and smoothing angle finite and clamped.
    // Also upgrades settings stored before the normals method existed (cluster_settings::version 0)
    void sanitize(cluster_settings& settings);


    // @brief True if cluster data is safe to draw: every range inside its array, every index a valid vertex, every
    // cluster in the submesh that lists it. Loaders call this before accepting the chunks. [vertex_count] is the size
    // of the stream the indices address: [vertices ++ cluster_vertices]
    [[nodiscard]] bool validate_clusters(std::span<const cluster> clusters, std::span<const u32> indices,
        std::span<const cluster_submesh> submeshes, size_t vertex_count, size_t submesh_count);


    // @brief Screen-space size (fraction of the viewport height) of [error] at the nearest point of [sphere]. Both are
    // in mesh units, [scale] is the largest axis scale of view.model
    [[nodiscard]] f32 projected_cluster_error(const glm::vec4& sphere, f32 error, const cluster_view& view, f32 scale);


    // @brief True if [value] is drawn for [view]: detailed enough itself, while its coarser replacement is not
    [[nodiscard]] bool cluster_in_cut(const cluster& value, const cluster_view& view);


    // @brief Clusters / triangles the level-of-detail selection picks for [view]. No frustum or backface culling
    [[nodiscard]] cluster_cut measure_cut(std::span<const cluster> clusters, const cluster_view& view);


    // @brief Indices (into [clusters], ascending) of the clusters the selection picks for [view]: what measure_cut()
    // counts. [out] is cleared first
    void collect_cut(std::span<const cluster> clusters, const cluster_view& view, std::vector<u32>& out);


    // @brief Colour of cluster [cluster_index] (index inside its mesh) in the renderer's "Geometry Clusters" debug view,
    // for overlays that should match it. Same function as id_color() in virtual_geometry_debug.vert.glsl
    [[nodiscard]] glm::vec3 cluster_debug_color(u32 cluster_index);


    // @brief Colour of hierarchy level [depth] in the "Cluster Levels" debug view (level_color() in the same shader)
    [[nodiscard]] glm::vec3 cluster_level_color(u32 depth);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
