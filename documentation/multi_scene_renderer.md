
# Multi-Scene Renderer — Interface Design Document

## 1. Purpose and Scope

The engine must render multiple scenes concurrently and independently:

- an editor world viewport, a play-mode window, a mesh-editor preview, a material-editor preview, multiple multiplayer windows, and future tool views.

Each of these is modeled as a **scene renderer**: an isolated rendering pipeline with its own target (window-backed or offscreen), its own lifecycle, its own mutable state, its own camera binding, and its own debug drawing channel. Everything immutable — meshes, textures, pipelines — is uploaded once through a **shared resource manager** owned by the renderer plugin and referenced by every scene renderer through stable handles.

This document specifies the interfaces, their contracts, their ownership boundaries, and the composition rules between them. It is deliberately backend-agnostic: the concrete renderer plugin may be a forward rasterizer, a ray tracer, or any future backend, and none of those choices appear in this interface. Backend-specific concerns (device creation, queues, swapchains, command recording, acceleration structures) live inside the plugin implementation and are not part of this contract.

The design preserves the existing `world -> submit_scene` flow, the asset-handle based submission, the `i_debug_draw` side-interface pattern, and the plugin-based microkernel architecture.

**Out of scope:** process isolation between scenes, multi-device rendering, and any backend-specific resource type or synchronization primitive.

---

## 2. Design Principles

1. **Shared-immutable below, per-scene-mutable above.** Resources read by more than one scene renderer live in the shared layer. Resources tied to a single scene or to a frame-in-flight belong to the scene renderer that owns them.
2. **Backend-agnostic interfaces.** No interface in this document references a graphics API type. Any backend may implement `i_scene_renderer` by translating the abstract submission and lifecycle calls to its own pipeline model.
3. **One plugin instance per process, one plugin per backend.** The renderer plugin and everything it implements — shared resource storage, scene renderers, debug draw, shader compiler — live in a single plugin. These internal components communicate directly through C++ references; they do not cross the plugin interface when talking to each other. The plugin interface exposes only what the application layer needs.
4. **Submission is a sink.** `submit_scene` replaces the receiving scene renderer's view of the scene. It never calls back into the world layer.
5. **Every scene renderer owns its camera.** Two renderers attached to the same world may look through different cameras. The camera binding is per-renderer and is resolved by the world layer, not shared across renderers.
6. **Debug draw is per-scene.** Each scene renderer exposes its own `i_debug_draw`. Primitives submitted to one scene renderer never appear in another.
7. **Procedural meshes have three lifetime classes**, each with a defined update contract. The API distinguishes them so backends can choose an appropriate strategy per class.
8. **Handles, not pointers.** Cross-layer references are small generated handles. No shared ownership on hot paths.
9. **Failure is expressed, not asserted.** Construction, uploads, and frame production return typed results. Callers decide how to react.

---

## 3. Architecture Overview

```
+-------------------------------------------------------------+
| Application (editor / game / tools)                          |
|   owns world layers, coordinates scene renderers             |
+-------------------------------------------------------------+
| i_renderer_plugin  (public interface — backend entry point)  |
|   - scene renderer factory                                   |
|   - per-frame shared-storage tick                            |
+-------------------------------------------------------------+
| plugin implementation (backend-specific)                     |
|                                                              |
|   shared resource storage  <---direct C++ ref at construction|
|   (meshes, textures, pipelines)                              |
|                                                              |
|   scene renderer implementation #1 (surface)                 |
|   scene renderer implementation #2 (offscreen)               |
|   ...                                                        |
|                                                              |
|   per-scene i_debug_draw implementations                     |
+-------------------------------------------------------------+
```

The application layer interacts only with `i_scene_renderer`, `i_debug_draw`, and the asset system. The world layer interacts only with `i_scene_renderer`. Scene renderers hold a direct reference to the shared resource storage within the same plugin. No component above the plugin sees a backend type.

---

## 4. Shared Resource Manager

### 4.1 Responsibility

