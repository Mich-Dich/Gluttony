
#include "util/pch.h"
#include "components.h"

#include <imgui.h>

#include <util/ui/panel_collection.h>
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
    void register_sky_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin);
    void register_ambient_light_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin);
    void move_light_to_world(i_world& world, entity_id id);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    template<typename T>
    T* fetch(i_world& world, entity_id id) { return GLT::world::component_registry::get<T>(world, id); }

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    ecs_world* as_entt(i_world& world) { return static_cast<ecs_world*>(&world); }


    // Angle stored in radians, edited in degrees
    bool property_angle(const char* label, f32& radians, const f32 max_degrees, const char* desc = nullptr, const f32* default_radians = nullptr) {

        const bool is_default = !default_radians || radians == *default_radians;
        return GLT::UI::property_custom(label, [&](f32 width) {
            f32 degrees = glm::degrees(radians);
            ImGui::SetNextItemWidth(width);
            if (!ImGui::DragFloat("##angle", &degrees, 0.25f, 0.f, max_degrees, "%.1f deg", ImGuiSliderFlags_AlwaysClamp))
                return false;
            radians = glm::radians(degrees);
            return true;
        }, desc, is_default, [&]() { radians = *default_radians; });
    }


    // Copies the light and the entity's WORLD rotation onto the world entity, then removes the light from [id]
    void move_light_to_world(i_world& world, entity_id id) {

        auto* inspector = world.as<GLT::world::i_world_inspector>();
        const entity_id world_entity = world.world_entity();
        const auto* source = fetch<component::light>(world, id);
        if (!inspector || !world_entity.is_valid() || !source)
            return;

        const component::light copy = *source;
        glm::quat rotation = world.get_local_rotation_q(id);
        for (entity_id parent = world.parent_of(id); parent.is_valid(); parent = world.parent_of(parent))
            rotation = world.get_local_rotation_q(parent) * rotation;

        const u64 light_hash = GLT::world::component_registry::hash_of("light");
        if (!fetch<component::light>(world, world_entity))
            inspector->add_component(world_entity, light_hash);
        if (auto* target = fetch<component::light>(world, world_entity))
            *target = copy;
        world.set_local_rotation_q(world_entity, rotation);
        inspector->remove_component(id, light_hash);
    }


    void register_transform_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin) {

        plugin.register_component_type(
            GLT::world::component_registry::make_descriptor<component::transform>(
                "transform", "Core",

                [](i_world& world, entity_id id) {
                    auto* t = fetch<component::transform>(world, id);
                    if (!t) return;
                    static const component::transform defaults{};

                    ImGui::PushID("transform_component");
                    GLT::UI::property_drag("Position", t->position, 0.05f, 0.f, 0.f, "%.2f", "Relative to the parent", &defaults.position);

                    // stored as radians, edited in degrees
                    glm::vec3 degrees = glm::degrees(t->rotation);
                    const bool rotation_default = t->rotation == defaults.rotation;
                    const bool rotated = GLT::UI::property_custom("Rotation", [&](f32 width) {
                        return GLT::UI::drag_axes("##rotation", &degrees.x, 3, 0.5f, 0.f, 0.f, "%.1f", width);
                    }, "Euler angles in degrees", rotation_default, [&]() { degrees = glm::vec3(0.f); });
                    if (rotated)
                        t->rotation = glm::radians(degrees);

                    GLT::UI::property_drag("Scale", t->scale, 0.01f, 0.f, 0.f, "%.2f", nullptr, &defaults.scale);

                    // set_mobility (not a direct write): moving an entity between fixed and movable rebuilds the render cache
                    static constexpr const char* mobility_options[] = { "Static", "Movable" };
                    static constexpr i32 default_mobility = static_cast<i32>(GLT::world::mobility::fixed);
                    i32 mobility_index = static_cast<i32>(t->mobility);
                    if (GLT::UI::property_segmented("Mobility", mobility_index, mobility_options,
                        "Static: render data is cached and only refreshed when the entity is edited (free per frame)\n"
                        "Movable: recomputed every frame. Use for anything moved by scripts or animation\n"
                        "Static entities can still move, it is just the slower path. Children of movables and "
                        "dynamic / kinematic physics bodies always count as movable", &default_mobility))
                        world.set_mobility(id, static_cast<GLT::world::mobility>(mobility_index));
                    ImGui::PopID();
                },

                [](i_world& world, entity_id id) {
                    const auto* t = fetch<component::transform>(world, id);
                    if (!t) return std::string{};
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "(%.1f, %.1f, %.1f)", t->position.x, t->position.y, t->position.z);
                    return std::string(buf);
                },
                {}, GLT::world::component::scope::both));
    }


    void register_mesh_renderer_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin) {

        plugin.register_component_type(
            GLT::world::component_registry::make_descriptor<component::mesh>(
                "mesh", "Rendering",

                [](i_world& world, entity_id id) {
                    auto* mr = fetch<component::mesh>(world, id);
                    if (!mr) return;

                    auto* entt = as_entt(world);
                    // component::mesh holds asset refs, so no static default instance: plain values instead
                    static const bool                       default_visible = true;
                    static const physics::motion_type       default_motion = physics::motion_type::static_body;
                    static const bool                       default_use_mesh_material = true;
                    static const physics::physics_material  default_material{};

                    ImGui::PushID("mesh_component");

                    const GLT::asset::handle old_mesh_handle = mr->mesh.get();
                    GLT::UI::property_asset("Mesh", mr->mesh, GLT::asset::core_types::static_mesh);
                    const bool mesh_changed = mr->mesh.get() != old_mesh_handle;

                    u32  submesh_count = 0;
                    bool has_colliders = false;
                    if (auto registry = GLT::asset::registry::get_ref(); registry && mr->mesh != INVALID_HANDLE) {
                        if (const auto* asset = registry->data_as<GLT::asset::mesh::mesh_asset>(mr->mesh)) {
                            submesh_count = asset->submesh_count();
                            has_colliders = !asset->colliders.empty();
                        }
                    }

                    GLT::UI::property_toggle("Visible", mr->visible, nullptr, &default_visible);

                    if (submesh_count > 0) {
                        if (!mr->material_overrides.empty() && !mr->overrides_match(submesh_count))
                            mr->material_overrides.clear();

                        if (mr->material_overrides.empty()) {
                            auto new_material = GLT::asset::asset_ref{};
                            if (GLT::UI::property_asset("Material", new_material, GLT::asset::core_types::material,
                                "Override the material of every submesh. Empty = materials of the mesh asset")) {
                                mr->begin_override(submesh_count);
                                for (auto& slot : mr->material_overrides)
                                    slot = new_material;
                            }
                        } else {
                            for (u32 i = 0; i < submesh_count; ++i) {
                                ImGui::PushID(static_cast<int>(i));
                                char label[24];
                                std::snprintf(label, sizeof(label), "Material %u", i);
                                GLT::UI::property_asset(label, mr->material_overrides[i], GLT::asset::core_types::material);
                                ImGui::PopID();
                            }
                            if (GLT::UI::subtle_button("Use the mesh's materials", ImVec2(-FLT_MIN, 0.f)))
                                mr->material_overrides.clear();
                        }
                    } else if (!mr->material_overrides.empty()) {
                        ImGui::TextDisabled("%zu override slot(s) waiting for the mesh asset to load", mr->material_overrides.size());
                        if (GLT::UI::subtle_button("Clear material overrides", ImVec2(-FLT_MIN, 0.f)))
                            mr->material_overrides.clear();
                    }

                    if (has_colliders) {
                        const physics::motion_type      old_motion   = mr->motion;
                        const physics::physics_material old_material = mr->physics_material;
                        const bool                      old_use_mesh = mr->use_mesh_material;

                        GLT::UI::property_enum("Mobility", mr->motion, "Static never moves, kinematic is moved by code, dynamic by physics",
                            &default_motion);
                        GLT::UI::property_toggle("Mesh material", mr->use_mesh_material, "Use the physics material authored on the mesh asset",
                            &default_use_mesh_material);

                        if (!mr->use_mesh_material) {
                            GLT::UI::property_drag("Friction",    mr->physics_material.friction,    0.01f, 0.f, 10.f, "%.2f", nullptr, &default_material.friction);
                            GLT::UI::property_drag("Restitution", mr->physics_material.restitution, 0.01f, 0.f, 1.f,  "%.2f", "Bounciness", &default_material.restitution);
                            GLT::UI::property_drag("Density",     mr->physics_material.density,     1.f,   1.f, 100000.f, "%.0f", "kg / m^3", &default_material.density);
                        }

                        const bool motion_changed = mr->motion != old_motion;
                        const bool material_changed = mr->use_mesh_material != old_use_mesh ||
                            mr->physics_material.friction    != old_material.friction ||
                            mr->physics_material.restitution != old_material.restitution ||
                            mr->physics_material.density     != old_material.density;

                        if ((mesh_changed || motion_changed || material_changed) && entt->get_physics())
                            entt->rebuild_physics_body(id);
                    }

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
                    ImGui::PushID("name_component");
                    GLT::UI::property_text("Name", n->name, "Shown in the Outliner", "Unnamed");
                    ImGui::PopID();
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

                    std::string parent_name = "None";
                    if (parent.is_valid()) {
                        const auto name = world.entity_name(parent);
                        parent_name = name.empty() ? std::format("Entity {}", parent.index) : std::string(name);
                    }

                    ImGui::PushID("hierarchy_component");
                    GLT::UI::property_value("Parent", parent_name, "Change it by dragging in the Outliner");
                    GLT::UI::property_value("Children", std::to_string(kids.size()));
                    ImGui::PopID();
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

                    static const GLT::asset::audio::source_config defaults{};
                    static const bool default_autoplay = false;

                    ImGui::PushID("audio_source");
                    GLT::UI::property_asset("Clip", audio->clip, GLT::asset::core_types::audio);
                    GLT::UI::property_toggle("Play on start", audio->autoplay, "Starts playing when the simulation starts", &default_autoplay);
                    GLT::UI::property_toggle("Loop", audio->config.loop, nullptr, &defaults.loop);
                    GLT::UI::property_slider("Volume", audio->config.volume, 0.f, 2.f, "%.2f", nullptr, &defaults.volume);
                    GLT::UI::property_slider("Pan", audio->config.pan, -1.f, 1.f, "%.2f", "-1 left, 1 right (2D only)", &defaults.pan);
                    GLT::UI::property_drag("Speed", audio->config.play_speed, 0.01f, 0.01f, 10.f, "%.2fx", "Playback speed, also changes the pitch", &defaults.play_speed);

                    GLT::UI::property_toggle("3D", audio->config.is_3d, "Volume and panning depend on the listener position", &defaults.is_3d);
                    if (audio->config.is_3d) {
                        GLT::UI::property_drag("Position", audio->config.position, 0.05f, 0.f, 0.f, "%.2f", nullptr, &defaults.position);
                        GLT::UI::property_drag("Velocity", audio->config.velocity, 0.05f, 0.f, 0.f, "%.2f", "For the doppler effect", &defaults.velocity);
                        GLT::UI::property_drag("Min distance", audio->config.min_distance, 0.01f, 0.f, 1000.f, "%.2f", "Full volume inside", &defaults.min_distance);
                        GLT::UI::property_drag("Max distance", audio->config.max_distance, 0.01f, 0.f, 1000.f, "%.2f", "Silent beyond", &defaults.max_distance);
                        GLT::UI::property_drag("Rolloff", audio->config.rolloff_factor, 0.01f, 0.f, 1000.f, "%.2f", "How fast the volume falls off", &defaults.rolloff_factor);
                        GLT::UI::property_enum("Attenuation", audio->config.attenuation, nullptr, &defaults.attenuation);
                    }
                    ImGui::PopID();

                    audio->config.min_distance = GLT::math::clamp(audio->config.min_distance, 0.f, audio->config.max_distance);
                },
                {}, {}, GLT::world::component::scope::both));
    }


    void register_no_inherit_transform_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& /*plugin*/) { /* marker-only; nothing to draw */ }


    void register_camera_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin) {

        plugin.register_component_type(
            GLT::world::component_registry::make_descriptor<component::camera>(
                "camera", "Rendering",

                [](i_world& world, entity_id id) {
                    auto* cam = fetch<component::camera>(world, id);
                    if (!cam) return;

                    static const component::camera defaults{};

                    ImGui::PushID("camera_component");
                    GLT::UI::property_slider("Field of view", cam->fov, 1.f, 179.f, "%.0f deg", "Vertical opening angle", &defaults.fov);
                    GLT::UI::property_drag("Near plane", cam->near_plane, 0.01f, 0.001f, 100.f, "%.3f", "Closer things are cut off", &defaults.near_plane);
                    GLT::UI::property_drag("Far plane", cam->far_plane, 1.f, 0.01f, 100000.f, "%.0f", "Farther things are cut off", &defaults.far_plane);

                    auto* primary_session = GLT::application::get().get_game_layer_ptr()->get_primary_session();
                    bool is_active = (primary_session->get_editor_camera() == id);
                    if (GLT::UI::property_toggle("Viewport camera", is_active, "The editor viewport looks through this camera"))
                        primary_session->set_editor_camera(is_active ? id : INVALID_ENTITY);
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

                    static const component::light defaults{};

                    ImGui::PushID("light_component");
                    GLT::UI::property_enum("Type", light->type, "Directional = sun, point = bulb, spot = cone", &defaults.type);
                    GLT::UI::property_color("Color", light->color, "Linear RGB", &defaults.color);
                    GLT::UI::property_drag("Intensity", light->intensity, 0.01f, 0.f, 1000.f, "%.2f", nullptr, &defaults.intensity);

                    if (light->type != GLT::world::light_type::directional)
                        GLT::UI::property_drag("Radius", light->radius, 0.1f, 0.01f, 100000.f, "%.1f", "Light reaches up to this distance", &defaults.radius);

                    if (light->type == GLT::world::light_type::spot) {
                        property_angle("Inner cone", light->inner_cone_angle, 180.f, "Full brightness inside", &defaults.inner_cone_angle);
                        property_angle("Outer cone", light->outer_cone_angle, 180.f, "Fades out towards this angle", &defaults.outer_cone_angle);
                        light->inner_cone_angle = GLT::math::clamp(light->inner_cone_angle, 0.f, light->outer_cone_angle);
                    }

                    // A sun on a normal entity can be streamed out with its region. Offer to move it to the world entity
                    const entity_id world_entity = world.world_entity();
                    if (light->type == GLT::world::light_type::directional && world_entity.is_valid() && !world.is_world_entity(id)) {

                        ImGui::Spacing();
                        ImGui::TextDisabled("This sun disappears when its region streams out.");
                        if (GLT::UI::subtle_button("Move to World", ImVec2(-FLT_MIN, 0.f))) {
                            move_light_to_world(world, id);
                            ImGui::PopID();
                            return;                                         // [light] no longer exists on this entity
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Moves this light (and its world rotation) to the world entity, which is never streamed.\n"
                                "Replaces a light the world entity already has.");
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

                { GLT::world::component_registry::hash_of("transform") }, GLT::world::component::scope::both));
    }
    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    void register_sky_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin) {

        plugin.register_component_type(
            GLT::world::component_registry::make_descriptor<component::sky>(
                "sky", "Environment",

                [](i_world& world, entity_id id) {
                    auto* sky = fetch<component::sky>(world, id);
                    if (!sky) return;

                    static const component::sky defaults{};

                    ImGui::PushID("sky_component");
                    GLT::UI::property_drag("Intensity", sky->intensity, 0.01f, 0.f, 10.f, "%.2f", "Sky brightness relative to the sun", &defaults.intensity);
                    GLT::UI::property_drag("Haze", sky->haze, 0.01f, 0.f, 10.f, "%.2f", "Dust and moisture in the air", &defaults.haze);
                    GLT::UI::property_slider("Ground albedo", sky->ground_albedo, 0.f, 1.f, "%.2f", "How much light the ground below the horizon reflects", &defaults.ground_albedo);
                    GLT::UI::property_drag("Sun disk size", sky->sun_disk_size, 0.01f, 0.f, 5.f, "%.2f deg", "0 hides the sun disk", &defaults.sun_disk_size);
                    ImGui::TextDisabled("Driven by the world's directional light (rotate the World to move the sun).");
                    ImGui::PopID();
                },

                [](i_world& world, entity_id id) {
                    const auto* sky = fetch<component::sky>(world, id);
                    if (!sky) return std::string{};
                    char buf[48];
                    std::snprintf(buf, sizeof(buf), "haze %.2f", sky->haze);
                    return std::string(buf);
                },

                {}, GLT::world::component::scope::world));
    }


    void register_ambient_light_descriptor(GLT::world::world_ecs_entt::ecs_world_plugin& plugin) {

        plugin.register_component_type(
            GLT::world::component_registry::make_descriptor<component::ambient_light>(
                "ambient_light", "Environment",

                [](i_world& world, entity_id id) {
                    auto* ambient = fetch<component::ambient_light>(world, id);
                    if (!ambient) return;

                    static const component::ambient_light defaults{};

                    ImGui::PushID("ambient_light_component");
                    GLT::UI::property_drag("Intensity", ambient->intensity, 0.01f, 0.f, 20.f, "%.2f", nullptr, &defaults.intensity);
                    if (fetch<component::sky>(world, id) == nullptr) {
                        GLT::UI::property_color("Sky color", ambient->sky_color, "Light from above", &defaults.sky_color);
                        GLT::UI::property_color("Ground color", ambient->ground_color, "Light from below", &defaults.ground_color);
                    }
                    if (fetch<component::sky>(world, id))
                        ImGui::TextDisabled("Scales the image based lighting from the sky.");
                    ImGui::PopID();
                },

                [](i_world& world, entity_id id) {
                    const auto* ambient = fetch<component::ambient_light>(world, id);
                    if (!ambient) return std::string{};
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "i=%.2f", ambient->intensity);
                    return std::string(buf);
                },

                {}, GLT::world::component::scope::world));
    }


    void register_all_component_descriptors(GLT::world::world_ecs_entt::ecs_world_plugin& plugin) {

        register_transform_descriptor(plugin);
        register_name_descriptor(plugin);
        register_hierarchy_descriptor(plugin);
        register_mesh_renderer_descriptor(plugin);
        register_audio_source_descriptor(plugin);
        register_no_inherit_transform_descriptor(plugin);
        register_camera_descriptor(plugin);
        register_light_descriptor(plugin);
        register_sky_descriptor(plugin);
        register_ambient_light_descriptor(plugin);
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
