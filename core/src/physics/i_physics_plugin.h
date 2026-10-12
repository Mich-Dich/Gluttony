
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

    // convenience accessor
    FORCE_INLINE_R ref<i_physics_plugin> get_ref() {
        return plugin_manager::get_plugin_ref<i_physics_plugin>(plugin_manager::interface::physics);
    }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================


    class i_physics_plugin : public plugin_manager::i_plugin {
    public:

        ~i_physics_plugin() override = default;

        // ---- capability cast (same pattern as i_world_plugin) -------------------
        template<typename T> requires std::is_polymorphic_v<T>
        [[nodiscard]] T* as() noexcept { return dynamic_cast<T*>(this); }
        template<typename T> requires std::is_polymorphic_v<T>
        [[nodiscard]] const T* as() const noexcept { return dynamic_cast<const T*>(this); }

        // ---- identity -----------------------------------------------------------

        [[nodiscard]] virtual std::string_view backend_name() const noexcept = 0;

        // ---- lifecycle ----------------------------------------------------------

        virtual std::expected<void, physics_error> init(const glm::vec3& gravity = { 0.f, -9.81f, 0.f }) = 0;
        virtual void shutdown() noexcept = 0;

        // ---- stepping -----------------------------------------------------------

        // One call per frame from the world layer. `substeps` is advisory; the
        // plugin may internally fix the timestep and run N substeps if needed.
        virtual void step(f32 delta_time, u32 substeps = 1) = 0;

        [[nodiscard]] virtual glm::vec3 get_gravity() const noexcept = 0;
        virtual void set_gravity(const glm::vec3& gravity) = 0;

        // ---- shapes -------------------------------------------------------------

        // create_shape copies the descriptor's geometry; the caller's spans
        // need not outlive the call. Returns `unsupported_feature` when the
        // backend cannot express a given shape (e.g. dynamic triangle mesh).

        [[nodiscard]] virtual std::expected<shape_handle, physics_error> create_shape(const shape_desc& desc) = 0;
        virtual void release_shape(shape_handle h) noexcept = 0;

        // ---- bodies -------------------------------------------------------------

        [[nodiscard]] virtual std::expected<body_handle, physics_error> create_body(const body_desc& desc) = 0;
        virtual void destroy_body(body_handle h) noexcept = 0;
        [[nodiscard]] virtual bool is_alive(body_handle h) const noexcept = 0;

        // --- read ---
        [[nodiscard]] virtual std::optional<body_state> get_body_state(body_handle h) const = 0;

        // Bulk read — same shape as gather_scene(). `out` is sized by the caller;
        // returns how many entries were written. Missing/dead handles produce
        // a zeroed body_state, so positional alignment is preserved.
        virtual size_t get_body_states(std::span<const body_handle> bodies, std::span<body_state> out) const = 0;

        // --- write ---
        virtual void set_body_transform (body_handle h, const glm::vec3& pos, const glm::quat& rot) = 0;
        virtual void set_linear_velocity (body_handle h, const glm::vec3& v) = 0;
        virtual void set_angular_velocity(body_handle h, const glm::vec3& w) = 0;
        virtual void translate(body_handle h, const glm::vec3& delta) = 0;
        virtual void rotate(body_handle h, const glm::quat& delta) = 0;

        // --- forces ---

        // Force/torque: integrated over the next step (reset after).
        // Impulse/angular impulse: instantaneous.
        // `point` is world-space; omit for center-of-mass application.
        virtual void apply_force  (body_handle h, const glm::vec3& force, const glm::vec3& point = {}) = 0;
        virtual void apply_impulse(body_handle h, const glm::vec3& impulse, const glm::vec3& point = {}) = 0;
        virtual void apply_torque (body_handle h, const glm::vec3& torque) = 0;
        virtual void apply_angular_impulse(body_handle h, const glm::vec3& imp) = 0;

        // --- sleep/activation ---
        virtual void set_active(body_handle h, bool active) = 0;
        [[nodiscard]] virtual bool is_active(body_handle h) const = 0;

        // ---- queries ------------------------------------------------------------

        // Single-hit raycast returns by value (fast, inline-able).
        // Multi-hit variants write into caller spans, no allocation.
        [[nodiscard]] virtual ray_cast_hit ray_cast(const glm::vec3& origin, const glm::vec3& direction, f32 max_distance = 1e30f,
            const query_filter& filter = {}) const = 0;

        virtual size_t ray_cast_all (const glm::vec3& origin, const glm::vec3& direction, f32 max_distance, std::span<ray_cast_hit> out,
            const query_filter& filter = {}) const = 0;

        virtual size_t overlap_sphere(const glm::vec3& center, f32 radius, std::span<overlap_hit> out, 
            const query_filter& filter = {}) const = 0;

        virtual size_t overlap_box   (const glm::vec3& half_extents, const glm::vec3& center, const glm::quat& rotation, 
            std::span<overlap_hit> out, const query_filter& filter = {}) const = 0;

        // ---- events -------------------------------------------------------------

        // Opt-in per body. The plugin buffers events during step(); the caller
        // drains once per frame. The returned span is valid until the next
        // call to step() or drain_contact_events().
        virtual void enable_contact_events(body_handle h, contact_event_mask mask) = 0;
        [[nodiscard]] virtual std::span<const contact_event> drain_contact_events() = 0;

        // ---- stats --------------------------------------------------------------
        [[nodiscard]] virtual physics_stats get_stats() const noexcept = 0;
    };

}
