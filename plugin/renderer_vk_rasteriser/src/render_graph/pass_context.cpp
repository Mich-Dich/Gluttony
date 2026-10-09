
#include "util/pch.h"

#include "pass_context.h"
#include "render_graph.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::graph {

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

    vk::Image pass_context::get_image(texture_handle handle) const { return graph->texture_image(handle); }


    vk::ImageView pass_context::get_view(texture_handle handle, u32 mip, u32 layer) const { return graph->texture_view(handle, mip, layer); }


    vk::Buffer pass_context::get_buffer(buffer_handle handle) const {

        auto* b = graph->buffer_allocation(handle);
        return b ? b->buffer : nullptr;
    }


    util::allocated_buffer* pass_context::get_allocated_buffer(buffer_handle handle) const { return graph->buffer_allocation(handle); }


    vk::Extent2D pass_context::get_extent(texture_handle handle) const { return graph->texture_extent(handle); }


    vk::Format pass_context::get_format(texture_handle handle) const { return graph->texture_format(handle); }


    void pass_context::begin_rendering() {

        std::vector<vk::RenderingAttachmentInfo> colors;
        colors.reserve(m_attachments->m_color_attachments.size());

        for (const auto& a : m_attachments->m_color_attachments) {
            vk::RenderingAttachmentInfo rendering_attachment_i{};
            rendering_attachment_i.imageView = get_view(a.handle, 0, 0);
            rendering_attachment_i.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            rendering_attachment_i.loadOp = a.load_op;
            rendering_attachment_i.storeOp = a.store_op;
            rendering_attachment_i.clearValue = a.clear_value;
            colors.push_back(rendering_attachment_i);
        }

        std::optional<vk::RenderingAttachmentInfo> depth;
        if (m_attachments->m_depth_attachment) {
            const auto& a = *m_attachments->m_depth_attachment;
            vk::RenderingAttachmentInfo rendering_attachment_i{};
            rendering_attachment_i.imageView = get_view(a.handle, 0, 0);
            rendering_attachment_i.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            rendering_attachment_i.loadOp = a.load_op;
            rendering_attachment_i.storeOp = a.store_op;
            rendering_attachment_i.clearValue = a.clear_value;
            depth = rendering_attachment_i;
        }

        vk::RenderingInfo rendering_i{};
        rendering_i.renderArea = vk::Rect2D(vk::Offset2D(0, 0), render_area);
        rendering_i.layerCount = 1;
        rendering_i.colorAttachmentCount = static_cast<u32>(colors.size());
        rendering_i.pColorAttachments = colors.empty() ? nullptr : colors.data();
        rendering_i.pDepthAttachment = depth ? &*depth : nullptr;

        cmd.beginRendering(rendering_i);
        cmd.setViewport(0, vk::Viewport(0.f, 0.f, static_cast<f32>(render_area.width), static_cast<f32>(render_area.height), 0.f, 1.f));
        cmd.setScissor(0, vk::Rect2D({0,0}, render_area));
    }


    void pass_context::end_rendering() { cmd.endRendering(); }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
