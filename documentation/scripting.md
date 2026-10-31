# GLT Scripting System — Design Document

**Status:** Proposed
**Scope:** Interface design, script model, bindings component, lifecycle, communication, hot reload, build model
**Out of scope:** Implementation details, editor UI specifics, save-file system, graphics backend

---

## 1. Purpose

The scripting system gives game code a structured place to run inside a live `game_session` — reacting to physics, mutating entities, exchanging messages with other scripts, spawning threads, and surviving hot reload without losing state.

Organized around five concerns:

1. **A script is** a C++ class inheriting from `i_script`, with member fields for state and virtual methods for behavior.
2. **Scripts are associated with entities** through a per-entity ECS component (`script_binding_component`) that lists the scripts on that entity and holds their per-instance editor settings.
3. **State lives** in the script object's member fields, classified by annotation: `[[=persist]]` (serialized across reloads and saves), `[[=editor]]` (authored per-instance, stored in the component), `[[=transient]]` (re-initialized from source defaults), unmarked (runtime-only, never serialized).
4. **Scripts talk to each other** through a session-scoped event bus, with sibling access and blackboards for tighter coupling.
5. **An editor script is distinguished from a play-time script** by an annotation on the class, not a separate interface.

---

## 2. Core principles

### 2.1 A script is a normal C++ class

A script is a class inheriting from `i_script` with member fields, member functions, constructors, destructors, threads, statics — everything a normal C++ class can have. The engine imposes no restriction on what the class contains.

The only rule enforced is on **classification**: every non-static data member must be serializable (as runtime state, as editor settings, or unmarked runtime-only) or explicitly marked `[[=transient]]`. Non-serializable members must be transient. This is a rule about what happens to a field across reloads, not about what the class may contain.

### 2.2 State lives in the script instance

No external state blob. Scripts read and write `this->field` like any other C++ class. The hot reload machinery reaches into the instance via reflection, serializes the fields it cares about, and restores them into a freshly constructed instance.

### 2.3 Bindings are authored data, state is session data

Which scripts belong to which entity — and the per-instance settings those scripts expose — is **authored data**, stored in the world asset in an ECS component. What those scripts have *done* during play is **session data**, owned by a save-file system separate from scripting. The scripting system exposes `snapshot_state()` / `restore_state()` as its persistence API. The two are never conflated.

### 2.4 One script library, not N

The engine loads **one game plugin** containing every script. Hot reload rebuilds and reloads that one library, snapshotting and restoring runtime state for every script in every session. Per-script `.so` files are explicitly rejected — see §16.

### 2.5 Sessions are isolated

Each `game_session` gets its own `i_script_host`, session event bus, entity blackboards, and session resources. Editor preview and play-test sessions never share runtime state.

### 2.6 Identifiers are names, not types

Scripts are referred to by string name. The string survives hot reload; the C++ type does not.

### 2.7 Registration is reflection-driven

Scripts are discovered by walking a namespace with `std::meta`. Adding a new script requires zero boilerplate in the game plugin.

---

## 3. Architecture

```
plugin_manager
  interface::scripting → i_script_plugin (singleton)
                             │ create_host()
                             ▼
game_layer
  slot 0: game_session (primary)
  slot 1: game_session (play-test)
         │ each owns one
         ▼
  game_session
    m_physics   : i_physics
    m_world     : i_world ──► entities
    m_scripting : i_script_host
    m_renderers : vector<slot>
    m_controller: ref<controller>

entity ──► script_binding_component
              ├─ binding: "controller" / "controller"
              ├─ binding: "muzzle_flash" / ""
              └─ binding: "health" / "player_health"
                     │
                     ▼ host reconciles per frame
slot { instance: controller*,  class_name: "controller" }
slot { instance: muzzle_flash*, class_name: "muzzle_flash" }
slot { instance: health*,       class_name: "health" }
```

| System | Relationship |
|---|---|
| `game_session` | Owns one `i_script_host`, declared after `m_world` and `m_physics`. |
| `i_world` | Owns entity `script_binding_component`s. Provides component access to the host. |
| `i_physics` | Passed via `ctx.physics()`. The host drains contact events and routes them to `on_contact`. |
| `plugin_manager` | Hosts `i_script_plugin` under `interface::scripting`. |
| `event_bus` | Each host owns a session-scoped bus. |
| `reflect` | Drives field enumeration, serialization, annotations, and registration. |
| Save-file system | Future system. Calls `host->snapshot_state()` / `restore_state()`. |

---

## 4. Script model

A script is a class inheriting from `i_script`. State is member fields; behavior is virtual overrides; fields are classified by annotation.

```cpp
class rotate : public GLT::script::i_script {
public:
    f32                                         target_speed = 45.0f;
    glm::vec3                                   rotation_axis = { 0, 1, 0 };

    [[=GLT::script::annotations::editor{"Rotation|Speed"}]]
    [[=GLT::reflect::annotations::range{0.1, 360.0}]]
    f32                                         speed_deg_per_sec = 45.0f;

    [[=GLT::script::annotations::persist]]
    f32                                         accumulated_angle = 0.0f;

    [[=GLT::script::annotations::transient]]
    GLT::event_bus::subscription_guard          guard;

    void on_start(GLT::script::script_context& ctx) override {
        guard = ctx.events().subscribe_scoped<damage_event>(
            [this, e = ctx.entity()](damage_event& ev) { if (ev.victim == e) on_hit(); });
    }

    void on_update(GLT::script::script_context& ctx, f32 dt) override {
        const f32 delta = speed_deg_per_sec * dt;
        accumulated_angle += delta;
        ctx.world()->rotate_local(ctx.entity(), rotation_axis * delta);
    }

    void on_contact(/* .. */) { /* ... */ }
};
```

- Every non-static data member is a field of the class, accessed via `this->`.
- `[[=editor{...}]]` fields are shown in the editor and stored per-instance in the entity's `script_binding_component`.
- `[[=persist]]` fields survive hot reload; they are the natural target of a save-file system.
- `[[=transient]]` fields are re-initialized from source defaults every time.
- **Unmarked serializable fields are runtime-only and are never serialized.**
- No `state` typedef, no `ctx.state<S>()`, no external blob.
- The constructor runs every time the instance is created; it has no access to `script_context`.
- `on_start` runs every instance creation and can distinguish restore via `was_restored`.

---

## 5. Interfaces

Four interfaces, one annotation namespace, one component type, one registration entry point.

