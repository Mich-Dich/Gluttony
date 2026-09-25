
#include "util/pch.h"
#include "components.h"

#include <imgui.h>

#include <util/ui/pannel_collection.h>

#include "world.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    void register_transform_descriptor(component_registry& reg);

    void register_mesh_renderer_descriptor(component_registry& reg);

    void register_name_descriptor(component_registry& reg);

    void register_hierarchy_descriptor(component_registry& reg);

    void register_audio_source_descriptor(component_registry& reg);

    void register_no_inherit_transform_descriptor(component_registry& reg);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    void register_transform_descriptor(component_registry& reg) {

        reg.register_component<GLT::world::world_ecs_entt::transform>("glt.transform", "Core",

            [](entity_id id) {

                // The registry handles entt<->entity_id translation in the wrapper, but the draw lambda needs the plugin.
                // Grab it once via a context accessor.
                auto* plugin = GLT::world::manager::get_ref()->as<ecs_world_plugin>();
                if (!plugin)
                    return;

                auto& transform = plugin->registry().get<GLT::world::world_ecs_entt::transform>(plugin->entt_of(id));

                ImGui::PushID("transform");
                GLT::UI::begin_table(GLT::asset::COMPONENT_DATA_TABLE_NAME, false);
                GLT::UI::table_row("position", transform.position);
                GLT::UI::table_row("rotation", transform.rotation);
                GLT::UI::table_row("scale", transform.scale);
                GLT::UI::end_table();
                ImGui::PopID();
            },

            [](entity_id id) {
                // Optional one-line preview.
                auto* plugin = GLT::world::manager::get_ref()->as<ecs_world_plugin>();
                if (!plugin)
                    return std::string{};

                const auto& transform = plugin->registry().get<GLT::world::world_ecs_entt::transform>(plugin->entt_of(id));
                char buf[64];
                std::snprintf(buf, sizeof(buf), "(%.1f, %.1f, %.1f)", transform.position.x, transform.position.y, transform.position.z);
                return std::string(buf);
            });
    }


    void register_mesh_renderer_descriptor(component_registry& reg) {

        reg.register_component<mesh_renderer>("glt.mesh_renderer", "Rendering",

            [](entity_id id) {

                auto* plugin = GLT::world::manager::get_ref()->as<ecs_world_plugin>();
                if (!plugin) return;

                auto& mr = plugin->registry().get<mesh_renderer>(plugin->entt_of(id));

                ImGui::PushID("mesh_renderer");

                // // Asset picker — pulls from the registry, filters to mesh types.
                // GLT::editor::asset_picker("Mesh", mr.mesh, GLT::asset::core_types::static_mesh);

                // GLT::editor::asset_picker("Material Override", mr.material_override, GLT::asset::core_types::material_instance);

                ImGui::Checkbox("Visible", &mr.visible);

                // A button that runs engine logic — this is the "very specific editor functionality" case. 
                // The lambda captures state and calls into whatever runtime API is appropriate.
                if (ImGui::Button("Fit to Bounds")) {
                    // ... reload mesh bounds, apply to sibling transform, etc.
                }

                ImGui::PopID();
            });
    }


    void register_name_descriptor(component_registry& reg) {

        reg.register_component<name_component>("glt.name", "Core",

            [](entity_id id) {

                auto* plugin = GLT::world::manager::get_ref()->as<ecs_world_plugin>();
                if (!plugin)
                    return;

                auto& name_comp = plugin->registry().get<name_component>(plugin->entt_of(id));
                bool enable_input = false;
                GLT::UI::begin_table(GLT::asset::COMPONENT_DATA_TABLE_NAME, false);
                GLT::UI::table_row("name", name_comp.name, enable_input);
                GLT::UI::end_table();
            });

        // [name_component] is structural — the outliner needs it. Prevent removal from the UI (It can still be removed programmatically.)
        // The helper sets can_remove to true; override it directly here. Alternatively pass an `allow_remove` bool to the helper.
    }


    void register_hierarchy_descriptor(component_registry& reg) {

        reg.register_component<hierarchy>("glt.hierarchy", "Core",

            [](entity_id id) {

                // Read-only view. The outliner is the editing surface for hierarchy; showing it here would be redundant
                // and invite the user to corrupt the tree.
                auto* plugin = GLT::world::manager::get_ref()->as<ecs_world_plugin>();
                if (!plugin)
                    return;

                const auto parent = plugin->parent_of(id);
                const auto kids = plugin->children_of(id);

                const auto start_pos = ImGui::GetCursorPosX();
                const float avail = ImGui::GetContentRegionAvail().x;
                ImGui::TextDisabled("Parent: %s", parent.is_valid() ? "(set)" : "(none)");
                ImGui::SameLine();
                ImGui::SetCursorPosX(start_pos + avail/2);
                ImGui::TextDisabled("Children: %zu", kids.size());
            });
    }


    void register_audio_source_descriptor(component_registry& /*reg*/) { }


    void register_no_inherit_transform_descriptor(component_registry& /*reg*/) {  }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // ---- top-level entry point --------------------------------------------------------------------

    void register_all_component_descriptors(component_registry& reg) {

        register_transform_descriptor(reg);
        register_name_descriptor(reg);
        register_hierarchy_descriptor(reg);
        register_mesh_renderer_descriptor(reg);
        register_audio_source_descriptor(reg);
        register_no_inherit_transform_descriptor(reg);
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
