
#pragma once

#include "asset/material.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    enum class shader_compile_error_code : u32 {

        none = 0,
        slang_unavailable,          // SDK not linked / global session creation failed
        slang_parse_failed,
        slang_link_failed,
        contract_version_mismatch,
        missing_surface_evaluate,   // material lacks the mandatory entry point
        unsupported_param_type,
        reflection_schema_error,
        backend_error,
        already_compiled,           // asset already holds a successful compile for this exact source; no work was done
    };


    struct shader_compile_error {

        shader_compile_error_code               code = shader_compile_error_code::none;
        std::string                             message{};
    };


    // Renderer-native compiled shader, produced by a renderer's i_shader_compiler.
    struct compiled_shader {

        // simple binary blob, how the renderer interprets it is up to them, exists purly to ease compile time at runtime
        std::vector<std::byte>                  bytecode;               // SPIR-V for Vulkan, DXIL for D3D12, ...

        std::vector<std::byte>                  reflection;             // GLT::material::reflect_header + param table + string table
        std::vector<std::byte>                  default_params;         // packed per the reflection schema
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // A renderer's Slang-to-native compiler. Registered as a service by the renderer plugin. The engine's matc uses this.
    class i_shader_compiler {
    public:

        virtual ~i_shader_compiler() = default;

        // Renderer name for diagnostics ("vulkan_rasterizer", "d3d12", ...)
        [[nodiscard]] virtual std::string_view name() const = 0;

        // Hash of the compiler implementation. Must change whenever a change would invalidate bytecode from a previous version
        // Covers: Slang toolchain version, target IR version, reflection schema version, renderer codegen logic
        [[nodiscard]] virtual u64 implementation_hash() const = 0;

        // The contract version this compiler emits
        [[nodiscard]] virtual u32 contract_version() const = 0;

        // Compile one material's Slang source (which imports material_contract_v0).  On success, bytecode + reflection + default_params are all populated
        [[nodiscard]] virtual std::expected<compiled_shader, shader_compile_error> compile(std::string_view slang_source,
            GLT::asset::material::blend_mode blend = GLT::asset::material::blend_mode::opaque) = 0;

    };

}
