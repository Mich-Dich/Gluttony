
#include "util/pch.h"
#include "util/util_pch.h"
#include "shared_resource_manager.h"

#include <slang.h>

#include "asset/material.h"
#include "util/slang_compiler.h"
#include "shared/shading_strings.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    static std::size_t skip_ws(std::string_view s, std::size_t i);

    static std::optional<std::pair<std::size_t, std::size_t>> find_struct_body(std::string_view s, std::string_view name);

    static std::vector<std::string_view> split_field_decls(std::string_view body);

    static std::optional<std::pair<std::string, std::string>> parse_field(std::string_view decl);

    static std::optional<std::vector<f64>> parse_value_components(std::string_view expr);

    static void write_components(std::byte* dst, GLT::asset::material::param_type type, const std::vector<f64>& c);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static std::size_t skip_ws(std::string_view s, std::size_t i) {

        while (i < s.size()) {
            if (std::isspace(static_cast<unsigned char>(s[i]))) {
                i++;
                continue;
            }
            if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') {
                i += 2;
                while (i < s.size() && s[i] != '\n') 
                    i++;
                continue;
            }
            if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '*') {
                i += 2;
                while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/'))
                    i++;
                if (i + 1 < s.size())
                    i += 2;
                continue;
            }
            break;
        }
        return i;
    }


    static std::optional<std::pair<std::size_t, std::size_t>> find_struct_body(std::string_view s, std::string_view name) {

        std::size_t pos = 0;
        while (pos < s.size()) {

            pos = s.find("struct", pos);
            if (pos == std::string_view::npos)
                return std::nullopt;
            std::size_t p = skip_ws(s, pos + 6);
            if (p + name.size() > s.size() || s.substr(p, name.size()) != name) {
                pos += 6;
                continue;
            }
            std::size_t after = p + name.size();
            if (after < s.size() && (std::isalnum(static_cast<unsigned char>(s[after])) || s[after] == '_')) {
                pos += 6;
                continue;
            }
            p = skip_ws(s, after);
            if (p >= s.size() || s[p] != '{') {
                pos += 6;
                continue;
            }
            const std::size_t start = p + 1;
            std::size_t depth = 1, i = start;
            while (i < s.size() && depth > 0) {

                const char c = s[i];
                if (c == '{') {
                    depth++;
                    i++;
                    continue;
                }
                if (c == '}') {
                    depth--;
                    i++;
                    continue;
                }
                if (c == '/' && i + 1 < s.size()) {
                    if (s[i + 1] == '/') {
                        i += 2;
                        while (i < s.size() && s[i] != '\n') 
                            i++;
                        continue;
                    }
                    if (s[i + 1] == '*') {
                        i += 2;
                        while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/'))
                            i++;
                        if (i + 1 < s.size())
                        i += 2;
                        continue;
                    }
                }
                i++;
            }
            if (depth == 0)
                return std::make_pair(start, i - 1);
            return std::nullopt;
        }
        return std::nullopt;
    }


    static std::vector<std::string_view> split_field_decls(std::string_view body) {

        std::vector<std::string_view> out;
        std::size_t start = 0; int depth = 0;
        for (std::size_t i = 0; i < body.size(); i++) {
            const char c = body[i];
            if (c == '(' || c == '{' || c == '[')
                depth++;
            else if (c == ')' || c == '}' || c == ']')
                depth--;
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
        std::size_t i = lhs.size();
        while (i > 0 && std::isspace(static_cast<unsigned char>(lhs[i - 1])))
            i--;
        const std::size_t end = i;
        while (i > 0 && (std::isalnum(static_cast<unsigned char>(lhs[i - 1])) || lhs[i - 1] == '_'))
            i--;
        if (i == end)
            return std::nullopt;
        std::string name(lhs.substr(i, end - i));
        std::size_t rs = 0, re = rhs.size();
        while (rs < re && std::isspace(static_cast<unsigned char>(rhs[rs])))
            rs++;
        while (re > rs && std::isspace(static_cast<unsigned char>(rhs[re - 1])))
            re--;
        return std::make_pair(std::move(name), std::string(rhs.substr(rs, re - rs)));
    }


    static std::optional<std::vector<f64>> parse_value_components(std::string_view expr) {

        std::size_t s = 0, e = expr.size();
        while (s < e && std::isspace(static_cast<unsigned char>(expr[s])))
            s++;
        while (e > s && std::isspace(static_cast<unsigned char>(expr[e - 1])))
            e--;

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
        for (std::size_t i = 0; i <= args.size(); i++) {
            if (i != args.size() && args[i] != ',')
                continue;
            std::string_view tok = args.substr(start, i - start);
            start = i + 1;
            std::size_t ts = 0, te = tok.size();
            while (ts < te && std::isspace(static_cast<unsigned char>(tok[ts])))
                ts++;
            while (te > ts && std::isspace(static_cast<unsigned char>(tok[te - 1])))
                te--;
            tok = tok.substr(ts, te - ts);
            if (tok.empty())
                return std::nullopt;
            std::string tmp(tok);
            if (tmp.size() > 1 && (tmp.back() == 'f' || tmp.back() == 'F'))
                tmp.pop_back();
            try {

                if (tmp == "true")
                    out.push_back(1.0);
                else if (tmp == "false")
                    out.push_back(0.0);
                else
                    out.push_back(std::stod(tmp));

            } catch (...) { return std::nullopt; }
        }
        return out;
    }


    static void write_components(std::byte* dst, GLT::asset::material::param_type type, const std::vector<f64>& c) {

        using PT = GLT::asset::material::param_type;
        switch (type) {
            case PT::f32:  {
                if (c.size() != 1)
                    return;
                const f32 v = static_cast<f32>(c[0]);
                std::memcpy(dst, &v, sizeof(v));
                break;
            }
            case PT::f32x2: {
                f32 t[2];
                if (c.size()==1) {
                    t[0] = t[1] = static_cast<f32>(c[0]);
                } else if (c.size()==2) {
                    for(int k=0; k<2; k++)
                        t[k] = static_cast<f32>(c[k]);
                } else
                    return;
                std::memcpy(dst,t,sizeof(t));
                break;
            }
            case PT::f32x3: {
                f32 t[3];
                if (c.size()==1) {
                    for(int k=0; k<3; k++)
                        t[k] = static_cast<f32>(c[0]);
                } else if (c.size()==3) {
                    for(int k=0; k<3; k++)
                        t[k] = static_cast<f32>(c[k]);
                } else
                    return;
                std::memcpy(dst,t,sizeof(t));
                break;
            }
            case PT::f32x4: {
                f32 t[4];
                if (c.size()==1) {
                    for(int k=0; k<4; k++)
                        t[k] = static_cast<f32>(c[0]);
                } else if (c.size()==4) {
                    for(int k=0; k<4; k++)
                        t[k]=static_cast<f32>(c[k]);
                } else
                    return;
                std::memcpy(dst,t,sizeof(t));
                break;
            }
            case PT::i32: {
                if (c.size() != 1)
                    return;
                const i32 v = static_cast<i32>(c[0]);
                std::memcpy(dst, &v, sizeof(v));
                break;
            }
            case PT::u32: {
                if (c.size() != 1)
                    return;
                const u32 v = static_cast<u32>(c[0]);
                std::memcpy(dst, &v, sizeof(v));
                break;
            }
            case PT::boolean: {
                if (c.size() != 1)
                    return;
                const u32 v = c[0] != 0.0 ? 1u : 0u;
                std::memcpy(dst, &v, sizeof(v));
                break;
            }
            default: break; // matrices etc. — not extracted from source in v0
        }
    }


    static std::optional<GLT::asset::material::param_type> slang_scalar_to_param_type(slang::TypeReflection::ScalarType s) {

        using PT = GLT::asset::material::param_type;
        using S = slang::TypeReflection::ScalarType;
        switch (s) {
            case S::Float32:                return PT::f32;
            case S::Int32:                  return PT::i32;
            case S::UInt32:                 return PT::u32;
            case S::Bool:                   return PT::boolean;
            default:                        return std::nullopt;
        }
    }


    static std::optional<GLT::asset::material::param_type> slang_type_to_param_type(slang::TypeReflection* t) {

        using PT = GLT::asset::material::param_type;
        using K = slang::TypeReflection::Kind;
        if (!t)
            return std::nullopt;
        switch (t->getKind()) {
            case K::Scalar:
                return slang_scalar_to_param_type(t->getScalarType());

            case K::Vector:
                if (t->getScalarType() != slang::TypeReflection::ScalarType::Float32)
                    return std::nullopt;
                switch (t->getElementCount()) {
                    case 2:                 return PT::f32x2;
                    case 3:                 return PT::f32x3;
                    case 4:                 return PT::f32x4;
                    default:                return std::nullopt;
                }

            case K::Matrix: {
                if (t->getScalarType() != slang::TypeReflection::ScalarType::Float32)
                    return std::nullopt;
                const auto r = t->getRowCount(), c = t->getColumnCount();
                if (r == 2 && c == 2)
                    return PT::f32x2x2;
                if (r == 3 && c == 3)
                    return PT::f32x3x3;
                if (r == 4 && c == 4)
                    return PT::f32x4x4;
                return std::nullopt;
            }
            default: return std::nullopt;
        }
    }


    static std::expected<std::vector<std::byte>, GLT::render::shader_compile_error> build_reflection_schema(
        slang::IComponentType* linked, u32 entry_flags) {

        namespace AM = GLT::asset::material;
        namespace GR = GLT::render;
        #define BUILD_ERROR(type) std::unexpected{ GR::shader_compile_error{ GR::shader_compile_error_code::type, ""} }
        if (!linked)
            return BUILD_ERROR(reflection_schema_error);
        auto* layout = linked->getLayout();
        if (!layout)
            return BUILD_ERROR(reflection_schema_error);
        auto* type = layout->findTypeByName("material_params");
        if (!type)
            return BUILD_ERROR(reflection_schema_error);
        auto* tl = layout->getTypeLayout(type);
        if (!tl)
            return BUILD_ERROR(reflection_schema_error);
        const unsigned field_count = tl->getFieldCount();
        if (field_count == 0)
            return BUILD_ERROR(reflection_schema_error);

        std::vector<AM::reflection_param> params; params.reserve(field_count);
        std::string string_table; string_table.reserve(field_count * 16);
        u32 cursor = 0;

        for (unsigned i = 0; i < field_count; i++) {
            auto* field = tl->getFieldByIndex(i);
            if (!field)
                return BUILD_ERROR(reflection_schema_error);
            const char* name = field->getName();
            if (!name || !*name)
                return BUILD_ERROR(reflection_schema_error);
            auto pt = slang_type_to_param_type(field->getType());
            if (!pt)
                return BUILD_ERROR(unsupported_param_type);
            const u32 off = static_cast<u32>(field->getOffset());
            const u32 size = AM::param_type_size(*pt);
            cursor = off + size;

            AM::reflection_param p{};
            p.name_hash = GLT::util::fnv1a_64(name);
            p.name_offset = static_cast<u32>(string_table.size());
            p.type = *pt;
            p.byte_offset = off;
            p.byte_size = size;
            p.array_count = 1;
            p._pad = 0;
            string_table.append(name); string_table.push_back('\0');
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
        #define DEF_ERROR(t, m) std::unexpected{ GR::shader_compile_error{ GR::shader_compile_error_code::t, m} }

        if (reflection_blob.size() < sizeof(AM::reflection_header))
            return DEF_ERROR(reflection_schema_error, "reflection blob too small");
        const auto* hdr = reinterpret_cast<const AM::reflection_header*>(reflection_blob.data());
        if (hdr->magic != AM::REFLECT_SCHEMA_MAGIC)
            return DEF_ERROR(reflection_schema_error, "reflection magic mismatch");
        std::vector<std::byte> packed(hdr->packed_param_size, std::byte{0});

        const auto body_range = find_struct_body(material_source, "material_params");
        if (!body_range) {
            #undef DEF_ERROR
            return packed;
        }
        const std::string_view body{ material_source.data() + body_range->first, body_range->second - body_range->first };
        std::unordered_map<std::string, std::string> field_exprs;
        for (const auto decl : split_field_decls(body))
            if (auto f = parse_field(decl))
                field_exprs.emplace(std::move(f->first), std::move(f->second));

        const auto* params = reinterpret_cast<const AM::reflection_param*>(reflection_blob.data() + hdr->param_table_offset);
        const auto* strtab = reinterpret_cast<const char*>(reflection_blob.data() + hdr->string_table_offset);

        u32 applied = 0, missing = 0;
        for (u32 i = 0; i < hdr->param_count; i++) {
            const auto& p = params[i];
            const char* name = strtab + p.name_offset;
            const auto it = field_exprs.find(name);
            if (it == field_exprs.end()) {
                missing++;
                continue;
            }
            const auto comps = parse_value_components(it->second);
            if (!comps) {
                missing++;
                continue;
            }
            write_components(packed.data() + p.byte_offset, p.type, *comps);
            applied++;
        }
        #undef DEF_ERROR
        LOG(info, "packed {} default(s) from source ({} param(s) zero-init)", applied, missing);
        return packed;
    }


    static GLT::render::shader_compile_error_code map_raw_slang_code(util::raw_slang_error_code code) {

        using R = util::raw_slang_error_code;
        using S = GLT::render::shader_compile_error_code;
        switch (code) {
            case R::already_compiled:        return S::already_compiled;
            case R::session_creation_failed: return S::slang_unavailable;
            case R::link_failed:             return S::slang_link_failed;
            case R::entry_point_missing:     return S::slang_parse_failed;
            case R::spirv_emission_failed:   return S::backend_error;
            case R::source_load_failed:
            case R::none:
            default:                         return S::slang_parse_failed;
        }
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    std::string_view shared_resource_manager::compiler_name() const { return "vulkan_rasterizer"; }


    u32 shared_resource_manager::contract_version() const { return GLT::asset::material::MATERIAL_CONTRACT_VERSION; }


    std::expected<GLT::render::compiled_shader, GLT::render::shader_compile_error>
    shared_resource_manager::compile(std::string_view slang_source, GLT::asset::material::blend_mode blend) {

        namespace AM = GLT::asset::material;
        namespace GR = GLT::render;
        #define BUILD_ERROR(type, msg)     std::unexpected{ GR::shader_compile_error{ GR::shader_compile_error_code::type, msg} }
        #define BUILD_ERROR_D(type, msg)   std::unexpected{ GR::shader_compile_error{ type, msg} }

        const bool has_vertex_transform = slang_source.find("vertex_transform") != std::string_view::npos;
        const bool has_alpha_test = slang_source.find("alpha_test") != std::string_view::npos;
        const bool masked = blend == AM::blend_mode::masked;

        if (masked != has_alpha_test)
            return BUILD_ERROR(contract_version_mismatch,
                std::format("blend/masked rule violated (masked={} alpha_test={})", masked, has_alpha_test));

        u32 entry_flags = AM::surface_evaluate;
        if (has_vertex_transform)
            entry_flags |= AM::vertex_transform;
        if (has_alpha_test)
            entry_flags |= AM::alpha_test;

        const auto assemble = [&](std::string_view suffix) {
            std::string out;
            out.reserve(shading_strings::prefix.size() + slang_source.size() + suffix.size() + 8);
            out.append(shading_strings::prefix);
            out.append("\n"); out.append(slang_source); out.append("\n"); out.append(suffix);
            return out;
        };

        std::vector<std::byte> vs_bytes, fs_bytes, ds_bytes;

        {
            const auto vsuf = has_vertex_transform ? shading_strings::vertex_suffix_with_transform : shading_strings::vertex_suffix;
            const auto src = assemble(vsuf);
            auto bc = m_slang.compile(src, "vertex_main", "material.vert.slang");
            if (!bc)
                return BUILD_ERROR_D(map_raw_slang_code(bc.error().code),
                    std::format("vertex stage: {} {}", GLT::util::enum_to_string(bc.error().code), bc.error().message));
            vs_bytes = std::move(bc->spv);
        }

        GR::compiled_shader out;
        {
            const auto src = assemble(shading_strings::fragment_suffix);
            auto bc = m_slang.compile(src, "fragment_main", "material.frag.slang");
            if (!bc)
                return BUILD_ERROR_D(map_raw_slang_code(bc.error().code),
                    std::format("fragment stage: {} {}", GLT::util::enum_to_string(bc.error().code), bc.error().message));
            fs_bytes = std::move(bc->spv);

            auto* linked = static_cast<slang::IComponentType*>(bc->linked.raw_component());
            auto schema = build_reflection_schema(linked, entry_flags);
            if (!schema)
                return std::unexpected{ GR::shader_compile_error{ schema.error().code, "reflection schema failed" } };
            out.reflection = std::move(*schema);

            auto defaults = pack_defaults_from_linked(linked, out.reflection, slang_source);
            if (!defaults)
                return std::unexpected{ GR::shader_compile_error{ defaults.error().code, "default packing failed" } };
            out.default_params = std::move(*defaults);
        }

        {
            const auto suffix = masked ? shading_strings::depth_suffix : shading_strings::depth_noop_suffix;
            const auto src = assemble(suffix);
            auto bc = m_slang.compile(src, "depth_main", "material.depth.slang");
            if (!bc)
                return BUILD_ERROR_D(map_raw_slang_code(bc.error().code),
                    std::format("depth stage: {} {}", GLT::util::enum_to_string(bc.error().code), bc.error().message));
            ds_bytes = std::move(bc->spv);
        }
        #undef BUILD_ERROR
        #undef BUILD_ERROR_D

        // pack blob: [vs_size][fs_size][ds_size][vs][fs][ds]
        GLT::util::set_bytes<u64>(out.bytecode, vs_bytes.size());
        GLT::util::set_bytes<u64>(out.bytecode, fs_bytes.size());
        GLT::util::set_bytes<u64>(out.bytecode, ds_bytes.size());
        out.bytecode.insert(out.bytecode.end(), vs_bytes.begin(), vs_bytes.end());
        out.bytecode.insert(out.bytecode.end(), fs_bytes.begin(), fs_bytes.end());
        out.bytecode.insert(out.bytecode.end(), ds_bytes.begin(), ds_bytes.end());
        return out;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void shared_resource_manager::create_shader_compiler() {

        const auto contract_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "material_contract" / "v0";
        auto slang = util::slang_compiler::create(contract_dir);
        VALIDATE(slang.has_value(), return, "", "Failed to create Slang session")
        m_slang = std::move(*slang);

        u64 h = 14695981039346656037ull;
        h = GLT::util::mix_u64(h, GLT::util::fnv1a_64(util::slang_toolchain_version()));
        h = GLT::util::mix_u64(h, static_cast<u64>(GLT::asset::material::REFLECT_SCHEMA_VERSION));
        h = GLT::util::mix_u64(h, static_cast<u64>(GLT::asset::material::MATERIAL_CONTRACT_VERSION));
        h = GLT::util::mix_u64(h, static_cast<u64>(VK_MATERIAL_CODEGEN_REVISION));
        h = GLT::util::mix_u64(h, GLT::util::fnv1a_64(shading_strings::prefix));
        h = GLT::util::mix_u64(h, GLT::util::fnv1a_64(shading_strings::vertex_suffix));
        h = GLT::util::mix_u64(h, GLT::util::fnv1a_64(shading_strings::vertex_suffix_with_transform));
        h = GLT::util::mix_u64(h, GLT::util::fnv1a_64(shading_strings::fragment_suffix));
        h = GLT::util::mix_u64(h, GLT::util::fnv1a_64(shading_strings::depth_suffix));
        m_shader_compiler_hash = h;
    }


    void shared_resource_manager::destroy_shader_compiler() { }

}
