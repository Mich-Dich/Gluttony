#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::material {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // @brief Byte size of one element of a parameter type
    constexpr u32 param_type_size(param_type type) noexcept {

        switch (type) {
            case param_type::f32:       return 4;
            case param_type::f32x2:     return 8;
            case param_type::f32x3:     return 12;
            case param_type::f32x4:     return 16;
            case param_type::i32:       return 4;
            case param_type::u32:       return 4;
            case param_type::boolean:   return 4;
            case param_type::f32x2x2:   return 16;
            case param_type::f32x3x3:   return 36;
            case param_type::f32x4x4:   return 64;
            default:                    return 0;
        }
    }


    // @brief Natural alignment of one element of a parameter type
    constexpr u32 param_type_alignment(param_type type) noexcept {

        switch (type) {
            case param_type::f32:       return 4;
            case param_type::f32x2:     return 8;
            case param_type::f32x3:     return 4;   // vec3 packs at 4, sizeof 12
            case param_type::f32x4:     return 16;
            case param_type::i32:       return 4;
            case param_type::u32:       return 4;
            case param_type::boolean:   return 4;
            case param_type::f32x2x2:   return 8;
            case param_type::f32x3x3:   return 16;  // std140 column alignment
            case param_type::f32x4x4:   return 16;
            default:                    return 4;
        }
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // material_asset --------------------------------------------------------------------------------------------------

    FORCE_INLINE_R GLT::asset::type material_asset::type() const noexcept { return asset_type; }


    FORCE_INLINE_R u64 material_asset::memory_usage() const noexcept {

        return sizeof(*this)
            + slang_source.capacity()
            + bytecode.capacity()
            + reflection.capacity()
            + packed_params.capacity()
            + name.capacity();
    }

    // material_instance_asset -----------------------------------------------------------------------------------------

    FORCE_INLINE_R GLT::asset::type material_instance_asset::type() const noexcept { return asset_type; }


    FORCE_INLINE_R u64 material_instance_asset::memory_usage() const noexcept {

        u64 bytes = sizeof(*this) + name.capacity();
        for (const auto& o : overrides)
            bytes += sizeof(o) + o.value.capacity();
        return bytes;
    }
    
    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
