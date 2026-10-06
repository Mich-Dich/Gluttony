#include "util/pch.h"
#include "asset_create.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <asset/i_asset_registry.h>
#include <asset/material.h>
#include <event/event_bus.h>

#include "util/event/asset_event.h"
#include "util/ui/pannel_collection.h"
#include "config/imgui_config.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr f32                                   CREATE_WINDOW_WIDTH = 560.0f;

    constexpr f32                                   CREATE_WINDOW_HEIGHT = 380.0f;

    // Width reserved for the left-hand field labels
    constexpr f32                                   FIELD_LABEL_WIDTH = 110.0f;

    // Content area padding (window padding is zeroed; each zone applies its own)
    constexpr f32                                   CONTENT_PADDING_X = 20.0f;

    constexpr f32                                   CONTENT_PADDING_Y = 16.0f;

    // Footer bar padding
    constexpr f32                                   FOOTER_PADDING_X = 20.0f;

    constexpr f32                                   FOOTER_PADDING_Y = 10.0f;

    // Status hint colors
    constexpr ImVec4                                HINT_ERROR = ImVec4(0.94f, 0.36f, 0.36f, 1.0f);

    constexpr ImVec4                                HINT_OK = ImVec4(0.50f, 0.78f, 0.50f, 1.0f);

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

        m_target_dir  = std::move(target_dir);
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

        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;
        const f32 footer_h = ImGui::GetFrameHeight() + FOOTER_PADDING_Y * 2.0f;
        if (!ImGui::Begin(m_window_id.c_str(), &m_show_window, flags)) {
            ImGui::End();
            return;
        }

        const ImVec2 saved_spacing = ImGui::GetStyle().ItemSpacing;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(saved_spacing.x, 0.0f));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(CONTENT_PADDING_X, CONTENT_PADDING_Y));
        ImGui::BeginChild("##asset_content", ImVec2(0, -footer_h), false, ImGuiWindowFlags_NoScrollbar);
        draw_asset_section();
        ImGui::EndChild();
        ImGui::PopStyleVar();

        draw_footer(footer_h);              // Footer, pinned to the bottom edge

        ImGui::PopStyleVar();               // ItemSpacing
        ImGui::End();
    }

    void asset_create_window::update(const f32 /*delta_time*/) {}


    void asset_create_window::dock_to(ImGuiID /*dock_id*/) { /* deliberately non-dockable */ }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    std::span<const asset_create_window::creatable_type> asset_create_window::creatable_types() {

        // Only entries that support a meaningful create-in-place flow belong
        // here. Meshes and images come from files, so they live in the import
        // wizard instead.
        static constexpr creatable_type kTypes[] = {
            {
                GLT::asset::core_types::material,
                "Material",
                "PBR material with base color, roughness, metallic, and six texture slots."
            },
        };
        return kTypes;
    }


    void asset_create_window::draw_asset_section() {

        const ImGuiStyle& style = ImGui::GetStyle();

        // ---- heading -------------------------------------------------------------------
        ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold_big));
        ImGui::TextUnformatted("New Asset");
        ImGui::PopFont();

        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.0f, 1.0f, 1.0f, 0.10f));
        ImGui::Separator();
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        // ---- fields --------------------------------------------------------------------
        const f32 table_w = ImGui::GetContentRegionAvail().x;

        if (GLT::UI::begin_table("##create_asset_fields", false, ImVec2(table_w, 0.0f), 0.0f, true, 0.30f)) {

            // ---- asset type ------------------------------------------------------------
            GLT::UI::table_row(
                []() { ImGui::TextUnformatted("Asset Type"); },
                [&]() {

                    const auto types = creatable_types();
                    const char* current_label = types.front().label;
                    for (const auto& t : types)
                        if (t.type == m_target_type)
                            current_label = t.label;

                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::BeginCombo("##type", current_label)) {

                        for (const auto& t : types) {

                            const bool selected = (t.type == m_target_type);
                            if (ImGui::Selectable(t.label, selected)) {
                                m_target_type = t.type;
                                m_operation_error.clear();
                            }
                            if (ImGui::IsItemHovered() && t.tooltip)
                                ImGui::SetTooltip("%s", t.tooltip);
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                });

            // ---- location --------------------------------------------------------------
            // Uses the new table_row() directory-picker overload. Displays content-relative.
            GLT::UI::table_row("Location", m_target_dir, PROJECT_CONTENT_DIR);

            // ---- name ------------------------------------------------------------------
            GLT::UI::table_row([]() { ImGui::TextUnformatted("Name"); },
                [&]() {

                    std::string ext_hint = ".";
                    ext_hint += GLT::asset::type_to_extension(m_target_type);
                    const f32 ext_w = ImGui::CalcTextSize(ext_hint.c_str()).x + 8.0f;

                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ext_w - style.ItemSpacing.x);
                    if (ImGui::InputText("##name", m_name_buffer, sizeof(m_name_buffer)))
                        m_operation_error.clear();

                    ImGui::SameLine();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%s", ext_hint.c_str());
                });

            // ---- options ---------------------------------------------------------------
            GLT::UI::table_row("Auto open editor", m_auto_open);

            // ---- result preview --------------------------------------------------------
            // Monospace "what will land on disk" line - lives in the table so it aligns with the other rows and uses the same two-column rhythm
            GLT::UI::table_row([]() { ImGui::TextUnformatted("Result"); },
                [&]() {

                    std::filesystem::path name_only = m_name_buffer;
                    name_only.replace_extension("");
                    if (!name_only.empty()) {
                        name_only += ".";
                        name_only += GLT::asset::type_to_extension(m_target_type);
                    }
                    const std::filesystem::path preview = m_target_dir / name_only;

                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::monospace_regular));
                    ImGui::TextWrapped("%s", preview.generic_string().c_str());
                    ImGui::PopFont();
                    ImGui::PopStyleColor();
                });

            GLT::UI::end_table();
        }
    }


    void asset_create_window::draw_settings_section() {
        // Reserved for future per-type settings (e.g. material template presets).
    }


    void asset_create_window::draw_footer(const f32 footer_height) {

        const ImGuiStyle& style = ImGui::GetStyle();

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
            full += GLT::asset::type_to_extension(m_target_type);
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

        // ---- footer chrome -------------------------------------------------------------
        // We draw the separator + tinted background ourselves because the footer is
        // not a real ImGui window - it is the space left at the bottom of the parent.
        const ImVec2 footer_p0 = ImGui::GetCursorScreenPos();
        const f32    footer_w  = ImGui::GetContentRegionAvail().x;
        ImDrawList*  dl        = ImGui::GetWindowDrawList();

        dl->AddLine(
            ImVec2(footer_p0.x,             footer_p0.y + 0.5f),
            ImVec2(footer_p0.x + footer_w,  footer_p0.y + 0.5f),
            ImGui::GetColorU32(ImGuiCol_Separator, 0.6f), 1.0f);

        dl->AddRectFilled(
            ImVec2(footer_p0.x,            footer_p0.y + 1.0f),
            ImVec2(footer_p0.x + footer_w, footer_p0.y + footer_height),
            ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.15f)));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(FOOTER_PADDING_X, FOOTER_PADDING_Y));
        ImGui::BeginChild("##asset_footer", ImVec2(0, footer_height), false, ImGuiWindowFlags_NoScrollbar);

        // ---- left: status hint ---------------------------------------------------------
        const bool has_validation_error = !m_validation_error.empty();
        const bool has_operation_error  = !m_operation_error.empty();
        const bool has_error            = has_validation_error || has_operation_error;
        const std::string& msg          = has_validation_error ? m_validation_error : m_operation_error;

        ImGui::AlignTextToFramePadding();
        if (has_error)
            ImGui::TextColored(HINT_ERROR, "%s", msg.c_str());
        else
            ImGui::TextDisabled("Ready to create");

        // ---- right: buttons ------------------------------------------------------------
        const char* cancel_label = "Cancel";
        const char* create_label = "Create";

        // Extra horizontal padding so the labels breathe.
        const f32 pad_x     = style.FramePadding.x * 2.0f + 14.0f;
        const f32 cancel_w  = ImGui::CalcTextSize(cancel_label).x + pad_x;
        const f32 create_w  = ImGui::CalcTextSize(create_label).x + pad_x;
        const f32 buttons_w = cancel_w + create_w + style.ItemSpacing.x;

        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - buttons_w);

        if (UI::gray_button(cancel_label, ImVec2(cancel_w, 0.0f)))
            m_show_window = false;

        ImGui::SameLine();

        // Primary action uses the theme's main accent color. Only blocked by
        // *validation* errors - an operation error is informational, and the
        // user is allowed to retry (e.g. after fixing something externally).
        ImGui::BeginDisabled(has_validation_error);
        ImGui::PushStyleColor(ImGuiCol_Button,        GLT::imgui_config::get_action_color00_default_ref());
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GLT::imgui_config::get_action_color00_hover_ref());
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  GLT::imgui_config::get_action_color00_active_ref());
        if (ImGui::Button(create_label, ImVec2(create_w, 0.0f)))
            try_create();
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();

        ImGui::EndChild();
        ImGui::PopStyleVar();
    }


    bool asset_create_window::try_create() {

        auto registry = GLT::asset::registry::get_ref();
        if (!registry) {
            m_operation_error = "No asset registry";
            return false;
        }

        // ---- resolve target path ------------------------------------------------------
        std::filesystem::path filename = m_name_buffer;
        filename.replace_extension("");
        filename += ".";
        filename += GLT::asset::type_to_extension(m_target_type);

        const std::filesystem::path abs_target = m_target_dir / filename;
        const std::filesystem::path rel_target = GLT::project::extract_path_from_project_content_dir(abs_target);
        if (rel_target.empty()) {
            m_operation_error = "Location must be inside the project content directory.";
            return false;
        }

        // ---- build the runtime asset --------------------------------------------------
        if (m_target_type == GLT::asset::core_types::material) {

            auto asset = GLT::create_unique_ref<GLT::asset::material::material_asset>();
            asset->asset_type = m_target_type;
            asset->textures.fill(INVALID_HANDLE);
            asset->name = filename.stem().string();

            auto registered = registry->register_runtime(
                std::move(asset), rel_target, filename.stem().string());

            VALIDATE(registered.has_value(), return false, "",
                "asset_create: register_runtime failed for [{}]", rel_target.generic_string())

            auto save_result = registry->save(*registered);
            if (!save_result) {

                LOG(error, "asset_create: failed to write [{}] (error {})",
                    rel_target.generic_string(), static_cast<int>(save_result.error()));

                // Slot exists only in memory; drop it so a retry doesn't collide.
                registry->unload(*registered);
                m_operation_error = "Failed to write asset to disk.";
                return false;
            }

            LOG(info, "asset_create: created [{}]", rel_target.generic_string());

            if (m_auto_open)
                GLT::event_bus::post(asset_open_event{ m_target_type, rel_target });

            m_show_window = false;
            return true;
        }

        // Placeholder for future creatable types - the create button is only
        // enabled for types with a real branch above.
        m_operation_error = "Unsupported target type.";
        return false;
    }

}
