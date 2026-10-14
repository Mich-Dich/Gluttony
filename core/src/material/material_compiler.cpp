#include "util/pch.h"
#include "material_compiler.h"
#include "render/i_renderer.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::material_compiler {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // @brief Preserve the user's edited parameter values across a schema change produced by a recompile.
    //
    // Walks the NEW reflection table; for each row tries to find a matching row in the OLD table keyed by
    // (name_hash, type, byte_size, array_count). On a match, the old value bytes are copied into the new layout
    // at the row's declared offset. Everything else keeps its compiler-provided default.
    //
    // A mismatch between old/new blob sizes or a missing header just returns the defaults unchanged
    static std::vector<std::byte> repack_params(std::span<const std::byte> old_reflection, std::span<const std::byte> old_params,
        std::span<const std::byte> new_reflection, std::span<const std::byte> new_defaults);


    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static std::vector<std::byte> repack_params(std::span<const std::byte> old_reflection, std::span<const std::byte> old_params,
        std::span<const std::byte> new_reflection, std::span<const std::byte> new_defaults) {

        using namespace GLT::asset::material;

        std::vector<std::byte> out(new_defaults.begin(), new_defaults.end());

        if (old_reflection.size() < sizeof(reflection_header)
            || new_reflection.size() < sizeof(reflection_header))
            return out;

        const auto* old_hdr = reinterpret_cast<const reflection_header*>(old_reflection.data());
        const auto* new_hdr = reinterpret_cast<const reflection_header*>(new_reflection.data());

        if (old_hdr->magic != REFLECT_SCHEMA_MAGIC || new_hdr->magic != REFLECT_SCHEMA_MAGIC)
            return out;

        if (old_params.size() != old_hdr->packed_param_size)
            return out;

        if (out.size() != new_hdr->packed_param_size)
            return out;

        const auto* old_table = reinterpret_cast<const reflection_param*>(old_reflection.data() + old_hdr->param_table_offset);
        const auto* new_table = reinterpret_cast<const reflection_param*>(new_reflection.data() + new_hdr->param_table_offset);

        for (u32 i = 0; i < new_hdr->param_count; ++i) {

            const auto& np = new_table[i];
            const reflection_param* match = nullptr;

            for (u32 j = 0; j < old_hdr->param_count; ++j) {
                const auto& op = old_table[j];
                if (op.name_hash == np.name_hash
                    && op.type == np.type
                    && op.byte_size == np.byte_size
                    && op.array_count == np.array_count) {
                    match = &op;
                    break;
                }
            }

            if (!match)
                continue;   // new parameter - keep the compiler's default

            if (static_cast<std::size_t>(match->byte_offset) + match->byte_size > old_params.size())
                continue;
            if (static_cast<std::size_t>(np.byte_offset) + np.byte_size > out.size())
                continue;

            std::memcpy(out.data() + np.byte_offset, old_params.data() + match->byte_offset, np.byte_size);
        }

        return out;
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    std::expected<compiled_material, compile_error> compile(std::string_view slang_source, GLT::asset::material::blend_mode blend,
        std::span<const std::byte> old_reflection, std::span<const std::byte> old_params) {

        namespace AM = GLT::asset::material;
        #define RETURN_ERROR(type, message)                                                                         \
            return std::unexpected{ compile_error{                                                                  \
                GLT::asset::import_error::type,                                                                     \
                GLT::render::shader_compile_error_code::none,                                                       \
                message                                                                                             \
            } };

        // Reach the active renderer's i_shader_compiler ---------------------------------------------------------------
        auto renderer = GLT::render::renderer::get_ref();
        if (!renderer)
            RETURN_ERROR(not_supported, "no renderer available")

        auto* compiler = renderer->as<GLT::render::i_shader_compiler>();
        if (!compiler)
            RETURN_ERROR(not_supported, "active renderer does not expose i_shader_compiler")

        // Compile -----------------------------------------------------------------------------------------------------
        auto compiled = compiler->compile(slang_source, blend);
        if (!compiled) {

            // "Already compiled" isn't a failure: it means the asset still holds a valid compile for this exact source.
            // Report it as a successful no-op so the editor can display the appropriate status
            if (compiled.error().code == GLT::render::shader_compile_error_code::already_compiled) {

                compiled_material noop{};
                noop.already_compiled = true;
                noop.blend = blend;
                return noop;
            }

            // Forward the shader compiler's diagnostic verbatim
            return std::unexpected{ compile_error{
                GLT::asset::import_error::handler_rejected,
                compiled.error().code,
                std::format("shader compile failed [{}]: {}", GLT::util::enum_to_string(compiled.error().code), compiled.error().message)
            } };
        }

        // Validate the returned reflection blob -----------------------------------------------------------------------
        if (compiled->reflection.size() < sizeof(AM::reflection_header))
            RETURN_ERROR(handler_rejected, "compiler returned a reflection blob that is too small")

        const auto* hdr = reinterpret_cast<const AM::reflection_header*>(compiled->reflection.data());
        if (hdr->magic != AM::REFLECT_SCHEMA_MAGIC)
            RETURN_ERROR(handler_rejected, std::format("reflection magic mismatch (0x{:08X})", hdr->magic))

        if (hdr->schema_version != AM::REFLECT_SCHEMA_VERSION)
            RETURN_ERROR(handler_rejected, std::format("reflection schema version mismatch ({} vs {})", hdr->schema_version, AM::REFLECT_SCHEMA_VERSION))

        if (hdr->contract_version != AM::MATERIAL_CONTRACT_VERSION)
            RETURN_ERROR(handler_rejected, std::format("contract version mismatch ({} vs {})", hdr->contract_version, AM::MATERIAL_CONTRACT_VERSION))

        if (!(hdr->entry_flags & AM::surface_evaluate))
            RETURN_ERROR(handler_rejected, "material does not declare the mandatory surface_evaluate() entry point")

        // Blend-mode / entry-point contract ---------------------------------------------------------------------------
        const bool has_alpha = (hdr->entry_flags & AM::alpha_test) != 0;
        if (blend == AM::blend_mode::masked && !has_alpha)
            RETURN_ERROR(handler_rejected, "masked blend mode requires an alpha_test() entry point")

        if (blend != AM::blend_mode::masked && has_alpha)
            RETURN_ERROR(handler_rejected, "alpha_test() declared but blend mode is not masked")

        // Repack parameters preserving edited values across the schema change -----------------------------------------
        std::vector<std::byte> final_params;
        if (!old_reflection.empty() && !old_params.empty())
            final_params = repack_params(old_reflection, old_params, compiled->reflection, compiled->default_params);
        else
            final_params = std::move(compiled->default_params);

        #undef RETURN_ERROR

        compiled_material out;
        out.bytecode = std::move(compiled->bytecode);
        out.reflection = std::move(compiled->reflection);
        out.packed_params = std::move(final_params);
        out.compiler_hash = compiler->implementation_hash();
        out.blend = blend;
        return out;
    }


    std::expected<void, compile_error> run(GLT::asset::asset_writer& out, std::string_view src, GLT::asset::material::blend_mode blend,
        const GLT::asset::chunk_reader* existing) {

        namespace AM = GLT::asset::material;

        // recover the existing reflection + params so edited values survive the recompile
        std::vector<std::byte> old_reflection;
        std::vector<std::byte> old_params;

        if (existing && existing->has(AM::CHUNK_MATERIAL_REFLECT) && existing->has(AM::CHUNK_MATERIAL_PARAMS)) {

            const auto r = existing->get(AM::CHUNK_MATERIAL_REFLECT);
            old_reflection.assign(r.begin(), r.end());

            const auto p = existing->get(AM::CHUNK_MATERIAL_PARAMS);
            old_params.assign(p.begin(), p.end());
        }

        auto compiled = compile(src, blend, old_reflection, old_params);
        if (!compiled) {
            LOG(error, "matc: compile failed - {}", compiled.error().message);
            return std::unexpected{ compiled.error() };
        }

        // A short-circuited compile means the renderer cache is authoritative, but [run()] is only ever called when the caller
        // has already accepted that the file needs writing. Treat it as a failure to avoid overwriting an existing asset with
        // empty chunks
        if (compiled->already_compiled) {
            return std::unexpected{ compile_error{
                GLT::asset::import_error::handler_rejected,
                GLT::render::shader_compile_error_code::already_compiled,
                "matc: cannot emit an already-compiled material; clear the renderer cache or edit the source"
            } };
        }

        // emit chunks
        std::string src_z(src);
        src_z.push_back('\0');
        
        const u32 blend_u32 = static_cast<u32>(compiled->blend);
        out.write_chunk(AM::CHUNK_MATERIAL_PARAMS, compiled->packed_params);
        out.write_chunk(AM::CHUNK_MATERIAL_SLANG_SRC, std::as_bytes(std::span(src_z)));
        out.write_chunk(AM::CHUNK_MATERIAL_BYTECODE, compiled->bytecode);
        out.write_chunk(AM::CHUNK_MATERIAL_REFLECT, compiled->reflection);
        out.write_chunk(AM::CHUNK_MATERIAL_COMPILER_HASH, std::as_bytes(std::span(&compiled->compiler_hash, 1)));
        out.write_chunk(AM::CHUNK_MATERIAL_BLEND_MODE, std::as_bytes(std::span(&blend_u32, 1)));

        return {};
    }

}
