
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

    // Slang doesn't expose default-value reflection through a stable API, so we parse the source directly. The parser is
    // deliberately small and only understands what the v0 contract allows on material_params fields
    namespace {

        // Skip whitespace and //, /* */ comments
        static std::size_t skip_ws(std::string_view s, std::size_t i);


        // Find `struct <name> { ... }` and return the byte range of the body (exclusive of braces)
        static std::optional<std::pair<std::size_t, std::size_t>> find_struct_body(std::string_view s, std::string_view struct_name);


        // Split a struct body on top-level ';'
        static std::vector<std::string_view> split_field_decls(std::string_view body);

        // From `float3 base_color = float3(0.8, 0.8, 0.8)` pull out (name, rhs)
        static std::optional<std::pair<std::string, std::string>> parse_field(std::string_view decl);

        // Parse an initializer into a flat list of components.
        // Accepts:
        //   scalar literal          0.5      -1  42  true  false  0.5f
        //   broadcast               float3(0.5)
        //   explicit vector         float3(0.8, 0.8, 0.8)
        //   brace init              { 0.8, 0.8, 0.8 }
        static std::optional<std::vector<f64>> parse_value_components(std::string_view expr);

        // Write a component list into the packed buffer at dst according to the contract type
        static void write_components(std::byte* dst, GLT::asset::material::param_type type, const std::vector<f64>& comps);
    }

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


    // @brief Reads each field's default value out of the *source text* and writes it into the packed params blob at the
    // offset given by the reflection schema
    //
    // NOTE(Slang SDK): the pinned Slang reflection API does not expose struct field default initializers through a stable
    // C++ accessor. Rather than reach into unstable internals, we parse the `material_params` struct declaration from the
    // source directly. This is sufficient for the v0 contract, which restricts params to scalars, float2/3/4, and bool/int/uint
    //
    // Fields whose initializer can't be parsed (a named constant, an expression, an unsupported type) are left zero-initialized
    static std::expected<std::vector<std::byte>, GLT::render::shader_compile_error> pack_defaults_from_linked(
        slang::IComponentType* linked, std::span<const std::byte> reflection_blob, std::string_view material_source);

    // @brief Translate a raw slang error code into the public shader_compile_error_code, preserving the
    // "already_compiled" signal so the editor can report it as a status rather than a failure
    static GLT::render::shader_compile_error_code map_raw_slang_code(util::raw_slang_error_code code);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    namespace {

        static std::size_t skip_ws(std::string_view s, std::size_t i) {

            while (i < s.size()) {

                if (std::isspace(static_cast<unsigned char>(s[i]))) { ++i; continue; }

                if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') {
                    i += 2;
                    while (i < s.size() && s[i] != '\n') ++i;
                    continue;
                }

                if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '*') {
                    i += 2;
                    while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) ++i;
                    if (i + 1 < s.size()) i += 2;
                    continue;
                }

                break;
            }
            return i;
        }


        static std::optional<std::pair<std::size_t, std::size_t>> find_struct_body(std::string_view s, std::string_view struct_name) {

            std::size_t pos = 0;
            while (pos < s.size()) {

                pos = s.find("struct", pos);
                if (pos == std::string_view::npos)
                    return std::nullopt;

                std::size_t p = skip_ws(s, pos + 6);

                // Must match the requested name exactly (no prefix matches)
                if (p + struct_name.size() > s.size()
                    || s.substr(p, struct_name.size()) != struct_name) {
                    pos += 6;
                    continue;
                }

                std::size_t after = p + struct_name.size();
                if (after < s.size()
                    && (std::isalnum(static_cast<unsigned char>(s[after])) || s[after] == '_')) {
                    pos += 6;
                    continue;
                }

                // Skip ws then require '{'
                p = skip_ws(s, after);
                if (p >= s.size() || s[p] != '{') { pos += 6; continue; }

                // Match braces, respecting comments
                const std::size_t body_start = p + 1;
                std::size_t depth = 1;
                std::size_t i = body_start;

                while (i < s.size() && depth > 0) {

                    const char c = s[i];

                    if (c == '{') { ++depth; ++i; continue; }
                    if (c == '}') { --depth; ++i; continue; }

                    if (c == '/' && i + 1 < s.size()) {
                        if (s[i + 1] == '/') {
                            i += 2;
                            while (i < s.size() && s[i] != '\n') ++i;
                            continue;
                        }
                        if (s[i + 1] == '*') {
                            i += 2;
                            while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) ++i;
                            if (i + 1 < s.size()) i += 2;
                            continue;
                        }
                    }

                    ++i;
                }

                if (depth == 0)
                    return std::make_pair(body_start, i - 1);
                return std::nullopt;
            }
            return std::nullopt;
        }


        static std::vector<std::string_view> split_field_decls(std::string_view body) {

            std::vector<std::string_view> out;
            std::size_t start = 0;
            int depth = 0;

            for (std::size_t i = 0; i < body.size(); ++i) {

                const char c = body[i];
                if (c == '(' || c == '{' || c == '[') ++depth;
                else if (c == ')' || c == '}' || c == ']') --depth;
                else if (c == ';' && depth == 0) {
                    out.push_back(body.substr(start, i - start));
                    start = i + 1;
                }
            }

            if (start < body.size())
                out.push_back(body.substr(start));

            return out;
        }


        static std::optional<std::pair<std::string, std::string>> parse_field(std::string_view decl) {

            const auto eq = decl.find('=');
            if (eq == std::string_view::npos)
                return std::nullopt;

            const std::string_view lhs = decl.substr(0, eq);
            const std::string_view rhs = decl.substr(eq + 1);

            // Walk lhs backwards to grab the last identifier (the field name)
            std::size_t i = lhs.size();
            while (i > 0 && std::isspace(static_cast<unsigned char>(lhs[i - 1]))) --i;
            const std::size_t end = i;
            while (i > 0 && (std::isalnum(static_cast<unsigned char>(lhs[i - 1])) || lhs[i - 1] == '_')) --i;

            if (i == end)
                return std::nullopt;

            std::string name(lhs.substr(i, end - i));

            // Trim rhs
            std::size_t rs = 0, re = rhs.size();
            while (rs < re && std::isspace(static_cast<unsigned char>(rhs[rs]))) ++rs;
            while (re > rs && std::isspace(static_cast<unsigned char>(rhs[re - 1]))) --re;

            return std::make_pair(std::move(name), std::string(rhs.substr(rs, re - rs)));
        }


        static std::optional<std::vector<f64>> parse_value_components(std::string_view expr) {

            // Trim
            std::size_t s = 0, e = expr.size();
            while (s < e && std::isspace(static_cast<unsigned char>(expr[s]))) ++s;
            while (e > s && std::isspace(static_cast<unsigned char>(expr[e - 1]))) --e;
            expr = expr.substr(s, e - s);

            if (expr.empty())
                return std::nullopt;

            std::string_view args = expr;

            if (const auto paren = expr.find('('); paren != std::string_view::npos) {
                const auto close = expr.rfind(')');
                if (close == std::string_view::npos || close < paren)
                    return std::nullopt;
                args = expr.substr(paren + 1, close - paren - 1);
            } else if (expr.front() == '{' && expr.back() == '}') {
                args = expr.substr(1, expr.size() - 2);
            }

            std::vector<f64> out;
            std::size_t start = 0;

            for (std::size_t i = 0; i <= args.size(); ++i) {

                if (i != args.size() && args[i] != ',')
                    continue;

                std::string_view tok = args.substr(start, i - start);
                start = i + 1;

                // Trim
                std::size_t ts = 0, te = tok.size();
                while (ts < te && std::isspace(static_cast<unsigned char>(tok[ts]))) ++ts;
                while (te > ts && std::isspace(static_cast<unsigned char>(tok[te - 1]))) --te;
                tok = tok.substr(ts, te - ts);

                if (tok.empty())
                    return std::nullopt;

                std::string tmp(tok);

                // Strip Slang/HLSL float suffix
                if (tmp.size() > 1 && (tmp.back() == 'f' || tmp.back() == 'F'))
                    tmp.pop_back();

                try {
                    if (tmp == "true")       out.push_back(1.0);
                    else if (tmp == "false") out.push_back(0.0);
                    else                     out.push_back(std::stod(tmp));
                } catch (...) {
                    return std::nullopt;
                }
            }

            return out;
        }


        static void write_components(std::byte* dst, GLT::asset::material::param_type type, const std::vector<f64>& comps) {

            using PT = GLT::asset::material::param_type;

            switch (type) {

                case PT::f32: {
                    if (comps.size() != 1) return;
                    const f32 v = static_cast<f32>(comps[0]);
                    std::memcpy(dst, &v, sizeof(v));
                    break;
                }

                case PT::f32x2: {
                    f32 tmp[2];
                    if      (comps.size() == 1) { tmp[0] = tmp[1] = static_cast<f32>(comps[0]); }
                    else if (comps.size() == 2) { for (int k = 0; k < 2; ++k) tmp[k] = static_cast<f32>(comps[k]); }
                    else return;
                    std::memcpy(dst, tmp, sizeof(tmp));
                    break;
                }

                case PT::f32x3: {
                    f32 tmp[3];
                    if      (comps.size() == 1) { for (int k = 0; k < 3; ++k) tmp[k] = static_cast<f32>(comps[0]); }
                    else if (comps.size() == 3) { for (int k = 0; k < 3; ++k) tmp[k] = static_cast<f32>(comps[k]); }
                    else return;
                    std::memcpy(dst, tmp, sizeof(tmp));
                    break;
                }

                case PT::f32x4: {
                    f32 tmp[4];
                    if      (comps.size() == 1) { for (int k = 0; k < 4; ++k) tmp[k] = static_cast<f32>(comps[0]); }
                    else if (comps.size() == 4) { for (int k = 0; k < 4; ++k) tmp[k] = static_cast<f32>(comps[k]); }
                    else return;
                    std::memcpy(dst, tmp, sizeof(tmp));
                    break;
                }

                case PT::i32: {
                    if (comps.size() != 1) return;
                    const i32 v = static_cast<i32>(comps[0]);
                    std::memcpy(dst, &v, sizeof(v));
                    break;
                }

                case PT::u32: {
                    if (comps.size() != 1) return;
                    const u32 v = static_cast<u32>(comps[0]);
                    std::memcpy(dst, &v, sizeof(v));
                    break;
                }

                case PT::boolean: {
                    if (comps.size() != 1) return;
                    const u32 v = comps[0] != 0.0 ? 1u : 0u;
                    std::memcpy(dst, &v, sizeof(v));
                    break;
                }

                default:
                    // Matrix types: not supported for text-extracted defaults in v0
                    break;
            }
        }

    }


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
        namespace GR = GLT::render;
        #define BUILD_ERROR(type, msg)          std::unexpected{ GR::shader_compile_error{ GR::shader_compile_error_code::type, msg} };

        if (!linked)
            return BUILD_ERROR(reflection_schema_error, "")

        auto* layout = linked->getLayout();
        if (!layout)
            return BUILD_ERROR(reflection_schema_error, "")

        auto* type = layout->findTypeByName("material_params");
        if (!type)
            return BUILD_ERROR(reflection_schema_error, "")

        auto* type_layout = layout->getTypeLayout(type);
        if (!type_layout)
            return BUILD_ERROR(reflection_schema_error, "")

        const unsigned field_count = type_layout->getFieldCount();
        if (field_count == 0)
            return BUILD_ERROR(reflection_schema_error, "")

        std::vector<AM::reflection_param> params;
        params.reserve(field_count);

        std::string string_table;
        string_table.reserve(field_count * 16);

        u32 cursor = 0;

        for (unsigned index = 0; index < field_count; index++) {

            auto* field = type_layout->getFieldByIndex(index);
            if (!field)
                return BUILD_ERROR(reflection_schema_error, "")

            const char* name = field->getName();
            if (!name || !*name)
                return BUILD_ERROR(reflection_schema_error, "")

            auto pt = slang_type_to_param_type(field->getType());
            if (!pt)
                return BUILD_ERROR(unsupported_param_type, "")

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

        #undef BUILD_ERROR

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


    static std::expected<std::vector<std::byte>, GLT::render::shader_compile_error> pack_defaults_from_linked(
        slang::IComponentType* /*linked*/, std::span<const std::byte> reflection_blob, std::string_view material_source) {


        namespace AM = GLT::asset::material;
        namespace GR = GLT::render;

        #define BUILD_ERROR(type, msg)  std::unexpected{ GR::shader_compile_error{ GR::shader_compile_error_code::type, msg} };

        if (reflection_blob.size() < sizeof(AM::reflection_header))
            return BUILD_ERROR(reflection_schema_error, "reflection blob too small")

        const auto* hdr = reinterpret_cast<const AM::reflection_header*>(reflection_blob.data());
        if (hdr->magic != AM::REFLECT_SCHEMA_MAGIC)
            return BUILD_ERROR(reflection_schema_error, "reflection magic mismatch")

        std::vector<std::byte> packed(hdr->packed_param_size, std::byte{ 0 });

        // Extract field defaults from the source text ------------------------------------
        const auto body_range = find_struct_body(material_source, "material_params");
        if (!body_range) {
            // No struct in the source - leave zeroes, not an error. The rest of the compile
            // pipeline will fail earlier if the struct is truly required
            #undef BUILD_ERROR
            return packed;
        }

        const std::string_view body{
            material_source.data() + body_range->first,
            body_range->second - body_range->first
        };

        std::unordered_map<std::string, std::string> field_exprs;
        for (const auto decl : split_field_decls(body))
            if (auto f = parse_field(decl))
                field_exprs.emplace(std::move(f->first), std::move(f->second));

        // Walk the reflection table and write matching defaults --------------------------
        const auto* params = reinterpret_cast<const AM::reflection_param*>(
            reflection_blob.data() + hdr->param_table_offset);
        const auto* strtab = reinterpret_cast<const char*>(
            reflection_blob.data() + hdr->string_table_offset);

        u32 applied = 0, missing = 0;

        for (u32 i = 0; i < hdr->param_count; ++i) {

            const auto& p = params[i];
            const char* name = strtab + p.name_offset;

            const auto it = field_exprs.find(name);
            if (it == field_exprs.end()) { ++missing; continue; }

            const auto comps = parse_value_components(it->second);
            if (!comps) { ++missing; continue; }

            write_components(packed.data() + p.byte_offset, p.type, *comps);
            ++applied;
        }

        #undef BUILD_ERROR

        LOG(info, "material_compiler: packed {} default(s) from source ({} param(s) left zero-initialized)",
            applied, missing);

        return packed;
    }


    static GLT::render::shader_compile_error_code map_raw_slang_code(util::raw_slang_error_code code) {

        using R = util::raw_slang_error_code;
        using S = GLT::render::shader_compile_error_code;

        switch (code) {
            case R::already_compiled:       return S::already_compiled;
            case R::session_creation_failed:return S::slang_unavailable;
            case R::link_failed:            return S::slang_link_failed;
            case R::entry_point_missing:    return S::slang_parse_failed;
            case R::spirv_emission_failed:  return S::backend_error;
            case R::source_load_failed:
            case R::none:
            default:                        return S::slang_parse_failed;
        }
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
        namespace GR = GLT::render;
        #define BUILD_ERROR(type, msg)      std::unexpected{ GR::shader_compile_error{ GR::shader_compile_error_code::type, msg}};
        #define BUILD_ERROR_D(type, msg)    std::unexpected{ GR::shader_compile_error{ type, msg}};

        // Discover optional entry points by identifier scan

        // Slang reflection on the *material* module alone can't tell us whether vertex_transform / alpha_test were declared - they're
        // plain functions, not shader entry points. A text scan is the pragmatic choice for v0; if it ever misfires we can switch to
        // a throwaway compile that references each symbol and observe the failure
        const bool has_vertex_transform = slang_source.find("vertex_transform") != std::string_view::npos;
        const bool has_alpha_test = slang_source.find("alpha_test") != std::string_view::npos;

        const bool masked = blend == AM::blend_mode::masked;
        if(masked != has_alpha_test)
            return BUILD_ERROR(contract_version_mismatch, 
                std::format("blend/masked rule violated (masked={} alpha_test={})", masked, has_alpha_test))

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
        GR::compiled_shader out{};

        {   // Vertex

            const auto vsuf = has_vertex_transform ? shading_strings::vertex_suffix_with_transform : shading_strings::vertex_suffix;
            const auto src = assemble(vsuf);
            auto bytecode = m_slang.compile(src, "vertex_main", "material.vert.slang");
            if (!bytecode)
                return BUILD_ERROR_D(map_raw_slang_code(bytecode.error().code),
                    std::format("Failed to compile stage [vertex]: slang error code {} message {}",
                        GLT::util::enum_to_string(bytecode.error().code), bytecode.error().message))

            buffer.vertex_bytecode = std::move(bytecode->spv);
        }

        {   // Fragment (also produces reflection + defaults)

            const auto src = assemble(shading_strings::fragment_suffix);
            auto bytecode = m_slang.compile(src, "fragment_main", "material.frag.slang");
            if (!bytecode)
                return BUILD_ERROR_D(map_raw_slang_code(bytecode.error().code),
                    std::format("Failed to compile stage [fragment]: slang error code {} message {}",
                        GLT::util::enum_to_string(bytecode.error().code), bytecode.error().message))

            buffer.fragment_bytecode = std::move(bytecode->spv);

            auto* linked = static_cast<slang::IComponentType*>(bytecode->linked.raw_component());

            auto schema = build_reflection_schema(linked, entry_flags);
            if (!schema)
                return std::unexpected{ GR::shader_compile_error{
                    schema.error().code,
                    "reflection schema failed"
                }};
            out.reflection = std::move(*schema);

            auto defaults = pack_defaults_from_linked(linked, out.reflection, slang_source);
            if (!defaults) {
                return std::unexpected{  GR::shader_compile_error{
                    defaults.error().code,
                    "material compile: default packing failed"
                }};
            }
            out.default_params = std::move(*defaults);
        }

        if (masked) {   // Depth-only (masked only)

            const auto src = assemble(shading_strings::depth_suffix);
            auto bytecode = m_slang.compile(src, "depth_main", "material.depth.slang");
            if (!bytecode)
                return BUILD_ERROR_D(map_raw_slang_code(bytecode.error().code),
                    std::format("Failed to compile stage [depth]: slang error code {} message {}",
                        GLT::util::enum_to_string(bytecode.error().code), bytecode.error().message))

            buffer.depth_bytecode = std::move(bytecode->spv);
        }

        #undef BUILD_ERROR

        // combine the 3 binary blobs
        GLT::util::set_bytes<u64>(out.bytecode, buffer.vertex_bytecode.size());
        GLT::util::set_bytes<u64>(out.bytecode, buffer.fragment_bytecode.size());
        GLT::util::set_bytes<u64>(out.bytecode, buffer.depth_bytecode.size());
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
        h = GLT::util::mix_u64(h, GLT::util::fnv1a_64(shading_strings::vertex_suffix_with_transform));
        h = GLT::util::mix_u64(h, GLT::util::fnv1a_64(shading_strings::fragment_suffix));
        h = GLT::util::mix_u64(h, GLT::util::fnv1a_64(shading_strings::depth_suffix));

        m_shader_compiler_hash = h;
    }


    void renderer::destroy_shader_compiler() { }

}
