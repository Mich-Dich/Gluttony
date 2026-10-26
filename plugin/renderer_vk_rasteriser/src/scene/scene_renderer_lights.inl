
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    constexpr f32                               LIGHT_TYPE_DIRECTIONAL = 0.f;

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

    void scene_renderer::submit_lights(std::span<const GLT::world::light_snapshot> lights) {

        m_submitted_lights.assign(lights.begin(), lights.end());
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void scene_renderer::update_lights() {

        m_shadow.active = false;
        m_packed_lights.clear();
        m_packed_lights.reserve(std::min<size_t>(MAX_LIGHTS, m_submitted_lights.size()));

        for (const auto& submitted_light : m_submitted_lights) {

            if (m_packed_lights.size() >= MAX_LIGHTS)
                break;
            const glm::vec3 dir = glm::dot(submitted_light.direction, submitted_light.direction) > 1e-6f
                ? glm::normalize(submitted_light.direction) : glm::vec3(0.f, -1.f, 0.f);

            gpu_light light{};
            light.position_radius = glm::vec4(submitted_light.position, submitted_light.radius);
            light.color_intensity = glm::vec4(submitted_light.color, submitted_light.intensity);
            light.direction_inner_cos = glm::vec4(dir, std::cos(submitted_light.inner_cone_angle));
            light.params = glm::vec4(std::cos(submitted_light.outer_cone_angle), static_cast<f32>(submitted_light.type), 0.f, 0.f);

            if (m_shadow_settings.enabled && !m_shadow.active && light.params.y == LIGHT_TYPE_DIRECTIONAL) {
                light.params.z = 1.f;
                m_shadow.active = true;
                m_shadow.direction = dir;
            }
            m_packed_lights.push_back(light);
        }

        m_light_count = static_cast<u32>(m_packed_lights.size());
        if (m_light_count == 0) {
            m_local_shadow_view_count = 0;
            m_local_shadow_tiles.clear();
            return;
        }

        update_local_shadows();

        void* mapped = m_plugin.get_dev()->map_buffer(m_light_buffers[m_current_frame]);
        std::memcpy(mapped, m_packed_lights.data(), m_light_count * sizeof(gpu_light));
        m_plugin.get_dev()->unmap_buffer(m_light_buffers[m_current_frame]);
    }

}
