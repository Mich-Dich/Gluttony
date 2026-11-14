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

        buffer_handle create_buffer(const buffer_desc&  desc);

        void import_texture(texture_handle handle, vk::Image image, vk::ImageView view, resource_state initial_state = {});

        void import_buffer(buffer_handle handle, const util::allocated_buffer& buf);

        // Reserve a slot for an externally-owned buffer and register it. Nothing is allocated by the graph
        buffer_handle  import_buffer (const buffer_desc& desc, const util::allocated_buffer& buf);

        // Same for textures
        texture_handle import_texture(const texture_desc& desc, vk::Image image, vk::ImageView view, resource_state initial_state = {});

        // -------- passes -----------------

        template<typename Setup, typename Execute>
        void add_pass(std::string name, Setup&& setup, Execute&& execute);

        // -------- execution --------------

        void compile();

        void execute(vk::CommandBuffer cmd);

        // -------- debug ------------------

        const std::vector<std::string>& pass_names() const { return m_pass_names; }

        std::string dump() const;

        // -------- deferred destruction ---

        // Graph-owned images/buffers that are dropped while frames are in flight (transient resources at begin_frame(),
        // persistent resources rebuilt on resize) are handed to this callback instead of being destroyed immediately.
        // Without a callback they are destroyed on the spot (only safe if the caller waited for the GPU)
        using retire_fn = std::function<void(std::function<void()>&&)>;
        void set_retire_callback(retire_fn fn) { m_retire = std::move(fn); }

        // -------- GPU timing -------------

        // When set, execute() resets [first_query, first_query + query_count) and writes one timestamp before the first
        // live pass and one after every live pass. Query i+1 - query i is the duration of timed_pass_names()[i].
        // Passes beyond the query budget are not timed. Pass a null pool to disable
        void set_timestamp_queries(vk::QueryPool pool, u32 first_query, u32 query_count);

        // Tracy Vulkan context (tracy::VkCtx*, opaque here). When set, execute() collects it and wraps every pass in a
        // GPU zone named after the pass
        void set_gpu_profiler(void* tracy_vk_ctx) noexcept { m_gpu_profiler = tracy_vk_ctx; }
        [[nodiscard]] const std::vector<std::string>& timed_pass_names() const { return m_timed_pass_names; }
        [[nodiscard]] u32 timestamps_written() const { return m_timestamps_written; }

        // -------- resource lookup (used by pass_context) --------

        vk::Image texture_image(texture_handle handle) const;

        vk::ImageView texture_view(texture_handle handle, u32 mip, u32 layer);

        // 2D-array view of one mip covering every layer (e.g. all 6 cube faces as image2DArray for compute writes)
        vk::ImageView texture_mip_array_view(texture_handle handle, u32 mip);

        // View that includes every mip level. Use this when a shader needs to sample the full mip chain via textureLod();
        // texture_view(h, 0, 0) only covers mip 0
        vk::ImageView texture_default_view(texture_handle handle) const;

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
            u32                                                 last_use = 0;
            bool                                                write_dirty = false;
        };


        struct buffer_resource {

            buffer_desc                                         desc;
            bool                                                imported = false;
            util::allocated_buffer                              allocated{};
            resource_state                                      state{};
            u32                                                 first_use = UINT32_MAX;
            u32                                                 last_use = 0;
            bool                                                write_dirty = false;
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

        resource_state target_buffer_state(const pass& pass, buffer_handle  handle) const;

        void emit_barriers(vk::CommandBuffer cmd, const pass& pass);

        void cull();

        void compute_lifetimes();

        void destroy_texture(texture_resource& texture_resource);

        void destroy_buffer(buffer_resource& buffer_resource);

        // Deferred variants: go through m_retire when set, otherwise identical to destroy_*
        void release_texture(texture_resource& texture_resource);

        void release_buffer(buffer_resource& buffer_resource);


        util::device*                                           m_device = nullptr;
        std::vector<texture_resource>                           m_textures;
        std::vector<buffer_resource>                            m_buffers;
        std::vector<pass>                                       m_passes;
        std::vector<std::string>                                m_pass_names;

        // Persistent resources are looked up by name so their handle is stable across frames
        std::unordered_map<std::string, texture_handle>         m_persistent_textures;
        std::unordered_map<std::string, buffer_handle >         m_persistent_buffers;

        retire_fn                                               m_retire{};

        vk::QueryPool                                           m_timestamp_pool = nullptr;
        void*                                                   m_gpu_profiler = nullptr;       // tracy::VkCtx*
        u32                                                     m_first_query = 0;
        u32                                                     m_query_count = 0;
        u32                                                     m_timestamps_written = 0;
        std::vector<std::string>                                m_timed_pass_names;
    };

}

#include "render_graph.inl"