### 5.1 `i_script` — behavior contract

```cpp
namespace GLT::script {
    class script_context;

    class i_script {
    public:
        virtual ~i_script() = default;

        // Instance created: fresh attach, hot reload restore, save restore, world load.
        // Fires after editor settings and persisted fields are restored.
        // `get_flags()` carries `was_reloaded` or `from_save` during a restore.
        // `is_sleeping()` reports the restored sleep state (false for fresh attach).
        virtual void on_start(script_context& ctx) {}

        // Every frame while not paused, not sleeping, and enabled.
        virtual void on_update(script_context& ctx, f32 delta_time) {}

        // After physics step, before on_update. Not called while sleeping or disabled.
        // `other == INVALID_ENTITY` when the other side has no entity (static collider).
        virtual void on_contact(script_context& ctx, world::entity_id other,
            const physics::contact_event& contact) {}

        // Before destroy: detach, hot reload unload, world unload, session shutdown.
        // `was_reloaded` set only when teardown is part of a hot reload.
        virtual void on_stop(script_context& ctx) {}
    };
}
```

**Contract notes:**
- Every callback receives `script_context&`. Scripts do not cache the context across frames — it is constructed fresh per call.
- `on_start` fires on every instance creation, including every hot reload. Scripts that need once-per-session setup gate on `was_restored`.
- `on_contact` is a virtual, not an event. Contacts are high-frequency and entity-scoped; direct dispatch is the right tool. A script that wants to broadcast posts an event from within `on_contact`.
- `enabled` gates both `on_update` and `on_contact`. A disabled script still receives events it subscribed to via `subscribe_scoped`.
- No `on_editor_action` (editor actions are a *kind* of script, §13), no `on_serialize`/`on_deserialize` (persistence is reflection-driven).
- A script that owns a thread **must** join it in `on_stop`; alternatively, move the thread into a session resource.
- Exceptions thrown from callbacks are caught by the host; see §8.6.

### 5.2 `context_flags` and `script_context`

```cpp
using context_flags = u32;

namespace context_flag {
    inline constexpr context_flags none             = 0u;
    inline constexpr context_flags was_reloaded     = BIT(0);  // on_start (hot reload) and on_stop (pre-snapshot)
    inline constexpr context_flags from_save        = BIT(1);  // on_start (save restore)
    inline constexpr context_flags woke_from_sleep  = BIT(2);  // first on_update after waking
    inline constexpr context_flags is_start         = BIT(3);
    inline constexpr context_flags is_update        = BIT(4);
    inline constexpr context_flags is_contact       = BIT(5);
    inline constexpr context_flags is_stop          = BIT(6);
    inline constexpr context_flags plugin_reserved_base = BIT(16);
}

inline bool has_flag(context_flags set, context_flags f) noexcept { return (set & f) != 0u; }
inline bool was_restored(context_flags set) noexcept {
    return has_flag(set, context_flag::was_reloaded | context_flag::from_save);
}

class script_context {
public:
    // identity
    [[nodiscard]] world::entity_id entity() const noexcept;   // INVALID for globals
    [[nodiscard]] world::i_world* world() noexcept;
    [[nodiscard]] physics::i_physics* physics() noexcept;
    [[nodiscard]] asset::i_asset_registry_plugin* assets() noexcept;
    [[nodiscard]] i_script_host* host() noexcept;

    // flags
    [[nodiscard]] context_flags get_flags() const noexcept;

    // time
    [[nodiscard]] f32 delta_time() const noexcept;
    [[nodiscard]] f32 total_time() const noexcept;

    // session resources (survive plugin reload)
    template <typename T>
    [[nodiscard]] T& session_resource(std::string_view key);
    template <typename T, typename Factory>
    [[nodiscard]] T& session_resource(std::string_view key, Factory&& factory);

    // communication
    [[nodiscard]] event_bus_accessor events() noexcept;
    [[nodiscard]] entity_blackboard& blackboard() noexcept;
    template <typename S>
    [[nodiscard]] S* sibling(std::string_view instance_name = {}) noexcept;

    // sleep / wake
    [[nodiscard]] bool is_sleeping() const noexcept;
    void sleep() noexcept;
    void sleep_for(f32 seconds) noexcept;
    void sleep_until_contact() noexcept;
    template <typename E> void sleep_until_event() noexcept;
    void wake_self() noexcept;
    template <typename S> void wake_sibling(std::string_view instance_name = {}) noexcept;

    // self-control
    void disable_self() noexcept;
    void destroy_self() noexcept;
};
```

**Flag lifecycle:**

| Callback | Flags set |
|---|---|
| `on_start` after `restore_state` (hot reload) | `is_start \| was_reloaded` |
| `on_start` after save restore | `is_start \| from_save` |
| `on_start` on fresh attach | `is_start` |
| `on_stop` before snapshot | `is_stop \| was_reloaded` |
| `on_stop` on natural detach | `is_stop` |
| `on_update` | `is_update` (+ `woke_from_sleep` on first frame after waking) |
| `on_contact` | `is_contact` |

Test with `has_flag(ctx.get_flags(), context_flag::was_reloaded)` or the `was_restored()` shortcut. **Extending the set:** a plugin claims a bit above `plugin_reserved_base` and documents it; engine code ignores it.

**Global scripts** (`ctx.entity() == INVALID_ENTITY`):
- `blackboard()` returns a null blackboard (reads return defaults, writes are no-ops). Use session resources for shared global state.
- `sibling<S>()` returns `nullptr`; `wake_sibling<S>()` is a no-op.
- `sleep_until_contact()` is a no-op.
- `destroy_self()` detaches the global instance.
- `disable_self()` works as for entity scripts.

**Sibling access across plugin boundaries.** `sibling<S>()` compares the calling slot's `registrant` against `S`'s `registrant` and returns `nullptr` (with a debug log) on mismatch. If `S` is a `global_script` and the caller is an `entity_script` (or vice versa), returns `nullptr`.

**Sleep/wake semantics** (§10): all sleep requests take effect only after the current callback returns. Multiple wake conditions may be active; the first to fire wakes the slot and cancels the others.

**Self-control semantics:**
- `disable_self()` sets `enabled = false`. Slot stays alive, receives no `on_update`/`on_contact`, can be re-enabled via `host->set_enabled(id, true)`.
- `destroy_self()`: for `entity_script`, requests destruction of the owning entity (deferred to end of host update, cascading to every script on the entity); for `global_script`, requests detachment, processed at end of host update.

