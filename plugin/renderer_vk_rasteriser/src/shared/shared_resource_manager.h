
#pragma once

#include "shared/type.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Owns every resource that is immutable-after-upload and shared between scene renderers
    //
    // One instance per renderer plugin. Scene renderers grab a reference at construction and call into it directly
    // The plugin owns the instance and drives `begin_frame()` once per frame (before any scene renderer's update())
    //
    // Threading: v1 is single-threaded — all ensure_*/retain/release/acquire must be called from the update thread
    // Uploads happen synchronously (via immediate_submit) so an `ensure_*` that returns success means `is_*_ready` is true
    class shared_resource_manager {
    public:

        GETTER_C(vk::Buffer,                            vertex_buffer, m_vertex_buffer.buffer)
        GETTER_C(vk::Buffer,                            index_buffer, m_index_buffer.buffer)
        GETTER_C(vk::Buffer,                            mesh_data_buffer, m_mesh_data_buffer.buffer)
        GETTER_C(vk::Buffer,                            submesh_data_buffer, m_submesh_data_buffer.buffer)
        GETTER_REF(util::allocated_buffer,              mesh_data_buffer_alloc, m_mesh_data_buffer)
        GETTER_REF(util::allocated_buffer,              submesh_data_buffer_alloc, m_submesh_data_buffer)
        DEFAULT_GETTER_C(vk::DescriptorSetLayout,       mesh_set_layout)
        DEFAULT_GETTER_C(vk::DescriptorSetLayout,       material_set_layout)
        DEFAULT_GETTER_C(vk::PipelineLayout,            mesh_pipeline_layout)
        DEFAULT_GETTER_C(vk::Sampler,                   material_sampler)
        DEFAULT_GETTER_C(vk::Sampler,                   shadow_sampler)
        DEFAULT_GETTER_C(vk::Sampler,                   depth_sampler)

        // lifecycle ---------------------------------------------------------------------------------------------------

        shared_resource_manager(vk::Device device, vk::PhysicalDevice physical_device, util::device* dev, vk::Queue graphics_queue,
            u32 graphics_queue_family);
        ~shared_resource_manager();

        // Called once from the plugin's create(). Prepares layouts, samplers, the shader compiler, fallback material
        void create();

        // Called once from the plugin's destroy(). Waits for the GPU, then releases everything
        void destroy();

        // Per-frame tick. Advances material polling; retires any pending resources
        void begin_frame(u64 frame_number);

        // meshes ------------------------------------------------------------------------------------------------------

        // Idempotent per asset handle. Uploads synchronously on first use; subsequent calls return the same id
        // `retain_mesh(id)` must be called to keep the mesh resident
        std::expected<shared_mesh_id, upload_error> ensure_mesh(GLT::asset::handle asset);

        // Increment ref count. Balanced with release_mesh()
        void retain_mesh(shared_mesh_id id);

        // Decrement ref count. When it reaches zero the mesh (and its material slots) are released to the GPU
        void release_mesh(shared_mesh_id id);

        // True if the mesh is resident and usable
        [[nodiscard]] bool is_mesh_ready(shared_mesh_id id) const;

        // Returns nullptr if id is invalid
        [[nodiscard]] const mesh_slot* get_mesh(shared_mesh_id id) const;

        // textures ----------------------------------------------------------------------------------------------------

        // Idempotent per asset handle
        std::expected<shared_texture_id, upload_error> ensure_texture(GLT::asset::handle asset);

        void retain_texture(shared_texture_id id);

        void release_texture(shared_texture_id id);

        [[nodiscard]] bool is_texture_ready(shared_texture_id id) const;

        // Returns a white-fallback accessible_image if id is invalid
        [[nodiscard]] const util::accessible_image& get_texture(shared_texture_id id) const;

        // materials ---------------------------------------------------------------------------------------------------

        // Ref++ and returns the SSBO slot. Pass INVALID_HANDLE to obtain the default material slot
        // First use uploads shaders/textures/params; subsequent calls just increment
        [[nodiscard]] u32 acquire_material(GLT::asset::handle handle);

        // Ref--. When it reaches zero, releases shaders, textures, params
        void release_material(u32 slot);

        [[nodiscard]] const material_slot& get_material(u32 slot) const;

        // Called by begin_frame() every MATERIAL_POLL_INTERVAL frames. Reloads shaders/textures/params for
        // materials that have changed on disk. Public so tests / tools can force a sweep
        void poll_material_changes();

        // Helper for scene renderers: writes the material descriptor for [slot_index] into the scene's own
        // descriptor buffer at [target] for [frame_index]. Call once per (slot, frame) that the scene actually uses
        void write_material_descriptor(util::descriptor_buffer& target, u32 slot_index) const;

        // compiler ----------------------------------------------------------------------------------------------------

        [[nodiscard]] std::string_view compiler_name() const;
        [[nodiscard]] u64 implementation_hash() const { return m_shader_compiler_hash; }
        [[nodiscard]] u32 contract_version() const;

        std::expected<GLT::render::compiled_shader, GLT::render::shader_compile_error> compile(std::string_view slang_source,
            GLT::asset::material::blend_mode blend);

        // stats -------------------------------------------------------------------------------------------------------

        [[nodiscard]] u32 live_buffer_count() const { return m_live_buffer_count; }
        [[nodiscard]] u32 live_pipeline_count() const { return m_live_pipeline_count; }
        [[nodiscard]] u32 live_descriptor_set_count() const { return m_live_descriptor_set_count; }

        // immediate submit --------------------------------------------------------------------------------------------

        void immediate_submit(std::function<void(vk::CommandBuffer)>&& fn);

    private:

        // lifecycle helpers -------------------------------------------------------------------------------------------

        void create_layouts();
        void destroy_layouts();
        void create_samplers();
        void destroy_samplers();
        void create_immediate_submit();
        void destroy_immediate_submit();
        void create_mesh_buffers();
        void destroy_mesh_buffers();
        void create_fallback_textures();
        void install_fallback_material();

        // mesh internal -----------------------------------------------------------------------------------------------

        [[nodiscard]] mesh_slot* find_slot(GLT::asset::handle handle);
        [[nodiscard]] const mesh_slot* find_slot(GLT::asset::handle handle) const;
        bool reserve_mesh_space(u32 vertex_count, u32 index_count);
        void upload_mesh_slice(mesh_slot& slot, const GLT::asset::mesh::mesh_asset& mesh);
        void pack_mesh_into_ssbo(mesh_slot& slot, const GLT::asset::mesh::mesh_asset& mesh);
        void clear_mesh_ssbo_entry(mesh_slot& slot);
        shared_mesh_id load_mesh_internal(GLT::asset::handle handle);

        // texture internal --------------------------------------------------------------------------------------------

        bool create_gpu_texture(vk::Format format, u32 width, u32 height, const std::byte* pixels, u64 pixel_bytes,
            std::span<const texture_upload_mip> mips, bool generate_mips, gpu_texture& out);

        void destroy_gpu_texture(gpu_texture& texture);

        shared_texture_id acquire_texture_internal(GLT::asset::handle handle);
        void release_texture_internal(u32 slot);

        // material internal -------------------------------------------------------------------------------------------

        void pack_material(u32 slot_index);
        void destroy_material_shaders(material_slot& slot);
        bool load_material_shaders(material_slot& slot, const GLT::asset::material::material_asset& asset);
        vk::Pipeline create_material_pipeline(const material_slot& slot, material_pipeline_kind kind);

        // shader compiler internal ------------------------------------------------------------------------------------

        void create_shader_compiler();
        void destroy_shader_compiler();



        vk::Device                                                          m_device = nullptr;
        vk::PhysicalDevice                                                  m_physical_device = nullptr;
        util::device*                                                       m_dev = nullptr;
        vk::Queue                                                           m_graphics_queue = nullptr;
        u32                                                                 m_graphics_family = 0;

        // immediate submit --------------------------------------------------------------------------------------------

        vk::CommandPool                                                     m_immediate_pool = nullptr;
        vk::CommandBuffer                                                   m_immediate_cmd = nullptr;
        vk::Fence                                                           m_immediate_fence = nullptr;

        // meshes ------------------------------------------------------------------------------------------------------

        util::allocated_buffer                                              m_vertex_buffer{};
        util::allocated_buffer                                              m_index_buffer{};
        u32                                                                 m_vertex_capacity = 0;
        u32                                                                 m_index_capacity = 0;
        u32                                                                 m_vertex_used = 0;
        u32                                                                 m_index_used = 0;

        std::vector<mesh_slot>                                              m_mesh_slots{};
        std::vector<u32>                                                    m_free_slots{};
        std::unordered_set<GLT::asset::handle>                              m_failed_mesh_loads{};
        std::unordered_map<GLT::asset::handle, shared_mesh_id>              m_mesh_id_by_asset{};

        util::allocated_buffer                                              m_mesh_data_buffer{};
        util::allocated_buffer                                              m_submesh_data_buffer{};
        u32                                                                 m_submesh_used = 0;

        // textures ----------------------------------------------------------------------------------------------------

        std::vector<gpu_texture>                                            m_gpu_textures{};
        std::vector<u32>                                                    m_free_texture_slots{};
        std::unordered_map<GLT::asset::handle, u32>                         m_texture_slot_by_asset{};
        std::vector<util::accessible_image>                                 m_texture_table{};

        // materials ---------------------------------------------------------------------------------------------------

        std::vector<material_slot>                                          m_material_slots{};
        std::vector<u32>                                                    m_free_material_slots{};
        std::unordered_map<GLT::asset::handle, u32>                         m_material_slot_by_asset{};
        u32                                                                 m_material_poll_counter = 0;

        // layouts + samplers ------------------------------------------------------------------------------------------

        vk::DescriptorSetLayout                                             m_mesh_set_layout = nullptr;
        vk::DescriptorSetLayout                                             m_material_set_layout = nullptr;
        vk::PipelineLayout                                                  m_mesh_pipeline_layout = nullptr;
        vk::Sampler                                                         m_material_sampler = nullptr;
        vk::Sampler                                                         m_shadow_sampler = nullptr;
        vk::Sampler                                                         m_depth_sampler = nullptr;

        // shader compiler ---------------------------------------------------------------------------------------------

        util::slang_compiler                                                m_slang{};
        u64                                                                 m_shader_compiler_hash = 0;

        // stats -------------------------------------------------------------------------------------------------------

        u32                                                                 m_live_buffer_count = 0;
        u32                                                                 m_live_pipeline_count = 0;
        u32                                                                 m_live_descriptor_set_count = 0;

    };

}
