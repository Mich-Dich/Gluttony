
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

        LOG(warn, "failed to resolve asset [{}] while loading a component", asset_path.generic_string());
        return {};
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

                mesh_component.visible = (in_bytes.size() > offset) ? (in_bytes[offset] != std::byte{0}) : true;

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

                // Config was written in full at save time. If it's missing or truncated the blob came from a different
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

        LOG_LOADED
    }


    void ecs_world_plugin::on_unload() {
        m_asset_handler.on_unload();
        LOG_UNLOADED
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
