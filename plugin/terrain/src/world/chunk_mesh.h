
#pragma once

#include <render/procedural_mesh.h>
#include <terrain/types.h>



namespace GLT::terrain {

    struct chunk_mesh_options {
        f32                                             skirt_depth = 0.0f;     // > 0: a skirt hangs down this far around the chunk
        f32                                             uv_scale = 1.0f / 32.0f; // uv = world xz * uv_scale
    };

    struct chunk_mesh_result {
        GLT::render::procedural_mesh_data               data{};
        f32                                             min_height = 0.0f;
        f32                                             max_height = 0.0f;
    };

    // @brief A heightfield chunk as a mesh: one vertex per sample of [vertices] (world positions, x / z from the lattice,
    // y = height), normals from central differences. [heights] must cover [vertices] grown by one sample (the ring the
    // normals at the border need). Triangles are counter-clockwise seen from above (the renderer's front face).
    //
    // Skirts: chunks of different detail meet with small cracks (a coarse edge skips the fine edge's middle vertices). A
    // skirt (a vertical strip along the border, both sides drawn) hides them
    [[nodiscard]] chunk_mesh_result build_chunk_mesh(const field& heights, const region& vertices, const chunk_mesh_options& options = {});

}
