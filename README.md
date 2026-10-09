
# Gluttony

**A work-in-progress game engine built around a plugin-based, microkernel architecture.**

Gluttony is the start of an actual game engine. The core stays small and loads most of its functionality as plugins, so subsystems can be developed, swapped, and tested independently. It’s still very early: many features are missing or incomplete, and the code is rough around the edges. But it’s no longer just an experiment - it’s the foundation of an engine.

> **⚠ This is very much a work in progress.**  
> Expect missing features, occasional jank, and interfaces that don’t do much yet. You’ve been warned.

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

- **Core** – application lifecycle, plugin manager, event bus, layers, config, logging, crash handling, thread pool, and basic utilities.
- **Plugin system** – phase-based loading, dependency resolution by name or interface, shared and static plugin variants.
- **Game loop** – dedicated loop plugin that owns tick timing and drives the update/render cycle; the application no longer has to manage layers directly.
- **Asset registry** – chunked, versioned `.glt_*` file format with a string table, dependency graph, path->handle index, hot-reload watching, and async load. Handlers and factories are separate plugins: handlers decode a type at runtime, factories convert external formats into our files. Shipped today:
  - **mesh** – static mesh geometry (vertices, indices, submeshes, bounds).
  - **texture** – 2D textures.
  - **world / region** – world region index and region entity blobs.
- **World manager** – EnTT-backed world plugin. Stable `entity_id ↔ entt::entity` slot table, region streaming driven by an anchor + radius, a pluggable `entity_codec` for region blobs, and an `i_world_inspector` surface the editor uses to enumerate components and edit them.
- **Window** – GLFW-based window plugin with input, fullscreen modes, vsync, and Vulkan surface creation.
- **Renderer** – Vulkan hardware accelerated ray-tracer plugin (`renderer_vk_ray`).
- **Audio** – SoLoud + miniaudio backend plugin (`audio_soloud`).
- **Editor** – large ImGui/ImPlot editor plugin with content browser, image/audio viewers, log viewer, stats, viewport, world outliner, and icon manager.
- **Virtual File System** – unified filesystem abstraction using `vfspp`.
- **Logger** – asynchronous logger with custom format strings, severity levels, and thread labels.

Most of these are first-pass implementations. The editor and asset registry are the most fleshed out; the renderer and audio backends are functional but minimal.

---

## Planned / missing

- Proper settings system to select plugin implementations per interface
- Asset handler / factory coverage beyond the current mesh, texture, and world types (materials, animation, audio clips, skeletal mesh, …)
- Resource cache and streaming policy on top of the asset registry (refcounting, eviction)
- Physics, scripting, UI system, networking
- Additional renderers, window backends, and audio backends
- Better Windows support
- More editor tools and polish

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

Content / asset pipeline
- Import factories (currently only: mesh/texture/audio)
- VFS

World / ECS / scene
- Component serialization
- world serialization
- world streaming
- controller

Platform / OS
- Filesystem/dialogs
- Timers
- Crash handler

Editor / tooling
- content browser (with thumbnail cache, import panel, drag-drop into scene, ...)
- Material editor
- World inspector (Editor outliner/details)

Engine infrastructure
- generic serialization
- reflection

# Planned features until a real game can be created
Gameplay / Runtime
- physics
- scripting (attaching a script to an entity)
- animations
- runtime UI (currently using ImGui for debugging)
- save/load system
- prefabs / entity templates
- navigation

World
- terrain/volumes
- navigation

Networking
- networking/online

Asset Pipeline / Content
- hot-reload watcher
- asset cooking/packaging
- resource cache

Build / Export
- exporting the game (currently works only with editor)
- asset cooking/packaging

Rendering
- portable renderer (currently using HW RT, need a rasterizer as well)
- shader/pipeline system
- Render graph
- LOD
- Post-processing
- Particles/VFX
- Debug draw/gizmos
- Sky/atmosphere

Editor / Tooling
- Undo/redo
- Selection/gizmos
- Play-in-editor

Engine Infrastructure
- Memory manager
- Versioning/migration - asset versions exist, but no migration framework
- Plugin ABI/versioning
- Tests/CI

Platform / OS
- in-game console
