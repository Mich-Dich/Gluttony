#include "util/pch.h"
#include "component/binding_component.h"

#include <imgui.h>
#include <scripting/script_binding_component.h>
#include <scripting/script_fields.h>
#include <world/owned_storage.h>
#include <reflection/archive_binary.h>
#include "util/ui/panel_collection.h"

#include "plugin/plugin.h"
#include "host/script_host.h"



namespace GLT::scripting_cpp {

    using namespace GLT::script;

    namespace {

        // CONSTANTS ===================================================================================================

        // Payload: u32 version, u32 count, per binding { str script, str instance, u8 enabled, blob editor settings }.
        // Append-only per binding is not possible (count prefix): a new version gets a new number
        constexpr u32                                   PAYLOAD_VERSION = 1;
        constexpr u32                                   MAX_BINDINGS = 1024;

        // payload -----------------------------------------------------------------------------------------------------

        void save(const script_binding_component& c, std::vector<std::byte>& out) {

            reflect::binary_writer w{};
            w.write(PAYLOAD_VERSION);
            w.write(static_cast<u32>(c.bindings.size()));
            for (const script_binding& b : c.bindings) {
                w.write(b.script_name);
                w.write(b.instance_name);
                w.write(static_cast<u8>(b.enabled ? 1 : 0));
                w.write_blob(b.editor_settings);
            }
            const auto& data = w.data();
            out.insert(out.end(), data.begin(), data.end());
        }


        void load(script_binding_component& c, std::span<const std::byte> in) {

            reflect::binary_reader r(in);
            u32 version = 0, count = 0;
            if (!r.read(version) || version != PAYLOAD_VERSION || !r.read(count))
                return;
            for (u32 i = 0; i < count && i < MAX_BINDINGS && r.ok(); ++i) {
                script_binding b{};
                u8 enabled = 1;
                std::span<const std::byte> settings{};
                if (!r.read(b.script_name) || !r.read(b.instance_name) || !r.read(enabled) || !r.read_blob(settings))
                    break;
                b.enabled = enabled != 0;
                b.editor_settings.assign(settings.begin(), settings.end());
                c.bindings.push_back(std::move(b));
            }
        }


        std::string summary(world::i_world& world, world::entity_id entity) {

            const auto* comp = find_bindings(world, entity);
            if (!comp || comp->bindings.empty())
                return "no scripts";
            if (comp->bindings.size() == 1)
                return comp->bindings.front().script_name;
            return std::format("{} scripts", comp->bindings.size());
        }

        // field rows --------------------------------------------------------------------------------------------------

        std::vector<std::string> split_options(std::string_view text) {

            std::vector<std::string> out{};
            while (!text.empty()) {
                const size_t bar = text.find('|');
                std::string_view part = text.substr(0, bar);
                if (const size_t eq = part.find('='); eq != std::string_view::npos)
                    part = part.substr(eq + 1);            // "value=label": shown by label, stored by index (simple form)
                out.emplace_back(part);
                if (bar == std::string_view::npos)
                    break;
                text.remove_prefix(bar + 1);
            }
            return out;
        }


        template <typename I>
        bool int_row(const char* label, I& value, const reflect::member_descriptor& md, const char* desc, const I* def) {

            const bool ranged = (md.flags & static_cast<u32>(reflect::member_flags::has_range)) != 0;

            if (md.options && md.options[0]) {
                const std::vector<std::string> names = split_options(md.options);
                std::vector<const char*> items{};
                for (const auto& n : names)
                    items.push_back(n.c_str());
                i32 index = static_cast<i32>(value);
                const i32 def_index = def ? static_cast<i32>(*def) : 0;
                if (UI::property_combo(label, index, items, desc, def ? &def_index : nullptr)) {
                    value = static_cast<I>(index);
                    return true;
                }
                return false;
            }

            i32 buffer = static_cast<i32>(value);
            const i32 def_buffer = def ? static_cast<i32>(*def) : 0;
            bool changed = false;
            if (ranged)
                changed = UI::property_slider(label, buffer, static_cast<i32>(md.range_min), static_cast<i32>(md.range_max), "%d", desc,
                    def ? &def_buffer : nullptr);
            else
                changed = UI::property_custom(label, [&](f32 width) {
                    ImGui::SetNextItemWidth(width);
                    return ImGui::DragInt("##value", &buffer, 0.2f);
                }, desc, !def || buffer == def_buffer, [&] { buffer = def_buffer; });
            if (changed) {
                if constexpr (std::is_unsigned_v<I>)
                    buffer = std::max(buffer, 0);
                value = static_cast<I>(buffer);
            }
            return changed;
        }


