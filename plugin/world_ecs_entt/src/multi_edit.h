#pragma once

#include <world/entity.h>

#include "ecs_world.h"
#include "world_ecs_entt/components.h"



// FORWARD DECLARATIONS ================================================================================================

// Editing several entities at once (component::i_storage::capture / apply_changes): how the difference between a
// component "before" and "now" is carried over to the same component on another entity.
namespace GLT::world::world_ecs_entt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // Plain structs: compares [before] and [now] in steps of four bytes (the size of the floats, integers and enums
    // components are made of) and copies the steps that differ to [target]. Returns true if [target] changed.
    // A field of eight bytes would be copied in halves; components with such fields need their own multi_edit<T>
    [[nodiscard]] inline bool apply_changed_words(const void* before, const void* now, void* target, size_t size);

    // TEMPLATE DECLARATION ============================================================================================

    // How component T takes part. The primary template covers plain data; a specialisation handles a type that owns
    // resources or needs work after a direct write:
    //
    //      template<> struct multi_edit<my_component> {
    //          static constexpr bool supported = true;
    //          static bool apply(ecs_world& world, entity_id target_id, const my_component& before, const my_component& now, my_component& target);
    //      };
    template<typename T>
    struct multi_edit {

        static constexpr bool supported = std::is_trivially_copyable_v<T> && !std::is_empty_v<T>;

        static bool apply(ecs_world&, entity_id, const T& before, const T& now, T& target) {
            return apply_changed_words(&before, &now, &target, sizeof(T));
        }
    };


    // Runtime handles of the physics backend: nothing an editor edits (the settings are on the mesh component)
    template<>
    struct multi_edit<component::physics_body> {

        static constexpr bool supported = false;
        static bool apply(ecs_world&, entity_id, const component::physics_body&, const component::physics_body&, component::physics_body&) { return false; }
    };


    template<>
    struct multi_edit<component::mesh> {

        static constexpr bool supported = true;

        static bool apply(ecs_world& world, entity_id target_id, const component::mesh& before, const component::mesh& now, component::mesh& target) {

            bool changed = false;
            bool body_changed = false;

            if (before.mesh.get() != now.mesh.get() && target.mesh.get() != now.mesh.get()) {
                target.mesh = now.mesh;
                target.material_overrides.clear();              // they belonged to the submeshes of the old mesh
                changed = body_changed = true;
            }

            const auto same_overrides = [](const component::mesh& a, const component::mesh& b) {
                return a.material_overrides.size() == b.material_overrides.size()
                    && std::equal(a.material_overrides.begin(), a.material_overrides.end(), b.material_overrides.begin(),
                        [](const GLT::asset::asset_ref& x, const GLT::asset::asset_ref& y) { return x.get() == y.get(); });
            };
            if (!same_overrides(before, now) && !same_overrides(target, now)) {

                // One slot per submesh: the list only fits a target with the same mesh. "No overrides" fits everything
                if (now.material_overrides.empty() || target.mesh.get() == now.mesh.get()) {
                    target.material_overrides = now.material_overrides;
                    changed = true;
                }
            }

            if (before.visible != now.visible && target.visible != now.visible) {
                target.visible = now.visible;
                changed = true;
            }
            if (before.motion != now.motion && target.motion != now.motion) {
                target.motion = now.motion;
                changed = body_changed = true;
            }
            if (before.use_mesh_material != now.use_mesh_material && target.use_mesh_material != now.use_mesh_material) {
                target.use_mesh_material = now.use_mesh_material;
                changed = body_changed = true;
            }
            if (apply_changed_words(&before.physics_material, &now.physics_material, &target.physics_material, sizeof(target.physics_material)))
                changed = body_changed = true;

            if (body_changed && world.get_physics())
                world.rebuild_physics_body(target_id);          // what the component's own editor does after such a change
            return changed;
        }
    };


    template<>
    struct multi_edit<component::audio_source> {

        static constexpr bool supported = true;

        static bool apply(ecs_world&, entity_id, const component::audio_source& before, const component::audio_source& now,
            component::audio_source& target) {

            bool changed = false;
            if (before.clip.get() != now.clip.get() && target.clip.get() != now.clip.get()) {
                target.clip = now.clip;
                changed = true;
            }
            if constexpr (std::is_trivially_copyable_v<GLT::asset::audio::source_config>)
                changed |= apply_changed_words(&before.config, &now.config, &target.config, sizeof(target.config));
            if (before.autoplay != now.autoplay && target.autoplay != now.autoplay) {
                target.autoplay = now.autoplay;
                changed = true;
            }
            return changed;
        }
    };

    // CLASS DECLARATION ===============================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    inline bool apply_changed_words(const void* before, const void* now, void* target, const size_t size) {

        const auto* a = static_cast<const std::byte*>(before);
        const auto* b = static_cast<const std::byte*>(now);
        auto* out = static_cast<std::byte*>(target);

        bool changed = false;
        for (size_t offset = 0; offset < size; offset += 4) {
            const size_t count = std::min<size_t>(4, size - offset);
            if (std::memcmp(a + offset, b + offset, count) == 0)
                continue;                                       // this field was not touched
            if (std::memcmp(out + offset, b + offset, count) == 0)
                continue;
            std::memcpy(out + offset, b + offset, count);
            changed = true;
        }
        return changed;
    }

}
