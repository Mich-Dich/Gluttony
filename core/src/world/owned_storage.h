
#pragma once

#include "world/component.h"
#include "world/component_registry.h"
#include "world/i_world.h"
#include "world/i_world_plugin.h"



// Plugin-defined components ===========================================================================================
//
// A plugin defines a component type without knowing the world implementation:
//
//      struct my_settings { f32 strength = 1.0f; std::string label; };
//
//      // on_load
//      GLT::world::component_registry::add_owned<my_settings>("my_plugin.settings", "My Plugin",
//          [](i_world& world, entity_id id) { ... ImGui inspector ... },       // draw (optional)
//          {},                                                                 // summary (optional)
//          [](const my_settings& v, std::vector<std::byte>& out) { ... },     // save: stable, append-only layout
//          [](my_settings& v, std::span<const std::byte> in) { ... },         // load: older (shorter) payloads must work
//          {}, GLT::world::component::scope::world);
//
//      // on_unload
//      GLT::world::component_registry::remove_owned<my_settings>();
//
//      // anywhere
//      my_settings* s = GLT::world::component_registry::get_world<my_settings>(world);
//
// The values live in an owned_storage<T> inside each world (instantiated in the plugin, never in the world's own ECS),
// so unloading the plugin cannot leave the world with code that is gone: remove_owned<T>() turns every value back into
// its saved payload first. The world keeps those payloads (also for worlds loaded while the plugin is not there), saves
// them unchanged and hands them to the type when the plugin registers it again. Nothing is lost by opening a world
// without the plugin.

namespace GLT::world::component {

    // CLASS DECLARATION ===============================================================================================

    template<typename T>
    class owned_storage final : public i_storage {
    public:

        using save_fn = std::function<void(const T&, std::vector<std::byte>&)>;
        using load_fn = std::function<void(T&, std::span<const std::byte>)>;

        owned_storage(i_world& world, save_fn save, load_fn load)
            : m_world(world), m_save(std::move(save)), m_load(std::move(load)) {}

        [[nodiscard]] bool has(entity_id id) const override { return m_world.alive(id) && m_values.contains(id); }

        void add(entity_id id) override {
            if (m_world.alive(id))
                m_values.try_emplace(id);
            m_world.mark_changed(id);
        }

        void remove(entity_id id) override {
            m_values.erase(id);
        }

        [[nodiscard]] void* get(entity_id id) override {
            auto it = m_values.find(id);
            return it != m_values.end() && m_world.alive(id) ? &it->second : nullptr;
        }

        [[nodiscard]] const void* get(entity_id id) const override {
            auto it = m_values.find(id);
            return it != m_values.end() && m_world.alive(id) ? &it->second : nullptr;
        }

        void copy(entity_id from, entity_id to) override {
            if (auto it = m_values.find(from); it != m_values.end() && m_world.alive(to))
                m_values[to] = it->second;
        }

        [[nodiscard]] std::vector<entity_id> entities() const override {
            std::vector<entity_id> out;
            out.reserve(m_values.size());
            for (const auto& [id, value] : m_values)
                if (m_world.alive(id))
                    out.push_back(id);
            return out;
        }

        void save(entity_id id, std::vector<std::byte>& out) const override {
            if (auto it = m_values.find(id); it != m_values.end() && m_save)
                m_save(it->second, out);
        }

        void load(entity_id id, std::span<const std::byte> in) override {
            T& value = m_values[id];
            value = T{};
            if (m_load)
                m_load(value, in);
        }

        [[nodiscard]] bool owns_data() const noexcept override { return true; }

        [[nodiscard]] std::shared_ptr<void> capture(entity_id id) const override {
            if constexpr (std::is_copy_constructible_v<T>)
                if (auto it = m_values.find(id); it != m_values.end())
                    return std::make_shared<T>(it->second);
            return {};
        }

    private:

        i_world&                                        m_world;
        save_fn                                         m_save;
        load_fn                                         m_load;
        std::unordered_map<entity_id, T>                m_values{};
    };

}


namespace GLT::world::component_registry {

    // FUNCTION DECLARATION ============================================================================================

    // Registers a plugin component (see the top of this file). [name] is stored in files: keep it stable
    template<typename T>
    void add_owned(std::string_view name, std::string_view category,
        std::function<void(i_world&, entity_id)> draw,
        std::function<std::string(i_world&, entity_id)> summary,
        typename component::owned_storage<T>::save_fn save,
        typename component::owned_storage<T>::load_fn load,
        std::vector<u64> required = {},
        component::scope scope = component::scope::entity) {

        const u64 hash = hash_of(name);
        g_type_hash<T> = hash;

        component::type_descriptor td{};
        td.meta.hash = hash;
        td.meta.name = std::string(name);
        td.meta.category = std::string(category);
        td.meta.required = std::move(required);
        td.meta.scope = scope;
        td.draw = std::move(draw);
        td.summary = std::move(summary);
        td.make_storage = [save = std::move(save), load = std::move(load)](i_world& world) -> GLT::unique_ref<component::i_storage> {
            return GLT::create_unique_ref<component::owned_storage<T>>(world, save, load);
        };

        register_meta(td.meta);
        if (auto* plugin = GLT::world::manager::get_ptr())
            plugin->register_component_type(td);
    }


    // Call from the plugin's on_unload: values become payloads the worlds keep (the plugin's code is still loaded here)
    template<typename T>
    bool remove_owned() {

        const u64 hash = g_type_hash<T>;
        if (hash == 0)
            return false;

        unregister_meta(hash);
        bool present = true;
        if (auto* plugin = GLT::world::manager::get_ptr())
            present = plugin->unregister_component_type(hash);
        g_type_hash<T> = 0;
        return present;
    }

}