        // One [[=editor]] field. [def]: the same field of a default instance (reset arrow), may be null
        bool field_row(const script_field_info& field, void* ptr, const void* def) {

            const reflect::member_descriptor& md = *field.member;
            const std::string label = UI::pretty_name((md.display_name && md.display_name[0]) ? md.display_name : md.name);
            const char* text = label.c_str();
            const char* desc = (md.tooltip && md.tooltip[0]) ? md.tooltip : nullptr;
            const bool ranged = (md.flags & static_cast<u32>(reflect::member_flags::has_range)) != 0;
            const bool color = (md.flags & static_cast<u32>(reflect::member_flags::color)) != 0;
            const f32 lo = static_cast<f32>(md.range_min), hi = static_cast<f32>(md.range_max);

            // Exact types first (struct-like types the reflection only knows by template name)
            if (field.type_id == script_type_id_of<glm::vec2>())
                return UI::property_drag(text, *static_cast<glm::vec2*>(ptr), 0.05f, lo, hi, "%.3f", desc, static_cast<const glm::vec2*>(def));
            if (field.type_id == script_type_id_of<glm::vec3>())
                return color ? UI::property_color(text, *static_cast<glm::vec3*>(ptr), desc, static_cast<const glm::vec3*>(def))
                             : UI::property_drag(text, *static_cast<glm::vec3*>(ptr), 0.05f, lo, hi, "%.3f", desc, static_cast<const glm::vec3*>(def));
            if (field.type_id == script_type_id_of<glm::vec4>())
                return UI::property_color(text, *static_cast<glm::vec4*>(ptr), true, desc, static_cast<const glm::vec4*>(def));

            switch (md.kind) {

                case reflect::type_kind::boolean:
                    return UI::property_toggle(text, *static_cast<bool*>(ptr), desc, static_cast<const bool*>(def));

                case reflect::type_kind::i8:     return int_row(text, *static_cast<i8*>(ptr), md, desc, static_cast<const i8*>(def));
                case reflect::type_kind::i16:    return int_row(text, *static_cast<i16*>(ptr), md, desc, static_cast<const i16*>(def));
                case reflect::type_kind::i32:    return int_row(text, *static_cast<i32*>(ptr), md, desc, static_cast<const i32*>(def));
                case reflect::type_kind::i64:    return int_row(text, *static_cast<i64*>(ptr), md, desc, static_cast<const i64*>(def));
                case reflect::type_kind::u8:     return int_row(text, *static_cast<u8*>(ptr), md, desc, static_cast<const u8*>(def));
                case reflect::type_kind::u16:    return int_row(text, *static_cast<u16*>(ptr), md, desc, static_cast<const u16*>(def));
                case reflect::type_kind::u32:    return int_row(text, *static_cast<u32*>(ptr), md, desc, static_cast<const u32*>(def));
                case reflect::type_kind::u64:    return int_row(text, *static_cast<u64*>(ptr), md, desc, static_cast<const u64*>(def));

                case reflect::type_kind::f32: {
                    auto& value = *static_cast<f32*>(ptr);
                    const auto* d = static_cast<const f32*>(def);
                    return ranged ? UI::property_slider(text, value, lo, hi, "%.3f", desc, d)
                                  : UI::property_drag(text, value, 0.05f, 0.f, 0.f, "%.3f", desc, d);
                }

                case reflect::type_kind::f64: {
                    auto& value = *static_cast<f64*>(ptr);
                    f32 buffer = static_cast<f32>(value);
                    const f32 d = def ? static_cast<f32>(*static_cast<const f64*>(def)) : 0.0f;
                    const bool changed = ranged ? UI::property_slider(text, buffer, lo, hi, "%.3f", desc, def ? &d : nullptr)
                                                : UI::property_drag(text, buffer, 0.05f, 0.f, 0.f, "%.3f", desc, def ? &d : nullptr);
                    if (changed)
                        value = static_cast<f64>(buffer);
                    return changed;
                }

                case reflect::type_kind::string_type:
                    return UI::property_text(text, *static_cast<std::string*>(ptr), desc);

                case reflect::type_kind::path_type: {
                    auto& value = *static_cast<std::filesystem::path*>(ptr);
                    std::string buffer = value.generic_string();
                    if (UI::property_text(text, buffer, desc)) {
                        value = buffer;
                        return true;
                    }
                    return false;
                }

                default:
                    UI::property_value(text, "(not editable here)", desc);
                    return false;
            }
        }