### 5.3 `i_script_host` — per-session instance owner

Owned by `game_session` as `unique_ref<i_script_host>`.

```cpp
using script_instance_id = u32;
inline constexpr script_instance_id INVALID_SCRIPT_INSTANCE = 0;

struct script_instance_info {
    script_instance_id  id;
    world::entity_id    entity;          // INVALID for globals
    std::string_view    script_name;
    std::string_view    instance_name;
    bool                enabled;
    bool                sleeping;
    u64                 update_count;
    u64                 total_update_ns;
    u64                 last_update_ns;
};

struct script_error {
    enum class code : u8 {
        none = 0, class_not_registered, no_host, invalid_entity, duplicate_name,
        method_not_found, not_callable, wrong_kind, bad_params,
        script_threw, snapshot_version_mismatch,
    } kind = code::none;
    std::string message;
};

class i_script_host {
public:
    virtual ~i_script_host() = default;

    // reconciliation
    virtual void refresh(world::entity_id entity) = 0;
    virtual void refresh_all() = 0;

    // global scripts
    [[nodiscard]] virtual std::expected<script_instance_id, script_error>
        attach_global(std::string_view script_name, std::string_view instance_name) = 0;
    virtual void detach_global(script_instance_id id) = 0;

    // runtime controls
    virtual void set_enabled(script_instance_id id, bool enabled) = 0;
    virtual void wake(script_instance_id id) = 0;

    // one-shot actions (editor_action / world_generator)
    [[nodiscard]] virtual std::expected<void, script_error> run_action(
        std::string_view script_name,
        world::entity_id context = world::INVALID_ENTITY,
        std::span<const std::byte> params = {}) = 0;

    // live instance methods ([[=editor_callable]])
    [[nodiscard]] virtual std::expected<void, script_error> call(
        std::string_view script_name, std::string_view method_name,
        world::entity_id entity, std::string_view instance_name = {},
        std::span<const std::byte> params = {}) = 0;

    // world generators
    virtual void run_world_generators() = 0;

    // per-frame
    virtual void update(f32 delta_time) = 0;

    // introspection (span valid until next refresh/restore/attach/update)
    [[nodiscard]] virtual std::span<const script_instance_info> instances() const = 0;

    // hot reload
    virtual void prepare_hot_reload(std::string_view registrant = {}) = 0;
    [[nodiscard]] virtual std::expected<std::vector<std::byte>, script_error>
        snapshot_state(std::string_view registrant = {}) const = 0;
    virtual u32 restore_state(std::span<const std::byte> snapshot) = 0;
};
```

The host owns live instances and their runtime state. Bindings live in the world via the component; the host is a **slave to the component** and stays in sync via `refresh`. Globals are the exception: the host owns them directly.

### 5.4 `i_script_plugin` — class registry

Singleton, resolved through `plugin_manager` under `interface::scripting`.

```cpp
namespace GLT::script {
    namespace manager {
        FORCE_INLINE_R i_script_plugin* get_ptr() noexcept;
        FORCE_INLINE_R ref<i_script_plugin> get_ref();
    }

    enum class script_kind : u8 {
        entity_script, global_script, world_generator, editor_action,
    };

    struct script_class_info {
        std::string_view  name;
        script_kind       kind;
        std::string_view  editor_label;      // for editor_action; empty otherwise
        std::string_view  registrant;
        std::span<const reflect::member_descriptor> fields;
        std::span<const reflect::member_descriptor> callable_methods;
    };

    class i_script_plugin : public plugin_manager::i_plugin {
    public:
        template <typename T> requires std::derived_from<T, i_script>
        void register_script_class(std::string_view registrant = {});
        virtual void unregister_script_class(std::string_view name) = 0;

        template <auto Namespace>
        void register_scripts_in_namespace(std::string_view registrant = {});
        template <auto Namespace>
        void unregister_scripts_in_namespace();

        [[nodiscard]] virtual unique_ref<i_script_host> create_host() = 0;

        [[nodiscard]] virtual std::span<const script_class_info> registered_classes() const = 0;
    };
}
```

`register_script_class<T>()` performs at registration time:
1. **Reject abstract classes** (`std::is_abstract_v<T>`): log and skip.
2. Derive the script name from `reflect::type_name(^^T)` (or `[[=annotations::name{"..."}]]` override).
3. Read the kind annotation. Exactly one of `entity_script`, `global_script`, `world_generator`, `editor_action` must be present; zero is a registration error.
4. Walk `nonstatic_data_members_of(^^T)`:
   - `[[=transient]]` → no serialization check.
   - `[[=editor]]` → require serializable (per-instance settings blob).
   - `[[=persist]]` → require serializable (hot reload / save snapshots).
   - Neither → if serializable, runtime-only (never serialized); if not serializable, registration error naming the field.
5. Walk `members_of(^^T)` for functions annotated `[[=editor_callable]]`. Validate return type `void` and params `(script_context&)` or `(script_context&, const P&)`.
6. Store the construction lambda: `[](void* m) -> i_script* { return new (m) T(); }`.

A script that fails validation is **logged and skipped**, not aborted.

---

## 6. Annotations

All in `GLT::script::annotations`.

### 6.1 Kind annotations (exactly one per class)

```cpp
struct entity_script {};                        // per-entity, runs during play
struct global_script {};                        // one per session, no entity
struct world_generator {};                      // runs once on world load / editor button
struct editor_action { const char* label; };    // editor toolbar button
```

### 6.2 Field annotations

```cpp
struct persist {};                              // hot reload + save-file snapshot
struct editor { const char* category = ""; };   // per-instance component setting
struct transient {};                            // never serialized
```

The `editor` category string is pipe-separated (`"Weather|Ski|Coloring"`); empty means no category.

**Field classification:**

| Annotations | Component | Hot reload | Save file |
|---|---|---|---|
| `[[=editor]]` | yes | no | no |
| `[[=persist]]` | no | yes | yes (future) |
| `[[=editor]] [[=persist]]` | yes | yes | yes |
| (unmarked, serializable) | **no** | **no** | **no** |
| `[[=transient]]` | no | no | no |

`[[=editor]] [[=persist]]` is the pattern for "user picks an initial value, runtime can mutate it, both survive." On a fresh instance the component's value applies; on hot reload the persist snapshot applies.

**Restore order for `[[=editor]] [[=persist]]`:** during hot-reload construction, the host applies the component's editor settings first, then overlays the persist snapshot. **Persist always wins on reload.**