Own every resource that is **immutable after upload** and **shared between scenes**: meshes, textures, and the derived state built from them (pipelines, material tables, acceleration structures, bindless sets — whatever the backend requires). Deduplicate by asset handle. Never reference scene state. Never expose the underlying resource type.

The shared resource manager is an **internal component of the renderer plugin**, not an interface exposed on `i_renderer_plugin`. Scene renderers receive a direct C++ reference to it at construction time and call it without a plugin boundary hop.

### 4.2 Interface used by scene renderers

```cpp
namespace GLT::render {

using shared_mesh_id    = u32;   // 0 = invalid
using shared_texture_id = u32;

struct upload_error {
    enum class code : u32 {
        none = 0,
        not_found,
        decode_failed,
        unsupported,
        backend
    } kind = code::none;
    std::string message{};
};

class shared_resource_manager {
public:
    virtual ~shared_resource_manager() = default;

    // --- meshes ---
    // Idempotent per asset handle.
    // Returns a handle immediately; completion is observed via is_mesh_ready().
    virtual std::expected<shared_mesh_id, upload_error>
        ensure_mesh(GLT::asset::handle asset) = 0;
    virtual void retain_mesh(shared_mesh_id id) = 0;
    virtual void release_mesh(shared_mesh_id id) = 0;
    [[nodiscard]] virtual bool is_mesh_ready(shared_mesh_id id) const = 0;

    // --- textures ---
    virtual std::expected<shared_texture_id, upload_error>
        ensure_texture(GLT::asset::handle asset) = 0;
    virtual void retain_texture(shared_texture_id id) = 0;
    virtual void release_texture(shared_texture_id id) = 0;
    [[nodiscard]] virtual bool is_texture_ready(shared_texture_id id) const = 0;

    // --- frame tick ---
    // Advances internal upload state; retires completed work.
    // Called from i_renderer_plugin::begin_frame().
    virtual void begin_frame(u64 frame_number) = 0;
};

}
```

The interface is abstract so that scene renderer implementations can be tested against a stub, but the concrete implementation is a plugin-internal type. It is not exposed on `i_renderer_plugin` and is not reachable by the application layer.

### 4.3 Contract

- **Immutable after readiness.** Once `is_*_ready()` returns true, the resource behind the handle never changes contents. A mesh that needs modification is replaced by uploading a new asset and swapping the handle in the scene submission. This is what allows concurrent reads across scene renderers without coordination.
- **Asynchronous upload.** `ensure_*` returns a handle before the resource is usable. A scene renderer must check `is_*_ready()` and may substitute a fallback (unit mesh, default texture) while the resource is in flight.
- **Reference-counted lifetime.** `retain_*` and `release_*` are balanced. A resource is released when the last retain is dropped and no scene renderer holds a live reference.
- **Deduplication is by asset handle.** Repeated `ensure_*` calls with the same asset return the same shared id.
- **Derived state is internal.** Pipelines, material tables, descriptor tables, acceleration structures, and any other backend-specific artifacts are constructed and cached inside the manager. None of these appear in the interface.
- **The manager is shared within one plugin.** All scene renderers created by a given plugin instance reference the same manager. Two plugins (e.g., a rasterizer and a ray tracer) each own their own.

### 4.4 Why this is internal to the plugin

The scene renderer and shared resource manager are always paired: a scene renderer cannot exist without a shared resource manager, and the shared resource manager is meaningless without at least one scene renderer consuming it. Bundling them in one plugin:

- Avoids a virtual dispatch and a plugin-boundary hop every time a scene renderer resolves a shared handle.
- Lets the scene renderer implementation share internal types with the shared manager (e.g., a rasterizer's bindless descriptor sets, a ray tracer's BLAS/TLAS handles) without exposing them through a generic interface.
- Keeps the plugin's public surface small. The application layer never needs the shared manager directly — it only needs to know when a *scene* is ready, which `submit_scene` already answers.

---

## 5. Scene Renderer Interface

### 5.1 Responsibility

Own everything mutable and tied to one drawn target: the target image or window surface, per-frame state, camera-derived state, procedural mesh storage, debug draw queue, and the backend's internal per-scene pipeline state.

