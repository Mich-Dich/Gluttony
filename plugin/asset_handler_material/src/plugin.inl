#pragma once

#include <asset/i_asset_registry.h>
#include <asset/material.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::handler::material {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION =================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    FORCE_INLINE void plugin::on_load() {

        m_registry = GLT::asset::registry::get_ref();
        VALIDATE(m_registry, return, "", "asset_registry not available - handler inactive")
        m_registry->register_handler(this);
        LOG_LOADED
    }


    FORCE_INLINE void plugin::on_unload() {

        if (m_registry)
            m_registry->unregister_handler(this);

        m_registry.reset();
        LOG_UNLOADED
    }


    FORCE_INLINE_R std::span<const GLT::asset::type> plugin::types() const noexcept {

        // Only the base material type. material_instance, if it ever ships, would live in a separate handler since the chunk layout differs.
        static constexpr GLT::asset::type t[] = { GLT::asset::core_types::material, };
        return t;
    }


    FORCE_INLINE_R std::expected<GLT::unique_ref<GLT::asset::i_runtime_asset>, GLT::asset::load_error> plugin::deserialize(
        const GLT::asset::info& info, GLT::asset::chunk_reader& reader) {

        // required chunk ----------------------------------------------------------------------------------------------

        const auto params_span = reader.get_as<GLT::asset::material::material_params>(GLT::asset::material::CHUNK_MATERIAL_PARAMS);
        VALIDATE(params_span.size() == 1, return std::unexpected{ GLT::asset::load_error::corrupt_header }, "",
            "[{}] missing or malformed material_params chunk (got {})", info.name, params_span.size())

        // optional texture refs ---------------------------------------------------------------------------------------

        const auto refs = reader.get_as<GLT::asset::material::texture_ref>(GLT::asset::material::CHUNK_TEXTURE_REFS);

        // build runtime asset -----------------------------------------------------------------------------------------

        auto asset = std::make_unique<GLT::asset::material::material_asset>();
        asset->asset_type = info.asset_type;
        asset->params = params_span[0];
        asset->name = info.name;
        asset->textures.fill(INVALID_HANDLE);

        // Resolve each texture_ref's UUID against the deps that the registry already loaded for us. info.dependencies is positional
        // against the on-disk dep table; info.dependencies[i] is the handle for whatever UUID row i named, or INVALID_HANDLE when it
        // failed to resolve. The handler only knows the UUIDs, so build the reverse map by asking the registry.
        std::unordered_map<UUID, GLT::asset::handle> by_id;
        by_id.reserve(info.dependencies.size());

        for (GLT::asset::handle dep : info.dependencies) {
            if (dep == INVALID_HANDLE)
                continue;
            by_id.emplace(m_registry->info(dep).id, dep);
        }

        for (const auto& ref : refs) {

            const auto slot = static_cast<size_t>(ref.slot);
            if (slot >= static_cast<size_t>(GLT::asset::material::texture_slot::count)) {
                LOG(warn, "[{}] texture_ref names slot {} which is out of range - skipping", info.name, slot);
                continue;
            }

            if (auto it = by_id.find(ref.id); it != by_id.end())
                asset->textures[slot] = it->second;
            else
                LOG(warn, "[{}] texture_ref for slot {} references UUID {} which was not resolved as a dependency", 
                    info.name, GLT::util::enum_to_string(ref.slot), GLT::util::to_string(ref.id))
        }

        LOG(info, "loaded material [{}] - {} texture ref(s)", info.name, refs.size());

        return GLT::unique_ref<GLT::asset::i_runtime_asset>(std::move(asset));
    }


    FORCE_INLINE_R std::expected<void, GLT::asset::load_error> plugin::serialize(const GLT::asset::info& info,
        const GLT::asset::i_runtime_asset& asset, GLT::asset::asset_writer& out) const {

        // This handler owns core_types::material; the registry only ever hands us a runtime asset of that type, so a static_cast is safe.
        const auto& mat = static_cast<const GLT::asset::material::material_asset&>(asset);

        // required chunk ----------------------------------------------------------------------------------------------

        out.write_chunk(GLT::asset::material::CHUNK_MATERIAL_PARAMS, std::as_bytes(std::span{ &mat.params, 1 }));

        // optional texture refs ---------------------------------------------------------------------------------------

        std::vector<GLT::asset::material::texture_ref> refs;
        refs.reserve(static_cast<size_t>(GLT::asset::material::texture_slot::count));

        for (size_t index = 0; index < static_cast<size_t>(GLT::asset::material::texture_slot::count); ++index) {

            const GLT::asset::handle tex = mat.textures[index];
            if (tex == INVALID_HANDLE)
                continue;

            // The registry must still be holding this texture. If it's gone (unloaded between the material's load and its save),
            // drop the ref - better a material with a missing texture than a broken file.
            if (!m_registry->is_loaded(tex)) {
                LOG(warn, "[{}] texture slot {} points at an unloaded asset - omitting from save", info.name, index);
                continue;
            }

            const auto& tex_info = m_registry->info(tex);

            GLT::asset::material::texture_ref ref{};
            ref.slot = static_cast<GLT::asset::material::texture_slot>(index);
            ref.id = tex_info.id;
            refs.push_back(ref);

            // Re-declare the dependency so a cold load can find it via the path even if the registry's id map hasn't been warmed up yet.
            out.declare_dependency(tex_info.id, tex_info.virtual_path.generic_string(), tex_info.asset_type);
        }

        if (!refs.empty())
            out.write_chunk(GLT::asset::material::CHUNK_TEXTURE_REFS, std::as_bytes(std::span{ refs.data(), refs.size() }));

        return {};
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
