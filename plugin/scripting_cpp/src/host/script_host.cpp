#include "util/pch.h"
#include "host/script_host.h"

#include <scripting/script_fields.h>
#include <world/i_world.h>
#include <reflection/archive_binary.h>

#include "plugin/plugin.h"



namespace GLT::scripting_cpp {

    using namespace GLT::script;

    // CONSTANTS =======================================================================================================

    static constexpr u32                                SNAPSHOT_VERSION = 1;

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static u64 hash_bytes(std::span<const std::byte> bytes) {

        u64 h = 0xcbf29ce484222325ull;
        for (const std::byte b : bytes) {
            h ^= static_cast<u8>(b);
            h *= 0x100000001b3ull;
        }
        return h ^ bytes.size();
    }


    static std::string failure_key(world::entity_id entity, std::string_view instance, std::string_view script, u64 settings) {

        return std::format("{}:{}/{}/{}/{}", entity.index, entity.generation, instance, script, settings);
    }


    static u64 elapsed_ns(std::chrono::steady_clock::time_point since) {

        return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - since).count());
    }

    // CLASS PUBLIC ====================================================================================================

    script_host::script_host(plugin& owner)
        : m_plugin(owner) { m_plugin.add_host(this); }


    script_host::~script_host() {

        stop_all();
        destroy_resources(std::nullopt);
        m_plugin.remove_host(this);
    }


    void script_host::set_context(const script_host_context& ctx) {

        // Another (or no) world: the entity slots belong to the old one. Stop them while it may still be alive
        if (ctx.world != m_env.world)
            for (size_t i = m_slots.size(); i-- > 0;)
                if (i < m_slots.size() && m_slots[i]->entity.is_valid())
                    destroy_slot(m_slots[i]->id);
        m_env = ctx;
    }


    void script_host::refresh(world::entity_id entity) { reconcile(entity); }


    void script_host::refresh_all() { reconcile(); }


    void script_host::set_running(const bool running) {

        if (running == m_running)
            return;

        if (!running) {
            stop_all();
            destroy_resources(std::nullopt);            // session resources belong to one run
            m_pending.clear();
            m_failed.clear();
            m_reported.clear();
            m_despawn_requests.clear();
            m_detach_requests.clear();
            m_running = false;
            return;
        }

        m_running = true;
        m_total_time = 0.0f;
        for (const default_global& g : m_plugin.default_globals())
            if (auto r = attach_global(g.script_name, g.instance_name); !r)
                LOG(warn, "Default global [{}] ({}) not attached: {}", g.script_name, g.instance_name, r.error().message)
        run_world_generators();
    }


    void script_host::release_registrant(std::string_view registrant) {

        if (registrant.empty())
            return;
        (void)snapshot_and_release(registrant);         // keeps the persist state in m_pending for the re-created instances
        destroy_resources(registrant);
    }

    // globals ---------------------------------------------------------------------------------------------------------

    std::expected<script_instance_id, script_error> script_host::attach_global(std::string_view script_name, std::string_view instance_name) {

        if (!m_running)
            return std::unexpected(script_error{ script_error::code::no_host, "the script host is not running" });

        const script_class_info* cls = m_plugin.find_class(script_name);
        if (!cls)
            return std::unexpected(script_error{ script_error::code::class_not_registered, std::format("no script class [{}]", script_name) });
        if (cls->kind != script_kind::global_script)
            return std::unexpected(script_error{ script_error::code::wrong_kind, std::format("[{}] is not a global script", script_name) });

        const std::string_view name = instance_name.empty() ? script_name : instance_name;
        for (const auto& s : m_slots)
            if (!s->entity.is_valid() && s->instance_name == name)
                return std::unexpected(script_error{ script_error::code::duplicate_name, std::format("a global named [{}] exists", name) });

        slot* s = create_slot(world::INVALID_ENTITY, *cls, name, {}, 0);
        if (!s)
            return std::unexpected(script_error{ script_error::code::script_threw, std::format("[{}] threw in on_start", script_name) });
        return s->id;
    }


    void script_host::detach_global(script_instance_id id) {

        if (const slot* s = find(id); s && !s->entity.is_valid())
            destroy_slot(id);
    }

    // controls --------------------------------------------------------------------------------------------------------

    void script_host::set_enabled(script_instance_id id, const bool enabled) {

        if (slot* s = find(id))
            s->enabled = enabled;
    }


    void script_host::wake(script_instance_id id) {

        if (slot* s = find(id); s && s->sleeping) {
            s->sleeping = false;
            s->sleep_timer = -1.0f;
            s->woke = true;
        }
    }


    void script_host::control(script_instance_id id, const control_op op, const f32 seconds) {

        slot* s = find(id);
        if (!s)
            return;

        switch (op) {
            case control_op::sleep:
            case control_op::sleep_until_contact:
                s->sleeping = true;
                s->sleep_timer = -1.0f;
                break;
            case control_op::sleep_for:
                s->sleeping = true;
                s->sleep_timer = std::max(seconds, 0.0f);
                break;
            case control_op::wake:
                wake(id);
                break;
            case control_op::disable:
                s->enabled = false;
                break;
            case control_op::destroy:
                if (s->entity.is_valid())
                    m_despawn_requests.push_back(s->entity);
                else
                    m_detach_requests.push_back(s->id);
                break;
        }
    }


    bool script_host::is_sleeping(script_instance_id id) const {

        const slot* s = find(id);
        return s && s->sleeping;
    }

    // session resources -----------------------------------------------------------------------------------------------

    void* script_host::session_resource_erased(std::string_view key, u32 type_hash, u32 size, u32 alignment,
        void (*construct)(void* out, void* factory), void* factory, void (*destroy)(void*), std::string_view registrant) {

        if (auto it = m_resources.find(std::string(key)); it != m_resources.end()) {
            if (it->second.type_hash != type_hash) {
                LOG(error, "Session resource [{}] was created with another type", key)
                throw std::runtime_error(std::format("session resource [{}] requested with another type", key));
            }
            return it->second.memory;
        }

        resource r{ .type_hash = type_hash, .memory = nullptr, .alignment = alignment, .destroy = destroy, .registrant = std::string(registrant) };
        r.memory = ::operator new(size, std::align_val_t(alignment));
        try {
            construct(r.memory, factory);
        } catch (...) {
            ::operator delete(r.memory, std::align_val_t(alignment));
            throw;
        }
        return m_resources.emplace(std::string(key), std::move(r)).first->second.memory;
    }

    // one-shot / live calls -------------------------------------------------------------------------------------------

    std::expected<void, script_error> script_host::run_action(std::string_view script_name, world::entity_id context,
        std::span<const std::byte> params) {

        const script_class_info* found = m_plugin.find_class(script_name);
        if (!found)
            return std::unexpected(script_error{ script_error::code::class_not_registered, std::format("no script class [{}]", script_name) });
        if (found->kind != script_kind::editor_action && found->kind != script_kind::world_generator)
            return std::unexpected(script_error{ script_error::code::wrong_kind, std::format("[{}] is not an action or world generator", script_name) });

        const script_class_info cls = *found;
        i_script* instance = cls.create();
        if (!params.empty())
            (void)apply_fields(cls, *instance, params, field_set::editor);

        slot temp{ .id = INVALID_SCRIPT_INSTANCE, .entity = context, .script_name = std::string(cls.name), .instance_name = {},
            .cls = cls, .instance = instance };
        std::expected<void, script_error> result{};
        slot* previous = m_current;
        m_current = &temp;
        try {
            auto ctx = make_context(temp, context_flag::is_start);
            instance->on_start(ctx);
            auto stop_ctx = make_context(temp, context_flag::is_stop);
            instance->on_stop(stop_ctx);
        } catch (const std::exception& e) {
            LOG(error, "Script action [{}] threw: {}", script_name, e.what())
            result = std::unexpected(script_error{ script_error::code::script_threw, e.what() });
        } catch (...) {
            result = std::unexpected(script_error{ script_error::code::script_threw, "unknown exception" });
        }
        m_current = previous;
        cls.destroy(instance);
        return result;
    }


    std::expected<void, script_error> script_host::call(std::string_view script_name, std::string_view method_name,
        world::entity_id entity, std::string_view instance_name, std::span<const std::byte> /*params*/) {

        for (const auto& s : m_slots) {
            if (s->script_name != script_name || s->entity != entity || (!instance_name.empty() && s->instance_name != instance_name))
                continue;

            for (const script_method_info& m : s->cls.callable_methods) {
                if (method_name != m.name)
                    continue;
                slot* previous = m_current;
                m_current = s.get();
                std::expected<void, script_error> result{};
                try {
                    auto ctx = make_context(*s, 0);
                    m.invoke(s->instance, ctx, {});
                } catch (const std::exception& e) {
                    LOG(error, "Script [{}].{} threw: {}", script_name, method_name, e.what())
                    result = std::unexpected(script_error{ script_error::code::script_threw, e.what() });
                }
                m_current = previous;
                return result;
            }
            return std::unexpected(script_error{ script_error::code::method_not_found,
                std::format("[{}] has no editor_callable method [{}]", script_name, method_name) });
        }
        return std::unexpected(script_error{ script_error::code::invalid_entity,
            std::format("no live instance of [{}] ({}) on that entity", script_name, instance_name) });
    }


    void script_host::run_world_generators() {

        if (!m_running || !m_env.world)
            return;

        std::vector<std::string> names{};
        for (const script_class_info& c : m_plugin.registered_classes())
            if (c.kind == script_kind::world_generator)
                names.emplace_back(c.name);
        for (const std::string& name : names)
            if (auto r = run_action(name, world::INVALID_ENTITY, {}); !r)
                LOG(warn, "World generator [{}]: {}", name, r.error().message)
    }

    // per frame -------------------------------------------------------------------------------------------------------

    void script_host::update(const f32 delta_time) {

        if (!m_running)
            return;

        m_total_time += delta_time;
        attach_pending_globals();
        reconcile();

        // Index loop: callbacks may attach globals (m_slots grows); slots are only removed below / in the next update
        const size_t count = m_slots.size();
        for (size_t i = 0; i < count && i < m_slots.size(); ++i) {

            slot& s = *m_slots[i];
            if (s.sleeping && s.sleep_timer > 0.0f) {
                s.sleep_timer -= delta_time;
                if (s.sleep_timer <= 0.0f)
                    wake(s.id);
            }
            if (!s.enabled || s.sleeping || s.stopped || !s.instance)
                continue;
            if (s.entity.is_valid() && (!m_env.world || !m_env.world->alive(s.entity)))
                continue;                                   // torn down by the next reconciliation

            context_flags flags = context_flag::is_update;
            if (s.woke) {
                flags |= context_flag::woke_from_sleep;
                s.woke = false;
            }

            m_current = &s;
            const auto start = std::chrono::steady_clock::now();
            try {
                auto ctx = make_context(s, flags, delta_time);
                s.instance->on_update(ctx, delta_time);
            } catch (const std::exception& e) {
                LOG(error, "Script [{}] ({}) threw in on_update, disabled: {}", s.script_name, s.instance_name, e.what())
                s.enabled = false;
            } catch (...) {
                LOG(error, "Script [{}] ({}) threw in on_update, disabled", s.script_name, s.instance_name)
                s.enabled = false;
            }
            m_current = nullptr;
            s.last_update_ns = elapsed_ns(start);
            s.total_update_ns += s.last_update_ns;
            ++s.update_count;
        }

        process_destroy_requests();
    }


    std::span<const script_instance_info> script_host::instances() const {

        m_info.clear();
        m_info.reserve(m_slots.size());
        for (const auto& s : m_slots)
            m_info.push_back(script_instance_info{
                .id = s->id,
                .entity = s->entity,
                .script_name = s->script_name,
                .instance_name = s->instance_name,
                .enabled = s->enabled,
                .sleeping = s->sleeping,
                .update_count = s->update_count,
                .total_update_ns = s->total_update_ns,
                .last_update_ns = s->last_update_ns,
            });
        return m_info;
    }


    const script_instance_info* script_host::find_instance(world::entity_id entity, std::string_view instance_name) const {

        (void)instances();
        for (const script_instance_info& info : m_info)
            if (info.entity == entity && info.instance_name == instance_name)
                return &info;
        return nullptr;
    }

    // siblings --------------------------------------------------------------------------------------------------------

    void* script_host::find_sibling_erased(std::string_view class_name, world::entity_id entity, std::string_view instance_name,
        std::string_view registrant) {

        if (!entity.is_valid())
            return nullptr;
        for (const auto& s : m_slots) {
            if (s->entity != entity || s->cls.type_key != class_name || (!instance_name.empty() && s->instance_name != instance_name))
                continue;
            if (s->cls.registrant != registrant) {
                LOG(debug, "sibling<{}>: belongs to module [{}], the caller to [{}]", class_name, s->cls.registrant, registrant)
                return nullptr;
            }
            return s->cls.object_of ? s->cls.object_of(s->instance) : s->instance;
        }
        return nullptr;
    }


    void script_host::sleep_until_event_erased(u32 /*event_type_hash*/) {

        // Events do not wake scripts yet (scripting.md §0.6 step 5): the script sleeps until woken
        if (m_current)
            control(m_current->id, control_op::sleep, 0.0f);
    }


    void script_host::wake_sibling_erased(std::string_view class_name, world::entity_id entity, std::string_view instance_name) {

        for (const auto& s : m_slots)
            if (s->entity == entity && s->cls.type_key == class_name && (instance_name.empty() || s->instance_name == instance_name)) {
                wake(s->id);
                return;
            }
    }

    // hot reload ------------------------------------------------------------------------------------------------------

    void script_host::prepare_hot_reload(std::string_view registrant) {

        for (size_t i = m_slots.size(); i-- > 0;) {
            slot& s = *m_slots[i];
            if (!s.stopped && (registrant.empty() || s.cls.registrant == registrant))
                stop_slot(s, context_flag::was_reloaded);
        }
    }


    std::expected<std::vector<std::byte>, script_error> script_host::snapshot_state(std::string_view registrant) const {

        std::vector<pending_state> records{};
        for (const auto& s : m_slots)
            if (registrant.empty() || s->cls.registrant == registrant)
                records.push_back(record_of(*s));
        return write_records(records);
    }


    std::expected<std::vector<std::byte>, script_error> script_host::snapshot_and_release(std::string_view registrant) {

        // on_stop first (was_reloaded): a script may still settle its persist fields there; then the snapshot
        prepare_hot_reload(registrant);
        std::vector<pending_state> records{};
        for (const auto& s : m_slots)
            if (registrant.empty() || s->cls.registrant == registrant)
                records.push_back(record_of(*s));

        for (size_t i = m_slots.size(); i-- > 0;)
            if (i < m_slots.size() && (registrant.empty() || m_slots[i]->cls.registrant == registrant))
                destroy_slot(m_slots[i]->id, context_flag::was_reloaded);

        std::vector<std::byte> blob = write_records(records);
        for (pending_state& r : records) {
            r.flag = context_flag::was_reloaded;
            m_pending.push_back(std::move(r));
        }
        return blob;
    }


    u32 script_host::restore_state(std::span<const std::byte> snapshot) {

        reflect::binary_reader in(snapshot);
        u32 version = 0, count = 0;
        if (!in.read(version) || version != SNAPSHOT_VERSION || !in.read(count))
            return 0;

        u32 restored = 0;
        for (u32 i = 0; i < count && in.ok(); ++i) {
            pending_state r{};
            u8 global = 0, enabled = 1, sleeping = 0;
            std::span<const std::byte> persist{};
            if (!in.read(global) || !in.read(r.entity.index) || !in.read(r.entity.generation) || !in.read(r.script_name)
                || !in.read(r.instance_name) || !in.read(enabled) || !in.read(sleeping) || !in.read(r.sleep_timer) || !in.read_blob(persist))
                break;
            r.global = global != 0;
            r.enabled = enabled != 0;
            r.sleeping = sleeping != 0;
            r.persist.assign(persist.begin(), persist.end());
            r.flag = context_flag::from_save;
            m_pending.push_back(std::move(r));
            ++restored;
        }
        return restored;
    }

    // CLASS PRIVATE ===================================================================================================

    script_context script_host::make_context(const slot& s, const context_flags flags, const f32 dt) {

        return script_context_access::make({
            .host = this,
            .env = &m_env,
            .instance = s.id,
            .entity = s.entity,
            .flags = flags,
            .delta_time = dt,
            .total_time = m_total_time,
            .registrant = s.cls.registrant,
        });
    }


    void script_host::reconcile(std::optional<world::entity_id> only) {

        if (!m_running)
            return;

        // Desired slots from the bindings ----------------------------------------------------------------------------
        struct wanted {
            world::entity_id                            entity;
            const script_binding*                       binding;
        };
        std::vector<wanted> desired{};

        world::component::i_storage* storage = m_env.world
            ? m_env.world->storage_for(world::component_registry::hash_of(BINDING_COMPONENT_NAME)) : nullptr;
        if (storage) {
            const std::vector<world::entity_id> entities = only ? std::vector<world::entity_id>{ *only } : storage->entities();
            for (const world::entity_id entity : entities) {
                if (!m_env.world->alive(entity))
                    continue;
                const auto* comp = static_cast<const script_binding_component*>(storage->get(entity));
                if (!comp)
                    continue;
                for (const script_binding& b : comp->bindings) {
                    if (!b.enabled || b.script_name.empty())
                        continue;
                    const std::string_view name = b.instance_name.empty() ? std::string_view(b.script_name) : std::string_view(b.instance_name);
                    const bool duplicate = std::any_of(desired.begin(), desired.end(), [&](const wanted& w) {
                        return w.entity == entity
                            && (w.binding->instance_name.empty() ? std::string_view(w.binding->script_name) : std::string_view(w.binding->instance_name)) == name;
                    });
                    if (duplicate) {
                        if (m_reported.insert(failure_key(entity, name, "duplicate", 0)).second)
                            LOG(warn, "Entity {}:{} has two script bindings named [{}]: the second is ignored", entity.index, entity.generation, name)
                        continue;
                    }
                    desired.push_back({ entity, &b });
                }
            }
        }

        // Tear down entity slots that are no longer wanted (or whose class changed) -----------------------------------
        for (size_t i = m_slots.size(); i-- > 0;) {
            if (i >= m_slots.size())
                continue;
            slot& s = *m_slots[i];
            if (!s.entity.is_valid() || (only && s.entity != *only))
                continue;
            const bool keep = std::any_of(desired.begin(), desired.end(), [&](const wanted& w) {
                const std::string_view name = w.binding->instance_name.empty() ? std::string_view(w.binding->script_name) : std::string_view(w.binding->instance_name);
                return w.entity == s.entity && name == s.instance_name && w.binding->script_name == s.script_name;
            });
            if (!keep || (m_env.world && !m_env.world->alive(s.entity)))
                destroy_slot(s.id);
        }

        // Create the missing ones, update the settings of the others --------------------------------------------------
        for (const wanted& w : desired) {

            const script_binding& b = *w.binding;
            const std::string_view name = b.instance_name.empty() ? std::string_view(b.script_name) : std::string_view(b.instance_name);
            const u64 settings_hash = hash_bytes(b.editor_settings);

            slot* existing = nullptr;
            for (const auto& s : m_slots)
                if (s->entity == w.entity && s->instance_name == name) {
                    existing = s.get();
                    break;
                }

            if (existing) {
                if (existing->settings_hash != settings_hash && existing->instance) {
                    (void)apply_fields(existing->cls, *existing->instance, b.editor_settings, field_set::editor);
                    existing->settings_hash = settings_hash;
                }
                continue;
            }

            const script_class_info* cls = m_plugin.find_class(b.script_name);
            if (!cls) {
                if (m_reported.insert(b.script_name).second)
                    LOG(warn, "Script class [{}] is not registered (module not loaded or not built?): bindings to it wait", b.script_name)
                continue;
            }
            if (cls->kind != script_kind::entity_script) {
                if (m_reported.insert(b.script_name).second)
                    LOG(warn, "Script class [{}] is not an entity script: it can not be bound to an entity", b.script_name)
                continue;
            }
            if (m_failed.contains(failure_key(w.entity, name, b.script_name, settings_hash)))
                continue;

            if (!create_slot(w.entity, *cls, name, b.editor_settings, settings_hash))
                m_failed.insert(failure_key(w.entity, name, b.script_name, settings_hash));
        }
    }


    script_host::slot* script_host::create_slot(world::entity_id entity, const script_class_info& cls, std::string_view instance_name,
        std::span<const std::byte> editor_settings, const u64 settings_hash) {

        auto s = std::make_unique<slot>();
        s->id = m_next_id++;
        s->entity = entity;
        s->script_name = std::string(cls.name);
        s->instance_name = std::string(instance_name);
        s->cls = cls;
        s->settings_hash = settings_hash;
        s->instance = cls.create();
        if (!editor_settings.empty())
            (void)apply_fields(cls, *s->instance, editor_settings, field_set::editor);

        context_flags flags = context_flag::is_start;
        if (auto pending = take_pending(!entity.is_valid(), entity, cls.name, instance_name)) {
            (void)apply_fields(cls, *s->instance, pending->persist, field_set::persist);
            s->enabled = pending->enabled;
            s->sleeping = pending->sleeping;
            s->sleep_timer = pending->sleep_timer;
            flags |= pending->flag;
        }

        slot& ref = *s;
        m_slots.push_back(std::move(s));

        m_current = &ref;
        bool ok = true;
        try {
            auto ctx = make_context(ref, flags);
            ref.instance->on_start(ctx);
        } catch (const std::exception& e) {
            LOG(error, "Script [{}] ({}) threw in on_start, not started: {}", ref.script_name, ref.instance_name, e.what())
            ok = false;
        } catch (...) {
            LOG(error, "Script [{}] ({}) threw in on_start, not started", ref.script_name, ref.instance_name)
            ok = false;
        }
        m_current = nullptr;

        if (!ok) {
            ref.stopped = true;                          // no on_stop for an instance that did not start
            destroy_slot(ref.id);
            return nullptr;
        }
        return &ref;
    }


    void script_host::stop_slot(slot& s, const context_flags extra) {

        if (s.stopped || !s.instance)
            return;
        s.stopped = true;
        slot* previous = m_current;
        m_current = &s;
        try {
            auto ctx = make_context(s, context_flag::is_stop | extra);
            s.instance->on_stop(ctx);
        } catch (const std::exception& e) {
            LOG(error, "Script [{}] ({}) threw in on_stop: {}", s.script_name, s.instance_name, e.what())
        } catch (...) {
            LOG(error, "Script [{}] ({}) threw in on_stop", s.script_name, s.instance_name)
        }
        m_current = previous;
    }


    void script_host::destroy_slot(script_instance_id id, const context_flags extra) {

        const auto it = std::find_if(m_slots.begin(), m_slots.end(), [&](const auto& s) { return s->id == id; });
        if (it == m_slots.end())
            return;

        slot& s = **it;
        stop_slot(s, extra);
        if (s.instance && s.cls.destroy)
            s.cls.destroy(s.instance);
        s.instance = nullptr;
        if (m_current == &s)
            m_current = nullptr;

        // Find again: on_stop may have attached or detached others
        const auto again = std::find_if(m_slots.begin(), m_slots.end(), [&](const auto& p) { return p->id == id; });
        if (again != m_slots.end())
            m_slots.erase(again);
    }


    void script_host::stop_all() {

        while (!m_slots.empty())
            destroy_slot(m_slots.back()->id);           // reverse attach order
    }


    void script_host::attach_pending_globals() {

        // Globals released by a module reload come back once their class is registered again
        for (size_t i = 0; i < m_pending.size();) {
            const pending_state& p = m_pending[i];
            if (!p.global || !m_plugin.find_class(p.script_name)) {
                ++i;
                continue;
            }
            const std::string script = p.script_name, instance = p.instance_name;
            if (auto r = attach_global(script, instance); !r)
                LOG(warn, "Global [{}] ({}) not restored: {}", script, instance, r.error().message)
            // attach_global consumed the record if it succeeded; drop it otherwise
            if (i < m_pending.size() && m_pending[i].global && m_pending[i].script_name == script && m_pending[i].instance_name == instance)
                m_pending.erase(m_pending.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }


    void script_host::process_destroy_requests() {

        auto despawn = std::move(m_despawn_requests);
        m_despawn_requests.clear();
        for (const world::entity_id entity : despawn) {
            for (size_t i = m_slots.size(); i-- > 0;)
                if (i < m_slots.size() && m_slots[i]->entity == entity)
                    destroy_slot(m_slots[i]->id);
            if (m_env.world && m_env.world->alive(entity))
                m_env.world->despawn(entity);
        }

        auto detach = std::move(m_detach_requests);
        m_detach_requests.clear();
        for (const script_instance_id id : detach)
            detach_global(id);
    }


    void script_host::destroy_resources(std::optional<std::string_view> registrant) {

        for (auto it = m_resources.begin(); it != m_resources.end();) {
            if (registrant && it->second.registrant != *registrant) {
                ++it;
                continue;
            }
            if (it->second.destroy)
                it->second.destroy(it->second.memory);
            ::operator delete(it->second.memory, std::align_val_t(it->second.alignment));
            it = m_resources.erase(it);
        }
    }


    script_host::slot* script_host::find(script_instance_id id) {

        for (const auto& s : m_slots)
            if (s->id == id)
                return s.get();
        return nullptr;
    }


    const script_host::slot* script_host::find(script_instance_id id) const {

        for (const auto& s : m_slots)
            if (s->id == id)
                return s.get();
        return nullptr;
    }


    std::optional<script_host::pending_state> script_host::take_pending(const bool global, world::entity_id entity,
        std::string_view script_name, std::string_view instance_name) {

        for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
            if (it->global == global && (global || it->entity == entity) && it->script_name == script_name
                && it->instance_name == instance_name) {
                pending_state out = std::move(*it);
                m_pending.erase(it);
                return out;
            }
        return std::nullopt;
    }


    script_host::pending_state script_host::record_of(const slot& s) const {

        return pending_state{
            .entity = s.entity,
            .global = !s.entity.is_valid(),
            .script_name = s.script_name,
            .instance_name = s.instance_name,
            .enabled = s.enabled,
            .sleeping = s.sleeping,
            .sleep_timer = s.sleep_timer,
            .persist = s.instance ? capture_fields(s.cls, *s.instance, field_set::persist) : std::vector<std::byte>{},
            .flag = 0,
        };
    }


    std::vector<std::byte> script_host::write_records(std::span<const pending_state> records) const {

        reflect::binary_writer out{};
        out.write(SNAPSHOT_VERSION);
        out.write(static_cast<u32>(records.size()));
        for (const pending_state& r : records) {
            out.write(static_cast<u8>(r.global ? 1 : 0));
            out.write(r.entity.index);
            out.write(r.entity.generation);
            out.write(r.script_name);
            out.write(r.instance_name);
            out.write(static_cast<u8>(r.enabled ? 1 : 0));
            out.write(static_cast<u8>(r.sleeping ? 1 : 0));
            out.write(r.sleep_timer);
            out.write_blob(r.persist);
        }
        return std::move(out).take();
    }

}
