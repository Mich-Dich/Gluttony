# Terrain Generation Plugin

> **Status: T0 + T1 done (verified, self test passes), T2 implemented (not yet built), T3–T7 proposal.** It builds on
> [procedural_geometry.md](procedural_geometry.md) (the procedural mesh store, `i_world::set_procedural_mesh`, baking). The
> editor extension API lives in `core/src/editor/i_editor.h`; plugins register through `GLT::editor::add_editor_hook()`
> (load-order independent; plugin `update()` is not dispatched by the engine).
>
> **T1 as built** (`plugin/terrain`):
> - Node SDK in `include/terrain/` (`types.h` lattice regions + fields, `node.h` node base / context / `node_builder` /
>   registry interface, `noise.h`). Parameters are declared with `node_builder` and bound to member variables (the member
>   initializer is the default). This replaces the `std::meta` annotation sketch below for now: same single source of truth,
>   no reflection needed per node. `register_nodes<^^namespace>` for node packs remains a T5 convenience on top.
> - Regions are integer lattice rectangles (`x0, y0, width, height, spacing`); every field is indexed by global lattice
>   coordinates, so a sample is bit-identical in any region that contains it.
> - Evaluator (`src/graph/evaluator.*`): upstream sort, regions grown by aprons, per-node signature cache (a cached region
>   that contains the request is reused), `evaluate_async` on `GLT::thread_pool`, cancellation.
> - Expression compiler (`src/expression/`), 61 built-in node types (`src/nodes/`). Not in T1 yet: heightmap from texture,
>   curve node (needs the details panel, T2), white / blue noise, points.
> - Asset type `terrain_graph` (`.glt_terrain_graph`, chunks GRPH / GEDT / GPRV), handler, "Create Asset" entry, a T1
>   summary window as its asset editor (lists the graph, test-evaluates the surface).
> - Acceptance test: Tools > Terrain > Run self test (every node: four quadrants equal the whole region bit for bit; graph
>   round trip; expressions). Results go to the log.
>
> **T2 as built** (`src/editor/`): `terrain_editor_window` is the asset editor: node canvas | sidebar (2D map on top,
> details below), both splits draggable. Toolbar, details and palette use core's `util/ui/panel_collection` (cards,
> property rows with reset-to-default markers, segmented controls, search field). The canvas colours are patched in
> VisualNodeSystem comes from our fork (github.com/Mich-Dich/visual_node_system) with: `Node::DrawFrame` (custom node
> frame), `Node::GetSocketLocalPosition` (custom socket layout), socket hit area = circle + label (highlighted on
> hover), pressing a connected input picks its link up, socket / wire colour from the first socket type,
> `NodeArea::GetSettings()` (style, font, context menu) and `SetZoomLimits()`, octilinear ("subway") connections
> (`ConnectionShape::Octilinear`: horizontal / vertical / 45 degree segments only, also for hit testing), middle-mouse
> panning, canvas / node dragging not blocked by in-node widgets. No source patches any more; the sync
> skips a checkout with uncommitted changes.
> - Nodes (`vns_terrain_node`): own frame (category-coloured header, accent border when selected), one row per pin
>   with the sockets on the node edge, then one row per parameter edited right in the node (Blender-style drag fields
>   with a fill bar for bounded values, double-click / Ctrl+click to type; switches; segmented controls or combos;
>   vector fields; the expression node gets a code field with its compile state). Right-click a node: reset
>   parameters. Parameters travel with copy / paste (JSON).
> - Canvas (`graph_canvas`): one VisualNodeSystem adapter class (`vns_terrain_node`) for every node type; the canvas is
>   compiled back into the terrain graph every frame (add via right-click palette with search, delete, connect, copy /
>   paste), grid snap, zoom 8 % .. 115 %. VisualNodeSystem's own context menu is disabled by a CMake patch (its
>   rename-socket / sub-area entries would break the pin mapping); sub-areas, reroutes and comments are not compiled yet.
>   Node positions are stored in CHUNK_EDITOR as "id x y" lines, not as VisualNodeSystem JSON.
> - Map (`terrain_map`): the selected node's output (pin chooser for several outputs, sinks show their inputs), else the
>   Surface; fills its rectangle (256 samples along the longer side) on the thread pool, hill-shaded height tint or
>   heat map for masks, pan / zoom, value readout; overlays for value range, scale bar, timing and errors. One
>   evaluation at a time; out-of-date ones finish unless they run long.
> - Details: node title, description, a read-only parameter summary, pins; with nothing selected: graph info, preview
>   domain and preview overrides of exposed parameters.
> - Not yet: undo, the map's chunk grid / point views, graph-level exposed parameter list (the Exposed Parameter nodes
>   are the list for now).
> - Preview relation: the canvas marks the node the map shows (ring in its header) and every node upstream of it; a
>   node that does not feed it shows an amber warning + tooltip on hover. Clicking a widget inside a node edits it
>   without selecting it, so the preview can stay on a downstream node while upstream values change.
>
> **T4 as built** (`src/world/`):
> - Engine: plugin-defined components are first-class (`core/src/world/owned_storage.h`): `component_registry::
>   add_owned<T>(name, category, draw, summary, save, load, required, scope)` / `remove_owned<T>()`. The values live in
>   an `owned_storage<T>` (created in the plugin, never in the world's entt registry), flagged by
>   `i_storage::owns_data()`. `world_ecs_entt` saves / loads them inside each entity's codec record (codec
>   `extension`), removes them with their entity (`release_slot`), and keeps the payloads of components no loaded
>   plugin provides (`preserved_components`): written back unchanged on save, handed to the type when its plugin
>   registers, and filled from the live values when a plugin unloads. A world opened without the terrain plugin keeps
>   its terrain settings.
> - Engine: `i_world::spawn_transient / set_transient / is_transient` (runtime-only entities: no region, never saved,
>   not listed as roots), `i_world::set_mesh` (static mesh asset on an entity), `i_world::set_entity_name`.
> - `terrain_component` (`terrain.world_terrain`, world scope): graph (saved as its content path), seed, exposed
>   parameter overrides, mode (live / baked), enabled, chunk size / resolution / detail levels / view distance / detail,
>   bake extent / level / folder. Versioned, append-only payload. Inspector with asset picker, overrides from the
>   graph's Exposed Parameter nodes, Regenerate / Bake / Clear bake, status line. The terrain editor's "Apply to
>   world" adds / updates it on the edited world (graph, preview seed, preview overrides, live).
> - Live (`terrain_streamer`, one per live world, driven by the update event through `terrain_world_system`): quadtree
>   over the ground around the world's streaming anchor (the session's primary camera); a chunk splits while the
>   anchor is closer than detail x its size. Same quads per chunk at every level, lattice per level, skirts hide the
>   cracks between levels. Evaluated on the thread pool (holes first, then nearest, 4 at a time), procedural meshes on
>   transient entities. Chunks leaving the selection stay until their area is covered. Any change of graph revision,
>   seed, overrides, chunk size / resolution or Regenerate re-evaluates every chunk (old meshes stay until replaced).
> - Bake (`terrain_world_system::bake`): the bake extent at the bake level, evaluated synchronously, one static mesh
>   asset per chunk (`procedural_mesh_store::bake`, folder `<bake folder>/<world>_<timestamp>/chunk_L<l>_<x>_<y>`),
>   ordinary entities at the chunk corners (so they land in their regions) under a "Terrain (baked)" entity. Clear
>   bake removes those entities (the files stay). Baked mode stops the streamer.
> - Not yet: collision for the chunks (T7), hybrid mode (baked inside the extent, live beyond), progress / async bake,
>   materials.
>
> **T3 as built** (`src/editor/terrain_preview_3d`): the preview panel switches between 2D (map) and 3D. 3D: its own
> offscreen scene renderer (sky + sun). Like the map it follows the camera: a window of 10 x 10 chunks (64 x 64 quads)
> under the orbit point, sample spacing = a power of two from the camera distance (window ~3x the distance wide), so
> zooming out covers more ground. Chunks sit on the lattice of their spacing (shared edge vertices, one extra ring of
> samples for the normals), are evaluated on the thread pool (nearest first, at most 6 at a time) and published as
> `occasional` procedural meshes (no detail builds). Chunks that left the window stay until the window chunks over
> their area have meshes (no holes while zooming / panning); a chunk keeps its old mesh until the new state arrives.
> Shows the map's target when it is a heightfield, else the Surface. Camera: LMB orbit, MMB / RMB pan, wheel zoom, WASD
> + QE move, F frame the preview domain. The renderer is inactive while the 2D map is shown. Not yet: finer chunks near
> the camera (rings, the streamer of T4), click on the map to move the 3D camera, colouring by height.
>
> **Decided:** graph + C++ nodes; node canvas = VisualNodeSystem; one plugin with a conditional editor part; new terrains
> default to **live**; custom nodes live in node-pack plugins in the project directory (no dependency on scripting).

