#pragma once

#include <asset/i_asset_registry.h>
#include <asset/material.h>
#include <render/i_renderer.h>
#include <render/i_shader_compiler.h>



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

        // Every fallback below sets this. The renderer checks `compiler_hash_matches` and recompiles when false
        bool needs_recompile = false;

        // ---- slang source (missing => shipped default) --------------------------------------------------------------

        std::string slang_source;
        {
            const auto slang_bytes = reader.get(CHUNK_MATERIAL_SLANG_SRC);

            std::size_t len = slang_bytes.size();
            while (len > 0 && slang_bytes[len - 1] == std::byte{ 0 })
                len--;

            if (len == 0) {
                LOG(warn, "[{}] CHUNK_MATERIAL_SLANG_SRC missing or empty - falling back to DEFAULT_SLANG_SOURCE", info.name);
                slang_source = DEFAULT_SLANG_SOURCE;
                needs_recompile = true;
            } else {
                slang_source.assign(reinterpret_cast<const char*>(slang_bytes.data()), len);
            }
        }

        // ---- reflection (missing / too small / bad magic => drop, force recompile) ---------------------------------
        // `refl_hdr` is only valid while `reflect_bytes` (a span into the reader) is alive, i.e. within this call

        const auto reflect_bytes = reader.get(CHUNK_MATERIAL_REFLECT);
        const reflection_header* refl_hdr = nullptr;

        if (reflect_bytes.size() < sizeof(reflection_header)) {
            LOG(warn, "[{}] CHUNK_MATERIAL_REFLECT missing or too small ({} byte(s)) - recompile required", info.name, reflect_bytes.size())
            needs_recompile = true;
        } else {
            refl_hdr = reinterpret_cast<const reflection_header*>(reflect_bytes.data());
            if (refl_hdr->magic != REFLECT_SCHEMA_MAGIC) {
                LOG(warn, "[{}] CHUNK_MATERIAL_REFLECT has bad magic (0x{:08X}) - recompile required", info.name, refl_hdr->magic)
                refl_hdr = nullptr;
                needs_recompile = true;
            }
        }

        // ---- packed params (size must agree with reflection; else drop, force recompile) ---------------------------

        const auto params_bytes = reader.get(CHUNK_MATERIAL_PARAMS);
        bool params_ok = false;

        if (!refl_hdr) {
            // Reflection is already gone - params are unusable without it
            if (!params_bytes.empty())
                LOG(warn, "[{}] CHUNK_MATERIAL_PARAMS present but reflection is unusable - dropping params", info.name)
            needs_recompile = true;
        } else if (params_bytes.size() != refl_hdr->packed_param_size) {
            LOG(warn, "[{}] CHUNK_MATERIAL_PARAMS size {} != reflection packed_param_size {} - recompile required", 
                info.name, params_bytes.size(), refl_hdr->packed_param_size)
            needs_recompile = true;
        } else
            params_ok = true;

        // ---- build the runtime asset -------------------------------------------------------------------------------

        auto asset = std::make_unique<GLT::asset::material::material_asset>();
        asset->asset_type = info.asset_type;
        asset->name = info.name;
        asset->slang_source = std::move(slang_source);

        if (refl_hdr)
            asset->reflection.assign(reflect_bytes.begin(), reflect_bytes.end());

        if (params_ok)
            asset->packed_params.assign(params_bytes.begin(), params_bytes.end());

        // ---- bytecode (missing => force recompile) -----------------------------------------------------------------

        if (const auto bytecode_bytes = reader.get(CHUNK_MATERIAL_BYTECODE); !bytecode_bytes.empty()) {
            asset->bytecode.assign(bytecode_bytes.begin(), bytecode_bytes.end());
        } else {
            needs_recompile = true;
        }

        // ---- compiler hash (missing => 0, force recompile) ---------------------------------------------------------

        if (const auto h = reader.get_as<u64>(CHUNK_MATERIAL_COMPILER_HASH); h.size() == 1) {
            asset->compiler_hash = h[0];
        } else {
            LOG(warn, "[{}] CHUNK_MATERIAL_COMPILER_HASH missing - treating as stale", info.name)
            needs_recompile = true;
        }

        // ---- blend mode (absent / out-of-range => opaque) ----------------------------------------------------------

        asset->blend = blend_mode::opaque;
        if (const auto b = reader.get_as<u32>(CHUNK_MATERIAL_BLEND_MODE); b.size() == 1) {
            const auto bm = static_cast<blend_mode>(b[0]);
            if (static_cast<u8>(bm) < static_cast<u8>(blend_mode::count))
                asset->blend = bm;
            else
                LOG(warn, "[{}] CHUNK_MATERIAL_BLEND_MODE out of range ({}) - defaulting to opaque", info.name, b[0])
        }

        // ---- texture refs + dependency resolution -------------------------------------------------------------------

        asset->textures.fill(INVALID_HANDLE);
        const auto refs = reader.get_as<texture_ref>(CHUNK_TEXTURE_REFS);

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
            if (slot >= static_cast<size_t>(texture_slot::count)) {
                LOG(warn, "[{}] texture_ref names slot {} which is out of range - skipping", info.name, slot)
                continue;
            }

            if (auto it = by_id.find(ref.id); it != by_id.end())
                asset->textures[slot] = it->second;
            else
                LOG(warn, "[{}] texture_ref for slot {} references UUID {} which was not resolved as a dependency",
                    info.name, GLT::util::enum_to_string(ref.slot), GLT::util::to_string(ref.id))
        }

        // ---- final state -------------------------------------------------------------------------------------------

        const auto shader_compiler = GLT::render::renderer::get_ref()->as<GLT::render::i_shader_compiler>();
        if (asset->compiler_hash != shader_compiler->implementation_hash())
            needs_recompile = true;

        asset->compiler_hash_matches = !needs_recompile;

        if (needs_recompile)
            LOG(warn, "[{}] material loaded with issues - recompile flagged", info.name)
        else
            LOG(info, "loaded material [{}] - {} byte(s) bytecode, {} param byte(s), {} texture ref(s)",
                info.name, asset->bytecode.size(), asset->packed_params.size(), refs.size())

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
