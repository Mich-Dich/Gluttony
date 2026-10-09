
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

    shader device::create_shader_from_spv(const std::vector<uint32_t>& spv) {

        shader out_shader = {};
        if (spv.empty()) {

            LOG(error, "ShaderCreateInfo must have SPIRV code");
            return out_shader; // return empty shader, because no shader was created
        }

        out_shader.module = create_shader_module(spv);
        return out_shader;
    }


    void device::destroy_shader(shader &shader) { m_device.destroyShaderModule(shader.module); }


    vk::ShaderModule device::create_shader_module(const std::vector<uint32_t> &spvCode) {

        // create shader module
        auto shaderModuleCreateInfo = vk::ShaderModuleCreateInfo().setCodeSize(spvCode.size() * sizeof(uint32_t)).setPCode(spvCode.data());
        auto shaderModule = m_device.createShaderModule(shaderModuleCreateInfo);
        return shaderModule;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
