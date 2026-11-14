
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


    void scene_renderer::submit_environment(const GLT::world::environment_snapshot& environment) { m_environment = environment; }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void scene_renderer::update_lights() {
        GLT_PROFILE_SCOPE("scene_renderer::update_lights");

        m_shadow.active = false;
        m_packed_lights.clear();
        m_packed_light_keys.clear();
        m_packed_lights.reserve(std::min<size_t>(MAX_LIGHTS, m_submitted_lights.size()));
        m_packed_light_keys.reserve(m_packed_lights.capacity());

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

            // The first directional light is the sun: it casts shadows (cascades, update_shadow()). Like the local
            // lights' atlas, the cascades need clip distances in the vertex shader
            if (m_shadow_settings.enabled && !m_shadow.active && light.params.y == LIGHT_TYPE_DIRECTIONAL
                && m_shared->local_shadow_atlas_supported()) {
                light.params.z = 1.f;
                m_shadow.active = true;
                m_shadow.direction = dir;
            }
            // Stable id for the shadow cache: the light's entity. Lights without one are told apart by their position in
            // the list
            const GLT::world::entity_id entity = submitted_light.entity;
            m_packed_light_keys.push_back(entity.is_valid()
                ? (static_cast<u64>(entity.generation) << 32) | entity.index
                : 0xFFFFFFFF00000000ull | m_packed_lights.size());
            m_packed_lights.push_back(light);
        }

        m_light_count = static_cast<u32>(m_packed_lights.size());
        update_local_shadows();                 // sets params.z of the local lights; without lights it empties the cache
        if (m_light_count == 0)
            return;

        void* mapped = m_plugin.get_dev()->map_buffer(m_light_buffers[m_current_frame]);
        std::memcpy(mapped, m_packed_lights.data(), m_light_count * sizeof(gpu_light));
        m_plugin.get_dev()->unmap_buffer(m_light_buffers[m_current_frame]);
    }

}
