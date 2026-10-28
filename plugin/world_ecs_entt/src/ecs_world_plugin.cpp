
#include "util/pch.h"
#include "ecs_world_plugin.h"

#include <asset/world.h>

#include "ecs_world.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // Serialize an asset handle as its registry virtual path, length-prefixed. Zero-length path encodes INVALID_HANDLE
    static void write_asset_handle(const ref<asset::i_asset_registry_plugin>& asset_registry, asset::handle handle,
        std::vector<std::byte>& out_bytes);


    // Inverse of write_asset_handle. Unresolvable assets degrade to INVALID_HANDLE
    static asset::asset_ref read_asset_handle(const ref<asset::i_asset_registry_plugin>& asset_registry, std::span<const std::byte> in_bytes,
        size_t& offset);


    static void register_core_preset_factories(ecs_world_plugin& plugin);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static void write_asset_handle(const ref<asset::i_asset_registry_plugin>& asset_registry, asset::handle handle,
        std::vector<std::byte>& out_bytes) {

        std::string asset_path;
        if (handle != INVALID_HANDLE && asset_registry)
            asset_path = asset_registry->info(handle).virtual_path.generic_string();

        const u32 path_length = static_cast<u32>(asset_path.size());
        const auto* length_bytes = reinterpret_cast<const std::byte*>(&path_length);
        out_bytes.insert(out_bytes.end(), length_bytes, length_bytes + sizeof(path_length));

        if (path_length != 0) {
            const auto* path_bytes = reinterpret_cast<const std::byte*>(asset_path.data());
            out_bytes.insert(out_bytes.end(), path_bytes, path_bytes + path_length);
        }
    }


    static asset::asset_ref read_asset_handle(const ref<asset::i_asset_registry_plugin>& asset_registry, std::span<const std::byte> in_bytes,
        size_t& offset) {

        if (in_bytes.size() < offset + sizeof(u32))
            return INVALID_HANDLE;

        u32 path_length = 0;
        std::memcpy(&path_length, in_bytes.data() + offset, sizeof(path_length));
        offset += sizeof(path_length);

        if (path_length == 0)
            return INVALID_HANDLE;
        if (in_bytes.size() < offset + path_length)
            return INVALID_HANDLE;

        const std::filesystem::path asset_path{
            std::string(reinterpret_cast<const char*>(in_bytes.data() + offset), path_length)
        };
        offset += path_length;

        if (!asset_registry)
            return {};

        if (const auto borrowed = asset_registry->find(asset_path); borrowed != INVALID_HANDLE)
            return asset::asset_ref{ borrowed };

        if (auto loaded = asset_registry->load(asset_path); loaded)
            return std::move(*loaded);

        LOG(warn, "failed to resolve asset [{}] while loading audio_comp component", asset_path.generic_string());
        return {};
    }


    static void register_core_preset_factories(ecs_world_plugin& plugin) {

        plugin.register_preset_component("light", [](entity_builder& builder, const GLT::world::component_spec& spec) {

                component::light light_comp;

                if (const auto* value = spec.get("type"); value && std::holds_alternative<std::string>(*value)) {
                    const auto& s = std::get<std::string>(*value);
                    if (s == "directional")
                        light_comp.type = GLT::world::light_type::directional;
                    else if (s == "point")
                        light_comp.type = GLT::world::light_type::point;
                    else if (s == "spot")
                        light_comp.type = GLT::world::light_type::spot;
                }
                if (const auto* value = spec.get("color"); value && std::holds_alternative<glm::vec3>(*value))
                    light_comp.color = std::get<glm::vec3>(*value);
                if (const auto* value = spec.get("intensity"); value && std::holds_alternative<f64>(*value))
                    light_comp.intensity = static_cast<f32>(std::get<f64>(*value));
                if (const auto* value = spec.get("radius"); value && std::holds_alternative<f64>(*value))
                    light_comp.radius = static_cast<f32>(std::get<f64>(*value));
                if (const auto* value = spec.get("inner_cone_angle"); value && std::holds_alternative<f64>(*value))
                    light_comp.inner_cone_angle = static_cast<f32>(std::get<f64>(*value));
                if (const auto* value = spec.get("outer_cone_angle"); value && std::holds_alternative<f64>(*value))
                    light_comp.outer_cone_angle = static_cast<f32>(std::get<f64>(*value));

                builder.add_or_replace(std::move(light_comp));
            });

        plugin.register_preset_component("camera", [](entity_builder& builder, const GLT::world::component_spec& spec) {

                component::camera camera_comp;
                if (const auto* value = spec.get("fov"); value && std::holds_alternative<f64>(*value))
                    camera_comp.fov = static_cast<f32>(std::get<f64>(*value));
                if (const auto* value = spec.get("near_plane"); value && std::holds_alternative<f64>(*value))
                    camera_comp.near_plane = static_cast<f32>(std::get<f64>(*value));
                if (const auto* value = spec.get("far_plane"); value && std::holds_alternative<f64>(*value))
                    camera_comp.far_plane  = static_cast<f32>(std::get<f64>(*value));

                builder.add_or_replace(std::move(camera_comp));
            });

        plugin.register_preset_component("mesh", [](entity_builder& builder, const GLT::world::component_spec& spec) {

                // The factory owns the property → component translation. Right now the only knob is [visible]; a future
                // version could accept a "mesh_path" and go through asset::registry::load() to obtain a real handle. That
                // extension stays inside this factory - the preset schema doesn't need to change
                component::mesh mesh_comp{};

                if (const auto* value = spec.get("visible"); value && std::holds_alternative<bool>(*value))
                    mesh_comp.visible = std::get<bool>(*value);

                if (const auto* value = spec.get("mesh_path"); value && std::holds_alternative<std::string>(*value)) {
                    // Reserved for a future extension. Currently a no-op: content-dir paths are project-specific and
                    // don't belong in a core preset
                }
                builder.add_or_replace(std::move(mesh_comp));
            });

        plugin.register_preset_component("audio_source", [](entity_builder& builder, const GLT::world::component_spec& spec) {

                component::audio_source audio_comp{};

                if (const auto* value = spec.get("autoplay"); value && std::holds_alternative<bool>(*value))
                    audio_comp.autoplay = std::get<bool>(*value);
                if (const auto* value = spec.get("is_3d"); value && std::holds_alternative<bool>(*value))
                    audio_comp.config.is_3d = std::get<bool>(*value);
                if (const auto* value = spec.get("volume"); value && std::holds_alternative<f64>(*value))
                    audio_comp.config.volume = static_cast<f32>(std::get<f64>(*value));
                if (const auto* value = spec.get("loop"); value && std::holds_alternative<bool>(*value))
                    audio_comp.config.loop = std::get<bool>(*value);

                builder.add_or_replace(std::move(audio_comp));
            });
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    void ecs_world_plugin::on_load() {

        m_asset_handler.on_load();

        // POD components
        m_codec.register_component<component::transform>("glt.transform");
        m_codec.register_component<component::no_inherit_transform>("glt.no_inherit_transform");
        m_codec.register_component<component::camera>("glt.camera");
        m_codec.register_component<component::light>("glt.light");

        // custom components -------------------------------------------------------------------------------------------

        m_codec.register_custom_component<component::mesh>("glt.mesh_renderer",

            [](const entt::registry& entity_registry, entt::entity entity_handle, std::vector<std::byte>& out_bytes) {

                const auto& mesh_component = entity_registry.get<component::mesh>(entity_handle);
                auto asset_registry = GLT::asset::registry::get_ref();

                write_asset_handle(asset_registry, mesh_component.mesh, out_bytes);

                const u32 count = static_cast<u32>(mesh_component.material_overrides.size());
                const auto* comp_bytes = reinterpret_cast<const std::byte*>(&count);
                out_bytes.insert(out_bytes.end(), comp_bytes, comp_bytes + sizeof(count));

                for (const auto& ov : mesh_component.material_overrides)
                    write_asset_handle(asset_registry, ov, out_bytes);

                out_bytes.push_back(mesh_component.visible ? std::byte{1} : std::byte{0});

                out_bytes.push_back(static_cast<std::byte>(mesh_component.motion));

                const auto* mat_bytes = reinterpret_cast<const std::byte*>(&mesh_component.physics_material);
                out_bytes.insert(out_bytes.end(), mat_bytes, mat_bytes + sizeof(mesh_component.physics_material));

                out_bytes.push_back(mesh_component.use_mesh_material ? std::byte{1} : std::byte{0});
            },

            [](entt::registry& entity_registry, entt::entity entity_handle, std::span<const std::byte> in_bytes) {

                auto asset_registry = GLT::asset::registry::get_ref();
                auto& mesh_component = entity_registry.emplace_or_replace<component::mesh>(entity_handle);

                size_t offset = 0;
                mesh_component.mesh = read_asset_handle(asset_registry, in_bytes, offset);

                if (in_bytes.size() < offset + sizeof(u32))
                    return;
                u32 count = 0;
                std::memcpy(&count, in_bytes.data() + offset, sizeof(count));
                offset += sizeof(count);

                mesh_component.material_overrides.clear();
                mesh_component.material_overrides.reserve(count);
                for (u32 i = 0; i < count; ++i)
                    mesh_component.material_overrides.emplace_back(read_asset_handle(asset_registry, in_bytes, offset));

                mesh_component.visible = (in_bytes.size() > offset) ? (in_bytes[offset++] != std::byte{0}) : true;
                if (in_bytes.size() > offset)
                    mesh_component.motion = static_cast<physics::motion_type>(static_cast<u8>(in_bytes[offset++]));

                if (in_bytes.size() >= offset + sizeof(physics::physics_material)) {
                    std::memcpy(&mesh_component.physics_material, in_bytes.data() + offset, sizeof(physics::physics_material));
                    offset += sizeof(physics::physics_material);
                }

                mesh_component.use_mesh_material = (in_bytes.size() > offset) ? (in_bytes[offset] != std::byte{0}) : true;

                // Defence in depth: if the asset is already resident, verify the count.
                // (If it isn't loaded yet we can't validate here — do it on first submit.)
                if (const auto* asset = asset_registry->data_as<GLT::asset::mesh::mesh_asset>(mesh_component.mesh)) {
                    if (!mesh_component.overrides_match(asset->submesh_count())) {
                        LOG(warn, "mesh component override count {} doesn't match submesh count {} - clearing",
                            mesh_component.material_overrides.size(), asset->submesh_count());
                        mesh_component.material_overrides.clear();
                    }
                }
                
            });


        m_codec.register_custom_component<component::audio_source>("glt.audio_source",

            // Layout:
            //   [clip_path_length : u32][clip_path : bytes]    // length == 0 ⇒ INVALID_HANDLE
            //   [source_config    : raw bytes]                 // memcpy of GLT::asset::audio::source_config
            //   [autoplay         : u8]

            [](const entt::registry& entity_registry, entt::entity entity_handle, std::vector<std::byte>& out_bytes) {  // save

                const auto& audio_component = entity_registry.get<component::audio_source>(entity_handle);
                auto asset_registry = GLT::asset::registry::get_ref();

                write_asset_handle(asset_registry, audio_component.clip, out_bytes);

                const auto* config_bytes = reinterpret_cast<const std::byte*>(&audio_component.config);
                out_bytes.insert(out_bytes.end(), config_bytes, config_bytes + sizeof(audio_component.config));

                out_bytes.push_back(audio_component.autoplay ? std::byte{ 1 } : std::byte{ 0 });
            },

            [](entt::registry& entity_registry, entt::entity entity_handle, std::span<const std::byte> in_bytes) {      // load

                auto asset_registry = GLT::asset::registry::get_ref();
                auto& audio_component = entity_registry.emplace_or_replace<component::audio_source>(entity_handle);

                size_t offset = 0;
                audio_component.clip = read_asset_handle(asset_registry, in_bytes, offset);

                // Config was written in full at save time. If it's missing or truncated the blob came from audio_comp different
                // (or hand-edited) writer - leave the config at its default-constructed values rather than risk reading past the buffer.
                if (in_bytes.size() < offset + sizeof(audio_component.config))
                    return;

                std::memcpy(&audio_component.config, in_bytes.data() + offset, sizeof(audio_component.config));
                offset += sizeof(audio_component.config);

                // Trailing byte; optional for forward compatibility
                audio_component.autoplay = (in_bytes.size() > offset) ? (in_bytes[offset] != std::byte{ 0 }) : false;
            });


        // name_component and hierarchy own heap data, so they get explicit codecs.
        m_codec.register_custom_component<component::name>("glt.name",

            [](const entt::registry& r, entt::entity entity, std::vector<std::byte>& out) {

                const auto& n = r.get<component::name>(entity);
                const u32 len = static_cast<u32>(n.name.size());
                const auto* lp = reinterpret_cast<const std::byte*>(&len);
                out.insert(out.end(), lp, lp + sizeof(len));
                const auto* sp = reinterpret_cast<const std::byte*>(n.name.data());
                out.insert(out.end(), sp, sp + len);
            },

            [](entt::registry& r, entt::entity entity, std::span<const std::byte> data) {

                if (data.size() < sizeof(u32))
                    return;
                u32 len;
                std::memcpy(&len, data.data(), sizeof(len));
                if (data.size() < sizeof(u32) + len)
                    return;
                auto& n = r.emplace_or_replace<component::name>(entity);
                n.name.assign(reinterpret_cast<const char*>(data.data() + sizeof(u32)), len);
            });


        // hierarchy: parent pointer only. children lists are rebuilt after load by rebuild_hierarchy() so we don't duplicate
        // the tree in every blob
        m_codec.register_custom_component<component::hierarchy>("glt.hierarchy",

            [](const entt::registry& r, entt::entity entity, std::vector<std::byte>& out) {
                const auto& h = r.get<component::hierarchy>(entity);
                const auto* parent = reinterpret_cast<const std::byte*>(&h.parent);
                out.insert(out.end(), parent, parent + sizeof(h.parent));
            },

            [](entt::registry& r, entt::entity entity, std::span<const std::byte> data) {
                if (data.size() != sizeof(entity_id))
                    return;
                auto& h = r.emplace_or_replace<component::hierarchy>(entity);
                std::memcpy(&h.parent, data.data(), sizeof(entity_id));
            });

        GLT::world::register_core_presets();
        register_core_preset_factories(*this);

        LOG_LOADED
    }


    void ecs_world_plugin::on_unload() {
        
        m_preset_factories.clear();
        m_asset_handler.on_unload();
        LOG_UNLOADED
    }

    // preset factory registry -----------------------------------------------------------------------------------------

    void ecs_world_plugin::register_preset_component(std::string_view name, preset_factory_fn fn) {

        m_preset_factories[std::string(name)] = std::move(fn);
    }


    void ecs_world_plugin::unregister_preset_component(std::string_view name) noexcept {

        m_preset_factories.erase(std::string(name));
    }


    const ecs_world_plugin::preset_factory_fn* ecs_world_plugin::find_preset_component(std::string_view name) const noexcept {

        const auto it = m_preset_factories.find(std::string(name));
        return it == m_preset_factories.end() ? nullptr : &it->second;
    }


    std::expected<unique_ref<i_world>, asset::load_error> ecs_world_plugin::instantiate(asset::asset_ref asset) {

        if (!asset.valid())
            return std::unexpected{ asset::load_error::not_found };

        auto* registry = asset::registry::get_ptr();
        if (!registry)
            return std::unexpected{ asset::load_error::no_handler };

        if (!registry->data_as<asset::world::world_asset>(asset.get()))
            return std::unexpected{ asset::load_error::corrupt_header };

        auto world = create_unique_ref<ecs_world>(*this, std::move(asset));

        if (auto init = world->init(); !init)
            return std::unexpected{ init.error() };

        return unique_ref<i_world>{ std::move(world) };
    }


    std::expected<unique_ref<i_world>, asset::load_error> ecs_world_plugin::instantiate_from_path(const std::filesystem::path& path) {

        auto* registry = asset::registry::get_ptr();
        if (!registry)
            return std::unexpected{ asset::load_error::no_handler };

        auto loaded = registry->load(path);
        if (!loaded)
            return std::unexpected{ loaded.error() };

        return instantiate(std::move(*loaded));
    }


    std::expected<void, asset::load_error> ecs_world_plugin::save_world(i_world& world) { return world.save(); }


    std::expected<void, asset::load_error> ecs_world_plugin::save_world_as(i_world& world, const std::filesystem::path& path) {

        return world.save_as(path);
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
