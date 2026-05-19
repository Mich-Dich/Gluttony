#pragma once

#include "util/pch.h"

#if defined(RENDER_API_VULKAN)
    #include <vulkan/vulkan.h>
#endif
#include "util/data_structures/type_deletion_queue.h"


// FORWARD DECLARATIONS ================================================================================================


namespace GLT::renderer_vk_ray::utils {

    // CONSTANTS =======================================================================================================

    constexpr u32 general_performance_metric_array_size = 200;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct BVH_debug_visualization_settings {

        u32 max_depth = 3;
        u32 debug_method = 0;         // 0: Show no debug info, 1: Show bounding boxes, 2: Show box tests, 3: Show triangle
        glm::vec4 color{0, 0, 1, 1};
    };


    struct general_performance_metric {

        u32 meshes = 0, mesh_instances = 0, draw_calls = 0, material_binding_count = 0, pipline_binding_count = 0;
        u64 vertices = 0;
        f32 sleep_time = 0.f, work_time = 0.f;

        FORCE_INLINE u32 get_array_size() { return general_performance_metric_array_size; }

        f32 renderer_draw_time[general_performance_metric_array_size] = {};
        f32 draw_geometry_time[general_performance_metric_array_size] = {};
        f32 waiting_idle_time[general_performance_metric_array_size] = {};
        u16 current_index = 0;

        void next_iteration() {

            current_index = (current_index + 1) % general_performance_metric_array_size;
            material_binding_count = pipline_binding_count = draw_calls = 0;
            vertices = 0;
            sleep_time = work_time = 0.f;
        }
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class deletion_queue : public GLT::util::type_deletion_queue {
    public:

        #if defined(RENDER_API_VULKAN)
            void setup(VkDevice device);
        #endif

        void shutdown();

        void flush_pointer(std::pair<std::type_index, void*> pointer) override;

    private:
        #if defined(RENDER_API_VULKAN)
            VkDevice							    m_dq_device{};
        #endif
    };

}