### 5.2 Target descriptor

```cpp
struct surface_target_desc {
    ref<GLT::platform::i_window_plugin> window;
};

struct offscreen_target_desc {
    glm::uvec2                          extent{ 512, 512 };
};

using scene_target = std::variant<surface_target_desc, offscreen_target_desc>;

struct scene_renderer_desc {
    scene_target                        target;
    std::optional<glm::vec4>            clear_color;
    u32                                 frames_in_flight = 3;
    u64                                 procedural_mesh_capacity_bytes = 8ull * 1024 * 1024;
};
```

The variant is the only place a caller distinguishes window-backed from offscreen. Everything else is uniform.

### 5.3 Interface

```cpp
class i_scene_renderer {
public:
    virtual ~i_scene_renderer() = default;

    // --- content submission ---
    // Replaces the scene renderer's current view of the scene.
    // Thread-safe. May be called once per frame before update().
    virtual void submit_scene(std::span<const GLT::asset::mesh::instance> instances) = 0;
    virtual void submit_lights(std::span<const GLT::world::light_snapshot> lights) = 0;
    virtual void set_active_camera(const GLT::world::camera_snapshot& camera) = 0;

    // --- per-frame lifecycle ---
    // update() runs before render(). It packs per-frame state and prepares
    // internal buffers from the last submission. Cheap and side-effect free
    // with respect to the target.
    virtual void update(f32 delta_time) = 0;

    // render() produces one frame on the scene renderer's target.
    // Returns an error when the target must be resized or when the frame
    // could not be produced; the caller resizes and retries.
    [[nodiscard]] virtual std::expected<void, render_error> render(u64 frame_number) = 0;

    // --- target lifecycle ---
    virtual void resize(glm::uvec2 extent) = 0;
    [[nodiscard]] virtual void*      get_output_image() const = 0;   // UI-compatible handle
    [[nodiscard]] virtual glm::uvec2 get_extent() const = 0;
    [[nodiscard]] virtual bool       is_offscreen() const = 0;

    // --- debug draw ---
    [[nodiscard]] virtual GLT::render::i_debug_draw* get_debug_draw() = 0;

    // --- render mode ---
    [[nodiscard]] virtual std::span<const GLT::render::render_mode_info>
        supported_modes() const = 0;
    virtual void set_render_mode(u32 mode_id) = 0;
    [[nodiscard]] virtual u32 get_render_mode() const = 0;

    // --- procedural meshes ---
    virtual procedural_mesh_id create_procedural_mesh(mesh_update_class cls) = 0;
    virtual void update_procedural_mesh(procedural_mesh_id id,
        std::span<const GLT::asset::mesh::vertex> vertices,
        std::span<const u32> indices) = 0;
    virtual void destroy_procedural_mesh(procedural_mesh_id id) = 0;
};

struct render_error {
    enum class code : u32 {
        none = 0,
        target_out_of_date,     // caller should resize and retry
        target_lost,            // caller should recreate the renderer
        submission_rejected,    // input was invalid
        backend
    } kind = code::none;
    std::string message{};
};
```

### 5.4 Contract

- **Per-scene isolation.** Every scene renderer owns its own target, per-frame state, camera state, procedural buffers, and debug draw queue. Two scene renderers never share mutable state, even when they reference the same world.
- **Shared reads only.** A scene renderer reads shared resources through its internal reference to the shared resource manager. It does not mutate shared resources.
- **Submission replaces, not accumulates.** `submit_scene` and `submit_lights` each replace the previous submission. Two consecutive calls without an intervening `update()` collapse to the last.
- **Camera is per-renderer.** `set_active_camera` sets the camera for this renderer only. Two scene renderers attached to the same world may hold different camera snapshots.
- **`update` before `render`.** The lifecycle is strictly `submit_* → update → render`. `render` without a preceding `update` may be rejected.
- **`render` may fail.** `target_out_of_date` signals the caller to `resize` and retry on the next frame. `target_lost` signals the target was lost and the renderer should be recreated.
- **`get_output_image` returns a UI-consumable handle** whose concrete type is backend-defined. The application treats it as an opaque ID for its UI layer. It is not null after the first successful `render`.
- **Threading.** `submit_*`, `update`, `set_active_camera`, and `get_debug_draw()->*` are safe to call from the update thread. `render` is called from the application frame loop. Two scene renderers may be rendered concurrently; a single scene renderer may not.

