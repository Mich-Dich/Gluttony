#pragma once

#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <glm/glm.hpp>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief One property value attached to a [component_spec]
    //
    // Presets are data, not code - core doesn't know what "directional light" means. The world plugin decides how to
    // translate a property bag into its concrete component types. A tagged variant keeps that bag typed enough that the
    // world plugin can validate ("light type must be a string") and the editor could one day surface these in a UI.
    using property_value = std::variant<bool, i64, f64, glm::vec2, glm::vec3, glm::vec4, glm::quat, std::string>;


    // @brief A single component to instantiate on the preset entity
    //
    // [name] is the short component name used by the world plugin's descriptor table ("light", "camera", ...). For
    // user components, whatever name the user plugin registers with the world plugin's preset factory map
    struct component_spec {

        std::string                                         name;
        std::vector<std::pair<std::string, property_value>> properties;

        [[nodiscard]] const property_value* get(std::string_view key) const noexcept {
            for (const auto& [k, v] : properties) if (k == key) return &v;
            return nullptr;
        }

        // Typed set helpers - avoid the "int literal collapses to bool" trap of std::variant

        component_spec& set_bool(std::string_view key, bool v) { properties.emplace_back(std::string(key), v); return *this; }

        component_spec& set_int(std::string_view key, i64 v) { properties.emplace_back(std::string(key), v); return *this; }

        component_spec& set_number(std::string_view key, f64 v) { properties.emplace_back(std::string(key), v); return *this; }

        component_spec& set_vec2(std::string_view key, const glm::vec2& v) { properties.emplace_back(std::string(key), v); return *this; }

        component_spec& set_vec3(std::string_view key, const glm::vec3& v) { properties.emplace_back(std::string(key), v); return *this; }

        component_spec& set_vec4(std::string_view key, const glm::vec4& v) { properties.emplace_back(std::string(key), v); return *this; }

        component_spec& set_quat(std::string_view key, const glm::quat& v) { properties.emplace_back(std::string(key), v); return *this; }

        component_spec& set_string(std::string_view key, std::string v) { properties.emplace_back(std::string(key), std::move(v)); return *this; }
    };


    // @brief A template for a single entity (recursively may contain children)
    //
    // Presets describe *intent*, not implementation: "this entity is a directional light named Sun with a warm color". The
    // concrete world plugin translates that intent into real ECS components. That indirection is what lets future plugins
    // contribute presets without knowing the engine's ECS.
    struct entity_preset {

        std::string                                         name;                       // Display + registry key. "Sun", "Empty", ...
        std::string                                         entity_name;                // Name given to the created entity. Empty = use [name]
        std::string                                         icon;                       // icon_manager key
        std::string                                         category;                   // Menu grouping. "" = ungrouped
        std::string                                         tooltip;

        glm::vec3                                           position{ 0.f };
        glm::vec3                                           rotation{ 0.f };            // Euler, radians (matches component::transform)
        glm::vec3                                           scale{ 1.f };

        // Components to apply to the *root* entity. `transform` and `name` are handled implicitly from the fields above.
        // For an entity that needs N of the same single-component (e.g. 3 meshes) use children - see below.
        std::vector<component_spec>                         components;

        // Recursive children. Each is spawned as a child of the parent entity. Use this whenever a preset needs more than
        // one of a singleton component (multiple meshes, multiple lights, ...)
        std::vector<entity_preset>                          children;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // @brief Registers the built-in presets every plugin can rely on (Empty, Sun, ...)
    //
    // Idempotent - safe to call from every world plugin's on_load(). Extend by adding entries here (engine presets) or by
    // calling preset_registry::get().add() from a user plugin
    void register_core_presets();

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Process-wide registry of entity presets
    //
    // Presets are pure data. Nothing here interprets them - that's the world plugin's job. Registration is expected at
    // startup (plugin on_load / core init), reading from the main thread during editor UI. Not thread-safe against
    // concurrent add/remove because it doesn't need to be: register all presets before any UI iterates them
    class preset_registry {
    public:

        static preset_registry& get() noexcept {
            static preset_registry r;
            return r;
        }

        // No-op if a preset with the same name already exists (idempotent for re-registration across plugin reloads)
        void add(entity_preset preset);

        bool remove(std::string_view name);

        [[nodiscard]] const entity_preset* find(std::string_view name) const noexcept;

        // Stable reference. Valid until the next add()/remove()
        [[nodiscard]] const std::vector<entity_preset>& all() const noexcept { return m_presets; }

    private:

        std::vector<entity_preset>                          m_presets;
    };

}
