#include "util/pch.h"
#include "asset_import.h"

#include "util/ui/pannel_collection.h"

#include <plugin_system/i_asset_registry_plugin.h>



namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr f32                               IMPORT_WINDOW_WIDTH = 760.0f;

    constexpr f32                               IMPORT_WINDOW_HEIGHT_MIN = 360.0f;

    constexpr f32                               OPTION_LABEL_WIDTH = 180.0f;

    constexpr f32                               ITEM_BUTTON_WIDTH = 110.0f;

    // Light green tint used on the header of successfully-imported items.
    constexpr ImVec4                            IMPORTED_BG = ImVec4(0.28f, 0.62f, 0.32f, 0.55f);

    constexpr ImVec4                            IMPORTED_BG_HOVER = ImVec4(0.32f, 0.72f, 0.36f, 0.70f);

    constexpr ImVec4                            IMPORTED_BG_ACTIVE = ImVec4(0.32f, 0.72f, 0.36f, 0.90f);

    // Warning / error tints for the conflict hint.
    constexpr ImVec4                            HINT_WARN = ImVec4(0.96f, 0.72f, 0.20f, 1.0f);

    constexpr ImVec4                            HINT_ERROR = ImVec4(0.94f, 0.36f, 0.36f, 1.0f);

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    std::string display_name_for(GLT::asset::type t);

    std::vector<std::filesystem::path> list_subdirs(const std::filesystem::path& dir);

    bool draw_directory_picker(const char* id, std::filesystem::path& in_out, const std::filesystem::path& root);

    bool draw_dir_tree(const std::filesystem::path& dir, const std::filesystem::path& current, std::filesystem::path& out_selected);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    std::string display_name_for(GLT::asset::type t) {

        auto registry = GLT::asset::registry::get_ref();
        if (registry) {
            const auto n = registry->name_from_type(t);
            if (!n.empty())
                return std::string(n);
        }
        return std::to_string(t.value);
    }


    std::vector<std::filesystem::path> list_subdirs(const std::filesystem::path& dir) {

        std::vector<std::filesystem::path> out;
        std::error_code error{};
        auto it = GLT::vfs::directory_iterator(dir, error);
        if (error)
            return out;

        for (auto& e : it) {
            if (!e.is_directory(error))
                continue;

            const auto name = e.path().filename().string();
            if (!name.empty() && name.front() == '.')
                continue;

            out.push_back(e.path());
        }
        std::sort(out.begin(), out.end());
        return out;
    }


    bool draw_dir_tree(const std::filesystem::path& dir, const std::filesystem::path& current, std::filesystem::path& out_selected) {

        bool picked = false;
        const auto sub = list_subdirs(dir);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

        if (dir == current)
            flags |= ImGuiTreeNodeFlags_Selected;

        if (sub.empty())
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        if (!current.empty() && current != dir) {
            for (auto p = current.parent_path(); !p.empty(); p = p.parent_path()) {
                if (p == dir) {
                    flags |= ImGuiTreeNodeFlags_DefaultOpen;
                    break;
                }
                if (p == p.root_path())
                    break;
            }
        }

        ImGui::PushID(dir.string().c_str());

        const std::string label = dir.filename().empty() ? dir.generic_string() : dir.filename().string();
        const bool open = ImGui::TreeNodeEx(label.c_str(), flags);

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            out_selected = dir;
            picked = true;
        }

        if (open && !sub.empty()) {
            for (const auto& s : sub)
                if (draw_dir_tree(s, current, out_selected))
                    picked = true;
            ImGui::TreePop();
        }

        ImGui::PopID();
        return picked;
    }


    bool draw_directory_picker(const char* id, std::filesystem::path& in_out, const std::filesystem::path& root) {

        bool changed = false;
        ImGui::PushID(id);

        if (ImGui::Button(in_out.generic_string().c_str(), ImVec2(-FLT_MIN, 0)))
            ImGui::OpenPopup("##picker");

        if (ImGui::BeginPopup("##picker")) {

            if (ImGui::BeginChild("##tree_scroll", ImVec2(440.0f, 300.0f), true)) {

                if (root.empty()) {
                    ImGui::TextDisabled("(no content root configured)");
                } else {
                    std::filesystem::path selected;
                    if (draw_dir_tree(root, in_out, selected)) {
                        in_out  = selected;
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }

        ImGui::PopID();
        return changed;
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    asset_import_window::asset_import_window(std::vector<std::filesystem::path> sources, std::filesystem::path target_dir) {

        make_window_name("Import Assets");
        m_window_specs.can_be_deleted = true;

        m_target_dir      = std::move(target_dir);
        m_importing       = false;
        m_imports_pending = 0;

        m_items.clear();
        m_items.reserve(sources.size());

        for (auto& src : sources) {

            import_item item{};
            item.source      = std::move(src);
            item.target_name = item.source.stem().string();
            m_items.push_back(std::move(item));
        }

        rebuild_candidates();

        m_show_window       = true;
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
            ImGui::SetNextWindowPos (center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(IMPORT_WINDOW_WIDTH, 0.0f), ImGuiCond_Appearing);

            m_center_next_frame = false;
        }

        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;

        if (!ImGui::Begin(m_window_id.c_str(), &m_show_window, flags)) {
            ImGui::End();
            return;
        }

        // ---- header: target directory picker ---------------------------------------------------
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Import into:");
        ImGui::SameLine(120.0f);
        ImGui::SetNextItemWidth(-FLT_MIN);
        draw_directory_picker("##target_dir", m_target_dir, PROJECT_CONTENT_DIR);

        // ---- override toggle -------------------------------------------------------------------
        ImGui::Checkbox("Override existing files", &m_override_existing);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("If checked, imports that would overwrite an existing file will proceed.");

        ImGui::Separator();

        // ---- per-file sections -----------------------------------------------------------------
        if (ImGui::BeginChild("##items_scroll", ImVec2(0.f, 500.f), true)) {
            for (int i = 0; i < static_cast<int>(m_items.size()); ++i)
                draw_item_section(m_items[i], i);
        }
        ImGui::EndChild();

        ImGui::Separator();
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

        // Locate the factory that advertised this target_type.
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
                    f.float_value = d.default_value.empty() ? 0.0f : std::atof(std::string(d.default_value).c_str());
                    break;

                default:
                    f.string_value = std::string(d.default_value);
                    break;
            }

            item.options.push_back(std::move(f));
        }
    }


    void asset_import_window::draw_item_section(import_item& item, int index) {

        ImGui::PushID(index);

        // ---- green tint on the header if already imported ---------------------------------------
        if (item.imported) {
            ImGui::PushStyleColor(ImGuiCol_Header,        IMPORTED_BG);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IMPORTED_BG_HOVER);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,  IMPORTED_BG_ACTIVE);
        }

        const std::string header = item.source.filename().string()
            + (item.status.empty() ? "" : "   —   " + item.status);

        const bool open = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

        if (item.imported) {
            ImGui::PopStyleColor(3);
        }

        if (open) {

            // --- Name ------------------------------------------------------------------------
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Name");
            ImGui::SameLine(OPTION_LABEL_WIDTH);
            ImGui::SetNextItemWidth(-FLT_MIN);

            char name_buf[256];
            std::snprintf(name_buf, sizeof(name_buf), "%s", item.target_name.c_str());
            if (ImGui::InputText("##name", name_buf, sizeof(name_buf))) {
                item.target_name = name_buf;
                item.imported    = false;               // any edit invalidates the prior import
                item.status.clear();
            }

            // --- Type ------------------------------------------------------------------------
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Type");
            ImGui::SameLine(OPTION_LABEL_WIDTH);
            ImGui::SetNextItemWidth(-FLT_MIN);

            const std::string preview = item.candidates.empty()
                ? std::string("(no factory)")
                : display_name_for(item.target_type);

            if (ImGui::BeginCombo("##type", preview.c_str())) {

                for (const auto& cand : item.candidates) {

                    const bool selected = (cand.target_type == item.target_type);
                    const auto label    = display_name_for(cand.target_type);

                    if (ImGui::Selectable(label.c_str(), selected)) {
                        if (item.target_type != cand.target_type) {
                            item.target_type = cand.target_type;
                            item.imported    = false;
                            item.status.clear();
                            rebuild_options_for(item);
                        }
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            // --- Resolved path (read-only preview) -------------------------------------------
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Output");
            ImGui::SameLine(OPTION_LABEL_WIDTH);
            ImGui::TextDisabled("%s", resolved_target_path(item).generic_string().c_str());

            // --- Conflict hint ---------------------------------------------------------------
            const bool has_conflict = target_exists(item);

            if (has_conflict && !item.imported) {

                if (m_override_existing) {
                    ImGui::TextColored(HINT_WARN, "Existing file will be overwritten.");
                } else {
                    ImGui::TextColored(HINT_ERROR, "Name conflict — rename or tick \"Override existing files\".");
                }
            }

            ImGui::Spacing();

            // --- Per-item Import button (right-aligned) --------------------------------------
            const bool item_can_import = !m_importing
                && !item.candidates.empty()
                && !item.target_name.empty()
                && (!has_conflict || m_override_existing);

            {
                const f32 avail = ImGui::GetContentRegionAvail().x;
                if (avail > ITEM_BUTTON_WIDTH)
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - ITEM_BUTTON_WIDTH));

                ImGui::BeginDisabled(!item_can_import);
                if (ImGui::Button(item.imported ? "Re-import" : "Import", ImVec2(ITEM_BUTTON_WIDTH, 0)))
                    start_imports(index);
                ImGui::EndDisabled();
            }

            ImGui::Spacing();

            // --- Engine toggles --------------------------------------------------------------
            if (ImGui::TreeNodeEx("Engine Options")) {

                ImGui::Checkbox("Editor preview only", &item.editor_preview_only);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Keep the runtime asset transient; do not persist to the content tree.");

                ImGui::Checkbox("Strip editor data", &item.strip_editor_data);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Drop authoring-only chunks (thumbnails, gizmos, debug bounds).");

                ImGui::TreePop();
            }

            // --- Factory options -------------------------------------------------------------
            if (!item.options.empty()) {

                if (ImGui::TreeNodeEx("Factory Options", ImGuiTreeNodeFlags_DefaultOpen)) {

                    for (int i = 0; i < static_cast<int>(item.options.size()); ++i)
                        draw_option_field(item.options[i], i);

                    ImGui::TreePop();
                }
            }

            ImGui::Spacing();
        }

        ImGui::PopID();
        ImGui::Separator();
    }



    void asset_import_window::draw_option_field(option_field& field, int index) {

        ImGui::PushID(index);

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(std::string(field.desc.label).c_str());

        if (!field.desc.tooltip.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%.*s", (int)field.desc.tooltip.size(), field.desc.tooltip.data());

        ImGui::SameLine(OPTION_LABEL_WIDTH);
        ImGui::SetNextItemWidth(-FLT_MIN);

        switch (field.desc.type) {

            case GLT::asset::factory::i_asset_factory_plugin::option_descriptor::kind::boolean:
                ImGui::Checkbox("##v", &field.bool_value);
                break;

            case GLT::asset::factory::i_asset_factory_plugin::option_descriptor::kind::integer:
                ImGui::InputInt("##v", &field.int_value);
                break;

            case GLT::asset::factory::i_asset_factory_plugin::option_descriptor::kind::real:
                ImGui::InputFloat("##v", &field.float_value);
                break;

            case GLT::asset::factory::i_asset_factory_plugin::option_descriptor::kind::enumeration: {

                std::vector<std::string> items;
                std::string_view ev = field.desc.enumeration_values;
                for (size_t start = 0; start <= ev.size(); ) {
                    const auto pos = ev.find('|', start);
                    if (pos == std::string_view::npos) {
                        items.emplace_back(ev.substr(start));
                        break;
                    }
                    items.emplace_back(ev.substr(start, pos - start));
                    start = pos + 1;
                }

                int current = 0;
                for (int i = 0; i < (int)items.size(); ++i)
                    if (items[i] == field.string_value) { current = i; break; }

                std::vector<const char*> cstrs;
                cstrs.reserve(items.size());
                for (auto& s : items) cstrs.push_back(s.c_str());

                if (ImGui::Combo("##v", &current, cstrs.data(), (int)cstrs.size()) && current >= 0)
                    field.string_value = items[current];

                break;
            }

            case GLT::asset::factory::i_asset_factory_plugin::option_descriptor::kind::path: {

                char buf[512];
                std::snprintf(buf, sizeof(buf), "%s", field.string_value.c_str());
                if (ImGui::InputText("##v", buf, sizeof(buf)))
                    field.string_value = buf;

                break;
            }
        }

        ImGui::PopID();
    }


    void asset_import_window::draw_footer() {

        auto registry = GLT::asset::registry::get_ref();

        bool can_import = registry && !m_items.empty() && !m_importing;
        if (can_import) {
            for (const auto& item : m_items) {
                if (item.candidates.empty() || item.target_name.empty()) {
                    can_import = false;
                    break;
                }
            }
        }

        // Left side: batch status text.
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("(%zu file%s)%s",
            m_items.size(), m_items.size() == 1 ? "" : "s",
            m_importing ? "  — importing..." : "");

        // Right side: buttons, right-aligned.
        ImGui::SameLine();

        const auto& style = ImGui::GetStyle();
        const char* cancel_label = "Cancel";
        const char* import_label = "Import All";

        const f32 cancel_w = ImGui::CalcTextSize(cancel_label).x + style.FramePadding.x * 2.0f;
        const f32 import_w = ImGui::CalcTextSize(import_label).x + style.FramePadding.x * 2.0f;
        const f32 total_w  = cancel_w + import_w + style.ItemSpacing.x;

        const f32 avail = ImGui::GetContentRegionAvail().x;
        if (avail > total_w)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - total_w));

        if (UI::gray_button(cancel_label)) {
            m_show_window = false;
            m_items.clear();
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!can_import);
        if (ImGui::Button(import_label))
            start_imports();
        ImGui::EndDisabled();
    }


    std::filesystem::path asset_import_window::resolved_target_path(const import_item& item) const {

        std::filesystem::path p = item.target_name;
        p.replace_extension(std::string(".") + std::string(GLT::asset::extension_for_type(item.target_type)));
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

        m_close_when_all_done = (only_index < 0);               // Batch behaviour only for "Import All".

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
            item.status.clear();                                // Clear status only for the items we're actually submitting.
            if (item.candidates.empty()) {
                item.status = "No import factory available.";
                continue;
            }

            const auto target_path = resolved_target_path(item);
            std::error_code error{};
            if ((GLT::vfs::exists(target_path, error) && !error) && !m_override_existing) {
                item.status = "Target already exists — skipped.";
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
            m_importing           = false;
            m_close_when_all_done = false;
            return;
        }

        m_imports_pending = static_cast<u32>(jobs.size());

        // NOTE: `this` is captured. The window lives as long as the editor layer (see event subscription), so this stays valid. 
        // If windows ever become transient, swap in a weak handle.
        for (auto& j : jobs) {

            GLT::thread_pool::push([this, registry, j = std::move(j)]() {

                GLT::asset::import_options opts{};
                opts.editor_preview_only = j.editor_preview_only;
                opts.strip_editor_data   = j.strip_editor_data;
                opts.type_specific       = j.type_specific;

                auto result = registry->import(j.source, j.target_type, j.target_path, opts);
                GLT::thread_pool::push_main([this, src = j.source, result = std::move(result)]() mutable {

                    for (auto& it : m_items) {
                        if (it.source == src) {
                            if (result) {
                                it.status = "Imported.";
                                it.imported = true;
                            } else
                                it.status = std::string("Failed (") + std::to_string(static_cast<int>(result.error())) + ")";

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
