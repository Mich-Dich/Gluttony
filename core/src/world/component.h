
#pragma once

#include "world/entity.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {
    class i_world;
}

namespace GLT::world::component {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct component_type_meta {

        u64                                                                 hash{};
        std::string                                                         name;
        std::string                                                         category;
        std::vector<u64>                                                    required;
    };


    // One typed slot, type-erased. Everything the world plugin and the editor need to touch a component goes through this
    // The plugin never names T; the storage that T's TU instantiates does
    class i_storage {
    public:
        virtual ~i_storage() = default;

        [[nodiscard]] virtual bool has(entity_id id) const = 0;

        virtual void add(entity_id id) = 0;

        virtual void remove(entity_id id) = 0;

        [[nodiscard]] virtual void* get(entity_id id) = 0;

        [[nodiscard]] virtual const void* get(entity_id id) const = 0;

        virtual void copy(entity_id from, entity_id to) = 0;

        [[nodiscard]] virtual std::vector<entity_id> entities() const = 0;

        virtual void save(entity_id id, std::vector<std::byte>& out) const = 0;

        virtual void load(entity_id id, std::span<const std::byte> in) = 0;
    };

    // ---- registration payload ---------------------------------------------------------------------------------------

    // What add<T>() hands to i_world_plugin::register_component_type(). draw/summary take i_world& so the plugin can
    // hook them to whichever live world instance is rendering. make_storage instantiates the concrete i_storage<T>
    // inside the plugin's TU (templates can't cross the DLL boundary otherwise).
    struct type_descriptor {

        component_type_meta                                                 meta;
        std::function<void(GLT::world::i_world&, entity_id)>                draw;
        std::function<std::string(GLT::world::i_world&, entity_id)>         summary;
        std::function<GLT::unique_ref<i_storage>(GLT::world::i_world&)>     make_storage;
    };


    // ---- per-world installed view -----------------------------------------------------------------------------------

    // What the world's component_registry stores and what i_world_inspector exposes. Every entity-level op is bound to
    // a specific world at install time. Flat shape so the editor can iterate descriptors without knowing about the
    // registration split.
    struct descriptor {

        u64                                                                 hash{};
        std::string                                                         name;
        std::string                                                         category;
        std::vector<u64>                                                    required;
        std::function<bool(entity_id)>                                      has;
        std::function<void(entity_id)>                                      add;
        std::function<void(entity_id)>                                      remove;
        std::function<bool(entity_id)>                                      can_remove;
        std::function<void(entity_id)>                                      draw;                // world already captured
        std::function<std::string(entity_id)>                               summary;             // world already captured
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
