
#pragma once

#include "asset/material.h"
#include "render/i_shader_compiler.h"
#include "asset/i_asset_handler.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::material_compiler {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Runs the full contract-required compile+validate+repack pipeline and emits: CHUNK_MATERIAL_PARAMS / _SLANG_SRC
    // / _BYTECODE / _REFLECT / _COMPILER_HASH / _BLEND_MODE into [out]
    //
    // [existing_chunks] is null on first save, non-null when re-emitting an already-imported asset so parameter values survive
    // schema changes
    [[nodiscard]] static std::expected<void, GLT::asset::import_error> run(GLT::asset::asset_writer& out, 
        std::string_view slang_source, GLT::asset::material::blend_mode blend, const GLT::asset::chunk_reader* existing_chunks = nullptr);

}