### 6.3 Class annotations

```cpp
struct name { const char* value; };             // stable identifier override
struct priority { i32 value; };                 // ordering within an entity (higher first)
```

`priority` applies only to `entity_script` bindings within one entity's list. Globals run in `attach_global` order; `priority` is ignored for globals.

### 6.4 Function annotations

```cpp
// Callable via `i_script_host::call()`. Must return void, take
// (script_context&) or (script_context&, const P&) with P serializable.
struct editor_callable { const char* label = ""; };
```

### 6.5 Category rendering

`[[=editor{"Weather|Ski|Coloring"}]]` renders as nested sections, a tree, or tabs. Per-field; no per-class default. A script with 112 editor fields can organize them into a handful of top-level categories.

---

## 7. State model

### 7.1 Member fields are state

```cpp
void on_update(script_context& ctx, f32 dt) override {
    health -= dt * damage_rate;
    ctx.world()->translate_local(ctx.entity(), velocity * dt);
}
```

No extra indirection, no pointer chasing, no cache invalidation concern.

### 7.2 Field classification

| Category | Trigger | Behavior |
|---|---|---|
| Editor setting | `[[=editor]]` | Shown in editor, stored in component, applied at instantiation |
| Runtime state | `[[=persist]]` | Serialized on hot reload and (future) save file |
| Transient | `[[=transient]]` | Re-initialized from source default on every construction |
| Runtime-only | (unmarked, serializable) | Never serialized |
| Registration error | (unmarked, non-serializable) | Class fails to register with a diagnostic |

Serializable types: arithmetic types, `bool`, enums, `std::string`, `std::filesystem::path`, `GLT::UUID`, `GLT::asset::handle`, `GLT::world::entity_id`, `glm::vec2/3/4`, `glm::quat`, `glm::mat3/4`, `std::vector/array/map/unordered_map` of serializable types, aggregate structs whose non-static data members are all serializable, any type with a registered `reflect::serialization_override`.

`std::string_view` is **not** serializable (would dangle across reload); use `std::string`. Non-serializable types (`std::thread`, `std::mutex`, `std::function`, `std::any`, raw pointers, `subscription_guard`, `unique_ref<T>`, `ref<T>`, OS/GPU handles) must be `[[=transient]]`.

### 7.3 Session resources

```cpp
auto& pool  = ctx.session_resource<thread_pool>("game.worker_pool");
auto& cache = ctx.session_resource<mesh_cache>("game.mesh_cache", []{ return mesh_cache{ /* ... */ }; });
```

- **Session-scoped:** created on first request, destroyed with the session.
- **Plugin-agnostic:** survives reload of any plugin.
- **Name-keyed, type-erased:** requesting the same key with a different `T` is a fatal error (logged with both type names before abort).
- **Lazy-constructed:** factory runs only on first request.
- **Creation is main-thread-only.** No synchronization on access; `T` must provide its own.

### 7.4 Static members

- `static constexpr` / `static constinit` — recompiled, correct on every load.
- `static` non-const — **reset to its initialized value on reload**.

For state that must survive reload, use session resources or a `[[=persist]]` member field.

### 7.5 Threads

| Intent | Approach |
|---|---|
| Short-lived worker tied to the script | `[[=transient]] std::thread`, spawn in `on_start`, `join()` in `on_stop` |
| Long-lived worker that must survive reload | Session resource holding the thread; script submits work, drains results in `on_update` |
| One-shot asynchronous task | Session-scoped thread pool (session resource) |

`on_stop` **must** join any thread the script owns. A joinable `std::thread` reaching the destructor calls `std::terminate`.

**Event bus is main-thread-only.** Worker threads that produce results must marshal them back — idiomatic pattern: a lock-protected queue in a session resource, drained by the owning script in `on_update`:

```cpp
auto& q = ctx.session_resource<result_queue>("game.results");
q.drain([&](const worker_result& r) {
    ctx.events().post<result_ready_event>({ .payload = r });
});
```

### 7.6 Data placement

| Data | Home |
|---|---|
| Editor setting per instance | `[[=editor]]` field, component |
| Runtime state surviving hot reload / save | `[[=persist]]` field |
| Re-derived state | Unmarked or `[[=transient]]` field |
| Per-instance resources (threads, guards, handles) | `[[=transient]]` field, torn down in `on_stop` |
| Cross-instance shared state within a session | Session resource |
| Process-global shared state | Plugin singleton |
| Per-entity shared state between scripts | Entity blackboard |

### 7.7 Persistence boundaries

| Kind | Owner | Contains | Trigger |
|---|---|---|---|
| Authoring | World asset | Bindings + per-instance editor settings | World save |
| Runtime | Save-file system (future) | `[[=persist]]` field values | Game save |
| Hot-reload | In-memory snapshot | `[[=persist]]` values, enabled/sleeping flags, entity blackboards | Plugin reload |

The world asset **never** contains runtime state. The save file **never** contains bindings. Hot reload **never** touches disk.

---

## 8. Lifecycle

### 8.1 Update loop

```cpp
void game_session::update(f32 dt) {
    if (m_paused) return;
    if (m_controller) m_controller->update(dt);
    if (m_physics)    m_physics->step(dt);
    if (m_scripting)  m_scripting->update(dt);   // reconcile, contacts, update
    if (m_world)      m_world->update(dt);
    // render gather + submit
}
```

Scripts run **after physics, before world update**. Scripts that need to influence physics on the next step set the value on frame N; physics applies it on frame N+1.

### 8.2 `on_start` semantics

Fires every instance creation: fresh attach, hot reload restore, save-file restore, world load, `attach_global`. Anything not stored in a persisted or editor field must be (re)established here. Scripts that capture an environmental baseline stable across restores test `was_restored`.

### 8.3 Update ordering within the host

1. **Reconcile bindings** (§14.4).
2. **Drain physics contacts** from `physics->drain_contact_events()`. For each contact, resolve `(body_a, body_b) → (entity_a, entity_b)` and for every slot on both entities:
   - If awake and enabled → `on_contact`.
   - If sleeping with a pending `sleep_until_contact()` → wake (cancelling other conditions), `on_contact` for this contact.
   - Entityless side receives no callback; the other side gets `other == INVALID_ENTITY`.
