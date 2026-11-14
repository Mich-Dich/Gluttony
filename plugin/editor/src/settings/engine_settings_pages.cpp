
#include "util/pch.h"
#include "engine_settings_pages.h"

#include <imgui.h>

#include <config/imgui_config.h>
#include <settings/engine_settings.h>
#include <util/ui/panel_collection.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    namespace {

        namespace ES = GLT::engine_settings;
        using GLT::reflect::member_descriptor;
        using GLT::reflect::type_kind;

        // The file the window edits. Starts with the project: that is what a new setting is usually meant for
        ES::scope                               s_scope = ES::scope::project;

        // annotations::options of a member, split up once ("1024=Low|2048=Medium" -> values 1024, 2048 + labels)
        struct choice_list {

            std::vector<f64>                    values{};
            std::vector<std::string>            labels{};
            std::vector<const char*>            label_pointers{};       // into [labels]
        };


        const choice_list& choices_of(const member_descriptor& member) {

            static std::unordered_map<const char*, choice_list> s_cache{};         // by the annotation's (static) string
            auto it = s_cache.find(member.options);
            if (it != s_cache.end())
                return it->second;

            choice_list list{};
            std::string_view rest = member.options;
            while (!rest.empty()) {

                const size_t bar = rest.find('|');
                std::string_view item = rest.substr(0, bar);
                rest = bar == std::string_view::npos ? std::string_view{} : rest.substr(bar + 1);

                f64 value = static_cast<f64>(list.values.size());                   // no "value=": 0, 1, 2, ...
                if (const size_t equals = item.find('='); equals != std::string_view::npos) {
                    const std::string_view number = item.substr(0, equals);
                    const auto parsed = std::from_chars(number.data(), number.data() + number.size(), value);
                    if (parsed.ec == std::errc{} && parsed.ptr == number.data() + number.size())
                        item = item.substr(equals + 1);
                    else
                        value = static_cast<f64>(list.values.size());               // an '=' that belongs to the label
                }
                list.values.push_back(value);
                list.labels.emplace_back(item);
            }
            for (const auto& label : list.labels)
                list.label_pointers.push_back(label.c_str());

            return s_cache.emplace(member.options, std::move(list)).first->second;
        }


        // The entry closest to [number] (a hand-edited file may hold a value in between)
        i32 choice_index(const choice_list& list, const f64 number) {

            i32 best = 0;
            for (size_t x = 1; x < list.values.size(); ++x)
                if (std::abs(list.values[x] - number) < std::abs(list.values[static_cast<size_t>(best)] - number))
                    best = static_cast<i32>(x);
            return best;
        }


        // "Shadows|Sun" -> page "Shadows", section "Sun". No category: page "General"
        std::string_view page_of(const member_descriptor& member) {

            const std::string_view category = (member.category && member.category[0]) ? std::string_view(member.category) : std::string_view("General");
            return category.substr(0, category.find('|'));
        }


        // Null-terminated: the rest of the (static) category string. nullptr: the member is in no section
        const char* section_of(const member_descriptor& member) {

            if (!member.category)
                return nullptr;
            const char* bar = std::strchr(member.category, '|');
            return (bar && bar[1]) ? bar + 1 : nullptr;
        }


        std::string number_text(const member_descriptor& member, const f64 number) {

            if (member.kind == type_kind::boolean)
                return number != 0.0 ? "on" : "off";
            if (member.options && member.options[0]) {
                const choice_list& list = choices_of(member);
                if (!list.labels.empty())
                    return list.labels[static_cast<size_t>(choice_index(list, number))];
            }
            if (member.kind == type_kind::f32 || member.kind == type_kind::f64)
                return std::format("{:.6g}", number);
            return std::format("{:.0f}", number);
        }


        // One row. Returns true if the value was changed (already written to the store)
        bool draw_setting(const ES::store::block& block, const member_descriptor& member) {

            ES::store& settings = ES::get_store();

            std::array<std::byte, 8> buffer{};
            settings.read(block, member, s_scope, buffer.data());
            const f64 shown = ES::detail::to_number(member.kind, buffer.data());
            settings.read_inherited(block, member, s_scope, buffer.data());
            const f64 inherited = ES::detail::to_number(member.kind, buffer.data());

            const std::string label = (member.display_name && member.display_name[0])
                ? std::string(member.display_name) : UI::pretty_name(member.name);

            // What the row does not show: on the project page, a value that this computer overrides
            std::string description = (member.tooltip && member.tooltip[0]) ? std::string(member.tooltip) : std::string{};
            if (s_scope == ES::scope::project && settings.is_set(block, member, ES::scope::user)) {
                settings.read(block, member, ES::scope::user, buffer.data());
                if (!description.empty())
                    description += ". ";
                description += std::format("This computer uses its own value: {}",
                    number_text(member, ES::detail::to_number(member.kind, buffer.data())));
            }
            const char* desc = description.empty() ? nullptr : description.c_str();

            const bool has_range = (member.flags & static_cast<u32>(GLT::reflect::member_flags::has_range)) != 0u
                && member.range_min < member.range_max;
            f64 edited = shown;
            bool changed = false;

            if (member.kind == type_kind::boolean) {

                bool value = shown != 0.0;
                changed = UI::settings_toggle(label.c_str(), desc, value, inherited != 0.0);
                edited = value ? 1.0 : 0.0;

            } else if (member.options && member.options[0] && !choices_of(member).values.empty()) {

                const choice_list& list = choices_of(member);
                i32 index = choice_index(list, shown);
                const i32 inherited_index = choice_index(list, inherited);
                changed = list.values.size() <= 3
                    ? UI::settings_segmented(label.c_str(), desc, index, inherited_index, list.label_pointers)
                    : UI::settings_combo(label.c_str(), desc, index, inherited_index, list.label_pointers);
                edited = list.values[static_cast<size_t>(std::clamp(index, 0, static_cast<i32>(list.values.size()) - 1))];

            } else if (member.kind == type_kind::f32 || member.kind == type_kind::f64) {

                f32 value = static_cast<f32>(shown);
                if (has_range) {
                    const f32 low = static_cast<f32>(member.range_min);
                    const f32 high = static_cast<f32>(member.range_max);
                    const bool wide = low > 0.f && high / low >= 50.f;          // 50 .. 5000: the low end needs the room
                    const char* format = (high - low) >= 100.f ? "%.0f" : ((high - low) >= 10.f ? "%.1f" : "%.2f");
                    changed = UI::settings_slider(label.c_str(), desc, value, static_cast<f32>(inherited), low, high, format,
                        wide ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None);
                } else
                    changed = UI::settings_drag(label.c_str(), desc, value, static_cast<f32>(inherited), 0.01f, 0.f, 0.f, "%.3f");
                edited = static_cast<f64>(value);

            } else {                                                            // integers

                constexpr f64 I32_MIN = -2147483648.0;
                constexpr f64 I32_MAX = 2147483647.0;
                i32 value = static_cast<i32>(std::clamp(shown, I32_MIN, I32_MAX));
                const i32 inherited_value = static_cast<i32>(std::clamp(inherited, I32_MIN, I32_MAX));
                if (has_range) {
                    const i32 low = static_cast<i32>(std::clamp(member.range_min, I32_MIN, I32_MAX));
                    const i32 high = static_cast<i32>(std::clamp(member.range_max, I32_MIN, I32_MAX));
                    changed = UI::settings_slider(label.c_str(), desc, value, inherited_value, low, high);
                } else {
                    f32 as_float = static_cast<f32>(value);
                    changed = UI::settings_drag(label.c_str(), desc, as_float, static_cast<f32>(inherited_value), 0.2f, 0.f, 0.f, "%.0f");
                    value = static_cast<i32>(std::round(as_float));
                }
                edited = static_cast<f64>(value);
            }

            if (!changed)
                return false;

            // Writing the inherited value removes the entry from the file (that is what "reset" does)
            ES::detail::from_number(member.kind, edited, buffer.data());
            settings.write(block, member, s_scope, buffer.data());
            return true;
        }


        bool draw_page(const std::string& block_name, const std::string& page_name) {

            const ES::store::block* block = ES::get_store().find(block_name);
            if (!block) {
                ImGui::TextDisabled("The plugin these settings belong to is not loaded");
                return false;
            }

            bool changed = false;
            bool section_open = false;
            const char* current_section = nullptr;

            for (const auto& member : block->desc.type->members) {

                if (!ES::is_setting(member) || page_of(member) != page_name)
                    continue;

                const char* section = section_of(member);
                const bool same = section_open && ((!section && !current_section)
                    || (section && current_section && std::strcmp(section, current_section) == 0));
                if (!same) {
                    if (section_open)
                        UI::settings_end_section();
                    UI::settings_begin_section(section);
                    current_section = section;
                    section_open = true;
                }

                changed |= draw_setting(*block, member);
            }

            if (section_open)
                UI::settings_end_section();
            return changed;
        }


        // Sidebar: which file the rows edit
        void draw_scope_switch() {

            static constexpr const char* SCOPES[] = { "Project", "This computer" };

            ImGui::Dummy(ImVec2(0.f, 2.f));
            i32 index = static_cast<i32>(s_scope);
            if (UI::segmented_control("##engine_settings_scope", index, SCOPES, ImGui::GetContentRegionAvail().x))
                s_scope = index == 0 ? ES::scope::project : ES::scope::user;

            ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::small));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextUnformatted(s_scope == ES::scope::project
                ? "Values of the project. Saved with it (config/engine.config) and used by the packaged game."
                : "Your changes on top of the project's values. Only on this computer (config/engine.user.config).");
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    void build_engine_settings_pages(settings_registry& registry) {

        registry.clear();
        registry.set_sidebar_header(&draw_scope_switch);

        for (const auto& block : ES::get_store().blocks()) {

            // Pages in the order their first member appears in the struct
            std::vector<std::string_view> pages{};
            for (const auto& member : block.desc.type->members) {
                if (!ES::is_setting(member))
                    continue;
                const std::string_view page = page_of(member);
                if (std::find(pages.begin(), pages.end(), page) == pages.end())
                    pages.push_back(page);
            }

            for (const std::string_view page : pages) {
                registry.add({
                    .group = block.desc.group.empty() ? std::string("Engine") : block.desc.group,
                    .name = std::string(page),
                    .description = "Used in the editor and in the packaged game",
                    .draw = [block_name = block.desc.name, page_name = std::string(page)]() { return draw_page(block_name, page_name); },
                    .save = []() { ES::save(); },
                });
            }
        }
    }


    u64 engine_settings_signature() {

        u64 signature = 14695981039346656037ull;
        for (const auto& block : ES::get_store().blocks()) {
            for (const char c : block.desc.name)
                signature = (signature ^ static_cast<u8>(c)) * 1099511628211ull;
            signature = (signature ^ reinterpret_cast<uintptr_t>(block.desc.type)) * 1099511628211ull;
        }
        return signature;
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
