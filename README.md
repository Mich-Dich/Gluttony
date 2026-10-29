# Gluttony

**A work-in-progress game engine built around a plugin-based, microkernel architecture.**

Gluttony is the start of an actual game engine. The core stays small and loads most of its functionality as plugins, so subsystems can be developed, swapped, and tested independently. It’s still early: features are landing fast and the API is moving, but the pieces needed to actually build and run a game are coming together.

> **⚠ This is very much a work in progress.**  
> Expect missing features, occasional jank, and interfaces that don’t do much yet

---

## Quickstart

### Prerequisites

- Linux (primary target; Windows support is partial, macOS is unsupported)
- CMake ≥ 3.20
- A C++26 compiler with experimental reflection support
- Vulkan SDK 1.4.357.x or newer
- Git with SSH access to GitHub (some vendor dependencies are cloned via SSH)

### Install Vulkan SDK

Download the newest [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) (1.4.357.x) and extract it to `~/vulkan-sdk/`.

Add to your `~/.bashrc`:

```bash
export VULKAN_SDK=~/vulkan-sdk/1.4.357.1/x86_64
export PATH=$VULKAN_SDK/bin:$PATH
export LD_LIBRARY_PATH=$VULKAN_SDK/lib:$LD_LIBRARY_PATH
export VK_LAYER_PATH=$VULKAN_SDK/etc/vulkan/explicit_layer.d
```

Then `source ~/.bashrc`.

### Build


**Install GCC 16 and G++ 16**

The repo uses C++26's reflection, so g++-16 is needed
```bash
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt update
sudo apt install gcc-16 g++-16
```


