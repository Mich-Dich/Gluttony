#include "util/pch.h"
#include "material_viewer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <config/imgui_config.h>
#include <asset/type.h>
#include <asset/i_asset_registry.h>
#include <plugin_system/plugin_manager.h>
#include <render/i_renderer.h>

#include "material/material_compiler.h"
#include "util/ui/panel_collection.h"
#include "util/ui/asset_picker.h"
#include "resource_manager/icon_manager.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr f32                                   MIN_DETAILS_WIDTH = 300.0f;

    constexpr f32                                   MIN_SOURCE_WIDTH = 320.0f;

    constexpr f32                                   SPLITTER_WIDTH = 4.0f;

    constexpr f32                                   PREVIEW_MAX_HEIGHT = 260.0f;

    constexpr const char*                           DEFAULT_SLANG_SOURCE = R"slang(
import material_contract_v0;


struct material_params {
    float3  		base_color = float3(0.8, 0.8, 0.8);
    float   		roughness = 0.5;
    float   		metallic = 0.0;
};

surface_output surface_evaluate(surface_input s, material_params p, material_textures tex) {
    surface_output o = make_default_surface(s);
    o.base_color = p.base_color;
    o.roughness = p.roughness;
    o.metallic = p.metallic;
    return o;
}

)slang";

    constexpr static TextEditor::Palette            kSlangPalette = {{
        IM_COL32(224, 224, 224, 255),   // text
        IM_COL32(197, 134, 192, 255),   // keyword
        IM_COL32( 90, 179, 155, 255),   // declaration
        IM_COL32(181, 206, 168, 255),   // number
        IM_COL32(206, 145, 120, 255),   // string
        IM_COL32(255, 255, 153, 255),   // punctuation
        IM_COL32( 64, 192, 128, 255),   // preprocessor
        IM_COL32(156, 220, 254, 255),   // identifier
        IM_COL32( 79, 193, 255, 255),   // known identifier
        IM_COL32(106, 153,  85, 255),   // comment
        IM_COL32( 30,  30,  30, 255),   // background
        IM_COL32(224, 224, 224, 255),   // cursor
        IM_COL32( 32,  96, 160, 255),   // selection
        IM_COL32(  0,   0,   0,   0),   // whitespace
        IM_COL32( 70,  70,  70, 255),   // matchingBracketBackground
        IM_COL32(140, 140, 140, 255),   // matchingBracketActive
        IM_COL32(246, 222,  36, 255),   // matchingBracketLevel1
        IM_COL32( 66, 120, 198, 255),   // matchingBracketLevel2
        IM_COL32(213,  96, 213, 255),   // matchingBracketLevel3
        IM_COL32(198,   8,  32, 255),   // matchingBracketError
        IM_COL32(128, 128, 144, 255),   // line number
        IM_COL32(224, 224, 240, 255),   // current line number
    }};

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================
        
    static const char* blend_mode_label(GLT::asset::material::blend_mode b);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    template <typename T>
    static T read_packed(const std::vector<std::byte>& blob, u32 offset);


    template <typename T>
    static void write_packed(std::vector<std::byte>& blob, u32 offset, const T& v);

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static const char* blend_mode_label(GLT::asset::material::blend_mode b) {

        using bm = GLT::asset::material::blend_mode;
        switch (b) {
            case bm::opaque:            return "Opaque";
            case bm::masked:            return "Masked";
            case bm::translucent:       return "Translucent";
            case bm::additive:          return "Additive";
            case bm::modulate:          return "Modulate";
            case bm::alpha_composite:   return "Alpha Composite";
            case bm::alpha_holdout:     return "Alpha Holdout";
            default:                    return "(unknown)";
        }
    }


    namespace {

        // One line under the blend mode combo: what the mode does
        const char* blend_mode_description(const GLT::asset::material::blend_mode b) {

            using bm = GLT::asset::material::blend_mode;
            switch (b) {
                case bm::opaque:            return "Solid surface. Writes depth and casts shadows";
                case bm::masked:            return "Solid, but pixels are cut out by the shader's alpha_test()";
                case bm::translucent:       return "See-through. Sorted back to front, no depth write, no shadows";
                case bm::additive:          return "Adds its color to what is behind it (fire, glow)";
                case bm::modulate:          return "Multiplies what is behind it (stains, tinted glass)";
                case bm::alpha_composite:   return "Premultiplied alpha, the shader outputs rgb * opacity";
                case bm::alpha_holdout:     return "Cuts a hole into what is behind it";
                default:                    return "";
            }
        }


        // Parameter names that are edited with a color picker instead of number fields
        bool looks_like_color(std::string_view name) {

            std::string lower(name);
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            for (const char* key : { "color", "colour", "tint", "albedo", "emissive" })
                if (lower.find(key) != std::string::npos)
                    return true;
            return false;
        }


        // Thin draggable divider. [vertical_line] = divides left | right, otherwise top / bottom.
        // [invert] = dragging right / down makes [size] smaller (the panel sits after the splitter)
        void splitter(const char* id, const bool vertical_line, f32& size, const f32 min_size, const f32 max_size, const f32 length,
            const bool invert) {

            if (length < 1.f)
                return;

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton(id, vertical_line ? ImVec2(SPLITTER_WIDTH, length) : ImVec2(length, SPLITTER_WIDTH));

            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();

            if (active) {
                const f32 delta = vertical_line ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y;
                size = std::clamp(size + (invert ? -delta : delta), min_size, std::max(min_size, max_size));
            }

            if (hovered || active)
                ImGui::SetMouseCursor(vertical_line ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            const f32 center = (vertical_line ? pos.x : pos.y) + SPLITTER_WIDTH * 0.5f;
            const ImU32 accent = ImGui::GetColorU32(active ? GLT::imgui_config::get_action_color00_active_ref()
                                                           : GLT::imgui_config::get_action_color00_hover_ref());

            if (vertical_line) {
                if (hovered || active)
                    draw_list->AddRectFilled(ImVec2(center - 1.f, pos.y), ImVec2(center + 1.f, pos.y + length), accent);
                else
                    draw_list->AddLine(ImVec2(center, pos.y), ImVec2(center, pos.y + length), ImGui::GetColorU32(ImGuiCol_Separator));
            } else {
                if (hovered || active)
                    draw_list->AddRectFilled(ImVec2(pos.x, center - 1.f), ImVec2(pos.x + length, center + 1.f), accent);
                else
                    draw_list->AddLine(ImVec2(pos.x, center), ImVec2(pos.x + length, center), ImGui::GetColorU32(ImGuiCol_Separator));
            }
        }


        // Background band for a panel header (source / output). Returns its height
        f32 panel_strip(const char* title, const ImVec2& min, const f32 width) {

            const f32 height = ImGui::GetFrameHeight() + 8.f;
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddRectFilled(min, ImVec2(min.x + width, min.y + height), ImGui::GetColorU32(ImGuiCol_TitleBg));
            draw_list->AddLine(ImVec2(min.x, min.y + height - 1.f), ImVec2(min.x + width, min.y + height - 1.f),
                ImGui::GetColorU32(ImGuiCol_Separator, 0.6f));

            ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold));
            draw_list->AddText(ImVec2(min.x + 12.f, min.y + (height - ImGui::GetTextLineHeight()) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), title);
            ImGui::PopFont();
            return height;
        }


        // Small rounded label with a colored dot ("Compiled", "Failed", "Unsaved changes")
        f32 status_pill(ImDrawList* draw_list, const ImVec2& pos, const char* text, const ImU32 dot, const ImU32 fill, const ImU32 text_color) {

            const ImVec2 size = ImGui::CalcTextSize(text);
            const ImVec2 pill_min(pos.x, pos.y - 2.f);
            const ImVec2 pill_max(pos.x + size.x + 22.f, pos.y + size.y + 2.f);
            draw_list->AddRectFilled(pill_min, pill_max, fill, (pill_max.y - pill_min.y) * 0.5f);
            draw_list->AddCircleFilled(ImVec2(pos.x + 9.f, pos.y + size.y * 0.5f), 3.f, dot);
            draw_list->AddText(ImVec2(pos.x + 16.f, pos.y), text_color, text);
            return pill_max.x - pill_min.x;
        }


        void warning_text(const char* text) {

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 200, 110, 255));
            ImGui::TextWrapped("%s", text);
            ImGui::PopStyleColor();
        }


        void hint_text(const char* text) {

            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", text);
            ImGui::PopStyleColor();
        }

    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    template <typename T>
    static T read_packed(const std::vector<std::byte>& blob, u32 offset) {

        T v{};
        if (static_cast<std::size_t>(offset) + sizeof(T) <= blob.size())
            std::memcpy(&v, blob.data() + offset, sizeof(T));
        return v;
    }


    template <typename T>
    static void write_packed(std::vector<std::byte>& blob, u32 offset, const T& v) {

        if (static_cast<std::size_t>(offset) + sizeof(T) <= blob.size())
            std::memcpy(blob.data() + offset, &v, sizeof(T));
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    material_viewer_window::material_viewer_window() {

        m_slang_editor.SetLanguage(TextEditor::Language::Cpp());
        m_slang_editor.SetPalette(kSlangPalette);
        m_slang_editor.SetReadOnlyEnabled(false);

        open(std::filesystem::path{});
    }


    material_viewer_window::material_viewer_window(const std::filesystem::path& path) {

        m_slang_editor.SetLanguage(TextEditor::Language::Cpp());
        m_slang_editor.SetPalette(kSlangPalette);
        m_slang_editor.SetReadOnlyEnabled(false);

        open(path);
    }


    material_viewer_window::~material_viewer_window() { close_material(); }

    // CLASS PUBLIC ====================================================================================================

    void material_viewer_window::open(const std::filesystem::path& path) {

        close_material();

        m_material_path = path;
        m_material_name = path.empty() ? std::string("New Material") : path.filename().replace_extension("").string();
        m_load_failed = false;

        cache_window_state();
        make_window_name((std::string("MAT: ") + m_material_name).c_str());

        if (path.empty()) {
            build_new_material();
            m_slang_editor.SetText(m_edit_slang);
            m_show_window = true;
            return;
        }

        std::error_code error{};
        VALIDATE(GLT::vfs::exists(PROJECT_CONTENT_DIR / path, error) && !error, 
            m_load_failed = true; m_has_material = false; m_show_window = true; return, 
            "", "file not found [{}]", (path.generic_string()))

        VALIDATE(!GLT::vfs::is_directory(PROJECT_CONTENT_DIR / path, error) && !error,
            m_load_failed = true; m_has_material = false; m_show_window = true; return, 
            "", "path is a directory [{}]", (path.generic_string()))

        auto* registry = GLT::asset::registry::get_ptr();
        VALIDATE(registry, m_load_failed = true; m_show_window = true; return, "", "no asset registry")

        auto loaded = registry->load(path);
        VALIDATE(loaded, m_load_failed = true; m_show_window = true; return, "", "failed to load material [{}]", (path.generic_string()))

        if (!load_from_handle(std::move(*loaded))) {

            m_load_failed = true;
            m_show_window = true;
            return;
        }

        m_show_window = true;
    }


    void material_viewer_window::close_material() {

        m_asset_handle.reset();
        m_has_material = false;
        m_dirty = false;
        m_edit_slang.clear();
        m_baseline_slang.clear();
        m_edit_params.clear();
        m_baseline_params.clear();
        m_edit_blend = GLT::asset::material::blend_mode::opaque;
        m_baseline_blend = GLT::asset::material::blend_mode::opaque;
        m_edit_flags = 0;
        m_baseline_flags = 0;
        m_edit_textures.fill(INVALID_HANDLE);
        m_baseline_textures.fill(INVALID_HANDLE);
        m_reflection_blob.clear();
        m_bytecode_blob.clear();
        m_compiler_hash = 0;
        m_compiler_hash_matches = true;
        m_compile_error_message.clear();
    }


    void material_viewer_window::window(const f32 /*delta_time*/) {

        if (!m_show_window)
            return;

        apply_pending_dock();
        ImGui::SetNextWindowSizeConstraints(ImVec2(880.0f, 520.0f), ImVec2(std::numeric_limits<f32>::max(), std::numeric_limits<f32>::max()));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        const bool open = ImGui::Begin(m_window_id.c_str(), &m_show_window, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();

        if (open) {

            handle_shortcuts();
            draw_header_bar();
            draw_body();
        }
        ImGui::End();
    }


    void material_viewer_window::update(const f32 /*delta_time*/) { }


    bool material_viewer_window::serialize(const std::filesystem::path& /*project_file*/, const GLT::serializer::option /*option*/) {
        return false;
    }


    bool material_viewer_window::save() {

        if (!m_has_material)
            return false;

        if (!m_asset_handle.valid()) {
            LOG(warn, "no destination path for [{}] - cannot save", m_material_name);
            return false;
        }

        auto* registry = GLT::asset::registry::get_ptr();
        if (!registry)
            return false;

        auto* asset = registry->data_as<GLT::asset::material::material_asset>(m_asset_handle.get());
        if (!asset) {
            LOG(error, "asset for [{}] disappeared before save", m_material_name);
            return false;
        }

        asset->slang_source = m_edit_slang;
        asset->packed_params = m_edit_params;
        asset->blend = m_edit_blend;
        asset->flags = m_edit_flags;

        for (std::size_t i = 0; i < TEXTURE_SLOT_COUNT; ++i) {
            asset->textures[i] = m_edit_textures[i];
            if (m_edit_textures[i] != INVALID_HANDLE)
                registry->add_dependency(m_asset_handle, m_edit_textures[i]);
        }

        if (m_edit_slang != m_baseline_slang)
            asset->compiler_hash_matches = false;

        auto result = registry->save(m_asset_handle);
        if (!result) {
            LOG(error, "save failed for [{}] (error {})",
                m_material_name, static_cast<int>(result.error()));
            return false;
        }

        m_baseline_slang = m_edit_slang;
        m_baseline_params = m_edit_params;
        m_baseline_blend = m_edit_blend;
        m_baseline_flags = m_edit_flags;
        m_baseline_textures = m_edit_textures;

        m_compiler_hash_matches = asset->compiler_hash_matches;
        m_dirty = false;
        return true;
    }


    bool material_viewer_window::compile() {

        if (!m_has_material)
            return false;

        // The material compiler owns the compile + validate + repack pipeline. The editor only pushes the result
        // into the live asset, mirrors the derived artifacts back for display, and surfaces any diagnostic
        auto result = GLT::material_compiler::compile(m_edit_slang, m_edit_blend, m_reflection_blob, m_edit_params);

        // ---- Failure --------------------------------------------------------------------------------------------
        if (!result) {

            // "Already compiled" surfaces as a status, not an error
            if (result.error().shader_code == GLT::render::shader_compile_error_code::already_compiled) {

                m_compile_error_message.clear();
                append_compile_log(compile_log_level::success, "Already compiled - asset still holds a valid compile for this source.");
                return true;
            }

            m_compile_error_message = result.error().message;
            append_compile_log(compile_log_level::error, result.error().message);
            LOG(error, "material_viewer: compile failed for [{}] - {}", m_material_name, result.error().message);
            return false;
        }

        // ---- Success (no-op) ------------------------------------------------------------------------------------
        if (result->already_compiled) {

            m_compile_error_message.clear();
            append_compile_log(compile_log_level::success, "Already compiled - asset still holds a valid compile for this source.");
            return true;
        }

        // ---- Success (real) -------------------------------------------------------------------------------------

        m_compile_error_message.clear();

        auto* registry = GLT::asset::registry::get_ptr();
        VALIDATE(registry, return false, "", "asset registry unavailable")

        auto* asset = registry->data_as<GLT::asset::material::material_asset>(m_asset_handle);
        VALIDATE(asset, return false, "", "material asset for [{}] disappeared", (m_material_name))

        asset->slang_source = m_edit_slang;
        asset->blend = m_edit_blend;
        asset->flags = m_edit_flags;
        asset->bytecode = std::move(result->bytecode);
        asset->bytecode_hash = GLT::UUID{};                 // force a random hash in editor
        asset->reflection = std::move(result->reflection);
        asset->packed_params = std::move(result->packed_params);
        asset->compiler_hash = result->compiler_hash;
        asset->compiler_hash_matches = true;

        // Refresh the editor's mirrors
        m_edit_params = asset->packed_params;
        m_reflection_blob = asset->reflection;
        m_bytecode_blob = asset->bytecode;
        m_compiler_hash = asset->compiler_hash;
        m_compiler_hash_matches = true;

        // Compile is an in-memory operation - the file on disk is still stale
        mark_dirty();

        append_compile_log(compile_log_level::success, std::format("Compile succeeded - {} byte(s) bytecode, {} byte(s) reflection,"
            " {} byte(s) params, compiler hash 0x{:016X}", m_bytecode_blob.size(), m_reflection_blob.size(), m_edit_params.size(),
            m_compiler_hash));

        LOG(info, "compiled [{}] - {} byte(s) bytecode, {} param byte(s)",
            m_material_name, m_bytecode_blob.size(), m_edit_params.size());

        return true;
    }


    void material_viewer_window::revert() {

        m_edit_slang = m_baseline_slang;
        m_edit_params = m_baseline_params;
        m_edit_blend = m_baseline_blend;
        m_edit_flags = m_baseline_flags;
        m_edit_textures = m_baseline_textures;
        push_live_render_state();

        m_slang_editor.SetText(m_edit_slang);

        m_dirty = false;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    // top-level layout ------------------------------------------------------------------------------------------------

    // Ctrl+S saves, F7 compiles. Works while typing in the source editor too
    void material_viewer_window::handle_shortcuts() {

        if (!m_has_material || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            return;

        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && !io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false) && m_dirty)
            save();

        if (ImGui::IsKeyPressed(ImGuiKey_F7, false))
            compile();
    }


    // Header bar: material icon, name, unsaved marker, path | Revert, Compile, Save
    void material_viewer_window::draw_header_bar() {

        const f32 frame_h = ImGui::GetFrameHeight();
        const f32 bar_h = frame_h + 14.f;
        const ImVec2 bar_min = ImGui::GetCursorScreenPos();
        const f32 width = ImGui::GetContentRegionAvail().x;
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        draw_list->AddRectFilled(bar_min, ImVec2(bar_min.x + width, bar_min.y + bar_h), ImGui::GetColorU32(ImGuiCol_TitleBg));

        // buttons on the right
        const char* revert_label = "Revert";
        const char* compile_label = "Compile";
        const char* save_label = "Save";
        const f32 pad = ImGui::GetStyle().FramePadding.x * 4.f;
        const f32 revert_w = ImGui::CalcTextSize(revert_label).x + pad;
        const f32 compile_w = ImGui::CalcTextSize(compile_label).x + pad;
        const f32 save_w = ImGui::CalcTextSize(save_label).x + pad;
        const f32 buttons_x = bar_min.x + width - (revert_w + compile_w + save_w + 12.f) - 10.f;

        // icon + name + unsaved pill + path on the left
        const f32 icon = frame_h;
        const ImVec2 icon_min(bar_min.x + 10.f, bar_min.y + (bar_h - icon) * 0.5f);
        const auto icon_data = icon_manager::get(icon_manager::icon::material);
        draw_list->AddImage(icon_data.tex_ref, icon_min, ImVec2(icon_min.x + icon, icon_min.y + icon), icon_data.uv0, icon_data.uv1);

        f32 x = icon_min.x + icon + 8.f;
        const f32 text_y = bar_min.y + (bar_h - ImGui::GetTextLineHeight()) * 0.5f;
        const std::string name = (m_has_material || m_load_failed) ? m_material_name : std::string("No material");
        ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold));
        draw_list->AddText(ImVec2(x, text_y), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
        x += ImGui::CalcTextSize(name.c_str()).x + 10.f;
        ImGui::PopFont();

        if (m_dirty)
            x += status_pill(draw_list, ImVec2(x, text_y), "Unsaved changes", IM_COL32(230, 180, 60, 255), IM_COL32(230, 180, 60, 40),
                IM_COL32(230, 200, 120, 255)) + 10.f;

        std::string full_path;
        if (m_has_material || m_load_failed)
            full_path = m_material_path.empty() ? std::string("Not saved to disk yet") : m_material_path.generic_string();
        if (!full_path.empty() && x < buttons_x - 40.f) {
            std::string path = full_path;
            const f32 max_w = buttons_x - 10.f - x;
            while (path.size() > 4 && ImGui::CalcTextSize(path.c_str()).x > max_w)
                path.erase(0, 4);
            if (path.size() != full_path.size())
                path = "..." + path;
            draw_list->AddText(ImVec2(x, text_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), path.c_str());
        }

        ImGui::SetCursorScreenPos(ImVec2(buttons_x, bar_min.y + (bar_h - frame_h) * 0.5f));

        ImGui::BeginDisabled(!m_dirty || !m_has_material);
        if (UI::subtle_button(revert_label, ImVec2(revert_w, 0.f)))
            revert();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Throw away all changes since the last save");
        ImGui::EndDisabled();

        // Compile stays enabled without pending edits - it doubles as "recompile after the compiler changed", the way to get
        // rid of the "out of date" state in the Compiled Shader card
        ImGui::SameLine(0.f, 6.f);
        ImGui::BeginDisabled(!m_has_material);
        if (UI::subtle_button(compile_label, ImVec2(compile_w, 0.f)))
            compile();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Compile the Slang source (F7)");
        ImGui::EndDisabled();

        ImGui::SameLine(0.f, 6.f);
        ImGui::BeginDisabled(!m_dirty || !m_has_material);
        if (UI::accent_button(save_label, ImVec2(save_w, 0.f)))
            save();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Save to disk (Ctrl+S)");
        ImGui::EndDisabled();

        ImGui::SetCursorScreenPos(ImVec2(bar_min.x, bar_min.y + bar_h));
        ImGui::Dummy(ImVec2(0.f, 0.f));
    }


    // Details (preview + cards) | splitter | source editor + compile output
    void material_viewer_window::draw_body() {

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x < MIN_DETAILS_WIDTH || avail.y < 1.f)
            return;

        const ImVec2 start = ImGui::GetCursorScreenPos();
        const f32 max_details_w = std::max(MIN_DETAILS_WIDTH, avail.x - MIN_SOURCE_WIDTH - SPLITTER_WIDTH);
        m_details_width = std::clamp(m_details_width, MIN_DETAILS_WIDTH, max_details_w);

        // ---- details -------------------------------------------------------------------------------------------------
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 10.f));
        ImGui::BeginChild("##details", ImVec2(m_details_width, avail.y), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        draw_details_panel();
        ImGui::EndChild();

        // ---- splitter ------------------------------------------------------------------------------------------------
        ImGui::SetCursorScreenPos(ImVec2(start.x + m_details_width, start.y));
        splitter("##details_splitter", true, m_details_width, MIN_DETAILS_WIDTH, max_details_w, avail.y, false);

        // ---- source --------------------------------------------------------------------------------------------------
        ImGui::SetCursorScreenPos(ImVec2(start.x + m_details_width + SPLITTER_WIDTH, start.y));
        ImGui::BeginChild("##source", ImVec2(0.f, avail.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        draw_source_panel();
        ImGui::EndChild();
    }


    void material_viewer_window::draw_details_panel() {

        if (m_load_failed) {
            UI::empty_state("Failed to load the material", "See the log for details");
            return;
        }

        if (!m_has_material) {
            UI::empty_state("No material loaded", "Open a material from the Content Browser");
            return;
        }

        draw_preview_canvas();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));

        UI::begin_properties(UI::property_layout::compact);
        draw_blend_section();
        draw_parameters_section();
        draw_textures_section();
        draw_derived_section();
        UI::end_properties();
    }


    void material_viewer_window::draw_source_panel() {

        if (!m_has_material) {
            UI::empty_state("No source to show", "Open a material, or create a new one, to edit its Slang shader here");
            return;
        }

        constexpr f32 MIN_EDITOR_HEIGHT = 120.0f;
        constexpr f32 MIN_TERMINAL_HEIGHT = 90.0f;

        // ---- header strip ----------------------------------------------------------------------------------------------
        const ImVec2 strip_min = ImGui::GetCursorScreenPos();
        const f32 width = ImGui::GetContentRegionAvail().x;
        const f32 strip_h = panel_strip("Slang Source", strip_min, width);
        {
            const char* hint = "Source edits apply after a compile (F7)";
            const ImVec2 size = ImGui::CalcTextSize(hint);
            if (size.x + 160.f < width)
                ImGui::GetWindowDrawList()->AddText(ImVec2(strip_min.x + width - size.x - 12.f, strip_min.y + (strip_h - size.y) * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
        }
        ImGui::Dummy(ImVec2(width, strip_h));

        const ImVec2 avail = ImGui::GetContentRegionAvail();

        // Clamp the persisted terminal height so the editor always keeps its minimum, and vice versa
        const f32 max_terminal = std::max(MIN_TERMINAL_HEIGHT, avail.y - MIN_EDITOR_HEIGHT - SPLITTER_WIDTH);
        m_terminal_height = std::clamp(m_terminal_height, MIN_TERMINAL_HEIGHT, max_terminal);
        const f32 editor_height = std::max(1.f, avail.y - m_terminal_height - SPLITTER_WIDTH);

        // ---- editor ----------------------------------------------------------------------------------------------------
        ImGui::BeginChild("##slang_host", ImVec2(0, editor_height), ImGuiChildFlags_None);
        ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::monospace_regular));
        m_slang_editor.Render("##slang_editor");
        ImGui::PopFont();
        ImGui::EndChild();

        // ---- splitter --------------------------------------------------------------------------------------------------
        splitter("##source_splitter", false, m_terminal_height, MIN_TERMINAL_HEIGHT, max_terminal, avail.x, true);

        // ---- compile output --------------------------------------------------------------------------------------------
        draw_compile_terminal();

        // Sync back to the edit buffer so mark_dirty() / revert() see the change
        const std::string current = m_slang_editor.GetText();
        if (current != m_edit_slang) {
            m_edit_slang = current;
            mark_dirty();
        }
    }


    void material_viewer_window::draw_compile_terminal() {

        // ---- summary ---------------------------------------------------------------------------------------------------
        u32 errors = 0;
        u32 warnings = 0;
        const compile_log_entry* last_result = nullptr;         // newest error / success decides the status pill

        for (const auto& entry : m_compile_log) {
            if (entry.level == compile_log_level::error)
                ++errors;
            else if (entry.level == compile_log_level::warning)
                ++warnings;
            if (entry.level == compile_log_level::error || entry.level == compile_log_level::success)
                last_result = &entry;
        }

        // ---- header strip ----------------------------------------------------------------------------------------------
        const ImVec2 strip_min = ImGui::GetCursorScreenPos();
        const f32 width = ImGui::GetContentRegionAvail().x;
        const f32 strip_h = panel_strip("Output", strip_min, width);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        const f32 text_y = strip_min.y + (strip_h - ImGui::GetTextLineHeight()) * 0.5f;
        ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold));
        f32 x = strip_min.x + 12.f + ImGui::CalcTextSize("Output").x + 12.f;
        ImGui::PopFont();

        if (last_result) {
            const bool failed = last_result->level == compile_log_level::error;
            x += status_pill(draw_list, ImVec2(x, text_y), failed ? "Failed" : "Compiled",
                failed ? IM_COL32(230, 100, 100, 255) : IM_COL32(110, 200, 120, 255),
                failed ? IM_COL32(230, 100, 100, 40) : IM_COL32(110, 200, 120, 36),
                failed ? IM_COL32(240, 150, 150, 255) : IM_COL32(160, 220, 165, 255)) + 10.f;
        }

        if (errors > 0 || warnings > 0) {
            const std::string counts = std::format("{} error{}, {} warning{}", errors, errors == 1 ? "" : "s", warnings, warnings == 1 ? "" : "s");
            draw_list->AddText(ImVec2(x, text_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), counts.c_str());
        }

        const char* clear_label = "Clear";
        const f32 clear_w = ImGui::CalcTextSize(clear_label).x + ImGui::GetStyle().FramePadding.x * 4.f;
        ImGui::SetCursorScreenPos(ImVec2(strip_min.x + width - clear_w - 8.f, strip_min.y + (strip_h - ImGui::GetFrameHeight()) * 0.5f));
        ImGui::BeginDisabled(m_compile_log.empty());
        if (UI::subtle_button(clear_label, ImVec2(clear_w, 0.f)))
            clear_compile_log();
        ImGui::EndDisabled();

        ImGui::SetCursorScreenPos(ImVec2(strip_min.x, strip_min.y + strip_h));

        // ---- log -------------------------------------------------------------------------------------------------------
        ImVec4 log_bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        log_bg.x = std::max(0.f, log_bg.x - 0.025f);
        log_bg.y = std::max(0.f, log_bg.y - 0.025f);
        log_bg.z = std::max(0.f, log_bg.z - 0.025f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, log_bg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f, 8.f));
        ImGui::BeginChild("##compile_terminal", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        if (m_compile_log.empty()) {

            hint_text("Nothing compiled yet. Press Compile or F7 to build the material.");

        } else {

            ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::monospace_regular));
            const f32 line_h = ImGui::GetTextLineHeight();

            for (const auto& entry : m_compile_log) {

                ImU32 dot;
                ImVec4 color;
                switch (entry.level) {
                    case compile_log_level::error:      dot = IM_COL32(230, 100, 100, 255); color = ImVec4(0.94f, 0.55f, 0.55f, 1.0f); break;
                    case compile_log_level::warning:    dot = IM_COL32(230, 180,  60, 255); color = ImVec4(0.95f, 0.80f, 0.45f, 1.0f); break;
                    case compile_log_level::success:    dot = IM_COL32(110, 200, 120, 255); color = ImVec4(0.65f, 0.87f, 0.67f, 1.0f); break;
                    default:                            dot = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                                                        color = ImGui::GetStyleColorVec4(ImGuiCol_Text); break;
                }

                // colored dot in front of the message, wrapped lines stay aligned with the first one
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + 4.f, pos.y + line_h * 0.5f), 3.f, dot);
                ImGui::SetCursorScreenPos(ImVec2(pos.x + 14.f, pos.y));

                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextWrapped("%s", entry.text.c_str());
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0.f, 2.f));
            }

            ImGui::PopFont();

            if (m_scroll_terminal_to_bottom) {
                ImGui::SetScrollHereY(1.0f);
                m_scroll_terminal_to_bottom = false;
            }
        }

        ImGui::EndChild();
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Surface card (blend mode + flags)
    // -----------------------------------------------------------------------------------------------------------------

    void material_viewer_window::draw_blend_section() {

        using bm = GLT::asset::material::blend_mode;
        namespace mf = GLT::asset::material::material_flag;

        static constexpr const char* kBlendLabels[] = {
            "Opaque", "Masked", "Translucent", "Additive",
            "Modulate", "Alpha Composite", "Alpha Holdout"
        };
        static_assert(IM_ARRAYSIZE(kBlendLabels) == static_cast<std::size_t>(bm::count));
        static constexpr bm kDefaultBlend = bm::opaque;
        static constexpr bool kDefaultDoubleSided = false;

        if (UI::begin_card("Surface", UI::card_options{ .summary = blend_mode_label(m_edit_blend) })) {

            if (UI::property_combo("Blend mode", m_edit_blend, kBlendLabels, "How the surface is combined with what is behind it",
                &kDefaultBlend)) {
                push_live_render_state();
                mark_dirty();
            }

            // what the selected mode does, right under the combo
            hint_text(blend_mode_description(m_edit_blend));
            ImGui::Dummy(ImVec2(0.f, 2.f));

            bool double_sided = (m_edit_flags & mf::double_sided) != 0;
            if (UI::property_toggle("Double sided", double_sided, "Render back faces too (no back-face culling, back faces get a flipped normal)",
                &kDefaultDoubleSided)) {
                m_edit_flags = double_sided ? (m_edit_flags | mf::double_sided) : (m_edit_flags & ~mf::double_sided);
                push_live_render_state();
                mark_dirty();
            }
        }
        UI::end_card();
    }


    void material_viewer_window::push_live_render_state() {

        if (!m_has_material)
            return;

        auto* registry = GLT::asset::registry::get_ptr();
        if (!registry)
            return;

        if (auto* asset = registry->data_as<GLT::asset::material::material_asset>(m_asset_handle.get())) {
            asset->blend = m_edit_blend;
            asset->flags = m_edit_flags;
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Parameters card
    // -----------------------------------------------------------------------------------------------------------------

    void material_viewer_window::draw_parameters_section() {

        using namespace GLT::asset::material;

        const reflection_header* hdr = m_reflection_blob.size() >= sizeof(reflection_header)
            ? reinterpret_cast<const reflection_header*>(m_reflection_blob.data()) : nullptr;
        const bool valid = hdr && hdr->magic == REFLECT_SCHEMA_MAGIC;

        std::string summary;
        if (valid)
            summary = std::to_string(hdr->param_count);

        UI::card_options options{};
        options.description = "Fields of material_params. Changes show up live, no compile needed";
        options.summary = summary.empty() ? nullptr : summary.c_str();

        if (UI::begin_card("Parameters", options)) {

            if (!hdr) {
                hint_text("No parameters yet. Compile the material (F7) to read them from the source.");

            } else if (!valid) {
                warning_text(std::format("The parameter list is damaged (bad magic 0x{:08X}). Compile the material again.", hdr->magic).c_str());

            } else {

                if (hdr->contract_version != MATERIAL_CONTRACT_VERSION)
                    warning_text(std::format("Compiled against material contract v{}, the engine expects v{}. Compile again.",
                        hdr->contract_version, MATERIAL_CONTRACT_VERSION).c_str());

                if (hdr->param_count == 0) {
                    hint_text("material_params has no fields.");

                } else if (m_edit_params.size() != hdr->packed_param_size) {
                    warning_text(std::format("Parameter buffer does not match the source ({} vs {} bytes). Compile the material again.",
                        m_edit_params.size(), hdr->packed_param_size).c_str());

                } else {

                    const auto* params = reinterpret_cast<const reflection_param*>(m_reflection_blob.data() + hdr->param_table_offset);
                    const auto* strtab = reinterpret_cast<const char*>(m_reflection_blob.data() + hdr->string_table_offset);

                    for (u32 i = 0; i < hdr->param_count; ++i) {

                        ImGui::PushID(static_cast<int>(i));
                        if (draw_param_widget(strtab + params[i].name_offset, params[i])) {
                            mark_dirty();
                            push_live_params();
                        }
                        ImGui::PopID();
                    }
                }
            }
        }
        UI::end_card();
    }


    // One property row per parameter. [name] is the field name from the source; the row shows it prettified
    // ("base_color" -> "Base Color") with the original as tooltip. The reset arrow goes back to the saved value
    bool material_viewer_window::draw_param_widget(const char* name, const GLT::asset::material::reflection_param& p) {

        using pt = GLT::asset::material::param_type;

        const std::string label = UI::pretty_name(name);
        const bool has_saved = m_baseline_params.size() == m_edit_params.size();
        const u32 offset = p.byte_offset;
        bool changed = false;

        // reads value + saved value, draws [row], writes back on change
        auto edit = [&]<typename T>(std::type_identity<T>, auto&& row) {
            T value = read_packed<T>(m_edit_params, offset);
            const T saved = has_saved ? read_packed<T>(m_baseline_params, offset) : value;
            if (row(value, has_saved ? &saved : nullptr)) {
                write_packed<T>(m_edit_params, offset, value);
                changed = true;
            }
        };

        switch (p.type) {

            case pt::f32:
                edit(std::type_identity<f32>{}, [&](f32& v, const f32* d) {
                    return UI::property_drag(label.c_str(), v, 0.01f, 0.f, 0.f, "%.3f", name, d); });
                break;

            case pt::f32x2:
                edit(std::type_identity<glm::vec2>{}, [&](glm::vec2& v, const glm::vec2* d) {
                    return UI::property_drag(label.c_str(), v, 0.01f, 0.f, 0.f, "%.3f", name, d); });
                break;

            case pt::f32x3:
                edit(std::type_identity<glm::vec3>{}, [&](glm::vec3& v, const glm::vec3* d) {
                    return looks_like_color(name) ? UI::property_color(label.c_str(), v, name, d)
                                                  : UI::property_drag(label.c_str(), v, 0.01f, 0.f, 0.f, "%.3f", name, d); });
                break;

            case pt::f32x4:
                edit(std::type_identity<glm::vec4>{}, [&](glm::vec4& v, const glm::vec4* d) {
                    if (looks_like_color(name))
                        return UI::property_color(label.c_str(), v, true, name, d);
                    return UI::property_custom(label.c_str(), [&v](const f32 width) {
                            return UI::drag_axes("##v", &v.x, 4, 0.01f, 0.f, 0.f, "%.3f", width);
                        }, name, !d || *d == v, [&v, d]() { if (d) v = *d; });
                });
                break;

            case pt::i32:
                edit(std::type_identity<i32>{}, [&](i32& v, const i32* d) {
                    return UI::property_custom(label.c_str(), [&v](const f32 width) {
                            ImGui::SetNextItemWidth(width);
                            return ImGui::DragInt("##v", &v);
                        }, name, !d || *d == v, [&v, d]() { if (d) v = *d; });
                });
                break;

            case pt::u32:
                edit(std::type_identity<u32>{}, [&](u32& v, const u32* d) {
                    return UI::property_custom(label.c_str(), [&v](const f32 width) {
                            ImGui::SetNextItemWidth(width);
                            return ImGui::DragScalar("##v", ImGuiDataType_U32, &v);
                        }, name, !d || *d == v, [&v, d]() { if (d) v = *d; });
                });
                break;

            case pt::boolean:
                edit(std::type_identity<u32>{}, [&](u32& raw, const u32* d) {
                    bool value = raw != 0;
                    const bool saved = d ? (*d != 0) : value;
                    if (!UI::property_toggle(label.c_str(), value, name, d ? &saved : nullptr))
                        return false;
                    raw = value ? 1u : 0u;
                    return true;
                });
                break;

            default:
                UI::property_value(label.c_str(), "Matrix, not editable here yet", name);
                break;
        }

        return changed;
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Textures card
    // -----------------------------------------------------------------------------------------------------------------

    void material_viewer_window::draw_textures_section() {

        using slot = GLT::asset::material::texture_slot;

        struct row { const char* label; slot s; const char* desc; };
        static constexpr row rows[] = {
            { "Base color",         slot::base_color,           "Albedo, the surface color" },
            { "Metal / Rough",      slot::metallic_roughness,   "Metallic and roughness packed into one texture" },
            { "Normal",             slot::normal,               "Tangent space normal map for small surface detail" },
            { "Emissive",           slot::emissive,             "Light the surface gives off by itself" },
            { "Occlusion",          slot::occlusion,            "Ambient occlusion, darkens creases and corners" },
            { "Height",             slot::height,               "Height / displacement map" },
        };

        u32 assigned = 0;
        for (const auto& h : m_edit_textures)
            if (h != INVALID_HANDLE)
                ++assigned;
        const std::string summary = std::format("{} of {}", assigned, TEXTURE_SLOT_COUNT);

        if (UI::begin_card("Textures", UI::card_options{ .description = "Pick a texture per channel, or leave it empty",
            .summary = summary.c_str() })) {

            for (const auto& r : rows) {

                auto& handle = m_edit_textures[static_cast<std::size_t>(r.s)];
                const auto saved = m_baseline_textures[static_cast<std::size_t>(r.s)];

                const bool changed = UI::property_custom(r.label, [&](const f32 width) {
                        return UI::asset_picker_widget({ .label = r.label, .filter = GLT::asset::core_types::texture2d }, handle, width);
                    }, r.desc, handle == saved, [&]() { handle = saved; });

                if (changed)
                    mark_dirty();
            }
        }
        UI::end_card();
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Compiled shader card (read-only)
    // -----------------------------------------------------------------------------------------------------------------

    void material_viewer_window::draw_derived_section() {

        using namespace GLT::asset::material;

        const char* summary = m_bytecode_blob.empty() ? "Not compiled" : (m_compiler_hash_matches ? "Up to date" : "Out of date");

        if (UI::begin_card("Compiled Shader", UI::card_options{ .summary = summary, .collapsible = true, .default_open = false })) {

            if (!m_compiler_hash_matches) {
                warning_text("Compiled with a different compiler than the active renderer uses. The renderer recompiles "
                             "from the Slang source on the next load, press Compile to update the asset now.");
                ImGui::Dummy(ImVec2(0.f, 4.f));
            }

            UI::property_value("Compiler hash", std::format("0x{:016X}", m_compiler_hash), "Identifies the shader compiler the bytecode was built with");
            UI::property_value("Bytecode", std::format("{} bytes", m_bytecode_blob.size()));
            UI::property_value("Reflection", std::format("{} bytes", m_reflection_blob.size()), "Parameter layout read from the source");

            if (m_reflection_blob.size() >= sizeof(reflection_header)) {

                const auto* hdr = reinterpret_cast<const reflection_header*>(m_reflection_blob.data());
                if (hdr->magic == REFLECT_SCHEMA_MAGIC) {
                    UI::property_value("Schema version", std::to_string(hdr->schema_version));
                    UI::property_value("Contract version", std::to_string(hdr->contract_version));
                    UI::property_value("Parameters", std::to_string(hdr->param_count));
                }
            }
        }
        UI::end_card();
    }

    // -----------------------------------------------------------------------------------------------------------------
    // Preview
    // -----------------------------------------------------------------------------------------------------------------

    void material_viewer_window::draw_preview_canvas() {

        const f32 width = std::max(1.f, ImGui::GetContentRegionAvail().x);
        const f32 height = std::clamp(width * 0.75f, 120.f, PREVIEW_MAX_HEIGHT);

        const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
        const ImVec2 canvas_max(canvas_min.x + width, canvas_min.y + height);

        ImGui::InvisibleButton("##mat_preview", ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        handle_preview_input();

        ImDrawList* draw = ImGui::GetWindowDrawList();

        // Renderer preview --------------------------------------------------------------------------------------------
        // void* tex = nullptr;
        // if (m_has_material && m_asset_handle != INVALID_HANDLE) {
        //     if (auto renderer = GLT::render::renderer::get_ref())
        //         tex = renderer->render_material_preview(m_asset_handle, m_preview_camera_pos);
        // }
        //
        // if (tex) {
        //     draw->AddImageRounded(reinterpret_cast<ImTextureID>(tex), canvas_min, canvas_max, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 6.f);
        // } else {
        //     draw_preview_fallback(draw, canvas_min, canvas_max);
        // }

        draw_preview_fallback(draw, canvas_min, canvas_max);

        // controls hint only while the mouse is over the preview
        if (hovered) {
            const char* hint = "Drag to pan   Scroll to zoom   Double-click to reset";
            const ImVec2 size = ImGui::CalcTextSize(hint);
            if (size.x + 20.f < width) {
                const ImVec2 pos(canvas_min.x + (width - size.x) * 0.5f, canvas_max.y - size.y - 10.f);
                draw->AddRectFilled(ImVec2(pos.x - 8.f, pos.y - 3.f), ImVec2(pos.x + size.x + 8.f, pos.y + size.y + 3.f), IM_COL32(0, 0, 0, 120), 4.f);
                draw->AddText(pos, IM_COL32(220, 220, 225, 220), hint);
            }
        }
    }


    void material_viewer_window::mark_dirty() {

        m_dirty = (m_edit_slang    != m_baseline_slang)
               || (m_edit_params   != m_baseline_params)
               || (m_edit_blend    != m_baseline_blend)
               || (m_edit_flags    != m_baseline_flags)
               || (m_edit_textures != m_baseline_textures);
    }


    void material_viewer_window::push_live_params() {

        if (m_asset_handle == INVALID_HANDLE)
            return;     // new, unsaved material - there is no live asset yet

        auto* registry = GLT::asset::registry::get_ptr();
        if (!registry)
            return;

        auto* asset = registry->data_as<GLT::asset::material::material_asset>(m_asset_handle);
        if (!asset)
            return;

        asset->packed_params = m_edit_params;

        // Force the renderer to re-key its caches. The bytecode itself is still valid — same struct
        // layout, same shader — but the pipeline cache uses this hash as the identity of the material's
        // GPU state, so it needs a fresh value to allocate a new parameter buffer
        asset->bytecode_hash = GLT::UUID{};
    }


    void material_viewer_window::build_new_material() {

        m_edit_slang = DEFAULT_SLANG_SOURCE;
        m_baseline_slang = m_edit_slang;

        m_edit_params.clear();
        m_baseline_params.clear();

        m_edit_blend = GLT::asset::material::blend_mode::opaque;
        m_baseline_blend = m_edit_blend;
        m_edit_flags = 0;
        m_baseline_flags = 0;

        m_edit_textures.fill(INVALID_HANDLE);
        m_baseline_textures.fill(INVALID_HANDLE);

        m_reflection_blob.clear();
        m_bytecode_blob.clear();
        m_compiler_hash = 0;
        m_compiler_hash_matches = false;
        m_compile_error_message.clear();
        clear_compile_log();

        m_dirty = false;
        m_has_material = true;
        m_load_failed = false;
        m_asset_handle = INVALID_HANDLE;

        m_preview_camera_pos = { 0.0f, 0.0f, 3.0f };
        m_preview_target = { 0.0f, 0.0f, 0.0f };
    }


    bool material_viewer_window::load_from_handle(GLT::asset::handle h) {

        auto* registry = GLT::asset::registry::get_ptr();
        if (!registry)
            return false;

        auto* asset = registry->data_as<GLT::asset::material::material_asset>(h);
        if (!asset)
            return false;

        m_asset_handle = h;

        m_edit_slang = asset->slang_source;
        if (m_edit_slang.empty())
            m_edit_slang = DEFAULT_SLANG_SOURCE;

        m_baseline_slang = m_edit_slang;

        m_edit_params = asset->packed_params;
        m_baseline_params = m_edit_params;

        m_edit_blend = asset->blend;
        m_baseline_blend = m_edit_blend;
        m_edit_flags = asset->flags;
        m_baseline_flags = m_edit_flags;

        for (std::size_t i = 0; i < TEXTURE_SLOT_COUNT; ++i)
            m_edit_textures[i] = asset->textures[i];
        m_baseline_textures = m_edit_textures;

        m_reflection_blob = asset->reflection;
        m_bytecode_blob = asset->bytecode;
        m_compiler_hash = asset->compiler_hash;
        m_compiler_hash_matches = asset->compiler_hash_matches;
        m_compile_error_message.clear();

        m_slang_editor.SetText(m_edit_slang);

        m_dirty = false;
        m_has_material = true;
        m_load_failed = false;

        m_preview_camera_pos = { 0.0f, 0.0f, 3.0f };
        m_preview_target = { 0.0f, 0.0f, 0.0f };
        return true;
    }


    void material_viewer_window::handle_preview_input() {

        ImGuiIO& io = ImGui::GetIO();

        const f32 dist = glm::length(m_preview_camera_pos);
        if (dist < 1e-4f)
            return;

        const glm::vec3 forward = -m_preview_camera_pos / dist;

        glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
        if (glm::dot(right, right) < 1e-6f)
            right = glm::vec3(1.0f, 0.0f, 0.0f);
        else
            right = glm::normalize(right);
        const glm::vec3 up = glm::normalize(glm::cross(right, forward));

        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {

            const f32 pan_speed = dist * 0.0035f;
            const glm::vec3 delta = (-right * io.MouseDelta.x + up * io.MouseDelta.y) * pan_speed;
            m_preview_camera_pos += delta;
        }

        if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {

            const f32 factor = std::pow(0.9f, io.MouseWheel);
            const f32 new_dist = std::clamp(dist * factor, 0.5f, 50.0f);
            m_preview_camera_pos = glm::normalize(m_preview_camera_pos) * new_dist;
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_preview_camera_pos = { 0.0f, 0.0f, 3.0f };
            m_preview_target = { 0.0f, 0.0f, 0.0f };
        }
    }


    // Until the renderer can draw a material preview: a shaded sphere tinted with the material's "base_color" parameter and
    // a highlight that gets sharper with lower "roughness" / stronger with "metallic". Zooming scales the sphere
    void material_viewer_window::draw_preview_fallback(ImDrawList* draw, const ImVec2& min, const ImVec2& max) {

        using namespace GLT::asset::material;

        // background
        ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        bg.x = std::max(0.f, bg.x - 0.04f);
        bg.y = std::max(0.f, bg.y - 0.04f);
        bg.z = std::max(0.f, bg.z - 0.04f);
        draw->AddRectFilled(min, max, ImGui::GetColorU32(bg), 6.f);
        draw->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border, 0.6f), 6.f);

        if (!m_has_material)
            return;

        // read the few parameters the approximation cares about (only when the parameter buffer is usable)
        glm::vec3 base_color{ 0.8f };
        f32 roughness = 0.5f;
        f32 metallic = 0.f;

        if (m_reflection_blob.size() >= sizeof(reflection_header)) {

            const auto* hdr = reinterpret_cast<const reflection_header*>(m_reflection_blob.data());
            if (hdr->magic == REFLECT_SCHEMA_MAGIC && m_edit_params.size() == hdr->packed_param_size) {

                const auto* params = reinterpret_cast<const reflection_param*>(m_reflection_blob.data() + hdr->param_table_offset);
                const auto* strtab = reinterpret_cast<const char*>(m_reflection_blob.data() + hdr->string_table_offset);

                for (u32 i = 0; i < hdr->param_count; ++i) {

                    const std::string_view name = strtab + params[i].name_offset;
                    const auto type = params[i].type;
                    if (name == "base_color" && (type == param_type::f32x3 || type == param_type::f32x4))
                        base_color = read_packed<glm::vec3>(m_edit_params, params[i].byte_offset);
                    else if (name == "roughness" && type == param_type::f32)
                        roughness = read_packed<f32>(m_edit_params, params[i].byte_offset);
                    else if (name == "metallic" && type == param_type::f32)
                        metallic = read_packed<f32>(m_edit_params, params[i].byte_offset);
                }
            }
        }

        base_color = glm::clamp(base_color, glm::vec3(0.f), glm::vec3(1.f));
        roughness = std::clamp(roughness, 0.f, 1.f);
        metallic = std::clamp(metallic, 0.f, 1.f);

        // sphere: stacked circles from the dark rim towards the lit top-left
        const f32 distance = std::max(0.5f, glm::length(m_preview_camera_pos));
        const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        const f32 radius = std::min(max.x - min.x, max.y - min.y) * 0.34f * (3.f / distance);
        const ImVec2 light_dir(-0.35f, -0.4f);

        draw->PushClipRect(min, max, true);

        // soft contact shadow under the sphere
        for (int i = 0; i < 4; ++i) {
            const f32 s = 1.f - static_cast<f32>(i) * 0.2f;
            draw->AddEllipseFilled(ImVec2(center.x, center.y + radius * 1.04f), ImVec2(radius * 0.85f * s, radius * 0.14f * s), IM_COL32(0, 0, 0, 28), 0.f, 48);
        }

        constexpr int STEPS = 24;
        for (int i = 0; i < STEPS; ++i) {

            const f32 t = static_cast<f32>(i) / static_cast<f32>(STEPS - 1);
            const f32 r = radius * (1.f - t * 0.82f);
            const ImVec2 c(center.x + light_dir.x * radius * t * 0.8f, center.y + light_dir.y * radius * t * 0.8f);
            const f32 light = 0.18f + 0.82f * std::pow(t, 0.8f);
            const glm::vec3 color = glm::clamp(base_color * light * (1.f - metallic * 0.35f), glm::vec3(0.f), glm::vec3(1.f));
            draw->AddCircleFilled(c, r, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 1.f)), 48);
        }

        // highlight: small + bright when smooth, wide + faint when rough. Metals tint it with the base color
        const f32 spec_radius = radius * (0.08f + roughness * 0.35f);
        const f32 spec_alpha = (1.f - roughness) * 0.85f + 0.08f;
        const glm::vec3 spec = glm::mix(glm::vec3(1.f), base_color, metallic);
        const ImVec2 spec_center(center.x + light_dir.x * radius * 0.9f, center.y + light_dir.y * radius * 0.9f);
        for (int i = 0; i < 6; ++i) {
            const f32 t = static_cast<f32>(i) / 5.f;
            draw->AddCircleFilled(spec_center, spec_radius * (1.f - t * 0.7f), ImGui::GetColorU32(ImVec4(spec.x, spec.y, spec.z, spec_alpha * 0.25f)), 32);
        }

        draw->PopClipRect();

        // be honest about what this is
        const char* label = "Approximate preview";
        draw->AddText(ImVec2(min.x + 10.f, min.y + 8.f), ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
    }


    void material_viewer_window::append_compile_log(compile_log_level level, std::string text) {

        m_compile_log.push_back({ level, std::move(text) });

        constexpr std::size_t MAX_LOG_ENTRIES = 200;
        if (m_compile_log.size() > MAX_LOG_ENTRIES)
            m_compile_log.erase(m_compile_log.begin(), m_compile_log.begin() + (m_compile_log.size() - MAX_LOG_ENTRIES));

        m_scroll_terminal_to_bottom = true;
    }


    void material_viewer_window::clear_compile_log() {

        m_compile_log.clear();
        m_scroll_terminal_to_bottom = false;
    }

}