        // inspector ---------------------------------------------------------------------------------------------------

        std::string unique_instance_name(const script_binding_component& comp, std::string_view script_name) {

            std::string base(script_name);
            if (const size_t dot = base.rfind('.'); dot != std::string::npos)
                base = base.substr(dot + 1);
            auto taken = [&](std::string_view name) {
                return std::any_of(comp.bindings.begin(), comp.bindings.end(), [&](const script_binding& b) { return b.instance_name == name; });
            };
            if (!taken(base))
                return base;
            for (u32 i = 2;; ++i)
                if (const std::string candidate = std::format("{}_{}", base, i); !taken(candidate))
                    return candidate;
        }


        void draw_settings(script_binding& binding, const script_class_info& cls) {

            bool any = false;
            for (const script_field_info& f : cls.fields)
                any = any || in_set(f.role, field_set::editor);
            if (!any)
                return;

            // A temporary instance holds the values while they are drawn; the defaults come from a second, fresh one
            i_script* value = cls.create();
            i_script* defaults = cls.create();
            (void)apply_fields(cls, *value, binding.editor_settings, field_set::editor);

            bool changed = false;
            std::string_view group{};
            for (const script_field_info& f : cls.fields) {
                if (!in_set(f.role, field_set::editor))
                    continue;
                const std::string_view category = f.editor_category ? std::string_view(f.editor_category) : std::string_view{};
                if (!category.empty() && category != group) {
                    group = category;
                    ImGui::Spacing();
                    ImGui::TextDisabled("%.*s", static_cast<int>(group.size()), group.data());
                }
                ImGui::PushID(f.member->name);
                changed |= field_row(f, field_address(cls, *value, f), field_address(cls, *defaults, f));
                ImGui::PopID();
            }

            if (changed)
                binding.editor_settings = capture_fields(cls, *value, field_set::editor);
            cls.destroy(defaults);
            cls.destroy(value);
        }


        void draw_live(script_host& host, world::entity_id entity, const script_binding& binding, const script_class_info* cls) {

            const std::string_view name = binding.instance_name.empty() ? std::string_view(binding.script_name) : std::string_view(binding.instance_name);
            const script_instance_info* live = host.find_instance(entity, name);
            if (!live) {
                UI::property_value("Running", "not started (see the log)");
                return;
            }
            const f64 average = live->update_count ? static_cast<f64>(live->total_update_ns) / static_cast<f64>(live->update_count) / 1.0e6 : 0.0;
            UI::property_value("Running", std::format("{}{}, {} updates, {:.3f} ms avg", live->enabled ? "yes" : "disabled",
                live->sleeping ? ", sleeping" : "", live->update_count, average));

            const script_instance_id id = live->id;
            if (!live->enabled && ImGui::SmallButton("Enable"))
                host.set_enabled(id, true);
            if (live->sleeping) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Wake"))
                    host.wake(id);
            }

            if (!cls)
                return;
            for (const script_method_info& m : cls->callable_methods) {
                const char* label = (m.label && m.label[0]) ? m.label : m.name;
                if (ImGui::Button(label)) {
                    if (auto r = host.call(binding.script_name, m.name, entity, name, {}); !r)
                        LOG(error, "{}: {}", label, r.error().message)
                }
                ImGui::SameLine();
            }
            if (!cls->callable_methods.empty())
                ImGui::NewLine();
        }


