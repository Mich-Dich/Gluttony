
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
#include <world/component.h>
#include <world/component_registry.h>
#include <game/game_layer.h>
#include <game/game_session.h>

#include "ecs_world_plugin.h"
#include "ecs_world.h"
#include "register.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt::component {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    template<typename T>
    T* fetch(i_world& world, entity_id id);

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // Only the mesh lambda needs entt-specific API (rebuild_physics_body). Cast once.
    ecs_world* as_entt(i_world& world);

    void register_transform_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin);

    void register_mesh_renderer_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin);

    void register_name_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin);

    void register_hierarchy_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin);

    void register_audio_source_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin);

    void register_no_inherit_transform_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin);

    void register_camera_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin);

    void register_light_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    template<typename T>
    T* fetch(i_world& world, entity_id id) { return GLT::world::component_registry::get<T>(world, id); }

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    ecs_world* as_entt(i_world& world) { return static_cast<ecs_world*>(&world); }


    void register_transform_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin) {

        plugin.register_component_type(
            GLT::world::component_registry::make_descriptor<component::transform>(
                "transform", "Core",

                [](i_world& world, entity_id id) {
                    auto* t = fetch<component::transform>(world, id);
                    if (!t) return;
                    ImGui::PushID("transform_component");
                    if (GLT::UI::begin_table(GLT::asset::COMPONENT_DATA_TABLE_NAME, false)) {
                        GLT::UI::table_row("position", t->position);
                        GLT::UI::table_row("rotation", t->rotation);
                        GLT::UI::table_row("scale",    t->scale);
                        GLT::UI::end_table();
                    }
                    ImGui::PopID();
                },

                [](i_world& world, entity_id id) {
                    const auto* t = fetch<component::transform>(world, id);
                    if (!t) return std::string{};
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "(%.1f, %.1f, %.1f)", t->position.x, t->position.y, t->position.z);
                    return std::string(buf);
                },
                {}));
    }


    void register_mesh_renderer_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin) {

        plugin.register_component_type(
            GLT::world::component_registry::make_descriptor<component::mesh>(
                "mesh", "Rendering",

                [](i_world& world, entity_id id) {
                    auto* mr = fetch<component::mesh>(world, id);
                    if (!mr) return;

                    auto* entt = as_entt(world);

                    ImGui::PushID("mesh_component");
                    if (!GLT::UI::begin_table(GLT::asset::COMPONENT_DATA_TABLE_NAME, false)) {
                        ImGui::PopID();
                        return;
                    }

                    const GLT::asset::handle old_mesh_handle = mr->mesh.get();
                    GLT::UI::table_row_asset_picker("Mesh", mr->mesh, GLT::asset::core_types::static_mesh);
                    const bool mesh_changed = mr->mesh.get() != old_mesh_handle;

                    u32  submesh_count = 0;
                    bool has_colliders = false;
                    if (auto registry = GLT::asset::registry::get_ref(); registry && mr->mesh != INVALID_HANDLE) {
                        if (const auto* asset = registry->data_as<GLT::asset::mesh::mesh_asset>(mr->mesh)) {
                            submesh_count = asset->submesh_count();
                            has_colliders = !asset->colliders.empty();
                        }
                    }

                    GLT::UI::table_row("visible", mr->visible);

                    if (submesh_count > 0) {
                        if (!mr->material_overrides.empty() && !mr->overrides_match(submesh_count))
                            mr->material_overrides.clear();

                        if (mr->material_overrides.empty()) {
                            auto new_material = GLT::asset::asset_ref{};
                            if (GLT::UI::table_row_asset_picker("Material Override", new_material, GLT::asset::core_types::material)) {
                                mr->begin_override(submesh_count);
                                for (auto& slot : mr->material_overrides)
                                    slot = new_material;
                            }
                        } else {
                            for (u32 i = 0; i < submesh_count; ++i) {
                                ImGui::PushID(static_cast<int>(i));
                                char label[24];
                                std::snprintf(label, sizeof(label), "Material [%u]", i);
                                GLT::UI::table_row_asset_picker(label, mr->material_overrides[i], GLT::asset::core_types::material);
                                ImGui::PopID();
                            }
                            if (GLT::UI::gray_button("Reset material overrides"))
                                mr->material_overrides.clear();
                        }
                    } else if (!mr->material_overrides.empty()) {
                        ImGui::TextDisabled("(%zu pending override slot(s) - mesh asset not loaded)", mr->material_overrides.size());
                        if (GLT::UI::gray_button("Clear material overrides"))
                            mr->material_overrides.clear();
                    }

                    if (has_colliders) {
                        const physics::motion_type      old_motion   = mr->motion;
                        const physics::physics_material old_material = mr->physics_material;
                        const bool                      old_use_mesh = mr->use_mesh_material;

                        GLT::UI::table_row("mobility",          mr->motion, GLT::util::enum_values<physics::motion_type>);
                        GLT::UI::table_row("use mesh material", mr->use_mesh_material);

                        if (!mr->use_mesh_material) {
                            GLT::UI::table_row("friction",    mr->physics_material.friction,    0.01f, 0.f, 10.f);
                            GLT::UI::table_row("restitution", mr->physics_material.restitution, 0.01f, 0.f, 1.f);
                            GLT::UI::table_row("density",     mr->physics_material.density,     1.f,   1.f, 100000.f);
                        }

                        const bool motion_changed = mr->motion != old_motion;
                        const bool material_changed = mr->use_mesh_material != old_use_mesh ||
                            mr->physics_material.friction    != old_material.friction ||
                            mr->physics_material.restitution != old_material.restitution ||
                            mr->physics_material.density     != old_material.density;

                        if ((mesh_changed || motion_changed || material_changed) && entt->get_physics())
                            entt->rebuild_physics_body(id);
                    }

                    GLT::UI::end_table();
                    ImGui::PopID();
                },

                [](i_world& world, entity_id id) {
                    const auto* mr = fetch<component::mesh>(world, id);
                    if (!mr) return std::string{};

                    const char* motion_tag = nullptr;
                    if (mr->motion == physics::motion_type::dynamic_body)   motion_tag = "dyn";
                    else if (mr->motion == physics::motion_type::kinematic_body) motion_tag = "kin";

                    char buf[64] = {};
                    if (motion_tag && !mr->material_overrides.empty())
                        std::snprintf(buf, sizeof(buf), "[%s] (%zu overrides)", motion_tag, mr->material_overrides.size());
                    else if (motion_tag)
                        std::snprintf(buf, sizeof(buf), "[%s]", motion_tag);
                    else if (!mr->material_overrides.empty())
                        std::snprintf(buf, sizeof(buf), "(%zu overrides)", mr->material_overrides.size());
                    return std::string(buf);
                },

                { GLT::world::component_registry::hash_of("transform"),
                  GLT::world::component_registry::hash_of("name") }));
    }


    void register_name_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin) {

        plugin.register_component_type(
            GLT::world::component_registry::make_descriptor<component::name>(
                "name", "Core",

                [](i_world& world, entity_id id) {
                    auto* n = fetch<component::name>(world, id);
                    if (!n) return;
                    static bool enable_input = false;
                    if (GLT::UI::begin_table(GLT::asset::COMPONENT_DATA_TABLE_NAME, false)) {
                        GLT::UI::table_row("name", n->name, enable_input);
                        GLT::UI::end_table();
                    }
                },
                {}, {}));
    }


    void register_hierarchy_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin) {

        plugin.register_component_type(
            GLT::world::component_registry::make_descriptor<component::hierarchy>(
                "hierarchy", "Core",

                [](i_world& world, entity_id id) {
                    const auto parent = world.parent_of(id);
                    const auto kids   = world.children_of(id);
                    const auto start_pos = ImGui::GetCursorPosX();
                    const float avail = ImGui::GetContentRegionAvail().x;
                    ImGui::TextDisabled("Parent: %s", parent.is_valid() ? "(set)" : "(none)");
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(start_pos + avail / 2);
                    ImGui::TextDisabled("Children: %zu", kids.size());
                },
                {}, {}));
    }


    void register_audio_source_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin) {

        plugin.register_component_type(
            GLT::world::component_registry::make_descriptor<component::audio_source>(
                "audio source", "Audio",

                [](i_world& world, entity_id id) {
                    auto* audio = fetch<component::audio_source>(world, id);
                    if (!audio) return;

                    ImGui::PushID("audio_source");
                    if (GLT::UI::begin_table(GLT::asset::COMPONENT_DATA_TABLE_NAME, false)) {
                        GLT::UI::table_row_asset_picker("clip", audio->clip, GLT::asset::core_types::audio);
                        GLT::UI::table_row("autop play",   audio->autoplay);
                        GLT::UI::table_row("volume",       audio->config.volume);
                        GLT::UI::table_row("pan",          audio->config.pan, 0.01f, -1.f, 1.f);
                        GLT::UI::table_row("loop",         audio->config.loop);
                        GLT::UI::table_row("play speed",   audio->config.play_speed);
                        GLT::UI::table_row("is 3d",        audio->config.is_3d);
                        GLT::UI::table_row_slider("position", audio->config.position);
                        GLT::UI::table_row_slider("velocity", audio->config.velocity);
                        GLT::UI::table_row("min distance",      audio->config.min_distance,      0.01f, 0.f, 1000.f);
                        GLT::UI::table_row("max distance",      audio->config.max_distance,      0.01f, 0.f, 1000.f);
                        GLT::UI::table_row("rolloff factor",    audio->config.rolloff_factor,    0.01f, 0.f, 1000.f);
                        GLT::UI::table_row("attenuation factor", audio->config.attenuation, GLT::util::enum_values<GLT::asset::audio::attenuation_model>);
                        GLT::UI::end_table();
                    }
                    ImGui::PopID();

                    audio->config.min_distance = GLT::math::clamp(audio->config.min_distance, 0.f, audio->config.max_distance);
                },
                {}, {}));
    }


    void register_no_inherit_transform_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& /*plugin*/) { /* marker-only; nothing to draw */ }


    void register_camera_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin) {

        plugin.register_component_type(
            GLT::world::component_registry::make_descriptor<component::camera>(
                "camera", "Rendering",

                [](i_world& world, entity_id id) {
                    auto* cam = fetch<component::camera>(world, id);
                    if (!cam) return;

                    ImGui::PushID("camera_component");
                    if (GLT::UI::begin_table(GLT::asset::COMPONENT_DATA_TABLE_NAME, false)) {
                        GLT::UI::table_row("fov",        cam->fov,        0.5f,  1.f,     179.f);
                        GLT::UI::table_row("near plane", cam->near_plane, 0.01f, 0.001f,  100.f);
                        GLT::UI::table_row("far plane",  cam->far_plane,  1.f,   0.01f,   100000.f);

                        auto* primary_session = GLT::application::get().get_game_layer_ptr()->get_primary_session();
                        bool is_active = (primary_session->get_editor_camera() == id);
                        if (GLT::UI::table_row("active camera", is_active))
                            primary_session->set_editor_camera(is_active ? id : INVALID_ENTITY);

                        GLT::UI::end_table();
                    }
                    ImGui::PopID();

                    cam->near_plane = GLT::math::clamp(cam->near_plane, 0.001f, cam->far_plane);
                },

                [](i_world& world, entity_id id) {
                    const auto* cam = fetch<component::camera>(world, id);
                    if (!cam) return std::string{};
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "fov %.0f", cam->fov);
                    return std::string(buf);
                },

                { GLT::world::component_registry::hash_of("transform") }));
    }


    void register_light_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin) {

        plugin.register_component_type(
            GLT::world::component_registry::make_descriptor<component::light>(
                "light", "Rendering",

                [](i_world& world, entity_id id) {
                    auto* light = fetch<component::light>(world, id);
                    if (!light) return;

                    ImGui::PushID("light_component");
                    if (GLT::UI::begin_table(GLT::asset::COMPONENT_DATA_TABLE_NAME, false)) {
                        GLT::UI::table_row("type",      light->type, GLT::util::enum_values<GLT::world::light_type>);
                        GLT::UI::table_row("color",     light->color);
                        GLT::UI::table_row("intensity", light->intensity, 0.01f, 0.f, 1000.f);

                        if (light->type != GLT::world::light_type::directional)
                            GLT::UI::table_row("radius", light->radius, 0.1f, 0.01f, 100000.f);

                        if (light->type == GLT::world::light_type::spot) {
                            GLT::UI::table_row("inner cone", light->inner_cone_angle, 0.005f, 0.f, glm::pi<f32>());
                            GLT::UI::table_row("outer cone", light->outer_cone_angle, 0.005f, 0.f, glm::pi<f32>());
                            light->inner_cone_angle = GLT::math::clamp(light->inner_cone_angle, 0.f, light->outer_cone_angle);
                        }

                        GLT::UI::end_table();
                    }
                    ImGui::PopID();
                },

                [](i_world& world, entity_id id) {
                    const auto* light = fetch<component::light>(world, id);
                    if (!light) return std::string{};

                    const char* tag = "?";
                    switch (light->type) {
                        case GLT::world::light_type::directional:
                            tag = "dir";
                            break;
                        case GLT::world::light_type::point:
                            tag = "point";
                            break;
                        case GLT::world::light_type::spot:
                            tag = "spot";
                            break;
                    }
                    char buf[48];
                    std::snprintf(buf, sizeof(buf), "%s  i=%.2f", tag, light->intensity);
                    return std::string(buf);
                },

                { GLT::world::component_registry::hash_of("transform") }));
    }
    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    void register_all_component_descriptors(GLT::world::world_ecs_entt::ecs_world_plugin& plugin) {

        register_transform_descriptor(plugin);
        register_name_descriptor(plugin);
        register_hierarchy_descriptor(plugin);
        register_mesh_renderer_descriptor(plugin);
        register_audio_source_descriptor(plugin);
        register_no_inherit_transform_descriptor(plugin);
        register_camera_descriptor(plugin);
        register_light_descriptor(plugin);
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
