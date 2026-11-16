#include "util/pch.h"
#include "asset_import.h"

#include "util/ui/panel_collection.h"
#include "config/imgui_config.h"
#include "window/base/window_chrome.h"

#include <asset/i_asset_registry.h>



namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr f32                               IMPORT_WINDOW_WIDTH = 720.0f;

    constexpr f32                               IMPORT_WINDOW_HEIGHT = 640.0f;

    // Pill colors of a file's state
    constexpr ImU32                             STATE_OK = IM_COL32(110, 200, 120, 255);       // imported

    constexpr ImU32                             STATE_BUSY = IM_COL32(110, 170, 240, 255);     // queued / importing

    constexpr ImU32                             STATE_WARN = IM_COL32(230, 180, 60, 255);      // will overwrite

    constexpr ImU32                             STATE_ERROR = IM_COL32(235, 100, 100, 255);    // conflict / failed

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    static std::string display_name_for(GLT::asset::type t);

    // Icon shown for an asset type (header of the file's card)
    static icon_manager::icon_type icon_for_type(GLT::asset::type t);

    // "import|calculate|ignore" -> { "import", "calculate", "ignore" }
    static std::vector<std::string> split_enumeration(std::string_view values);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static std::string display_name_for(GLT::asset::type t) {

        auto registry = GLT::asset::registry::get_ref();
        if (registry) {
            const auto n = registry->name_from_type(t);
            if (!n.empty())
                return UI::pretty_name(n);
        }
        return std::to_string(t.value);
    }


    static icon_manager::icon_type icon_for_type(const GLT::asset::type t) {

        switch (t.value) {
            case GLT::asset::core_types::static_mesh.value:
            case GLT::asset::core_types::procedural_mesh.value:
            case GLT::asset::core_types::dynamic_mesh.value:
            case GLT::asset::core_types::skeletal_mesh.value:
            case GLT::asset::core_types::mesh_collection.value:     return icon_manager::icon::mesh_asset;
            case GLT::asset::core_types::texture2d.value:
            case GLT::asset::core_types::texture3d.value:
            case GLT::asset::core_types::cube_map.value:            return icon_manager::icon::texture;
            case GLT::asset::core_types::material.value:            return icon_manager::icon::material;
            case GLT::asset::core_types::material_instance.value:   return icon_manager::icon::material_inst;
            case GLT::asset::core_types::world.value:
            case GLT::asset::core_types::region.value:              return icon_manager::icon::world;
            case GLT::asset::core_types::audio.value:               return icon_manager::icon::audio;
            default:                                                return icon_manager::icon::file;
        }
    }


    static std::vector<std::string> split_enumeration(const std::string_view values) {

        std::vector<std::string> out;
        for (size_t start = 0; start <= values.size(); ) {
            const auto pos = values.find('|', start);
            if (pos == std::string_view::npos) {
                out.emplace_back(values.substr(start));
                break;
            }
            out.emplace_back(values.substr(start, pos - start));
            start = pos + 1;
        }
        return out;
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    asset_import_window::asset_import_window(std::vector<std::filesystem::path> sources, std::filesystem::path target_dir) {

        make_window_name("Import Assets");
        m_window_specs.can_be_deleted = true;

        m_target_dir = std::move(target_dir);
        m_importing = false;
        m_imports_pending = 0;

        m_items.clear();
        m_items.reserve(sources.size());

        for (auto& src : sources) {

            import_item item{};
            item.source = std::move(src);
            item.target_name = item.source.stem().string();
            m_items.push_back(std::move(item));
        }

        rebuild_candidates();

        m_show_window = true;
        m_center_next_frame = true;
    }


    asset_import_window::~asset_import_window() = default;

    // CLASS PUBLIC ====================================================================================================

    void asset_import_window::window(const f32 /*delta_time*/) {

        if (!m_show_window)
            return;

        if (m_center_next_frame) {

            const ImGuiViewport* vp = ImGui::GetMainViewport();
            const ImVec2 center(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f);
            ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(IMPORT_WINDOW_WIDTH, std::min(IMPORT_WINDOW_HEIGHT, vp->Size.y * 0.85f)), ImGuiCond_Appearing);
            m_center_next_frame = false;
        }

        ImGui::SetNextWindowSizeConstraints(ImVec2(520.0f, 320.0f), ImVec2(FLT_MAX, FLT_MAX));
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        const bool open = ImGui::Begin(m_window_id.c_str(), &m_show_window, flags);
        ImGui::PopStyleVar();
        if (!open) {
            ImGui::End();
            return;
        }

        draw_header();

        // ---- scrollable content: destination + one card per file ------------------------------
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f, 10.f));
        ImGui::BeginChild("##import_content", ImVec2(0.f, -chrome::footer_height()), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();

        draw_destination_card();

        chrome::section_label("FILES", std::to_string(m_items.size()).c_str());
        if (m_items.empty())
            UI::empty_state("Nothing to import", "Drop files on the content browser or use its Import button");
        for (int i = 0; i < static_cast<int>(m_items.size()); ++i)
            draw_item_section(m_items[i], i);

        ImGui::EndChild();

        draw_footer();

        ImGui::End();
    }


    void asset_import_window::update(const f32 /*delta_time*/) {}


    void asset_import_window::dock_to(ImGuiID /*dock_id*/) { /* deliberately non-dockable */ }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void asset_import_window::rebuild_candidates() {

        auto registry = GLT::asset::registry::get_ref();
        if (!registry) return;

        for (auto& item : m_items) {

            item.candidates.clear();

            const auto bindings = registry->candidate_imports(item.source);
            item.candidates.assign(bindings.begin(), bindings.end());

            if (!item.candidates.empty())
                item.target_type = item.candidates.front().target_type;

            rebuild_options_for(item);
        }
    }


    void asset_import_window::rebuild_options_for(import_item& item) {

        item.options.clear();

        // Locate the factory that advertised this target_type
        const GLT::asset::factory::i_asset_factory_plugin* factory = nullptr;
        for (const auto& b : item.candidates) {
            if (b.target_type == item.target_type && b.factory) {
                factory = b.factory;
                break;
            }
        }
        if (!factory)
            return;

        const auto schema = factory->option_schema(item.target_type);
        item.options.reserve(schema.size());

        for (const auto& d : schema) {

            option_field f{};
            f.desc = d;

            switch (d.type) {

                case GLT::asset::factory::i_asset_factory_plugin::option_descriptor::kind::boolean:
                    f.bool_value = (d.default_value == "true" || d.default_value == "1");
                    break;

                case GLT::asset::factory::i_asset_factory_plugin::option_descriptor::kind::integer:
                    f.int_value = d.default_value.empty() ? 0 : std::atoi(std::string(d.default_value).c_str());
                    break;

                case GLT::asset::factory::i_asset_factory_plugin::option_descriptor::kind::real:
                    f.float_value = d.default_value.empty() ? 0.0f : static_cast<f32>(std::atof(std::string(d.default_value).c_str()));
                    break;

                default:
                    f.string_value = std::string(d.default_value);
                    break;
            }

            f.default_bool = f.bool_value;
            f.default_int = f.int_value;
            f.default_float = f.float_value;
            f.default_string = f.string_value;

            item.options.push_back(std::move(f));
        }
    }


    void asset_import_window::draw_header() {

        std::string count = std::to_string(m_items.size()) + (m_items.size() == 1 ? " file" : " files");
        if (m_importing)
            count += ", " + std::to_string(m_imports_pending) + " pending";

        chrome::header_options options{};
        options.subtitle = count.c_str();
        options.pill = m_importing ? "Importing" : nullptr;
        options.pill_color = STATE_BUSY;
        const chrome::header_bar bar = chrome::begin_header_bar(icon_manager::icon::folder_add, "Import Assets", options);
        chrome::end_header_bar(bar);
    }


    void asset_import_window::draw_destination_card() {

        UI::card_options options{};
        options.icon = icon_manager::get(icon_manager::icon::folder).tex_ref;
        options.icon_uv0 = icon_manager::get(icon_manager::icon::folder).uv0;
        options.icon_uv1 = icon_manager::get(icon_manager::icon::folder).uv1;
        options.has_icon = true;

        if (UI::begin_card("Destination", options)) {

            UI::property_custom("Folder", [&](const f32 /*width*/) {
                    return GLT::UI::draw_directory_picker("##target_dir", m_target_dir, PROJECT_CONTENT_DIR);
                }, "Content folder the imported assets are written to");

            constexpr bool default_override = false;
            UI::property_toggle("Overwrite existing", m_override_existing,
                "Allow imports that would replace a file that already exists in the folder", &default_override);
        }
        UI::end_card();
    }


    void asset_import_window::draw_item_section(import_item& item, int index) {

        ImGui::PushID(index);

        // ---- state of this file: drives the pill in the card header ----------------------------
        const bool has_conflict = !item.imported && target_exists(item);
        const char* state_text = nullptr;
        ImU32 state_color = 0;
        if (item.imported) {
            state_text = "Imported";
            state_color = STATE_OK;
        } else if (item.status.starts_with("Failed")) {
            state_text = "Failed";
            state_color = STATE_ERROR;
        } else if (item.status == "Queued...") {
            state_text = "Importing";
            state_color = STATE_BUSY;
        } else if (item.candidates.empty()) {
            state_text = "Not supported";
            state_color = STATE_ERROR;
        } else if (has_conflict) {
            state_text = m_override_existing ? "Will overwrite" : "Name conflict";
            state_color = m_override_existing ? STATE_WARN : STATE_ERROR;
        }

        const bool item_can_import = !m_importing
            && !item.candidates.empty()
            && !item.target_name.empty()
            && (!has_conflict || m_override_existing);

        // ---- card: icon, file name, type as summary, pill + Import button in the header ---------
        const std::string file_name = item.source.filename().string();
        const std::string type_name = item.candidates.empty() ? std::string("no import factory") : display_name_for(item.target_type);
        const char* button_label = item.imported ? "Re-import" : "Import";
        const f32 button_w = chrome::button_width(button_label);
        const f32 pill_w = state_text ? ImGui::CalcTextSize(state_text).x + 22.f + 8.f : 0.f;
        const auto icon = icon_manager::get(icon_for_type(item.target_type));

        UI::card_options options{};
        options.summary = type_name.c_str();
        options.collapsible = true;
        options.default_open = true;
        options.icon = icon.tex_ref;
        options.icon_uv0 = icon.uv0;
        options.icon_uv1 = icon.uv1;
        options.has_icon = true;
        options.header_actions_width = pill_w + button_w;
        options.header_actions = [&]() {
            if (state_text) {
                chrome::status_pill_item(state_text, state_color);
                ImGui::SameLine(0.f, 8.f);
            }
            ImGui::BeginDisabled(!item_can_import);
            if (UI::subtle_button(button_label, ImVec2(button_w, 0.f)))
                start_imports(index);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", item_can_import ? "Import only this file" : "Fix the issues below first");
            ImGui::EndDisabled();
        };

        if (UI::begin_card(file_name.c_str(), options)) {

            // --- name -------------------------------------------------------------------------------
            if (UI::property_text("Name", item.target_name, "Name of the asset file, without extension", "asset name")) {
                item.imported = false;                      // any edit invalidates the prior import
                item.status.clear();
            }

            // --- type -------------------------------------------------------------------------------
            if (!item.candidates.empty()) {

                std::vector<std::string> labels;
                std::vector<const char*> label_ptrs;
                labels.reserve(item.candidates.size());
                i32 current = 0;
                for (size_t i = 0; i < item.candidates.size(); ++i) {
                    labels.push_back(display_name_for(item.candidates[i].target_type));
                    if (item.candidates[i].target_type == item.target_type)
                        current = static_cast<i32>(i);
                }
                for (const auto& label : labels)
                    label_ptrs.push_back(label.c_str());

                if (UI::property_combo("Type", current, label_ptrs, "What the file is imported as") && current >= 0
                    && item.candidates[current].target_type != item.target_type) {

                    item.target_type = item.candidates[current].target_type;
                    item.imported = false;
                    item.status.clear();
                    rebuild_options_for(item);
                }
            } else
                UI::property_value("Type", "No import factory for this file type");

            // --- output path + hints ----------------------------------------------------------------
            UI::property_value("Output", resolved_target_path(item).generic_string(), "Where the asset file will be written");

            if (has_conflict) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(m_override_existing ? STATE_WARN : STATE_ERROR));
                ImGui::TextWrapped("%s", m_override_existing
                    ? "A file with this name exists and will be overwritten."
                    : "A file with this name exists. Rename it, or switch on \"Overwrite existing\".");
                ImGui::PopStyleColor();
            }
            if (!item.status.empty() && !item.imported && item.status != "Queued...") {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(STATE_ERROR));
                ImGui::TextWrapped("%s", item.status.c_str());
                ImGui::PopStyleColor();
            }

            // --- factory options --------------------------------------------------------------------
            if (!item.options.empty()) {

                chrome::section_label("IMPORT OPTIONS");
                for (int i = 0; i < static_cast<int>(item.options.size()); ++i)
                    draw_option_field(item.options[i], i);
            }

            // --- engine options ---------------------------------------------------------------------
            chrome::section_label("ENGINE");
            constexpr bool default_off = false;
            UI::property_toggle("Editor preview only", item.editor_preview_only,
                "Keep the runtime asset transient; do not persist it to the content tree", &default_off);
            UI::property_toggle("Strip editor data", item.strip_editor_data,
                "Drop authoring-only chunks (thumbnails, gizmos, debug bounds)", &default_off);
        }
        UI::end_card();

        ImGui::Dummy(ImVec2(0.f, 2.f));
        ImGui::PopID();
    }


    void asset_import_window::draw_option_field(option_field& field, int index) {

        using kind = GLT::asset::factory::i_asset_factory_plugin::option_descriptor::kind;

        ImGui::PushID(index);

        const std::string label = field.desc.label.empty() ? UI::pretty_name(field.desc.key) : std::string(field.desc.label);
        const std::string tooltip(field.desc.tooltip);
        const char* desc = tooltip.empty() ? nullptr : tooltip.c_str();

        switch (field.desc.type) {

            case kind::boolean:
                UI::property_toggle(label.c_str(), field.bool_value, desc, &field.default_bool);
                break;

            case kind::integer:
                UI::property_custom(label.c_str(), [&](const f32 /*width*/) {
                        return ImGui::DragInt("##v", &field.int_value, 0.2f);
                    }, desc, field.int_value == field.default_int, [&]() { field.int_value = field.default_int; });
                break;

            case kind::real:
                UI::property_drag(label.c_str(), field.float_value, 0.01f, 0.f, 0.f, "%.3f", desc, &field.default_float);
                break;

            case kind::enumeration: {

                const std::vector<std::string> values = split_enumeration(field.desc.enumeration_values);
                std::vector<std::string> shown;
                std::vector<const char*> shown_ptrs;
                shown.reserve(values.size());
                i32 current = 0;
                i32 default_index = 0;
                for (size_t i = 0; i < values.size(); ++i) {
                    shown.push_back(UI::pretty_name(values[i]));
                    if (values[i] == field.string_value)
                        current = static_cast<i32>(i);
                    if (values[i] == field.default_string)
                        default_index = static_cast<i32>(i);
                }
                for (const auto& s : shown)
                    shown_ptrs.push_back(s.c_str());

                const bool changed = values.size() <= 3
                    ? UI::property_segmented(label.c_str(), current, shown_ptrs, desc, &default_index)
                    : UI::property_combo(label.c_str(), current, shown_ptrs, desc, &default_index);
                if (changed && current >= 0 && current < static_cast<i32>(values.size()))
                    field.string_value = values[current];
                break;
            }

            case kind::path:
                UI::property_text(label.c_str(), field.string_value, desc, "path");
                break;
        }

        ImGui::PopID();
    }


    void asset_import_window::draw_footer() {

        auto registry = GLT::asset::registry::get_ref();

        bool can_import = registry && !m_items.empty() && !m_importing;
        u32 conflicts = 0;
        u32 imported = 0;
        for (const auto& item : m_items) {
            if (item.candidates.empty() || item.target_name.empty())
                can_import = false;
            if (item.imported)
                ++imported;
            else if (target_exists(item))
                ++conflicts;
        }

        chrome::begin_footer();

        // ---- left: what is going on --------------------------------------------------------------
        ImGui::AlignTextToFramePadding();
        if (m_importing)
            ImGui::TextDisabled("Importing... %u left", m_imports_pending);
        else if (conflicts > 0 && !m_override_existing)
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(STATE_ERROR), "%u name conflict%s", conflicts, conflicts == 1 ? "" : "s");
        else if (imported > 0)
            ImGui::TextDisabled("%u of %zu imported", imported, m_items.size());
        else
            ImGui::TextDisabled("Ready to import");

        // ---- right: Cancel + Import All -----------------------------------------------------------
        const char* cancel_label = "Cancel";
        const char* import_label = "Import All";
        const f32 cancel_w = chrome::button_width(cancel_label);
        const f32 import_w = chrome::button_width(import_label);

        ImGui::SameLine();
        chrome::align_right(cancel_w + import_w + 6.f);

        if (UI::subtle_button(cancel_label, ImVec2(cancel_w, 0.f))) {
            m_show_window = false;
            m_items.clear();
        }

        ImGui::SameLine(0.f, 6.f);
        ImGui::BeginDisabled(!can_import);
        if (UI::accent_button(import_label, ImVec2(import_w, 0.f)))
            start_imports();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Import every file and close this window when all are done");
        ImGui::EndDisabled();

        chrome::end_footer();
    }


    std::filesystem::path asset_import_window::resolved_target_path(const import_item& item) const {

        std::filesystem::path p = item.target_name;
        p.replace_extension(std::string(".") + std::string(GLT::asset::type_to_extension(item.target_type)));
        return m_target_dir / p;
    }


    bool asset_import_window::target_exists(const import_item& item) const {

        std::error_code error{};
        const bool exists = GLT::vfs::exists(resolved_target_path(item), error);
        return exists && !error;
    }


    std::vector<std::byte> asset_import_window::pack_type_specific(const import_item& item) const {

        std::vector<std::byte> out;
        out.reserve(item.options.size() * 8);

        auto push_bytes = [&](const void* p, size_t n) {
            const auto* b = static_cast<const std::byte*>(p);
            out.insert(out.end(), b, b + n);
        };

        for (const auto& f : item.options) {

            switch (f.desc.type) {

                case GLT::asset::factory::i_asset_factory_plugin::option_descriptor::kind::boolean: {
                    const u8 v = f.bool_value ? 1 : 0;
                    push_bytes(&v, sizeof(v));
                    break;
                }
                case GLT::asset::factory::i_asset_factory_plugin::option_descriptor::kind::integer: {
                    const i32 v = f.int_value;
                    push_bytes(&v, sizeof(v));
                    break;
                }
                case GLT::asset::factory::i_asset_factory_plugin::option_descriptor::kind::real: {
                    const f32 v = f.float_value;
                    push_bytes(&v, sizeof(v));
                    break;
                }
                case GLT::asset::factory::i_asset_factory_plugin::option_descriptor::kind::enumeration:
                case GLT::asset::factory::i_asset_factory_plugin::option_descriptor::kind::path: {
                    const u32 len = static_cast<u32>(f.string_value.size());
                    push_bytes(&len, sizeof(len));
                    push_bytes(f.string_value.data(), len);
                    break;
                }
            }
        }

        return out;
    }


    void asset_import_window::start_imports(int only_index) {

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return;

        m_importing = true;

        m_close_when_all_done = (only_index < 0);               // Batch behaviour only for "Import All"

        struct job {
            std::filesystem::path           source;
            GLT::asset::type                target_type;
            std::filesystem::path           target_path;
            std::vector<std::byte>          type_specific;      // owned
            bool                            editor_preview_only;
            bool                            strip_editor_data;
        };
        std::vector<job> jobs;

        for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {

            if (only_index >= 0 && i != only_index)
                continue;

            auto& item = m_items[i];
            item.status.clear();                                // Clear status only for the items we're actually submitting
            if (item.candidates.empty()) {
                item.status = "No import factory available.";
                continue;
            }

            const auto target_path = resolved_target_path(item);
            std::error_code error{};
            if ((GLT::vfs::exists(target_path, error) && !error) && !m_override_existing) {
                item.status = "Target already exists - skipped.";
                continue;
            }

            item.status = "Queued...";

            jobs.push_back({
                item.source,
                item.target_type,
                target_path,
                pack_type_specific(item),
                item.editor_preview_only,
                item.strip_editor_data,
            });
        }

        if (jobs.empty()) {
            m_importing = false;
            m_close_when_all_done = false;
            return;
        }

        m_imports_pending = static_cast<u32>(jobs.size());

        // NOTE: `this` is captured. The window lives as long as the editor layer (see event subscription), so this stays valid
        // If windows ever become transient, swap in a weak handle
        for (auto& j : jobs) {

            GLT::thread_pool::push([this, registry, j = std::move(j)]() {

                GLT::asset::import_options opts{};
                opts.editor_preview_only = j.editor_preview_only;
                opts.strip_editor_data = j.strip_editor_data;
                opts.type_specific = j.type_specific;

                auto result = registry->import(j.source, j.target_type, j.target_path, opts);
                GLT::thread_pool::push_main([this, src = j.source, result = std::move(result)]() mutable {

                    for (auto& it : m_items) {
                        if (it.source == src) {
                            if (result) {
                                it.status = "Imported.";
                                it.imported = true;
                            } else {

                                it.status = std::string("Failed the import: [") + GLT::util::enum_to_string(result.error()) + "]";
                                LOG(warn, "{}", it.status)
                            }

                            break;
                        }
                    }

                    if (m_imports_pending > 0)
                        --m_imports_pending;

                    if (m_imports_pending == 0) {

                        m_importing = false;
                        if (m_close_when_all_done) {

                            m_close_when_all_done = false;
                            m_show_window = false;
                        }
                    }
                });
            });
        }
    }

}
