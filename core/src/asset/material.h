
#pragma once

#include "asset/type.h"
#include "asset/header.h"
#include "asset/texture.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::material {

    // CONSTANTS =======================================================================================================

    inline constexpr u32                        MATERIAL_CONTRACT_VERSION = 0;

    inline constexpr u32                        REFLECT_SCHEMA_MAGIC = 0x30464C54;   // "TLF0"

    inline constexpr u32                        REFLECT_SCHEMA_VERSION = 1;

    // Chunk IDs for the material_asset format
    inline constexpr chunk_id                   CHUNK_MATERIAL_PARAMS = 0x0300;

    inline constexpr chunk_id                   CHUNK_MATERIAL_REFLECT = 0x0304;

    inline constexpr chunk_id                   CHUNK_TEXTURE_REFS = 0x0301;

    // Bump when the binary layout below changes. Independent of MATERIAL_CONTRACT_VERSION.
    inline constexpr u16                        REFLECTION_SCHEMA_VERSION = 1;

    // Chunk ids for the v0 contract. 0x0300 and 0x0301 exist already.
    inline constexpr chunk_id                   CHUNK_MATERIAL_SLANG_SRC = 0x0302;

    inline constexpr chunk_id                   CHUNK_MATERIAL_BYTECODE = 0x0303;

    inline constexpr chunk_id                   CHUNK_MATERIAL_BYTECODE_HASH = 0x0307;

    inline constexpr chunk_id                   CHUNK_MATERIAL_COMPILER_HASH = 0x0305;

    inline constexpr chunk_id                   CHUNK_MATERIAL_BLEND_MODE = 0x0306;

    inline constexpr chunk_id                   CHUNK_INSTANCE_BASE = 0x0310;

    inline constexpr chunk_id                   CHUNK_INSTANCE_OVERRIDES = 0x0311;

    namespace material_flag {

        inline constexpr u32                    double_sided            = BIT(0);

        inline constexpr u32                    cast_dynamic_shadow     = BIT(1);

        inline constexpr u32                    cast_ray_traced_shadow  = BIT(2);

        inline constexpr u32                    wireframe               = BIT(3);

        inline constexpr u32                    use_opacity_mask        = BIT(4);   // required for blend_mode::masked

        inline constexpr u32                    responsive_aa           = BIT(5);

        inline constexpr u32                    disable_depth_test      = BIT(6);

        inline constexpr u32                    output_velocity         = BIT(7);

        inline constexpr u32                    use_lightmap            = BIT(8);

    }

    constexpr const char*                       DEFAULT_SLANG_SOURCE = R"slang(
import material_contract_v0;


struct material_params {
    float3  		base_color = float3(0.8, 0.8, 0.8);
    float   		roughness = 0.5;
};

