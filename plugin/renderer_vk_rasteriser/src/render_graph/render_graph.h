#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include "util/buffer.h"
#include "util/device.h"
#include "render_graph/types.h"
#include "render_graph/pass_builder.h"
#include "render_graph/pass_context.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::graph {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class render_graph {
    public:

        explicit render_graph(util::device* device);
        ~render_graph();

        render_graph(const render_graph&) = delete;
        render_graph& operator=(const render_graph&) = delete;

        // -------- frame lifecycle --------

        void begin_frame();

        void end_frame();

        // -------- resources --------------

        texture_handle create_texture(const texture_desc& desc);

        buffer_handle  create_buffer (const buffer_desc&  desc);

        void import_texture(texture_handle handle, vk::Image image, vk::ImageView view, resource_state initial_state = {});

        void import_buffer (buffer_handle  handle, const util::allocated_buffer& buf);

        // -------- passes -----------------

        template<typename Setup, typename Execute>
        void add_pass(std::string name, Setup&& setup, Execute&& execute);

        // -------- execution --------------

        void compile();

        void execute(vk::CommandBuffer cmd);

        // -------- debug ------------------

        const std::vector<std::string>& pass_names() const { return m_pass_names; }

        std::string dump() const;

        // -------- resource lookup (used by pass_context) --------

        vk::Image texture_image(texture_handle handle) const;

        vk::ImageView texture_view(texture_handle handle, u32 mip, u32 layer);

        vk::Extent2D texture_extent(texture_handle handle) const;

        vk::Format texture_format(texture_handle handle) const;

        util::allocated_buffer* buffer_allocation(buffer_handle handle) const;

        vk::ImageLayout texture_layout(texture_handle handle) const;

    private:

        struct texture_resource {

            texture_desc                                        desc;
            bool                                                imported = false;

            util::allocated_image                               allocated{};         // owned if !imported
            vk::Image                                           image = nullptr;       // always valid
            vk::ImageView                                       default_view = nullptr;

            // cache of per-(mip,layer) views
            std::unordered_map<u64, vk::ImageView>              view_cache;

            resource_state                                      state{};               // current GPU state
            u32                                                 first_use = UINT32_MAX;
            u32                                                 last_use  = 0;
        };


        struct buffer_resource {

            buffer_desc                                         desc;
            bool                                                imported = false;
            util::allocated_buffer                              allocated{};
            resource_state                                      state{};
            u32                                                 first_use = UINT32_MAX;
            u32                                                 last_use  = 0;
        };


        struct pass {

            std::string                                         name;
            pass_builder                                        builder;
            std::function<void(pass_context&)>                  execute;
            bool                                                culled = false;
            u32                                                 index = 0;
        };
        

        // Helper: compute the target state for a pass's usage of a resource
        resource_state target_texture_state(const pass& pass, texture_handle handle) const;

        resource_state target_buffer_state (const pass& pass, buffer_handle  handle) const;

        void emit_barriers(vk::CommandBuffer cmd, const pass& pass);

        void cull();

        void compute_lifetimes();

        void destroy_texture(texture_resource& texture_resource);

        void destroy_buffer (buffer_resource& buffer_resource);
        

        util::device*                                           m_device = nullptr;
        std::vector<texture_resource>                           m_textures;
        std::vector<buffer_resource>                            m_buffers;
        std::vector<pass>                                       m_passes;
        std::vector<std::string>                                m_pass_names;

        // Persistent resources are looked up by name so their handle is stable across frames
        std::unordered_map<std::string, texture_handle>         m_persistent_textures;
        std::unordered_map<std::string, buffer_handle >         m_persistent_buffers;
    };

}

#include "render_graph.inl"
