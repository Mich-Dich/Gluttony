#pragma once

#include "util/jolt_pch.h"      // self-contained: Jolt types are base classes / members below
#include <physics/i_physics.h>

#include "layers.h"
#include "shape_cache.h"
#include "body_cache.h"
#include "contact_listener.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics::jolt {
    class physics_world;
}

namespace GLT::physics::jolt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief One live Jolt simulation
    //
    // Owns the PhysicsSystem, the body/shape/contact caches, and the layer registries. Construction fully initialises the
    // instance — there is no separate init() step. Destruction releases every body via the body interface (so per-pair
    // contact-end bookkeeping still fires) and drops the PhysicsSystem before the layer registries go out of scope
    //
    // The shared JPH::JobSystemThreadPool is borrowed from the owning plugin. It must outlive every physics_world
    // instance — guaranteed because physics worlds only exist for the lifetime of a game_session, and sessions are torn
    // down before the plugin unloads
    class physics_world final : public GLT::physics::i_physics {
    public:

        physics_world(JPH::JobSystemThreadPool* job_system, const glm::vec3& gravity);
        ~physics_world() override;

        // i_physics ---------------------------------------------------------------------------------------------------

        void step(f32 delta_time, u32 substeps) override;
        [[nodiscard]] glm::vec3 get_gravity() const noexcept override;
        void set_gravity(const glm::vec3& gravity) override;

        [[nodiscard]] std::expected<shape_handle, physics_error> create_shape(const shape_desc& desc) override;
        void release_shape(shape_handle h) noexcept override;

        [[nodiscard]] std::expected<body_handle, physics_error> create_body(const body_desc& desc) override;
        void destroy_body(body_handle h) noexcept override;
        [[nodiscard]] bool is_alive(body_handle h) const noexcept override;

        [[nodiscard]] std::optional<body_state> get_body_state(body_handle h) const override;
        size_t get_body_states(std::span<const body_handle> bodies, std::span<body_state> out) const override;

        void set_body_transform(body_handle h, const glm::vec3& pos, const glm::quat& rot) override;
        void set_linear_velocity(body_handle h, const glm::vec3& v) override;
        void set_angular_velocity(body_handle h, const glm::vec3& w) override;
        void translate(body_handle h, const glm::vec3& delta) override;
        void rotate(body_handle h, const glm::quat& delta) override;

        void set_body_transforms(std::span<const body_handle> bodies, std::span<const glm::vec3> positions,
            std::span<const glm::quat> rotations) override;
        void set_linear_velocities(std::span<const body_handle> bodies, std::span<const glm::vec3> velocities) override;
        void set_angular_velocities(std::span<const body_handle> bodies, std::span<const glm::vec3> velocities) override;

        void apply_force(body_handle h, const glm::vec3& force, std::optional<glm::vec3> point) override;
        void apply_impulse(body_handle h, const glm::vec3& impulse, std::optional<glm::vec3> point) override;
        void apply_torque(body_handle h, const glm::vec3& torque) override;
        void apply_angular_impulse(body_handle h, const glm::vec3& imp) override;

        void set_active(body_handle h, bool active) override;
        [[nodiscard]] bool is_active(body_handle h) const override;

        [[nodiscard]] ray_cast_hit ray_cast(const glm::vec3& origin, const glm::vec3& direction, f32 max_distance,
            const query_filter& filter) const override;
        size_t ray_cast_multi(const glm::vec3& origin, const glm::vec3& direction, f32 max_distance, std::span<ray_cast_hit> out,
            const query_filter& filter) const override;
        size_t overlap_sphere(const glm::vec3& center, f32 radius, std::span<overlap_hit> out, const query_filter& filter) const override;
        size_t overlap_box(const glm::vec3& center, const glm::quat& rotation, const glm::vec3& half_extents, std::span<overlap_hit> out,
            const query_filter& filter) const override;

        void enable_contact_events(body_handle h, contact_event_mask mask) override;
        [[nodiscard]] std::span<const contact_event> drain_contact_events() override;

        [[nodiscard]] physics_stats get_stats() const noexcept override;

    private:

        void wake_if_dynamic(JPH::BodyInterface& bi, JPH::BodyID id);

        // IMPORTANT: declaration order. `m_layers` and `m_object_vs_bp` are captured by reference inside
        // `JPH::PhysicsSystem::Init()` and dereferenced during `Update()` / queries. They must be declared BEFORE `m_system`
        // so they are destroyed AFTER it (reverse-declaration order). Violating this is silent UB, not a compile error
        layer_registry                                  m_layers;
        layer_vs_broadphase                             m_object_vs_bp;

        JPH::JobSystemThreadPool*                       m_job_system{ nullptr };
        glm::vec3                                       m_gravity{ 0.f, -9.81f, 0.f };

        GLT::unique_ref<JPH::TempAllocatorImpl>         m_temp_allocator;
        GLT::unique_ref<JPH::PhysicsSystem>             m_system;
        shape_cache                                     m_shapes;
        body_cache                                      m_bodies;
        contact_listener                                m_contacts{ &m_bodies };

        // Stats
        u32                                             m_substeps_last_frame{ 0 };
        f32                                             m_step_ms_last { 0.f };
        f32                                             m_step_ms_avg  { 0.f };
        f32                                             m_step_ms_accum{ 0.f };
        u32                                             m_step_frame_count{ 0 };
    };

}
