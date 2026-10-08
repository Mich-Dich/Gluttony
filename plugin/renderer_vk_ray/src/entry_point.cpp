
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <vk_ray/vk_ray.h>
#include <vk_ray/builders/builders.h>

#include <render/i_renderer.h>
#include <platform/i_window.h>
#include <asset/i_asset_registry.h>
#include <asset/mesh.h>
#include <asset/material.h> 
#include <render/image.h>
#include <application.h>

#include "util/utils.h"
#include "util/data_structures.h"
#include "util/shader_compiler.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer_vk_ray {

    class image;
}


namespace GLT::renderer_vk_ray {

    // CONSTANTS =======================================================================================================

    constexpr u32                                               MAX_CONCURRENT_FRAMES = 3;

    static constexpr u32                                        TLAS_MAX_INSTANCES   = 1024;

    static constexpr u32                                        VERTEX_HEADROOM_MIN  = 64 * 1024;       // 64k verts

    static constexpr u32                                        INDEX_HEADROOM_MIN   = 128 * 1024;      // 128k indices

    static constexpr u32                                        MATERIAL_HEADROOM_MIN = 1024;

    static constexpr size_t                                     TEXTURE_SLOT_COUNT = static_cast<size_t>(GLT::asset::material::texture_slot::count);

    static constexpr u32                                        BINDLESS_TEXTURE_MAX = 1024;

    static constexpr u32                                        PREVIEW_IMAGE_SIZE = 420;

    static constexpr u32                                        PREVIEW_SPHERE_SEGMENTS = 32;

