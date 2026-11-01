# Gluttony

**A work-in-progress game engine built around a plugin-based, microkernel architecture — with a focus on procedural generation.**

<p align="center">
  <img src="https://raw.githubusercontent.com/wiki/mich-dich/gluttony/images/Screenshot_20261013_100900.png" alt="The Gluttony editor" width="49%">
  <img src="https://raw.githubusercontent.com/wiki/mich-dich/gluttony/images/Screenshot_20261007_200234.png" alt="Hardware ray-traced scene" width="49%">
</p>
<p align="center">
  <img src="https://raw.githubusercontent.com/wiki/mich-dich/gluttony/images/Screenshot_20261013_100806.png" alt="World outliner and region streaming" width="49%">
  <img src="https://raw.githubusercontent.com/wiki/mich-dich/gluttony/images/Screenshot_20261013_104354.png" alt="Debug Stats" width="49%">
</p>

Gluttony is an attempt to build a game engine from the ground up, where the core stays small and almost everything else, renderer / audio / physics / world / editor, is a plugin loaded at runtime. Subsystems are developed, swapped, and tested independently, and the engine itself never needs to know which implementation is behind any given interface.

It is not a game engine you can ship a game with yet. It is a foundation, the parts that need to be right *before* the parts that are fun

> **This is very much a work in progress.**
> Expect missing features, occasional jank, and interfaces that don't do much yet.

---

## The idea

Gluttony's focus is **procedural generation**. The idea that a set of rules can produce complex structure (geometry, textures, worlds, gameplay, even behaviour) and that a small, well-defined seed can unfold into a scene nobody has seen before. A generator and a renderer, neither of which knows the other exists, cooperating to produce something neither was written to describe.

Some of the systems planned for the engine:

- **Terrain generators**: continents, biomes, caves, rivers, and road networks derived from noise, erosion, and tectonic rules.
- **A ship hull creator**: a handful of parameters (length, beam, purpose, style) producing a game-ready hull with ribs, bulkheads, floors, and walkable interior volumes.
- **A grid-based build system**: *Space Engineers*, but blocks resize freely: a wall grows into a corridor, a corridor into an atrium, and the block adapts.
- **Dungeon, city, and level generators**: room graphs, road hierarchies, parcel subdivision, facade detail, encounter pacing, lighting, and narrative anchors, all driven by rulesets rather than hand-placement.
- **Procedural creatures and animations**: skeletal rigs, body segments, and locomotion assembled from a grammar; motion derived from simulation and control systems rather than authored keyframes.
- **Procedural materials, audio, UI, and gameplay**: node-graph textures generated at import time, synthesized impacts and ambience, layouts and theming derived from a style description, spawn tables and encounters that adapt to context.

Many of these are prime candidates for plugins. A terrain generator doesn't need to know how the renderer works. A ship hull creator doesn't need to know how the asset registry stores meshes. The microkernel architecture means each of these systems can live in its own plugin, evolve at its own pace, and be swapped or replaced without touching the engine core — or each other.

---

## The name

The name is an internal joke about ambition. Gluttony is meant to be an engine that *eats* features — it consumes subsystems, ideas, and experiments. The goal is to keep feeding it: rendering techniques, generation systems, tooling, whatever a future game might need, until the list of things it can do is genuinely absurd. The name is the *aspiration*, not a diagnosis.

It's also a reminder that an engine that swallows everything has to digest it efficiently — that a project can be ambitious *and* fast, as long as the seams are designed with care.

---

## The engine today

The engine is early, but it already renders, plays audio, streams worlds, and provides a workable editor. There is a Vulkan renderer, a Jolt-backed physics plugin, a chunked asset registry with hot-reload, a multi-window editor with a content browser and world outliner, and a full plugin lifecycle with phase-based loading and dependency resolution.

For a full, honest breakdown of what exists today — and what's still in design — see the [What Gluttony is now](https://github.com/mich-dich/gluttony/wiki#what-gluttony-is-now) section of the wiki home page.

---

## The road ahead

The roadmap is long and deliberately ambitious. A high-level teaser:

- **Procedural content**: the generators above: terrain, ships, dungeons, cities, creatures, materials, audio, UI, and gameplay.
- **Rendering**: Forward+ completion, LOD, post-processing, particles and VFX, sky and atmosphere, and a portable ray tracer alongside the rasterizer.
- **Gameplay**: scripting, animations, runtime UI, save/load, navigation, prefabs, and networking.
- **Tooling**: undo/redo, asset cooking and standalone packaging, and an in-game console.
- **Infrastructure**: a memory manager, an asset migration framework, plugin ABI versioning, and tests/CI.

