
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

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

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    template<typename T>
    void entity_codec::register_component(std::string_view name) {

        static_assert(std::is_trivially_copyable_v<T>,
            "Components registered with entity_codec must be trivially copyable. "
            "Use register_custom_component<T>() if your component owns heap data.");

        register_custom_component<T>(name,

            [](const entt::registry& r, entt::entity e, std::vector<std::byte>& out) {
                const auto& c = r.get<T>(e);
                const auto* p = reinterpret_cast<const std::byte*>(&c);
                out.insert(out.end(), p, p + sizeof(T));
            },

            [](entt::registry& r, entt::entity e, std::span<const std::byte> data) {
                if (data.size() != sizeof(T))
                    return;   // version skew
                auto& c = r.emplace_or_replace<T>(e);
                std::memcpy(&c, data.data(), sizeof(T));
            });
    }


    template<typename T>
    void entity_codec::register_custom_component(std::string_view name,
        std::function<void(const entt::registry&, entt::entity, std::vector<std::byte>&)> save_fn,
        std::function<void(entt::registry&, entt::entity, std::span<const std::byte>)> load_fn) {

        entry e{
            .type_hash = component_name_hash(name),
            .name = std::string(name),
            .has = [](const entt::registry& r, entt::entity ent) { return r.all_of<T>(ent); },
            .save = std::move(save_fn),
            .load = std::move(load_fn),
        };

        for (auto& existing : m_components) {
            if (existing.type_hash == e.type_hash) {
                existing = std::move(e);
                return;
            }
        }
        m_components.push_back(std::move(e));
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
