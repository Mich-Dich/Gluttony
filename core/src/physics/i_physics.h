
#pragma once

#include "physics/type.h"
#include "physics/body.h"
#include "physics/shape.h"
#include "physics/query.h"
#include "physics/event.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics {
    class i_physics;
}

namespace GLT::physics {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief One live physics simulation instance
    //
    // Owns a Jolt PhysicsSystem (or whatever the backend uses), its body/shape/contact caches, and its gravity. Multiple instances
    // can coexist — one per game_session. Lifetime is RAII: instances are minted by i_physics_plugin::instantiate() and destroyed
    // when their owning unique_ref drops
    //
    // Destruction is the only shutdown path. There is no shutdown() method — the destructor releases every body, shape, and
    // Jolt-side allocation
    class i_physics {
    public:
        virtual ~i_physics() = default;

        // Backend-specific escape hatch (e.g. casting to jolt::physics_world).
        // NOT ambiguous with i_physics_plugin::as<>() because callers go through one interface or the other
        template<typename T> requires std::is_polymorphic_v<T>
        [[nodiscard]] T* as() noexcept { return dynamic_cast<T*>(this); }

        template<typename T> requires std::is_polymorphic_v<T>
        [[nodiscard]] const T* as() const noexcept { return dynamic_cast<const T*>(this); }

        // stepping ----------------------------------------------------------------------------------------------------

        virtual void step(f32 delta_time, u32 substeps = 1) = 0;

        [[nodiscard]] virtual glm::vec3 get_gravity() const noexcept = 0;
        virtual void set_gravity(const glm::vec3& gravity) = 0;

        // shapes ------------------------------------------------------------------------------------------------------

        [[nodiscard]] virtual std::expected<shape_handle, physics_error> create_shape(const shape_desc& desc) = 0;
        virtual void release_shape(shape_handle h) noexcept = 0;

        // bodies ------------------------------------------------------------------------------------------------------

        [[nodiscard]] virtual std::expected<body_handle, physics_error> create_body(const body_desc& desc) = 0;
        virtual void destroy_body(body_handle h) noexcept = 0;
        [[nodiscard]] virtual bool is_alive(body_handle h) const noexcept = 0;

        [[nodiscard]] virtual std::optional<body_state> get_body_state(body_handle h) const = 0;
        virtual size_t get_body_states(std::span<const body_handle> bodies, std::span<body_state> out) const = 0;

        // state writes ------------------------------------------------------------------------------------------------

        virtual void set_body_transform(body_handle h, const glm::vec3& pos, const glm::quat& rot) = 0;
        virtual void set_linear_velocity(body_handle h, const glm::vec3& v) = 0;
        virtual void set_angular_velocity(body_handle h, const glm::vec3& w) = 0;
        virtual void translate(body_handle h, const glm::vec3& delta) = 0;
        virtual void rotate(body_handle h, const glm::quat& delta) = 0;

        virtual void set_body_transforms(std::span<const body_handle> bodies, std::span<const glm::vec3> positions,
            std::span<const glm::quat> rotations) = 0;
        virtual void set_linear_velocities(std::span<const body_handle> bodies, std::span<const glm::vec3> velocities) = 0;
        virtual void set_angular_velocities(std::span<const body_handle> bodies, std::span<const glm::vec3> velocities) = 0;

        // forces ------------------------------------------------------------------------------------------------------

        virtual void apply_force(body_handle h, const glm::vec3& force, std::optional<glm::vec3> point = std::nullopt) = 0;
        virtual void apply_impulse(body_handle h, const glm::vec3& impulse, std::optional<glm::vec3> point = std::nullopt) = 0;
        virtual void apply_torque(body_handle h, const glm::vec3& torque) = 0;
        virtual void apply_angular_impulse(body_handle h, const glm::vec3& imp) = 0;

        // activation --------------------------------------------------------------------------------------------------

        virtual void set_active(body_handle h, bool active) = 0;
        [[nodiscard]] virtual bool is_active(body_handle h) const = 0;

        // queries -----------------------------------------------------------------------------------------------------

        [[nodiscard]] virtual ray_cast_hit ray_cast(const glm::vec3& origin, const glm::vec3& direction,
            f32 max_distance = 1e30f, const query_filter& filter = {}) const = 0;

        virtual size_t ray_cast_multi(const glm::vec3& origin, const glm::vec3& direction, f32 max_distance,
            std::span<ray_cast_hit> out, const query_filter& filter = {}) const = 0;

        virtual size_t overlap_sphere(const glm::vec3& center, f32 radius, std::span<overlap_hit> out,
            const query_filter& filter = {}) const = 0;

        virtual size_t overlap_box(const glm::vec3& center, const glm::quat& rotation, const glm::vec3& half_extents,
            std::span<overlap_hit> out, const query_filter& filter = {}) const = 0;

        // contact events ----------------------------------------------------------------------------------------------

        virtual void enable_contact_events(body_handle h, contact_event_mask mask) = 0;
        [[nodiscard]] virtual std::span<const contact_event> drain_contact_events() = 0;

        // stats -------------------------------------------------------------------------------------------------------

        [[nodiscard]] virtual physics_stats get_stats() const noexcept = 0;
    };

}
