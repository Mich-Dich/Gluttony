
#include "util/pch.h"
#include "components.h"

#include <imgui.h>

#include <util/ui/pannel_collection.h>
#include <util/ui/asset_picker.h>
#include <asset/audio.h>

#include "world.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt::component {

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

    void register_camera_descriptor(component_registry& reg);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    void register_transform_descriptor(component_registry& reg) {

        reg.register_component<GLT::world::world_ecs_entt::component::transform>("transform", "Core",

            [](entity_id id) {

                // The registry handles entt<->entity_id translation in the wrapper, but the draw lambda needs the plugin
                // Grab it once via a context accessor
                auto* plugin = GLT::world::manager::get_ref()->as<ecs_world_plugin>();
                if (!plugin)
                    return;

                auto& transform = plugin->registry().get<GLT::world::world_ecs_entt::component::transform>(plugin->entt_of(id));

                ImGui::PushID("transform");
                GLT::UI::begin_table(GLT::asset::COMPONENT_DATA_TABLE_NAME, false);
                GLT::UI::table_row("position", transform.position);
                GLT::UI::table_row("rotation", transform.rotation);
                GLT::UI::table_row("scale", transform.scale);
                GLT::UI::end_table();
                ImGui::PopID();
            },

            [](entity_id id) {
                // Optional one-line preview
                auto* plugin = GLT::world::manager::get_ref()->as<ecs_world_plugin>();
                if (!plugin)
                    return std::string{};

                const auto& transform = plugin->registry().get<GLT::world::world_ecs_entt::component::transform>(plugin->entt_of(id));
                char buf[64];
                std::snprintf(buf, sizeof(buf), "(%.1f, %.1f, %.1f)", transform.position.x, transform.position.y, transform.position.z);
                return std::string(buf);
            });
    }


    void register_mesh_renderer_descriptor(component_registry& reg) {

        reg.register_component<component::mesh>("mesh", "Rendering",

            [](entity_id id) {

                auto* plugin = GLT::world::manager::get_ref()->as<ecs_world_plugin>();
                if (!plugin)
                    return;

                auto& mr = plugin->registry().get<component::mesh>(plugin->entt_of(id));

                ImGui::PushID("mesh");
                
                GLT::UI::begin_table(GLT::asset::COMPONENT_DATA_TABLE_NAME, false);
                GLT::UI::table_row_asset_picker("Mesh", mr.mesh, GLT::asset::core_types::static_mesh);
                GLT::UI::table_row_asset_picker("Material Override", mr.material_override, GLT::asset::core_types::material_instance);
                GLT::UI::table_row("visible", mr.visible);
                GLT::UI::end_table();

                ImGui::PopID();
            },

            {},     // no summary

            // mesh has no meaning without a transform to place it in the world
            { component_name_hash("transform") });
    }


    void register_name_descriptor(component_registry& reg) {

        reg.register_component<component::name>("name", "Core",

            [](entity_id id) {

                auto* plugin = GLT::world::manager::get_ref()->as<ecs_world_plugin>();
                if (!plugin)
                    return;

                auto& name_comp = plugin->registry().get<component::name>(plugin->entt_of(id));
                static bool enable_input = false;
                GLT::UI::begin_table(GLT::asset::COMPONENT_DATA_TABLE_NAME, false);
                GLT::UI::table_row("name", name_comp.name, enable_input);
                GLT::UI::end_table();
            });

        // [name] is structural - the outliner uses it. Prevent removal from the UI (It can still be removed programmatically.)
        // The helper sets can_remove to true; override it directly here
    }


    void register_hierarchy_descriptor(component_registry& /*reg*/) {

        // reg.register_component<hierarchy>("hierarchy", "Core",

        //     [](entity_id id) {

        //         // Read-only view. The outliner is the editing surface for hierarchy; showing it here would be redundant
        //         // and invite the user to corrupt the tree
        //         auto* plugin = GLT::world::manager::get_ref()->as<ecs_world_plugin>();
        //         if (!plugin)
        //             return;

        //         const auto parent = plugin->parent_of(id);
        //         const auto kids = plugin->children_of(id);

        //         const auto start_pos = ImGui::GetCursorPosX();
        //         const float avail = ImGui::GetContentRegionAvail().x;
        //         ImGui::TextDisabled("Parent: %s", parent.is_valid() ? "(set)" : "(none)");
        //         ImGui::SameLine();
        //         ImGui::SetCursorPosX(start_pos + avail/2);
        //         ImGui::TextDisabled("Children: %zu", kids.size());
        //     });
    }


    void register_audio_source_descriptor(component_registry& reg) {

        reg.register_component<component::audio_source>("audio source", "Audio",

            [](entity_id id) {

                auto* plugin = GLT::world::manager::get_ref()->as<ecs_world_plugin>();
                if (!plugin)
                    return;

                auto& audio = plugin->registry().get<component::audio_source>(plugin->entt_of(id));

                ImGui::PushID("audio_source");
                GLT::UI::begin_table(GLT::asset::COMPONENT_DATA_TABLE_NAME, false);
                GLT::UI::table_row_asset_picker("clip", audio.clip, GLT::asset::core_types::audio);
                GLT::UI::table_row("autop play", audio.autoplay);
                GLT::UI::table_row("volume", audio.config.volume);
                GLT::UI::table_row("pan", audio.config.pan, 0.01f, -1.f, 1.f);
                GLT::UI::table_row("loop", audio.config.loop);
                GLT::UI::table_row("play speed", audio.config.play_speed);
                GLT::UI::table_row("is 3d", audio.config.is_3d);
                GLT::UI::table_row_slider("position", audio.config.position);
                GLT::UI::table_row_slider("velocity", audio.config.velocity);
                GLT::UI::table_row("min distance", audio.config.min_distance, .01f, 0.f, 1000.f);
                GLT::UI::table_row("max distance", audio.config.max_distance, .01f, 0.f, 1000.f);
                GLT::UI::table_row("rolloff factor", audio.config.rolloff_factor, .01f, 0.f, 1000.f);
                GLT::UI::table_row("attenuation factor", audio.config.attenuation, GLT::util::enum_values<GLT::asset::audio::attenuation_model>);
                GLT::UI::end_table();
                ImGui::PopID();

                audio.config.min_distance = GLT::math::clamp(audio.config.min_distance, 0.f, audio.config.max_distance);               
            });
    }


    void register_no_inherit_transform_descriptor(component_registry& /*reg*/) {  }


    void register_camera_descriptor(component_registry& reg) {

        reg.register_component<component::camera>("camera", "Rendering",

            [](entity_id id) {

                auto* plugin = GLT::world::manager::get_ref()->as<ecs_world_plugin>();
                if (!plugin)
                    return;

                auto& cam = plugin->registry().get<component::camera>(plugin->entt_of(id));

                ImGui::PushID("camera");
                GLT::UI::begin_table(GLT::asset::COMPONENT_DATA_TABLE_NAME, false);
                GLT::UI::table_row("fov", cam.fov, 0.5f, 1.f, 179.f);
                GLT::UI::table_row("near plane", cam.near_plane, 0.01f, 0.001f, 100.f);
                GLT::UI::table_row("far plane", cam.far_plane, 1.f, 0.01f, 100000.f);

                // "Active camera" is a selection on the world, not a stored flag on the component. Expose it as a checkbox that
                // routes through i_world_scene. Shows only if the concrete plugin implements the scene interface (it always does today,
                // but keep the guard so headless / stripped world plugins don't crash the editor).
                if (auto* scene = plugin->as<GLT::world::i_world_scene>()) {

                    bool is_active = (scene->get_active_camera() == id);
                    if (GLT::UI::table_row("active camera", is_active))
                        scene->set_active_camera(is_active ? id : INVALID_ENTITY);
                }

                GLT::UI::end_table();
                ImGui::PopID();

                // Keep the near/far invariant consistent regardless of which row the user dragged. Same pattern as the audio descriptor's
                // min/max-distance clamp.
                cam.near_plane = GLT::math::clamp(cam.near_plane, 0.001f, cam.far_plane);
            },

            [](entity_id id) {

                // One-line preview for the outliner. Short is the point.
                auto* plugin = GLT::world::manager::get_ref()->as<ecs_world_plugin>();
                if (!plugin)
                    return std::string{};

                const auto& cam = plugin->registry().get<component::camera>(plugin->entt_of(id));
                char buf[32];
                std::snprintf(buf, sizeof(buf), "fov %.0f", cam.fov);
                return std::string(buf);
            },

            // A camera is meaningless without a transform to position it —
            // identical reasoning to mesh_renderer's required list.
            { component_name_hash("transform") });
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    void register_all_component_descriptors(component_registry& reg) {

        register_transform_descriptor(reg);
        register_name_descriptor(reg);
        register_hierarchy_descriptor(reg);
        register_mesh_renderer_descriptor(reg);
        register_audio_source_descriptor(reg);
        register_no_inherit_transform_descriptor(reg);
        register_camera_descriptor(reg);
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