3. **Process timer and event wake conditions.**
4. **Process deferred destructions and global detaches.**
5. **Check entity liveness.** Slots on dead entities are torn down.
6. **Update every non-sleeping, enabled slot.** Ordering is defined within an entity (§14.5); *across* entities, slots update in the world's entity iteration order (deterministic for a given world state, otherwise unspecified).

Contacts fire before `on_update`. Both sides of a contact are called symmetrically.

### 8.4 Detach

1. Call `on_stop` (joins threads, releases external resources).
2. Destroy the instance (destructor runs, `subscription_guard` members torn down).
3. Free the slot.

Detach order for multiple scripts on the same entity is **reverse attach order**. Global scripts detach in reverse `attach_global` order. During a hot reload, `on_stop` is called by `prepare_hot_reload` (before the snapshot); the detach path skips the `on_stop` call for slots already marked stopped.

### 8.5 Thread ownership and `on_stop`

See §7.5. A script that owns a `std::thread` **must** join it in `on_stop`; alternatively, move the thread into a session resource.

### 8.6 Exception policy

| Callback | On exception |
|---|---|
| `on_start` | Log with script/instance/entity. **Destroy the instance** (slot removed; binding remains and re-instantiates on next reconcile). |
| `on_update` | Log. **Disable the slot** (`enabled = false`). Binding remains; user can re-enable. |
| `on_contact` | Log. **Disable the slot.** |
| `on_stop` | Log. Continue teardown. |
| Event subscriber | Log. The subscription is torn down. Script continues; it can re-subscribe. |

`snapshot_state` catches serializer exceptions and skips the field; the returned blob contains a partial record for that slot. `restore_state` catches deserializer exceptions; the field keeps its source default and a warning is logged. `run_action` and `call` return `script_error{code::script_threw, message}`.

---

## 9. Hot reload

### 9.1 Overview

Hot reload is **not** a plugin-manager phase transition. Phases sequence the initial boot; a reload of an already-loaded plugin is a separate operation driven by a file watcher and a dedicated coordinator. The plugin manager gains unload/load by name; the scripting system owns the snapshot/swap/restore sequence.

```
[File watcher notices game plugin .so rebuilt]
    │
    ▼
per session: host->prepare_hot_reload(registrant)
    Calls on_stop on every affected slot. Marks stopped so the plugin-unload
    path does not call on_stop again.
    │
    ▼
per session: snapshot = host->snapshot_state(registrant)
    Captures [[=persist]] values, per-slot enabled + sleeping flags, entity
    blackboards. Bindings and editor settings are NOT captured — they live in
    the world's binding components, unaffected by the swap.
    │
    ▼
Plugin manager unloads the game plugin
    on_unload() → unregister_scripts_in_namespace<...>()
    (plugin .so unmapped; every instance destroyed without a second on_stop)
    │
    ▼
Plugin manager loads the new game plugin
    on_load() → register_scripts_in_namespace<...>()
    │
    ▼
per session: host->restore_state(snapshot)
    For each entity with a binding component, for each binding:
        lookup class → construct, apply editor settings from component,
        apply persist snapshot, restore enabled + sleeping,
        call on_start with was_reloaded
        not found → leave binding, log warning
    For each global slot in the snapshot:
        reattach via attach_global; apply persist; restore enabled + sleeping;
        call on_start with was_reloaded
        Duplicate global instance names → log and skip
    Merge entity blackboards into live blackboards
    │
    ▼
Coordinator marks reload complete and resumes the main loop
```

**Ordering is load-bearing.** `on_stop` runs *before* `snapshot_state`, so `[[=persist]]` writes performed in `on_stop` are captured. The plugin-unload path must not call `on_stop` again; the host marks stopped slots and skips them during teardown.

**Reload coordinator ownership.** The coordinator is the scripting plugin's responsibility. Single entry point:

```cpp
[[nodiscard]] std::expected<void, script_error> reload_game_plugin(
    std::string_view plugin_name, std::string_view registrant = {});
```

Drives: `prepare_hot_reload` → `snapshot_state` → `unload(plugin_name)` + `load(plugin_name)` → `restore_state`.

**Save-file restore is the same sequence minus the plugin swap.** A future save-file system calls `restore_state` with the save blob; scripts see `from_save` instead of `was_reloaded`.

### 9.2 Snapshot format

```
snapshot {
    u32 version
    u32 slot_count
    slot[slot_count] {
        u64     entity                   // INVALID_ENTITY for globals
        string  instance_name
        string  script_name
        bool    enabled
        bool    sleeping
        u32     persist_field_count
        { string name, bytes value }[persist_field_count]
    }
    u32 blackboard_count
    blackboard[blackboard_count] {
        u64     entity                   // never INVALID_ENTITY
        u32     entry_count
        { string key, u8 tag, bytes value }[entry_count]
    }
}
```

Bindings and editor settings are **not** included. `enabled` and `sleeping` are per-slot. Entity blackboards are per entity; the `tag` identifies which alternative of `blackboard_value` (§11.4) the bytes encode. Session blackboards do not exist.

If `registrant` is non-empty, only slots whose class was registered by that plugin are included. Blackboards are captured in full regardless of `registrant` (per-entity; keys not tagged by writer). On restore, blackboard entries are merged into the live blackboard.

`snapshot_state` returns `std::expected`. The error path is reserved for whole-snapshot failures (e.g., allocation). Per-field serialization failures are logged and the field is skipped.

### 9.3 Restore

1. Read `version`. Mismatch without a compatible migration → log `snapshot_version_mismatch`, return 0, leave session alone (bindings remain; scripts freshly instantiated on next reconcile).
2. Parse snapshot into maps keyed by `(entity, instance_name)` → `{ enabled, sleeping, persist values }`, plus `entity → blackboard entries`.
3. Reconcile every entity with a binding component. For each binding:
   - Class not found → log warning, **drop the binding**, continue.
   - Construct fresh instance (constructor runs, source defaults).
   - Apply editor settings from the component's `editor_settings` blob to `[[=editor]]` fields.
   - Apply persist snapshot for this `(entity, instance_name)` to `[[=persist]]` fields.
   - Restore `enabled` and `sleeping`.
   - Call `on_start(ctx)` with `was_reloaded`.
4. For each global slot in the snapshot, call `attach_global` and apply the same restore steps. `duplicate_name` (e.g., hand-edited snapshot) → log and skip.
5. Merge blackboard entries into each entity's blackboard.
6. Discard all pending wake conditions (§10.4).

Returns the number of slots restored.

### 9.4 Migration

