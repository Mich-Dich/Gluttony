#pragma once

#include "asset/mesh.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::mesh {

    // CONSTANTS =======================================================================================================

    // Reference viewport height for lod_settings::pixel_error (screen sizes are resolution independent, the pixel
    // tolerance is expressed at this height)
    inline constexpr f32                                LOD_REFERENCE_SCREEN_HEIGHT = 1080.0f;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief Output of build_lods(): what ends up in mesh_asset::lods / lod_ranges / lod_indices
    struct lod_build_output {

        std::vector<lod_level>                          lods;           // [0] = source mesh
        std::vector<lod_range>                          ranges;         // [level * submesh_count + submesh]
        std::vector<u32>                                indices;        // LOD 1..N, addressed after the source indices
        std::vector<vertex>                             vertices;       // extra vertices (recomputed normals), addressed after the source vertices
        u32                                             requested = 1;  // lod_count asked for
        f64                                             build_ms = 0.0;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // @brief Generate LOD 1..N of a mesh with meshoptimizer (attribute aware edge collapse, per submesh so material
    // boundaries survive). Every level is simplified from LOD 0 directly, so its error is exact, not accumulated
    //
    // Generation stops early when a level no longer removes triangles (the error limit was reached), so the result can
    // hold fewer levels than [settings.lod_count]. Pure function: safe to call from import tools and the editor alike
    [[nodiscard]] lod_build_output build_lods(std::span<const vertex> vertices, std::span<const u32> indices,
        std::span<const submesh> submeshes, const lod_settings& settings);


    // @brief build_lods() on [mesh] with [mesh.lod], stores the result, applies auto screen sizes and bumps
    // [mesh.revision] so renderers re-upload. Returns the build time in milliseconds
    f64 generate_lods(mesh_asset& mesh);


    // @brief Remove all generated levels (the mesh renders with LOD 0 only)
    void clear_lods(mesh_asset& mesh);


    // @brief Derive per-level screen sizes from the achieved errors: the switch happens where the level's geometric
    // error projects to [settings.pixel_error] pixels at LOD_REFERENCE_SCREEN_HEIGHT. Keeps the sizes monotonic
    void fit_screen_sizes(lod_settings& settings, std::span<const lod_level> lods, const GLT::AABB& bounds);


    // @brief Sanitize user input: counts in range, ratios / errors / sizes clamped and monotonic per level
    void sanitize(lod_settings& settings);


    // @brief Bounding-sphere radius used by the LOD metric (half the AABB diagonal). The renderer uses the same value
    FORCE_INLINE_R f32 lod_sphere_radius(const GLT::AABB& bounds) noexcept {

        return 0.5f * glm::length(bounds.max - bounds.min);
    }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