### 5.5 What a scene renderer does not expose

- No backend types: no device, no command buffer, no descriptor, no pipeline layout, no synchronization primitive, no swapchain, no acceleration structure.
- No direct way to draw. The only draw path is `submit_scene` and `render`.
- No access to other scene renderers.
- No access to the shared resource manager through the scene renderer interface. The shared manager is used internally.

This is the boundary that makes a forward rasterizer and a ray tracer interchangeable from the application's perspective.

---

## 6. Renderer Plugin Interface

### 6.1 Responsibility

The plugin is the backend's public entry point and the owner of the shared resource manager. It creates scene renderers and ticks the shared manager once per frame. It does not itself render, does not drive frames, and does not track the scene renderers it has created.

### 6.2 Interface

```cpp
class i_renderer_plugin : public GLT::plugin_manager::i_plugin {
public:
    // existing: create(), destroy(), backend identity, feature queries, shader compiler side interface, settings side interface

    // --- scene renderer factory ---
    // Constructs a scene renderer bound to this plugin's shared resource manager. The returned renderer is owned by the caller
    virtual std::expected<GLT::unique_ref<i_scene_renderer>, scene_renderer_error> create_scene_renderer(
        const scene_renderer_desc& desc) = 0;

    // --- per-frame tick ---
    // Called once per frame by the application before any scene renderer's update()
    // Forwards to the shared resource manager's begin_frame(), advancing upload state for the whole plugin
    virtual void begin_frame(u64 frame_number) = 0;
};

struct scene_renderer_error {
    enum class code : u32 {
        none = 0,
        target_invalid,
        feature_unsupported,
        backend
    } kind = code::none;
    std::string message{};
};
```

### 6.3 Not on the plugin interface

The following are deliberately absent from `i_renderer_plugin`:

| Absent | Reason |
|---|---|
| `shared_resources()` accessor | The shared manager is internal; scene renderers get direct references at construction. The application never needs to touch shared resources directly. |
| mesh/texture upload entry points | Owned by the shared resource manager, driven by `submit_scene`. |
| `submit_scene`, `submit_lights`, `set_active_camera` | Per-scene concern, owned by `i_scene_renderer`. |
| target size, clear color, render mode | Per-scene concern, owned by `i_scene_renderer`. |
| per-scene frame driving (`begin_frame`/`draw_frame` per target) | Replaced by `i_scene_renderer::update` / `render`. `begin_frame(N)` on the plugin now only ticks shared storage. |
| `i_preview` side interface | Deleted; replaced by offscreen `i_scene_renderer`. |
| `i_debug_draw` side interface | Per-scene via `i_scene_renderer::get_debug_draw()`. |
| `set_world_render_enabled` | Application-level concern — the application simply skips `render` calls. |

The plugin retains: backend identity, device-level feature queries, settings, the `i_shader_compiler` side interface, the shared resource manager (internal), and the scene renderer factory.

### 6.4 Contract

- **One plugin per process.** The plugin manager guarantees a single registered renderer instance.
- **`create_scene_renderer` may fail.** Unsupported target types or feature queries return a typed error. The application decides whether to fall back.
- **The plugin does not track scene renderers after creation.** It does not update them, does not render them, does not destroy them. The caller owns the returned `unique_ptr`.
- **`begin_frame(N)` runs before any scene renderer.** It advances the shared resource manager's internal frame state for the whole plugin.

---

## 7. Application Integration

### 7.1 World layer and per-renderer cameras

The world layer gathers scene data once and submits it to each attached scene renderer. Every renderer gets its own camera binding, resolved by the world layer from an entity id supplied at attach time.

