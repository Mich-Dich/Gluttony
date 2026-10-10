
#pragma once

#include <imgui.h>
#include <world/light_snapshot.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    void renderer::submit_lights(std::span<const GLT::world::light_snapshot> lights) {
        m_submitted_lights.assign(lights.begin(), lights.end());
    }

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void renderer::create_light_resources() {

        for (auto& buffer : m_light_buffers) {

            buffer = m_dev->create_buffer(MAX_LIGHTS * sizeof(gpu_light),
                vk::BufferUsageFlagBits::eStorageBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            m_live_buffer_count++;

            // Zero it so the shader can safely read MAX_LIGHTS slots without a count branch
            std::memset(m_dev->map_buffer(buffer), 0, MAX_LIGHTS * sizeof(gpu_light));
            m_dev->unmap_buffer(buffer);
        }
    }


    void renderer::destroy_light_resources() {

        for (auto& buffer : m_light_buffers) {
            if (buffer.buffer)
                m_dev->destroy_buffer(buffer);
            buffer = {};
        }
    }


    void renderer::update_lights() {

        m_packed_lights.clear();
        m_packed_lights.reserve(std::min<size_t>(MAX_LIGHTS,
            m_submitted_lights.size() + m_debug_lights.size()));

        auto pack = [&](const GLT::world::light_snapshot& snapshot) {

            if (m_packed_lights.size() >= MAX_LIGHTS)
                return;

            // Fall back to straight-down if the direction is degenerate; avoids normalize(0)
            const glm::vec3 direction = glm::dot(snapshot.direction, snapshot.direction) > 1e-6f
                ? glm::normalize(snapshot.direction) : glm::vec3(0.f, -1.f, 0.f);

            gpu_light light{};
            light.position_radius = glm::vec4(snapshot.position, snapshot.radius);
            light.color_intensity = glm::vec4(snapshot.color, snapshot.intensity);
            light.direction_inner_cos = glm::vec4(direction, std::cos(snapshot.inner_cone_angle));
            light.params = glm::vec4(std::cos(snapshot.outer_cone_angle), static_cast<f32>(snapshot.type), 0.f, 0.f);
            m_packed_lights.push_back(light);
        };

        for (const auto& light : m_submitted_lights)
            pack(light);

        for (const auto& light : m_debug_lights)
            pack(light);

        m_light_count = static_cast<u32>(m_packed_lights.size());

        if (m_light_count == 0)
            return;

        void* mapped = m_dev->map_buffer(m_light_buffers[m_current_frame]);
        std::memcpy(mapped, m_packed_lights.data(), m_light_count * sizeof(gpu_light));
        m_dev->unmap_buffer(m_light_buffers[m_current_frame]);
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
