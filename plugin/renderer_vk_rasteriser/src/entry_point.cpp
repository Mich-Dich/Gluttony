
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <plugin_system/plugin_manager.h>
#include <render/i_renderer.h>
#include <platform/i_window.h>
#include <asset/i_asset_registry.h>
#include <asset/mesh.h>
#include <asset/material.h> 
#include <render/image.h>
#include <application.h>

#include "util/buffer.h"
#include "util/descriptors.h"
#include "util/device.h"
#include "util/shader.h"
#include "util/data_structures.h"
#include "util/shader_compiler.h"
#include "util/builders.h"
#include "render_graph/render_graph.h"
#include "settings.inl"
#include "types.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    class image;
}


namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    class renderer : public GLT::render::i_renderer_plugin {
    public:

        DEFAULT_GETTER(util::device*, dev);
        GETTER(vk::Device, vk_device, m_device)

        void on_load();
        void on_unload();

        bool create() override;
        void destroy() override;

        void begin_frame() override;
        void draw_frame() override;
        FORCE_INLINE void wait_for_gpu() override { m_device.waitIdle(); }

        FORCE_INLINE_R glm::ivec2 get_swapchain_size() const override;

        void set_vsync(const bool /*enabled*/) override { }
        FORCE_INLINE_R bool get_vsync() const override { return false; }

        FORCE_INLINE void set_clear_color(const glm::vec4& color) override { m_clear_color = color; }
        void set_render_size(const glm::ivec2& size) override;

        FORCE_INLINE_R GLT::render::renderer_feature get_supported_features() const override { return m_features; }
        FORCE_INLINE_R GLT::render::backend_api get_backend_api() const override { return GLT::render::backend_api::vulkan; }

        [[nodiscard]] void* get_rendered_image() override;
        [[nodiscard]] glm::uvec2 get_rendered_image_size() override;

        FORCE_INLINE_R void* get_native_device_handle() const override { return {m_physical_device}; }
        FORCE_INLINE_R void* get_native_context_handle() const override { return {}; }

        [[nodiscard]] debug::render_stats get_render_stats() const;
        void set_active_camera(const GLT::world::camera_snapshot& camera) override;
        void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

        // mesh --------------------------------------------------------------------------------------------------------

        bool load_mesh(const std::filesystem::path& content_relative_path) override;


        bool load_mesh(GLT::asset::handle handle) override;


        void unload_mesh(GLT::asset::handle handle) override;


        void submit_scene(std::span<const GLT::asset::mesh::instance> instances) override;


        void retain_mesh(GLT::asset::handle mesh) override;


        void release_mesh(GLT::asset::handle mesh) override;

        // modes -------------------------------------------------------------------------------------------------------

        [[nodiscard]] std::span<const GLT::render::render_mode_info> supported_modes() const override;


        void set_render_mode(u32 mode_id) override;


        [[nodiscard]] u32 get_render_mode() const override;

        // settings ----------------------------------------------------------------------------------------------------

        [[nodiscard]] const GLT::reflect::type_descriptor* settings_descriptor() const override;


        [[nodiscard]] void* settings_data() override;


        void on_settings_changed() override;


        FORCE_INLINE void set_sun_settings(const GLT::render::sun_settings& s) override { (void)s; }


        FORCE_INLINE_R const GLT::render::sun_settings& get_sun_settings() const override { return m_sun_settings; }


        [[nodiscard]] void* render_material_preview(GLT::asset::handle /*material*/, const GLT::asset::material::material_params& /*params*/,
            std::span<const GLT::asset::handle> /*textures*/, const glm::vec3& /*camera_pos*/) override { return nullptr; }

    private:

        struct mesh_slot {
        
            GLT::asset::handle                                      asset{};
            u32                                                     vertex_offset = 0;      // in vertices
            u32                                                     vertex_count = 0;
            u32                                                     index_offset = 0;       // in indices
            u32                                                     index_count = 0;
            bool                                                    alive = false;
        };


        void init_vulkan();
        void create_swapchain(const glm::ivec2 size);
        void destroy_swapchain();
        void resize_swapchain(const glm::ivec2 size);

        // the render output image (this is what get_rendered_image() returns)
        void create_render_target();
        void destroy_render_target();

        // imgui
        void imgui_init();
        void imgui_shutdown();
        void create_imgui_resources();
        void destroy_imgui_resources();
        void begin_imgui_frame();
        void end_imgui_frame();


        bool reserve_mesh_space(u32 vertex_count, u32 index_count);
        void upload_mesh_slice(mesh_slot& slot, const GLT::asset::mesh::mesh_asset& mesh);
        mesh_slot* find_slot(GLT::asset::handle h) noexcept;
        const mesh_slot* find_slot(GLT::asset::handle h) const noexcept;

        void process_pending_meshes();

        void create_mesh_resources();                           // buffers, UBOs, descriptor buffer, set layout, pipeline
        void destroy_mesh_resources();
        void update_camera_ubo();                               // called once per frame in begin_frame()
        void build_instance_buffer();                           // packs scene instances into the per-frame SSBO
        void draw_scene_meshes(vk::CommandBuffer cmd);

        // helpers
        void transition_image_layout(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout oldL,
            vk::ImageLayout newL, vk::ImageSubresourceRange range = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});

        glm::ivec2                                                  m_render_size{ 800, 600 };
        GLT::render::renderer_feature                               m_features{};
        u32                                                         m_active_mode{ 0 };

        util::instance_wrapper                                      m_instance;
        util::command_queues                                        m_queues{};
        vk::Device                                                  m_device = nullptr;
        vk::SurfaceKHR                                              m_surface = nullptr;
        vk::PhysicalDevice                                          m_physical_device = nullptr;
        util::deletion_queue                                        m_deletion_queue{};
        util::swapchain_builder                                     m_swapchain_builder;
        util::swapchain_resources                                   m_swapchain;
        util::device*                                               m_dev = nullptr;

        // sync
        u32                                                         m_image_count = 0;
        u32                                                         m_current_frame = 0;
        u32                                                         m_current_swapchain_image = 0;
        std::vector<vk::Semaphore>                                  m_render_semaphores{};
        std::vector<vk::Semaphore>                                  m_present_semaphores{};
        std::vector<vk::Fence>                                      m_in_flight_fences{};
        std::vector<vk::ImageLayout>                                m_swapchain_images_layout{};

        // command
        vk::CommandPool                                             m_graphics_pool;
        std::array<vk::CommandBuffer, MAX_CONCURRENT_FRAMES>        m_render_cmd;

        // immediate submit
        vk::Fence                                                   m_immediate_submit_fence{};
        vk::CommandBuffer                                           m_immediate_submit_command_buffer{};
        vk::CommandPool                                             m_immediate_submit_command_pool{};

        // render target + pipeline
        GLT::ref<image>                                             m_output_image = nullptr;
        vk::RenderPass                                              m_triangle_render_pass = nullptr;
        std::vector<vk::Framebuffer>                                m_triangle_framebuffers;
        util::shader_compiler                                       m_shader_compiler{};

        // imgui
        vk::DescriptorPool                                          m_imgui_descriptor_pool = nullptr;
        vk::RenderPass                                              m_imgui_render_pass = nullptr;
        std::vector<vk::Framebuffer>                                m_imgui_framebuffers;
        bool                                                        m_imgui_initialized = false;
        ImGuiContext*                                               m_imgui_context = nullptr;

        // platform
        GLT::platform::backend_api                                  mp_window_backend_api{};
        ref<GLT::platform::i_window_plugin>                         mp_window{};
        handle                                                      m_framebuffer_resize_sub{};

        glm::ivec2                                                  m_target_framebuffer_size{};
        glm::vec4                                                   m_clear_color{ 0.09f, 0.09f, 0.09f, 1.f };

        // stats
        u32                                                         m_frame_draw_calls = 0;
        u32                                                         m_frame_render_passes = 0;
        u32                                                         m_live_buffer_count = 0;
        u32                                                         m_live_pipeline_count = 0;
        u32                                                         m_live_descriptor_set_count = 0;

        GLT::world::camera_snapshot                                 m_active_camera{};

        // settings (kept for interface compatibility)
        GLT::render::sun_settings                                   m_sun_settings{};
        GLT::renderer::vk_rasterizer::settings::visual              m_visual_settings{};

        // render graph ------------------------------------------------------------------------------------------------

        GLT::unique_ref<graph::render_graph>                        m_graph;
        graph::texture_handle                                       m_backbuffer_handle;
        graph::texture_handle                                       m_triangle_target;   // persistent - persists across frames
        graph::texture_handle                                       m_depth_target;

        // mesh state --------------------------------------------------------------------------------------------------

        util::allocated_buffer                                      m_vertex_buffer{};
        util::allocated_buffer                                      m_index_buffer{};
        u32                                                         m_vertex_capacity = 0;
        u32                                                         m_index_capacity = 0;
        u32                                                         m_vertex_used = 0;
        u32                                                         m_index_used = 0;

        std::vector<mesh_slot>                                      m_mesh_slots{};
        std::vector<u32>                                            m_free_slots{};

        // scene state (populated by submit_scene) ---------------------------------------------------------------------

        std::vector<GLT::asset::mesh::instance>                     m_scene_instances{};
        std::unordered_set<GLT::asset::handle>                      m_scene_meshes{};
        std::unordered_set<GLT::asset::handle>                      m_retained_meshes{};
        std::vector<GLT::asset::handle>                             m_pending_loads{};
        std::vector<GLT::asset::handle>                             m_pending_unloads{};

        // mesh pipeline + per-frame descriptor data -------------------------------------------------------------------

        vk::DescriptorSetLayout                                     m_mesh_set_layout = nullptr;
        vk::PipelineLayout                                          m_mesh_pipeline_layout = nullptr;
        vk::Pipeline                                                m_mesh_pipeline = nullptr;
        vk::Pipeline                                                m_depth_pipeline = nullptr;
        std::array<util::allocated_buffer, MAX_CONCURRENT_FRAMES>   m_camera_ubos{};
        std::array<util::allocated_buffer, MAX_CONCURRENT_FRAMES>   m_instance_buffers{};
        std::array<util::descriptor_buffer, MAX_CONCURRENT_FRAMES>  m_frame_desc_buffers{};
        std::vector<instance_batch>                                 m_instance_batches{};

    };

    class image : public GLT::render::image {
    public:

        image();

        image(const glm::uvec3 size);

        image(const std::filesystem::path& image_path, const bool mipmapped = false);

        image(const void* data, const u32 width, const u32 height, const bool mipmapped = false);

        ~image();


        SETTER(VmaAllocation, allocation,                       m_allocated_image.allocation);

        DEFAULT_GETTER_REF(util::allocated_image,               allocated_image);
        DEFAULT_SETTER(util::allocated_image,                   allocated_image);
        DEFAULT_GETTER_REF(util::accessible_image,              accessible_image);
        DEFAULT_SETTER(util::accessible_image,                  accessible_image);

        FORCE_INLINE_R glm::uvec2 get_size() override;


        FORCE_INLINE_R void* get_descriptor_set() override;


        [[nodiscard]] void* load(const std::filesystem::path& path, u32& out_width, u32& out_height) override;


        void resize(const glm::uvec3& new_size, const GLT::render::image_format format = GLT::render::image_format::RGBA,
            const bool mipmapped = false) override;


        void reupload(const void* data) override;


        void update_region(const void* data, const u32 x, const u32 y, const u32 width, const u32 height, const u32 mip_level = 0) override;

    private:

	    void allocate_memory(const void* data, const glm::uvec3 size,
            const GLT::render::image_format format = GLT::render::image_format::RGBA, const bool mipmapped = true);


        void assign_data(const void* data, const glm::uvec3 size, const GLT::render::image_format format, const u32 mip_levels);


        void release();


        util::allocated_image                                   m_allocated_image{};
        util::accessible_image                                  m_accessible_image{};
        GLT::ref<GLT::renderer::vk_rasterizer::renderer>        m_renderer{};
        GLT::render::image_format                               m_format = GLT::render::image_format::RGBA;
        u32                                                     m_mip_levels = 1;

        #if defined(DEBUG)

            u64                                                 m_vram_bytes = 0;      // tracked size, used to decrement on release
        
        #endif
    };

    // STATIC VARIABLES ================================================================================================

    static constexpr const char*                                dependencies_names[] = {

        nullptr
    };

    static constexpr GLT::plugin_manager::interface             dependencies_interfaces[] = {

        GLT::plugin_manager::interface::window,
    };

    static constexpr GLT::plugin_manager::plugin_descriptor     descriptor = {

        .name                                                   = GLT_MODULE_NAME,
        .load_phase                                             = GLT::plugin_manager::phase::pre_application,
        .unload_phase                                           = GLT::plugin_manager::phase::post_application_shutdown,
        .target                                                 = GLT::plugin_manager::interface::renderer,
        .dependency_names_count                                 = ARRAY_SIZE(dependencies_names),
        .dependency_names                                       = dependencies_names,
        .dependency_interface_count                             = ARRAY_SIZE(dependencies_interfaces),
        .dependency_interfaces                                  = dependencies_interfaces,
    };

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

#include "image.inl"
#include "plugin.inl"
#include "renderer_imgui.inl"
#include "renderer.inl"
#include "renderer_util.inl"
#include "renderer_swapchain.inl"
#include "renderer_mesh.inl"
#include "renderer_scene.inl"
// #include "renderer_material_texture.inl"
// #include "renderer_preview.inl"

EXPORT_PLUGIN_CLASS(GLT::renderer::vk_rasterizer::renderer, GLT::renderer::vk_rasterizer::descriptor)
