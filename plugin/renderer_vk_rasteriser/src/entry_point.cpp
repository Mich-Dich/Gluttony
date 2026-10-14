
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <plugin_system/plugin_manager.h>
#include <render/i_renderer.h>
#include <platform/i_window.h>
#include <asset/i_asset_registry.h>
#include <asset/mesh.h>
#include <asset/material.h> 
#include <asset/texture.h>
#include <render/image.h>
#include <application.h>

#include "util/buffer.h"
#include "util/descriptors.h"
#include "util/device.h"
#include "util/shader.h"
#include "util/data_structures.h"
#include "util/shader_compiler.h"
#include "util/builders.h"
#include "util/shader_compiler.h"
#include "util/slang_compiler.h"
#include "render_graph/render_graph.h"
#include "settings.inl"
#include "types.h"




// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {
    class image;
}

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // Renderer codegen revision. Bump when the renderer's material pipeline changes in a way that invalidates bytecode
    // (descriptor layout, entry-point ABI, ...)
    static constexpr u32 VK_MATERIAL_CODEGEN_REVISION = 1;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    class renderer : public GLT::render::i_renderer_plugin, public GLT::render::i_shader_compiler {
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

        // light -------------------------------------------------------------------------------------------------------

        // Called by the world each frame with the current set of lights
        void submit_lights(std::span<const GLT::world::light_snapshot> lights) override;

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

        void set_world_render_enabled(const bool enabled) override { m_world_render_enabled = enabled; }
        [[nodiscard]] bool is_world_render_enabled() const override { return m_world_render_enabled; }

        // preview -----------------------------------------------------------------------------------------------------

        // DISABLED DURING MATERIAL REFACTOR
        // TODO: reenable after refactor is done

        // [[nodiscard]] void* render_material_preview(GLT::asset::handle /*material*/, const GLT::asset::material::material_params& /*params*/,
        //     std::span<const GLT::asset::handle> /*textures*/, const glm::vec3& /*camera_pos*/) override { return nullptr; }

    public:

        // i_shader_compiler -------------------------------------------------------------------------------------------

        [[nodiscard]] std::string_view name() const override;
        [[nodiscard]] u64 implementation_hash() const override;
        [[nodiscard]] u32 contract_version() const override;
        [[nodiscard]] std::expected<GLT::render::compiled_shader, GLT::render::shader_compile_error> compile(
            std::string_view slang_source, GLT::asset::material::blend_mode blend) override;


    private:

        struct mesh_slot {
            GLT::asset::handle                                      asset{};
            u32                                                     vertex_offset = 0;      // in vertices
            u32                                                     vertex_count = 0;
            u32                                                     index_offset = 0;       // in indices
            u32                                                     index_count = 0;
            bool                                                    alive = false;
            std::vector<u32>                                        material_slots{};    // per submesh, SSBO slot (default)
        };

        // Lifecycle ---------------------------------------------------------------------------------------------------

        void init_vulkan();
        void create_swapchain(const glm::ivec2 size);
        void destroy_swapchain();
        void resize_swapchain(const glm::ivec2 size);

        void create_render_target();
        void destroy_render_target();

        // ImGui -------------------------------------------------------------------------------------------------------

        void imgui_init();
        void imgui_shutdown();
        void create_imgui_resources();
        void destroy_imgui_resources();
        void begin_imgui_frame();
        void end_imgui_frame();

        // Mesh loading ------------------------------------------------------------------------------------------------

        bool reserve_mesh_space(u32 vertex_count, u32 index_count);
        void upload_mesh_slice(mesh_slot& slot, const GLT::asset::mesh::mesh_asset& mesh);
        FORCE_INLINE_R mesh_slot* find_slot(GLT::asset::handle h) noexcept;
        FORCE_INLINE_R const mesh_slot* find_slot(GLT::asset::handle h) const noexcept;
        void process_pending_meshes();

        // Lights ------------------------------------------------------------------------------------------------------

        void create_light_resources();
        void destroy_light_resources();
        void update_lights();

        // Lights (cluster infrastructure) -----------------------------------------------------------------------------

        void create_cluster_resources();
        void destroy_cluster_resources();

        // Recompute cluster dimensions from the current render size; rebuild buffers if they changed. Called from begin_frame()
        // after m_graph->begin_frame(), so non-persistent resources have already been reset by the graph and persistent ones are 
        // ready to be re-queried
        void update_cluster_dimensions();

        // Light culling -----------------------------------------------------------------------------------------------

        void create_light_culling_pipeline();
        void destroy_light_culling_pipeline();

        // Refresh the per-frame descriptors to point at the current frame's resources. Cheap and idempotent;
        // called once per frame from begin_frame()
        void update_light_cull_descriptors();

        // Rebuild the mesh pass descriptor buffer to point at the current frame's light grid/index list. Called from begin_frame()
        // after update_cluster_dimensions(), since the graph-owned cluster buffers may only exist after the first
        // update_cluster_dimensions() call (and after any render-size change)
        void update_mesh_descriptors();

        // Write the light meta UBO (light_count, index list capacity, per-cluster cap) for the current frame
        void update_light_meta_ubo();

        // Mesh pipeline and per-frame descriptor data -----------------------------------------------------------------

        void create_mesh_resources();
        void destroy_mesh_resources();
        void update_camera_ubo();

        // GPU-driven rendering ----------------------------------------------------------------------------------------

        void create_cull_pipeline();
        void destroy_cull_pipeline();

        void pack_mesh_into_ssbo(mesh_slot& mesh_slot, const GLT::asset::mesh::mesh_asset& mesh_asset);
        void clear_mesh_ssbo_entry(mesh_slot& mesh_slot);

        void build_instance_buffer();

        // Issue the whole scene as a single indexed indirect count draw. The graphics pipeline layout is unchanged — the vertex shader
        // still reads the instance SSBO via gl_InstanceIndex, and the cull pass filled the commands' firstInstance field to point into 
        // that buffer
        void draw_scene_indirect(vk::CommandBuffer cmd, vk::Buffer draw_commands, vk::Buffer draw_count);

        // Helpers -----------------------------------------------------------------------------------------------------

        void transition_image_layout(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout oldL,
            vk::ImageLayout newL, vk::ImageSubresourceRange range = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});

        // Debug view --------------------------------------------------------------------------------------------------

        void create_debug_view_resources();
        void destroy_debug_view_resources();
        void update_debug_view_descriptors();

        // shadow ------------------------------------------------------------------------------------------------------

        void create_shadow_resources();
        void destroy_shadow_resources();
        void update_shadow();                               // fit the light matrices + write the shadow camera UBO
        void create_local_shadow_resources();
        void destroy_local_shadow_resources();
        void update_local_shadows();                        // called from update_lights()
        glm::mat4 camera_projection() const;

        // materials & textures ----------------------------------------------------------------------------------------

        void create_material_resources();
        void destroy_material_resources();
        void update_materials();                                // per frame: poll for edits, upload SSBO + descriptor table when stale

        u32  acquire_material(GLT::asset::handle material);     // ref++ and returns the SSBO slot (default slot for INVALID_HANDLE)
        void release_material(u32 slot);
        void pack_material(u32 slot);                           // asset -> m_material_cpu[slot]; bumps the version if it changed
        void poll_material_changes();
        void refresh_material(GLT::asset::handle material);     // call from the editor after editing a material asset

        u32  acquire_texture(GLT::asset::handle texture);       // ref++ and returns the bindless slot (fallback on failure)
        void release_texture(u32 slot);
        bool create_gpu_texture(vk::Format format, u32 width, u32 height, const std::byte* pixels, u64 pixel_bytes, 
            std::span<const texture_upload_mip> mips, bool generate_mips, gpu_texture& out);
        void destroy_gpu_texture(gpu_texture& texture);

        // i_shader_compiler -------------------------------------------------------------------------------------------

        // mask over [GLT::render::compiled_shader] to give the compiled bytecode more meaning
        struct compiled_shader_bytecode_meaning {

            std::vector<std::byte>                  vertex_bytecode{};
            std::vector<std::byte>                  fragment_bytecode{};
            std::vector<std::byte>                  depth_bytecode{};
        };

        void create_shader_compiler();
        void destroy_shader_compiler();



        // Configuration -----------------------------------------------------------------------------------------------

        glm::ivec2                                                          m_render_size{ 800, 600 };
        glm::ivec2                                                          m_target_framebuffer_size{};
        glm::vec4                                                           m_clear_color{ 0.09f, 0.09f, 0.09f, 1.f };
        GLT::render::renderer_feature                                       m_features{};
        u32                                                                 m_active_mode{ 0 };
        bool                                                                m_world_render_enabled = true;

        // Vulkan core -------------------------------------------------------------------------------------------------

        util::instance_wrapper                                              m_instance;
        util::command_queues                                                m_queues{};
        vk::Device                                                          m_device = nullptr;
        vk::SurfaceKHR                                                      m_surface = nullptr;
        vk::PhysicalDevice                                                  m_physical_device = nullptr;
        util::deletion_queue                                                m_deletion_queue{};
        util::device*                                                       m_dev = nullptr;

        // Swapchain ---------------------------------------------------------------------------------------------------

        util::swapchain_builder                                             m_swapchain_builder;
        util::swapchain_resources                                           m_swapchain;
        u32                                                                 m_image_count = 0;
        u32                                                                 m_current_swapchain_image = 0;
        std::vector<vk::ImageLayout>                                        m_swapchain_images_layout{};

        // Frame synchronisation ---------------------------------------------------------------------------------------

        u32                                                                 m_current_frame = 0;
        std::vector<vk::Semaphore>                                          m_render_semaphores{};
        std::vector<vk::Semaphore>                                          m_present_semaphores{};
        std::vector<vk::Fence>                                              m_in_flight_fences{};

        // Command buffers ---------------------------------------------------------------------------------------------

        vk::CommandPool                                                     m_graphics_pool;
        std::array<vk::CommandBuffer, MAX_CONCURRENT_FRAMES>                m_render_cmd;

        vk::Fence                                                           m_immediate_submit_fence{};
        vk::CommandBuffer                                                   m_immediate_submit_command_buffer{};
        vk::CommandPool                                                     m_immediate_submit_command_pool{};

        // Platform ----------------------------------------------------------------------------------------------------

        GLT::platform::backend_api                                          mp_window_backend_api{};
        ref<GLT::platform::i_window_plugin>                                 mp_window{};
        handle                                                              m_framebuffer_resize_sub{};

        // Render graph resources --------------------------------------------------------------------------------------

        GLT::unique_ref<graph::render_graph>                                m_graph;
        graph::texture_handle                                               m_backbuffer_handle;
        graph::texture_handle                                               m_scene_color;
        graph::texture_handle                                               m_depth_target;

        // Output image (sampled by ImGui) and shader compiler ---------------------------------------------------------

        GLT::ref<image>                                                     m_output_image = nullptr;
        util::shader_compiler                                               m_shader_compiler{};

        // ImGui -------------------------------------------------------------------------------------------------------

        vk::DescriptorPool                                                  m_imgui_descriptor_pool = nullptr;
        vk::RenderPass                                                      m_imgui_render_pass = nullptr;
        std::vector<vk::Framebuffer>                                        m_imgui_framebuffers;
        bool                                                                m_imgui_initialized = false;

        // Camera and lighting settings --------------------------------------------------------------------------------

        GLT::world::camera_snapshot                                         m_active_camera{};
        GLT::render::sun_settings                                           m_sun_settings{};
        GLT::renderer::vk_rasterizer::settings::visual                      m_visual_settings{};

        // Stats and GPU timing ----------------------------------------------------------------------------------------

        u32                                                                 m_frame_draw_calls = 0;
        u32                                                                 m_frame_render_passes = 0;
        u32                                                                 m_live_buffer_count = 0;
        u32                                                                 m_live_pipeline_count = 0;
        u32                                                                 m_live_descriptor_set_count = 0;

        vk::QueryPool                                                       m_timestamp_pool = nullptr;
        f32                                                                 m_timestamp_period_ns = 1.0f;
        std::array<f32, MAX_CONCURRENT_FRAMES>                              m_gpu_frame_time_ms{};
        f32                                                                 m_last_gpu_time_ms = 0.0f;

        // Mesh system -------------------------------------------------------------------------------------------------

        util::allocated_buffer                                              m_vertex_buffer{};
        util::allocated_buffer                                              m_index_buffer{};
        u32                                                                 m_vertex_capacity = 0;
        u32                                                                 m_index_capacity = 0;
        u32                                                                 m_vertex_used = 0;
        u32                                                                 m_index_used = 0;

        std::vector<mesh_slot>                                              m_mesh_slots{};
        std::vector<u32>                                                    m_free_slots{};

        // Lights ------------------------------------------------------------------------------------------------------

        std::vector<GLT::world::light_snapshot>                             m_submitted_lights{};
        std::vector<GLT::world::light_snapshot>                             m_debug_lights{};
        std::vector<gpu_light>                                              m_packed_lights{};
        std::array<util::allocated_buffer, MAX_CONCURRENT_FRAMES>           m_light_buffers{};
        u32                                                                 m_light_count = 0;
        GLT::world::light_snapshot                                          m_new_debug_light{};

        // Clustered lighting ------------------------------------------------------------------------------------------

        struct cluster_grid_params {
            u32                                                             grid_x = 0;
            u32                                                             grid_y = 0;
            u32                                                             grid_z = 0;
            u32                                                             cluster_count = 0;
        };

        cluster_grid_params                                                 m_cluster_params{};
        std::array<graph::buffer_handle, MAX_CONCURRENT_FRAMES>             m_light_grid_handles{};
        std::array<graph::buffer_handle, MAX_CONCURRENT_FRAMES>             m_light_index_list_handles{};
        std::array<graph::buffer_handle, MAX_CONCURRENT_FRAMES>             m_light_counter_handles{};
        u32                                                                 m_cluster_index_capacity = 0;
        vk::DescriptorSetLayout                                             m_light_cull_set_layout = nullptr;
        vk::PipelineLayout                                                  m_light_cull_pipeline_layout = nullptr;
        vk::Pipeline                                                        m_light_cull_pipeline = nullptr;
        std::array<util::allocated_buffer, MAX_CONCURRENT_FRAMES>           m_light_meta_ubos{};
        std::array<util::descriptor_buffer, MAX_CONCURRENT_FRAMES>          m_light_cull_desc_buffers{};

        // Scene state (populated by submit_scene) ---------------------------------------------------------------------

        std::vector<GLT::asset::mesh::instance>                             m_scene_instances{};
        std::unordered_set<GLT::asset::handle>                              m_scene_meshes{};
        std::unordered_set<GLT::asset::handle>                              m_retained_meshes{};
        std::vector<GLT::asset::handle>                                     m_pending_loads{};
        std::vector<GLT::asset::handle>                                     m_pending_unloads{};

        // Mesh pipeline + per-frame descriptor data -------------------------------------------------------------------

        vk::DescriptorSetLayout                                             m_mesh_set_layout = nullptr;
        vk::PipelineLayout                                                  m_mesh_pipeline_layout = nullptr;
        vk::Pipeline                                                        m_mesh_pipeline = nullptr;
        vk::Pipeline                                                        m_depth_pipeline = nullptr;

        std::array<util::allocated_buffer, MAX_CONCURRENT_FRAMES>           m_camera_ubos{};
        std::array<util::allocated_buffer, MAX_CONCURRENT_FRAMES>           m_instance_buffers{};
        std::vector<gpu_instance_data>                                      m_packed_instances{};
        u32                                                                 m_instance_count = 0;
        std::array<util::descriptor_buffer, MAX_CONCURRENT_FRAMES>          m_frame_desc_buffers{};

        // GPU-driven culling ------------------------------------------------------------------------------------------

        vk::DescriptorSetLayout                                             m_cull_set_layout = nullptr;
        vk::PipelineLayout                                                  m_cull_pipeline_layout = nullptr;
        vk::Pipeline                                                        m_cull_pipeline = nullptr;

        util::allocated_buffer                                              m_mesh_data_buffer{};
        util::allocated_buffer                                              m_submesh_data_buffer{};
        u32                                                                 m_submesh_used = 0;

        std::array<util::descriptor_buffer, MAX_CONCURRENT_FRAMES>          m_cull_desc_buffers{};
        std::array<graph::buffer_handle, MAX_CONCURRENT_FRAMES>             m_draw_commands_handles{};
        std::array<graph::buffer_handle, MAX_CONCURRENT_FRAMES>             m_draw_count_handles{};
        vk::Sampler                                                         m_depth_sampler = nullptr;

        // Debug view ---------------------------------------------------------------------------------------------------

        vk::DescriptorSetLayout                                             m_light_cull_debug_set_layout = nullptr;
        vk::PipelineLayout                                                  m_light_cull_debug_pipeline_layout = nullptr;
        vk::Pipeline                                                        m_light_cull_debug_pipeline = nullptr;
        std::array<util::descriptor_buffer, MAX_CONCURRENT_FRAMES>          m_light_cull_debug_desc_buffers{};

        // shadow ------------------------------------------------------------------------------------------------------

        shadow_settings                                                     m_shadow_settings{};
        shadow_state                                                        m_shadow{};
        graph::texture_handle                                               m_shadow_map;
        vk::Sampler                                                         m_shadow_sampler = nullptr;
        std::array<util::allocated_buffer, MAX_CONCURRENT_FRAMES>           m_shadow_camera_ubos{};
        std::array<util::descriptor_buffer, MAX_CONCURRENT_FRAMES>          m_shadow_desc_buffers{};        // mesh set, binding 0 -> shadow UBO
        std::array<util::descriptor_buffer, MAX_CONCURRENT_FRAMES>          m_shadow_cull_desc_buffers{};
        std::array<graph::buffer_handle, MAX_CONCURRENT_FRAMES>             m_shadow_draw_commands_handles{};
        std::array<graph::buffer_handle, MAX_CONCURRENT_FRAMES>             m_shadow_draw_count_handles{};
        graph::texture_handle                                               m_shadow_atlas;
        std::array<util::allocated_buffer, MAX_CONCURRENT_FRAMES>           m_shadow_view_buffers{};            // SSBO + UBO slices
        std::vector<gpu_shadow_view>                                        m_packed_shadow_views{};
        std::vector<local_shadow_tile>                                      m_local_shadow_tiles{};
        u32                                                                 m_local_shadow_view_count = 0;
        std::array<util::descriptor_buffer, MAX_CONCURRENT_FRAMES>          m_local_shadow_desc_buffers{};      // MAX_SHADOW_VIEWS sets, set i -> UBO slice i
        std::array<util::descriptor_buffer, MAX_CONCURRENT_FRAMES>          m_local_shadow_cull_desc_buffers{};
        vk::DescriptorSetLayout                                             m_local_shadow_cull_set_layout = nullptr;
        vk::PipelineLayout                                                  m_local_shadow_cull_pipeline_layout = nullptr;
        vk::Pipeline                                                        m_local_shadow_cull_pipeline = nullptr;
        std::array<graph::buffer_handle, MAX_CONCURRENT_FRAMES>             m_local_shadow_commands_handles{};
        std::array<graph::buffer_handle, MAX_CONCURRENT_FRAMES>             m_local_shadow_counts_handles{};

        // materials & textures ----------------------------------------------------------------------------------------

        vk::DescriptorSetLayout                                             m_material_set_layout = nullptr;    // set 1
        vk::Sampler                                                         m_material_sampler = nullptr;       // set 0, binding 8
        std::array<util::allocated_buffer, MAX_CONCURRENT_FRAMES>           m_material_buffers{};
        std::array<util::descriptor_buffer, MAX_CONCURRENT_FRAMES>          m_material_desc_buffers{};
        std::array<u32, MAX_CONCURRENT_FRAMES>                              m_material_uploaded_version{};
        std::array<u32, MAX_CONCURRENT_FRAMES>                              m_texture_uploaded_version{};

        std::vector<material_slot>                                          m_material_slots{};
        std::vector<u32>                                                    m_free_material_slots{};
        std::unordered_map<GLT::asset::handle, u32>                         m_material_slot_by_asset{};
        std::vector<gpu_material>                                           m_material_cpu{};                   // MAX_MATERIALS entries
        u32                                                                 m_material_version = 1;
        u32                                                                 m_material_poll_counter = 0;

        std::vector<gpu_texture>                                            m_gpu_textures{};                   // indexed by bindless slot
        std::vector<u32>                                                    m_free_texture_slots{};
        std::unordered_map<GLT::asset::handle, u32>                         m_texture_slot_by_asset{};
        std::vector<util::accessible_image>                                 m_texture_table{};                  // mirrors the bindless array (MAX_TEXTURES)
        u32                                                                 m_texture_version = 1;
        u32                                                                 m_texture_high_water = 0;

        std::vector<u32>                                                    m_instance_material_slots{};   // size = instances * MAX_SUBMESHES_PER_MESH
        std::vector<GLT::asset::handle>                                     m_instance_material_handles{}; // parallel, for change detection
        std::array<util::allocated_buffer, MAX_CONCURRENT_FRAMES>           m_instance_override_buffers{}; // per-frame GPU mirror

        // Material shader compiler (i_shader_compiler)
        util::slang_compiler                                                m_slang{};
        u64                                                                 m_shader_compiler_hash = 0;

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
#include "renderer_gpu_driven.inl"
#include "renderer_lights.inl"
#include "renderer_clusters.inl"
#include "renderer_light_culling.inl"
#include "renderer_debug_view.inl"
#include "renderer_shadow.inl"
#include "renderer_material.inl"
#include "renderer_shader_compiler.inl"
// #include "renderer_material_texture.inl"
// #include "renderer_preview.inl"

EXPORT_PLUGIN_CLASS(GLT::renderer::vk_rasterizer::renderer, GLT::renderer::vk_rasterizer::descriptor)
