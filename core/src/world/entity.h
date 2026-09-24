
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Stable, portable entity handle. The index/generation split lets us detect
    // stale references after despawn without ever reusing a handle value.
    //
    // This is the ONLY entity type that crosses the plugin boundary. Concrete
    // ECS implementations map it to their internal handle (entt::entity, ...).
    struct entity_id {

        u32                                     index = 0;
        u32                                     generation = 0;

        constexpr bool operator==(const entity_id&) const noexcept = default;
        constexpr auto operator<=>(const entity_id&) const noexcept = default;

        [[nodiscard]] constexpr bool is_valid() const noexcept { return generation != 0; }
    };

    // CONSTANTS =======================================================================================================

    inline constexpr entity_id                  INVALID_ENTITY{ 0, 0 };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

namespace std {

    template<> struct hash<GLT::world::entity_id> {
        size_t operator()(const GLT::world::entity_id& e) const noexcept {
            return (static_cast<size_t>(e.generation) << 32) | e.index;
        }
    };

}