For `[[=persist]]` fields whose layout changes:
- Added field → source default.
- Removed field → silently dropped.
- Type changed → `reflect::register_migration` transforms raw bytes before deserialization. Without a migration, source default + warning.

Migration functions registered in the game plugin's `on_load()` before `register_scripts_in_namespace`. Key is `(script_name, field_name)`, stable across type changes. `[[=editor]]` fields migrate the same way via the component's `editor_settings` blob.

**Nested fields are not migrated individually.** The migration key covers a top-level member only. If a `[[=persist]]` aggregate has a nested member whose type changes, the whole top-level field falls back to source default. Known limitation; dotted-path keys (`(script_name, "outer.inner")`) are a future extension.

---

## 10. Sleep and wake

### 10.1 API

```cpp
[[nodiscard]] bool is_sleeping() const noexcept;
void sleep();                                         // no wake condition
void sleep_for(f32 seconds);                          // timer
void sleep_until_contact();                           // next contact
template <typename E> void sleep_until_event();       // next event of type E
void wake_self();                                     // wake immediately, cancel all conditions
template <typename S> void wake_sibling(std::string_view instance_name = {});
```

### 10.2 Semantics

- Sleeping script receives **no `on_update` and no `on_contact`**, except the contact that satisfies a `sleep_until_contact()` condition (§8.3 step 2).
- Sleeping script **continues to receive events** it subscribed to via `subscribe_scoped`.
- `sleep()` marks the slot as sleeping with no wake condition. `sleep_for` / `sleep_until_contact` / `sleep_until_event` **add** a pending condition; the slot stays asleep until one fires.
- Multiple conditions may be active; the first to fire wakes the slot and cancels the others.
- **All sleep requests take effect only after the current callback returns.** `sleep_for(5.0f)` followed by `sleep_until_event<hit_event>()` registers both; the slot transitions to sleeping when the callback ends.
- `wake_self()` / `wake_sibling<S>()` wake immediately and cancel all pending conditions.
- `sleep_until_event<E>()` registers a one-shot subscription; on the first invocation the slot wakes and the subscription is torn down. Additive, not a replacement for a normal `subscribe_scoped<E>`.

### 10.3 Contact subscription enablement

Contact events are enabled on the entity's body **iff at least one attached slot either overrides `on_contact` or has a pending `sleep_until_contact()`**. Re-evaluated on reconcile and on every sleep/wake transition. When a slot wakes on a contact, `on_contact` is still dispatched to it (a no-op if the class doesn't override it).

### 10.4 Sleep and hot reload

Sleeping flag preserved in the snapshot; pending wake conditions discarded. Scripts that need to resume a timed or event-driven sleep test `ctx.is_sleeping()` in `on_start` and re-issue the appropriate `sleep_*()` call.

---

## 11. Communication

Four tiers, in order of preference.

### 11.1 Tier 0: session event bus

Each session owns a `GLT::event_bus` instance. Sessions never see each other's events.

```cpp
void door::on_start(script_context& ctx) {
    m_guard = ctx.events().subscribe_scoped<open_request_event>(
        [this, e = ctx.entity()](open_request_event& ev) { if (ev.target == e) open(); });
}

damage_event ev;
ev.attacker = ctx.entity();
ev.victim = other;
ev.amount = impulse;
ctx.events().post<damage_event>(ev);
```

`subscription_guard` is RAII — its destructor tears down the subscription, including during hot reload. `event_bus::post` snapshots subscribers before dispatch; reentrancy is safe. **Main-thread-only** (§7.5).

### 11.2 Tier 1: entity-targeted events

Same mechanism with `entity_id target` in the payload. Subscribers filter on the target.

### 11.3 Tier 2: sibling access (same entity)

```cpp
if (auto* loco = ctx.sibling<locomotion>())
    loco->walk_toward(m_next_waypoint);
if (auto* fast = ctx.sibling<rotate>("rotate_fast"))
    fast->boost(2.0f);
```

Returns a typed pointer to the first instance of class `S` on the same entity (or by instance name), or `nullptr`. **Same-plugin only** — mismatched `registrant` returns `nullptr` (with a debug log). This closes the cross-plugin dangling-pointer hazard in the two-tier split (§16.2). Cross-plugin communication is event-only.

### 11.4 Tier 3: entity blackboard (same entity)

```cpp
ctx.blackboard().set<bool>("is_alerted", true);
if (ctx.blackboard().get_or<bool>("is_alerted", false)) { /* ... */ }
```

Name-keyed map per entity. **Values are a closed `std::variant`**, not an open type-erased store:

```cpp
using blackboard_value = std::variant<
    bool, i32, u32, i64, u64, f32, f64,
    std::string,
    glm::vec2, glm::vec3, glm::vec4, glm::quat, glm::mat3, glm::mat4,
    world::entity_id, UUID
>;
```

Serializable by construction; the snapshot stores the alternative index as a `u8` tag. Users who need containers or aggregates use a session resource or a typed event. `get<T>` returns `std::optional<T>`, `nullopt` if the key is absent or the stored alternative is not `T`. `set<T>` overwrites (every `T` in the variant is serializable). Captured in the hot-reload snapshot.

### 11.5 External calls into scripts

| Target | Path |
|---|---|
| One-shot action (`editor_action`, `world_generator`) | `host->run_action(script_name, entity, params)` |
| Live `entity_script` / `global_script` | `host->call(script_name, method_name, entity, instance_name, params)` |

`call` invokes a `[[=editor_callable]]` method, constructing a `script_context`. `params`, if non-empty, deserializes into the method's optional serializable second parameter. Errors returned as `script_error`:

- `class_not_registered` — no class with that name.
- `method_not_found` — class has no such method.
- `not_callable` — method exists but is not `[[=editor_callable]]`.
- `wrong_kind` — `run_action` on a live-script kind, or `call` on a one-shot kind.
- `bad_params` — `params` could not be deserialized.
- `invalid_entity` — entity has no live instance of that class with that instance name.
- `script_threw` — script's method threw; exception caught and logged.

---

## 12. Physics integration

### 12.1 Contact routing

`script_host::update` drains `physics->drain_contact_events()` once per frame, before `on_update`. For each `contact_event`:
1. Resolve `body_a` and `body_b` to `entity_id` via the physics↔world bridge.
2. For every slot on `entity_a`:
   - Awake and enabled → `on_contact(ctx, entity_b, contact)`.
   - Sleeping with pending `sleep_until_contact()` → wake, cancel other conditions, `on_contact(ctx, entity_b, contact)`.