The terrain plugin turns a description of a landscape into geometry in a world. It must give the user control (code
where it matters), make the result explorable **before** it exists in the world, and keep generation managed: the user
never hand-places chunks, the plugin owns them.

The model is close to Unreal's PCG framework, adapted to this engine: a **graph** of typed operations, evaluated
**per region** (any rectangle of the world at any resolution), with **inspectable intermediate results**, and nodes that
are **plain C++ classes** you can write yourself.

## Answers in short

| Question | Proposal |
|---|---|
| Where does the terrain description live? | In an asset: a **terrain graph** (`.glt_terrain_graph`, a type the plugin reserves). Versioned, diffable, reusable by several worlds. The world only references it (world-scope `terrain` component: graph + seed + parameter overrides + extent). Hand-made data (sculpting, painted masks) lives in a second asset type, **terrain layer data**, referenced from the graph |
| How does the editor represent it? | Three places, each for one job: a **terrain editor window** (asset editor of the graph: node canvas, details panel, 2D map, 3D preview), **viewport tools** in the world viewport (sculpt / paint / stamp, for the hand-made layers), and the **world settings** of the `terrain` component (which graph, seed, overrides, live or baked) |
| How does the user author it? | Composition in the graph (edited with [VisualNodeSystem](https://github.com/Azzinoth/VisualNodeSystem)), logic in code. About 80 built-in nodes (see "Node library"). Anything else is a **C++ node** in a **node pack**: a small plugin in the project's directory, reloadable without restarting, independent of the unfinished scripting system. Quick formulas go into an **expression node**. Every node parameter is a reflected field, so code and UI never diverge |
| How is it previewed before the world renders it? | The terrain editor evaluates the graph **itself**, independent of the world: a 2D **map** of the whole domain (zoomable, tiles evaluated at screen resolution, any node's output on demand) and a 3D **preview** in its own offscreen scene renderer. Click a spot on the map to fly the 3D preview there. Only "Apply" puts it into a world |
| Like PCG? | Yes, in the parts that matter: typed data flowing through nodes, region-based deterministic evaluation, per-node debug views, partitioned generation around the player. Differences below |

## Prerequisite: an editor extension API

Today a custom plugin cannot add anything to the editor: windows, asset editors (`asset_editor_registry`) and the world
viewport are internal to the `editor` plugin, and there is no ImGui hook outside its layer. Components are the exception:
their descriptors carry a `draw` callback.

Proposal: a small interface in core, implemented by the editor plugin (`interface::editor_core`):

```cpp
// core/src/editor/i_editor.h
namespace GLT::editor {

    class i_editor_window {                                     // core-side twin of the editor's base_window
    public:
        virtual ~i_editor_window() = default;
        virtual std::string_view title() const = 0;
        virtual void draw(f32 delta_time) = 0;                  // inside the editor's ImGui frame, own Begin/End
        virtual void update(f32 delta_time) {}
        virtual bool wants_close() const { return false; }
        virtual undo::stack* undo_stack() { return nullptr; }   // optional: Ctrl+Z routing
    };

    class i_viewport_tool {                                      // modal tool of the world viewport
    public:
        virtual ~i_viewport_tool() = default;
        virtual std::string_view name() const = 0;              // toolbar button
        virtual void draw_options() {}                          // tool panel (brush size, strength, ...)
        virtual bool on_input(const viewport_input& input) = 0; // ray, cursor, buttons, modifiers; true = consumed
        virtual void draw_overlay(render::i_debug_draw& draw) {}// brush circle, previews
    };

    class i_editor {
    public:
        virtual void register_asset_editor(asset::type type, std::function<unique_ref<i_editor_window>(const std::filesystem::path&)>) = 0;
        virtual void register_menu_item(std::string_view path, std::function<void()> action) = 0;   // "Tools/Terrain/..."
        virtual void register_viewport_tool(unique_ref<i_viewport_tool> tool) = 0;
        virtual void open_window(unique_ref<i_editor_window> window) = 0;
        virtual void notify(std::string_view title, std::string_view text, logger::severity) = 0;
        virtual world::i_world* edited_world() = 0;
        virtual void unregister_all(std::string_view owner) = 0;  // plugin unload
    };

    [[nodiscard]] i_editor* get();                              // null without an editor (games)
}
```

It is useful to every future plugin, not only terrain. The editor wraps an `i_editor_window` in its `base_window` and
keeps layout, docking and undo routing as they are.

## Plugin shape

One plugin, `plugin/terrain`, target `interface::custom`, so it is enabled by hand in the plugin manager window.

- **Load phase** `application_ready` (the asset registry and the world plugin exist). Dependencies: `asset_registry`,
  `world`. The editor is an *optional* dependency: the editor part registers itself only if `editor::get()` is non-null,
  so the same plugin runs in a shipped game (generation and streaming only).
- **Public header** `plugin/terrain/include/terrain/node.h`: the node SDK node packs include to write their own nodes
  (same arrangement as `world_ecs_entt/include`).
- **Third party:** VisualNodeSystem (MIT) as a static library, editor part only (see "Node canvas"). The runtime part
  (evaluation, streaming, baking) has no UI dependency.
- On load: reserve the asset types `glt.terrain_graph` and `glt.terrain_layer_data`, register their handlers, register the
  world-scope `terrain` component, register built-in nodes; if an editor exists: the asset editor, viewport tools, menu.
- On unload: everything above in reverse, destroy every procedural mesh the plugin created.

If the runtime/editor split ever matters (static linkage of a shipping build), the editor folder becomes its own plugin
`terrain_editor` without other changes: it only talks to the runtime through the same public header.

## Data model

```
terrain_graph asset (.glt_terrain_graph)            terrain_layer_data asset (.glt_terrain_layer)
├─ CHUNK_GRAPH     (runtime truth, binary)           ├─ tile size, resolution
│   nodes [ { id, type name, params } ]              └─ sparse tiles: height deltas, mask weights
│   links [ { from node.pin -> to node.pin } ]           (written by viewport tools, read by layer nodes)
│   exposed parameters (name, type, default)
├─ CHUNK_EDITOR    (VisualNodeSystem JSON: node positions, reroutes, group comments, sub-areas, link nodes)
└─ CHUNK_PREVIEW   (preview domain, resolution, camera)

world entity: component terrain (world scope, saved with the world)
├─ graph            asset_ref -> terrain_graph
├─ seed             u64
├─ overrides        exposed parameter -> value
├─ extent           world-space rectangle (or unbounded)
├─ chunk size, resolution per LOD ring
└─ mode             live | baked     (+ baked asset folder)
```

The graph is **world-independent**: the same graph can drive several worlds with different seeds and overrides, and can
be previewed with no world open at all.

**Two chunks, one source.** In the editor, the VisualNodeSystem graph is what the user edits (with its reroutes, comments,
sub-areas and link nodes). On every change it is **compiled** into the flat `CHUNK_GRAPH`: reroutes become plain links,
sub-areas are inlined, link nodes become the connection they tunnel, comments disappear. The evaluator and a shipped game
read only `CHUNK_GRAPH`, so neither needs ImGui, VisualNodeSystem or jsoncpp. A graph without `CHUNK_EDITOR` (e.g.
written by a tool) opens with an automatic layout.

## Evaluation model

Everything follows from one rule: **a graph is evaluated for a region**, a world-space rectangle plus a sample spacing,
and the result is fully determined by (graph, seed, overrides, region). No hidden state, no evaluation order effects.

**Data types on the pins** (PCG's "spatial data"):

| Type | Content | Typical producers / consumers |
|---|---|---|
| `heightfield` | f32 grid over the region | noise, combine, erosion, layer data → surface output |
| `mask` | f32 0..1 grid (weights) | slope / height / curvature masks, painted masks → material output, scatter density |
| `points` | position, normal, scale, rotation, attributes | scatter / sample nodes → filters → spawn output |
| `scalar` / `vector` | constants, exposed parameters | everywhere |

**Region evaluation with aprons.** A node declares how far it looks around a sample (blur radius, erosion reach). The
evaluator enlarges the input region by the sum of the downstream aprons, so a chunk evaluated alone is identical, sample
for sample, to the same area of a bigger evaluation. That is what makes chunks seamless and the preview truthful.

**Global nodes.** Some operations cannot be local (hydraulic erosion over a whole island). They are marked `global`:
evaluated once over the whole extent at their own resolution, cached in the graph's derived data, and sampled by
everything downstream. The editor shows them with a badge and their cost.

**Execution.** The evaluator builds a task graph per request on `GLT::thread_pool`. Results are cached per
(node, region, input hash), so changing one parameter re-runs only that node and what depends on it. Requests carry a
priority (the map tile under the cursor first, distant chunks last) and are cancellable (the user moved on).

**Outputs** are nodes too:

| Output node | Result |
|---|---|
| `surface` | the terrain mesh: a heightfield sampled per chunk into a grid mesh (procedural mesh store). The plugin supplies the coarser LODs itself (`procedural_lod`) by sampling the same heightfield at 2×, 4×, ... spacing with locked chunk borders, so no simplifier runs and neighbours never crack |
| `material layers` | up to N masks → material per submesh in v1 (dominant layer per triangle), a splat material later |
| `spawn` | points → instances of mesh assets (rocks, trees): entities with `component::mesh`, or (later) instance batches |
| `collision` | heightfield → physics shape per chunk (triangle mesh in v1, Jolt height field later) |

## Authoring: graph, code, expressions

**The graph** is the composition layer: what feeds what, which seed, which parameters are exposed. It is what you look
at, inspect and tweak.

**C++ nodes** are the control layer. A node is a reflected class (`std::meta`, no registration boilerplate) in a
**node pack**: an ordinary custom plugin in the project's plugin directory (`<project>/plugins/terrain_nodes_<name>/`),
which the plugin wizard creates from a "Terrain node pack" template. It depends on the terrain plugin by name and
registers its nodes in `on_load()` with one line (`terrain::register_nodes<^^my_nodes>("my_pack")`, which walks the
namespace).

Why a plugin and not the scripting system: terrain nodes are **stateless pure functions** of their parameters and inputs.
Reloading one is trivial: unload the pack, load the rebuilt one, re-evaluate. No state migration, which is the hard part
of script hot reload, so this works now, without waiting for scripting. The terrain editor has a **Reload node packs**
button (plugin manager `unload_plugin` / `load_plugin` by name). Graphs reference node types by name; a node whose type is
missing (pack not loaded, renamed) shows as a red placeholder that keeps its parameters and links, so nothing is lost.
When scripting is finished, the same node classes can also live in the game's script library; the node API does not
change.

```cpp
#include <terrain/node.h>

namespace my_project::terrain_nodes {

    struct [[=terrain::node_info{ "Ridged Mountains", "Noise" }]] ridged_mountains : terrain::node {

        [[=terrain::input]]  terrain::heightfield_in  base;
        [[=terrain::output]] terrain::heightfield_out result;

        [[=terrain::param{ "Frequency", 0.0001f, 0.1f }]] f32 frequency = 0.004f;
        [[=terrain::param{ "Sharpness" }]]               f32 sharpness = 2.0f;

        // Called per region on a worker thread. ctx.position(x, y) is world space, ctx.seed is stable per node
        void evaluate(const terrain::context& ctx) override {
            ctx.for_each_sample(result, [&](glm::vec2 p, f32& out) {
                const f32 r = 1.0f - std::abs(terrain::noise::perlin(p * frequency, ctx.seed));
                out = base.sample(p) + std::pow(r, sharpness) * 120.0f;
            });
        }
    };
}
```

The plugin reflects pins and parameters into the canvas and the details panel, so the UI is always what the code says.
Nodes declare `apron()` and `is_global()` when they need them.

**Expression nodes** cover the many small cases where compiling is overkill: `out = a * smoothstep(0.3, 0.7, b) + c`,
with named inputs. A tiny expression compiler (parse once into bytecode, evaluate per sample) built into the plugin.

**Settings** are the third level: exposed graph parameters (sea level, mountain height, seed), overridable per world in
the `terrain` component without opening the graph.

## Node library

Built in, so most terrains need no code. **v1** = first release (phase T1/T2), the rest follows with the phase that needs
it. Every heightfield / mask node evaluates per region (see "Evaluation model"); `global` ones are marked.

**Generators** (heightfield)

| Node | v1 | Notes |
|---|---|---|
| Constant, Gradient (linear / radial) | ✓ | |
| Perlin, Simplex, Value noise | ✓ | frequency, amplitude, offset, per-node seed |
| fBm, Ridged multifractal, Billow, Swiss / Jordan turbulence | ✓ | octaves, lacunarity, gain |
| Voronoi / Worley (F1, F2, F2−F1, cell id, cell value) | ✓ | jitter, distance metric |
| Domain warp | ✓ | warps any input by two noise fields |
| White noise, Blue-noise dither | | |
| Shapes: cone / mountain, island falloff, crater, ridge line, plateau disc | ✓ | positioned in world space, for art-directed features |
| Heightmap from texture asset | ✓ | imports real-world data / external tools (World Machine, Gaea) |
| Spline ridge / valley | | needs spline data (T6) |

**Combine**

| Node | v1 | Notes |
|---|---|---|
| Add, Subtract, Multiply, Divide, Min, Max, Average | ✓ | n inputs where it makes sense |
| Blend by mask (lerp), Overlay, Screen, Difference | ✓ | |
| Layer stack | ✓ | ordered list of inputs with blend mode + mask each, like an image editor's layers |
| Select / threshold switch | ✓ | |

**Modify** (heightfield → heightfield)

| Node | v1 | Notes |
|---|---|---|
| Remap / normalize, Clamp, Invert, Abs, Power, Quantize | ✓ | |
| Curve (spline curve editor in the details panel) | ✓ | |
| Terrace (with shape / smoothness) | ✓ | |
| Transform (offset, scale, rotate, tile) | ✓ | |
| Blur (gaussian), Sharpen, Smooth (edge-preserving) | ✓ | apron = radius |
| Warp by field | ✓ | |
| Clamp slope, Plateau, Peak / valley sharpen | | |
| Thermal erosion | ✓ | iterative, apron grows with iterations |
| Hydraulic erosion (particle based) | | `global`, the biggest single feature after v1 |
| Sediment / deposition | | `global`, pairs with hydraulic erosion |

**Masks / analysis** (→ mask)

| Node | v1 | Notes |
|---|---|---|
| Height range, Slope range, Aspect (facing direction) | ✓ | soft edges |
| Curvature (convex / concave) | ✓ | |
| Noise mask, Threshold, Mask combine (and / or / not), Grow / shrink, Blur | ✓ | |
| Coast / water level, Distance to height | ✓ | |
| Flow accumulation, Wetness | | `global` |
| Painted mask layer | | reads `terrain_layer_data` (T6) |

**Hydrology**

| Node | v1 | Notes |
|---|---|---|
| Sea level (flatten below, output water mask) | ✓ | |
| Lake fill (depressions) | | `global` |
| River carve (from flow or spline) | | `global` |

**Points** (scatter, T7)

| Node | v1 | Notes |
|---|---|---|
| Scatter: Poisson disk, Grid, Random, Density by mask | | deterministic per region (cells hashed by world position) |
| Filter by mask / slope / height / random | | |
| Prune by distance, Difference (keep away from other points) | | |
| Transform: jitter, random scale / rotation, align to normal, snap to surface | | |
| Attribute set / random, Merge | | |

**Data / layers / utility**

| Node | v1 | Notes |
|---|---|---|
| Exposed parameter, Seed offset, World position (x / y as fields) | ✓ | |
| Expression | ✓ | `out = a * smoothstep(0.3, 0.7, b) + c` |
| Cache (force a node to be computed once over the extent, like a `global`) | ✓ | for expensive but local chains |
| Inspect (named debug output shown in the map's view list) | ✓ | |
| Sculpt layer, Paint layer | | `terrain_layer_data` (T6) |
| Sub-graph, Reroute, Comment / group, Link | ✓ | provided by VisualNodeSystem (sub-areas, reroute nodes, group comments, link nodes), flattened when compiling |

**Outputs**

| Node | v1 | Notes |
|---|---|---|
| Surface | ✓ | heightfield → chunk meshes with plugin-made LODs |
| Material layers | ✓ | masks → material per submesh (dominant layer) in v1 |
| Collision | | T7 |
| Spawn meshes | | T7 |

## Node canvas: VisualNodeSystem

[VisualNodeSystem](https://github.com/Azzinoth/VisualNodeSystem) (MIT) works with unmodified Dear ImGui and brings what a
terrain graph needs: zoom with readable text, reroute nodes, group comments, sub-area nodes (nested sub-graphs), link
nodes (connections between graph areas), JSON serialization, copy / paste, a node factory for custom node types, and
custom context menus.

Integration:

- **Build:** our fork, cloned to `vendor/visual_node_system` as a live checkout (`git_clone_or_update(... LIVE)`: only fast-forwarded, local work is never overwritten), built as its static library with
  `DEAR_IMGUI_INCLUDE_DIR` pointing at the engine's ImGui, linked only into the plugin's editor part. It carries jsoncpp and
  stduuid; on Linux it links X11.
- **One adapter type.** A single `vns_terrain_node` subclass of the library's node class represents *every* terrain node:
  it builds its sockets from the terrain node's reflected pins and stores the terrain node's type name and parameters in
  its JSON. The library's factory creates it from that type name, so new node types (built in or from node packs) need no
  editor code.
- **Selection** drives the details panel and the map's Inspect view; **socket types** (heightfield, mask, points, scalar)
  get colours and the library's connection check rejects mismatches.
- **Risk to check first (spike):** the library's zoom renders fonts at several sizes; that has to cooperate with the
  engine's ImGui version and font setup (the renderer uploads ImGui textures itself). If it does not, the fallback is
  zoom without font scaling, not a different library.

## Editor representation

### Terrain editor window (asset editor of `.glt_terrain_graph`)

```
┌ Terrain: islands.glt_terrain_graph ─────────────────────────────────────────────────────────────────────┐
│ [Seed 1234 ⟳] [Resolution: preview ▾] [Inspect: Erosion ▾]  ● evaluating 42 tiles      [Apply to world ▾]│
├──────────────────────────────┬──────────────────────────────────────────────┬────────────────────────────┤
│ Node canvas                  │ Map (2D)                                     │ Details                    │
│                              │                                              │                            │
│  [Perlin]──┐                 │   ░░▒▒▓▓██  whole domain, pan / zoom,        │ Ridged Mountains           │
│            ├─[Combine]──┐    │   ░▒▒▓▓███  tiles at screen resolution       │  Frequency  0.004  ──●──   │
│  [Ridged]──┘            │    │   ▒▓▓████▓  view: height | slope | node out  │  Sharpness  2.0    ──●──   │
│                [Erosion]┴─┐  │   chunk grid, points, cursor height readout  │                            │
│  [Slope mask]────────────┐│  │   click: move 3D preview camera here         │ Exposed parameters         │
│                    [Surface]  ├──────────────────────────────────────────────┤  sea_level   12.0         │
│                    [Spawn]    │ 3D preview (offscreen scene renderer)        │  mountains   1.0          │
│                              │   orbit / fly, preview-resolution chunks,    │                            │
│                              │   sky + sun, wireframe / LOD views           │ Stats                      │
│                              │                                              │  last eval 38 ms           │
└──────────────────────────────┴──────────────────────────────────────────────┴────────────────────────────┘
```

- **Inspect** (PCG's debug): select a node and the map shows *its* output (heightfield shaded, mask as heat map, points
  as dots). No need to rewire the graph to look at an intermediate step.
- **Map** evaluates tiles lazily at screen resolution, so the whole domain (tens of km) is visible instantly in coarse
  form and refines while zooming. This is the "explore without flying around" view.
- **3D preview** builds preview chunks around the preview camera at preview resolution into the procedural mesh store
  and draws them in its own offscreen scene renderer (the mesh editor's preview pattern). Nothing touches the world.
- **Apply to world**: bind the graph to the edited world (creates or updates its `terrain` component), then either let it
  stream live, or bake (below).

### Viewport tools (world viewport)

For the hand-made parts, where the 3D world is the right place: **Sculpt** (raise, lower, smooth, flatten, ramp),
**Paint mask** (for material layers and scatter density), **Stamp** (place a heightfield brush). They write into a
`terrain_layer_data` asset; a layer node in the graph reads it, so sculpting is just another input the graph composes.
The brush is drawn with debug draw; the ray hit is computed against the plugin's own heightfield (no physics needed).
Edits are undoable (the tool pushes undo steps through the editor API).

### World settings

The `terrain` component's inspector (world entity): graph, seed, exposed overrides, extent, chunk size, view distance,
live or baked, and buttons: "Open graph", "Regenerate", "Bake".

## Getting it into the world

| Mode | How | When |
|---|---|---|
| **Live** | A streamer in the plugin follows the world's streaming anchor (`i_world::get_streaming_anchor`), requests chunk evaluations by distance rings (fine near, coarse far), creates / updates / destroys procedural meshes and the entities that draw them (`set_procedural_mesh`). Same in editor and game | iterating, infinite worlds, runtime-varied seeds |
| **Baked** | Evaluate every chunk once, bake each into a static mesh asset (`procedural_mesh_store::bake`) into a folder, spawn entities that reference them; saved with the world | fixed, hand-sculpted terrain, shipping without generation cost |
| **Hybrid** | Baked inside the extent, live beyond it | a crafted map in an endless world |

Chunk entities are **owned** by the plugin: they live under one hidden parent entity, are not saved in live mode (the
streamer recreates them), and are locked in the outliner so the user does not edit them by accident.

## Differences from Unreal PCG

- Heightfields are first-class (PCG works on points and relies on Landscape for height).
- Nodes are C++ classes in node-pack plugins in your project, reloadable, instead of Blueprint.
- One graph type does height, masks and scatter; Unreal splits Landscape, Landscape layers and PCG.
- No partition actors: chunking is a property of evaluation (regions), not of the world's data.

## Implementation phases

| # | Phase | Done when |
|---|---|---|
| T0 | Editor extension API in core + implementation in the editor plugin. VisualNodeSystem spike (builds against the engine's ImGui, zoom fonts work) | A test plugin adds a menu item, a window and an asset editor; a VisualNodeSystem canvas runs inside it |
| T1 | Plugin skeleton (custom, manual enable), asset types + handlers, node SDK, evaluator (regions, aprons, cache, thread pool), the **v1 node library**, expression node | Unit test: two adjacent chunks equal the same area of one big evaluation, sample for sample, for every v1 node |
| T2 | Terrain editor window: VisualNodeSystem canvas + compile to `CHUNK_GRAPH`, details, 2D map with tiles and node inspection | Edit a graph, see the whole domain update while dragging a parameter |
| T3 | 3D preview (offscreen renderer, preview chunks) | Fly over the preview without a world open |
| T4 | `terrain` world component, live streamer (default mode), bake | Apply a graph to a world, walk around in play mode, bake it, reload |
| T5 | Node packs: plugin wizard template, `register_nodes`, reload button, missing-node placeholders | A node from `<project>/plugins/` appears in the palette and updates after a rebuild + reload, without restarting |
| T6 | Viewport tools: sculpt, paint, stamp → layer data assets; layer nodes | Sculpt a hill in the viewport, the graph picks it up, undo works |
| T7 | Points nodes + spawn output, material layers, collision, hydraulic erosion | Rocks on slopes, grass on flats, the player walks on it |

## Open decisions

1. **Terrain materials.** v1 assigns a material per submesh by dominant layer. A real splat / triplanar terrain material
   needs renderer work (weight textures per chunk); worth its own design once T7 is reached.
2. **Node pack location.** Proposed: `<project>/plugins/terrain_nodes_<name>/`, discovered like any project plugin. If the
   plugin manager only scans the executable's plugin directory, the wizard's existing project-plugin path decides.