```cpp
class world_layer {
public:
    // Attach a renderer with a camera entity binding. The entity must carry a camera component; the world layer resolves
    // its snapshot every frame. Passing INVALID_ENTITY leaves the renderer without a camera from this layer (useful for
    // offscreen targets that manage their own camera)
    void attach_scene_renderer(GL::render::i_scene_renderer* renderer, GLT::world::entity_id camera_entity);

    void detach_scene_renderer(GL::render::i_scene_renderer* renderer);

    // Change the camera binding of an already-attached renderer
    void set_renderer_camera(GL::render::i_scene_renderer* renderer, GLT::world::entity_id camera_entity);

    // existing: update, render_imgui, set_world, set_controller

private:
    struct renderer_binding {
        GL::render::i_scene_renderer*           renderer;
        GLT::world::entity_id                   camera_entity;
    };
    std::vector<renderer_binding>               m_scene_renderers;
    // existing buffers
};

void world_layer::update(f32 dt) {

    m_world->update(dt);
    auto* scene = m_world->as<GLT::world::i_world_scene>();
    if (!scene)
        return;

    scene->gather_scene(m_scene_buffer);
    scene->gather_lights(m_light_buffer);

    for (auto& binding : m_scene_renderers) {
        GLT::world::camera_snapshot cam;
        const bool has_camera = (binding.camera_entity != GLT::world::INVALID_ENTITY) &&
            scene->get_camera_view(binding.camera_entity, cam);

        binding.renderer->submit_scene(m_scene_buffer);
        binding.renderer->submit_lights(m_light_buffer);
        if (has_camera)
            binding.renderer->set_active_camera(cam);
    }
}
```

`i_world_scene` gains a per-entity camera query to support this:

```cpp
// Fill [out] from the camera component on [camera_entity].
// Returns false if the entity is dead or has no camera component.
[[nodiscard]] virtual bool get_camera_view(entity_id camera_entity, camera_snapshot& out) const = 0;
```

The existing `get_camera_view(camera_snapshot&)` and `set_active_camera(entity_id)` may be kept as conveniences for single-view applications but are not used by the multi-renderer path.

### 7.2 What this means for two cameras in one world

Two editors looking at the same world from different angles are modeled as:

- Two `i_scene_renderer` instances, each with its own target.
- Both attached to the same `world_layer` with different camera entities.
- The world layer gathers the scene once and submits it to both, with a different `camera_snapshot` per renderer.

The two renderers receive the same instances and lights but different cameras. Their output images differ accordingly.

An editor viewport and a play-mode window follow the same pattern: two scene renderers, two camera entities (editor camera, player camera), one world layer.

---

`render` failures drive a resize on the next frame. Window resize events call `scene_renderer->resize(...)` directly; offscreen targets resize the same way through the editor's layout code.

### 7.3 Multi-world / multiplayer

Two worlds, two world layers, two scene renderers. Both scene renderers share the same plugin and therefore the same shared resource manager: static meshes and textures are uploaded once. Camera bindings, procedural buffers, per-frame state, and debug queues are per-scene.

Two scene renderers attached to the **same** world layer (split-screen, editor + player view of one world) share scene gathering but still hold distinct cameras and distinct per-frame state.

---

## 8. Debug Draw

`i_debug_draw` remains a side interface, obtained per-scene:

```cpp
auto* dbg = scene_renderer->get_debug_draw();
dbg->line({0,0,0}, {1,0,0}, {1,0,0,1});
```

The scene renderer owns the primitive queue, lifetime bookkeeping, watched meshes, and watched entities. `highlight_entity` and `set_draw_mesh_aabb` operate against the scene renderer's last submitted scene. No global debug draw exists; the plugin does not implement the side interface.

### 8.1 Contract

- **Per-scene isolation.** Primitives submitted to one scene renderer never appear in another.
- **Thread-safe submission.** Any thread may submit primitives.
- **Lifetime semantics unchanged.** `lifetime == 0` renders once. `lifetime > 0` persists across frames. `DEBUG_DRAW_FOREVER` persists until `clear()` or re-set.
- **Backend renders primitives as it sees fit.** The interface does not require lines to be drawn as lines — a ray tracer may rasterize debug primitives through an internal overlay path, and the interface permits that.