3. Symmetrically for `entity_b`.

Both sides see the contact from their own perspective. If one side has no associated entity (static collider), the other side receives `on_contact` with `other == INVALID_ENTITY`, and the entityless side receives no callback.

### 12.2 Enablement

See §10.3.

### 12.3 Ordering

Contacts fire **before** `on_update` for the same frame. Scripts react to contacts before the tick reads state.

---

## 13. Editor vs runtime — annotation kinds

There is one `i_script` interface. The distinction between editor scripts and play-time scripts is the annotation on the class.

| Kind | Attach model | When it runs |
|---|---|---|
| `entity_script` | Binding on the entity's component | `on_start`, `on_update`, `on_contact`, `on_stop` |
| `global_script` | `host->attach_global(name, instance_name)` | Same as `entity_script`; `ctx.entity()` is `INVALID_ENTITY` |
| `world_generator` | `host->run_action(name)` on world load or editor button | `on_start`/`on_stop` only; no `on_update`, no `on_contact` |
| `editor_action` | `host->run_action(name, entity)` on button press | `on_start`/`on_stop` only; instance torn down after return |

Live instances of `entity_script` / `global_script` can expose `[[=editor_callable]]` methods invoked via `host->call(...)`.

**World generator invocation.** `game_session::load_world()` calls `m_scripting->run_world_generators()` after `refresh_all()`. The host iterates every registered `world_generator` in registration order and calls `run_action` for each. The editor exposes a "Run Generators" button performing the same call.

### 13.1 Editor actions

An editor action is a one-shot script:

```cpp
[[=GLT::script::annotations::editor_action{"Rebuild NavMesh"}]]
class rebuild_navmesh : public i_script {
public:
    struct config {
        f32 merge_distance = 0.5f;
        u32 max_iterations = 1000;
    };
    [[=GLT::script::annotations::editor{"Settings"}]]
    config cfg;
    void on_start(script_context& ctx) override { /* ... */ }
};
```

`run_action`:
1. Construct fresh instance (constructor runs, source defaults).
2. Deserialize optional `params` into `[[=editor]]` fields.
3. Call `on_start`.
4. Call `on_stop` (releases any resources; `flags::none`).
5. Destroy.

Stateless by design — each invocation starts fresh. Persistent editor-side state belongs in a `world_generator` or a `global_script`.

---

## 14. Script bindings component

### 14.1 Definition

```cpp
struct script_binding {
    std::string                  script_name;         // "patrol"
    std::string                  instance_name;       // "patrol_1" — unique per entity
    std::vector<std::byte>       editor_settings;     // serialized [[=editor]] fields
};

struct script_binding_component {
    std::vector<script_binding>  bindings;
};
```

Plain, serializable data. No runtime state, no pointers, no behavior. The component is the **single source of truth** for which scripts belong to an entity and their per-instance settings.

### 14.2 Registration with the world

The scripting plugin is **engine-resident** (not hot-reloaded) and registers `script_binding_component` with the world plugin during its `on_load()`:

```cpp
void i_script_plugin::on_load() {
    if (auto* w = world::manager::get_ptr())
        w->register_component<script::script_binding_component>();
}
```

The component type is registered once for the process lifetime and never unregistered while data exists.

### 14.3 Adding and removing bindings

Bindings are added, removed, and modified by editing the component:

```cpp
auto& comp = world->ensure_component<script_binding_component>(entity);
comp.bindings.push_back({
    .script_name = "patrol",
    .instance_name = "patrol_1",
    .editor_settings = {},
});
host->refresh(entity);   // reconcile immediately for UI feedback
```

**Instance-name uniqueness.** Within one entity, `instance_name` must be unique. The editor enforces this. If a duplicate reaches the host (hand-edited asset, bug), reconciliation keeps the first and logs a warning for the rest; `sibling<S>(name)` returns the first match. `attach_global` returns `script_error::duplicate_name` for duplicate globals.

**Renaming an instance** is equivalent to remove + add: reconciliation tears down the old slot and instantiates a fresh one; persist state keyed by `(entity, instance_name)` is not carried across the rename. Scripts that need to preserve state across a rename should store it in the world or a session resource.

### 14.4 Reconciliation

The host keeps its slot table in sync with the component. Reconciliation runs:
- **Each frame** at `host->update()` start.
- **On demand** after an editor modifies a component, via `host->refresh(entity)` / `refresh_all()`.

| Component state | Slot action |
|---|---|
| Binding in component, no slot | Instantiate: construct, apply editor settings, `on_start` |
| Binding, slot exists, same class | Keep slot; if `editor_settings` changed, re-apply to live instance |
| Binding, slot exists, different class | Tear down old, instantiate new |
| Binding, class not registered | Leave binding (authored data), log once, skip instantiation. If the class is later registered, next reconcile picks it up. |
| Slot exists, no binding | `on_stop`, destroy |

The host caches a hash of each binding's `editor_settings` blob to detect changes.

**Cost.** O(total bindings) per frame. A dirty-flag optimization for large scenes is a straightforward future addition and does not change the contract.

Scripts that need a full restart on a specific setting change declare the field as `[[=editor]]` and cache the previous value in an unmarked or `[[=transient]]` field, comparing on each `on_update`.

### 14.5 Multiple scripts per entity

```cpp
auto& comp = world->ensure_component<script_binding_component>(player);
comp.bindings.push_back({ .script_name = "controller",   .instance_name = "controller",   .editor_settings = {} });
comp.bindings.push_back({ .script_name = "muzzle_flash", .instance_name = "muzzle_flash", .editor_settings = {} });
comp.bindings.push_back({ .script_name = "health",       .instance_name = "health",       .editor_settings = {} });
```

**Ordering within one entity:** component order, or by `[[=priority]]` (higher first, ties by component order). **Globals:** `attach_global` order; `priority` ignored. **World generators:** registration order.

### 14.6 Settings editing

Editor mutates the component's `editor_settings` blob for that binding, calls `host->refresh(entity)` for immediate visual feedback on the live instance. World plugin serializes on next world save. Restarting the editor preserves the settings because they live in the world asset.

### 14.7 Sibling access with multiple instances

`ctx.sibling<S>()` returns the first instance of class `S` in component order. `ctx.sibling<S>("name")` returns that specific instance or `nullptr`. Same-plugin enforcement applies (§11.3).

### 14.8 Multiple drivers