    static constexpr u32                                        PREVIEW_SPHERE_RINGS = 16;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    class renderer : public GLT::render::i_renderer_plugin {
    public:

        DEFAULT_GETTER(vr::device*,                             vr_dev);
        GETTER(vk::Device, vk_device,                           m_vr_dev->get_device())

        void on_load();


        void on_unload();


        bool create() override;


        void destroy() override;

        // --- frame control -------------------------------------------------------------------------------------------

        void begin_frame() override;


        void draw_frame() override;


        FORCE_INLINE void wait_for_gpu() override { m_device.waitIdle(); }

        // --- swapchain & configuration -------------------------------------------------------------------------------

        FORCE_INLINE_R glm::ivec2 get_swapchain_size() const override;
        
        
        void resize(const u32 width, const u32 height) override;

        IGNORE_UNUSED_PARAMETER_START
        IGNORE_UNUSED_VARIABLE_START

        void set_vsync(const bool enabled) override { }

        IGNORE_UNUSED_VARIABLE_STOP
        IGNORE_UNUSED_PARAMETER_STOP

        FORCE_INLINE_R bool get_vsync() const override { return false; }


        FORCE_INLINE void set_clear_color(const glm::vec4& color) override { m_clear_color = color; }


        void set_render_size(const glm::ivec2& size) override;

        // --- feature queries -----------------------------------------------------------------------------------------

        FORCE_INLINE_R GLT::render::renderer_feature get_supported_features() const override { return m_features; }


        FORCE_INLINE_R GLT::render::backend_api get_backend_api() const override { return GLT::render::backend_api::vulkan; }

        // --- native access -------------------------------------------------------------------------------------------

        [[nodiscard]] void* get_rendered_image() override;


        [[nodiscard]] glm::uvec2 get_rendered_image_size() override;


        FORCE_INLINE_R void* get_native_device_handle() const override { return {m_physical_device}; }


        FORCE_INLINE_R void* get_native_context_handle() const override { return {}; }


        [[nodiscard]] debug::render_stats get_render_stats() const;


        void set_active_camera(const GLT::world::camera_snapshot& camera) override;


        [[nodiscard]] void* render_material_preview(GLT::asset::handle material, const GLT::asset::material::material_params& params,
            std::span<const GLT::asset::handle> textures, const glm::vec3& camera_pos) override;


        void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

        // uploaded mesh data ------------------------------------------------------------------------------------------
        
        // load a mesh asset by pyth (must be a content relative path!)
        // the renderer remembers the uploaded meshes and skips if mesh already loaded
        bool load_mesh(const std::filesystem::path& content_relative_path) override;
        
        
        // the renderer remembers the uploaded meshes and skips if mesh already loaded
        bool load_mesh(const GLT::asset::handle handle) override;


        // the renderer remembers the uploaded meshes and skips if mesh not loaded
        void unload_mesh(GLT::asset::handle handle) override;

        // scene management --------------------------------------------------------------------------------------------
    
        void submit_scene(std::span<const GLT::asset::mesh::instance> instances) override;


        void retain_mesh(GLT::asset::handle mesh) override;


        void release_mesh(GLT::asset::handle mesh) override;

    private:

        enum class image_type{
            swapchain = 0,
            render,
            accum,
        };


        struct mesh_slot {

            GLT::asset::handle                                  asset{};
            u32                                                 vertex_offset = 0;      // in vertices
            u32                                                 vertex_count = 0;
            u32                                                 index_offset = 0;      // in indices
            u32                                                 index_count = 0;
            u32                                                 material_offset = 0;      // in gpu_material entries
            u32                                                 material_count = 0;
            vr::blas_handle                                     blas{};
            bool                                                alive = false;
        };


        struct material_slot {

            GLT::asset::handle                                  asset{};
            u32                                                 gpu_index{};          // into m_material_buffer
            std::array<GLT::asset::handle, TEXTURE_SLOT_COUNT>  textures{};           // for ref-count release
            bool                                                alive{ false };
        };


        struct texture_slot {

            GLT::asset::handle                                  asset{};
            vk::Image                                           image{};
            VmaAllocation                                       allocation{};         // or whatever your image wrapper holds
            vk::ImageView                                       view{};
            vk::Sampler                                         sampler{};
            u32                                                 ref_count{ 0 };
            u32                                                 bindless_index{ UINT32_MAX };
            bool                                                alive{ false };
        };

        void init_vulkan();


        void create_acceleration_structures();


        void create_rt_pipeline();


        void update_descriptor_set();


        void create_base_resources();


        void transition_image_layout(vk::CommandBuffer command_buffer, const image_type type, const vk::ImageLayout new_layout);


        void create_swapchain(const glm::ivec2 size);


        void destroy_swapchain();


        void resize_swapchain(const glm::ivec2 size);


        // Clear the output image to a background colour (e.g., dark blue)
        void clear_output_image(vk::CommandBuffer cmd, const glm::vec4& color);

        
        void clear_gbuffer(vk::CommandBuffer cmd, GLT::ref<image> &gbuffer);

        // --- IMGUI ---------------------------------------------------------------------------------------------------

        void imgui_init();


        void imgui_shutdown();


        void create_imgui_resources();


        void destroy_imgui_resources();


        void begin_imgui_frame(vk::CommandBuffer& current_cmd);


        void end_imgui_frame(vk::CommandBuffer& current_cmd);
        
        // mesh handling -----------------------------------------------------------------------------------------------

        bool reserve_mesh_space(u32 v, u32 i, u32 m);


        // void rebuild_tlas_if_dirty();


        void build_blas_for_slot(mesh_slot& slot);


        void rebuild_all_blases();


        void upload_mesh_slice(mesh_slot& slot, GLT::asset::mesh::mesh_asset& mesh);


        FORCE_INLINE_R mesh_slot* find_slot(GLT::asset::handle h) noexcept;


        FORCE_INLINE_R const mesh_slot* find_slot(GLT::asset::handle h) const noexcept;


        // Deferred load / unload - runs at a frame boundary, never mid-record
        void process_pending_meshes();


        // TLAS rebuilt from submitted instances
        void rebuild_tlas_from_scene();


        // material slots
        bool reserve_material_space(u32 count);
        material_slot* find_material_slot(GLT::asset::handle h) noexcept;
        const material_slot* find_material_slot(GLT::asset::handle h) const noexcept;
        bool load_material(GLT::asset::handle handle);
        void unload_material(GLT::asset::handle handle);

        // texture slots (bindless)
        void create_default_texture();
        bool reserve_geometry_space(u32 count);
        u32 load_texture(GLT::asset::handle handle);       // returns bindless index, ref-counted
        void unload_texture(GLT::asset::handle handle);
        u32 find_texture_index(GLT::asset::handle handle) const noexcept;

        // preview -----------------------------------------------------------------------------------------------------

        void  create_material_preview_resources();

        void  upload_preview_data();

        void  dispatch_material_preview(vk::CommandBuffer cmd);



        glm::ivec2                                              m_render_size{ 300, 400};

        GLT::render::renderer_feature                           m_features{};

        GLT::system_state                                       m_state = GLT::system_state::destroyed;
        vr::instance_wrapper                                    m_instance;
        vr::command_queues                                      m_queues{};
        vk::Device                                              m_device = nullptr;
        vk::SurfaceKHR                                          m_surface = nullptr;
        vk::PhysicalDevice                                      m_physical_device = nullptr;
        utils::deletion_queue                                   m_deletion_queue{};
        vr::swapchain_builder                                   m_swapchain_builder;
        vr::swapchain_resources                                 m_swapchain;
        vk::SwapchainKHR                                        m_old_swapchain = nullptr;
        
        vk::CommandPool                                         m_graphics_pool;
        u32                                                     m_image_count = 0;
        u32                                                     m_current_frame = 0;
        u32                                                     m_current_swapchain_image = 0;
        std::vector<vk::Semaphore>                              m_render_semaphores{};
        std::vector<vk::Semaphore>                              m_present_semaphores{};
        std::vector<vk::Fence>                                  m_in_flight_fences{};
        std::vector<vk::ImageLayout>                            m_swapchain_images_layout{};
        std::array<vk::CommandBuffer, MAX_CONCURRENT_FRAMES>    m_rt_render_cmd;
        vr::device*                                             m_vr_dev = nullptr;
        GLT::ref<image>                                         m_output_image = nullptr;
        vr::allocated_buffer                                    m_uniform_buffer = {};
        std::vector<vr::descriptor_item>                        m_resource_bindings;
        vk::DescriptorSetLayout                                 m_resource_descriptor_layout;
        vr::descriptor_buffer                                   m_resource_desc_buffer;
        vk::PipelineLayout                                      m_pipeline_layout = nullptr;
        utils::shader_compiler                                  m_shader_compiler{};
        vk::Pipeline                                            m_rt_pipeline = nullptr;
        vr::sbt_buffer                                          m_sbt_buffer; // contains the shader records for the SBT

        // ImGui resources
        vk::DescriptorPool                                      m_imgui_descriptor_pool = nullptr;
        vk::RenderPass                                          m_imgui_render_pass = nullptr;
        std::vector<vk::Framebuffer>                            m_imgui_framebuffers;
        bool                                                    m_imgui_initialized = false;
        ImGuiContext*                                           m_imgui_context = nullptr;
        glm::ivec2                                              m_target_framebuffer_size{};

        GLT::platform::backend_api                              mp_window_backend_api{};
        ref<GLT::platform::i_window_plugin>                     mp_window{};

        handle                                                  m_framebuffer_resize_sub{};
        glm::vec4                                               m_clear_color{0.09f, 0.09f, 0.09f, 1.f};
        // ref<GLT::world::camera>                                 m_active_camera{};

		vk::Fence										        m_immediate_submit_fence{};
		vk::CommandBuffer								        m_immediate_submit_command_buffer{};
		vk::CommandPool								            m_immediate_submit_command_pool{};
    
        // render stats (populated during the frame) -------------------------------------------------------------------
        u32                                                     m_frame_draw_calls = 0;
        u32                                                     m_frame_render_passes = 0;

        // --- GPU timing (timestamp queries) --------------------------------------------------------------------------
        vk::QueryPool                                           m_timestamp_pool = nullptr;
        f32                                                     m_timestamp_period_ns = 1.0f;
        std::array<f32, MAX_CONCURRENT_FRAMES>                  m_gpu_frame_time_ms{};
        f32                                                     m_last_gpu_time_ms = 0.0f;

        // --- live resource counters (absolute) -----------------------------------------------------------------------
        u32                                                     m_live_buffer_count = 0;
        u32                                                     m_live_pipeline_count = 0;
        u32                                                     m_live_descriptor_set_count = 0;

        // scene geometry (loaded from the asset registry) -------------------------------------------------------------

        vr::allocated_buffer                                    m_vertex_buffer{};
        vr::allocated_buffer                                    m_index_buffer{};
        // vr::allocated_buffer                                    m_material_buffer{};
        u32                                                     m_vertex_capacity = 0;      // in vertices
        u32                                                     m_index_capacity = 0;       // in indices
        u32                                                     m_material_capacity = 0;    // in gpu_material entries
        u32                                                     m_vertex_used = 0;
        u32                                                     m_index_used = 0;
        u32                                                     m_material_used = 0;

        std::vector<mesh_slot>                                  m_mesh_slots{};
        std::vector<u32>                                        m_free_slots{};             // indices into m_mesh_slots

        vr::tlas_handle                                         m_tlas_handle{};
        vr::tlas_build_info                                     m_tlas_build_info{};        // kept, not local
        vr::allocated_buffer                                    m_tlas_instance_buffer{};   // sized for TLAS_MAX_INSTANCES
        std::vector<vk::AccelerationStructureInstanceKHR>       m_tlas_instances;           // scratch, reused
        // bool                                                    m_tlas_dirty = false;

        // --- scene (populated by submit_scene from the world layer) --------------------------------------------------
        std::vector<GLT::asset::mesh::instance>                 m_scene_instances{};
        std::unordered_set<GLT::asset::handle>                  m_scene_meshes{};
        std::unordered_set<GLT::asset::handle>                  m_retained_meshes{};
        std::vector<GLT::asset::handle>                         m_pending_loads{};
        std::vector<GLT::asset::handle>                         m_pending_unloads{};

        vr::allocated_buffer                                    m_material_buffer{};        // gpu_material[]
        vr::allocated_buffer                                    m_geometry_buffer{};        // gpu_geometry[]
        vr::allocated_buffer                                    m_texture_buffer{};         // not needed if using descriptor indexing

        GLT::world::camera_snapshot                             m_active_camera{};


        // ---- materials -------------------------------------------------------------
        std::vector<material_slot>                              m_material_slots{};
        std::vector<u32>                                        m_free_material_slots{};

        // ---- textures (bindless) ---------------------------------------------------
        std::vector<texture_slot>                               m_texture_slots{};         // indexed by bindless_index
        std::vector<u32>                                        m_free_texture_slots{};

        // Descriptor-side mirror of m_texture_slots
        // Every element is initialised to the white fallback in create_default_texture() and overwritten per-element as textures load
        // Fixed size, so .data() is stable across the renderer's life
        std::array<vr::accessible_image, BINDLESS_TEXTURE_MAX>  m_texture_descriptors{};

        // Preview's own mirror. Same layout, independent backing array
        // Exists so that per-element descriptor writes targeting one descriptor buffer can never alias the other's bindless array
        // Slot 0 is the shared checkerboard fallback and is never recycled (the free list starts at index 1)
        std::array<vr::accessible_image, BINDLESS_TEXTURE_MAX>  m_preview_texture_descriptors{};

        // Shared samplers, created once in create_default_texture().
        vk::Sampler                                             m_default_sampler_linear{};
        vk::Sampler                                             m_default_sampler_nearest{};

        // ---- geometry (per-instance, per-submesh) ----------------------------------
        u32                                                     m_geometry_used = 0;
        u32                                                     m_geometry_capacity = 0;

        // ---- scene materials (mirrors the mesh bookkeeping) ------------------------
        std::unordered_set<GLT::asset::handle>                  m_scene_materials{};
        std::unordered_set<GLT::asset::handle>                  m_retained_materials{};
        std::vector<GLT::asset::handle>                         m_pending_material_loads{};
        std::vector<GLT::asset::handle>                         m_pending_material_unloads{};

        // improve render quality --------------------------------------------------------------------------------------
        std::array<GLT::ref<image>, 2>                          m_accum_image{};
        std::array<GLT::ref<image>, 2>                          m_gbuffer_pos{};
        std::array<GLT::ref<image>, 2>                          m_gbuffer_nrm{};
        u32                                                     m_gbuffer_index = 0;
        glm::mat4                                               m_prev_view_proj{1.0f};
        bool                                                    m_temporal_valid = false;   // false -> shader uses current only
        u32                                                     m_frame_counter = 0;

        // preview -----------------------------------------------------------------------------------------------------

        bool                                                    m_preview_ready = false;
        GLT::ref<image>                                         m_preview_image{};
        GLT::ref<image>                                         m_preview_accum_image{};        // separate accum for the preview pass
        GLT::ref<image>                                         m_preview_dummy_gbuffer{};
        vr::allocated_buffer                                    m_preview_vertex_buffer{};
        vr::allocated_buffer                                    m_preview_index_buffer{};
        vr::allocated_buffer                                    m_preview_instance_buffer{};
        vr::allocated_buffer                                    m_preview_material_buffer{};
        vr::allocated_buffer                                    m_preview_geometry_buffer{};
        vr::allocated_buffer                                    m_preview_camera_ubo{};
        vr::blas_handle                                         m_preview_sphere_blas{};
        vr::tlas_handle                                         m_preview_tlas{};
        vr::tlas_build_info                                     m_preview_tlas_build_info{};
        std::vector<vr::descriptor_item>                        m_preview_bindings{};
        vr::descriptor_buffer                                   m_preview_desc_buffer{};
        bool                                                    m_preview_queued = false;
        GLT::asset::handle                                      m_preview_material = INVALID_HANDLE;
        GLT::asset::material::material_params                   m_preview_params{};
        std::array<GLT::asset::handle, TEXTURE_SLOT_COUNT>      m_preview_textures{};
        glm::vec3                                               m_preview_camera_pos{ 0.0f, 0.0f, 3.0f };

    };


