
#include "util/pch.h"
#include "components.h"

#include <imgui.h>

#include <util/ui/pannel_collection.h>
#include <util/ui/asset_picker.h>
#include <asset/audio.h>
#include <asset/i_asset_registry.h>
#include <asset/mesh.h>
#include <world/i_world_plugin.h>
#include <world/i_world_scene.h>
#include <game/game_layer.h>
#include <game/game_session.h>

#include "ecs_world.h"



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

    void register_light_descriptor(component_registry& reg);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    void register_transform_descriptor(component_registry& reg) {

        reg.register_component<component::transform>("transform", "Core",

            [](ecs_world& world, entity_id id) {

                auto& transform = world.registry().get<component::transform>(world.entt_of(id));

                ImGui::PushID("transform");
                GLT::UI::begin_table(GLT::asset::COMPONENT_DATA_TABLE_NAME, false);
                GLT::UI::table_row("position", transform.position);
                GLT::UI::table_row("rotation", transform.rotation);
                GLT::UI::table_row("scale",    transform.scale);
                GLT::UI::end_table();
                ImGui::PopID();
            },

            [](ecs_world& world, entity_id id) {
                const auto& transform = world.registry().get<component::transform>(world.entt_of(id));
                char buf[64];
                std::snprintf(buf, sizeof(buf), "(%.1f, %.1f, %.1f)",
                    transform.position.x, transform.position.y, transform.position.z);
                return std::string(buf);
            });
    }


    void register_mesh_renderer_descriptor(component_registry& reg) {

        reg.register_component<component::mesh>("mesh", "Rendering",

            [](ecs_world& world, entity_id id) {

                auto& mr = world.registry().get<component::mesh>(world.entt_of(id));

                GLT::UI::begin_table(GLT::asset::COMPONENT_DATA_TABLE_NAME, false);

                const GLT::asset::handle old_mesh_handle = mr.mesh.get();
                GLT::UI::table_row_asset_picker("Mesh", mr.mesh, GLT::asset::core_types::static_mesh);
                const bool mesh_changed = mr.mesh.get() != old_mesh_handle;

                // One asset lookup gives us both the submesh count (for material overrides) and whether
                // the mesh carries authored colliders (which gates the physics section)
                u32  submesh_count = 0;
                bool has_colliders = false;
                if (auto registry = GLT::asset::registry::get_ref(); registry && mr.mesh != INVALID_HANDLE) {
                    if (const auto* asset = registry->data_as<GLT::asset::mesh::mesh_asset>(mr.mesh)) {
                        submesh_count = asset->submesh_count();
                        has_colliders = !asset->colliders.empty();
                    }
                }

                // material overrides ----------------------------------------------------------------------------------

                if (submesh_count > 0) {

                    if (!mr.material_overrides.empty() && !mr.overrides_match(submesh_count))
                        mr.material_overrides.clear();

                    if (mr.material_overrides.empty()) {

                        GLT::asset::asset_ref uniform{};
                        if (GLT::UI::table_row_asset_picker("Material Override", uniform, GLT::asset::core_types::material)) {
                            mr.begin_override(submesh_count);
                            for (auto& slot : mr.material_overrides)
                                slot = uniform;
                        }

                    } else {

                        for (u32 i = 0; i < submesh_count; ++i) {
                            ImGui::PushID(static_cast<int>(i));
                            char label[24];
                            std::snprintf(label, sizeof(label), "Material [%u]", i);
                            GLT::UI::table_row_asset_picker(label, mr.material_overrides[i], GLT::asset::core_types::material);
                            ImGui::PopID();
                        }

                        if (GLT::UI::gray_button("Reset material overrides"))
                            mr.material_overrides.clear();
                    }

                } else if (!mr.material_overrides.empty()) {

                    ImGui::TextDisabled("(%zu pending override slot(s) - mesh asset not loaded)",
                        mr.material_overrides.size());
                    if (GLT::UI::gray_button("Clear material overrides"))
                        mr.material_overrides.clear();
                }

                GLT::UI::table_row("visible", mr.visible);

                // ---- physics ----------------------------------------------------------------------------------------

                // Gated on the mesh actually carrying authored colliders. Without colliders there is no body to
                // configure, and showing inert controls would just confuse the user. Once colliders exist (either
                // authored up-front or added later), the rows appear automatically on the next draw
                if (has_colliders) {

                    // Snapshot before edits. The table_row return-value contract isn't uniform across value
                    // types, so a straight field comparison is the reliable "did it change" test
                    const physics::motion_type old_motion = mr.motion;
                    const physics::physics_material old_material = mr.physics_material;
                    const bool old_use_mesh = mr.use_mesh_material;

                    GLT::UI::table_row("mobility", mr.motion, GLT::util::enum_values<physics::motion_type>);
                    GLT::UI::table_row("use mesh material", mr.use_mesh_material);

                    if (!mr.use_mesh_material) {

                        // Ranges match the defaults baked into physics_material. Density has a min of 1 because a dynamic body with
                        // mass 0 is rejected by create_body() — better to clamp at the UI than silently fail inside the body builder
                        GLT::UI::table_row("friction", mr.physics_material.friction, 0.01f, 0.f, 10.f);
                        GLT::UI::table_row("restitution", mr.physics_material.restitution, 0.01f, 0.f, 1.f);
                        GLT::UI::table_row("density", mr.physics_material.density, 1.f, 1.f, 100000.f);
                    }

                    const bool motion_changed   = mr.motion != old_motion;
                    const bool material_changed = mr.use_mesh_material != old_use_mesh ||
                        mr.physics_material.friction != old_material.friction ||
                        mr.physics_material.restitution != old_material.restitution ||
                        mr.physics_material.density != old_material.density;

                    // Any change to shape or motion means the Jolt body must be rebuilt (motion type and material are
                    // baked into a Jolt body at create time). Guarded on world.get_physics() so editing fields on a
                    // world that never got a physics instance stays a pure data edit — the settings persist through
                    // save/reload and apply the next time physics is present
                    if ((mesh_changed || motion_changed || material_changed) && world.get_physics())
                        world.rebuild_physics_body(id);
                }

                GLT::UI::end_table();
            },

            [](ecs_world& world, entity_id id) {      // outliner summary

                const auto& mr = world.registry().get<component::mesh>(world.entt_of(id));

                const char* motion_tag = nullptr;
                if (mr.motion == physics::motion_type::dynamic_body)        motion_tag = "dyn";
                else if (mr.motion == physics::motion_type::kinematic_body) motion_tag = "kin";

                char buf[64] = {};
                if (motion_tag && !mr.material_overrides.empty())
                    std::snprintf(buf, sizeof(buf), "[%s] (%zu overrides)", motion_tag, mr.material_overrides.size());
                else if (motion_tag)
                    std::snprintf(buf, sizeof(buf), "[%s]", motion_tag);
                else if (!mr.material_overrides.empty())
                    std::snprintf(buf, sizeof(buf), "(%zu overrides)", mr.material_overrides.size());

                return std::string(buf);
            },

            { component_name_hash("transform") });
    }


    void register_name_descriptor(component_registry& reg) {

        reg.register_component<component::name>("name", "Core",

            [](ecs_world& world, entity_id id) {

                auto& name_comp = world.registry().get<component::name>(world.entt_of(id));
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

        //     [](ecs_world& world, entity_id id) {

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

            [](ecs_world& world, entity_id id) {

                auto& audio = world.registry().get<component::audio_source>(world.entt_of(id));

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

            [](ecs_world& world, entity_id id) {

                auto& cam = world.registry().get<component::camera>(world.entt_of(id));

                // ImGui::PushID("camera");
                GLT::UI::begin_table(GLT::asset::COMPONENT_DATA_TABLE_NAME, false);

                GLT::UI::table_row("fov", cam.fov, 0.5f, 1.f, 179.f);
                GLT::UI::table_row("near plane", cam.near_plane, 0.01f, 0.001f, 100.f);
                GLT::UI::table_row("far plane", cam.far_plane, 1.f, 0.01f, 100000.f);

                // Null-safe: skip the row if the scene interface isn't available, but DON'T early-return —
                // end_table() and PopID() below must always run to keep the ImGui stack balanced

                auto* primary_session = GLT::application::get().get_game_layer_ptr()->get_primary_session();
                bool is_active = (primary_session->get_editor_camera() == id);
                if (GLT::UI::table_row("active camera", is_active))
                    primary_session->set_editor_camera(is_active ? id : INVALID_ENTITY);

                GLT::UI::end_table();
                // ImGui::PopID();

                cam.near_plane = GLT::math::clamp(cam.near_plane, 0.001f, cam.far_plane);
            },

            [](ecs_world& world, entity_id id) {

                const auto& cam = world.registry().get<component::camera>(world.entt_of(id));
                char buf[32];
                std::snprintf(buf, sizeof(buf), "fov %.0f", cam.fov);
                return std::string(buf);
            },

            // A camera is meaningless without a transform to position it, identical reasoning to mesh_renderer's required list
            { component_name_hash("transform") });
    }


    void register_light_descriptor(component_registry& reg) {

        reg.register_component<component::light>("light", "Rendering",

            [](ecs_world& world, entity_id id) {

                auto& light = world.registry().get<component::light>(world.entt_of(id));

                ImGui::PushID("light");
                GLT::UI::begin_table(GLT::asset::COMPONENT_DATA_TABLE_NAME, false);

                GLT::UI::table_row("type", light.type, GLT::util::enum_values<GLT::world::light_type>);
                GLT::UI::table_row("color", light.color);
                GLT::UI::table_row("intensity", light.intensity, 0.01f, 0.f, 1000.f);

                if (light.type != GLT::world::light_type::directional)
                    GLT::UI::table_row("radius", light.radius, 0.1f, 0.01f, 100000.f);

                if (light.type == GLT::world::light_type::spot) {

                    GLT::UI::table_row("inner cone", light.inner_cone_angle, 0.005f, 0.f, glm::pi<f32>());
                    GLT::UI::table_row("outer cone", light.outer_cone_angle, 0.005f, 0.f, glm::pi<f32>());

                    // Same invariant pattern as the audio descriptor's min/max distance: whichever row the user dragged,
                    // restore the ordering at the end of the frame
                    light.inner_cone_angle = GLT::math::clamp(light.inner_cone_angle, 0.f, light.outer_cone_angle);
                }

                GLT::UI::end_table();
                ImGui::PopID();
            },

            [](ecs_world& world, entity_id id) {

                const auto& light = world.registry().get<component::light>(world.entt_of(id));

                const char* tag = "?";
                switch (light.type) {
                    case GLT::world::light_type::directional: tag = "dir";   break;
                    case GLT::world::light_type::point: tag = "point"; break;
                    case GLT::world::light_type::spot: tag = "spot";  break;
                }

                char buf[48];
                std::snprintf(buf, sizeof(buf), "%s  i=%.2f", tag, light.intensity);
                return std::string(buf);
            },

            // Same reasoning as camera/mesh: a light without a transform is unplaceable
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
        register_light_descriptor(reg);
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
