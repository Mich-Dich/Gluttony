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
#include "util/ui/pannel_collection.h"
#include "util/ui/asset_picker.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr f32                                   DETAILS_PANEL_WIDTH = 430.0f;

    constexpr f32                                   PREVIEW_MAX_SIDE = 420.0f;

    constexpr const char*                           TABLE_NAME = "material_viewer_table";

    constexpr f32                                   TABLE_LABEL_RATIO = 0.38f;

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

        auto registry = GLT::asset::registry::get_ref();
        VALIDATE(registry, m_load_failed = true; m_show_window = true; return, "", "no asset registry")

        auto loaded = registry->load(path);
        VALIDATE(loaded, m_load_failed = true; m_show_window = true; return, "", "failed to load material [{}]", (path.generic_string()))

        if (!load_from_handle(*loaded)) {
            registry->unload(*loaded);
            m_load_failed = true;
        }

        m_show_window = true;
    }


    void material_viewer_window::close_material() {

        if (m_asset_handle != INVALID_HANDLE) {
            if (auto registry = GLT::asset::registry::get_ref())
                registry->unload(m_asset_handle);
            m_asset_handle = INVALID_HANDLE;
        }

        m_has_material = false;
        m_dirty = false;

        m_edit_slang.clear();
        m_baseline_slang.clear();
        m_edit_params.clear();
        m_baseline_params.clear();
        m_edit_blend = GLT::asset::material::blend_mode::opaque;
        m_baseline_blend = GLT::asset::material::blend_mode::opaque;

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

        if (ImGui::Begin(m_window_id.c_str(), &m_show_window)) {

            GLT::UI::custom_frame(DETAILS_PANEL_WIDTH, true, ImGui::GetColorU32(GLT::imgui_config::get_default_gray1_ref()),
                [this]() {
                    draw_details_panel();
                },
                [this]() {
                    draw_source_panel();
                });
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

        if (m_asset_handle == INVALID_HANDLE) {
            LOG(warn, "no destination path for [{}] - cannot save", m_material_name);
            return false;
        }

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return false;

        auto* asset = registry->data_as<GLT::asset::material::material_asset>(m_asset_handle);
        if (!asset) {
            LOG(error, "asset for [{}] disappeared before save", m_material_name);
            return false;
        }

        asset->slang_source = m_edit_slang;
        asset->packed_params = m_edit_params;
        asset->blend = m_edit_blend;

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

        auto registry = GLT::asset::registry::get_ref();
        VALIDATE(registry, return false, "", "asset registry unavailable")

        auto* asset = registry->data_as<GLT::asset::material::material_asset>(m_asset_handle);
        VALIDATE(asset, return false, "", "material asset for [{}] disappeared", (m_material_name))

        asset->slang_source = m_edit_slang;
        asset->blend = m_edit_blend;
        asset->bytecode = std::move(result->bytecode);
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

        append_compile_log(compile_log_level::info, std::format("Compile succeeded - {} byte(s) bytecode, {} byte(s) reflection,"
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
        m_edit_textures = m_baseline_textures;

        m_slang_editor.SetText(m_edit_slang);

        m_dirty = false;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void material_viewer_window::draw_details_panel() {

        // ---- header: name + path --------------------------------------------------------------------------------
        const std::string title = m_material_name.empty() ? std::string("(no material)") : m_material_name;
        ImGui::TextUnformatted(title.c_str());

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (m_material_path.empty())
            ImGui::TextUnformatted("(unsaved)");
        else
            ImGui::TextWrapped("%s", m_material_path.generic_string().c_str());
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0.0f, 6.0f));

        // ---- action bar: always visible at the top ---------------------------------------------------------------

        ImGui::BeginDisabled(!m_dirty || !m_has_material);

        if (ImGui::Button("Save", ImVec2(100.0f, 0.0f)))
            save();

        ImGui::SameLine();
        if (ImGui::Button("Revert", ImVec2(100.0f, 0.0f)))
            revert();

        ImGui::EndDisabled();

        // Compile stays enabled whenever a material is open, even without pending edits - it doubles as
        // "force a recompile after the compiler hash changed", which is the primary way to recover from
        // the yellow "bytecode does not match" warning shown in the Derived section.
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_has_material);

        if (ImGui::Button("Compile", ImVec2(100.0f, 0.0f)))
            compile();

        ImGui::EndDisabled();

        if (m_dirty) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 200, 90, 255));
            ImGui::TextUnformatted("Unsaved changes");
            ImGui::PopStyleColor();
        }

        // ---- state: nothing to scroll if we have no usable material ---------------------------------------------
        if (m_load_failed) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 90, 90, 255));
            ImGui::TextUnformatted("Failed to load material.");
            ImGui::PopStyleColor();
            return;
        }

        if (!m_has_material) {
            ImGui::TextDisabled("No material loaded.");
            return;
        }

        // ---- scroll region: everything below the action bar ------------------------------------------------------
        if (ImGui::BeginChild("##details_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_None)) {

            draw_preview_canvas();
            ImGui::Dummy(ImVec2(0.0f, 12.0f));

            draw_blend_section();
            draw_parameters_section();
            draw_textures_section();
            draw_derived_section();
        }
        ImGui::EndChild();
    }


    void material_viewer_window::draw_source_panel() {

        if (!m_has_material) {
            ImGui::TextDisabled("Open a .glt_material, or create a new one, to edit its Slang source here.");
            return;
        }

        constexpr f32 SPLITTER_THICKNESS = 6.0f;
        constexpr f32 MIN_EDITOR_HEIGHT = 120.0f;
        constexpr f32 MIN_TERMINAL_HEIGHT = 80.0f;

        const ImVec2 avail = ImGui::GetContentRegionAvail();

        // Clamp the persisted terminal height so the editor always keeps its minimum, and vice versa
        m_terminal_height = std::clamp(m_terminal_height, MIN_TERMINAL_HEIGHT,
            std::max(MIN_TERMINAL_HEIGHT, avail.y - MIN_EDITOR_HEIGHT - SPLITTER_THICKNESS));

        f32 editor_height = avail.y - m_terminal_height - SPLITTER_THICKNESS;

        // slang editor

        ImGui::BeginChild("##slang_host", ImVec2(0, editor_height), false);

        ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::monospace_regular));
        m_slang_editor.Render("##slang_editor");
        ImGui::PopFont();

        ImGui::EndChild();

        // splitter
        {
            const ImVec2 item_min = ImGui::GetItemRectMin();
            const ImVec2 item_max = ImGui::GetItemRectMax();
            ImRect bb(ImVec2(item_min.x, item_max.y), ImVec2(item_max.x, item_max.y + SPLITTER_THICKNESS));
            ImGui::SplitterBehavior(bb, ImGui::GetID("##source_splitter"), ImGuiAxis_Y, &editor_height, &m_terminal_height,
                MIN_EDITOR_HEIGHT, MIN_TERMINAL_HEIGHT, 2.0f, 0.0f, ImGui::GetColorU32(ImGuiCol_Separator));
        }

        // compile output

        draw_compile_terminal();

        // Sync back to the edit buffer so mark_dirty() / revert() see the change.
        const std::string current = m_slang_editor.GetText();
        if (current != m_edit_slang) {
            m_edit_slang = current;
            mark_dirty();
        }
    }


        void material_viewer_window::draw_compile_terminal() {

        // Fixed header: label on the left, Clear on the right
        if (ImGui::BeginTable("##compile_terminal_header", 2, ImGuiTableFlags_SizingStretchProp)) {

            ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("##btn",   ImGuiTableColumnFlags_WidthFixed);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Compile Output");

            ImGui::TableSetColumnIndex(1);
            if (ImGui::SmallButton("Clear"))
                clear_compile_log();

            ImGui::EndTable();
        }

        // Scrolling log body
        ImGui::BeginChild("##compile_terminal", ImVec2(0, 0), true);

        if (m_compile_log.empty()) {

            ImGui::TextDisabled("No compile output yet. Press Compile to build the material.");
        } else {

            ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::monospace_regular));

            for (const auto& entry : m_compile_log) {

                ImVec4 color;
                const char* prefix;

                switch (entry.level) {
                    case compile_log_level::error:      color = ImVec4(0.90f, 0.40f, 0.40f, 1.0f); prefix = "[ERROR] "; break;
                    case compile_log_level::warning:    color = ImVec4(0.95f, 0.75f, 0.35f, 1.0f); prefix = "[WARN]  "; break;
                    case compile_log_level::success:    color = ImVec4(0.55f, 0.85f, 0.55f, 1.0f); prefix = "[OK]    "; break;
                    default:                            color = ImVec4(0.70f, 0.75f, 0.70f, 1.0f); prefix = "[INFO]  "; break;
                }

                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextWrapped("%s%s", prefix, entry.text.c_str());
                ImGui::PopStyleColor();
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
    // Blend section
    // -----------------------------------------------------------------------------------------------------------------

    void material_viewer_window::draw_blend_section() {

        if (!ImGui::CollapsingHeader("Blend", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        using bm = GLT::asset::material::blend_mode;

        static constexpr const char* kBlendLabels[] = {
            "Opaque", "Masked", "Translucent", "Additive",
            "Modulate", "Alpha Composite", "Alpha Holdout"
        };
        static_assert(IM_ARRAYSIZE(kBlendLabels) == static_cast<std::size_t>(bm::count));

        i32 current = static_cast<i32>(m_edit_blend);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##blend_mode", &current, kBlendLabels, IM_ARRAYSIZE(kBlendLabels))) {
            m_edit_blend = static_cast<bm>(current);
            mark_dirty();
        }

        if (m_edit_blend == bm::masked)
            ImGui::TextDisabled("Masked materials must declare alpha_test().");
    }


    // -----------------------------------------------------------------------------------------------------------------
    // Parameters section
    // -----------------------------------------------------------------------------------------------------------------

    void material_viewer_window::draw_parameters_section() {

        if (!ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        using namespace GLT::asset::material;

        if (m_reflection_blob.size() < sizeof(reflection_header)) {
            ImGui::TextDisabled("No reflection schema yet.");
            ImGui::TextDisabled("Save once so material_compiler can produce one.");
            return;
        }

        const auto* hdr = reinterpret_cast<const reflection_header*>(m_reflection_blob.data());
        if (hdr->magic != REFLECT_SCHEMA_MAGIC) {
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f),
                "Reflection schema has bad magic (0x%08X).", hdr->magic);
            return;
        }

        if (hdr->contract_version != MATERIAL_CONTRACT_VERSION) {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                "Reflection was compiled against contract v%u; engine expects v%u.",
                hdr->contract_version, MATERIAL_CONTRACT_VERSION);
        }

        if (hdr->param_count == 0) {
            ImGui::TextDisabled("This material declares no parameters.");
            return;
        }

        if (m_edit_params.size() != hdr->packed_param_size) {
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f),
                "Param buffer mismatch (%zu vs %u) - material is not editable.",
                m_edit_params.size(), hdr->packed_param_size);
            return;
        }

        const auto* params = reinterpret_cast<const reflection_param*>(m_reflection_blob.data() + hdr->param_table_offset);
        const auto* strtab = reinterpret_cast<const char*>(m_reflection_blob.data() + hdr->string_table_offset);

        if (!GLT::UI::begin_table(TABLE_NAME, false, ImVec2(0, 0), 0.0f, true, TABLE_LABEL_RATIO))
            return;

        for (u32 i = 0; i < hdr->param_count; ++i) {

            const auto& p = params[i];
            const char* name = strtab + p.name_offset;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(name);

            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(static_cast<int>(i));
            if (draw_param_widget("##v", p))
                mark_dirty();
            ImGui::PopID();
        }

        GLT::UI::end_table();

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::TextDisabled("Editing the source or the parameter list requires a recompile.");
    }


    bool material_viewer_window::draw_param_widget(const char* label, const GLT::asset::material::reflection_param& p) {

        using pt = GLT::asset::material::param_type;
        bool changed = false;

        ImGui::SetNextItemWidth(-1.0f);

        switch (p.type) {

            case pt::f32: {
                f32 v = read_packed<f32>(m_edit_params, p.byte_offset);
                if (ImGui::DragFloat(label, &v, 0.01f)) {
                    write_packed<f32>(m_edit_params, p.byte_offset, v);
                    changed = true;
                }
                break;
            }

            case pt::f32x2: {
                f32 v[2];
                std::memcpy(v, m_edit_params.data() + p.byte_offset, sizeof(v));
                if (ImGui::DragFloat2(label, v, 0.01f)) {
                    std::memcpy(m_edit_params.data() + p.byte_offset, v, sizeof(v));
                    changed = true;
                }
                break;
            }

            case pt::f32x3: {
                f32 v[3];
                std::memcpy(v, m_edit_params.data() + p.byte_offset, sizeof(v));
                if (ImGui::ColorEdit3(label, v, ImGuiColorEditFlags_Float)) {
                    std::memcpy(m_edit_params.data() + p.byte_offset, v, sizeof(v));
                    changed = true;
                }
                break;
            }

            case pt::f32x4: {
                f32 v[4];
                std::memcpy(v, m_edit_params.data() + p.byte_offset, sizeof(v));
                if (ImGui::ColorEdit4(label, v, ImGuiColorEditFlags_Float)) {
                    std::memcpy(m_edit_params.data() + p.byte_offset, v, sizeof(v));
                    changed = true;
                }
                break;
            }

            case pt::i32: {
                i32 v = read_packed<i32>(m_edit_params, p.byte_offset);
                if (ImGui::DragInt(label, &v)) {
                    write_packed<i32>(m_edit_params, p.byte_offset, v);
                    changed = true;
                }
                break;
            }

            case pt::u32: {
                u32 v = read_packed<u32>(m_edit_params, p.byte_offset);
                if (ImGui::DragScalar(label, ImGuiDataType_U32, &v)) {
                    write_packed<u32>(m_edit_params, p.byte_offset, v);
                    changed = true;
                }
                break;
            }

            case pt::boolean: {
                u32 raw = read_packed<u32>(m_edit_params, p.byte_offset);
                bool b = raw != 0;
                if (ImGui::Checkbox(label, &b)) {
                    write_packed<u32>(m_edit_params, p.byte_offset, b ? 1u : 0u);
                    changed = true;
                }
                break;
            }

            default: {
                ImGui::TextDisabled("(matrix types are not editable here yet)");
                break;
            }
        }

        return changed;
    }


    // -----------------------------------------------------------------------------------------------------------------
    // Textures section
    // -----------------------------------------------------------------------------------------------------------------

    void material_viewer_window::draw_textures_section() {

        if (!ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        using slot = GLT::asset::material::texture_slot;

        if (!GLT::UI::begin_table(TABLE_NAME, false, ImVec2(0, 0), 0.0f, true, TABLE_LABEL_RATIO))
            return;

        struct row { const char* label; slot s; };
        constexpr row rows[] = {
            { "Base color",         slot::base_color},
            { "Metallic/Roughness", slot::metallic_roughness},
            { "Normal",             slot::normal},
            { "Emissive",           slot::emissive},
            { "Occlusion",          slot::occlusion},
            { "Height",             slot::height},
        };

        for (const auto& r : rows) {
            auto& h = m_edit_textures[static_cast<std::size_t>(r.s)];
            if (GLT::UI::table_row_asset_picker(r.label, h, GLT::asset::core_types::texture2d))
                mark_dirty();
        }

        GLT::UI::end_table();
    }


    // -----------------------------------------------------------------------------------------------------------------
    // Derived section (read-only)
    // -----------------------------------------------------------------------------------------------------------------

    void material_viewer_window::draw_derived_section() {

        if (!ImGui::CollapsingHeader("Derived"))
            return;

        using namespace GLT::asset::material;

        if (!GLT::UI::begin_table(TABLE_NAME, false, ImVec2(0, 0), 0.0f, true, TABLE_LABEL_RATIO))
            return;

        char buf[64];

        std::snprintf(buf, sizeof(buf), "0x%016llX", static_cast<unsigned long long>(m_compiler_hash));
        GLT::UI::table_row("compiler hash", std::string_view{ buf });

        std::snprintf(buf, sizeof(buf), "%zu bytes", m_bytecode_blob.size());
        GLT::UI::table_row("bytecode", std::string_view{ buf });

        std::snprintf(buf, sizeof(buf), "%zu bytes", m_reflection_blob.size());
        GLT::UI::table_row("reflection", std::string_view{ buf });

        if (m_reflection_blob.size() >= sizeof(reflection_header)) {

            const auto* hdr = reinterpret_cast<const reflection_header*>(m_reflection_blob.data());
            if (hdr->magic == REFLECT_SCHEMA_MAGIC) {

                std::snprintf(buf, sizeof(buf), "%u", hdr->schema_version);
                GLT::UI::table_row("schema version", std::string_view{ buf });

                std::snprintf(buf, sizeof(buf), "%u", hdr->contract_version);
                GLT::UI::table_row("contract version", std::string_view{ buf });

                std::snprintf(buf, sizeof(buf), "%u", hdr->param_count);
                GLT::UI::table_row("parameters", std::string_view{ buf });
            }
        }

        GLT::UI::end_table();

        ImGui::Dummy(ImVec2(0.0f, 6.0f));

        if (m_compiler_hash_matches) {
            ImGui::TextDisabled("Bytecode matches the active renderer's compiler.");
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 200, 90, 255));
            ImGui::TextWrapped("Bytecode does not match the active compiler. "
                               "The renderer will recompile from the Slang source on the next load.");
            ImGui::PopStyleColor();
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::TextDisabled("Blend: %s", blend_mode_label(m_edit_blend));
    }


    // -----------------------------------------------------------------------------------------------------------------
    // Preview
    // -----------------------------------------------------------------------------------------------------------------

    void material_viewer_window::draw_preview_canvas() {

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const f32 preview_side = std::min(avail.x, PREVIEW_MAX_SIDE);

        const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
        const ImVec2 canvas_max(canvas_min.x + preview_side, canvas_min.y + preview_side);

        ImGui::InvisibleButton("##mat_preview", ImVec2(preview_side, preview_side));
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
        //     draw->AddImage(reinterpret_cast<ImTextureID>(tex), canvas_min, canvas_max, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
        //     draw->AddRect(canvas_min, canvas_max, IM_COL32(60, 60, 70, 255), 0.0f, 0, 1.0f);
        // } else {
        //     draw_preview_fallback(draw, canvas_min, canvas_max);
        // }

        draw_preview_fallback(draw, canvas_min, canvas_max);

        draw->AddText(ImVec2(canvas_min.x + 10.0f, canvas_max.y - 20.0f), IM_COL32(180, 180, 190, 150),
            "drag = translate     scroll = zoom     double-click = reset");
    }


    void material_viewer_window::mark_dirty() {

        m_dirty = (m_edit_slang    != m_baseline_slang)
               || (m_edit_params   != m_baseline_params)
               || (m_edit_blend    != m_baseline_blend)
               || (m_edit_textures != m_baseline_textures);
    }


    void material_viewer_window::build_new_material() {

        m_edit_slang = DEFAULT_SLANG_SOURCE;
        m_baseline_slang = m_edit_slang;

        m_edit_params.clear();
        m_baseline_params.clear();

        m_edit_blend = GLT::asset::material::blend_mode::opaque;
        m_baseline_blend = m_edit_blend;

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

        auto registry = GLT::asset::registry::get_ref();
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


    void material_viewer_window::draw_preview_fallback(ImDrawList* draw, const ImVec2& min, const ImVec2& max) {

        draw->AddRectFilled(min, max, IM_COL32(28, 28, 32, 255));
        draw->AddRect(min, max, IM_COL32(80, 80, 90, 255), 0.0f, 0, 1.0f);

        const char* msg = m_has_material ? "Preview unavailable (renderer hook not wired)" : "No material loaded";
        const ImVec2 s = ImGui::CalcTextSize(msg);
        draw->AddText(ImVec2((min.x + max.x - s.x) * 0.5f, (min.y + max.y - s.y) * 0.5f), IM_COL32(180, 180, 180, 220), msg);
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
