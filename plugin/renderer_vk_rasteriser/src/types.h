
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    constexpr u32                                               MAX_CONCURRENT_FRAMES = 3;

    static constexpr u32                                        VERTEX_HEADROOM_MIN = 64 * 1024;
    
    static constexpr u32                                        INDEX_HEADROOM_MIN  = 128 * 1024;

    static constexpr u32                                        MAX_INSTANCES = 4096;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct camera_ubo_data {
        glm::mat4                                               view_proj{1.0f};
        glm::mat4                                               view{1.0f};
        glm::vec4                                               camera_pos{0.0f};
    };
    static_assert(sizeof(camera_ubo_data) == 144, "camera_ubo_data layout mismatch");


    // One entry per scene instance. Layout matches the std430 declaration in mesh.vert.glsl. Total size 144 bytes, 16-byte aligned
    struct gpu_instance_data {
        glm::mat4                                               model{1.0f};
        glm::mat4                                               normal_matrix{1.0f};
        u32                                                     material_index = 0;
        u32                                                     _padding[3] = {0, 0, 0};
    };
    static_assert(sizeof(gpu_instance_data) == 144, "gpu_instance_data layout mismatch");


    // A contiguous run of instances in the packed instance buffer that all reference the same mesh. Produced by build_instance_buffer()
    struct instance_batch {
        GLT::asset::handle                                      mesh{};
        u32                                                     first_instance = 0;
        u32                                                     instance_count = 0;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