**Clone and Build project**
```bash
git clone https://github.com/your-username/gluttony.git
cd gluttony

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

Run:

```bash
./build/bin/Debug/Gluttony /path/to/project
```

On startup it discovers plugins, initializes config/logging/crash handling, creates the window, audio, renderer, and editor, and enters the main loop. You should get a window with an ImGui editor and log output.

---

## What exists today

- **Core** – application lifecycle, plugin manager, event bus, layers, config, logging, crash handling, thread pool, argument parser, and utilities.
- **Plugin system** – phase-based loading, dependency resolution by name or interface, shared and static plugin variants, and a global generation counter for cache invalidation of raw plugin pointers.
- **Game loop** – dedicated loop plugin that owns tick timing and drives the update/render cycle; the application no longer manages layers directly.
- **Game sessions** – `game_layer` owns every `game_session`. The *primary* session is created eagerly and never destroyed (the editor's persistent world); *sim* sessions come and go. Each session owns its world, physics instance, controller, and per-session sim clock, and renderers are attached externally (editor viewport, sim windows) rather than owned by the session. Sessions can be paused, and their sim time/delta are reported independently.
- **Asset registry** – chunked, versioned `.glt_*` file format with string table, dependency graph, path/id indexes, hot-reload watching, async load, and `read_chunk()` for editor peek-without-load.
  - **`asset_ref`** – RAII smart-handle for owned references. Copy/move-aware, refcounted through the registry, implicitly convertible to `handle`. Pass it anywhere a raw handle was expected.
  - **Handlers and factories** are separate plugins: handlers decode a type at runtime (from chunks), factories convert external formats into our files. The registry owns the file header, string table, and dependency table; both sides talk chunk layouts only.
  - **Embedded thumbnails** – importers can write `CHUNK_THUMBNAIL` (RGBA8, ≤256px) that the editor reads through `read_chunk()` without a full load.
  - Shipped handlers today:
    - **mesh** – static mesh (vertices, indices, submeshes, bounds, authored colliders).
    - **texture** – 2D textures (mip chains, format/usage metadata).
    - **world / region** – world region index and region entity blobs.
    - **audio** – canonical interleaved f32 PCM, loop points, metadata.
    - **material / material_instance** – Slang source, compiled bytecode, reflection schema, packed parameters.
- **Reflection** – C++26 compile-time reflection registry. Type/member descriptors, enum entries, serialization and editor-draw overrides, migrations by version, and annotations (`serialize`, `skip`, `transient`, `asset_ref`, `version`, `display_name`, `tooltip`, `category`, `range`, `color`, `multiline`, `readonly`, `name`).
- **World manager** – EnTT-backed world plugin. Stable `entity_id ↔ entt::entity` slot table, region streaming driven by an anchor + radius, pluggable `entity_codec` for region blobs, and a per-instance `i_world` (RAII, multiple can coexist). Surfaces for the editor: `i_world_inspector` (component registry + edits) and `i_world_scene` (per-frame renderable/lights/entity-positions snapshot).
- **Entity presets** – `preset_registry` plus recursive `entity_preset` trees. Presets describe *intent* (components + property bags) so world plugins interpret them; the editor consumes them to build the create menu without knowing ECS.
- **Window** – GLFW-based, **multi-window capable**. A `window_handle` per OS window; the plugin handles fullscreen modes, vsync, resize, cursor mode, and Vulkan surface creation. ImGui backend binding is per-window/context aware.
- **Scene renderers** – **multi-viewport rendering**. `i_scene_renderer` owns one render target: either a *surface* (a window) or an *offscreen* texture. `i_renderer_plugin::create_scene_renderer()` mints one per target; each has its own render mode, output image, extent, debug draw, and (optionally) ImGui pass. Procedural meshes (`immutable`, `occasional`, `streaming` classes) can be uploaded per scene renderer.
- **Renderer** – Vulkan **Forward+ renderer in progress**, exposed through `i_renderer_plugin` with per-renderer settings (reflection-described for the editor) and a per-scene render-mode selector. A Slang-based `i_shader_compiler` produces renderer-native bytecode and material reflection. A separate `i_debug_draw` interface is exposed for immediate-mode debug primitives (lines/triangles/AABBs/arrows/spheres/capsules/cones/polylines), plus entity highlighting and mesh-AABB outlines.
  - The earlier hardware ray tracer (`renderer_vk_ray`) still exists but is no longer the active target.
- **Audio** – SoLoud + miniaudio backend. **Voice-handle based**: `play(asset_handle, config)`, per-voice volume/pan/speed/loop, transport (seek/position), streaming, full 3D source (position, velocity, attenuation models) and a global listener, and a global master volume. Loading/caching is the registry's job; the plugin only plays.
- **Physics** – Jolt-backed `i_physics` instances, one per session. Bodies, shapes, and joints use generation-checked handles. Full state read/write (single and batched), force/impulse/torque, activation, ray casts, sphere/box overlap queries, contact-event draining, and per-instance gravity. Instances are minted by `i_physics_plugin::instantiate()` and destroyed when the owning `unique_ref` drops.
- **Editor** – large ImGui/ImPlot editor plugin with content browser (thumbnail cache, import panel, drag-drop into scene), image/audio viewers, log viewer, stats, multi-viewport support, world outliner/details, and an icon manager.
- **VFS** – unified filesystem abstraction using `vfspp`.
- **Logger** – asynchronous logger with custom format strings, severity levels, and thread labels.
- **Crash handler** – attach/detach plus `subscribe()`/`unsubscribe()` for callbacks to run at crash time (e.g. log flush).
- **Argument parser** – spec-driven CLI with typed values (`string`/`int`/`double`/`bool`/`path`/`UUID`), positional and named args, defaults, and `std::error_code` integration.

---

## Planned / missing

- Asset handler/factory coverage beyond current types (animation, skeletal mesh, volume, bvh, …)
- Resource cache and streaming policy on top of the asset registry (refcounted eviction)
- Scripting
- Runtime/in-game UI (ImGui is still the debugging surface)
- Networking / online subsystem
- Prefab save/load flow (presets exist; the round-trip asset pipeline doesn't)
- Navigation, terrain/volumes
- Forward+ completion, render graph, LOD, post-processing, particles/VFX, sky/atmosphere
- Additional renderers, window backends, and audio backends
- Better Windows support
- Memory manager, undo/redo, play-in-editor polish
- Asset cooking/packaging and standalone game export
- Tests/CI, plugin ABI/versioning story

---

## Documentation

Implementation details, architecture notes, and plugin authoring guides will live in the [GitHub Wiki](https://github.com/mich-dich/gluttony/wiki). This README is intentionally high-level.

---

If you find a bug or want to add something, open an issue or a PR. I can’t promise prompt responses, but I’ll be thrilled that someone else looked at the code.



# Existing features (will likely be polished further)
Runtime
- Input system
- Camera system
- Audio mixer/streaming/effects
- Job system / thread pool
- Game session system (primary + sim sessions)
- Physics (Jolt)
- Multi-window / multi-viewport rendering

Content / asset pipeline
- Import factories (mesh/texture/audio/material)
- Chunked asset format with embedded thumbnails
- VFS
- Reflection-driven serialization
- Asset hot-reload on dependency change

World / ECS / scene
- Component serialization
- World serialization
- World streaming (anchor + radius regions)
- Controller
- Entity presets (recursive)
- World inspector (component registry + editor surface)

Platform / OS
- Filesystem/dialogs
- Timers
- Crash handler (with crash-time callbacks)
- Argument parser

Editor / tooling
- Content browser (thumbnail cache, import panel, drag-drop into scene, peek-without-load)
- Material editor
- World outliner/details
- Debug draw / immediate-mode primitives

Engine infrastructure
- Generic serialization
- C++26 reflection registry (with migrations + overrides)

# Planned features until a real game can be created
Gameplay / Runtime
- Scripting (attaching a script to an entity)
- Animations
- Runtime UI (currently using ImGui for debugging)
- Save/load system (game-side, on top of asset registry)
- Navigation
- (Physics exists; polish remains)

World
- Terrain / volumes

Networking
- Networking / online

Asset Pipeline / Content
- Asset cooking / packaging
- Resource cache with eviction
- Remaining handlers: animation, skeletal mesh, light profiles, BVH, volumes

Build / Export
- Exporting the game (currently works only with editor)
- Standalone packaging

Rendering
- Forward+ renderer completion (rasterizer)
- Render graph
- LOD
- Post-processing
- Particles / VFX
- Sky / atmosphere

Editor / Tooling
- Undo/redo
- Selection/gizmos
- Play-in-editor

Engine Infrastructure
- Memory manager
- Versioning/migration framework for assets
- Plugin ABI/versioning
- Tests/CI

Platform / OS
- In-game console
