
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    constexpr u32                                               MAX_CONCURRENT_FRAMES = 3;

    static constexpr u32                                        VERTEX_HEADROOM_MIN = 64 * 1024;
    
    static constexpr u32                                        INDEX_HEADROOM_MIN  = 128 * 1024;

    static constexpr u32                                        MAX_INSTANCES = 4096;

    static constexpr u32                                        MAX_DRAW_COMMANDS = 65536;

    static constexpr u32                                        MAX_MESHES = 4096;

    static constexpr u32                                        MAX_SUBMESHES_PER_MESH = 32;

    static constexpr u32                                        MAX_SUBMESHES = MAX_MESHES * MAX_SUBMESHES_PER_MESH;

    static constexpr u32                                        HIZ_LEVEL_COUNT = 5;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct camera_ubo_data {
        glm::mat4                                               view_proj{1.0f};
        glm::mat4                                               view{1.0f};
        glm::vec4                                               camera_pos{0.0f};
        glm::vec4                                               frustum_planes[6]{};
        glm::uvec4                                              flags{0u};                  // x = hiz_enabled (1 = use occlusion test)
    };
    static_assert(sizeof(camera_ubo_data) == 256);


    // Per-instance data, written every frame by build_instance_buffer(). std430 aligned; matches InstanceData in the shaders
    struct gpu_instance_data {
        glm::mat4                                               model{1.0f};
        glm::mat4                                               normal_matrix{1.0f};
        u32                                                     mesh_index = 0;
        u32                                                     material_index = 0;
        u32                                                     _pad0 = 0;
        u32                                                     _pad1 = 0;
    };
    static_assert(sizeof(gpu_instance_data) == 144, "gpu_instance_data layout mismatch");


    // Persistent per-mesh data, written when a mesh is loaded. Indexed by the mesh slot index. Read by the cull compute shader
    struct gpu_mesh_data {
        glm::vec3                                               local_min{0.0f};
        u32                                                     first_submesh = 0;
        glm::vec3                                               local_max{0.0f};
        u32                                                     submesh_count = 0;
        u32                                                     vertex_offset = 0;
        u32                                                     index_offset = 0;
        u32                                                     _pad0 = 0;
        u32                                                     _pad1 = 0;
    };
    static_assert(sizeof(gpu_mesh_data) == 48, "gpu_mesh_data layout mismatch");


    // A contiguous run of instances in the packed instance buffer that all reference the same mesh. Produced by build_instance_buffer()
    struct instance_batch {
        GLT::asset::handle                                      mesh{};
        u32                                                     first_instance = 0;
        u32                                                     instance_count = 0;
    };


    // Persistent per-submesh data. `first_index` is relative to the mesh's index_offset. Read by the cull compute shader
    struct gpu_submesh_data {
        u32                                                     first_index = 0;
        u32                                                     index_count = 0;
    };
    static_assert(sizeof(gpu_submesh_data) == 8, "gpu_submesh_data layout mismatch");

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
