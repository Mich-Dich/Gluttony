
#pragma once

#include "util/pch.h"
#include "world/entity.h"
#include "scripting/i_script.h"
#include "event/event_bus.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {
    class i_world; 
}

namespace GLT::physics {
    class i_physics; 
}

namespace GLT::asset {
    class i_asset_registry_plugin; 
}

namespace GLT::script {

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    using script_instance_id = u32;

    struct script_instance_info {

        script_instance_id                      id;
        world::entity_id                        entity;
        std::string_view                        script_name;
        std::string_view                        instance_name;
        bool                                    enabled;
        bool                                    sleeping;
        u64                                     update_count;
        u64                                     total_update_ns;
        u64                                     last_update_ns;
    };


    struct script_error {

        enum class code : u8 {

            none = 0,
            class_not_registered,
            no_host,
            invalid_entity,
            duplicate_name,
            method_not_found,
            not_callable,
            wrong_kind,
            bad_params,
            script_threw,
            snapshot_version_mismatch,
            plugin_unloaded
        }                                       kind = code::none;

        std::string                             message;
    };


    // what the host needs from its session
    struct script_host_context {

        world::i_world*                         world = nullptr;
        physics::i_physics*                     physics = nullptr;
        event_bus::bus*                         events = nullptr;
        asset::i_asset_registry_plugin*         assets = nullptr;
    };

    // CONSTANTS =======================================================================================================

    inline constexpr script_instance_id         INVALID_SCRIPT_INSTANCE = 0;

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class i_script_host {
    public:

        virtual ~i_script_host() = default;

        // Called by game_session on ctor, world load/unload, and physics create/remove. Must tolerate world == nullptr
        // (globals tick without a world; on_stop must handle a null world during unload_world)
        virtual void set_context(const script_host_context& ctx) = 0;

        // reconciliation ----------------------------------------------------------------------------------------------

        virtual void refresh(world::entity_id entity) = 0;


        virtual void refresh_all() = 0;

        // global scripts ----------------------------------------------------------------------------------------------

        [[nodiscard]] virtual std::expected<script_instance_id, script_error> attach_global(std::string_view script_name,
            std::string_view instance_name) = 0;


        virtual void detach_global(script_instance_id id) = 0;

        // runtime controls --------------------------------------------------------------------------------------------

        virtual void set_enabled(script_instance_id id, bool enabled) = 0;


        virtual void wake(script_instance_id id) = 0;

        // Session-resource access (Phase 4 fully wires this up; declared here so
        // script_context.h's template can compile). Type-erased keyed store:
        // the scripting plugin rejects keys whose registrant does not match the
        // caller's registrant, per the D6 lifetime rule.
        virtual void* session_resource_erased(std::string_view key, u32 type_hash, u32 size, u32 alignment,
            void (*construct)(void* out, void* factory), void* factory, std::string_view registrant) = 0;

        // one-shot / live calls ---------------------------------------------------------------------------------------

        [[nodiscard]] virtual std::expected<void, script_error> run_action(std::string_view script_name,
            world::entity_id context = world::INVALID_ENTITY, std::span<const std::byte> params = {}) = 0;


        [[nodiscard]] virtual std::expected<void, script_error> call(std::string_view script_name, std::string_view method_name,
            world::entity_id entity, std::string_view instance_name = {}, std::span<const std::byte> params = {}) = 0;


        virtual void run_world_generators() = 0;


        virtual void update(f32 delta_time) = 0;

        [[nodiscard]] virtual std::span<const script_instance_info> instances() const = 0;

        // siblings ----------------------------------------------------------------------------------------------------

        virtual void* find_sibling_erased(std::string_view class_name, world::entity_id entity, std::string_view instance_name,
            std::string_view registrant) = 0;


        virtual void sleep_until_event_erased(u32 event_type_hash) = 0;


        virtual void wake_sibling_erased(std::string_view class_name, world::entity_id entity, std::string_view instance_name) = 0;
                                        
        // hot reload (D3) ---------------------------------------------------------------------------------------------

        // Phase 1 of a reload: call on_stop on every affected slot and mark the
        // slot stopped, so the unload path does not call on_stop a second time
        virtual void prepare_hot_reload(std::string_view registrant = {}) = 0;


        // Non-destructive snapshot (used by the save-file system, future)
        [[nodiscard]] virtual std::expected<std::vector<std::byte>, script_error> snapshot_state(std::string_view registrant = {}) const = 0;


        [[nodiscard]] virtual std::expected<std::vector<std::byte>, script_error> snapshot_and_release(std::string_view registrant = {}) = 0;


        // Restore from a snapshot produced by snapshot_state / snapshot_and_release
        virtual u32 restore_state(std::span<const std::byte> snapshot) = 0;

    };

}
