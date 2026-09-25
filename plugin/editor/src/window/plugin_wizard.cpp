#include "util/pch.h"
#include "plugin_wizard.h"

#include <plugin_system/i_plugin.h>
#include <config/config.h>
#include <config/imgui_config.h>
#include <util/system.h>

#include "util/ui/pannel_collection.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr f32                           SETTINGS_PANEL_WIDTH    = 450.0f;

    constexpr const char*                   ADD_DEP_POPUP_ID        = "##pw_add_dependency_popup";

    constexpr const char*                   ADD_VENDOR_POPUP_ID     = "##pw_add_vendor_popup";

    const std::string                       BUILD_FILE_NAME("CMakeLists.txt");

    const std::string                       ENTRY_FILE_NAME("entry_point.cpp");

    constexpr static TextEditor::Palette    dark_style = {{
		IM_COL32(224, 224, 224, 255),	// text
		IM_COL32(197, 134, 192, 255),	// keyword
		IM_COL32( 90, 179, 155, 255),	// declaration
		IM_COL32(181, 206, 168, 255),	// number
		IM_COL32(206, 145, 120, 255),	// string
		IM_COL32(255, 255, 153, 255),	// punctuation
		IM_COL32( 64, 192, 128, 255),	// preprocessor
		IM_COL32(156, 220, 254, 255),	// identifier
		IM_COL32( 79, 193, 255, 255),	// known identifier
		IM_COL32(106, 153,  85, 255),	// comment
		IM_COL32( 30,  30,  30, 255),	// background
		IM_COL32(224, 224, 224, 255),	// cursor
		IM_COL32( 32,  96, 160, 255),	// selection
		IM_COL32(  0,   0,   0,   0),	// whitespace
		IM_COL32( 70,  70,  70, 255),	// matchingBracketBackground
		IM_COL32(140, 140, 140, 255),	// matchingBracketActive
		IM_COL32(246, 222,  36, 255),	// matchingBracketLevel1
		IM_COL32( 66, 120, 198, 255),	// matchingBracketLevel2
		IM_COL32(213,  96, 213, 255),	// matchingBracketLevel3
		IM_COL32(198,   8,  32, 255),	// matchingBracketError
		IM_COL32(128, 128, 144, 255),	// line number
		IM_COL32(224, 224, 240, 255),	// current line number
	}};

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // Builds (once) the display-name list for an enum via the reflection helpers.
    // The returned reference stays valid for the lifetime of the program.
    template <typename E>
    requires std::is_enum_v<E>
    const std::vector<std::string>& enum_options();

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // Strip non-identifier characters so the plugin name can be used as a C++ namespace identifier. Empty input yields an empty string.
    static std::string sanitize_identifier(const std::string& raw);

    // Split a whitespace-separated list into trimmed tokens.
    static std::vector<std::string> split_tokens(const std::string& input);

    // Derive a short identifier from a git URL: ".../foo.git" -> "foo".
    static std::string url_stem(const std::string& url);

    // Uppercase a string, for the `<ALIAS>_DIR` CMake variable.
    static std::string to_upper_identifier(const std::string& in);

    // Escape a string for C++ string-literal output.
    static std::string escape_cpp_string(const std::string& s);

    static const TextEditor::Language* make_cmake_language_definition();

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    template <typename E>
    requires std::is_enum_v<E>
    const std::vector<std::string>& enum_options() {

        static const std::vector<std::string> s_instance = [] {

            std::vector<std::string> out;
            out.reserve(GLT::util::enum_values<E>.size());
            for (auto v : GLT::util::enum_values<E>)
                out.emplace_back(GLT::util::enum_to_string(v));
            return out;
        }();

        return s_instance;
    }

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static std::string sanitize_identifier(const std::string& raw) {

        std::string out;
        out.reserve(raw.size());
        for (char c : raw) {

            const auto uc = static_cast<unsigned char>(c);
            out.push_back((std::isalnum(uc) || c == '_') ? c : '_');
        }
        if (!out.empty() && std::isdigit(static_cast<unsigned char>(out.front())))
            out.insert(out.begin(), '_');
        return out;
    }


    static std::vector<std::string> split_tokens(const std::string& input) {

        std::vector<std::string> out;
        std::istringstream stream(input);
        std::string token;
        while (stream >> token)
            out.push_back(std::move(token));
        return out;
    }


    static std::string url_stem(const std::string& url) {

        std::string trimmed = url;
        while (!trimmed.empty() && (trimmed.back() == '/' || trimmed.back() == '\\'))
            trimmed.pop_back();

        const size_t slash = trimmed.find_last_of("/\\");
        std::string stem = (slash == std::string::npos) ? trimmed : trimmed.substr(slash + 1);

        if (stem.size() > 4 && stem.substr(stem.size() - 4) == ".git")
            stem.resize(stem.size() - 4);

        return stem.empty() ? std::string("vendor") : stem;
    }


    static std::string to_upper_identifier(const std::string& in) {

        std::string out;
        out.reserve(in.size());
        for (char c : in) {

            const auto uc = static_cast<unsigned char>(c);
            out.push_back((std::isalnum(uc) || c == '_') ? static_cast<char>(std::toupper(uc)) : '_');
        }
        return out.empty() ? std::string("VENDOR") : out;
    }


    static std::string escape_cpp_string(const std::string& s) {

        std::string out;
        out.reserve(s.size());
        for (char c : s) {

            switch (c) {
                case '\\': out += "\\\\"; break;
                case '\"': out += "\\\""; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:   out.push_back(c); break;
            }
        }
        return out;
    }


    static const TextEditor::Language* make_cmake_language_definition() {

        static TextEditor::Language lang{};
        lang.name = "CMake";
        lang.caseSensitive = false;                // CMake commands are case-insensitive

        // ---- comments ------------------------------------------------------
        // CMake uses '#' for single-line comments and '[[ ]]' for bracket
        // comments. The default tokenizer only honours one style at a time, so
        // we pick '#'. Bracket comments will simply be rendered as plain text.
        lang.singleLineComment = "#";

        // ---- keywords (control flow + boolean literals) ---------------------
        lang.keywords = {

            // block control
            "if", "elseif", "else", "endif",
            "foreach", "endforeach",
            "while", "endwhile",
            "function", "endfunction",
            "macro", "endmacro",
            "block", "endblock",
            "break", "continue", "return",

            // boolean operators and literals
            "and", "or", "not",
            "true", "false", "on", "off", "yes", "no",

            // condition / test operators
            "exists", "command", "defined", "policy", "target", "test",
            "in_list", "is_directory", "is_symlink", "is_absolute",
            "matches", "strless", "strgreater", "strequal",
            "version_less", "version_greater", "version_equal",
            "path_equal", "path_less", "path_greater",
        };

        // ---- built-in commands (coloured like known identifiers) -----------
        lang.identifiers = {

            // meta / project
            "cmake_minimum_required", "cmake_policy", "cmake_parse_arguments",
            "cmake_host_system_information",
            "project", "enable_language", "enable_testing",
            "include", "include_directories", "include_external_msproject", "include_guard",

            // targets / sources
            "add_executable", "add_library", "add_subdirectory",
            "add_custom_command", "add_custom_target",
            "add_compile_definitions", "add_compile_options", "add_link_options", "add_test",
            "target_compile_definitions", "target_compile_features",
            "target_compile_options", "target_include_directories",
            "target_link_directories", "target_link_libraries",
            "target_link_options", "target_precompile_headers", "target_sources",
            "set_target_properties", "get_target_property",
            "set_property", "get_property", "define_property",
            "link_directories", "link_libraries",

            // dependency discovery
            "find_package", "find_library", "find_program", "find_path", "find_file",

            // variables / diagnostics
            "set", "unset", "option", "message",
            "list", "string", "math",
            "file", "execute_process", "separate_arguments",
            "get_filename_component", "get_directory_property", "get_cmake_property",
            "site_name", "variable_watch", "mark_as_advanced",

            // install / export
            "install", "export",
            "configure_file", "configure_package_config_file",
            "write_file", "write_basic_package_version_file",

            // probes
            "check_include_file", "check_symbol_exists", "check_function_exists",
            "check_c_source_compiles", "check_cxx_source_compiles",
            "check_c_source_runs", "check_cxx_source_runs",
            "test_big_endian", "check_type_size",

            "source_group",
        };

        // Note: ${VAR}, $<genex>, and ${CMAKE_SOURCE_DIR} style references are
        // not specially coloured without a custom tokenizer. They render as
        // plain text under the default tokenizer, which is acceptable for a
        // read-only preview.

        return &lang;
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    plugin_wizard_window::plugin_wizard_window() {

        make_window_name("New Plugin");
        refresh_plugin_path();

        // Configure the entry point editor (C++)
        m_entry_point_editor.SetLanguage(TextEditor::Language::Cpp());
        m_entry_point_editor.SetReadOnlyEnabled(true);
        m_entry_point_editor.SetPalette(dark_style);

        // Configure the CMake editor (CMake)
        m_cmake_editor.SetLanguage(make_cmake_language_definition());
        m_cmake_editor.SetReadOnlyEnabled(true);
        m_cmake_editor.SetPalette(dark_style);
    }


    plugin_wizard_window::~plugin_wizard_window() { }

    // CLASS PUBLIC ====================================================================================================

    void plugin_wizard_window::window(const f32 /*delta_time*/) {

        if (!m_show_window)
            return;

        apply_pending_dock();
        ImGui::SetNextWindowSizeConstraints(ImVec2(820.0f, 480.0f), ImVec2(std::numeric_limits<f32>::max(), std::numeric_limits<f32>::max()));

        if (ImGui::Begin(m_window_id.c_str(), &m_show_window)) {

            GLT::UI::custom_frame(SETTINGS_PANEL_WIDTH, true, ImGui::GetColorU32(GLT::imgui_config::get_default_gray1_ref()),
                [this]() {
                    draw_settings_panel(); 
                },
                [this]() {
                    draw_preview_panel(); 
                });

            // Popups must be opened at the window scope so their ID-stack
            // level matches the BeginPopup calls.
            if (m_open_add_dependency) {

                ImGui::OpenPopup(ADD_DEP_POPUP_ID);
                m_open_add_dependency = false; 
            }

            if (m_open_add_vendor) {

                ImGui::OpenPopup(ADD_VENDOR_POPUP_ID); 
                m_open_add_vendor = false; 
            }

            draw_add_dependency_popup();
            draw_add_vendor_popup();
        }

        ImGui::End();
    }


    void plugin_wizard_window::update(const f32 /*delta_time*/) { }


    bool plugin_wizard_window::serialize(const std::filesystem::path& /*project_file*/, const GLT::serializer::option /*option*/) { return false; }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    // ---- panels -----------------------------------------------------------------------------------------------------

    void plugin_wizard_window::draw_settings_panel() {

        draw_identity_section();
        draw_lifecycle_section();
        draw_dependencies_section();
        draw_build_section();
        draw_vendor_section();

        ImGui::Spacing();
        draw_action_bar();
    }


    void plugin_wizard_window::draw_preview_panel() {

        const std::string entry_src = build_entry_point_cpp();
        const std::string cmake_src = build_cmake_lists();

        // Only update the editor's text if it has changed. This is important
        // for performance and to not reset the editor's internal state every frame.
        if (m_entry_point_editor.GetText() != entry_src)
            m_entry_point_editor.SetText(entry_src);

        if (m_cmake_editor.GetText() != cmake_src)
            m_cmake_editor.SetText(cmake_src);

        const std::string entry_file_loc = "src/" + ENTRY_FILE_NAME;
        if (GLT::UI::begin_collapsing_header_section(entry_file_loc.c_str())) {

            ImGui::BeginChild("##pw_preview_entry");
            ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::monospace_regular));
            m_entry_point_editor.Render("##entry_editor");
            ImGui::PopFont();
            ImGui::EndChild();
            GLT::UI::end_collapsing_header_section();
        }

        if (GLT::UI::begin_collapsing_header_section(BUILD_FILE_NAME.c_str())) {

            ImGui::BeginChild("##pw_preview_cmake");
            ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::monospace_regular));
            m_cmake_editor.Render("##cmake_editor");
            ImGui::PopFont();
            ImGui::EndChild();
            GLT::UI::end_collapsing_header_section();
        }
    }

    // ---- settings sections ------------------------------------------------------------------------------------------

    void plugin_wizard_window::draw_identity_section() {

        if (!GLT::UI::begin_collapsing_header_section("Identity"))
            return;

        if (GLT::UI::begin_table("plugin_editor_details", false)) {

            GLT::UI::table_row("name", m_plugin_name, m_editing_plugin_name);

            if (GLT::UI::table_row("##pw_root", m_target_root, enum_options<target_root>(), 
                "Where the plugin source should be written.\nProject: next to the project's content folder.\nEngine:  under the engine source tree (requires write access)."))
                    refresh_plugin_path();

            GLT::UI::end_table();
        }

        if (!m_plugin_name.empty() && namespace_name() != m_plugin_name)
            ImGui::TextDisabled("Namespace will be sanitized to '%s'", namespace_name().c_str());

        GLT::UI::end_collapsing_header_section();
    }


    void plugin_wizard_window::draw_lifecycle_section() {

        if (!GLT::UI::begin_collapsing_header_section("Lifecycle"))
            return;

        if (GLT::UI::begin_table("plugin_editor_details", false)) {

            GLT::UI::table_row("load phase", m_load_phase, enum_options<GLT::plugin_manager::phase>(),
                "Engine lifecycle moment when this plugin is loaded.\nPick the phase that provides the subsystems you depend on.");

            GLT::UI::table_row("unload phase", m_unload_phase, enum_options<GLT::plugin_manager::phase>(),
                "Engine lifecycle moment when this plugin is unloaded.\nShould be symmetric with the load phase in most cases.");

            GLT::UI::table_row("target interface", m_target_interface, enum_options<GLT::plugin_manager::interface>(),
                "Core subsystem this plugin replaces or extends.\nOnly one plugin per interface may be active at a time.");

            GLT::UI::end_table();
        }
        GLT::UI::end_collapsing_header_section();
    }


    void plugin_wizard_window::draw_dependencies_section() {

        if (!GLT::UI::begin_collapsing_header_section("Dependencies"))
            return;

        i32 remove_name_idx  = -1;
        i32 remove_iface_idx = -1;

        if (GLT::UI::begin_table("plugin_editor_details", false)) {

            // ---- name-based dependencies -----------------------------------
            for (size_t i = 0; i < m_plugin_dependencies_name.size(); ++i) {

                ImGui::PushID(static_cast<int>(i));

                GLT::UI::table_row(
                    []{ ImGui::TextUnformatted("name"); },
                    [this, &remove_name_idx, i]{
                        ImGui::TextUnformatted(m_plugin_dependencies_name[i].c_str());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X##dep_name_rm"))
                            remove_name_idx = static_cast<i32>(i);
                    });

                ImGui::PopID();
            }

            // ---- interface-based dependencies ------------------------------
            for (size_t i = 0; i < m_plugin_dependencies_interface.size(); ++i) {

                ImGui::PushID(static_cast<int>(i));

                bool removed = false;
                GLT::UI::table_row("interface", m_plugin_dependencies_interface[i], enum_options<GLT::plugin_manager::interface>(), &removed);

                if (removed)
                    remove_iface_idx = static_cast<i32>(i);

                ImGui::PopID();
            }

            GLT::UI::end_table();
        }

        if (remove_name_idx >= 0)
            m_plugin_dependencies_name.erase(m_plugin_dependencies_name.begin() + remove_name_idx);

        if (remove_iface_idx >= 0)
            m_plugin_dependencies_interface.erase(m_plugin_dependencies_interface.begin() + remove_iface_idx);

        if (m_plugin_dependencies_name.empty() && m_plugin_dependencies_interface.empty())
            ImGui::TextDisabled("(no dependencies)");

        ImGui::Spacing();
        if (GLT::UI::gray_button("+ Add dependency"))
            m_open_add_dependency = true;

        GLT::UI::end_collapsing_header_section();
    }


    void plugin_wizard_window::draw_build_section() {

        if (!GLT::UI::begin_collapsing_header_section("Build config"))
            return;

        if (GLT::UI::begin_table("plugin_editor_details", false)) {

            GLT::UI::table_row("compile defs", m_compile_defs, m_editing_compile_defs);
            GLT::UI::table_row("Use GLM", m_use_glm);
            GLT::UI::table_row("Use ImGui", m_use_imgui);
            GLT::UI::table_row("Create asset dir", m_create_asset_dir);
            GLT::UI::end_table();
        }

        GLT::UI::end_collapsing_header_section();
    }


    void plugin_wizard_window::draw_vendor_section() {

        if (!GLT::UI::begin_collapsing_header_section("Vendor repositories"))
            return;

        i32 remove_idx = -1;

        if (GLT::UI::begin_table("plugin_editor_details", false)) {

            for (size_t i = 0; i < m_vendors.size(); ++i) {

                ImGui::PushID(static_cast<int>(i));
                const auto& v = m_vendors[i];

                const std::string alias = v.alias.empty() ? url_stem(v.url) : v.alias;
                const char* ref_suffix =
                    (v.ref_mode == vendor_ref_mode::branch) ? " [BRANCH]" :
                    (v.ref_mode == vendor_ref_mode::tag)    ? " [TAG]"    : "";

                GLT::UI::table_row(
                    [&alias, ref_suffix]{ ImGui::Text("%s%s", alias.c_str(), ref_suffix); },
                    [&v, &remove_idx, i]{

                        ImGui::TextUnformatted(v.url.c_str());
                        if (!v.ref_value.empty()) {

                            ImGui::SameLine();
                            ImGui::TextDisabled("@ %s", v.ref_value.c_str());
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X##vend_rm"))
                            remove_idx = static_cast<i32>(i);
                    });

                ImGui::PopID();
            }

            GLT::UI::end_table();
        }

        if (remove_idx >= 0)
            m_vendors.erase(m_vendors.begin() + remove_idx);

        if (m_vendors.empty())
            ImGui::TextDisabled("(no vendor libraries)");

        ImGui::Spacing();
        if (GLT::UI::gray_button("+ Add vendor repo"))
            m_open_add_vendor = true;

        GLT::UI::end_collapsing_header_section();
    }


    void plugin_wizard_window::draw_action_bar() {

        // Status line ---------------------------------------------------------------------------------------------
        if (!m_status_message.empty()) {

            const ImVec4 ok_color  (0.40f, 0.85f, 0.40f, 1.0f);
            const ImVec4 err_color (0.90f, 0.35f, 0.35f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Text, m_status_is_error ? err_color : ok_color);
            ImGui::TextWrapped("%s", m_status_message.c_str());
            ImGui::PopStyleColor();
        }

        // Output path hint ----------------------------------------------------------------------------------------
        if (!m_plugin_path.empty())
            ImGui::TextDisabled("Output: %s", m_plugin_path.generic_string().c_str());
        else
            ImGui::TextDisabled("Output: <name the plugin first>");

        ImGui::Spacing();

        // Buttons -------------------------------------------------------------------------------------------------
        const f32 width = (ImGui::GetContentRegionAvail().x / 2.0f) - 5.0f;

        if (GLT::UI::gray_button("Reset", ImVec2(width, 0.0f)))
            reset_form();

        ImGui::SameLine();

        if (GLT::UI::gray_button("Create Plugin", ImVec2(width, 0.0f)))
            on_create_clicked();
    }

    // ---- popups -----------------------------------------------------------------------------------------------------

    void plugin_wizard_window::draw_add_dependency_popup() {

        if (!ImGui::BeginPopup(ADD_DEP_POPUP_ID))
            return;

        ImGui::TextUnformatted("Add plugin dependency");
        ImGui::Separator();

        if (GLT::UI::begin_table("plugin_editor_details", false)) {

            GLT::UI::table_row("mode", m_new_dep_mode, enum_options<dependency_mode>());

            if (m_new_dep_mode == dependency_mode::by_name) {

                GLT::UI::table_row("plugin name", m_new_dep_name, m_editing_new_dep_name);

            } else {

                GLT::UI::table_row("interface", m_new_dep_iface, enum_options<GLT::plugin_manager::interface>());
            }

            GLT::UI::end_table();
        }

        const bool can_add =
            (m_new_dep_mode == dependency_mode::by_interface) ||
            (m_new_dep_mode == dependency_mode::by_name && !m_new_dep_name.empty());

        ImGui::BeginDisabled(!can_add);

        if (GLT::UI::gray_button("Add")) {

            if (m_new_dep_mode == dependency_mode::by_name)
                m_plugin_dependencies_name.push_back(m_new_dep_name);
            else
                m_plugin_dependencies_interface.push_back(m_new_dep_iface);

            m_new_dep_name = "";
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndDisabled();

        ImGui::SameLine();
        if (GLT::UI::gray_button("Cancel"))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }


    void plugin_wizard_window::draw_add_vendor_popup() {

        ImGui::SetNextWindowSize(ImVec2(400, 300));
        if (!ImGui::BeginPopup(ADD_VENDOR_POPUP_ID))
            return;

        ImGui::TextUnformatted("Add vendor repository");
        ImGui::Separator();

        if (GLT::UI::begin_table("plugin_editor_details", false)) {

            GLT::UI::table_row("url", m_new_vendor_url, m_editing_new_vendor_url);

            GLT::UI::table_row("alias", m_new_vendor_alias, m_editing_new_vendor_alias, true,
                "Short identifier used for the vendored directory and\nthe generated <ALIAS>_DIR CMake variable.");

            GLT::UI::table_row("ref type", m_new_vendor_ref_mode, enum_options<vendor_ref_mode>());

            if (m_new_vendor_ref_mode != vendor_ref_mode::none)
                GLT::UI::table_row("ref value", m_new_vendor_ref_value, m_editing_new_vendor_ref_value);

            GLT::UI::table_row("shallow", m_new_vendor_shallow);

            GLT::UI::end_table();
        }

        ImGui::BeginDisabled(m_new_vendor_url.empty());

        if (GLT::UI::gray_button("Add")) {

            vendor_entry v{

                .url = m_new_vendor_url,
                .alias = m_new_vendor_alias,
                .ref_mode = m_new_vendor_ref_mode,
                .ref_value = m_new_vendor_ref_value,
                .shallow = m_new_vendor_shallow,
            };
            m_vendors.push_back(std::move(v));

            m_new_vendor_url.clear();
            m_new_vendor_alias.clear();
            m_new_vendor_ref_value.clear();
            m_new_vendor_ref_mode = vendor_ref_mode::none;

            ImGui::CloseCurrentPopup();
        }

        ImGui::EndDisabled();

        ImGui::SameLine();
        if (GLT::UI::gray_button("Cancel"))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    // ---- actions ----------------------------------------------------------------------------------------------------

    void plugin_wizard_window::on_create_clicked() {

        std::string error;
        if (!validate_form(error)) {

            m_status_message  = std::move(error);
            m_status_is_error = true;
            return;
        }

        try {

            write_files(m_plugin_path);
            m_status_message  = "Plugin created at [" + m_plugin_path.generic_string() + "]";
            m_status_is_error = false;

        } catch (const std::exception& e) {

            m_status_message  = std::string("Failed to create plugin: ") + e.what();
            m_status_is_error = true;
        }
    }


    void plugin_wizard_window::reset_form() {

        m_plugin_name = "my_new_plugin";
        m_target_root = target_root::project;

        m_load_phase = GLT::plugin_manager::phase::application_ready;
        m_unload_phase = GLT::plugin_manager::phase::pre_application_shutdown;
        m_target_interface = GLT::plugin_manager::interface::custom;

        m_plugin_dependencies_name.clear();
        m_plugin_dependencies_interface.clear();
        m_vendors.clear();

        m_compile_defs = "";
        m_use_glm = false;
        m_use_imgui = false;
        m_create_asset_dir = false;

        m_status_message.clear();
        m_status_is_error = false;

        refresh_plugin_path();
    }


    void plugin_wizard_window::refresh_plugin_path() {

        const auto base =
            (m_target_root == target_root::project)
                ? GLT::application::get().get_project_path() / GLT::config::PLUGIN_DIR
                : GLT::util::get_executable_path().parent_path().parent_path().parent_path() / GLT::config::PLUGIN_DIR;     
                // escape from                     build/        bin/          debug

        m_plugin_path = base / m_plugin_name;
    }

    // ---- helpers ----------------------------------------------------------------------------------------------------

    bool plugin_wizard_window::validate_form(std::string& out_error) const {

        if (m_plugin_name.empty()) {

            out_error = "Plugin name is required.";
            return false;
        }

        if (namespace_name().empty()) {

            out_error = "Plugin name must contain at least one alphanumeric character.";
            return false;
        }

        if (m_plugin_path.empty()) {

            out_error = "Could not resolve an output directory.";
            return false;
        }

        std::error_code error;
        const auto cmake_path = m_plugin_path / BUILD_FILE_NAME;
        if (GLT::vfs::exists(cmake_path, error) && !error) {

            out_error = "A plugin already exists at " + m_plugin_path.generic_string();
            return false;
        }

        return true;
    }


    std::string plugin_wizard_window::namespace_name() const { return sanitize_identifier(m_plugin_name); }

    // ---- generation -------------------------------------------------------------------------------------------------

    std::string plugin_wizard_window::build_entry_point_cpp() const {

        const std::string ns = namespace_name();

        std::vector<std::string>                    name_deps{};
        std::vector<GLT::plugin_manager::interface> iface_deps{};

        name_deps  = m_plugin_dependencies_name;
        iface_deps = m_plugin_dependencies_interface;

        std::ostringstream s{};

        s << "#include <plugin_system/i_plugin.h>\n\n\n";

        s << "// FORWARD DECLARATIONS ================================================================================================\n\n\n";
        s << "namespace GLT::" << ns << " {\n\n";

        s << "    // CONSTANTS =======================================================================================================\n\n";
        s << "    // MACROS ==========================================================================================================\n\n";
        s << "    // TYPES ===========================================================================================================\n\n";
        s << "    // STATIC VARIABLES ================================================================================================\n\n";

        // --- dependency names ---
        s << "    static constexpr const char* dependencies_names[] = {\n\n";
        if (name_deps.empty()) {

            s << "        nullptr\n";

        } else {

            for (const auto& n : name_deps)
                s << "        \"" << escape_cpp_string(n) << "\",\n";
        }
        s << "    };\n\n";

        // --- dependency interfaces ---
        s << "    static constexpr GLT::plugin_manager::interface dependencies_interfaces[] = {\n\n";
        if (iface_deps.empty()) {

            s << "        GLT::plugin_manager::interface::none\n";

        } else {

            for (auto i : iface_deps)
                s << "        GLT::plugin_manager::interface::"
                  << GLT::util::enum_to_string(i) << ",\n";
        }
        s << "    };\n\n";

        // --- descriptor ---
        s << "    static constexpr GLT::plugin_manager::plugin_descriptor descriptor = {\n\n";
        s << "        .name                                                   = GLT_MODULE_NAME,\n";
        s << "        .load_phase                                             = GLT::plugin_manager::phase::"
          << GLT::util::enum_to_string(m_load_phase) << ",\n";
        s << "        .unload_phase                                           = GLT::plugin_manager::phase::"
          << GLT::util::enum_to_string(m_unload_phase) << ",\n";
        s << "        .target                                                 = GLT::plugin_manager::interface::"
          << GLT::util::enum_to_string(m_target_interface) << ",\n";
        s << "        .dependency_names_count                                 = ARRAY_SIZE(dependencies_names),\n";
        s << "        .dependency_names                                       = dependencies_names,\n";
        s << "        .dependency_interface_count                             = ARRAY_SIZE(dependencies_interfaces),\n";
        s << "        .dependency_interfaces                                  = dependencies_interfaces,\n";
        s << "    };\n\n";

        // --- plugin class ---
        s << "    // FUNCTION IMPLEMENTATION =========================================================================================\n\n";
        s << "    // CLASS IMPLEMENTATION ============================================================================================\n\n";
        s << "    // CLASS PUBLIC ====================================================================================================\n\n";
        s << "    class plugin : public GLT::plugin_manager::i_plugin {\n";
        s << "    public:\n\n";
        s << "        void on_load() override {\n\n";
        s << "            LOG_LOADED\n";
        s << "        }\n\n";
        s << "        void on_unload() override {\n\n";
        s << "            LOG_UNLOADED\n";
        s << "        }\n";
        s << "    };\n\n";
        s << "}\n\n";

        s << "EXPORT_PLUGIN_CLASS(GLT::" << ns << "::plugin, GLT::" << ns << "::descriptor)\n";

        return s.str();
    }


    std::string plugin_wizard_window::build_cmake_lists() const {

        const std::string name = m_plugin_name;
        const std::vector<std::string> compile_defs = split_tokens(m_compile_defs);
        std::ostringstream s{};

        s << "# ===========================================================================================\n";
        s << "# Specific Plugin CMake Configuration\n";
        s << "# ===========================================================================================\n";
        s << "# Project:      plugin for Gluttony\n";
        s << "# Location:     ${PROJECT_SOURCE_DIR}/plugin/" << name << "/" << BUILD_FILE_NAME << "\n";
        s << "# Purpose:      compile the plugin in this dir as a shared lib\n";
        s << "# ===========================================================================================\n\n";

        s << "include(${CMAKE_SOURCE_DIR}/helper.cmake)\n";
        s << "set(PLUGIN_NAME                                                         \"" << name << "\")\n\n";

        s << "file(GLOB_RECURSE PLUGIN_SOURCES\n";
        s << "    CONFIGURE_DEPENDS\n";
        s << "    \"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp\"\n";
        s << "    \"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx\"\n";
        s << "    \"${CMAKE_CURRENT_SOURCE_DIR}/src/*.c\"\n";
        s << ")\n";
        s << "set_property(GLOBAL PROPERTY \"${CMAKE_CURRENT_SOURCE_DIR}_SOURCES\" \"${PLUGIN_SOURCES}\")\n\n";

        // --- compile_plugin calls ---
        auto emit_compile_plugin = [&](const std::string& target) {

            s << "compile_plugin(SHARED                                                   " << target;
            s << "    ";
            if (m_use_glm)          s << " glm";
            if (m_use_imgui)        s << " imgui";
            s << ")\n";
        };

        s << "# add libraries in both static and shared\n";
        emit_compile_plugin("${PLUGIN_NAME}       ");
        emit_compile_plugin("${PLUGIN_NAME}_static");
        s << "\n";

        // --- compile definitions ---
        if (!compile_defs.empty()) {

            s << "target_compile_definitions(${PLUGIN_NAME}                               PRIVATE\n";
            for (const auto& d : compile_defs)
                s << "    " << d << "\n";
            s << ")\n";
            s << "target_compile_definitions(${PLUGIN_NAME}_static                      PRIVATE\n";
            for (const auto& d : compile_defs)
                s << "    " << d << "\n";
            s << ")\n\n";
        }

        // --- vendor repositories ---
        if (!m_vendors.empty()) {

            s << "# vendor ------------------------------------------------------------------------------------\n";
            s << "set(VENDOR_DIR                                                          \"${CMAKE_CURRENT_SOURCE_DIR}/vendor\")\n";
            s << "file(MAKE_DIRECTORY                                                     \"${VENDOR_DIR}\")\n\n";

            for (const auto& v : m_vendors) {

                const std::string alias       = v.alias.empty() ? url_stem(v.url) : v.alias;
                const std::string alias_upper = to_upper_identifier(alias);

                s << "set(" << alias_upper << "_DIR"
                  << std::string(std::max<std::size_t>(1, 60 - alias_upper.size()), ' ')
                  << "\"${VENDOR_DIR}/" << alias << "\")\n";

                s << "git_clone_or_update(\"" << v.url << "\"";
                const std::size_t pad = (v.url.size() < 56) ? (56 - v.url.size()) : 1;
                s << std::string(pad, ' ')
                  << "\"${" << alias_upper << "_DIR}\"";

                if (v.ref_mode == vendor_ref_mode::branch && !v.ref_value.empty())
                    s << " BRANCH \"" << v.ref_value << "\"";
                else if (v.ref_mode == vendor_ref_mode::tag && !v.ref_value.empty())
                    s << " TAG \""    << v.ref_value << "\"";

                if (v.shallow)
                    s << " DEPTH 1";

                s << ")\n";
            }
            s << "\n";
        }

        // --- asset copy ---
        if (m_create_asset_dir) {

            s << "# ------------- Copy assets directory to output ---------------------------------------------------\n";
            s << "set(COMPILE_OUTPUT_DIR                                                  \"${CMAKE_BINARY_DIR}/bin/$<CONFIG>\")\n";
            s << "if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/asset)\n";
            s << "    add_custom_command(TARGET                                           ${PLUGIN_NAME}\n";
            s << "        POST_BUILD COMMAND                                              ${CMAKE_COMMAND} -E copy_directory\n";
            s << "            ${CMAKE_CURRENT_SOURCE_DIR}/asset\n";
            s << "            ${COMPILE_OUTPUT_DIR}/asset\n";
            s << "        COMMENT \"Copying assets to build directory: ${COMPILE_OUTPUT_DIR}/asset\"\n";
            s << "    )\n";
            s << "else()\n";
            s << "    message(WARNING \"Assets directory not found at ${CMAKE_CURRENT_SOURCE_DIR}/asset\")\n";
            s << "endif()\n\n";
        }

        s << "\n\nprint_source_file_details(\n";
        s << "    SOURCES                                                             ${PLUGIN_SOURCES}\n";
        s << "    NAME                                                                ${PLUGIN_NAME}\n";
        s << "    SOURCE_DIR                                                          \"${CMAKE_CURRENT_SOURCE_DIR}\"\n";
        s << "    VERBOSE                                                             CORE_VERBOSE_INFO\n";
        s << "    TARGET                                                              ${PLUGIN_NAME}\n";
        s << ")\n";

        return s.str();
    }


    void plugin_wizard_window::write_files(const std::filesystem::path& plugin_root) {

        std::error_code error{};

        // create src/
        const auto src_dir = plugin_root / "src";
        GLT::vfs::create_directories(src_dir, error);
        VALIDATE(!error, return, "", "Failed to create directories [{}]", error.message())

        {
            const std::string data = build_entry_point_cpp();
            VALIDATE(GLT::vfs::write_text_file(src_dir / ENTRY_FILE_NAME, data), return, 
                "", "Failed to write [src/{}]", ENTRY_FILE_NAME)
        }

        {
            const std::string data = build_cmake_lists();
            VALIDATE(GLT::vfs::write_text_file(plugin_root / BUILD_FILE_NAME, data), return, 
                "", "Failed to write [{}]", BUILD_FILE_NAME)
        }

        // optional asset/
        if (m_create_asset_dir) {
            const auto asset_dir = plugin_root / "asset";
            GLT::vfs::create_directories(asset_dir, error);
            VALIDATE(!error, return, "", "Failed to create directories [{}]", error.message())
        }
    
    }

}
