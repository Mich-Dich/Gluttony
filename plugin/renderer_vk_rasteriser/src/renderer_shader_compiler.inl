
#pragma once

#include <slang.h>

#include <asset/material.h>

#include "util/slang_compiler.h"
#include "renderer_shading_strings.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // @brief Convert a Slang scalar type to a contract param_type. Returns nullopt for anything the v0 contract forbids
    static std::optional<GLT::asset::material::param_type> slang_scalar_to_param_type(slang::TypeReflection::ScalarType scalar);


    // @brief Convert a Slang type reflection to a contract param_type. Returns nullopt for anything the v0 contract forbids
    // (non-float vectors/matrices, arrays, nested structs)
    static std::optional<GLT::asset::material::param_type> slang_type_to_param_type(slang::TypeReflection* type);


    // @brief Walks a linked Slang component's `material_params` struct and emits the
    //        [reflection_header][reflection_param[]][string table] blob.
    //
    // Layout contract (must match material_asset::decode_reflection):
    //   - magic == REFLECT_SCHEMA_MAGIC
    //   - schema_version == REFLECT_SCHEMA_VERSION
    //   - contract_version == MATERIAL_CONTRACT_VERSION
    //   - byte_offset laid out per param_type_alignment (Slang's "scalar" layout)
    //   - name_hash == GLT::util::fnv1a_64(name)
    static std::expected<std::vector<std::byte>, GLT::render::shader_compile_error> build_reflection_schema(slang::IComponentType* linked,
        u32 entry_flags);


    // @brief Reads each field's default value from the material source and writes it into
    //        the packed params blob at the offset given by the reflection schema.
    //
    // NOTE(Slang SDK): the pinned Slang reflection API does not expose field default
    // initializers through a stable C++ accessor. Until that changes (or we add a
    // generated "defaults struct" compile pass), we emit zero-initialized bytes and log
    // a warning. Materials that rely on non-zero defaults must have them written through
    // the editor on first save.
    static std::expected<std::vector<std::byte>, GLT::render::shader_compile_error> pack_defaults_from_linked(slang::IComponentType* linked,
        std::span<const std::byte> reflection_blob);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static std::optional<GLT::asset::material::param_type> slang_scalar_to_param_type(slang::TypeReflection::ScalarType scalar) {

        using PT = GLT::asset::material::param_type;
        using S = slang::TypeReflection::ScalarType;

        switch (scalar) {
            case S::Float32: return PT::f32;
            case S::Int32:   return PT::i32;
            case S::UInt32:  return PT::u32;
            case S::Bool:    return PT::boolean;
            default:         return std::nullopt;
        }
    }


    static std::optional<GLT::asset::material::param_type> slang_type_to_param_type(slang::TypeReflection* type) {

        using PT = GLT::asset::material::param_type;
        using K = slang::TypeReflection::Kind;

        if (!type)
            return std::nullopt;

        switch (type->getKind()) {

            case K::Scalar:
                return slang_scalar_to_param_type(type->getScalarType());

            case K::Vector: {
                if (type->getScalarType() != slang::TypeReflection::ScalarType::Float32)
                    return std::nullopt;
                switch (type->getElementCount()) {
                    case 2: return PT::f32x2;
                    case 3: return PT::f32x3;
                    case 4: return PT::f32x4;
                    default: return std::nullopt;
                }
            }

            case K::Matrix: {
                if (type->getScalarType() != slang::TypeReflection::ScalarType::Float32)
                    return std::nullopt;
                const auto r = type->getRowCount();
                const auto c = type->getColumnCount();
                if (r == 2 && c == 2) return PT::f32x2x2;
                if (r == 3 && c == 3) return PT::f32x3x3;
                if (r == 4 && c == 4) return PT::f32x4x4;
                return std::nullopt;
            }

            default:
                return std::nullopt;
        }
    }


    static std::expected<std::vector<std::byte>, GLT::render::shader_compile_error> build_reflection_schema(slang::IComponentType* linked,
        u32 entry_flags) {

        namespace AM = GLT::asset::material;

        if (!linked)
            return std::unexpected{ GLT::render::shader_compile_error::reflection_schema_error };

        auto* layout = linked->getLayout();
        if (!layout)
            return std::unexpected{ GLT::render::shader_compile_error::reflection_schema_error };

        auto* type = layout->findTypeByName("material_params");
        if (!type)
            return std::unexpected{ GLT::render::shader_compile_error::reflection_schema_error };

        auto* type_layout = layout->getTypeLayout(type);
        if (!type_layout)
            return std::unexpected{ GLT::render::shader_compile_error::reflection_schema_error };

        const unsigned field_count = type_layout->getFieldCount();
        if (field_count == 0)
            return std::unexpected{ GLT::render::shader_compile_error::reflection_schema_error };

        std::vector<AM::reflection_param> params;
        params.reserve(field_count);

        std::string string_table;
        string_table.reserve(field_count * 16);

        u32 cursor = 0;

        for (unsigned i = 0; i < field_count; ++i) {

            auto* field = type_layout->getFieldByIndex(i);
            if (!field)
                return std::unexpected{ GLT::render::shader_compile_error::reflection_schema_error };

            const char* name = field->getName();
            if (!name || !*name)
                return std::unexpected{ GLT::render::shader_compile_error::reflection_schema_error };

            auto pt = slang_type_to_param_type(field->getType());
            if (!pt)
                return std::unexpected{ GLT::render::shader_compile_error::unsupported_param_type };

            const u32 size = AM::param_type_size(*pt);
            const u32 alignment = AM::param_type_alignment(*pt);
            const u32 byte_off = (cursor + alignment - 1) & ~(alignment - 1);
            cursor = byte_off + size;

            AM::reflection_param p{};
            p.name_hash = GLT::util::fnv1a_64(name);
            p.name_offset = static_cast<u32>(string_table.size());
            p.type = *pt;
            p.byte_offset = byte_off;
            p.byte_size = size;
            p.array_count = 1;
            p._pad = 0;

            string_table.append(name);
            string_table.push_back('\0');

            params.push_back(p);
        }

        const u32 header_size = static_cast<u32>(sizeof(AM::reflection_header));
        const u32 params_bytes = static_cast<u32>(params.size() * sizeof(AM::reflection_param));
        const u32 strings_off = header_size + params_bytes;
        const u32 total = strings_off + static_cast<u32>(string_table.size());

        std::vector<std::byte> blob(total);

        AM::reflection_header hdr{};
        hdr.magic = AM::REFLECT_SCHEMA_MAGIC;
        hdr.schema_version = AM::REFLECT_SCHEMA_VERSION;
        hdr.contract_version = static_cast<u16>(AM::MATERIAL_CONTRACT_VERSION);
        hdr.entry_flags = entry_flags;
        hdr.param_count = static_cast<u32>(params.size());
        hdr.param_table_offset = header_size;
        hdr.string_table_offset = strings_off;
        hdr.string_table_size = static_cast<u32>(string_table.size());
        hdr.packed_param_size = cursor;

        std::memcpy(blob.data(), &hdr, sizeof(hdr));
        std::memcpy(blob.data() + header_size, params.data(), params_bytes);
        std::memcpy(blob.data() + strings_off, string_table.data(), string_table.size());

        return blob;
    }


    static std::expected<std::vector<std::byte>, GLT::render::shader_compile_error> pack_defaults_from_linked(slang::IComponentType* /*linked*/,
        std::span<const std::byte> reflection_blob) {

        namespace AM = GLT::asset::material;

        if (reflection_blob.size() < sizeof(AM::reflection_header))
            return std::unexpected{ GLT::render::shader_compile_error::reflection_schema_error };

        const auto* hdr = reinterpret_cast<const AM::reflection_header*>(reflection_blob.data());
        if (hdr->magic != AM::REFLECT_SCHEMA_MAGIC)
            return std::unexpected{ GLT::render::shader_compile_error::reflection_schema_error };

        std::vector<std::byte> packed(hdr->packed_param_size, std::byte{ 0 });

        static bool warned = false;
        if (!warned) {
            LOG(warn, "material_compiler: default values are zero-initialized Slang SDK has no stable default-value reflection);"
                " set non-zero defaults through the editor until Phase 2 extends this.");
            warned = true;
        }

        return packed;
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    std::string_view renderer::name() const { return "vulkan_rasterizer"; }


    u64 renderer::implementation_hash() const { return m_shader_compiler_hash; }


    u32 renderer::contract_version() const { return GLT::asset::material::MATERIAL_CONTRACT_VERSION; }


    std::expected<GLT::render::compiled_shader, GLT::render::shader_compile_error> renderer::compile(std::string_view slang_source,
        GLT::asset::material::blend_mode blend) {


        namespace AM = GLT::asset::material;

        // Discover optional entry points by identifier scan

        // Slang reflection on the *material* module alone can't tell us whether vertex_transform / alpha_test were declared - they're
        // plain functions, not shader entry points. A text scan is the pragmatic choice for v0; if it ever misfires we can switch to
        // a throwaway compile that references each symbol and observe the failure
        const bool has_vertex_transform = slang_source.find("vertex_transform") != std::string_view::npos;
        const bool has_alpha_test = slang_source.find("alpha_test") != std::string_view::npos;

        const bool masked = (blend == AM::blend_mode::masked);
        VALIDATE(masked == has_alpha_test, return std::unexpected{ GLT::render::shader_compile_error::contract_version_mismatch },
            "", "material compile: blend/masked rule violated (masked={} alpha_test={})", masked, has_alpha_test);

        u32 entry_flags = AM::surface_evaluate;
        if (has_vertex_transform)
            entry_flags |= AM::vertex_transform;

        if (has_alpha_test)
            entry_flags |= AM::alpha_test;

        // Assemble helper
        const auto assemble = [&](std::string_view suffix) {
            std::string out;
            out.reserve(shading_strings::prefix.size() + slang_source.size() + suffix.size() + 8);
            out.append(shading_strings::prefix);
            out.append("\n");
            out.append(slang_source);
            out.append("\n");
            out.append(suffix);
            return out;
        };

        compiled_shader_bytecode_meaning buffer{};
        GLT::render::compiled_shader out{};

        {   // Vertex --------------------------------------------------------------------

            const auto src = assemble(shading_strings::vertex_suffix);
            auto bytecode = m_slang.compile(src, "vertex_main", "material.vert.slang");
            VALIDATE(bytecode, return std::unexpected{ GLT::render::shader_compile_error::slang_parse_failed },
                "", "material compile [vertex]: slang error {}", GLT::util::enum_to_string(bytecode.error()));

            buffer.vertex_bytecode = std::move(bytecode->spv);
        }

        {   // Fragment (also produces reflection + defaults) ---------------------------

            const auto src = assemble(shading_strings::fragment_suffix);
            auto bytecode = m_slang.compile(src, "fragment_main", "material.frag.slang");
            VALIDATE(bytecode, return std::unexpected{ GLT::render::shader_compile_error::slang_parse_failed }, 
                "", "material compile [fragment]: slang error {}", GLT::util::enum_to_string(bytecode.error()));
            buffer.fragment_bytecode = std::move(bytecode->spv);
                
            auto* linked = static_cast<slang::IComponentType*>(bytecode->linked.raw_component());

            auto schema = build_reflection_schema(linked, entry_flags);
            VALIDATE(schema, return std::unexpected{ schema.error() }, "", "material compile: reflection schema failed");
            out.reflection = std::move(*schema);

            auto defaults = pack_defaults_from_linked(linked, out.reflection);
            if (!defaults) {
                LOG(error, "material compile: default packing failed");
                return std::unexpected{ defaults.error() };
            }
            out.default_params = std::move(*defaults);
        }

        if (masked) {   // Depth-only (masked only)

            const auto src = assemble(shading_strings::depth_suffix);
            auto bytecode = m_slang.compile(src, "depth_main", "material.depth.slang");
            VALIDATE(bytecode, return std::unexpected{ GLT::render::shader_compile_error::slang_parse_failed }, 
                "", "material compile [depth]: slang error {}", GLT::util::enum_to_string(bytecode.error()));
            buffer.depth_bytecode = std::move(bytecode->spv);
        }

        // combine the 3 binary blobs
        GLT::util::set_bytes(out.bytecode, buffer.vertex_bytecode.size());
        GLT::util::set_bytes(out.bytecode, buffer.fragment_bytecode.size());
        GLT::util::set_bytes(out.bytecode, buffer.depth_bytecode.size());
        out.bytecode.insert(out.bytecode.end(), buffer.vertex_bytecode.begin(), buffer.vertex_bytecode.end());
        out.bytecode.insert(out.bytecode.end(), buffer.fragment_bytecode.begin(), buffer.fragment_bytecode.end());
        out.bytecode.insert(out.bytecode.end(), buffer.depth_bytecode.begin(), buffer.depth_bytecode.end());

        return out;
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void renderer::create_shader_compiler() {

        const auto contract_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "material_contract" / "v0";
        auto slang = util::slang_compiler::create(contract_dir);
        VALIDATE(slang.has_value(), return, "", "Failed to create Slang session for material compilation")
        m_slang = std::move(*slang);

        // Bumping any of these invalidates stored bytecode and the material handler recompiles
        u64 h = 14695981039346656037ull;
        h = GLT::util::mix_u64(h, GLT::util::fnv1a_64(util::slang_toolchain_version()));
        h = GLT::util::mix_u64(h, static_cast<u64>(GLT::asset::material::REFLECT_SCHEMA_VERSION));
        h = GLT::util::mix_u64(h, static_cast<u64>(GLT::asset::material::MATERIAL_CONTRACT_VERSION));
        h = GLT::util::mix_u64(h, static_cast<u64>(VK_MATERIAL_CODEGEN_REVISION));

        // shader prefix/suffix
        h = GLT::util::mix_u64(h, GLT::util::fnv1a_64(shading_strings::prefix));
        h = GLT::util::mix_u64(h, GLT::util::fnv1a_64(shading_strings::vertex_suffix));
        h = GLT::util::mix_u64(h, GLT::util::fnv1a_64(shading_strings::fragment_suffix));
        h = GLT::util::mix_u64(h, GLT::util::fnv1a_64(shading_strings::depth_suffix));

        m_shader_compiler_hash = h;
    }


    void renderer::destroy_shader_compiler() { }

}
