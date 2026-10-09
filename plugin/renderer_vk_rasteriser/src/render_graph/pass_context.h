
#pragma once

#include "types.h"
#include "pass_builder.h"
#include "util/buffer.h"



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

    class pass_context {
    public:

        FORCE_INLINE void set_attachments(pass_builder* b) { m_attachments = b; }

        // Dynamic-rendering helper: begins rendering with the attachments the setup lambda declared, sets viewport + scissor,
        // and (in end) ends it
        void begin_rendering();

        void end_rendering();

        // Resource access ---------------------------------------------------------

        vk::Image get_image (texture_handle handle) const;

        vk::ImageView get_view  (texture_handle handle, u32 mip = 0, u32 layer = 0) const;

        vk::Buffer get_buffer(buffer_handle  handle) const;

        util::allocated_buffer* get_allocated_buffer(buffer_handle handle) const;

        vk::Extent2D get_extent(texture_handle handle) const;

        vk::Format get_format(texture_handle handle) const;


        vk::CommandBuffer                                       cmd;

        render_graph*                                           graph = nullptr;

        vk::Extent2D                                            render_area = {};

    private:

        pass_builder*                                           m_attachments = nullptr;

    };

}
