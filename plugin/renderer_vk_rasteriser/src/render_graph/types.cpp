
#include "util/pch.h"
#include "types.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::graph {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    bool texture_handle::operator==(const texture_handle& o) const { return id == o.id; }

    bool buffer_handle::operator==(const buffer_handle& o) const { return id == o.id; }

    bool resource_state::operator==(const resource_state& o) const { return layout == o.layout && access == o.access && stage == o.stage; }

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    resource_state derive_state(texture_usage u) {

        using L = vk::ImageLayout;
        using A = vk::AccessFlagBits2;
        using S = vk::PipelineStageFlagBits2;
        switch (u) {
            case texture_usage::sampled:
                return {L::eShaderReadOnlyOptimal, A::eShaderRead, S::eFragmentShader | S::eComputeShader | S::eVertexShader};

            case texture_usage::storage_read:
                return {L::eGeneral, A::eShaderRead, S::eComputeShader | S::eFragmentShader};

            case texture_usage::storage_readwrite:
                return {L::eGeneral, A::eShaderRead | A::eShaderWrite, S::eComputeShader | S::eFragmentShader};

            case texture_usage::color_attachment:
                return {L::eColorAttachmentOptimal, A::eColorAttachmentWrite, S::eColorAttachmentOutput};

            case texture_usage::depth_attachment:
                return {L::eDepthStencilAttachmentOptimal, A::eDepthStencilAttachmentWrite, S::eEarlyFragmentTests | S::eLateFragmentTests};

            case texture_usage::depth_readonly:
                return {L::eDepthStencilReadOnlyOptimal, A::eDepthStencilAttachmentRead, S::eEarlyFragmentTests | S::eLateFragmentTests};

            case texture_usage::transfer_src:
                return {L::eTransferSrcOptimal, A::eTransferRead, S::eTransfer};

            case texture_usage::transfer_dst:
                return {L::eTransferDstOptimal, A::eTransferWrite, S::eTransfer};

            case texture_usage::present:
                return {L::ePresentSrcKHR, {}, S::eNone};
        }
        return {};
    }


    resource_state derive_state(buffer_usage u) {

        using A = vk::AccessFlagBits2;
        using S = vk::PipelineStageFlagBits2;
        using I = vk::ImageLayout;

        switch (u) {
            case buffer_usage::uniform_read:      return {I::eUndefined, A::eUniformRead, S::eAllGraphics | S::eComputeShader};
            case buffer_usage::storage_read:      return {I::eUndefined, A::eShaderRead, S::eAllGraphics | S::eComputeShader};
            case buffer_usage::storage_readwrite: return {I::eUndefined, A::eShaderRead | A::eShaderWrite, S::eAllGraphics | S::eComputeShader};
            case buffer_usage::transfer_src:      return {I::eUndefined, A::eTransferRead, S::eTransfer};
            case buffer_usage::transfer_dst:      return {I::eUndefined, A::eTransferWrite, S::eTransfer};
            case buffer_usage::vertex:            return {I::eUndefined, A::eVertexAttributeRead, S::eVertexInput};
            case buffer_usage::index:             return {I::eUndefined, A::eIndexRead, S::eIndexInput};
            case buffer_usage::indirect:          return {I::eUndefined, A::eIndirectCommandRead, S::eDrawIndirect};
        }
        return {};
    }


    resource_state merge(texture_usage a, texture_usage b) {

        resource_state state_a = derive_state(a);
        resource_state state_b = derive_state(b);
        resource_state state{};
        state.access = state_a.access | state_b.access;
        state.stage  = state_a.stage  | state_b.stage;
        
        // Prefer the "more writable / more general" layout. Order matters
        auto pick = [](vk::ImageLayout x, vk::ImageLayout y) {
            // If one is Undefined, pick the other
            if (x == vk::ImageLayout::eUndefined)                                   return y;
            if (y == vk::ImageLayout::eUndefined)                                   return x;
            if (x == vk::ImageLayout::eGeneral || y == vk::ImageLayout::eGeneral)   return vk::ImageLayout::eGeneral;
            // Otherwise pick x (they're assumed the same for this application)
            return x;
        };
        state.layout = pick(state_a.layout, state_b.layout);
        return state;
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
