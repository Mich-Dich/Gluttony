# Gluttony – my little plugin playground

**A scrappy, work‑in‑progress C++23 application skeleton that loads things as plugins because why not?**

**Gluttony** is my hobby project: a microkernel‑ish executable that discovers shared libraries at runtime and stitches them together into a running program. The plugin manager sorts out load phases and dependencies, an event bus lets everything talk without knowing about each other, a GLFW window plugin actually opens a window and translates input into typed events, the async logger is fully baked with its own format language, and there’s even a crash handler for when things inevitably go sideways. It’s still rough around the edges, the code has training wheels, and you definitely shouldn’t use this for anything serious… but if you enjoy poking at half‑finished C++ experiments, pull up a chair.

> **⚠ This is very much a work in progress.**  
> The core runs, the plugin manager resolves dependencies, and the logger is actually pretty neat, but large parts are still just empty interfaces waiting for an implementation. Expect missing features, occasional jank, and comments that promise things that don’t exist yet. You’ve been warned.

---

## Quickstart

**Prerequisites**  
- Linux (Windows maybe later, when I buy a new keyboard to smash)  
- C++23 compiler (GCC ≥ 11 or Clang ≥ 13)  
- CMake ≥ 3.21  

```bash
git clone https://github.com/your-username/gluttony.git
cd gluttony
git submodule init
git submodule update
cmake -B build -DCMAKE_BUILD_TYPE=debug
cmake --build build --parallel
./build/bin/debug/Gluttony
```

It’ll start up, discover any plugins in the `plugin/` directory, load the ones ready for `pre_engine_init`, fire up the logger, create a window, and then … still just sit there because the application loop isn’t doing much yet. But you’ll get a pretty log file and a window that listens to input events.

---

## Core systems

* **Plugin Manager**
  * **Discovery**: The core scans the `plugin/` folder for shared libraries. Each one must export a descriptor function (`gluttony_plugin_descriptor`) that returns a struct with the plugin’s name, required load phase, and dependencies.
  * **Dependency resolution**: The manager loads plugins phase by phase. Within a phase, it repeatedly scans the list of candidates and loads any plugin whose dependencies are already present (from an earlier phase or from another plugin that just got loaded in the same phase). It keeps retrying until no more progress can be made. If a plugin still has unmet dependencies after all that, the manager logs a desolate error message and moves on to the next phase. So if plugin `B` needs plugin `A` and both are in the same phase, `A` gets loaded first.
  * **Lifetime**: `create_plugin()` gives the core a shiny new instance, `on_load()` lets it initialise, and later `on_unload()` tells it to clean up. When the core shuts down, plugins are unloaded in reverse order.

  The plugin interface (`i_plugin`) is intentionally minimal. Subsystem interfaces (like `i_window_plugin` for a window) inherit from it, so the core can talk to them without knowing the details.

* **Event Bus**
  A type‑safe publish/subscribe system that lets any part of the engine (plugins, core, or even other systems) define custom events and post them. Subscribers register via `subscribe<T>(handler)` and receive only events of the exact type `T`. Dispatching takes a snapshot of the handlers, so it’s safe to add or remove subscribers while an event is being fired. Events like `window_resize`, `key_event`, `mouse_event`, `window_drop`, etc. are already defined, and plugins can create their own by deriving from `event_template`.

* **Crash Handler**
  Catches signals (Linux) or exceptions (Windows) and runs any registered cleanup functions. The logger subscribes automatically, so you still get the last log lines when the program faceplants.

* **Resource Cache** (header‑only)
  Thread‑safe `resource_manager` template with optional background cleanup. Not used yet, but ready for a future asset pipeline.

* **Utility Library**
  Platform helpers (executable path, high‑precision sleep, system time), macro shortcuts for getters/setters, copy/move deletion, and other C++ boilerplate that makes life a little less painful.

---

## Current plugins

* **Logger**
  I spent way too much time on this. The format string is the heart of it:
  So `"$B[$T:$J] $L$E $C$Z"` prints something like `[14:22:05] INFO Hello` in colour.
  You can change the format on the fly, register thread names, and set a severity threshold that controls when buffered messages get flushed. It’s asynchonous, so the calling thread just drops a message and runs.

* **GLFW Window**
  A fully functional window plugin built on GLFW. It handles creation, input, and all the usual window callbacks (resize, move, focus, mouse, keyboard, drag‑and‑drop, etc.) and translates them into engine events via the event bus so any other system can react. Supports windowed / fullscreen / borderless fullscreen, vsync, cursor modes, and exposes a native window handle for rendering backends. It’s what makes the demo window open and respond to your mouse and keys.

* **Virtual File System**
  A unified filesystem abstraction using `vfspp`. It mounts the native OS filesystem at virtual `/` (configurable base path) and implements all core VFS operations: existence checks, directory listing, create/remove/rename/copy, and handle-based I/O (open/read/write/seek/tell/close). Thread-safe handle management with support for memory and ZIP backends planned.

---

## Current TODOs
- add capability to recompile the core and then static link the plugins
- create a settings system so the core can decide what plugin to use for a given task (e.g. glfw_window or sdl_window for the window)

### planned plugins
- **Memory Manager** – Central allocator, tracking, and leak detection
- **Configuration / Settings** – Typed settings API fed by config files and command line
- **Input System** – Decoupled input processing: action maps, chords, device abstraction
- **Graphics API Abstraction (RHI)** – Thin wrapper over Vulkan/D3D12/OpenGL
- **Render Hardware Interface (Device)** – GPU device, swapchain, primary render targets
- **Renderer Frontend** – Drawing pipeline, mesh submission, materials, post‑processing
- **Asset Registry** – Asset discovery, GUIDs, and metadata database
- **Resource Cache** – Thread‑safe on‑demand asset loading and background streaming
- **Entity Component System (ECS)** – Data‑oriented entity/component management and system execution
- **Scene / World Manager** – Load/unload scenes, serialisation, scene streaming
- **Physics** – Collision detection and dynamics (Bullet/PhysX/Jolt)
- **Audio** – 2D/3D sound playback, sound banks, spatial audio
- **Scripting** – Embed a language (Lua, AngelScript, C#) for gameplay logic
- **UI System** – Retained‑mode UI for menus and HUDs (e.g., Slate, Noesis)
- **Editor Core** – Editor framework: viewport, hierarchy, inspector, content browser
- **Editor Plugins** – Modular tools (material editor, terrain, graph editor) that extend the editor
- **Networking** – Client/server connections, entity replication, RPCs
- **Online Subsystem** – Platform‑specific services (Steamworks, Epic Online Services)
---

If you find a bug or want to add something, open an issue or a PR – I can’t promise prompt responses, but I’ll be thrilled that someone else looked at the code.

*Thanks for stopping by. Now go build something equally half‑baked.*
