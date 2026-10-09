
#include "util/pch.h"
#include "pass_builder.h"


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

    pass_builder& pass_builder::read (texture_handle handle, texture_usage usage) { m_tex_reads.push_back({handle, usage});  return *this; }


    pass_builder& pass_builder::write(texture_handle handle, texture_usage usage) { m_tex_writes.push_back({handle, usage}); return *this; }


    pass_builder& pass_builder::read (buffer_handle handle, buffer_usage usage) { m_buf_reads.push_back({handle, usage});  return *this; }


    pass_builder& pass_builder::write(buffer_handle handle, buffer_usage usage) { m_buf_writes.push_back({handle, usage}); return *this; }


    pass_builder& pass_builder::color_attachment(texture_handle handle, vk::AttachmentLoadOp load, vk::AttachmentStoreOp store,
        vk::ClearColorValue clear) {

        attachment_info attachment{};
        attachment.handle = handle;
        attachment.load_op = load;
        attachment.store_op = store;
        attachment.clear_value.color = clear;
        m_color_attachments.push_back(attachment);
        m_tex_writes.push_back({handle, texture_usage::color_attachment});
        return *this;
    }


    pass_builder& pass_builder::depth_attachment(texture_handle handle, vk::AttachmentLoadOp load, vk::AttachmentStoreOp store,
        f32 clear_depth, u32 clear_stencil) {

        attachment_info attachment{};
        attachment.handle = handle;
        attachment.load_op = load;
        attachment.store_op = store;
        attachment.clear_value.depthStencil = vk::ClearDepthStencilValue(clear_depth, clear_stencil);
        m_depth_attachment = attachment;
        m_tex_writes.push_back({handle, texture_usage::depth_attachment});
        return *this;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