If two scripts on the same entity both write the same field, both apply in component order. Last writer wins within the frame. The idiomatic fix is one driver, one observer; the engine does not enforce it.

---

## 15. Reflection-driven registration

### 15.1 API

```cpp
template <auto Namespace>
void register_scripts_in_namespace(std::string_view registrant = {});

template <auto Namespace>
void unregister_scripts_in_namespace();
```

### 15.2 Usage in the game plugin

```cpp
void game_plugin::on_load() {
    if (auto* s = GLT::script::manager::get_ptr())
        s->register_scripts_in_namespace<^^game::scripts>("game");
}

void game_plugin::on_unload() {
    if (auto* s = GLT::script::manager::get_ptr())
        s->unregister_scripts_in_namespace<^^game::scripts>();
}
```

### 15.3 Discovery

Recursive descent:
1. Enumerate `std::meta::members_of(Namespace, access_context::unchecked())`.
2. For each member:
   - **Namespace** → recurse.
   - **Complete class deriving from `i_script`** → validate and register (§5.4).
   - **Anything else** → silently ignored.
3. A class deriving from `i_script` with zero kind annotations is a **registration error** (log-and-skip), not a silent fall-through.

The descent uses C++26 `template for` (or `std::meta` recursion) and is entirely compile-time. A script that fails validation is logged and skipped; one bad script does not block the others.

### 15.4 Unregistration

Walks the same namespace, re-derives the names, removes each. O(scripts) per reload.

### 15.5 Namespace convention

Scripts live in a designated **root namespace** or any sub-namespace. The walk descends recursively, so the project can organize scripts by domain without the registration call changing:

```
game::scripts                      ← root, passed to register_scripts_in_namespace
game::scripts::world               ← walked
game::scripts::world::terrain      ← walked
game::scripts::ai::combat          ← walked
game::scripts::weather             ← walked
```

`script_name` (derived from `reflect::type_name`) does not include the namespace, so moving a script between sub-namespaces is a no-op from the registry's perspective. The root is whatever the game plugin passes; `game::scripts` is the recommended convention.

---

## 16. Build model

**One game plugin. One `.so`. Every script in it.**

### 16.1 Rationale

| Concern | Combined `.so` | Per-script `.so` |
|---|---|---|
| ODR / RTTI across boundaries | One `typeinfo` per type | Distinct per `.so` — `dynamic_cast` across boundaries fails |
| Template instantiation | One instance per type | One per `.so` — separate addresses |
| `dlopen` cost | One call | N calls with symbol resolution × N |
| Plugin manager fit | One entry in `s_loaded_plugins` | N entries, or all forced through `interface::custom` |
| Dependency ordering | None | Topological sort per load |
| Debugger | One symbol universe | Invalidates on every rebuild |

Hot reload granularity is the whole plugin. For a 5–10 second build this is invisible; for 30+ seconds it is annoying but is a compile-time problem (PCH, ccache, mold/lld), not an architectural one.

### 16.2 Two-tier split (future)

If compile time becomes a bottleneck:
- **`game_stable`** — engine glue, UI wiring, save/load hooks. Loads once, never reloads.
- **`game_hot`** — gameplay scripts. Reloads on save.

Both call `register_scripts_in_namespace` on the same `i_script_plugin` with distinct registrant strings (`"game_stable"`, `"game_hot"`). Reloading `game_hot` calls `prepare_hot_reload("game_hot")` and `snapshot_state("game_hot")`; `game_stable` slots keep running untouched. The `registrant` field, the `registrant` parameters on `prepare_hot_reload` / `snapshot_state`, and the same-registrant check in `sibling<S>()` exist specifically to support this split without further interface changes.

---

## 17. Integration with existing systems

### 17.1 `game_session`

Add one member, declared **after** `m_world` and `m_physics`:

```cpp
unique_ref<physics::i_physics>          m_physics{};
unique_ref<world::i_world>              m_world{};
unique_ref<script::i_script_host>       m_scripting{};   // destroyed first
```

Construction creates the host if the scripting plugin is loaded. `update()` follows §8.1. `load_world()` calls `m_world->set_physics(...)`, then, if `m_scripting`, `m_scripting->refresh_all()` and `m_scripting->run_world_generators()`. `unload_world()` calls `m_scripting->refresh_all()` after the world is cleared (tears down every entity-backed slot).

### 17.2 `i_world`

Two responsibilities:
1. **Component registration and storage** for `script_binding_component`; serialize it into the world asset.
2. **Generic component access** so the host can iterate entities with a `script_binding_component` (e.g. `for_each_component<script_binding_component>(...)` or an ECS view).

The exact shape of these APIs is part of the world plugin's design and outside this document's scope.

### 17.3 `i_physics`

No additions. Uses existing `enable_contact_events` and `drain_contact_events`. The physics↔world bridge provides `body_handle → entity_id`.

### 17.4 `plugin_manager`

Add `interface::scripting`. Existing discovery, load, unload, generation bump, and cache mechanisms apply unchanged.

### 17.5 `game_layer`

Plays no role beyond the initial session lifecycle. Hot reload is driven by the scripting plugin's coordinator (§9.1).

### 17.6 Save-file system (future)

The scripting system's only contribution to save-file persistence is `snapshot_state()` / `restore_state()`. The save-file system calls these at save/load time, storing the blob alongside the session's other state. Save restore calls `restore_state` with the same blob format and the same `was_restored` semantics as hot reload. Scripts that gate baseline capture on `!was_restored` behave correctly on load.

---

## 18. Non-goals

- **A scripting language.** Scripts are C++. No Lua, Python, WASM, or visual scripting.
- **Cross-plugin sibling access.** `ctx.sibling<S>()` requires compile-time knowledge of `S`; cross-plugin communication is event-only.
- **Per-script `.so` reload.** See §16.
- **A script debugger.** The C++ debugger is the debugger.
- **Sandboxing.** Scripts are game code written by the developer, with full engine access.
- **Deterministic cross-platform float behavior.** Out of scope.
- **Preservation of static data across reload.** Statics reset when the plugin reloads.
- **Runtime state persistence in the world asset.** World assets hold authored data only.
- **Editor field widgets beyond the basics.** Widget set from `reflect::annotations` (range, display name, tooltip, multiline, color). Custom widgets for exotic types are an additive future extension.
- **Runtime removal of bindings.** Bindings are authored data. Runtime code stops a script with `disable_self()`; removing a binding is an editor operation.
