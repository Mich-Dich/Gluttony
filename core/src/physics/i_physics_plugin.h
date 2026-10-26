
#pragma once

#include "physics/type.h"
#include "physics/shape.h"
#include "physics/body.h"
#include "physics/query.h"
#include "physics/event.h"
#include "plugin_system/plugin_manager.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics {
    class i_physics_plugin;
}

namespace GLT::physics {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================
    
    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // @brief Hot-path accessor: cached raw pointer, thread-safe, no refcount traffic. Cache the result and compare
    // `plugin_manager::get_generation()` for invalidation
    // @return nullptr if the audio plugin is not loaded
    FORCE_INLINE_R i_physics_plugin* get_ptr() noexcept {

        return GLT::plugin_manager::get_plugin_raw<i_physics_plugin>(GLT::plugin_manager::interface::physics);
    }


    // @brief Owning reference for cases where you need to keep the plugin alive across unload phases
    FORCE_INLINE_R ref<i_physics_plugin> get_ref() {

        return GLT::plugin_manager::get_plugin_ref_unsafe<i_physics_plugin>(GLT::plugin_manager::interface::physics);
    }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class i_physics_plugin : public plugin_manager::i_plugin {
    public:
        ~i_physics_plugin() override = default;

        template<typename T> requires std::is_polymorphic_v<T>
        [[nodiscard]] T* as() noexcept { return dynamic_cast<T*>(this); }

        template<typename T> requires std::is_polymorphic_v<T>
        [[nodiscard]] const T* as() const noexcept { return dynamic_cast<const T*>(this); }

        [[nodiscard]] virtual std::string_view backend_name() const noexcept = 0;

        virtual std::expected<void, physics_error> init(const glm::vec3& gravity = { 0.f, -9.81f, 0.f }) = 0;

        virtual void shutdown() noexcept = 0;

        virtual void step(f32 delta_time, u32 substeps = 1) = 0;

        [[nodiscard]] virtual glm::vec3 get_gravity() const noexcept = 0;
        virtual void set_gravity(const glm::vec3& gravity) = 0;

        // create_shape copies geometry. Returns invalid_argument for malformed
        // shape data. Motion compatibility is checked later by create_body().
        [[nodiscard]] virtual std::expected<shape_handle, physics_error> create_shape(const shape_desc& desc) = 0;

        virtual void release_shape(shape_handle h) noexcept = 0;

        [[nodiscard]] virtual std::expected<body_handle, physics_error> create_body(const body_desc& desc) = 0;

        virtual void destroy_body(body_handle h) noexcept = 0;
        [[nodiscard]] virtual bool is_alive(body_handle h) const noexcept = 0;

        [[nodiscard]] virtual std::optional<body_state> get_body_state(body_handle h) const = 0;

        virtual size_t get_body_states(std::span<const body_handle> bodies, std::span<body_state> out) const = 0;

        virtual void set_body_transform(body_handle h, const glm::vec3& pos, const glm::quat& rot) = 0;

        virtual void set_linear_velocity(body_handle h, const glm::vec3& v) = 0;
        virtual void set_angular_velocity(body_handle h, const glm::vec3& w) = 0;

        virtual void translate(body_handle h, const glm::vec3& delta) = 0;
        virtual void rotate(body_handle h, const glm::quat& delta) = 0;

        // Batched writes: preferred hot path for kinematic controllers,
        // moving platforms, and save-restore.
        virtual void set_body_transforms(std::span<const body_handle> bodies, std::span<const glm::vec3> positions,
            std::span<const glm::quat> rotations) = 0;

        virtual void set_linear_velocities(std::span<const body_handle> bodies, std::span<const glm::vec3> velocities) = 0;

        virtual void set_angular_velocities(std::span<const body_handle> bodies, std::span<const glm::vec3> velocities) = 0;

        virtual void apply_force(body_handle h, const glm::vec3& force, std::optional<glm::vec3> point = std::nullopt) = 0;

        virtual void apply_impulse(body_handle h, const glm::vec3& impulse, std::optional<glm::vec3> point = std::nullopt) = 0;

        virtual void apply_torque(body_handle h, const glm::vec3& torque) = 0;
        virtual void apply_angular_impulse(body_handle h, const glm::vec3& imp) = 0;

        virtual void set_active(body_handle h, bool active) = 0;
        [[nodiscard]] virtual bool is_active(body_handle h) const = 0;

        [[nodiscard]] virtual ray_cast_hit ray_cast(const glm::vec3& origin, const glm::vec3& direction, f32 max_distance = 1e30f,
            const query_filter& filter = {}) const = 0;

        virtual size_t ray_cast_multi(const glm::vec3& origin, const glm::vec3& direction, f32 max_distance, 
            std::span<ray_cast_hit> out, const query_filter& filter = {}) const = 0;

        virtual size_t overlap_sphere(const glm::vec3& center, f32 radius, std::span<overlap_hit> out, 
            const query_filter& filter = {}) const = 0;

        virtual size_t overlap_box(const glm::vec3& center, const glm::quat& rotation, const glm::vec3& half_extents, 
            std::span<overlap_hit> out, const query_filter& filter = {}) const = 0;

        virtual void enable_contact_events(body_handle h, contact_event_mask mask) = 0;

        [[nodiscard]] virtual std::span<const contact_event> drain_contact_events() = 0;

        [[nodiscard]] virtual physics_stats get_stats() const noexcept = 0;
    };

}
