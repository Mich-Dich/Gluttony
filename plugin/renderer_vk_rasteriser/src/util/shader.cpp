
#include <util/pch.h>

#include "util/device.h"

#include "shader.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::util {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    shader device::create_shader_from_spv(std::span<const std::byte> spv) {

        shader out_shader = {};
        if (spv.empty()) {
            LOG(error, "create_shader_from_spv: SPIR-V blob is empty")
            return out_shader;
        }

        out_shader.module = create_shader_module(spv);
        return out_shader;
    }


    shader device::create_shader_from_spv(std::span<const u32> spv) {

        shader out_shader = {};
        if (spv.empty()) {
            LOG(error, "create_shader_from_spv: SPIR-V blob is empty")
            return out_shader;
        }

        out_shader.module = create_shader_module(spv);
        return out_shader;
    }


    void device::destroy_shader(shader& shader) { m_device.destroyShaderModule(shader.module); }


    vk::ShaderModule device::create_shader_module(std::span<const std::byte> spvCode) {

        // ShaderModuleCreateInfo::pCode is a uint32_t*, so the byte view has to satisfy two preconditions before we can reinterpret it:
        //   - codeSize must be a multiple of 4 (SPIR-V is word-oriented)
        //   - the pointer must be 4-byte aligned, otherwise the reinterpret_cast is UB
        // std::vector<std::byte>'s allocator guarantees alignment >= alignof(std::max_align_t) in practice, but a caller handing us
        // a sub-span of a larger buffer might not
        if (spvCode.empty() || (spvCode.size() % sizeof(u32)) != 0) {
            LOG(error, "create_shader_module: SPIR-V blob must be non-empty and a multiple of 4 bytes (got {} byte(s))", spvCode.size())
            return nullptr;
        }

        const auto* code = reinterpret_cast<const u32*>(spvCode.data());

        if (reinterpret_cast<std::uintptr_t>(code) % alignof(u32) != 0) {
            LOG(error, "create_shader_module: SPIR-V blob is not 4-byte aligned - cannot reinterpret as uint32_t*")
            return nullptr;
        }

        auto shaderModuleCreateInfo = vk::ShaderModuleCreateInfo()
            .setCodeSize(spvCode.size())
            .setPCode(code);

        return m_device.createShaderModule(shaderModuleCreateInfo);
    }


    vk::ShaderModule device::create_shader_module(std::span<const u32> spvCode) {

        if (spvCode.empty()) {
            LOG(error, "create_shader_module: SPIR-V blob is empty")
            return nullptr;
        }

        auto shaderModuleCreateInfo = vk::ShaderModuleCreateInfo()
            .setCodeSize(spvCode.size_bytes())   // == size() * sizeof(u32)
            .setPCode(spvCode.data());

        return m_device.createShaderModule(shaderModuleCreateInfo);
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