        void draw(world::i_world& world, world::entity_id entity) {

            plugin* self = plugin::get();
            script_binding_component* comp = find_bindings(world, entity);
            if (!self || !comp)
                return;

            // Entity script classes of the loaded modules (names are static strings of the modules)
            std::vector<const script_class_info*> classes{};
            for (const script_class_info& c : self->registered_classes())
                if (c.kind == script_kind::entity_script)
                    classes.push_back(&c);
            std::sort(classes.begin(), classes.end(), [](const auto* a, const auto* b) { return a->name < b->name; });

            script_host* live_host = self->running_host_for(&world);
            std::optional<size_t> remove{};

            for (size_t i = 0; i < comp->bindings.size(); ++i) {

                script_binding& b = comp->bindings[i];
                ImGui::PushID(static_cast<int>(i));

                const script_class_info* cls = self->find_class(b.script_name);
                const std::string title = b.instance_name.empty() ? b.script_name : b.instance_name;
                const std::string summary_text = cls ? b.script_name : b.script_name + " (not loaded)";

                UI::card_options options{};
                options.collapsible = true;
                options.summary = summary_text.c_str();
                options.header_actions_width = 22.f;
                options.header_actions = [&] {
                    if (UI::menu_button("##binding_menu", 18.f, "Script options"))
                        ImGui::OpenPopup("##binding_ctx");
                    if (ImGui::BeginPopup("##binding_ctx")) {
                        if (ImGui::MenuItem("Remove"))
                            remove = i;
                        if (ImGui::MenuItem("Reset settings", nullptr, false, !b.editor_settings.empty()))
                            b.editor_settings.clear();
                        ImGui::EndPopup();
                    }
                };

                if (UI::begin_card(title.c_str(), options)) {

                    UI::begin_properties();

                    // Class: the registered entity scripts, plus the bound one if its module is not loaded
                    std::vector<const char*> names{};
                    i32 index = -1;
                    for (size_t k = 0; k < classes.size(); ++k) {
                        names.push_back(classes[k]->name.data());           // static, null terminated (define_static_string)
                        if (classes[k]->name == b.script_name)
                            index = static_cast<i32>(k);
                    }
                    const std::string missing = b.script_name + " (not loaded)";
                    if (index < 0) {
                        names.push_back(missing.c_str());
                        index = static_cast<i32>(names.size()) - 1;
                    }
                    if (UI::property_combo("Script", index, names, "The script class (the namespace path below the module's root namespace)")
                        && index >= 0 && static_cast<size_t>(index) < classes.size()) {
                        b.script_name = std::string(classes[static_cast<size_t>(index)]->name);
                        b.editor_settings.clear();
                    }

                    UI::property_text("Instance name", b.instance_name, "Unique on the entity: siblings and code find the instance by it");
                    const bool duplicate = std::count_if(comp->bindings.begin(), comp->bindings.end(), [&](const script_binding& o) {
                        return o.instance_name == b.instance_name;
                    }) > 1;
                    if (duplicate)
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Another script on this entity has this name: only the first runs");

                    const bool default_enabled = true;
                    UI::property_toggle("Enabled", b.enabled, "Off: no instance is created when the world runs", &default_enabled);

                    if (cls)
                        draw_settings(b, *cls);
                    else
                        ImGui::TextDisabled("The class is not registered: build and load the game module (Tools > Game module)");

                    if (live_host)
                        draw_live(*live_host, entity, b, cls);

                    UI::end_properties();
                }
                UI::end_card();
                ImGui::PopID();
            }

            if (remove && *remove < comp->bindings.size()) {
                comp->bindings.erase(comp->bindings.begin() + static_cast<std::ptrdiff_t>(*remove));
                world.mark_changed(entity);
            }

            // Add -----------------------------------------------------------------------------------------------------
            if (classes.empty()) {
                ImGui::TextDisabled("No entity scripts registered: build and load the game module (Tools > Game module)");
                return;
            }
            if (UI::subtle_button("+ Add script"))
                ImGui::OpenPopup("##add_script");
            if (ImGui::BeginPopup("##add_script")) {
                for (const script_class_info* c : classes)
                    if (ImGui::MenuItem(c->name.data())) {
                        comp->bindings.push_back(script_binding{
                            .script_name = std::string(c->name),
                            .instance_name = unique_instance_name(*comp, c->name),
                            .enabled = true,
                            .editor_settings = {},
                        });
                        world.mark_changed(entity);
                    }
                ImGui::EndPopup();
            }
        }

    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    void register_binding_component() {

        world::component_registry::add_owned<script_binding_component>(BINDING_COMPONENT_NAME, "Scripting",
            [](world::i_world& world, world::entity_id entity) { draw(world, entity); },
            [](world::i_world& world, world::entity_id entity) { return summary(world, entity); },
            [](const script_binding_component& c, std::vector<std::byte>& out) { save(c, out); },
            [](script_binding_component& c, std::span<const std::byte> in) { load(c, in); },
            {}, world::component::scope::both);
    }


    void unregister_binding_component() { (void)world::component_registry::remove_owned<script_binding_component>(); }

}