---

## 9. Preview Subsystem

Previews are offscreen scene renderers driven by an application-side controller. No dedicated preview interface exists at the render layer.

```cpp
class material_preview_controller {
public:
    material_preview_controller(i_renderer_plugin& plugin, const material_preview_desc& desc);

    void update(f32 dt);                     // animates camera, resubmits content
    i_scene_renderer* renderer();
    void* output_image() const;              // UI-compatible

private:
    GLT::unique_ref<i_scene_renderer>  m_renderer;
    GLT::render::procedural_mesh_id    m_mesh;
    GLT::render::preview_camera        m_camera_state;
    std::vector<mesh::instance>        m_instances;
    std::vector<light_snapshot>        m_lights;
    camera_snapshot                    m_camera;
};
```

Each frame the controller updates the orbit camera, resubmits the preview content (a single procedural sphere plus one directional light), and lets `render()` produce the image that `output_image()` exposes to the editor's UI.

Every preview type — material, mesh, instance, inspector thumbnails — follows the same pattern. Previews are typically **not** attached to a `world_layer`; their controller submits scene data directly. If a preview needs world data, it attaches to a world layer with its own camera binding, exactly like a viewport.

---

## 10. Procedural Meshes

Procedural meshes are owned by scene renderers because they are not shared. Three classes are distinguished so the backend can choose an appropriate strategy per class:

```cpp
enum class mesh_update_class : u8 {
    immutable = 0,     // uploaded once, never updated
    occasional,        // updated rarely, with dirty tracking
    streaming,         // updated most frames, ring-buffered
};
```

### 10.1 Contract per class

| Class | `update_procedural_mesh` may be called | Visibility |
|---|---|---|
| `immutable` | Exactly once, after creation | From the next frame |
| `occasional` | Any number of times, any rate | From the next frame after the update |
| `streaming` | Any number of times, every frame permitted | From the next frame after the update |

For `immutable`, a second call returns an error; the caller destroys and recreates. The interface does not specify *how* the backend stores each class — only the visibility guarantee and the number of legal updates. A ray-tracing backend may implement `immutable` as a pre-built acceleration structure and `streaming` as a per-frame refit; a rasterizing backend implements them as persistent and ring-buffered vertex buffers respectively.

### 10.2 Submission

Procedural mesh instances are submitted through `submit_scene` alongside asset-backed instances. The `mesh::instance` type is extended to carry a variant source:

```cpp
using instance_source = std::variant<
    GLT::asset::handle,          // shared mesh
    procedural_mesh_id           // per-scene procedural mesh
>;
```

The scene renderer resolves the variant: asset handles are translated to `shared_mesh_id` through its internal shared resource manager reference, procedural ids are looked up in its own table. The rest of the instance (transform, materials, submesh mapping) is identical for both.

---

## Appendix A - Backend Portability (reference only, not forced but a light suggestion)

The `i_scene_renderer` interface is the compatibility boundary. Any backend implements it independently:

| Concern | Rasterizer | Ray tracer |
|---|---|---|
| `submit_scene` | Pack instances into per-frame scene state | Refit or rebuild per-scene acceleration structures |
| `update` | Frustum culling, light-grid preparation | Camera matrices, acceleration refit preparation |
| `render` | Raster passes into the target | Trace, denoise, composite into the target |
| `streaming` procedural meshes | Ring buffer per frame-in-flight | Per-frame acceleration refit |
| Debug draw | Overlay lines/triangles | Internal rasterization overlay, or ray-traced primitives |
| `get_output_image` | Whatever the UI layer accepts | Same |

Each backend is a self-contained plugin containing its own shared resource manager, scene renderer implementations, and per-scene debug draw. The application layer never branches on backend identity. A feature the current backend does not support is reported through `supported_modes()` and `create_scene_renderer` errors; the application degrades gracefully.
