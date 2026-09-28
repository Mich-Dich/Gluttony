
#pragma once

#include "asset/type.h"
#include "asset/header.h"
#include "asset/texture.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::material {

    // CONSTANTS =======================================================================================================

    inline constexpr chunk_id                   CHUNK_MATERIAL_PARAMS = 0x0300;

    inline constexpr chunk_id                   CHUNK_TEXTURE_REFS = 0x0301;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    enum class texture_slot : u8 {

        base_color = 0,
        metallic_roughness,
        normal,
        emissive,
        occlusion,
        height,

        count
    };


    struct material_params {

        glm::vec4                               base_color{ 1.f };
        glm::vec3                               emissive{ 0.f };
        f32                                     roughness = .5f;
        f32                                     metallic = 0.f;
        f32                                     reflectance = .5f;
        f32                                     normal_scale = 1.f;
        f32                                     occlusion_strength = 1.f;
        u32                                     flags{0};                   // double-sided, alpha-test, etc.
    };
    static_assert(sizeof(material_params) == 52);
    static_assert(std::is_trivially_copyable_v<material_params>);


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

    class material_asset final : public GLT::asset::i_runtime_asset {
    public:

        FORCE_INLINE_R GLT::asset::type type() const noexcept override { return asset_type; }

        FORCE_INLINE_R u64 memory_usage() const noexcept override { return sizeof(*this); }


        GLT::asset::type                                                                asset_type{ GLT::asset::core_types::material };
        material_params                                                                 params{};
        std::array<GLT::asset::handle, static_cast<size_t>(texture_slot::count)>        textures{};
        std::string                                                                     name{};

    };

}