surface_output surface_evaluate(surface_input s, material_params p, material_textures tex) {
    surface_output o = make_default_surface(s);
    o.base_color = p.base_color;
    o.roughness = p.roughness;
    return o;
}
)slang";

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


    // @brief How the surface is blended into the framebuffer
    //  Mirrors Unreal's blend modes; keep `opaque = 0` so old files stay valid
    enum class blend_mode : u8 {

        opaque = 0,
        masked,                 // requires material_flag::use_opacity_mask
        translucent,
        additive,
        modulate,
        alpha_composite,
        alpha_holdout,

        count
    };


    // @brief Which lighting model the renderer should evaluate
    enum class shading_model : u8 {

        unlit = 0,
        default_lit,            // the "normal" PBR path
        subsurface,
        preintegrated_skin,
        clear_coat,             // uses clear_coat / clear_coat_roughness
        subsurface_profile,
        two_sided_foliage,
        hair,
        cloth,
        eye,
        single_layer_water,
        thin_translucent,

        count
    };


    // @brief Tessellation / displacement strategy
    enum class tessellation_mode : u8 {

        none = 0,
        flat,                   // uniform subdivision (uses tessellation_multiplier)
        pn_triangles,           // smooth curved subdivision

        count
    };


        // One bit per entry point declared by the material.
    enum entry_bits : u32 {

        none                        = 0,
        surface_evaluate            = BIT(0),   // mandatory for every material
        vertex_transform            = BIT(1),   // optional
        alpha_test                  = BIT(2),   // required iff blend_mode == masked
    };


    // Scalar / vector / matrix types allowed on material_params fields. Matches the
    // v0 contract restriction list in §material_params of the design doc.
    enum class param_type : u16 {

        f32 = 0,
        f32x2,
        f32x3,
        f32x4,
        i32,
        u32,
        boolean,
        f32x2x2,
        f32x3x3,
        f32x4x4,

        count
    };


    // @brief One parameter of material_params
    struct reflection_param {

        u64                                     name_hash;                      // XXH3-64 / FNV-1a-64 of the field name
        u32                                     name_offset;                    // into string table (NUL-terminated)
        param_type                              type;
        u32                                     byte_offset;                    // within packed params blob
        u32                                     byte_size;                      // sizeof of one element
        u32                                     array_count;                    // 1 for scalars; v0 forbids arrays
        u32                                     _pad;
    };
    static_assert(sizeof(reflection_param) == 32);
    static_assert(std::is_trivially_copyable_v<reflection_param>);


    struct reflection_header {

        u32                                     magic;                          // REFLECT_SCHEMA_MAGIC
        u16                                     schema_version;                 // REFLECTION_SCHEMA_VERSION
        u16                                     contract_version;               // MATERIAL_CONTRACT_VERSION (0 for v0)
        u32                                     entry_flags;                    // entry_point_flags
        u32                                     param_count;                    // number of reflection_param entries
        u32                                     param_table_offset;             // bytes from start of blob
        u32                                     string_table_offset;            // bytes from start of blob
        u32                                     string_table_size;              // bytes
        u32                                     packed_param_size;              // = byte length of CHUNK_MATERIAL_PARAMS
        u32                                     _pad[2];
    };
    static_assert(sizeof(reflection_header) == 40);
    static_assert(std::is_trivially_copyable_v<reflection_header>);


    // @brief One override row of a material instance asset.
    struct instance_override {
        u64                                     name_hash{};
        param_type                              type{ param_type::f32 };
        u32                                     byte_size{};
        std::vector<std::byte>                  value;
    };


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

    // @brief Byte size of one element of a parameter type
    FORCE_INLINE_R constexpr u32 param_type_size(param_type type) noexcept;


    // @brief Natural alignment of one element of a parameter type
    FORCE_INLINE_R constexpr u32 param_type_alignment(param_type type) noexcept;

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Runtime representation of a loaded material asset
    //
    // No fixed parameter layout: the contract requires the material's Slang source to declare its own material_params struct,
    // and everything in this class that is parameter-shaped (reflection, packed values) is an opaque blob whose schema is
    // carried alongside it
    class material_asset final : public GLT::asset::i_runtime_asset {
    public:

        material_asset();


        FORCE_INLINE_R GLT::asset::type type() const noexcept override;


        FORCE_INLINE_R u64 memory_usage() const noexcept override;


        // -------- reflection decode --------

        struct decoded_reflection {
            const reflection_header*                                                header = nullptr;
            std::span<const reflection_param>                                       params{};
            std::string_view                                                        string_table{};
        };

        [[nodiscard]] decoded_reflection decode_reflection() const noexcept;

        // Lookup by name hash. Returns nullptr if the param is not in the schema
        [[nodiscard]] const reflection_param* find_param(u64 name_hash) const noexcept;

        // Bytes of one parameter inside `packed_params`
        [[nodiscard]] std::span<const std::byte> param_bytes(const reflection_param& param) const noexcept;


        GLT::asset::type                                                            asset_type{ GLT::asset::core_types::material };

        // contract chunks, verbatim
        std::string                                                                 slang_source;       // NUL stripped on load
        std::vector<std::byte>                                                      bytecode{};
        u64                                                                         bytecode_hash = 0;
        std::vector<std::byte>                                                      reflection{};       // reflect_header + params + string table
        std::vector<std::byte>                                                      packed_params{};    // laid out per `reflection`
        u64                                                                         compiler_hash{ 0 };
        blend_mode                                                                  blend{ blend_mode::opaque };

        // resolved texture handles --------
    
        std::array<GLT::asset::handle, static_cast<size_t>(texture_slot::count)>    textures{};

        // runtime-only bookkeeping --------

        // false => recompile scheduled
        bool                                                                        compiler_hash_matches{ true };
        std::string                                                                 name{};
    };


    class material_instance_asset final : public GLT::asset::i_runtime_asset {
    public:


        FORCE_INLINE_R GLT::asset::type type() const noexcept override;


        FORCE_INLINE_R u64 memory_usage() const noexcept override;


        GLT::asset::type                                asset_type{ GLT::asset::core_types::material_instance };
        GLT::asset::handle                              base_material{ INVALID_HANDLE };
        UUID                                            base_material_id{};     // for id-only re-resolution on cold load
        std::vector<instance_override>                  overrides{};
        std::string                                     name{};
    };

}

#include "material.inl"
