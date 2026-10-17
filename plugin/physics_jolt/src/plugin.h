
#pragma once

#include <physics/i_physics_plugin.h>

#include "layers.h"
#include "shape_cache.h"
#include "body_cache.h"
#include "contact_listener.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics::jolt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class plugin final : public GLT::physics::i_physics_plugin {
    public:
        plugin();
        ~plugin() override;

        // i_plugin ----------------------------------------------------------------------------------------------------

        void on_load()   override;
        void on_unload() override;

        // identity ----------------------------------------------------------------------------------------------------

        [[nodiscard]] std::string_view backend_name() const noexcept override;

        // lifecycle ---------------------------------------------------------------------------------------------------

        std::expected<void, physics_error> init(const glm::vec3& gravity) override;
        void shutdown() noexcept override;

        // stepping ----------------------------------------------------------------------------------------------------

        void step(f32 delta_time, u32 substeps) override;
        [[nodiscard]] glm::vec3 get_gravity() const noexcept override;
        void set_gravity(const glm::vec3& gravity) override;

        // shapes ------------------------------------------------------------------------------------------------------

        [[nodiscard]] std::expected<shape_handle, physics_error> create_shape(const shape_desc& desc) override;
        void release_shape(shape_handle h) noexcept override;

        // bodies ------------------------------------------------------------------------------------------------------

        [[nodiscard]] std::expected<body_handle, physics_error> create_body(const body_desc& desc) override;
        void destroy_body(body_handle h) noexcept override;
        [[nodiscard]] bool is_alive(body_handle h) const noexcept override;

        [[nodiscard]] std::optional<body_state> get_body_state(body_handle h) const override;
        size_t get_body_states(std::span<const body_handle> bodies, std::span<body_state> out) const override;

        // state writes ------------------------------------------------------------------------------------------------

        void set_body_transform(body_handle h, const glm::vec3& pos, const glm::quat& rot) override;
        void set_linear_velocity(body_handle h, const glm::vec3& v) override;
        void set_angular_velocity(body_handle h, const glm::vec3& w) override;
        void translate(body_handle h, const glm::vec3& delta) override;
        void rotate(body_handle h, const glm::quat& delta) override;

        void set_body_transforms(std::span<const body_handle> bodies, std::span<const glm::vec3> positions, 
            std::span<const glm::quat> rotations) override;
        void set_linear_velocities(std::span<const body_handle> bodies, std::span<const glm::vec3> velocities) override;
        void set_angular_velocities(std::span<const body_handle> bodies, std::span<const glm::vec3> velocities) override;

        // forces ------------------------------------------------------------------------------------------------------

        void apply_force(body_handle h, const glm::vec3& force, std::optional<glm::vec3> point) override;
        void apply_impulse(body_handle h, const glm::vec3& impulse, std::optional<glm::vec3> point) override;
        void apply_torque(body_handle h, const glm::vec3& torque) override;
        void apply_angular_impulse(body_handle h, const glm::vec3& imp) override;

        // activation --------------------------------------------------------------------------------------------------

        void set_active(body_handle h, bool active) override;
        [[nodiscard]] bool is_active(body_handle h) const override;

        // queries -----------------------------------------------------------------------------------------------------

        [[nodiscard]] ray_cast_hit ray_cast(const glm::vec3& origin, const glm::vec3& direction, f32 max_distance, 
            const query_filter& filter) const override;
        size_t ray_cast_multi(const glm::vec3& origin, const glm::vec3& direction, f32 max_distance, std::span<ray_cast_hit> out, 
            const query_filter& filter) const override;
        size_t overlap_sphere(const glm::vec3& center, f32 radius, std::span<overlap_hit> out, const query_filter& filter) const override;
        size_t overlap_box(const glm::vec3& center, const glm::quat& rotation, const glm::vec3& half_extents, std::span<overlap_hit> out,
            const query_filter& filter) const override;

        // contact events ----------------------------------------------------------------------------------------------

        void enable_contact_events(body_handle h, contact_event_mask mask) override;
        [[nodiscard]] std::span<const contact_event> drain_contact_events() override;

        // stats -------------------------------------------------------------------------------------------------------

        [[nodiscard]] physics_stats get_stats() const noexcept override;

    private:

        // Internal: activate dynamic bodies only. Static/kinematic can't be woken, and Jolt's ActivateBody silently skips them anyway,
        // but we pay a branch to keep the debug builds clean
        void wake_if_dynamic(JPH::BodyInterface& bi, JPH::BodyID id);


        glm::vec3                                       m_gravity{ 0.f, -9.81f, 0.f };
        bool                                            m_initialized{ false };

        GLT::unique_ref<JPH::TempAllocatorImpl>         m_temp_allocator;
        GLT::unique_ref<JPH::JobSystemThreadPool>       m_job_system;
        GLT::unique_ref<JPH::PhysicsSystem>             m_system;
        layer_registry                                  m_layers;
        layer_vs_broadphase                             m_object_vs_bp;
        shape_cache                                     m_shapes;
        body_cache                                      m_bodies;
        contact_listener                                m_contacts{ &m_bodies };

        // Stats — stubbed values for now; wired to the real tables later
        u32                                             m_substeps_last_frame{ 0 };
        f32                                             m_step_ms_last{ 0.f };
        f32                                             m_step_ms_avg { 0.f };
        f32                                             m_step_ms_accum{ 0.f };
        u32                                             m_step_frame_count{ 0 };

    };

}
