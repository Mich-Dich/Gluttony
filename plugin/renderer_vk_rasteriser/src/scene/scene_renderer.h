#pragma once

#include "util/pch.h"

#include "render/i_scene_renderer.h"
#include "render/i_debug_draw.h"
#include "util/builders.h"
#include "plugin/image.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {
    class renderer;
    class shared_resource_manager;
}

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // A single scene renderer: one target (window surface or offscreen image), its own per-frame sync, its own
    // camera / instance / light submission buffers, its own swapchain (surface only)
    //
    // The plugin owns shared resources; this class holds a direct reference to the plugin + its shared_resource_manager
    // Phase 3 only clears the target — Phase 4 wires the forward pass
    class scene_renderer
        : public GLT::render::i_scene_renderer,
        public GLT::render::i_debug_draw {
    public:

        static std::expected<std::unique_ptr<scene_renderer>, GLT::render::scene_renderer_error> create(renderer& plugin,
            const GLT::render::scene_renderer_desc& desc);

        ~scene_renderer() override;

        scene_renderer(const scene_renderer&) = delete;
        scene_renderer& operator=(const scene_renderer&) = delete;

        // content submission ------------------------------------------------------------------------------------------

        void submit_scene(std::span<const GLT::asset::mesh::instance> instances) override;
        void submit_lights(std::span<const GLT::world::light_snapshot> lights) override;
        void set_active_camera(const GLT::world::camera_snapshot& camera) override;

        // lifecycle ---------------------------------------------------------------------------------------------------

        void update(f32 delta_time) override;
        [[nodiscard]] std::expected<void, GLT::render::render_error> render(u64 frame_number) override;

        // target ------------------------------------------------------------------------------------------------------

        void resize(glm::uvec2 extent) override;
        [[nodiscard]] void* get_output_image() const override;
        [[nodiscard]] glm::uvec2 get_extent() const override { return m_extent; }
        [[nodiscard]] bool is_offscreen() const override { return m_offscreen; }

        // debug draw --------------------------------------------------------------------------------------------------

        [[nodiscard]] GLT::render::i_debug_draw* get_debug_draw() override { return this; }

        // render modes ------------------------------------------------------------------------------------------------

        [[nodiscard]] std::span<const GLT::render::render_mode_info> supported_modes() const override;
        void set_render_mode(u32 id) override { m_active_mode = id; }
        [[nodiscard]] u32 get_render_mode() const override { return m_active_mode; }
        void set_active(bool active) override { m_active = active; }
        [[nodiscard]] bool is_active() const override { return m_active; }

        // procedural meshes -------------------------------------------------------------------------------------------

        [[nodiscard]] GLT::render::procedural_mesh_id create_procedural_mesh(GLT::render::mesh_update_class cls) override;
        std::expected<void, GLT::render::procedural_mesh_error> update_procedural_mesh(GLT::render::procedural_mesh_id id,
            std::span<const GLT::asset::mesh::vertex> vertices, std::span<const u32> indices, 
            GLT::asset::handle material = INVALID_HANDLE) override;
        void destroy_procedural_mesh(GLT::render::procedural_mesh_id id) override;

    public: // i_debug_draw --------------------------------------------------------------------------------------------

        void draw_line(const GLT::render::debug_line_primitive& line) override;
        void draw_triangle(const GLT::render::debug_triangle_primitive& tri) override;
        void draw_aabb(const GLT::render::debug_aabb_primitive& box) override;
        void clear() override;
        void set_draw_mesh_aabb(GLT::asset::handle mesh_handle, const glm::vec4& color, const f32 lifetime,
            const bool depth_test) override;
        void highlight_entity(std::span<const GLT::world::entity_id> ids, const glm::vec4& color, const f32 lifetime,
            const bool depth_test) override;

    private:

        struct frame_slot {
            vk::Fence                                   fence = nullptr;
            vk::CommandBuffer                           cmd = nullptr;
            vk::Semaphore                               image_available = nullptr;   // surface only
            vk::Semaphore                               render_finished = nullptr;   // surface only
        };

        struct cluster_grid_params {
            u32                                         grid_x = 0;
            u32                                         grid_y = 0;
            u32                                         grid_z = 0;
            u32                                         cluster_count = 0;
        };

        scene_renderer(renderer& plugin, const GLT::render::scene_renderer_desc& desc);

        // init / teardown ---------------------------------------------------------------------------------------------

        void init_surface_target(const GLT::render::surface_target_desc& target);
        void init_offscreen_target(const GLT::render::offscreen_target_desc& target);
        void init_frames();
        void init_gpu_state();
        void destroy_gpu_state();
        void destroy_internal();

        // per-frame CPU work (update()) -------------------------------------------------------------------------------

        void ensure_scene_meshes();
        void build_instance_buffer();
        void update_lights();
        void update_shadow();
        void update_local_shadows();
        void update_camera_ubo();
        void update_globals();
        void write_scene_material_descriptors();

        // per-frame GPU work (render()) -------------------------------------------------------------------------------

        void import_frame_textures();
        void update_cluster_dimensions();
        void update_light_meta_ubo();
        void update_light_cull_descriptors();
        void update_mesh_descriptors();

        // pipelines + resources ---------------------------------------------------------------------------------------

        void create_mesh_material_resources();
        void destroy_mesh_material_resources();
        void create_cluster_resources();
        void destroy_cluster_resources();
        void create_light_culling_pipeline();
        void destroy_light_culling_pipeline();
        void create_cull_pipeline();
        void destroy_cull_pipeline();
        void create_shadow_resources();
        void destroy_shadow_resources();
        void create_local_shadow_resources();
        void destroy_local_shadow_resources();
        void create_blit_pipeline();
        void destroy_blit_pipeline();

        // passes ------------------------------------------------------------------------------------------------------

        void add_reset_draw_count_pass();
        void add_cull_pass();
        void add_depth_prepass_pass();
        void add_clear_light_grid_pass();
        void add_light_culling_pass();
        void add_scene_pass();
        void add_blit_to_backbuffer_pass();
        void add_reset_shadow_draw_count_pass();
        void add_shadow_cull_pass();
        void add_shadow_pass();
        void add_local_shadow_passes();
        void add_clear_backbuffer_pass();

        // debug draw --------------------------------------------------------------------------------------------------

        void create_debug_draw_resources();
        void destroy_debug_draw_resources();
        void process_debug_primitives();
        void add_debug_draw_pass();

        // imgui (surface targets only) --------------------------------------------------------------------------------

        void imgui_init();
        void imgui_shutdown();
        void begin_imgui_frame();
        void end_imgui_frame();
        void add_imgui_pass();
        void recreate_imgui_framebuffers();

        // helpers -----------------------------------------------------------------------------------------------------

        glm::mat4 camera_projection() const;



        renderer&                                                           m_plugin;
        shared_resource_manager*                                            m_shared = nullptr;
        bool                                                                m_active = true;
        GLT::render::scene_renderer_desc                                    m_desc{};
        bool                                                                m_offscreen = false;
        glm::uvec2                                                          m_extent{0, 0};
        glm::vec4                                                           m_clear_color{0.09f, 0.09f, 0.09f, 1.f};
        u32                                                                 m_active_mode = 0;
        f32                                                                 m_delta_time = 0.f;
        u32                                                                 m_dup_counter = 250;

        // Per-frame sync
        vk::CommandPool                                                     m_graphics_pool = nullptr;
        std::array<frame_slot, MAX_CONCURRENT_FRAMES>                       m_frames{};
        u32                                                                 m_current_frame = 0;

        // Swapchain (surface only)
        util::swapchain_builder                                             m_swapchain_builder{};
        util::swapchain_resources                                           m_swapchain{};
        u32                                                                 m_image_count = 0;
        u32                                                                 m_current_swapchain_image = 0;
        std::vector<vk::ImageLayout>                                        m_swapchain_images_layout{};

        // Offscreen
        ref<GLT::renderer::vk_rasterizer::image>                            m_output_image = nullptr;

        // Submission
        std::vector<GLT::asset::mesh::instance>                             m_scene_instances{};
        std::vector<GLT::world::light_snapshot>                             m_submitted_lights{};
        GLT::world::camera_snapshot                                         m_active_camera{};
        std::unordered_map<GLT::asset::handle, shared_mesh_id>              m_resident_meshes{};

        // Instances
        std::vector<gpu_instance_data>                                      m_packed_instances{};
        u32                                                                 m_instance_count = 0;
        std::vector<u32>                                                    m_instance_material_slots{};
        std::vector<GLT::asset::handle>                                     m_instance_material_handles{};
        std::vector<u32>                                                    m_scene_materials{};

        // Lights
        std::vector<gpu_light>                                              m_packed_lights{};
        std::array<util::allocated_buffer, MAX_CONCURRENT_FRAMES>           m_light_buffers{};
        u32                                                                 m_light_count = 0;

        // Clusters
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

        // Cull
        vk::DescriptorSetLayout                                             m_cull_set_layout = nullptr;
        vk::PipelineLayout                                                  m_cull_pipeline_layout = nullptr;
        vk::Pipeline                                                        m_cull_pipeline = nullptr;
        std::array<util::descriptor_buffer, MAX_CONCURRENT_FRAMES>          m_cull_desc_buffers{};
        std::array<graph::buffer_handle, MAX_CONCURRENT_FRAMES>             m_draw_commands_handles{};
        std::array<graph::buffer_handle, MAX_CONCURRENT_FRAMES>             m_draw_count_handles{};

        // Mesh descriptor + per-frame resource buffers
        std::array<util::allocated_buffer, MAX_CONCURRENT_FRAMES>           m_camera_ubos{};
        std::array<util::allocated_buffer, MAX_CONCURRENT_FRAMES>           m_globals_ubos{};
        std::array<util::allocated_buffer, MAX_CONCURRENT_FRAMES>           m_instance_buffers{};
        std::array<util::allocated_buffer, MAX_CONCURRENT_FRAMES>           m_instance_override_buffers{};
        std::array<util::descriptor_buffer, MAX_CONCURRENT_FRAMES>          m_frame_desc_buffers{};
        std::array<util::descriptor_buffer, MAX_CONCURRENT_FRAMES>          m_material_desc_buffers{};

        // Shadow (directional)
        shadow_settings                                                     m_shadow_settings{};
        shadow_state                                                        m_shadow{};
        graph::texture_handle                                               m_shadow_map{};
        std::array<util::allocated_buffer, MAX_CONCURRENT_FRAMES>           m_shadow_camera_ubos{};
        std::array<util::descriptor_buffer, MAX_CONCURRENT_FRAMES>          m_shadow_desc_buffers{};
        std::array<util::descriptor_buffer, MAX_CONCURRENT_FRAMES>          m_shadow_cull_desc_buffers{};
        std::array<graph::buffer_handle, MAX_CONCURRENT_FRAMES>             m_shadow_draw_commands_handles{};
        std::array<graph::buffer_handle, MAX_CONCURRENT_FRAMES>             m_shadow_draw_count_handles{};

        // Shadow (local)
        graph::texture_handle                                               m_shadow_atlas{};
        std::array<util::allocated_buffer, MAX_CONCURRENT_FRAMES>           m_shadow_view_buffers{};
        std::vector<gpu_shadow_view>                                        m_packed_shadow_views{};
        std::vector<local_shadow_tile>                                      m_local_shadow_tiles{};
        u32                                                                 m_local_shadow_view_count = 0;
        std::array<util::descriptor_buffer, MAX_CONCURRENT_FRAMES>          m_local_shadow_desc_buffers{};
        std::array<util::descriptor_buffer, MAX_CONCURRENT_FRAMES>          m_local_shadow_cull_desc_buffers{};
        vk::DescriptorSetLayout                                             m_local_shadow_cull_set_layout = nullptr;
        vk::PipelineLayout                                                  m_local_shadow_cull_pipeline_layout = nullptr;
        vk::Pipeline                                                        m_local_shadow_cull_pipeline = nullptr;
        std::array<graph::buffer_handle, MAX_CONCURRENT_FRAMES>             m_local_shadow_commands_handles{};
        std::array<graph::buffer_handle, MAX_CONCURRENT_FRAMES>             m_local_shadow_counts_handles{};

        // Blit (surface only)
        vk::DescriptorSetLayout                                             m_blit_set_layout = nullptr;
        vk::PipelineLayout                                                  m_blit_pipeline_layout = nullptr;
        vk::Pipeline                                                        m_blit_pipeline = nullptr;
        std::array<util::descriptor_buffer, MAX_CONCURRENT_FRAMES>          m_blit_desc_buffers{};

        // Graph + target handles
        unique_ref<graph::render_graph>                                     m_graph;
        graph::texture_handle                                               m_backbuffer_handle{};
        graph::texture_handle                                               m_scene_color{};
        graph::texture_handle                                               m_depth_target{};

        u32                                                                 m_frame_counter = 0;

        // debug draw state --------------------------------------------------------------------------------------------

        mutable std::mutex                                                  m_debug_mutex{};
        std::vector<debug_primitive_record>                                 m_debug_primitives{};
        std::vector<watched_mesh>                                           m_watched_meshes{};
        std::vector<watched_entity>                                         m_watched_entities{};
        f64                                                                 m_debug_last_frame_time = 0.0;
        std::array<frame_debug_layout, MAX_CONCURRENT_FRAMES>               m_debug_layouts{};
        std::array<util::allocated_buffer, MAX_CONCURRENT_FRAMES>           m_debug_vertex_buffers{};
        std::array<util::descriptor_buffer, MAX_CONCURRENT_FRAMES>          m_debug_desc_buffers{};
        vk::DescriptorSetLayout                                             m_debug_set_layout = nullptr;
        vk::PipelineLayout                                                  m_debug_pipeline_layout = nullptr;
        vk::Pipeline                                                        m_debug_pipeline_line_depth = nullptr;
        vk::Pipeline                                                        m_debug_pipeline_line_overlay = nullptr;
        vk::Pipeline                                                        m_debug_pipeline_tri_depth = nullptr;
        vk::Pipeline                                                        m_debug_pipeline_tri_overlay = nullptr;

        // imgui state (surface only) ----------------------------------------------------------------------------------

        vk::DescriptorPool                                                  m_imgui_descriptor_pool = nullptr;
        vk::RenderPass                                                      m_imgui_render_pass = nullptr;
        std::vector<vk::Framebuffer>                                        m_imgui_framebuffers{};
        bool                                                                m_imgui_initialized = false;

        // Procedural meshes -------------------------------------------------------------------------------------------

        std::vector<procedural_mesh_slot>                                   m_procedural_meshes{};
        std::vector<u32>                                                    m_free_procedural_slots{};
        std::vector<u32>                                                    m_procedural_draw_slots{};   // per-frame: mesh slot per procedural instance
        u32                                                                 m_asset_instance_count = 0;  // instance SSBO prefix used by cull

    };

}
