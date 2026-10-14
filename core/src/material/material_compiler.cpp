
#include "util/pch.h"
#include "material_compiler.h"
#include "render/i_renderer.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::material_compiler {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // Decode both reflection schemas, walk the OLD param table. For each old row: find a NEW row with (name_hash, type) match
    //   match -> copy the old value bytes into the matching offset in the new blob
    //   no match -> nothing (parameter removed)
    // For each new row that has no old match: fill from `new_defaults`
    static std::vector<std::byte> repack_params(const GLT::asset::chunk_reader* old, std::span<const std::byte> new_reflection,
        std::span<const std::byte> new_defaults);

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    static std::vector<std::byte> repack_params(const GLT::asset::chunk_reader* old, std::span<const std::byte> new_reflection,
        std::span<const std::byte> new_defaults) {
            
        // TODO: implement
        return {};
    }

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    std::expected<void, GLT::asset::import_error> run(GLT::asset::asset_writer& out, std::string_view src, 
        GLT::asset::material::blend_mode blend, const GLT::asset::chunk_reader* existing) {

        namespace GLT_AM = GLT::asset::material;

        // Reach the active renderer's i_shader_compiler
        auto renderer = GLT::render::renderer::get_ref();
        VALIDATE(renderer, return std::unexpected{ GLT::asset::import_error::not_supported }, "", "Failed to get the renderer")

        auto* compiler = renderer->as<render::i_shader_compiler>();
        VALIDATE(compiler, return std::unexpected{ GLT::asset::import_error::not_supported }, "", "Failed to get the shader compiler")

        // Compile
        auto compiled = compiler->compile(src, blend);
        VALIDATE(compiled, return std::unexpected{ GLT::asset::import_error::handler_rejected }, 
            "", "matc: compile failed ({})", static_cast<u32>(compiled.error()));

        // Validate reflection blob
        if (compiled->reflection.size() < sizeof(GLT_AM::reflection_header))
            return std::unexpected{ GLT::asset::import_error::handler_rejected };

        const auto* header = reinterpret_cast<const GLT_AM::reflection_header*>(compiled->reflection.data());
        if (header->magic != GLT_AM::REFLECT_SCHEMA_MAGIC
            || header->schema_version != GLT_AM::REFLECT_SCHEMA_VERSION
            || header->contract_version != asset::material::MATERIAL_CONTRACT_VERSION)
            return std::unexpected{ GLT::asset::import_error::handler_rejected };

        if (!(header->entry_flags & GLT_AM::surface_evaluate))
            return std::unexpected{ GLT::asset::import_error::handler_rejected };

        // Blend-mode rule
        const bool has_alpha = (header->entry_flags & GLT_AM::alpha_test) != 0;
        if (blend == GLT_AM::blend_mode::masked && !has_alpha)
            return std::unexpected{ GLT::asset::import_error::handler_rejected };

        if (blend != GLT_AM::blend_mode::masked && has_alpha)
            return std::unexpected{ GLT::asset::import_error::handler_rejected };

        // Repack parameters if we have an existing asset to preserve values from
        std::vector<std::byte> final_params;
        if (existing
            && existing->has(asset::material::CHUNK_MATERIAL_REFLECT)
            && existing->has(asset::material::CHUNK_MATERIAL_PARAMS)) {

            final_params = repack_params(existing, compiled->reflection, compiled->default_params);
        } else
            final_params = std::move(compiled->default_params);

        // Emit chunks
        std::string src_z(src);
        src_z.push_back('\0');

        out.write_chunk(asset::material::CHUNK_MATERIAL_PARAMS, final_params);
        out.write_chunk(asset::material::CHUNK_MATERIAL_SLANG_SRC, std::as_bytes(std::span(src_z)));
        out.write_chunk(asset::material::CHUNK_MATERIAL_BYTECODE, compiled->bytecode);
        out.write_chunk(asset::material::CHUNK_MATERIAL_REFLECT, compiled->reflection);

        const u64 hash = compiler->implementation_hash();
        out.write_chunk(asset::material::CHUNK_MATERIAL_COMPILER_HASH, std::as_bytes(std::span(&hash, 1)));

        const u32 blend_u32 = static_cast<u32>(blend);
        out.write_chunk(asset::material::CHUNK_MATERIAL_BLEND_MODE, std::as_bytes(std::span(&blend_u32, 1)));

        return {};
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
