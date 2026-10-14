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

        // Only the base material type. material_instance, if it ever ships, would live in a separate handler since the chunk layout differs
        static constexpr GLT::asset::type t[] = { GLT::asset::core_types::material, };
        return t;
    }


    FORCE_INLINE_R std::expected<GLT::unique_ref<GLT::asset::i_runtime_asset>, GLT::asset::load_error> plugin::deserialize(
        const GLT::asset::info& info, GLT::asset::chunk_reader& reader) {

        using namespace GLT::asset::material;

        // required: slang source --------------------------------------------------------------------------------------

        const auto slang_bytes = reader.get(CHUNK_MATERIAL_SLANG_SRC);
        VALIDATE(!slang_bytes.empty(), return std::unexpected{ GLT::asset::load_error::corrupt_header }, 
            "", "[{}] missing or empty CHUNK_MATERIAL_SLANG_SRC", (info.name))

        // required: reflection schema ---------------------------------------------------------------------------------

        const auto reflect_bytes = reader.get(CHUNK_MATERIAL_REFLECT);
        VALIDATE(reflect_bytes.size() >= sizeof(reflection_header), return std::unexpected{ GLT::asset::load_error::corrupt_header }, 
            "", "[{}] CHUNK_MATERIAL_REFLECT is missing or too small ({} byte(s))", (info.name, reflect_bytes.size()))

        const auto* refl_hdr = reinterpret_cast<const reflection_header*>(reflect_bytes.data());
        VALIDATE(refl_hdr->magic == REFLECT_SCHEMA_MAGIC, return std::unexpected{ GLT::asset::load_error::corrupt_header }, 
            "", "[{}] CHUNK_MATERIAL_REFLECT has bad magic (0x{:08X})", (info.name, refl_hdr->magic))

        // required: packed params -------------------------------------------------------------------------------------

        const auto params_bytes = reader.get(CHUNK_MATERIAL_PARAMS);
        VALIDATE(params_bytes.size() == refl_hdr->packed_param_size, return std::unexpected{ GLT::asset::load_error::corrupt_header }, 
            "", "[{}] CHUNK_MATERIAL_PARAMS size {} does not match reflection's packed_param_size {}", 
            (info.name, params_bytes.size(), refl_hdr->packed_param_size))

        // build runtime asset -----------------------------------------------------------------------------------------

        auto asset = std::make_unique<GLT::asset::material::material_asset>();
        asset->asset_type = info.asset_type;
        asset->name = info.name;

        // slang source: NUL-stripped
        {
            std::size_t len = slang_bytes.size();
            while (len > 0 && slang_bytes[len - 1] == std::byte{ 0 })
                --len;
            asset->slang_source.assign(reinterpret_cast<const char*>(slang_bytes.data()), len);
        }

        // derived artifacts - copy verbatim
        if (const auto bytecode_bytes = reader.get(CHUNK_MATERIAL_BYTECODE); !bytecode_bytes.empty())
            asset->bytecode.assign(bytecode_bytes.begin(), bytecode_bytes.end());

        asset->reflection.assign(reflect_bytes.begin(), reflect_bytes.end());
        asset->packed_params.assign(params_bytes.begin(), params_bytes.end());

        // compiler hash -----------------------------------------------------------------------------------------------

        if (const auto h = reader.get_as<u64>(CHUNK_MATERIAL_COMPILER_HASH); h.size() == 1)
            asset->compiler_hash = h[0];

        // blend mode - absent => opaque, per contract -----------------------------------------------------------------

        asset->blend = blend_mode::opaque;
        if (const auto b = reader.get_as<u32>(CHUNK_MATERIAL_BLEND_MODE); b.size() == 1) {
            const auto bm = static_cast<blend_mode>(b[0]);
            VALIDATE(static_cast<u8>(bm) < static_cast<u8>(blend_mode::count), return std::unexpected{ GLT::asset::load_error::corrupt_header }, 
                "", "[{}] CHUNK_MATERIAL_BLEND_MODE has out-of-range value {}", (info.name, b[0]))
            asset->blend = bm;
        }

        // texture refs + dependency resolution ------------------------------------------------------------------------

        asset->textures.fill(INVALID_HANDLE);
        const auto refs = reader.get_as<texture_ref>(CHUNK_TEXTURE_REFS);

        // info.dependencies is positional against info.dependency_ids; build the reverse map by UUID
        std::unordered_map<UUID, GLT::asset::handle> by_id;
        by_id.reserve(info.dependencies.size());

        for (size_t i = 0; i < info.dependencies.size(); ++i) {
            const GLT::asset::handle dep = info.dependencies[i];
            if (dep == INVALID_HANDLE)
                continue;
            by_id.emplace(info.dependency_ids[i], dep);
        }

        for (const auto& ref : refs) {

            const auto slot = static_cast<size_t>(ref.slot);
            VALIDATE(slot < static_cast<size_t>(texture_slot::count), continue, 
                "", "[{}] texture_ref names slot {} which is out of range - skipping", (info.name, slot));

            if (auto it = by_id.find(ref.id); it != by_id.end())
                asset->textures[slot] = it->second;
            else
                LOG(warn, "[{}] texture_ref for slot {} references UUID {} which was not resolved as a dependency",
                    info.name, GLT::util::enum_to_string(ref.slot), GLT::util::to_string(ref.id))
        }

        // The renderer sets compiler_hash_matches when it has the active compiler in hand. Here we can only say
        // "there is bytecode to try". If the file carried no bytecode at all, force a recompile
        asset->compiler_hash_matches = !asset->bytecode.empty();

        LOG(info, "loaded material [{}] - {} byte(s) bytecode, {} param byte(s), {} texture ref(s)",
            info.name, asset->bytecode.size(), asset->packed_params.size(), refs.size());

        return GLT::unique_ref<GLT::asset::i_runtime_asset>(std::move(asset));
    }


    [[nodiscard]] std::expected<void, GLT::asset::load_error> plugin::serialize(const GLT::asset::info& info,
        const GLT::asset::i_runtime_asset& asset, GLT::asset::asset_writer& out) const {

        using namespace GLT::asset::material;

        // This handler owns core_types::material; the registry only ever hands us a runtime asset of that type,
        // so a static_cast is safe.
        const auto& mat = static_cast<const GLT::asset::material::material_asset&>(asset);

        // slang source (NUL-terminated on disk, per contract) ---------------------------------------------------------

        {
            std::string src = mat.slang_source;
            src.push_back('\0');
            out.write_chunk(CHUNK_MATERIAL_SLANG_SRC,
                std::as_bytes(std::span{ src.data(), src.size() }));
        }

        // derived artifacts - pass through verbatim. A never-compiled material legitimately has none of these;
        // material_compiler is expected to fill them in before the asset is committed.
        if (!mat.bytecode.empty())
            out.write_chunk(CHUNK_MATERIAL_BYTECODE, std::span<const std::byte>{ mat.bytecode });

        if (!mat.reflection.empty())
            out.write_chunk(CHUNK_MATERIAL_REFLECT, std::span<const std::byte>{ mat.reflection });

        if (!mat.packed_params.empty())
            out.write_chunk(CHUNK_MATERIAL_PARAMS, std::span<const std::byte>{ mat.packed_params });

        // compiler hash - always present, even for uncompiled materials (0 == "no compiler")
        out.write_chunk(CHUNK_MATERIAL_COMPILER_HASH, std::as_bytes(std::span{ &mat.compiler_hash, 1 }));

        // blend mode - authored, not derived
        {
            const u32 blend = static_cast<u32>(mat.blend);
            out.write_chunk(CHUNK_MATERIAL_BLEND_MODE, std::as_bytes(std::span{ &blend, 1 }));
        }

        // texture refs + dependency declarations ----------------------------------------------------------------------

        std::vector<texture_ref> refs;
        refs.reserve(static_cast<size_t>(texture_slot::count));

        for (size_t index = 0; index < static_cast<size_t>(texture_slot::count); ++index) {

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

            texture_ref ref{};
            ref.slot = static_cast<texture_slot>(index);
            ref.id = tex_info.id;
            refs.push_back(ref);

            // Re-declare the dependency so a cold load can find it via the path even if the registry's id map hasn't been warmed up yet.
            out.declare_dependency(tex_info.id, tex_info.virtual_path.generic_string(), tex_info.asset_type);
        }

        if (!refs.empty())
            out.write_chunk(CHUNK_TEXTURE_REFS, std::as_bytes(std::span{ refs.data(), refs.size() }));

        return {};
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
