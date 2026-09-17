

sudo chown -R mich:mich ~/workspace/Gluttony



# TODO
- application creates a lot a textures -> add some tracking mechanics OR runtime diagnostic










# Existing Editors

- Image viewer
- Console / Log viewer
- Statistics / Profiler HUD


# Planned Editors

- Settings / Preferences Editor
- Plugin manager UI
- Dependency graph viewer
- Keybinding editor
- Theme / style editor





categorized list of editors:

## Tier 1 — Zero prerequisites (pure engine/editor tooling)

**Settings / Preferences editor**
Every engine has one. Sections: Editor (theme, fonts, keybindings, docking layout), Rendering (default backend, vsync, MSAA, resolution scale), Audio, Input, Network, Paths (project dir, cache dir, plugin dir). Since you already have ImGui and a `pannel_collection`, this is a natural next window. Persist via your serializer.

**Plugin manager UI**
You have a plugin system with descriptors and dependency interfaces. A window that lists loaded plugins, their phase, target interface, dependencies, load status, and a "reload" button is high-value and near-zero risk. It also makes developing further plugins easier — which pays back immediately.

**Dependency graph viewer**
Visualize the plugin dependency graph (`i_window_plugin` → `i_renderer_plugin` etc.) as a node graph. Helpful once your plugin count grows past ~5.

**Keybinding editor**
A table of actions → key combos, with conflict detection and per-context (editor / game / play-mode) profiles. Pairs with the input mapping editor below.

**Theme / style editor**
Edit ImGui colors, spacing, rounding, font sizes, and save named themes. `ImGui::ShowStyleEditor` is a starting point, but a first-class "Engine Theme" window with named presets is what shipping engines have.



## Tier 2 — Simple file formats

These only need a file loader, not an asset pipeline.

**Shader editor**
A text editor (ImGui has `InputTextMultiline`) with a "compile" button that pipes source through your `shader_compiler`, and an error output pane that shows compiler messages. If you have `glslang`/`spirv-cross` in-process, you can even show a SPIR-V disassembly side-by-side. Extremely high value for engine development.

**Config / JSON / INI editor**
A tree view of the config file plus a text pane. Since you have a serializer, you can round-trip: edit values in the tree, see the text update; edit the text, see the tree rebuild. This is the "Regedit for your engine" and it saves enormous time during debugging.

**Input mapping editor**
Action → key table with an interactive "press a key to bind" mode. If your engine has an input abstraction (likely, given a window plugin), this is a small wrapper around it.

**Localization / string table editor**
A two-column view (key → localized text) with per-locale tabs, search, and missing-entry highlighting. Even before you have an asset system, a CSV or JSON-backed string table is trivial.

**Data table editor**
Generic CSV/JSON-backed tabular editor. Useful for gameplay constants, item definitions, dialogue lines, etc. Completely asset-system-free — it's just a spreadsheet over a text file.



## Tier 3 — Procedural / parameter-driven

These generate content from parameters rather than loading it.

**Curve editor**
Bezier / Catmull-Rom curve editing with control points, tangents, presets (linear, ease-in/out, ease-in-out), and export to a lookup table. Used for animation easing, LUTs, falloff curves, audio envelopes. Pairs beautifully with a small "preview" pane that shows the curve applied to a gradient or a rotating sprite.

**Gradient editor**
Color ramp editing with stops, interpolation (linear, step, smooth), and alpha. Feeds into particle color-over-lifetime, sky colors, UI theming, terrain tint. Same pattern as the curve editor, just 4 channels instead of 1.

**Procedural texture generator**
Perlin/simplex/worley noise with frequency, octaves, persistence, lacunarity, domain warping. Live preview at 128×128 or 256×256 through your existing `image` class. Combine with your new histogram and channel view to inspect results. This is a great "wow" editor and needs zero asset infrastructure.

