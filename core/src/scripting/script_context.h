
#pragma once

#include "util/pch.h"
#include "scripting/i_script.h"
#include "scripting/i_script_host.h"
#include "world/entity.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::script {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class script_context {
    public:

        // identity / systems

        [[nodiscard]] world::entity_id entity() const noexcept;


        [[nodiscard]] world::i_world* world() noexcept;


        [[nodiscard]] physics::i_physics* physics() noexcept;


        [[nodiscard]] asset::i_asset_registry_plugin* assets() noexcept;


        [[nodiscard]] i_script_host* host() noexcept;

        // flags

        [[nodiscard]] context_flags get_flags() const noexcept;

        // time

        [[nodiscard]] f32 delta_time() const noexcept;

        [[nodiscard]] f32 total_time() const noexcept;


        // A session resource survives the reload of plugins, BUT its destructor runs inside whichever .so instantiated T. Therefore:
        //
        //     - T must be defined in an engine-side module (core or the scripting plugin). Resources with T defined in the game
        //       plugin cannot safely survive a reload and are rejected at creation time
        //
        //     - The scripting plugin tags every resource with its `registrant`. snapshot_and_release(registrant) destroys all
        //       resources whose registrant matches, after the persist snapshot has been taken. Core-created resources
        //       (registrant == "") survive every reload.
        template <typename T>
        [[nodiscard]] T& session_resource(std::string_view key);


        template <typename T, typename Factory>
        [[nodiscard]] T& session_resource(std::string_view key, Factory&& factory);

        // communication

        [[nodiscard]] event_bus_accessor events() noexcept;


        [[nodiscard]] entity_blackboard& blackboard() noexcept;


        template <typename S>
        [[nodiscard]] S* sibling(std::string_view instance_name = {}) noexcept;

        // sleep / wake

        [[nodiscard]] bool is_sleeping() const noexcept;


        void sleep() noexcept;


        void sleep_for(f32 seconds) noexcept;


        void sleep_until_contact() noexcept;


        template <typename E>
        void sleep_until_event() noexcept;


        void wake_self() noexcept;


        template <typename S>
        void wake_sibling(std::string_view instance_name = {}) noexcept;

        // self-control

        void disable_self() noexcept;


        void destroy_self() noexcept;

    private:

        // filled in by the scripting plugin; not exposed to user code
        i_script_host*                                      m_host = nullptr;
        world::entity_id                                    m_entity = world::INVALID_ENTITY;
        context_flags                                       m_flags = 0u;
        f32                                                 m_delta_time = 0.f;
        f32                                                 m_total_time = 0.f;
        std::string_view                                    m_registrant = {};   // passed through to host->session_resource(...)

        friend class script_host;   // construct only from the host
    };

}

#include "scripting/script_context.inl"