The full, living list — what's in progress, what's deferred, and what's blocked on what — lives on the [Roadmap](https://github.com/mich-dich/gluttony/wiki/Roadmap) page.

---

## Design principles

Every subsystem in Gluttony follows the same set of rules. They exist because the alternative — subsystems that know about each other — is what kills a plugin architecture.

1. **The plugin owns the implementation. The core owns the handles.** Assets, entities, and bodies are POD handles. Nothing outside a plugin ever sees a backend pointer, a `vk::Buffer`, a `b2Body*`, or an `entt::entity`.
2. **Data that crosses a boundary is trivially copyable.** No `std::function`, no virtual members, no owning pointers. This is what lets the reflection system serialize it, the profiler inspect it, and the debugger read it without a type registry.
3. **Per-frame state is pulled, not pushed.** Plugins fill caller-owned vectors and spans. They never write into engine-owned memory, never invoke gameplay callbacks from inside their own update loop, and never have to know which entities care about their results.
4. **Optional capabilities are side-interfaces.** The base `i_world_plugin` knows nothing about inspectors or scene gathering. Each optional capability is a separate interface discovered via `as<T>()`.
5. **Fixed internals, variable façades.** The renderer decides its own swapchain policy. The physics plugin decides its own fixed timestep. The engine calls `step(dt)`, `begin_frame()`, `update(dt)` and does not care what happens inside.

A subsystem that violates one of these principles is a subsystem that will be rewritten. The contracts exist to make the principles concrete for each boundary.

---

## Getting started

Prerequisites, toolchain setup, and build instructions live on the wiki:

- **[Building Gluttony](https://github.com/mich-dich/gluttony/wiki/Building)**: Linux primary target, CMake ≥ 3.20, a C++26 compiler with experimental reflection, and Vulkan SDK 1.4.357.x. (Windows is partial; macOS is unsupported.)
- **[Architecture Overview](https://github.com/mich-dich/gluttony/wiki/Architecture-Overview)**: the microkernel model, plugin lifecycle, phases, and interfaces.
- **[Plugin Authoring](https://github.com/mich-dich/gluttony/wiki/Plugin-Authoring)**: descriptors, phases, dependency resolution, and how to write a plugin.

---

## Documentation

The [GitHub Wiki](https://github.com/mich-dich/gluttony/wiki) is the documentation home for Gluttony: design documents, user guides, the roadmap, showcases, and whatever else accumulates as the engine grows.

- [Showcases](https://github.com/mich-dich/gluttony/wiki/Showcases) — screenshots, demos, and small projects built with Gluttony.
- [Design documents](https://github.com/mich-dich/gluttony/wiki#design-documents) — contracts and design notes for each subsystem.
- [Roadmap](https://github.com/mich-dich/gluttony/wiki/Roadmap) — what's planned, in progress, and deferred.
- [Glossary](https://github.com/mich-dich/gluttony/wiki/Glossary) — engine-specific terms and their meanings.
- [FAQ](https://github.com/mich-dich/gluttony/wiki/FAQ) — common questions about the engine and its design.

---

## Contributing

This is a personal project, and I can't promise prompt responses — but issues and PRs are welcome. If you find a bug, or if you want to add a subsystem, open an issue first: the design documents exist so that changes to a subsystem can be discussed before they are written.

Before submitting a plugin, read the [Plugin Authoring](https://github.com/mich-dich/gluttony/wiki/Plugin-Authoring) page and the design document for the interface you're implementing. Before proposing a change to an existing design document, explain *why* the current design is wrong — the reasoning is often the point.

---

## Quick links

- [GitHub repository](https://github.com/mich-dich/gluttony)
- [Building the engine](https://github.com/mich-dich/gluttony/wiki/Building)
- [Architecture overview](https://github.com/mich-dich/gluttony/wiki/Architecture-Overview)
- [Plugin authoring guide](https://github.com/mich-dich/gluttony/wiki/Plugin-Authoring)
- [Roadmap](https://github.com/mich-dich/gluttony/wiki/Roadmap)
- [Glossary](https://github.com/mich-dich/gluttony/wiki/Glossary)
