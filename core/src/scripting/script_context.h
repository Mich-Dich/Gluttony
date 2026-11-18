
#pragma once

#include "util/pch.h"
#include "scripting/i_script.h"
#include "scripting/i_script_host.h"
#include "world/entity.h"
#include "event/event_bus.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::script {

    // CLASS DECLARATION ===============================================================================================

    // @brief Everything a script callback may use. Built by the host for each call: never keep it past the callback
    //
    // All functions are inline (they read the host's environment or call the host's virtuals), so a game module needs
    // no symbol of the scripting plugin
    class script_context {
    public:

        // identity / systems
        [[nodiscard]] world::entity_id entity() const noexcept                  { return m_entity; }
        [[nodiscard]] world::i_world* world() noexcept                          { return m_env ? m_env->world : nullptr; }
        [[nodiscard]] physics::i_physics* physics() noexcept                    { return m_env ? m_env->physics : nullptr; }
        [[nodiscard]] asset::i_asset_registry_plugin* assets() noexcept         { return m_env ? m_env->assets : nullptr; }
        [[nodiscard]] i_script_host* host() noexcept                            { return m_host; }
        [[nodiscard]] script_instance_id instance() const noexcept              { return m_instance; }

        // flags
        [[nodiscard]] context_flags get_flags() const noexcept                  { return m_flags; }

        // time
        [[nodiscard]] f32 delta_time() const noexcept                           { return m_delta_time; }
        [[nodiscard]] f32 total_time() const noexcept                           { return m_total_time; }

        // A session resource survives the reload of modules, BUT its destructor runs inside whichever module instantiated
        // T. The host destroys a resource when the module that created it unloads (release_registrant), so T may be
        // defined in the module; it then does not survive that module's reload. Resources whose T is defined engine side
        // and that are created by engine code (registrant "") survive every reload
        template <typename T>
        [[nodiscard]] T& session_resource(std::string_view key);

        template <typename T, typename Factory>
        [[nodiscard]] T& session_resource(std::string_view key, Factory&& factory);

        // communication: the session's event bus (null when the host has none). Subscriptions must be scoped
        // (subscription guard in a [[=transient]] member): the bus outlives the script
        [[nodiscard]] event_bus::bus* events() noexcept                         { return m_env ? m_env->events : nullptr; }

        // The first instance of class S on the same entity (or the one named [instance_name]); null if there is none or
        // it belongs to another module
        template <typename S>
        [[nodiscard]] S* sibling(std::string_view instance_name = {}) noexcept;

        // sleep / wake: take effect after the current callback returns. A sleeping script gets no on_update / on_contact
        [[nodiscard]] bool is_sleeping() const noexcept                         { return m_host && m_host->is_sleeping(m_instance); }
        void sleep() noexcept                                                   { control(i_script_host::control_op::sleep); }
        void sleep_for(f32 seconds) noexcept                                    { control(i_script_host::control_op::sleep_for, seconds); }
        // Contacts are not routed to scripts yet: until then this sleeps until woken
        void sleep_until_contact() noexcept                                     { control(i_script_host::control_op::sleep_until_contact); }
        template <typename E>
        void sleep_until_event() noexcept;
        void wake_self() noexcept                                               { control(i_script_host::control_op::wake); }
        template <typename S>
        void wake_sibling(std::string_view instance_name = {}) noexcept;

        // self-control
        // disable_self: no more on_update / on_contact until host->set_enabled(id, true)
        void disable_self() noexcept                                            { control(i_script_host::control_op::disable); }
        // destroy_self: entity script: despawns its entity (and so every script on it); global: detaches itself
        void destroy_self() noexcept                                            { control(i_script_host::control_op::destroy); }

    private:

        void control(i_script_host::control_op op, f32 seconds = 0.0f) noexcept {
            if (m_host)
                m_host->control(m_instance, op, seconds);
        }

        // filled in by the host (script_context_access); not exposed to user code
        i_script_host*                                      m_host = nullptr;
        const script_host_context*                          m_env = nullptr;
        script_instance_id                                  m_instance = INVALID_SCRIPT_INSTANCE;
        world::entity_id                                    m_entity = world::INVALID_ENTITY;
        context_flags                                       m_flags = 0u;
        f32                                                 m_delta_time = 0.f;
        f32                                                 m_total_time = 0.f;
        std::string_view                                    m_registrant = {};   // module of the calling script

        friend struct script_context_access;
    };


    // @brief The one way to fill a script_context. For script hosts (scripting plugins), not for scripts
    struct script_context_access {

        struct fields {
            i_script_host*                                  host = nullptr;
            const script_host_context*                      env = nullptr;
            script_instance_id                              instance = INVALID_SCRIPT_INSTANCE;
            world::entity_id                                entity = world::INVALID_ENTITY;
            context_flags                                   flags = 0u;
            f32                                             delta_time = 0.f;
            f32                                             total_time = 0.f;
            std::string_view                                registrant = {};
        };

        [[nodiscard]] static script_context make(const fields& f) noexcept {
            script_context ctx{};
            ctx.m_host = f.host;
            ctx.m_env = f.env;
            ctx.m_instance = f.instance;
            ctx.m_entity = f.entity;
            ctx.m_flags = f.flags;
            ctx.m_delta_time = f.delta_time;
            ctx.m_total_time = f.total_time;
            ctx.m_registrant = f.registrant;
            return ctx;
        }
    };

}

#include "scripting/script_context.inl"