**LUT (color grading) viewer/editor**
Load a `.cube` file or generate an identity LUT, edit its 3D LUT via a slider grid, and preview the effect on a test image (your image viewer already has the preview pipeline). Extremely high value for a ray-traced renderer.

**Particle system editor**
Even a simple one: emitter parameters, spawn rate, lifetime, velocity curves, color-over-lifetime gradient, size-over-lifetime curve, and a viewport preview using ImGui's `DrawList` (or a real GPU particle pass later). No assets — it's all parameters, and the "particle" can be a simple quad.

**Camera / Cinematic editor**
Keyframed camera paths with easing, FOV, focal length, DOF, and a scrub bar. Works against your existing `world::camera`. Useful for demos, trailers, and cutscenes even before you have a scene format.



## Tier 4 — Minimal asset support

These need *some* asset concept, but the minimal version is small.

**Scene hierarchy / Outliner**
A tree of entities with parent/child relationships. If you don't have an ECS yet, a plain `std::vector<entity>` with a parent index is enough. Add: rename, reparent, enable/disable, delete, duplicate, focus.

**Inspector / Details panel**
Property grid for the selected entity. Auto-generated from your C++ types (a lightweight reflection macro or a manual registration table) is the "right" way, but a hand-written inspector for a few core component types is a fine start.

**Material editor (flat parameter version)**
Before node graphs, a material is just: name, base color, roughness, metallic, normal map, emissive, plus a set of named parameters. That's a form, not a graph. It only needs your image viewer's texture binding (which you already have via `image`). The node graph comes much later.

**Viewport / Scene view**
You probably already have a viewport for the game. An *editor* viewport adds: gizmos (translate/rotate/scale), grid, axes, selection outline, stats overlay, view modes (lit/unlit/wireframe/normals/UV/overdraw). These are all drawing tricks on top of what you have.

**Animation timeline**
Scrub bar, keyframe tracks, play/pause, loop, speed. Even a single-track version (camera or transform animation) is a huge quality-of-life boost and doesn't need an asset system if the animation is authored in-editor.



## Tier 5 — Debugging tools

These make the *other* editors and the engine itself easier to develop.

**Render graph viewer**
Visualize your pass graph: nodes = passes, edges = resource dependencies, colors = read/write. Even a hand-authored version of this saves hours when debugging a renderer.

**Descriptor set / pipeline state viewer**
List descriptor set layouts, bindings, current contents, pipeline state (blend, raster, depth). Very useful for a Vulkan renderer.

**GPU resource inspector**
List all live `image`s and buffers with size, format, layout, usage flags, and a thumbnail for textures. You already track VRAM and expose `get_size()`/`get_descriptor_set()` on `image`, so a texture browser is a small extension of the image viewer.

**Frame debugger (single-frame capture)**
Freeze the frame, step through passes, show inputs/outputs of each pass. Even a coarse version (pause, dump each pass's output to the image viewer) is transformative for debugging a ray tracer.

**Memory profiler**
Heap allocations over time, per-subsystem breakdown, peak vs current, leak detection at shutdown. Pairs with your existing VRAM tracking.

**Network / Replication inspector** (later)
Once you have multiplayer, this shows packets, entities, RPCs, bandwidth over time.



## "Meta" editors

Because you have a plugin system and a serializer, these are unusually valuable for you specifically:

**Serializer inspector**
Open any project file and browse its tree: types, fields, versions, migration history. Lets you debug save/load without writing a bespoke viewer for every format.

**Reflection / property editor generator**
A window that takes a registered type and auto-generates an inspector UI. Feeds every future editor you write. Even a crude version (macros + `ImGui::InputText`/`DragFloat`) pays for itself in a week.

**Plugin template generator**
Wizard that scaffolds a new plugin: `.h`, `.cpp`, `descriptor`, CMake entries. Makes adding editors cheaper.

**Command palette**
Ctrl+P style fuzzy search over every registered editor action ("open image viewer", "reload renderer", "toggle checkerboard"). This is what makes the whole editor feel cohesive once you have 15+ windows.
