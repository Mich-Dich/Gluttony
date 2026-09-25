
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

        if constexpr (std::is_empty_v<T>) {

            // Tag component (e.g. no_inherit_transform): presence is the payload.
            // EnTT's get<T>() returns void for empty types, so we can't take its
            // address or memcpy it — the wire format is simply "zero bytes".
            register_custom_component<T>(name,
                [](const entt::registry&, entt::entity, std::vector<std::byte>&) { /*no payload*/ },
                [](entt::registry& registry, entt::entity entity, std::span<const std::byte>) { registry.emplace_or_replace<T>(entity); });

        } else {

            register_custom_component<T>(name,

                [](const entt::registry& registry, entt::entity entity, std::vector<std::byte>& out) {
                    const auto& comp = registry.get<T>(entity);
                    const auto* as_bytes = reinterpret_cast<const std::byte*>(&comp);
                    out.insert(out.end(), as_bytes, as_bytes + sizeof(T));
                },

                [](entt::registry& registry, entt::entity entity, std::span<const std::byte> data) {
                    if (data.size() != sizeof(T))
                        return;   // version skew
                    auto& comp = registry.emplace_or_replace<T>(entity);
                    std::memcpy(&comp, data.data(), sizeof(T));
                });
        }
    }


    template<typename T>
    void entity_codec::register_custom_component(std::string_view name,
        std::function<void(const entt::registry&, entt::entity, std::vector<std::byte>&)> save_fn,
        std::function<void(entt::registry&, entt::entity, std::span<const std::byte>)> load_fn) {

        entry loc_entry{
            .type_hash = component_name_hash(name),
            .name = std::string(name),
            .has = [](const entt::registry& registry, entt::entity entity) { return registry.all_of<T>(entity); },
            .save = std::move(save_fn),
            .load = std::move(load_fn),
        };

        for (auto& existing : m_components) {
            if (existing.type_hash == loc_entry.type_hash) {

                existing = std::move(loc_entry);
                return;
            }
        }
        m_components.push_back(std::move(loc_entry));
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
