# Game modules

A project's own C++ (scripts, construct packs, anything else that registers with the engine) is built into a
*game module*: a shared library the engine loads after its plugins. This is step 1 of the order in
[scripting.md §0.6](scripting.md); hot reload (step 2) builds on it.

## Layout

```
<project>/
    project.glt_proj
    CMakeLists.txt              the project's build script (template below)
    CMakePresets.json           optional: debug / release / release_with_debug_info presets
    game/                       the module's sources (.h / .cpp, any sub folders)
    build/<config>/bin/         <name>.so + <name>.glt_module (manifest), written by the build
    build/<config>/loaded/      copies the engine actually loaded (the build may overwrite bin/ meanwhile)
```

`<config>` is `debug`, `release` or `release_with_debug_info`. The engine is started with the project directory as its
first argument and loads the modules of **its own** configuration: a debug engine loads `build/debug/bin`, a release
engine `build/release/bin`.

## The project's CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.25)

set(GLUTTONY_DIR "$ENV{GLUTTONY_DIR}" CACHE PATH "Gluttony engine source directory")
if(NOT GLUTTONY_DIR)
    set(GLUTTONY_DIR "${CMAKE_CURRENT_LIST_DIR}/../Gluttony" CACHE PATH "Gluttony engine source directory" FORCE)
endif()
include("${GLUTTONY_DIR}/cmake/gluttony_game_module.cmake")     # before project(): selects the engine's compiler

project(my_game LANGUAGES CXX)
gluttony_add_game_module(my_game)
```

`gluttony_add_game_module(<target> ...)` (engine file `cmake/gluttony_game_module.cmake`) does the rest: the engine's
compiler and flags (C++26, reflection, the configuration defines, the engine's precompiled header), the engine headers,
the output location, the manifest and the entry point. Options:

| Option | Default | |
|---|---|---|
| `MODULE_NAME` | target name | module id and file name |
| `ROOT_NAMESPACE` | target name | namespace everything is registered from |
| `SOURCE_DIR` | `${PROJECT_SOURCE_DIR}/game` | |
| `SOURCES` | every .cpp / .cxx / .c below `SOURCE_DIR` | |
| `ENTRY_HEADERS` | every .h / .hpp below `SOURCE_DIR` | headers the generated entry point includes |
| `NO_GENERATED_ENTRY` | off | write the entry point yourself (`GLT_GAME_MODULE(ns)`) |
| `LINK` | | extra libraries |

`GLUTTONY_DIR` is the engine's **source** directory. When the editor builds the module it passes it itself (the engine
knows where it was built from).

Building by hand:

```
cmake --preset debug   && cmake --build --preset debug
cmake --preset release && cmake --build --preset release
```

or without presets: `cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug && cmake --build build/debug`.

In the editor: **Tools > Game module > Build and load** (configures if needed, builds in the background, output in the
log, then unloads the loaded modules and loads the new build), **Load** and **Unload**.

## What the module contains

Nothing but its own code: the entry point is generated. It includes every header of the module (reflection only sees
what that one translation unit sees) and expands `GLT_GAME_MODULE(<root namespace>)`, which exports the descriptor and
registers everything below the root namespace:

| Found | Goes to | Registered as |
|---|---|---|
| classes deriving from `script::i_script` with a kind annotation | scripting | registrant = module id |
| namespaces annotated `[[=construct::pack{...}]]` | procedural construction | owner = module id |
| `root::on_module_load(GLT::modules::context&)` | called last | |
| `root::on_module_unload(GLT::modules::context&)` | called first on unload | |

**Declare script classes and packs in headers.** One defined only in a .cpp file is invisible to the entry point.

```cpp
// game/scripts/spinner.h
namespace my_game::scripts {
    struct [[=GLT::reflect::annotations::entity_script{}]] spinner : GLT::script::i_script {
        [[=GLT::reflect::annotations::editor{"Spin"}]] f32 degrees_per_second = 45.0f;
        void on_update(GLT::script::script_context& ctx, f32 dt) override;
    };
}

// game/trees/trees.h: a construct pack
namespace my_game {
    namespace [[=GLT::construct::pack{ .name = GLT::construct::text("my_game.trees"), .display = GLT::construct::text("Trees") }]] trees {
        struct [[=GLT::construct::generator_info{ .name = GLT::construct::text("my_game.trees.trunk") }]] trunk final
            : GLT::construct::generator_of<trunk_settings> { ... };

        // optional, found by name:
        void customize_pack(GLT::construct::pack_desc& desc);                    // upgrade, self_test, ...
        void register_extras(GLT::construct::i_registry& registry, std::string_view owner, const std::string& pack);
    }
}
```

`modules::context` gives the module id, the project directory, the library path, the scripting plugin (null if none
is loaded) and the construct registry.

## Loading and unloading

The engine loads modules after all plugins (`application_ready`, after the Construct editor is installed) and unloads
them before the plugins (`pre_application_shutdown`). A module is refused, with the reason in the log, when

- it has no manifest-named library, no `gluttony_module_descriptor` symbol, or a symbol it needs is missing in the engine
  (usually: the engine changed, rebuild the module),
- its **ABI stamp** differs: compiler version, configuration, linkage style, standard library ABI flags,
  `modules::ABI_VERSION`, or the **layout hash**. The layout hash is computed by reflection over the engine types a module
  touches (`module/abi.h`: names, sizes and offsets of data members, virtual functions). A module built against older or
  newer engine headers is refused instead of crashing later,
- a module with the same id is already loaded.

Unloading runs the module's `on_module_unload`, then every **participant** releases what the module registered (in
reverse order: editor windows and menu items, construct packs after waiting for running evaluations, script classes and
default globals), then the library is closed. Engine systems and plugins add their own participant with
`modules::add_participant({ name, after_load, release })`. A module only undoes what the engine can not see: threads it
started, global state, files.

## Not done yet

- **Hot reload** beyond scripts: Build and load unloads and loads the modules. Script instances survive it (their
  persist fields are restored, scripting.md §0.7) and construct packs come back (open Construct editors re-evaluate).
  Other objects of module types that the engine holds must be released in the module's `on_module_unload` or by a
  participant.
- Static linkage (`LINKAGE_STYLE_STATIC`, shipped games): the module would be linked into the executable; not wired.
- Windows: loading is implemented for Linux only.
- A file watcher that rebuilds on save.
