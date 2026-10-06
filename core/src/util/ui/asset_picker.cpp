
#include "util/pch.h"
#include "asset_picker.h"

#include <imgui.h>

#include "config/project.h"
#include "asset/i_asset_registry.h"
#include "util/data_structures/thread_pool.h"
#include "util/ui/pannel_collection.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::UI {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    namespace {

        std::mutex                                          g_root_mutex;
        std::filesystem::path                               g_asset_root{};             // empty = not configured


        // One immutable, thread-safe scan result. Readers grab a shared_ptr
        // and hold it for as long as they need; writers swap in a new one.
        struct scan_snapshot {

            std::vector<std::filesystem::path>              paths;
            std::chrono::steady_clock::time_point           generated{};
        };


        struct cache_entry {

            std::shared_ptr<const scan_snapshot>            current;
            std::atomic_bool                                scanning{ false };
        };


        // Process-wide scan cache. Lives for the lifetime of the program;
        // the shutdown race (worker finishes after the singleton dies) is not
        // possible because the singleton outlives thread_pool::shutdown().
        class picker_cache {
        public:

            static picker_cache& instance() {

                static picker_cache c;
                return c;
            }


            // Never blocks. Returns whatever is cached, or nullptr if a scan
            // is in flight and no previous result exists. Kicks off a new
            // scan when the cached result is older than `max_age`.
            std::shared_ptr<const scan_snapshot> get(const std::filesystem::path& root, std::string_view extension, std::chrono::seconds max_age) {

                const std::string key = make_key(root, extension);

                std::lock_guard lock(m_mutex);

                cache_entry& entry = m_entries[key];
                const auto now = std::chrono::steady_clock::now();
                const bool stale = !entry.current || (now - entry.current->generated) > max_age;

                if (stale && !entry.scanning.exchange(true))
                    kick_scan(key, root, std::string(extension));

                return entry.current;
            }


            void invalidate_all() {

                std::lock_guard lock(m_mutex);
                m_entries.clear();
            }


            void invalidate(const std::filesystem::path& root, std::string_view ext) {

                std::lock_guard lock(m_mutex);
                m_entries.erase(make_key(root, ext));
            }

        private:

            static std::string make_key(const std::filesystem::path& root, std::string_view ext) {

                std::string k = root.generic_string();
                k.push_back('|');
                k.append(ext);
                return k;
            }


            void kick_scan(std::string key, std::filesystem::path root, std::string extension) {

                GLT::thread_pool::push([this, key = std::move(key), root = std::move(root), extension = std::move(extension)]() mutable {

                        auto snap = std::make_shared<scan_snapshot>();
                        snap->generated = std::chrono::steady_clock::now();

                        const std::string suffix = "." + extension;
                        std::error_code error{};
                        auto iterator = GLT::vfs::recursive_directory_iterator(root);
                        if (error)
                            return;

                        for (const auto& entry : iterator) {

                            if (!entry.is_regular_file(error))
                                continue;

                            // Convert to project-relative at the boundary. Everything downstream - the cache, the popup, resolve_path
                            // speaks the same language as the registry (which now stores virtual_path as project-relative).
                            const auto rel = GLT::project::to_content_relative(entry.path());
                            if (rel.empty())
                                continue;                                           // outside content dir; not our asset

                            if (rel.extension() != suffix)
                                continue;

                            snap->paths.push_back(rel);
                        }

                        std::sort(snap->paths.begin(), snap->paths.end());          // Stable across rescans: sort by generic path.

                        std::lock_guard lock(m_mutex);
                        if (auto it = m_entries.find(key); it != m_entries.end()) {
                            it->second.current  = std::move(snap);
                            it->second.scanning = false;
                        }
                    });
            }

            std::mutex                                          m_mutex;
            std::unordered_map<std::string, cache_entry>        m_entries;
        };


        std::string extension_for(GLT::asset::type t) { return std::string(GLT::asset::extension_for_type(t)); }


        // Resolve a chosen path to a handle, going through the registry so that hot-reload and refcounting stay consistent.
        std::optional<GLT::asset::handle>
        resolve_path(const GLT::ref<GLT::asset::i_asset_registry_plugin>& registry, const std::filesystem::path& path) {

            if (auto h = registry->find(path); h != INVALID_HANDLE)
                return h;

            auto loaded = registry->load(path);
            if (!loaded)
                return std::nullopt;

            return *loaded;
        }


        // Access trait: abstracts “read the handle”, “write the handle”, “clear the handle” over both a raw handle and an asset_ref.
        template<typename H>
        struct picker_handle_access;


        template<>
        struct picker_handle_access<GLT::asset::handle> {
            static GLT::asset::handle get(const GLT::asset::handle& h) noexcept { return h; }
            static void set(GLT::asset::handle& h, GLT::asset::handle v) noexcept { h = v; }
            static void clear(GLT::asset::handle& h) noexcept { h = INVALID_HANDLE; }
        };


        template<>
        struct picker_handle_access<GLT::asset::asset_ref> {
            static GLT::asset::handle get(const GLT::asset::asset_ref& h) noexcept { return h.get(); }
            static void set(GLT::asset::asset_ref& h, GLT::asset::handle v) { h = GLT::asset::asset_ref{ v }; }
            static void clear(GLT::asset::asset_ref& h) { h.reset(); }
        };


        template<typename H>
        bool asset_picker_widget_impl(const asset_picker_options& opts, H& in_out, f32 available_width) {

            using access = picker_handle_access<H>;

            auto registry = GLT::asset::registry::get_ref();
            if (!registry) {
                ImGui::TextDisabled("no asset registry");
                return false;
            }

            const std::filesystem::path root = opts.root.empty() ? get_asset_root() : opts.root;
            const std::string ext = extension_for(opts.filter);

            const GLT::asset::handle current = access::get(in_out);
            const auto& info = registry->info(current);
            const std::string display = (current == INVALID_HANDLE || info.name.empty())
                ? std::string(opts.placeholder)
                : info.name;

            bool changed = false;

            const ImGuiStyle& style = ImGui::GetStyle();
            const f32 frame_h = ImGui::GetFrameHeight();
            const f32 clear_w = (opts.allow_clear && current != INVALID_HANDLE) ? (frame_h + style.ItemSpacing.x) : 0.f;
            const f32 field_w = available_width - clear_w - frame_h - style.ItemSpacing.x;

            char display_buf[256];
            std::snprintf(display_buf, sizeof(display_buf), "%s", display.c_str());

            ImGui::SetNextItemWidth(field_w);
            ImGui::InputText("##value", display_buf, sizeof(display_buf), ImGuiInputTextFlags_ReadOnly);

            const ImVec2 field_min = ImGui::GetItemRectMin();
            const ImVec2 field_max = ImGui::GetItemRectMax();
            const f32 field_screen_w = field_max.x - field_min.x;

            if (ImGui::IsItemClicked())
                ImGui::OpenPopup("##picker_popup");

            ImGui::SameLine();
            if (ImGui::Button("...", ImVec2(frame_h, 0)))
                ImGui::OpenPopup("##picker_popup");

            if (opts.allow_clear && current != INVALID_HANDLE) {
                ImGui::SameLine();
                if (ImGui::Button("x", ImVec2(frame_h, 0))) {
                    access::clear(in_out);
                    changed = true;
                }
            }

            if (ImGui::IsPopupOpen("##picker_popup")) {
                ImGui::SetNextWindowPos(ImVec2(field_min.x, field_max.y), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(field_screen_w, 0.0f), ImGuiCond_Always);
            }

            ImGui::SetNextWindowSizeConstraints(ImVec2(420, 320), ImVec2(720, 560));
            if (ImGui::BeginPopup("##picker_popup")) {

                static char search_buf[128] = "";
                if (ImGui::IsWindowAppearing())
                    search_buf[0] = '\0';

                const auto snap = picker_cache::instance().get(root, ext, std::chrono::seconds(10));

                if (ImGui::Button("Refresh"))
                    invalidate_asset_cache(root, ext);

                ImGui::SameLine();
                ImGui::SetNextItemWidth(220.f);
                ImGui::InputTextWithHint("##search", "Filter...", search_buf, sizeof(search_buf));
                const std::string_view needle{ search_buf };

                ImGui::SameLine();
                if (snap)
                    ImGui::TextDisabled("%zu item%s", snap->paths.size(), snap->paths.size() == 1 ? "" : "s");
                else
                    ImGui::TextDisabled("scanning...");

                ImGui::Separator();

                if (snap) {
                    const f32 row_h = ImGui::GetTextLineHeightWithSpacing();
                    if (ImGui::BeginChild("##list", ImVec2(0, row_h * 14), true)) {

                        for (const auto& path : snap->paths) {

                            const std::string shown = (path.parent_path() / path.stem()).generic_string();

                            if (!needle.empty() && shown.find(needle) == std::string::npos)
                                continue;

                            const GLT::asset::handle live = access::get(in_out);
                            const bool selected = (live != INVALID_HANDLE) && registry->info(live).virtual_path == path;

                            if (ImGui::Selectable(shown.c_str(), selected)) {
                                if (auto h = resolve_path(registry, path)) {
                                    if (*h != access::get(in_out)) {
                                        access::set(in_out, *h);
                                        changed = true;
                                    }
                                } else
                                    LOG(warn, "load failed for [{}]", path.generic_string())

                                ImGui::CloseCurrentPopup();
                            }
                        }
                    }
                    ImGui::EndChild();
                }

                if (opts.allow_clear && access::get(in_out) != INVALID_HANDLE) {
                    ImGui::Separator();
                    if (ImGui::Selectable("Clear")) {
                        access::clear(in_out);
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                }

                ImGui::EndPopup();
            }

            return changed;
        }

    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    void set_asset_root(std::filesystem::path root) {

        std::lock_guard lock(g_root_mutex);
        g_asset_root = std::move(root);
    }


    const std::filesystem::path& get_asset_root() {

        std::lock_guard lock(g_root_mutex);

        if (g_asset_root.empty()) {
            LOG(warn, "asset_picker: asset root never set; call GLT::UI::set_asset_root(PROJECT_CONTENT_DIR) at startup");
            g_asset_root = std::filesystem::current_path();     // degraded fallback
        }
        return g_asset_root;
    }


    void invalidate_asset_cache() { picker_cache::instance().invalidate_all(); }


    void invalidate_asset_cache(const std::filesystem::path& root, std::string_view extension) { 

        picker_cache::instance().invalidate(root, extension);
    }

    // ---- widget (shared) --------------------------------------------------------------------------

    bool asset_picker_widget(const asset_picker_options& opts, GLT::asset::handle& in_out, f32 available_width) {
        return asset_picker_widget_impl(opts, in_out, available_width);
    }


    bool asset_picker_widget(const asset_picker_options& opts, GLT::asset::asset_ref& in_out, f32 available_width) {
        return asset_picker_widget_impl(opts, in_out, available_width);
    }

    // ---- standalone entry point -------------------------------------------------------------------

    bool asset_picker(const asset_picker_options& opts, GLT::asset::handle& in_out) {

        ImGui::PushID(opts.label.data());
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", opts.label.data());
        ImGui::SameLine();
        const bool changed = asset_picker_widget(opts, in_out, ImGui::GetContentRegionAvail().x);
        ImGui::PopID();
        return changed;
    }


    bool asset_picker(const asset_picker_options& opts, GLT::asset::asset_ref& in_out) {

        ImGui::PushID(opts.label.data());
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", opts.label.data());
        ImGui::SameLine();
        const bool changed = asset_picker_widget(opts, in_out, ImGui::GetContentRegionAvail().x);
        ImGui::PopID();
        return changed;
    }

    // ---- table-row entry point --------------------------------------------------------------------

    bool table_row_asset_picker(std::string_view label, GLT::asset::handle& in_out, GLT::asset::type filter, const char* desc,
        bool allow_clear) {

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        size_t pos = label.find("##");
        std::string_view display = (pos != std::string_view::npos) ? label.substr(0, pos) : label;
        ImGui::Text("%.*s", static_cast<int>(display.length()), display.data());

        if (desc) {
            ImGui::SameLine();
            GLT::UI::shift_cursor_pos(ImGui::GetContentRegionAvail().x - 12.f, 0.f);
            GLT::UI::help_marker(desc);
        }

        ImGui::TableSetColumnIndex(1);

        ImGui::PushID(label.data());
        ImGui::PushID(&in_out);

        asset_picker_options opts{};
        opts.label = label;
        opts.filter = filter;
        opts.allow_clear = allow_clear;
        const bool changed = asset_picker_widget(opts, in_out, ImGui::GetContentRegionAvail().x);

        ImGui::PopID();
        ImGui::PopID();

        return changed;
    }


    bool table_row_asset_picker(std::string_view label, GLT::asset::asset_ref& in_out, GLT::asset::type filter, const char* desc,
        bool allow_clear) {

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        size_t pos = label.find("##");
        std::string_view display = (pos != std::string_view::npos) ? label.substr(0, pos) : label;
        ImGui::Text("%.*s", static_cast<int>(display.length()), display.data());

        if (desc) {
            ImGui::SameLine();
            GLT::UI::shift_cursor_pos(ImGui::GetContentRegionAvail().x - 12.f, 0.f);
            GLT::UI::help_marker(desc);
        }

        ImGui::TableSetColumnIndex(1);

        ImGui::PushID(label.data());
        ImGui::PushID(&in_out);

        asset_picker_options opts{};
        opts.label = label;
        opts.filter = filter;
        opts.allow_clear = allow_clear;
        const bool changed = asset_picker_widget(opts, in_out, ImGui::GetContentRegionAvail().x);

        ImGui::PopID();
        ImGui::PopID();

        return changed;
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
