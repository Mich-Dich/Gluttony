# Virtual Geometry

Virtual geometry draws a static mesh as a set of small triangle clusters whose level of detail is chosen **per cluster
and per frame** on the GPU, instead of switching the whole mesh between a handful of LOD levels. A dense mesh then
costs roughly the triangles that are visible at their size on screen: the part next to the camera is drawn at full
detail, the far side of the same object coarsely, and clusters outside the view or facing away are not drawn at all.

A static mesh uses **one of two detail modes**: discrete LODs (see [level_of_detail.md](level_of_detail.md)) or virtual
geometry (`asset::mesh::detail_mode`, `mesh_asset::detail()`). The mode is chosen per mesh, on import or in the mesh
editor. In virtual geometry mode the discrete levels are no longer authored; they stay in the asset as a fallback for
shadows, translucent submeshes and frames or renderers without the cluster path. A mesh in discrete LOD mode carries
no cluster data.

## How it fits together

| Piece | Where | What it does |
|---|---|---|
| Data types | `core/src/asset/mesh.h` | `detail_mode`, `cluster_settings` (authored, incl. `cluster_normals_mode`), `cluster` / `cluster_submesh` / `cluster_vertices` (generated), chunks `0x000D`–`0x0011`, `mesh_asset::detail()` / `uses_virtual_geometry()` |
| Builder | `core/src/asset/mesh_cluster.{h,cpp}` | `build_clusters`, `generate_clusters`, `set_detail_mode`, `validate_clusters`, the CPU twin of the selection rule (`cluster_in_cut`, `measure_cut`, `collect_cut`) and of the debug colours. Uses meshoptimizer's `clusterlod` |
| Import | `plugin/asset_factory_mesh_assimp` | Import option "Virtual geometry" (default off) puts the mesh in virtual geometry mode and builds the clusters |
| Asset I/O | `plugin/asset_handler_mesh` | Reads and writes the five optional chunks; cluster data only for meshes in virtual geometry mode. Data that fails `validate_clusters` is dropped: the mesh renders with its LODs |
| GPU data | `shared_resource_manager` | Cluster triangle lists behind the mesh's indices in the shared index buffer, cluster vertices behind the mesh's (and its LODs') vertices in the shared vertex buffer, cluster records in a growable pool, one table entry per mesh |
| GPU pass | `scene/scene_renderer_virtual_geometry.inl`, `virtual_geometry_cull.comp.glsl` | Selects and culls clusters, writes one indexed indirect draw per visible cluster (plus, per command, the index of its cluster for the debug views) |
| Debug views | `scene/scene_renderer_virtual_geometry_debug.inl`, `virtual_geometry_debug.vert/.frag`, `virtual_geometry_debug_triangles.frag` | Render modes of the category "Virtual Geometry": one pass that redraws the frame's commands coloured per cluster, level, triangle or error |
| Editor | `plugin/editor/src/window/mesh_editor.*` | "Geometry Detail" mode switch, then only the active mode's cards; for virtual geometry: build, statistics, settings, fallback, debug views |
| Rendering analysis | `plugin/editor/src/window/mesh_editor_analysis.cpp` | Second view of the mesh editor's left panel ("Outliner / Rendering"): recommendation, numbers of the current view, data collected while looking at the mesh (saved per asset UUID), graphs over distance, memory |
| Frame statistics | `core/src/render/i_scene_renderer.h` | `frame_stats` / `i_scene_renderer::last_frame_stats()`: GPU time, draws and triangles of the newest measured frame of one scene renderer, with the camera it was rendered from |

## The cluster hierarchy

`build_clusters` works per submesh, so a cluster always has exactly one material.

1. The submesh is split into clusters of up to `cluster_triangles` triangles (default 128).
2. Neighbouring clusters are put into groups of about 16. Each group is simplified to half its triangles with the
   **outline of the group locked**, and the result is split into new clusters.
3. Step 2 repeats on the new clusters until nothing can be merged any more.

Because a group's outline never moves, the coarse clusters made from a group fit exactly against whatever is drawn
next to them, at any level. The result is a DAG: every cluster knows the error of the group it was made from
(`lod_error`, 0 for the source triangles) and the error of the group that replaces it with coarser clusters
(`parent_error`, `CLUSTER_ERROR_NONE` at the top). Errors are in mesh units and only grow towards the root.

Simplification only removes triangles: every corner of a simplified triangle sits on a source vertex position. The
hierarchy costs about twice the source index data plus 80 bytes per cluster, plus the vertices the normals method
adds (next section).

`Lock material seams` (default on) keeps the vertices that several submeshes share in place. Submeshes are simplified
independently, so without the lock their common border moves differently on both sides and cracks open between
materials. The price is a higher triangle floor along those seams at a distance.

## Normals of simplified clusters

A simplified triangle spans what used to be many small ones, so the normals of the source vertices it ends on belong
to a finer surface. Where the simplifier collapsed across a hard edge, a face even ends on a vertex that carries the
neighbouring face's normal. Shaded with those normals, coarse clusters look blotchy. `cluster_settings` therefore has
a normals method (`cluster_normals_mode`, flags bits 4..5; *Cluster Build › Normals* in the mesh editor):

| Method | Normals of the simplified clusters | Extra vertices |
|---|---|---|
| *Keep source* | The source vertices' normals. The behaviour before the methods existed | none |
| *Flat* | One normal per face | most: about one per corner |
| *Auto smooth* (default) | Recomputed from the simplified faces, smooth across edges bent less than `smooth_angle` (45°), hard above | medium |
| *Smooth* | Recomputed, smooth everywhere | medium |

Level 0 clusters (the source triangles) always use the mesh's own vertices and normals.

The recomputed methods give simplified clusters **vertices of their own**: copies of source vertices with a new normal
and the tangent made orthogonal to it again (`mesh_asset::cluster_vertices`, chunk `0x0011`). Cluster indices address
`[vertices ++ cluster_vertices]` (`mesh_asset::cluster_vertex_at`). A recomputed normal within 1° of the one the
vertex already has reuses that vertex. How many are added depends on the surface: none for planar faces, on the
test meshes typically 50–75 % of the source vertex count with the default (56 bytes each), up to about as many as
the source has on finely bumpy surfaces; *Flat* several times that. The mesh editor shows the count ("Extra
vertices") and the memory.

Clusters of different levels are drawn next to each other, so their normals have to agree where they meet. The rule:
when a group is simplified, positions on the group's outline (shared with another group of that level, or locked as a
material seam) **inherit** the vertex, and with it the normal, that the group's input clusters used there. Only
positions inside the group get recomputed normals. By induction every position has one consistent set of normals in
every selection the renderer can make, so shading is continuous across cluster and level borders.

Assets saved before the methods existed (`cluster_settings::version` 0) load as *Keep source* if they are in virtual
geometry mode, so they look as they did until a method is chosen and the clusters are rebuilt. Everything else gets
the default.

## Selection rule

For every cluster of every instance on the path, each frame:

```
projected(sphere, error) = error * scale * error_scale * (|P11| / 2) / max(distance(sphere centre, camera) - radius, near)

draw  <=>  projected(lod_bounds, lod_error) <= threshold   and   projected(parent_bounds, parent_error) > threshold
threshold = pixel error / viewport height
```

A cluster is drawn when it is detailed enough itself while its coarser replacement is not. The clusters made from a
group and the clusters that group replaces evaluate the same sphere and error, so for any camera exactly one version of
every piece of surface is drawn: no holes, no overlaps, no cracks between clusters of different levels. The shader marks
the expression `precise` so both sides get bit-identical results.

Selected clusters are then culled against the frustum (their own bounding sphere) and by their backface cone. Cone
culling is skipped for instances with non-uniform scale, shear or mirroring and for double-sided materials.

## Renderer integration

The path is separate from the instance cull on purpose: its own work list, command ranges, buffers and pass. A frame
without virtual geometry instances records exactly what it recorded before.

- **CPU, `build_instance_buffer`:** an instance goes on the path when the renderer setting is on, its mesh is in
  virtual geometry mode with built clusters, and no LOD is forced. It gets `INSTANCE_FLAG_VIRTUAL`, is listed as work items of 64 clusters, and each of
  its opaque submeshes reserves `cluster_submesh::max_visible` commands in its material's cluster command range.
  `max_visible` is an upper bound of clusters that can be selected at once, computed at build time.
- **GPU, `virtual_geometry_cull`:** one workgroup per work item, one thread per cluster. Runs between
  `reset_draw_count` and `cull`. `cull.comp` skips flagged instances in the main view.
- **Draws:** the depth pre-pass, the scene pass and the triangle density pass issue one extra
  `drawIndexedIndirectCount` per material over the cluster commands, with the same pipelines, vertex and index buffer.
- **Vertices:** a mesh is uploaded as `[vertices ++ lod_vertices ++ cluster_vertices]`. Cluster indices that address
  cluster vertices are shifted by the LOD vertex count at upload, so every draw of the mesh uses one vertex offset.
- **Shadows:** unchanged. Shadow views draw the discrete levels of every mesh, so the shadow cache is not affected.
- **Translucent submeshes** of a virtual geometry mesh keep going through the sorted translucent pass with the LODs.
- **Budget:** a frame reserves at most `MAX_VIRTUAL_DRAW_COMMANDS` (about one million, 20 bytes each per frame in
  flight) cluster commands. Instances beyond that stay on the LOD path for that frame.
- **Fail-safe:** if the shader or pipeline cannot be created, the path is off and every mesh uses its LODs.

Settings (renderer, "Virtual Geometry"): on/off, pixel error (default 1), backface cluster culling.

## Debug views

Four render modes in the category **Virtual Geometry**. They are ordinary render modes, so every viewport offers them
(render mode menu of the world viewport and of the mesh editor preview):

| Mode | Shows |
|---|---|
| *Geometry Clusters* | Every drawn cluster in its own colour (stable per cluster of a mesh) |
| *Cluster Levels* | The hierarchy level of each drawn cluster: green = source triangles, then yellow, orange, red, magenta, purple, blue, cyan (about half the triangles per level) |
| *Triangles* | Every drawn triangle in its own colour, for all meshes. Needs the `geometryShader` device feature (`gl_PrimitiveID`); without it the view shows cluster colours |
| *Screen Error* | The simplification error of each drawn cluster on screen as a share of the allowed pixel error: dark blue = source triangles, blue → green → yellow → red = 0 → 100 % |

Meshes drawn with discrete LODs are dark grey and procedural meshes light grey (except in *Triangles*).

How it works: while one of the modes is active the lit path still runs (the material shaders draw flat grey), and the
pass `virtual_geometry_debug` at the end replaces the image. It redraws exactly the commands the lit path drew
(culled LOD draws, cluster draws, translucents, procedural meshes) with one pipeline and clears and rewrites the
depth buffer, so debug lines drawn afterwards are tested against what the view shows. A cluster draw finds its
cluster through `gl_DrawID`: `virtual_geometry_cull.comp` writes, parallel to its commands, the index of the cluster
each command draws (binding 11), and the pass pushes the first command of each indirect call. What is not shown:
alpha-masked materials are drawn solid, and back faces of single-sided materials are visible where nothing is in
front of them.

Other modes: *Level of Detail* colours clusters by hierarchy level too (in the LOD palette). *Triangle Density*
includes the cluster draws. The GPU pass list shows the clusters and triangles drawn per target.

The mesh editor adds two overlays that it draws itself from the CPU twin of the selection (no frustum or backface
culling): *Cluster bounds* (the culling sphere of every selected cluster, in the colour it has in *Geometry Clusters*)
and *Wireframe* (the edges of the selected clusters' triangles).

## Using it

- **Import:** tick "Virtual geometry" in the import options, or
- **Mesh editor:** inspector → *Geometry Detail* → *Mode* → *Virtual Geometry*. The clusters are built in the background
  (roughly 0.2 s per 100k triangles in a release build). *Save* writes them with the asset. Switching back to
  *Discrete LODs* removes the clusters.
- The inspector then shows the virtual geometry cards instead of the LOD cards: *Virtual Geometry* (statistics,
  rebuild), *Cluster Detail* (detail scale, cull size: apply immediately), *Cluster Build* (normals method, cluster
  size, seam lock, simplifier weights: need *Rebuild clusters*), *Fallback Levels* (the discrete levels, read-only;
  generated with the mesh's LOD settings) and *Debug Views*.
- The preview shows what the camera selects ("Virtual geometry · N clusters · M tris"). Locking the preview to a
  fallback level (outliner or the pill in the preview) shows that discrete level instead.

It pays off for meshes with many triangles that are seen at varying distances or only partly. A mesh that fits into a
few clusters gains nothing over its LODs. The next section is the tool that answers this per mesh.

## Rendering analysis in the mesh editor

The left panel of the mesh editor has two views, switched at its top: **Outliner** (mesh, submeshes, colliders,
levels) and **Rendering**. The inspector on the right always shows the properties of the selection, so a setting can
be changed while its effect is in view. Each view keeps its own panel width (Rendering defaults to 400 px).

*Rendering* tells what the mesh costs to draw and which of the two detail systems suits it:

| Card | Content |
|---|---|
| *Recommendation* | Keep the mode or switch (with a button), the reasons, what the verdict is based on, and other findings: no fallback / LOD levels, *Keep source* normals, a high triangle floor far away, triangles smaller than two pixels in the current view, very many draw commands |
| *This View* | What the preview draws right now: system and level, distance and screen size, triangles and draw commands after culling, pixels per triangle, GPU time, and what the other system would draw from here |
| *Collected Data* | Which distances and view directions have been looked at, what is missing ("zoom out: nothing collected beyond ..."), *Measure distances*, *Reset data* |
| *Over Distance* | Graphs with a log distance axis: triangles and draw commands (lines: what each system selects; dots: measured after culling) and the measured GPU time of the geometry passes. Hover for the numbers of a distance range |
| *Memory* | Source mesh, levels, clusters, and the total of each system |

**Where the numbers come from.** While the mesh editor is open, every update reads the preview renderer's
`last_frame_stats()` and files the frame under the system that drew it (a frame with cluster draws went through the
virtual geometry path) and under the size the mesh had on screen in it (half-octave ranges of the screen size both
systems select by). The statistics carry the camera position they were rendered from, so the few frames GPU results
lag do not matter. Debug render modes, a preview locked to a level LOD mode would not pick, and the frames right
after such a change are left out. Samples are dropped when what they describe changes: rebuilt clusters, regenerated
levels, a resized preview. The lines of the graphs are computed on the CPU with the rules the renderer applies
(`auto_lod_for`, `measure_cut`), without frustum and backface culling.

**The verdict** uses, in this order: measured GPU time of the geometry passes where both systems were measured at the
same distances (differences under 10 % are a tie; if one system wins close up and the other far away, it says so
instead of recommending a switch); the computed triangle counts averaged over the distance ranges; rules of thumb by
triangle count (under 10k: discrete LODs, over 250k: virtual geometry).

**Measuring both systems.** A mesh is in one mode at a time. *Measure distances* flies the preview camera in one
smooth zoom from close up to far away: an even pace on the log distance axis, half a second per distance range (half an
octave), about 8 s for a typical mesh. *Auto rotate* is switched on for the run (at least 0.12 turns per second, faster
if the preview's own speed is higher) and the camera height swings slowly up and down, so all sides get sampled. For a
virtual geometry mesh with fallback levels it then zooms back in drawing the level LOD mode would pick at each distance,
which measures both systems in one go. While it runs, the *Collected Data* card is inactive and shows progress and a
**Stop** button over it, and the preview ignores camera input. It ends on its own and puts the camera, the auto rotate
setting and the preview's level lock back. A mesh in LOD mode has no clusters:
*Estimate virtual geometry* builds them in the background for the panel alone (nothing is saved or drawn), which
gives triangle counts, draw commands and memory. Its GPU time can only be measured after switching the mode; the
samples of the other mode survive the switch.

**The reminder.** One view says little about a mesh, so the panel counts the distance ranges and the view directions
(8 sectors around the mesh × below / level / above) seen so far and asks for the missing ones. Advice that rests on
few views is marked as such.

**Saved per asset.** The collected samples are written to `<project>/metadata/asset_stats/<asset UUID>.yml` (see
[asset_identity.md](asset_identity.md)): when the mesh closes, when a measurement ends, after *Reset data*, and at the
latest 10 s after new samples. They are loaded when the mesh opens again; samples that no longer fit the mesh (other
geometry, rebuilt clusters, regenerated levels, a different preview size) are dropped by the same checks as during a
session. Written and read with `GLT::serializer::yaml` (section `render_samples`: version, asset, geometry signature,
preview size, view directions, one record per series and per distance range); safe to delete. Timings are specific to
the machine that measured them.

**Cost in the editor.** The panel works from caches: the cluster selection of the preview camera (shared by the
statistics, the preview overlay, the wireframe, the cluster bounds and this panel) is recomputed when the view or the
clusters change, at most 10 times a second; the mesh bounds and the hierarchy statistics once per geometry change; the
"selects" curves once per data change, two distances per frame. Profile zones `mesh_editor: ...` show the rest.

What it cannot tell: the timings are from one instance in an otherwise empty preview scene. They compare the two
systems for this mesh; they are not the cost of the mesh in a level with thousands of instances, other materials and
shadows from many lights.

## Limitations and future upgrades

1. **Shadows from clusters.** Shadow maps still draw the discrete levels (LOD 0 unless forced). Running the cluster
   pass per shadow view, with the error measured from the light, would cut their triangle counts the same way.
2. **Occlusion culling.** Clusters hidden behind other geometry are still drawn. A hierarchical depth buffer from the
   previous frame, tested per cluster in the same pass, is the natural next step.
3. **Hierarchical selection.** Every cluster of every instance on the path is tested each frame. `clusterlod` can
   build a BVH over the groups (`clodBuildHierarchy`) so whole subtrees are skipped; worth it for very many instances.
   It would also give exact command counts and remove the fixed draw budget.
4. **Seams between materials.** Building one hierarchy across all submeshes and splitting clusters by material
   afterwards would simplify seams consistently on both sides instead of locking them.
5. **One draw per cluster.** Each visible cluster is one indirect draw. A mesh-shader path, or expanding the visible
   clusters into one index buffer per material, would lower the per-draw cost.
6. **Index memory.** The level 0 clusters repeat the triangles of LOD 0. Reordering LOD 0 into cluster order would let
   both share one index range.
7. **Vertex memory of the normals methods.** Recomputed normals cost extra vertices (see above). Storing only a
   packed normal + tangent per extra vertex and fetching position and UV from the source vertex would cut that to a
   fraction, at the price of a second vertex stream.
8. **Streaming.** All clusters of a resident mesh are resident. Loading coarse levels first and fine ones on demand
   needs per-level chunks and residency tracking on the GPU.
9. **Skeletal meshes** are not handled, as with the discrete LODs.
10. **Rendering analysis measures one instance.** A sweep with many instances of the mesh, or sampling in the world
    viewport, would show how the two systems scale.
