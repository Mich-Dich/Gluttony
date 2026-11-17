#include "util/pch.h"
#include "asset_create.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <asset/i_asset_registry.h>
#include <asset/material.h>
#include <event/event_bus.h>

#include "util/event/asset_event.h"
#include "util/extension_host.h"
#include "util/ui/panel_collection.h"
#include "config/imgui_config.h"
#include "window/base/window_chrome.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr f32                                   CREATE_WINDOW_WIDTH = 560.0f;

    constexpr f32                                   CREATE_WINDOW_HEIGHT = 420.0f;

    // Type tiles
    constexpr f32                                   TYPE_TILE_WIDTH = 124.0f;

    constexpr f32                                   TYPE_TILE_HEIGHT = 86.0f;

    constexpr f32                                   TYPE_TILE_ICON = 36.0f;

    // Footer hint colors
    constexpr ImU32                                 HINT_ERROR = IM_COL32(235, 100, 100, 255);

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    asset_create_window::asset_create_window(std::filesystem::path target_dir) {

        make_window_name("Create Asset");
        m_window_specs.can_be_deleted = true;

        m_target_dir = std::move(target_dir);
        m_target_type = creatable_types().front().type;

        std::snprintf(m_name_buffer, sizeof(m_name_buffer), "New Material");

        m_center_next_frame = true;
        m_show_window = true;
    }


    asset_create_window::~asset_create_window() = default;

    // CLASS PUBLIC ====================================================================================================

    void asset_create_window::window(const f32 /*delta_time*/) {

        if (!m_show_window)
            return;

        if (m_center_next_frame) {

            const ImGuiViewport* vp = ImGui::GetMainViewport();
            const ImVec2 center(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f);
            ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(CREATE_WINDOW_WIDTH, CREATE_WINDOW_HEIGHT), ImGuiCond_Appearing);
            m_center_next_frame = false;
        }

        ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 300.0f), ImVec2(FLT_MAX, FLT_MAX));
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        const bool open = ImGui::Begin(m_window_id.c_str(), &m_show_window, flags);
        ImGui::PopStyleVar();
        if (!open) {
            ImGui::End();
            return;
        }

        draw_header();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f, 10.f));
        ImGui::BeginChild("##asset_content", ImVec2(0.f, -chrome::footer_height()), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();

        draw_type_tiles();
        draw_asset_section();

        ImGui::EndChild();

        draw_footer();

        ImGui::End();
    }


    void asset_create_window::update(const f32 /*delta_time*/) {}


    void asset_create_window::dock_to(ImGuiID /*dock_id*/) { /* deliberately non-dockable */ }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    std::vector<asset_create_window::creatable_type> asset_create_window::creatable_types() {

        // Only entries that support a meaningful create-in-place flow belong
        // here. Meshes and images come from files, so they live in the import
        // wizard instead.
        std::vector<creatable_type> types{
            {
                GLT::asset::core_types::material,
                "Material",
                "PBR material with base color, roughness, metallic, and six texture slots.",
                icon_manager::icon::material_big,
                "New Material",
            },
        };

        for (const auto& entry : extension_host::asset_creators())
            types.push_back({ entry.creator.type, entry.creator.label, entry.creator.tooltip, icon_manager::icon::file_big,
                entry.creator.default_name.empty() ? std::string("New Asset") : entry.creator.default_name });
        return types;
    }


    void asset_create_window::draw_header() {

        const std::filesystem::path relative = GLT::project::extract_path_from_project_content_dir(m_target_dir);
        const std::string location = "Content/" + relative.generic_string();

        chrome::header_options options{};
        options.subtitle = location.c_str();
        const chrome::header_bar bar = chrome::begin_header_bar(icon_manager::icon::file, "Create Asset", options);
        chrome::end_header_bar(bar);
    }


    void asset_create_window::draw_type_tiles() {

        chrome::section_label("ASSET TYPE");
        ImGui::Dummy(ImVec2(0.f, 2.f));

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const f32 avail = ImGui::GetContentRegionAvail().x;
        f32 line_x = 0.f;

        const std::vector<creatable_type> types = creatable_types();
        // the plugin owning the selected type may have been unloaded while this dialog was open
        if (std::ranges::none_of(types, [this](const creatable_type& t) { return t.type == m_target_type; }))
            m_target_type = types.front().type;
        for (const auto& t : types) {

            if (line_x > 0.f && line_x + TYPE_TILE_WIDTH > avail) {
                line_x = 0.f;                                       // wrap
            } else if (line_x > 0.f)
                ImGui::SameLine(0.f, 8.f);

            ImGui::PushID(static_cast<int>(t.type.value));
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max(min.x + TYPE_TILE_WIDTH, min.y + TYPE_TILE_HEIGHT);
            const bool selected = (t.type == m_target_type);

            if (ImGui::InvisibleButton("##tile", ImVec2(TYPE_TILE_WIDTH, TYPE_TILE_HEIGHT)) && !selected) {
                // keep a name the user typed; only swap the default name of the previous type
                for (const auto& previous : types)
                    if (previous.type == m_target_type && previous.default_name == m_name_buffer)
                        std::snprintf(m_name_buffer, sizeof(m_name_buffer), "%s", t.default_name.c_str());
                m_target_type = t.type;
                m_operation_error.clear();
            }
            const bool hovered = ImGui::IsItemHovered();
            if (hovered && !t.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", t.tooltip.c_str());

            // background + frame
            const ImU32 fill = selected ? ImGui::GetColorU32(GLT::imgui_config::get_action_color00_faded_ref())
                : hovered ? ImGui::GetColorU32(GLT::imgui_config::get_action_color_gray_hover_ref())
                : ImGui::GetColorU32(ImGuiCol_FrameBg);
            draw_list->AddRectFilled(min, max, fill, 6.f);
            if (selected)
                draw_list->AddRect(min, max, ImGui::GetColorU32(GLT::imgui_config::get_action_color00_hover_ref()), 6.f, 0, 1.5f);

            // icon + label, centered
            const auto icon = icon_manager::get(t.icon);
            const ImVec2 icon_min(min.x + (TYPE_TILE_WIDTH - TYPE_TILE_ICON) * 0.5f, min.y + 12.f);
            draw_list->AddImage(icon.tex_ref, icon_min, ImVec2(icon_min.x + TYPE_TILE_ICON, icon_min.y + TYPE_TILE_ICON), icon.uv0, icon.uv1);

            const ImVec2 label_size = ImGui::CalcTextSize(t.label.c_str());
            draw_list->AddText(ImVec2(min.x + (TYPE_TILE_WIDTH - label_size.x) * 0.5f, max.y - label_size.y - 10.f),
                ImGui::GetColorU32(selected ? ImGuiCol_Text : ImGuiCol_TextDisabled), t.label.c_str());

            ImGui::PopID();
            line_x += TYPE_TILE_WIDTH + 8.f;
        }

        ImGui::Dummy(ImVec2(0.f, 6.f));
    }


    void asset_create_window::draw_asset_section() {

        std::string ext_hint = ".";
        ext_hint += extension_host::extension_of(m_target_type);

        if (UI::begin_card("Asset")) {

            // ---- name, with the extension shown behind the field ------------------------------------
            UI::property_custom("Name", [&](const f32 width) {
                    const f32 ext_w = ImGui::CalcTextSize(ext_hint.c_str()).x + 8.0f;
                    ImGui::SetNextItemWidth(std::max(40.f, width - ext_w - ImGui::GetStyle().ItemSpacing.x));
                    if (ImGui::IsWindowAppearing())
                        ImGui::SetKeyboardFocusHere();
                    const bool changed = ImGui::InputText("##name", m_name_buffer, sizeof(m_name_buffer));
                    if (changed)
                        m_operation_error.clear();
                    ImGui::SameLine();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%s", ext_hint.c_str());
                    return changed;
                }, "File name of the new asset");

            // ---- location ---------------------------------------------------------------------------
            UI::property_custom("Location", [&](const f32 /*width*/) {
                    const bool changed = GLT::UI::draw_directory_picker("##location", m_target_dir, PROJECT_CONTENT_DIR);
                    if (changed)
                        m_operation_error.clear();
                    return changed;
                }, "Content folder the asset is created in");

            // ---- options ----------------------------------------------------------------------------
            constexpr bool default_auto_open = true;
            UI::property_toggle("Open in editor", m_auto_open, "Open the new asset in its editor right away", &default_auto_open);

            // ---- what will land on disk -------------------------------------------------------------
            std::filesystem::path name_only = m_name_buffer;
            name_only.replace_extension("");
            if (!name_only.empty())
                name_only += ext_hint;
            const std::filesystem::path relative = GLT::project::extract_path_from_project_content_dir(m_target_dir / name_only);
            UI::property_custom("File", [&](const f32) {
                    ImGui::AlignTextToFramePadding();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::monospace_regular));
                    ImGui::TextWrapped("%s", relative.empty() ? "(outside the content folder)" : relative.generic_string().c_str());
                    ImGui::PopFont();
                    ImGui::PopStyleColor();
                    return false;
                }, "Path of the new file inside the content folder");
        }
        UI::end_card();
    }


    void asset_create_window::draw_settings_section() {
        // Reserved for future per-type settings (e.g. material template presets).
    }


    void asset_create_window::draw_footer() {

        // ---- validation, recomputed every frame ---------------------------------------
        // Kept separate from m_operation_error so that failures inside try_create()
        // survive across frames instead of being cleared by the next validation pass.
        m_validation_error.clear();

        std::filesystem::path filename = m_name_buffer;
        filename.replace_extension("");
        if (filename.empty()) {

            m_validation_error = "Name is required.";
        }
        else {

            std::string full = filename.generic_string();
            full += ".";
            full += extension_host::extension_of(m_target_type);
            filename = full;

            const std::filesystem::path abs_target = m_target_dir / filename;
            const std::filesystem::path rel_target = GLT::project::extract_path_from_project_content_dir(abs_target);

            if (rel_target.empty())
                m_validation_error = "Location must be inside the project content directory.";
            else {
                std::error_code error{};
                if (GLT::vfs::exists(abs_target, error) && !error)
                    m_validation_error = "A file with that name already exists.";
            }
        }

        const bool has_validation_error = !m_validation_error.empty();
        const bool has_error = has_validation_error || !m_operation_error.empty();
        const std::string& msg = has_validation_error ? m_validation_error : m_operation_error;

        // ---- keyboard: Enter creates, Escape cancels (while this window is focused) -----------
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        bool create = focused && !has_validation_error
            && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
        if (focused && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            m_show_window = false;

        chrome::begin_footer();

        // ---- left: status hint ---------------------------------------------------------
        ImGui::AlignTextToFramePadding();
        if (has_error)
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(HINT_ERROR), "%s", msg.c_str());
        else
            ImGui::TextDisabled("Enter to create");

        // ---- right: buttons ------------------------------------------------------------
        const char* cancel_label = "Cancel";
        const char* create_label = "Create";
        const f32 cancel_w = chrome::button_width(cancel_label);
        const f32 create_w = chrome::button_width(create_label);

        ImGui::SameLine();
        chrome::align_right(cancel_w + create_w + 6.f);

        if (UI::subtle_button(cancel_label, ImVec2(cancel_w, 0.0f)))
            m_show_window = false;

        // Only blocked by *validation* errors - an operation error is informational, and the user may retry (e.g. after
        // fixing something externally)
        ImGui::SameLine(0.f, 6.f);
        ImGui::BeginDisabled(has_validation_error);
        create |= UI::accent_button(create_label, ImVec2(create_w, 0.0f));
        ImGui::EndDisabled();

        chrome::end_footer();

        if (create)
            try_create();
    }


    bool asset_create_window::try_create() {

        auto* registry = GLT::asset::registry::get_ptr();
        if (!registry) {
            m_operation_error = "No asset registry";
            return false;
        }

        // ---- resolve target path ------------------------------------------------------
        std::filesystem::path filename = m_name_buffer;
        filename.replace_extension("");
        filename += ".";
        filename += extension_host::extension_of(m_target_type);

        const std::filesystem::path abs_target = m_target_dir / filename;
        const std::filesystem::path rel_target = GLT::project::extract_path_from_project_content_dir(abs_target);
        if (rel_target.empty()) {
            m_operation_error = "Location must be inside the project content directory.";
            return false;
        }

        // ---- plugin asset types: the plugin builds, registers and saves the asset -----------
        if (const GLT::editor::asset_creator* creator = extension_host::find_creator(m_target_type)) {

            const auto create = creator->create;            // copy: the plugin may (un)register creators inside create()
            if (auto created = create(rel_target); !created) {
                m_operation_error = created.error().empty() ? std::string("The plugin could not create the asset.") : created.error();
                return false;
            }

            LOG(info, "asset_create: created [{}]", rel_target.generic_string());
            if (m_auto_open)
                GLT::event_bus::post(asset_open_event{ m_target_type, rel_target });
            m_show_window = false;
            return true;
        }

        // ---- build the runtime asset --------------------------------------------------
        if (m_target_type == GLT::asset::core_types::material) {

            auto asset = GLT::create_unique_ref<GLT::asset::material::material_asset>();
            asset->asset_type = m_target_type;
            asset->textures.fill(INVALID_HANDLE);
            asset->name = filename.stem().string();

            auto registered = registry->register_runtime(std::move(asset), rel_target, filename.stem().string());
            VALIDATE(registered.has_value(), return false, 
                "", "asset_create: register_runtime failed for [{}]", (rel_target.generic_string()))

            if (auto save_result = registry->save(*registered); !save_result) {
                LOG(error, "asset_create: failed to write [{}] (error {})", rel_target.generic_string(), static_cast<int>(save_result.error()))
                m_operation_error = "Failed to write asset to disk.";
                return false;
            }

            LOG(info, "asset_create: created [{}]", rel_target.generic_string());

            if (m_auto_open)
                GLT::event_bus::post(asset_open_event{ m_target_type, rel_target });

            m_show_window = false;
            return true;
        }

        // Placeholder for future creatable types - the create button is only enabled for types with a real branch above
        m_operation_error = "Unsupported target type.";
        return false;
    }

}
