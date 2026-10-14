#pragma once

#include "asset/material.h"
#include "render/i_shader_compiler.h"
#include "asset/i_asset_handler.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::material_compiler {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief In-memory result of one compile pass
    struct compiled_material {

        std::vector<std::byte>                      bytecode;           // renderer-native (meaning defined by i_shader_compiler)
        std::vector<std::byte>                      reflection;         // reflection_header + param table + string table
        std::vector<std::byte>                      packed_params;      // repacked against `reflection`, edited values preserved where possible
        u64                                         compiler_hash{ 0 }; // i_shader_compiler::implementation_hash() at compile time
        GLT::asset::material::blend_mode            blend{ GLT::asset::material::blend_mode::opaque };

        // When true, the compiler short-circuited because the asset already holds a successful compile for this exact source
        // The three blobs above are empty and the caller should leave the live asset untouched
        bool                                        already_compiled{ false };
    };


    // @brief Structured failure of a material compile
    //
    // Carries both the coarse [import_error] the writer path uses, and the diagnostic message the shader compiler produced
    // The editor displays [message]; the writer path forwards [code]
    struct compile_error {

        GLT::asset::import_error                    code{ GLT::asset::import_error::handler_rejected };

        // Underlying shader-compiler code, preserved so the editor can render the "already_compiled" case as a status instead
        // of a red error
        GLT::render::shader_compile_error_code      shader_code{ GLT::render::shader_compile_error_code::none };
        std::string                                 message{};
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // @brief Compiles [slang_source] to in-memory form, validating the contract and repacking edited parameters
    //
    // [old_reflection] / [old_params] describe the previously-compiled asset. When both are non-empty, values are carried over
    // field-by-field into the new schema (matching by name_hash + type + byte_size + array_count). Parameters absent from the
    // old schema keep their compiler-provided defaults
    //
    // @param slang_source      The material's Slang source
    // @param blend             Requested blend mode; drives entry-point contract checks
    // @param old_reflection    Previously-compiled reflection blob (may be empty)
    // @param old_params        Previously-compiled packed params (may be empty)
    // @return The compiled material, or a compile_error carrying the diagnostic message
    [[nodiscard]] std::expected<compiled_material, compile_error> compile(std::string_view slang_source,
        GLT::asset::material::blend_mode blend, std::span<const std::byte> old_reflection = {}, std::span<const std::byte> old_params = {});


    // @brief Disk-emission path: compiles [slang_source] and writes every chunk a material file needs
    //
    // Thin wrapper around [compile()]. Emits CHUNK_MATERIAL_PARAMS / _SLANG_SRC / _BYTECODE / _REFLECT / _COMPILER_HASH / _BLEND_MODE
    // into [out]
    //
    // [existing_chunks] is null on first save, non-null when re-emitting an already-imported asset so parameter values survive a
    // schema change
    [[nodiscard]] std::expected<void, compile_error> run(GLT::asset::asset_writer& out, std::string_view slang_source,
        GLT::asset::material::blend_mode blend, const GLT::asset::chunk_reader* existing_chunks = nullptr);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
