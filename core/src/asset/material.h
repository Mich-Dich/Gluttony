
#pragma once

#include "asset/type.h"
#include "asset/header.h"
#include "asset/texture.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::material {

    // CONSTANTS =======================================================================================================

    // Chunk IDs for the material_asset format
    inline constexpr chunk_id                   CHUNK_MATERIAL_PARAMS = 0x0300;

    inline constexpr chunk_id                   CHUNK_TEXTURE_REFS = 0x0301;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief Named texture channels a material can reference
    //
    // `count` is the sentinel, used to size the texture handle array on the runtime asset
    // Values are positional: [material_asset::textures[slot]] holds the handle for that channel
    enum class texture_slot : u8 {

        base_color = 0,
        metallic_roughness,
        normal,
        emissive,
        occlusion,
        height,

        count
    };


    // @brief Scalar / vector parameters of a material
    //
    // Kept trivially copyable so the file format can [memcpy] it in and out
    struct material_params {

        glm::vec4                               base_color{ 1.f };
        glm::vec3                               emissive{ 0.f };
        f32                                     roughness = .5f;
        f32                                     metallic = 0.f;
        f32                                     reflectance = .5f;
        f32                                     normal_scale = 1.f;
        f32                                     occlusion_strength = 1.f;
        u32                                     flags{0};                   // double-sided, alpha-test, etc
    };
    static_assert(sizeof(material_params) == 52);
    static_assert(std::is_trivially_copyable_v<material_params>);


    // @brief Serialized reference to a texture asset
    //
    // [slot] says which material channel the referenced texture belongs to; [id] is the target asset's UUID
    // [_pad] keeps the struct 16 bytes so an array of these stays trivially copyable and alignas-clean
    struct texture_ref {

        texture_slot                            slot;
        u8                                      _pad[7];
        UUID                                    id{};                       // asset UUID of the texture
    };
    static_assert(sizeof(texture_ref) == 16);
    static_assert(std::is_trivially_copyable_v<texture_ref>);

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Runtime representation of a loaded material asset
    //
    // [params] and [textures] are the two pieces of state a renderer needs to shade with this material
    // [textures] is indexed by [texture_slot]; a missing slot is stored as INVALID_HANDLE
    class material_asset final : public GLT::asset::i_runtime_asset {
    public:

        // @brief Returns the asset-type tag used by the registry
        FORCE_INLINE_R GLT::asset::type type() const noexcept override { return asset_type; }

        // @brief Approximate resident bytes, used by the profiler
        FORCE_INLINE_R u64 memory_usage() const noexcept override { return sizeof(*this); }


        GLT::asset::type                                                                asset_type{ GLT::asset::core_types::material };
        material_params                                                                 params{};
        std::array<GLT::asset::handle, static_cast<size_t>(texture_slot::count)>        textures{};
        std::string                                                                     name{};

    };

}
