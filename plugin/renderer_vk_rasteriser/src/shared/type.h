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

    // mesh capacity ---------------------------------------------------------------------------------------------------

    static constexpr u32                                            MAX_MESHES = 4096;

    static constexpr u32                                            MAX_SUBMESHES_PER_MESH = 32;

    static constexpr u32                                            MAX_SUBMESHES = MAX_MESHES * MAX_SUBMESHES_PER_MESH;

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

    static constexpr u32                                            VK_MATERIAL_CODEGEN_REVISION = 1;

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

    // A single resident mesh slot inside the shared storage
    struct mesh_slot {

        GLT::asset::handle                                          asset{};
        u32                                                         vertex_offset = 0;   // in vertices
        u32                                                         vertex_count = 0;
        u32                                                         index_offset = 0;   // in indices
        u32                                                         index_count = 0;
        bool                                                        alive = false;
        std::vector<u32>                                            material_slots{};    // per submesh, resolved material slot
        glm::vec3                                                   local_min{0.0f};
        glm::vec3                                                   local_max{0.0f};
        u32                                                         ref_count = 0; 
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

    // Resident material slot
    struct material_slot {

        GLT::asset::handle                                          asset{};
        u32                                                         ref_count = 0;

        std::array<GLT::asset::handle, MATERIAL_TEXTURE_SLOTS>      texture_assets{};
        std::array<u32, MATERIAL_TEXTURE_SLOTS>                     texture_slots{};

        bool                                                        alive = false;

        util::allocated_buffer                                      params_ubo{};
        u32                                                         params_size{0};

        vk::ShaderModule                                            vertex_module = nullptr;
        vk::ShaderModule                                            fragment_module = nullptr;
        vk::ShaderModule                                            depth_module = nullptr;
        vk::Pipeline                                                pipeline = nullptr;
        vk::Pipeline                                                depth_pipeline = nullptr;

        GLT::asset::material::blend_mode                            blend{ GLT::asset::material::blend_mode::opaque };
        u64                                                         bytecode_hash = 0;
    };

    // Persistent per-mesh SSBO entry (read by the cull compute shader)
    struct gpu_mesh_data {

        glm::vec3                                                   local_min{0.0f};
        u32                                                         first_submesh = 0;
        glm::vec3                                                   local_max{0.0f};
        u32                                                         submesh_count = 0;
        u32                                                         vertex_offset = 0;
        u32                                                         index_offset = 0;
        u32                                                         _pad0 = 0;
        u32                                                         _pad1 = 0;
    };
    static_assert(sizeof(gpu_mesh_data) == 48, "gpu_mesh_data layout mismatch");

    // Persistent per-submesh SSBO entry
    struct gpu_submesh_data {

        u32                                                         first_index = 0;
        u32                                                         index_count = 0;
        u32                                                         material_index = 0;     // reserved (dead)
        u32                                                         _pad0 = 0;
    };
    static_assert(sizeof(gpu_submesh_data) == 16, "gpu_submesh_data layout mismatch");

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
