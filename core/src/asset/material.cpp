
#include "util/pch.h"
#include "material.h"


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

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    material_asset::material_asset() {

        slang_source = DEFAULT_SLANG_SOURCE;
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    inline material_asset::decoded_reflection material_asset::decode_reflection() const noexcept {

        decoded_reflection reflect{};
        if (reflection.size() < sizeof(reflection_header))
            return reflect;

        reflect.header = reinterpret_cast<const reflection_header*>(reflection.data());
        if (reflect.header->magic != REFLECT_SCHEMA_MAGIC)
            return reflect;

        const auto* params = reinterpret_cast<const reflection_param*>(reflection.data() + reflect.header->param_table_offset);
        reflect.params = { params, reflect.header->param_count };

        const auto* strtab = reinterpret_cast<const char*>(reflection.data() + reflect.header->string_table_offset);
        reflect.string_table = { strtab, reflect.header->string_table_size };

        return reflect;
    }


    const reflection_param* material_asset::find_param(u64 name_hash) const noexcept {

        const auto reflect = decode_reflection();
        if (!reflect.header)
            return nullptr;

        for (const auto& param : reflect.params)
            if (param.name_hash == name_hash)
                return &param;

        return nullptr;
    }


    std::span<const std::byte> material_asset::param_bytes(const reflection_param& param) const noexcept {

        // Guard against a malformed reflection row pointing outside the packed blob
        if (param.byte_offset > packed_params.size()
            || param.byte_size > packed_params.size() - param.byte_offset) {
            return {};
        }

        return { packed_params.data() + param.byte_offset, param.byte_size };
    }

    
    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
