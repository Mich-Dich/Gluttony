#pragma once

#include "util/pch.h"
#include "util/buffer.h"
#include "asset/type.h"
#include "asset/material.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    using shared_mesh_id = u32;

    using shared_texture_id = u32;

    // CONSTANTS =======================================================================================================

    // shared buffer growth --------------------------------------------------------------------------------------------

    static constexpr u32                                            VERTEX_HEADROOM_MIN = 64 * 1024;

    static constexpr u32                                            INDEX_HEADROOM_MIN = 128 * 1024;

    // Virtual geometry: records of the cluster pool (one per geometry cluster of every resident mesh that has them)
    static constexpr u32                                            CLUSTER_HEADROOM_MIN = 16 * 1024;

    // Submesh table entries (gpu_submesh_data, one per submesh and LOD level of every resident mesh)
    static constexpr u32                                            SUBMESH_HEADROOM_MIN = 16 * 1024;

    // mesh capacity ---------------------------------------------------------------------------------------------------

    // The mesh table (gpu_mesh_data) and the virtual geometry table (gpu_mesh_cluster_data) start with this many entries
    // and double when a slot index does not fit. Both are indexed by shared_mesh_id
    static constexpr u32                                            INITIAL_MESH_TABLE_CAPACITY = 4096;

    // Sanity limit only, nothing is sized by it (same bound as the instance index in firstInstance)
    static constexpr u32                                            MESH_SLOT_LIMIT = 1u << 20;

    static constexpr u32                                            MAX_SUBMESHES_PER_MESH = 32;

    // mesh uploads (shared_resource_manager_upload.cpp) ---------------------------------------------------------------

    // Mesh data goes to device-local (VRAM) buffers through host-visible staging blocks of this size. An upload larger
    // than half a block gets a staging buffer of its own
    static constexpr u64                                            STAGING_BLOCK_SIZE = 16ull * 1024 * 1024;

    // Free staging blocks kept around for the next uploads; more are released once the GPU is done with them
    static constexpr u32                                            STAGING_MAX_CACHED_BLOCKS = 4;

    // Procedural mesh versions uploaded per frame at most (bytes of geometry). A burst (world load, many regenerated
    // chunks) is spread over several frames; meshes waiting for their turn keep drawing their previous version. At least
    // one mesh is uploaded per frame, however large
    static constexpr u64                                            PROCEDURAL_UPLOAD_BUDGET = 32ull * 1024 * 1024;

    // A version whose upload fails (pools cannot grow) is retried on later frames this many times, then dropped
    static constexpr u32                                            PROCEDURAL_UPLOAD_ATTEMPTS = 3;

    // gpu_mesh_data::flags
    static constexpr u32                                            MESH_FLAG_PROCEDURAL = 1u << 0;    // debug colouring (INSTANCE_PROCEDURAL)

    // material table --------------------------------------------------------------------------------------------------

    static constexpr u32                                            MAX_MATERIALS = 1024;

    static constexpr u32                                            DEFAULT_MATERIAL_SLOT = 0;

    static constexpr u32                                            MATERIAL_POLL_INTERVAL = 30;

    static constexpr u32                                            MATERIAL_TEXTURE_SLOTS = static_cast<u32>(GLT::asset::material::texture_slot::count);

    // bindless texture slots ------------------------------------------------------------------------------------------

    static constexpr u32                                            TEXTURE_SLOT_WHITE = 0;

    static constexpr u32                                            TEXTURE_SLOT_FLAT_NORMAL = 1;

    static constexpr u32                                            FIRST_FREE_TEXTURE_SLOT = 2;

    // codegen revision (bump to invalidate bytecode) ------------------------------------------------------------------

    // Bump whenever shading_strings.h changes: it is mixed into the implementation hash, so materials recompile
    // rev 2: HDR output (linear radiance, tonemapping moved to a post pass), hemispherical sky ambient
    // rev 3: SV_IsFrontFace normal flip (double-sided materials)
    // rev 5: local shadow atlas projection + clip distances in the vertex shader, instance index without SV_InstanceID
    // rev 6: shadow view in firstInstance bits 20..30 (up to 1536 views), single-tap sampling of small shadow tiles
    // rev 7: sun shadows from cascades (shadow views), 3 x 3 tent filter with four taps
    static constexpr u32                                            VK_MATERIAL_CODEGEN_REVISION = 7;

    // Lit scene colour target. Material "main" pipelines are created against this format
    inline constexpr vk::Format                                     HDR_COLOR_FORMAT = vk::Format::eR16G16B16A16Sfloat;

    inline constexpr shared_mesh_id                                 INVALID_SHARED_MESH = 0xFFFFFFFFu;

    inline constexpr shared_texture_id                              INVALID_SHARED_TEXTURE = 0xFFFFFFFFu;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Discriminates the pipelines a material carries
    enum class material_pipeline_kind : u8 {

        main = 0,
        depth,
        preview,
    };


    // @brief Error type for `ensure_*` calls
    struct upload_error {

        enum class code : u32 {
            none = 0,
            not_found,
            decode_failed,
            unsupported,
            backend,
            already_exists,
        } kind = code::none;

        std::string                                                 message{};

        static upload_error make(code k, std::string msg) {
            return { k, std::move(msg) };
        }
    };


    // Free-range entry for the linear pools. Sorted by offset, coalesced on free.
    struct free_range {
        u32                                                         offset = 0;
        u32                                                         size = 0;
    };


    // A single resident mesh slot inside the shared storage.
    struct mesh_slot {

        GLT::asset::handle                                          asset{};
        u32                                                         vertex_offset = 0;   // in vertices
        u32                                                         vertex_count = 0;
        u32                                                         index_offset = 0;   // in indices
        u32                                                         index_count = 0;
        u32                                                         submesh_offset = 0;  // in gpu_submesh_data
        u32                                                         submesh_count = 0;   // per LOD level
        u32                                                         lod_count = 1;       // levels uploaded (>= 1)
        bool                                                        alive = false;
        std::vector<u32>                                            material_slots{};
        // [lod * submesh_count + submesh]: {first_index, index_count} into this slot's index range (CPU copy for direct
        // draws). The index range holds [indices ++ lod_indices] of the asset, the GPU submesh table the same layout
        std::vector<glm::uvec2>                                     submesh_ranges{};
        const void*                                                 source = nullptr;   // mesh_asset uploaded from (change detection only)
        u32                                                         source_revision = 0;
        u64                                                         source_geometry = 0; // hash of the geometry arrays' data pointers + sizes
        std::array<f32, 8>                                          lod_screen{};       // CPU copy of gpu_mesh_data::lod_screen (translucents)

        // Virtual geometry (asset/mesh_cluster.h). cluster_count == 0: the mesh has no clusters or does not use them, and
        // renders with the levels above only. The cluster triangle lists sit at the end of this slot's index range
        // ([indices ++ lod_indices ++ cluster_indices]), the cluster records in the cluster pool
        u32                                                         cluster_offset = 0;     // in cluster records
        u32                                                         cluster_count = 0;
        u32                                                         cluster_index_base = 0; // first cluster index, relative to index_offset
        u32                                                         cluster_vertex_base = 0;    // first cluster vertex (recomputed normals), relative to vertex_offset
        u32                                                         cluster_vertex_count = 0;
        std::vector<u32>                                            cluster_max_visible{};  // per submesh: most cluster draws at once
        glm::vec3                                                   local_min{0.0f};
        glm::vec3                                                   local_max{0.0f};
        u32                                                         ref_count = 0;
        // Uploaded from the procedural mesh store (a version of render::procedural_mesh_id [procedural_id]), not from an
        // asset. Owned by the shared manager, never ref-counted by scene renderers
        bool                                                        procedural = false;
        u32                                                         procedural_id = 0;
    };


    // One upload-level mip slice
    struct texture_upload_mip {

        u64                                                         offset = 0;         // into the staging pixels
        u32                                                         width = 0;
        u32                                                         height = 0;
    };

    // Bindless texture slot
    struct gpu_texture {

        util::allocated_image                                       image{};
        vk::ImageView                                               view = nullptr;
        GLT::asset::handle                                          asset{};
        u32                                                         ref_count = 0;
        bool                                                        alive = false;
    };

    // GPU timing of one completed frame (or the sum of several scene renderers' frames)
    struct gpu_frame_report {

        f32                                                         gpu_time_ms = 0.0f;
        u32                                                         render_passes = 0;
        u32                                                         draw_calls = 0;     // visible draws emitted by GPU culling (main view)
        u32                                                         triangles = 0;      // triangles of those draws
        u32                                                         virtual_clusters = 0;   // of the draws: virtual geometry clusters
        u32                                                         virtual_triangles = 0;  // of the triangles: in those clusters
        std::vector<std::pair<std::string, f32>>                    pass_times_ms{};
    };

    // Resident material slot
    struct material_slot {

        GLT::asset::handle                                          asset{};
        u32                                                         ref_count = 0;

        std::array<GLT::asset::handle, MATERIAL_TEXTURE_SLOTS>      texture_assets{};
        std::array<u32, MATERIAL_TEXTURE_SLOTS>                     texture_slots{};

        bool                                                        alive = false;

        util::allocated_buffer                                      params_ubo{};
        u32                                                         params_size{0};
        u64                                                         params_hash = 0;    // of the uploaded packed params (0 = none)

        vk::ShaderModule                                            vertex_module = nullptr;
        vk::ShaderModule                                            fragment_module = nullptr;
        vk::ShaderModule                                            depth_module = nullptr;

        // True: the depth-only fragment shader of this bytecode runs the material's alpha_test (masked). False: it is the
        // no-op variant, and the depth pipeline uses the manager's empty fragment shader instead of [depth_module]
        bool                                                        depth_alpha_test = false;

        vk::Pipeline                                                pipeline = nullptr;
        vk::Pipeline                                                depth_pipeline = nullptr;

        GLT::asset::material::blend_mode                            blend{ GLT::asset::material::blend_mode::opaque };
        u32                                                         flags = 0;          // material_flag bits
        u64                                                         bytecode_hash = 0;

        // Blend + flags the current pipelines were built with (pack_material rebuilds them when the asset changes)
        GLT::asset::material::blend_mode                            pipeline_blend{ GLT::asset::material::blend_mode::opaque };
        u32                                                         pipeline_flags = 0;

        // Translucent modes are drawn in the sorted translucent pass instead of the GPU-culled opaque path
        [[nodiscard]] bool is_translucent() const noexcept {
            using bm = GLT::asset::material::blend_mode;
            return blend != bm::opaque && blend != bm::masked;
        }
        [[nodiscard]] bool is_double_sided() const noexcept {
            return (flags & GLT::asset::material::material_flag::double_sided) != 0;
        }
    };

    // Persistent per-mesh SSBO entry (read by the cull compute shader)
    struct gpu_mesh_data {

        glm::vec3                                                   local_min{0.0f};
        u32                                                         first_submesh = 0;
        glm::vec3                                                   local_max{0.0f};
        u32                                                         submesh_count = 0;
        u32                                                         vertex_offset = 0;
        u32                                                         index_offset = 0;
        u32                                                         lod_count = 1;          // submesh table holds [lod][submesh_count]
        u32                                                         flags = 0;              // MESH_FLAG_* bits
        // [0] = cull screen size (0 = never), [1..7] = screen size below which LOD n takes over (asset lod_settings)
        f32                                                         lod_screen[8]{};
    };
    static_assert(sizeof(gpu_mesh_data) == 80, "gpu_mesh_data layout mismatch");

    // Persistent per-submesh SSBO entry
    struct gpu_submesh_data {

        u32                                                         first_index = 0;
        u32                                                         index_count = 0;
        u32                                                         material_index = 0;     // reserved (dead)
        u32                                                         _pad0 = 0;
    };
    static_assert(sizeof(gpu_submesh_data) == 16, "gpu_submesh_data layout mismatch");

    // Persistent per-mesh entry of the virtual geometry table (same index as gpu_mesh_data). Layout matches
    // MeshClusterData in virtual_geometry_cull.comp.glsl. The cluster records themselves are GLT::asset::mesh::cluster,
    // uploaded unchanged
    struct gpu_mesh_cluster_data {

        u32                                                         first_cluster = 0;      // into the cluster buffer
        u32                                                         cluster_count = 0;      // 0 = mesh without virtual geometry
        u32                                                         index_base = 0;         // + gpu_mesh_data::index_offset + cluster::first_index
        f32                                                         error_scale = 1.0f;     // asset cluster_settings::error_scale
    };
    static_assert(sizeof(gpu_mesh_cluster_data) == 16, "gpu_mesh_cluster_data layout mismatch");

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
