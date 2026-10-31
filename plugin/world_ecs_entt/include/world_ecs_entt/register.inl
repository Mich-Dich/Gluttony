#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::component_registry {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    template<typename T>
    component::type_descriptor make_descriptor(std::string_view name, std::string_view category,
        std::function<void(GLT::world::i_world&, entity_id)> draw, std::function<std::string(GLT::world::i_world&, entity_id)> summary,
        std::vector<u64> required) {

        const u64 hash = hash_of(name);
        g_type_hash<T> = hash;

        component::type_descriptor td{};
        td.meta.hash = hash;
        td.meta.name = std::string(name);
        td.meta.category = std::string(category);
        td.meta.required = std::move(required);
        td.draw = std::move(draw);
        td.summary = std::move(summary);

        td.make_storage = [](GLT::world::i_world& world) -> GLT::unique_ref<component::i_storage> {
            return GLT::create_unique_ref<world_ecs_entt::component_storage<T>>(
                static_cast<world_ecs_entt::ecs_world&>(world));
        };

        return td;
    }


    template<typename T>
    void add(std::string_view name, std::string_view category, std::function<void(GLT::world::i_world&, GLT::world::entity_id)> draw,
        std::function<std::string(GLT::world::i_world&, GLT::world::entity_id)> summary, std::vector<u64> required) {

        auto td = make_descriptor<T>(name, category, std::move(draw), std::move(summary), std::move(required));
        register_meta(td.meta);

        if (auto* plugin = GLT::world::manager::get_ptr()) {

            plugin->register_component_type(td);

            if constexpr (std::is_trivially_copyable_v<T>) {
                if (auto* entt = dynamic_cast<world_ecs_entt::ecs_world_plugin*>(plugin))
                    entt->register_codec_component<T>(name);
            }
        }
    }


    template<typename T>
    bool remove_type() {

        const u64 hash = g_type_hash<T>;
        if (hash == 0)
            return false;

        unregister_meta(hash);

        if (auto* plugin = GLT::world::manager::get_ptr())
            return plugin->unregister_component_type(hash);

        return true;
    }

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
