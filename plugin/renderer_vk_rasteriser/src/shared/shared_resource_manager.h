
#pragma once

#include "shared/type.h"
#include "util/retire_queue.h"
#include "render/procedural_mesh.h"



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
    //
    // Mesh data lives in device-local buffers (VRAM). `ensure_mesh` copies it into staging memory and records the GPU
    // copies into an upload command buffer that `flush_uploads()` submits; every scene renderer calls that right before
    // its own submission and waits for the returned value. So an `ensure_mesh` that returns success means the mesh is
    // drawable in the next submitted frame. Textures still upload synchronously (immediate_submit)
    class shared_resource_manager {
    public:

        GETTER_C(vk::Buffer,                            vertex_buffer, m_vertex_buffer.buffer)
        GETTER_C(vk::Buffer,                            index_buffer, m_index_buffer.buffer)
        GETTER_C(vk::Buffer,                            mesh_data_buffer, m_mesh_data_buffer.buffer)
        GETTER_C(vk::Buffer,                            submesh_data_buffer, m_submesh_data_buffer.buffer)
        GETTER_REF(util::allocated_buffer,              mesh_data_buffer_alloc, m_mesh_data_buffer)
        GETTER_REF(util::allocated_buffer,              submesh_data_buffer_alloc, m_submesh_data_buffer)
        GETTER_REF(util::allocated_buffer,              cluster_data_buffer_alloc, m_cluster_data_buffer)       // virtual geometry
        GETTER_REF(util::allocated_buffer,              mesh_cluster_buffer_alloc, m_mesh_cluster_buffer)
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

        // Idempotent per asset handle. Stages the upload on first use (drawable from the next submitted frame, see
        // flush_uploads()); subsequent calls return the same id
        // `retain_mesh(id)` must be called to keep the mesh resident
        std::expected<shared_mesh_id, upload_error> ensure_mesh(GLT::asset::handle asset);

        // Increment ref count. Balanced with release_mesh()
        void retain_mesh(shared_mesh_id id);

        // Decrement ref count. When it reaches zero the mesh (and its material slots) are released to the GPU
        void release_mesh(shared_mesh_id id);

        // Re-uploads the mesh if its asset was edited in place (mesh_asset::revision, e.g. LOD regeneration in the mesh
        // editor) or replaced (reload). The slot id stays the same; old GPU ranges are retired, never overwritten while
        // in flight. Returns true if it re-uploaded. Cheap when nothing changed (one registry lookup)
        bool refresh_mesh(shared_mesh_id id);

        // True if the mesh is resident and usable
        [[nodiscard]] bool is_mesh_ready(shared_mesh_id id) const;

        // procedural meshes -------------------------------------------------------------------------------------------

        // The slot drawing the newest uploaded version of [id], INVALID_SHARED_MESH while none is (not uploaded yet, stale
        // or destroyed id). O(1). Every version gets a fresh slot (and mesh-table index): an entry, once published, is never
        // rewritten, and shadow caches see a changed mesh as a changed caster
        [[nodiscard]] shared_mesh_id procedural_mesh(GLT::render::procedural_mesh_id id) const;

        // Called by begin_frame(): picks up what generators changed in GLT::render::procedural_meshes() and uploads it,
        // within PROCEDURAL_UPLOAD_BUDGET per frame. Ten updates of a mesh between two frames cost one upload
        void sync_procedural();

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

        // Local shadows render every view in one pass and keep each view in its atlas tile with clip distances (material
        // vertex shader, RENDERER_SHADOW_ATLAS). False without the shaderClipDistance device feature: no local shadows
        [[nodiscard]] bool local_shadow_atlas_supported() const { return m_local_shadow_atlas; }

        std::expected<GLT::render::compiled_shader, GLT::render::shader_compile_error> compile(std::string_view slang_source,
            GLT::asset::material::blend_mode blend);

        // deferred destruction ----------------------------------------------------------------------------------------

        // Every queue submission that can reference renderer resources must signal `retire_timeline()` with
        // `next_retire_value()`. Objects handed to `retire*()` are destroyed once that work has completed
        [[nodiscard]] vk::Semaphore retire_timeline() const { return m_retire.timeline(); }
        [[nodiscard]] u64 next_retire_value() { return m_retire.next_signal_value(); }

        // While an upload command buffer is being recorded, [fn] waits until that command buffer is submitted (it may
        // reference the object), then goes to the retire queue as usual
        void retire(std::function<void()>&& fn);

        // Moves [buffer] into the retire queue and clears the caller's copy
        void retire_buffer(util::allocated_buffer& buffer);

        // mesh uploads ------------------------------------------------------------------------------------------------

        // Submits the mesh copies recorded since the last call (no-op when there are none). Every scene renderer calls
        // this right before it reserves its own retire value and submits, and makes its submission wait on the retire
        // timeline for the returned value (0 = nothing was ever uploaded). Waiting for an already reached value is free
        [[nodiscard]] u64 flush_uploads();

        // GPU frame statistics ----------------------------------------------------------------------------------------

        // Scene renderers report their resolved timestamp results here (one report per completed frame). The totals of
        // all reports between two begin_frame() calls become `last_gpu_stats()`
        void report_gpu_frame(const gpu_frame_report& report);
        [[nodiscard]] const gpu_frame_report& last_gpu_stats() const { return m_last_gpu_stats; }

        // stats -------------------------------------------------------------------------------------------------------

        [[nodiscard]] u32 live_buffer_count() const { return m_live_buffer_count; }
        [[nodiscard]] u32 live_pipeline_count() const { return m_live_pipeline_count; }
        [[nodiscard]] u32 live_descriptor_set_count() const { return m_live_descriptor_set_count; }

        // immediate submit --------------------------------------------------------------------------------------------

        void immediate_submit(std::function<void(vk::CommandBuffer)>&& fn);

    private:

        // Linear pool — one per (vertex, index, submesh, cluster). Handles range allocation from a free list, bump
        // allocation past the high water mark, coalescing, tail reclaim, growth and compaction. The buffer is device-local:
        // data reaches it through staging copies (stage()), growth and compaction are GPU copies into a new buffer
        struct linear_pool {

            util::allocated_buffer*                         buffer = nullptr;
            const char*                                     name = "";
            vk::BufferUsageFlags                            usage{};            // besides storage + transfer src / dst
            u32                                             element_size = 0;
            u32                                             capacity = 0;       // in elements
            u32                                             high_water = 0;     // bump pointer, in elements
            u32                                             live = 0;           // total live allocation, in elements
            u32                                             min_headroom = 0;   // min slack added on grow / compaction
            u32                                             generation = 0;     // bumped by compaction: older pending frees are void
            bool                                            growable = true;
            std::vector<free_range>                         free_ranges{};      // sorted by offset
        };

        // One host-visible block the CPU writes upload data into; the upload command buffer copies from it
        struct staging_block {

            util::allocated_buffer                          buffer{};
            std::byte*                                      mapped = nullptr;
            u64                                             capacity = 0;
            u64                                             offset = 0;         // bump pointer of the current recording
            u64                                             last_use = 0;       // retire value of the last submission that read it
            bool                                            in_recording = false;   // used by the open upload command buffer
            bool                                            dedicated = false;  // one large upload: destroyed after use
        };

        struct upload_command {

            vk::CommandBuffer                               cmd = nullptr;
            u64                                             value = 0;          // retire value of its last submission (0 = never)
        };

        struct mesh_alloc {

            u32                                             vertex_offset = 0;
            u32                                             index_offset = 0;
            u32                                             submesh_offset = 0;
            u32                                             cluster_offset = 0;
        };



        // lifecycle helpers -------------------------------------------------------------------------------------------

        void init_pools();
        void create_layouts();
        void destroy_layouts();
        void create_samplers();
        void destroy_samplers();
        void create_immediate_submit();
        void destroy_immediate_submit();
        void create_mesh_buffers();
        void destroy_mesh_buffers();
        void create_fallback_textures();
        void create_depth_only_fragment_module();
        void destroy_depth_only_fragment_module();
        void install_fallback_material();

        // mesh internal -----------------------------------------------------------------------------------------------

        [[nodiscard]] mesh_slot* find_slot(GLT::asset::handle handle);
        [[nodiscard]] const mesh_slot* find_slot(GLT::asset::handle handle) const;
        void upload_mesh_slice(mesh_slot& slot, const GLT::asset::mesh::mesh_asset& mesh);
        // [in_place]: the slot's table entry is already published (refresh): frames in flight may be reading it
        void pack_mesh_into_ssbo(mesh_slot& slot, const GLT::asset::mesh::mesh_asset& mesh, bool in_place);
        shared_mesh_id load_mesh_internal(GLT::asset::handle handle);

        // Source-agnostic upload: everything below the registry lookup. A mesh_asset from any source (asset file today,
        // procedural mesh store later) goes through these two
        [[nodiscard]] shared_mesh_id upload_new_slot(const GLT::asset::mesh::mesh_asset& mesh, GLT::asset::handle key);
        bool reupload_slot(shared_mesh_id id, const GLT::asset::mesh::mesh_asset& mesh);

        // Frees a slot's ranges once the GPU is done with them (never while frames in flight may still draw them)
        void retire_slot_ranges(const mesh_slot& slot);

        // Upload layout of a mesh asset: levels that fit, total indices ([indices ++ lod_indices]) and submesh table entries
        struct mesh_upload_layout {
            u32                                             vertex_count = 0;
            u32                                             index_count = 0;    // everything below, back to back
            u32                                             submesh_count = 0;  // per level
            u32                                             lod_count = 1;
            u32                                             cluster_count = 0;      // virtual geometry, 0 = not uploaded
            u32                                             cluster_index_base = 0; // cluster triangle lists start here in the index range
            u32                                             cluster_vertex_base = 0;    // cluster vertices start here in the vertex range ([vertices ++ lod_vertices ++ cluster_vertices])
            u32                                             cluster_vertex_count = 0;
        };
        [[nodiscard]] static mesh_upload_layout mesh_layout_of(const GLT::asset::mesh::mesh_asset& mesh);

        // Drops the parts of [layout] that are optional, in the order they are given up when the pools are full:
        // first the virtual geometry clusters, then the LOD levels. Returns false when nothing is left to drop
        [[nodiscard]] static bool reduce_layout(mesh_upload_layout& layout, const GLT::asset::mesh::mesh_asset& mesh);
        void write_mesh_cluster_entry(u32 mesh_index, const mesh_slot& slot, f32 error_scale, bool in_place);
        [[nodiscard]] static u64 geometry_signature(const GLT::asset::mesh::mesh_asset& mesh);
        [[nodiscard]] static std::array<f32, 8> lod_screen_sizes(const GLT::asset::mesh::mesh_asset& mesh, u32 lod_count);

        // texture internal --------------------------------------------------------------------------------------------

        bool create_gpu_texture(vk::Format format, u32 width, u32 height, const std::byte* pixels, u64 pixel_bytes,
            std::span<const texture_upload_mip> mips, bool generate_mips, gpu_texture& out);

        void destroy_gpu_texture(gpu_texture& texture);

        shared_texture_id acquire_texture_internal(GLT::asset::handle handle);
        void release_texture_internal(u32 slot);

        // material internal -------------------------------------------------------------------------------------------

        void pack_material(u32 slot_index);
        void destroy_material_shaders(material_slot& slot);
        // Shaders of a material whose stored bytecode is out of date, built again from its source (see rebuild_stale_material())
        struct stale_material_build {
            std::vector<std::byte>                                          bytecode{};             // empty: compile failed
            u32                                                             packed_param_size = 0;
            bool                                                            alpha_test = false;
        };
        [[nodiscard]] const stale_material_build* rebuild_stale_material(const GLT::asset::material::material_asset& asset);
        bool load_material_shaders(material_slot& slot, std::span<const std::byte> bytecode, bool alpha_test);
        vk::Pipeline create_material_pipeline(const material_slot& slot, material_pipeline_kind kind);
        void rebuild_material_pipelines(material_slot& slot);

        // shader compiler internal ------------------------------------------------------------------------------------

        void create_shader_compiler();
        void destroy_shader_compiler();

        // pool primitives ---------------------------------------------------------------------------------------------

        [[nodiscard]] bool pool_can_fit(const linear_pool& p, u32 count) const;
        std::optional<u32> pool_try_allocate(linear_pool& p, u32 count);
        void pool_free(linear_pool& p, u32 offset, u32 count);
        // pool_free from a retire callback: skipped when the pool was compacted since [generation] (the range is gone)
        void pool_free_deferred(linear_pool& p, u32 generation, u32 offset, u32 count);
        bool pool_resize(linear_pool& p, u32 new_capacity);
        [[nodiscard]] util::allocated_buffer create_pool_buffer(const linear_pool& p, u32 capacity);

        // compaction --------------------------------------------------------------------------------------------------

        [[nodiscard]] bool can_compact() const;
        [[nodiscard]] bool should_opportunistic_compact() const;
        void try_compact();

        // mesh tables (gpu_mesh_data + gpu_mesh_cluster_data, indexed by shared_mesh_id) --------------------------------

        // Grows both tables (doubling) so [mesh_index] fits. Returns false if the buffers could not be created
        bool ensure_mesh_table_capacity(u32 mesh_index);
        // CPU copies are the truth; dirty entries go to the GPU tables at flush time (one copy per contiguous run)
        void mark_mesh_table_dirty(u32 mesh_index, bool in_place);
        void emit_mesh_table_uploads();

        // uploads (shared_resource_manager_upload.cpp) ------------------------------------------------------------------

        void create_upload_context();
        void destroy_upload_context();
        // The upload command buffer of this frame, begun on first use
        [[nodiscard]] vk::CommandBuffer upload_cmd();
        // Reserves [size] bytes of staging memory and records their copy to [dst] at [dst_offset]. The caller writes the
        // data through the returned pointer before the next flush_uploads() (write-only: the memory may be uncached).
        // nullptr when [size] is 0 or no staging memory could be created
        [[nodiscard]] std::byte* stage(const util::allocated_buffer& dst, vk::DeviceSize dst_offset, vk::DeviceSize size);
        // Execution + memory dependency between copies in the upload command buffer (writes before, reads / writes after)
        void upload_transfer_barrier();
        // Before overwriting data that frames in flight may still read: waits for all earlier queue work (in-place writes)
        void upload_war_barrier();
        void trim_staging_blocks();
        // Which memory a mesh buffer ended up in (VRAM / BAR / system RAM), logged when it is created or replaced
        void log_buffer_memory(std::string_view name, const util::allocated_buffer& buffer) const;

        // mesh allocation transaction ---------------------------------------------------------------------------------

        std::optional<mesh_alloc> allocate_mesh_ranges(u32 v, u32 i, u32 s, u32 c = 0);



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
        util::allocated_buffer                                              m_submesh_data_buffer{};
        util::allocated_buffer                                              m_mesh_data_buffer{};
        util::allocated_buffer                                              m_cluster_data_buffer{};    // GLT::asset::mesh::cluster[], pool
        util::allocated_buffer                                              m_mesh_cluster_buffer{};    // gpu_mesh_cluster_data[m_mesh_table_capacity]

        // CPU copies of the two mesh tables (the GPU copies are device-local). Only entries of alive slots are meaningful
        u32                                                                 m_mesh_table_capacity = 0;
        std::vector<gpu_mesh_data>                                          m_mesh_table{};
        std::vector<gpu_mesh_cluster_data>                                  m_mesh_cluster_table{};
        std::vector<u32>                                                    m_mesh_table_dirty{};       // indices to upload at the next flush
        std::vector<u8>                                                     m_mesh_table_dirty_flag{};  // [capacity], dedups m_mesh_table_dirty
        std::vector<mesh_slot>                                              m_mesh_slots{};
        std::vector<u32>                                                    m_free_slots{};
        std::unordered_set<GLT::asset::handle>                              m_failed_mesh_loads{};
        std::unordered_map<GLT::asset::handle, shared_mesh_id>              m_mesh_id_by_asset{};

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
        vk::ShaderModule                                                    m_depth_only_fragment_module = nullptr;
        vk::Sampler                                                         m_material_sampler = nullptr;
        vk::Sampler                                                         m_shadow_sampler = nullptr;
        vk::Sampler                                                         m_depth_sampler = nullptr;

        // shader compiler ---------------------------------------------------------------------------------------------

        util::slang_compiler                                                m_slang{};
        u64                                                                 m_shader_compiler_hash = 0;
        std::unordered_map<u64, stale_material_build>                       m_stale_material_builds{};     // by hash of (source, blend)
        bool                                                                m_local_shadow_atlas = false;   // see local_shadow_atlas_supported()

        // stats -------------------------------------------------------------------------------------------------------

        u32                                                                 m_live_buffer_count = 0;
        u32                                                                 m_live_pipeline_count = 0;
        u32                                                                 m_live_descriptor_set_count = 0;

        // pools -------------------------------------------------------------------------------------------------------

        linear_pool                                                         m_vertex_pool{};
        linear_pool                                                         m_index_pool{};
        linear_pool                                                         m_submesh_pool{};
        linear_pool                                                         m_cluster_pool{};
        u64                                                                 m_frames_since_compact = 0;

        // procedural meshes -------------------------------------------------------------------------------------------

        struct procedural_entry {
            GLT::render::procedural_mesh_id                                 id = GLT::render::INVALID_PROCEDURAL_MESH;
            shared_mesh_id                                                  slot = INVALID_SHARED_MESH;
            u64                                                             version = 0;
        };
        void release_procedural_entry(u32 index);

        GLT::render::procedural_mesh_store::consumer_id                     m_procedural_consumer = 0;
        bool                                                                m_procedural_registered = false;
        std::vector<procedural_entry>                                       m_procedural_entries{};     // by store slot index
        std::unordered_map<u32, GLT::render::procedural_mesh_store::change> m_procedural_pending{};     // by store slot index, newest wins
        std::deque<u32>                                                     m_procedural_order{};       // FIFO of m_procedural_pending keys
        std::unordered_map<u32, u32>                                        m_procedural_attempts{};    // failed uploads of the pending version
        std::vector<GLT::render::procedural_mesh_store::change>             m_procedural_drain{};       // scratch

        // uploads -----------------------------------------------------------------------------------------------------

        vk::CommandPool                                                     m_upload_pool = nullptr;
        std::vector<upload_command>                                         m_upload_commands{};
        vk::CommandBuffer                                                   m_upload_recording = nullptr;   // open upload command buffer, if any
        size_t                                                              m_upload_recording_index = 0;
        std::vector<staging_block>                                          m_staging_blocks{};
        i32                                                                 m_staging_current = -1;         // block being filled, -1 = none
        std::vector<std::function<void()>>                                  m_upload_deferred_retires{};    // retire() while recording
        bool                                                                m_upload_war_pending = false;   // in-place table writes in this recording
        u64                                                                 m_last_upload_value = 0;        // retire value of the newest upload submission
        u64                                                                 m_upload_bytes_this_frame = 0;  // profiler plot

        // deferred destruction + GPU stats ----------------------------------------------------------------------------

        util::retire_queue                                                  m_retire{};
        gpu_frame_report                                                    m_accum_gpu_stats{};
        gpu_frame_report                                                    m_last_gpu_stats{};

    };

}