    class image : public GLT::render::image {
    public:

        image();

        image(const glm::uvec3 size);

        image(const std::filesystem::path& image_path, const bool mipmapped = false);

        image(const void* data, const u32 width, const u32 height, const bool mipmapped = false);

        ~image();


        SETTER(VmaAllocation, allocation,                       m_allocated_image.allocation);

        DEFAULT_GETTER_REF(vr::allocated_image,                 allocated_image);
        DEFAULT_SETTER(vr::allocated_image,                     allocated_image);
        DEFAULT_GETTER_REF(vr::accessible_image,                accessible_image);
        DEFAULT_SETTER(vr::accessible_image,                    accessible_image);

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


        vr::allocated_image                                     m_allocated_image{};
        vr::accessible_image                                    m_accessible_image{};
        GLT::ref<GLT::renderer_vk_ray::renderer>                m_renderer{};
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
#include "renderer.inl"
#include "renderer_util.inl"
#include "renderer_swapchain.inl"
#include "renderer_imgui.inl"
#include "renderer_material_texture.inl"
#include "renderer_mesh.inl"
#include "renderer_scene.inl"
#include "renderer_preview.inl"

EXPORT_PLUGIN_CLASS(GLT::renderer_vk_ray::renderer, GLT::renderer_vk_ray::descriptor)
