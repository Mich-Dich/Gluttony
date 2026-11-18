#include <util/pch.h>
#include "plugin/plugin.h"

#include <editor/i_editor.h>
#include <util/data_structures/thread_pool.h>

#include "host/script_host.h"
#include "component/binding_component.h"



namespace GLT::scripting_cpp {

    // CONSTANTS =======================================================================================================

    static constexpr std::string_view                   OWNER = GLT_MODULE_NAME;

    // i_plugin --------------------------------------------------------------------------------------------------------

    void plugin::on_load() {

        s_instance = this;
        register_binding_component();
        classes_changed();                              // installs the editor hook (Tools > Scripts)
    }


    void plugin::on_unload() {

        GLT::editor::remove_editor_hook(OWNER);
        if (auto* ed = GLT::editor::get())
            ed->unregister_all(OWNER);
        unregister_binding_component();
        if (!m_hosts.empty())
            LOG(warn, "{} script host(s) still alive while the scripting plugin unloads", m_hosts.size())
        s_instance = nullptr;
    }


    void plugin::update(f32 /*delta_time*/) {
        // Hosts are driven by their sessions
    }

    // i_script_plugin: registry ---------------------------------------------------------------------------------------

    void plugin::register_script_class_impl(script::script_class_info info) {

        if (m_classes.add(std::move(info)))
            classes_changed();
    }


    void plugin::unregister_script_class(std::string_view name) {

        m_classes.remove(name);
        classes_changed();
    }


    void plugin::unregister_by_registrant(std::string_view registrant) {

        // Instances first: their vtables, destructors and field tables live in the module that is about to unload
        for (script_host* host : std::vector<script_host*>(m_hosts))
            host->release_registrant(registrant);
        m_classes.remove_by_registrant(registrant);
        classes_changed();
    }

    // i_script_plugin: host factory -----------------------------------------------------------------------------------

    unique_ref<script::i_script_host> plugin::create_host() { return create_unique_ref<script_host>(*this); }

    // i_script_plugin: default globals --------------------------------------------------------------------------------

    void plugin::set_default_globals(std::string_view registrant, std::span<const script::default_global> globals) {
        m_globals.set(registrant, globals);
    }


    void plugin::clear_default_globals(std::string_view registrant) { m_globals.clear(registrant); }


    std::span<const script::default_global> plugin::default_globals() const { return m_globals.all(); }

    // i_script_plugin: introspection ----------------------------------------------------------------------------------

    std::span<const script::script_class_info> plugin::registered_classes() const { return m_classes.all(); }

    // hosts -----------------------------------------------------------------------------------------------------------

    void plugin::add_host(script_host* host) { m_hosts.push_back(host); }


    void plugin::remove_host(script_host* host) { std::erase(m_hosts, host); }


    script_host* plugin::running_host_for(const world::i_world* world) const {

        if (!world)
            return nullptr;
        for (script_host* host : m_hosts)
            if (host->world() == world && host->is_running())
                return host;
        return nullptr;
    }


    script_host* plugin::host_for(const world::i_world* world) const {

        if (!world)
            return nullptr;
        if (script_host* running = running_host_for(world))
            return running;
        for (script_host* host : m_hosts)
            if (host->world() == world)
                return host;
        return nullptr;
    }

    // editor menu -----------------------------------------------------------------------------------------------------

    void plugin::classes_changed() {

        // Menu items are rebuilt between frames: this can run inside a module load (editor menu callback, main thread)
        if (m_menu_dirty)
            return;
        m_menu_dirty = true;

        thread_pool::push_main([] {

            plugin* self = plugin::get();
            if (!self)
                return;
            self->m_menu_dirty = false;

            if (auto* ed = GLT::editor::get())
                ed->unregister_all(OWNER);

            GLT::editor::add_editor_hook(OWNER, [](GLT::editor::i_editor& ed) {

                plugin* self = plugin::get();
                if (!self)
                    return;

                for (const script::script_class_info& c : self->registered_classes()) {
                    if (c.kind != script::script_kind::editor_action && c.kind != script::script_kind::world_generator)
                        continue;

                    const std::string name(c.name);
                    const std::string label = c.kind == script::script_kind::world_generator
                        ? std::format("Scripts/World generators/{}", c.name)
                        : std::format("Scripts/{}", c.editor_label.empty() ? c.name : c.editor_label);

                    ed.register_menu_item(OWNER, label, [name] {
                        plugin* self = plugin::get();
                        auto* ed = GLT::editor::get();
                        if (!self || !ed)
                            return;
                        world::i_world* world = ed->edited_world();
                        if (!world) {
                            LOG(warn, "[{}]: no world is open", name)
                            return;
                        }
                        // In the world the editor shows: its host if it has one (it need not run), else a temporary one
                        std::expected<void, script::script_error> result{};
                        if (script_host* host = self->host_for(world))
                            result = host->run_action(name, world::INVALID_ENTITY, {});
                        else {
                            script_host temp(*self);
                            temp.set_context({ .world = world, .physics = nullptr, .events = nullptr, .assets = nullptr });
                            result = temp.run_action(name, world::INVALID_ENTITY, {});
                        }
                        if (!result) {
                            LOG(error, "[{}] failed: {}", name, result.error().message)
                        } else {
                            LOG(info, "[{}] done", name)
                        }
                    });
                }
            });
        });
    }

}
