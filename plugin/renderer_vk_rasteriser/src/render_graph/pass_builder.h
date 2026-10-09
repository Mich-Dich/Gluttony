
#pragma once

#include "types.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::graph {
    class render_graph;
}

namespace GLT::renderer::vk_rasterizer::graph {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class pass_builder {
    public:

        // --- generic read/write -------------------------------------------------
        pass_builder& read(texture_handle handle, texture_usage usage);

        pass_builder& write(texture_handle handle, texture_usage usage);

        pass_builder& read(buffer_handle handle, buffer_usage usage);

        pass_builder& write(buffer_handle handle, buffer_usage usage);

        // --- attachments (imply the corresponding usage) -----------------------
        pass_builder& color_attachment(texture_handle handle, vk::AttachmentLoadOp load = vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp store = vk::AttachmentStoreOp::eStore, vk::ClearColorValue   clear = {});

        pass_builder& depth_attachment(texture_handle handle, vk::AttachmentLoadOp load = vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp store = vk::AttachmentStoreOp::eStore, f32 clear_depth = 1.0f, u32 clear_stencil = 0);

        // --- flags -------------------------------------------------------------
        pass_builder& set_side_effects(bool effect) { m_side_effects = effect; return *this; }

        pass_builder& set_name(std::string name)  { m_name = std::move(name); return *this; }

        // --- access to the graph (so you can allocate resources inside setup) --

        render_graph*                                           graph = nullptr;

        // --- internals, read by render_graph -----------------------------------

        std::vector<std::pair<texture_handle, texture_usage>>   m_tex_reads;

        std::vector<std::pair<texture_handle, texture_usage>>   m_tex_writes;

        std::vector<std::pair<buffer_handle,  buffer_usage >>   m_buf_reads;

        std::vector<std::pair<buffer_handle,  buffer_usage >>   m_buf_writes;

        std::vector<attachment_info>                            m_color_attachments;

        std::optional<attachment_info>                          m_depth_attachment;

        bool                                                    m_side_effects = false;

        std::string                                             m_name;
    };

}
