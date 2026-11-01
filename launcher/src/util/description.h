
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::launcher {

    // CONSTANTS =======================================================================================================

    static const std::string                        general_gluttony_description = R"(# Welcome to Gluttony

**A procedurally-focused game engine built on a microkernel architecture.**  

The core is small. Almost everything else — renderer, audio, physics, world, editor — is a plugin loaded at runtime. Subsystems are developed, swapped, and tested independently, and the engine never needs to know which implementation is behind any given interface.
This is a work in progress. It is not an engine you can ship a game with yet. It is a foundation — the parts that need to be right *before* the parts that are fun.

## The idea
Gluttony is built around **procedural generation**: the idea that a set of rules can produce complex structure — geometry, textures, worlds, gameplay, even behaviour — and that a small, well-defined seed can unfold into a scene nobody has seen before.
A generator and a renderer, neither of which knows the other exists, cooperating to produce something neither was written to describe.

## What works today

- Core application lifecycle, plugin manager, event bus, and thread pool
- Phase-based plugin loading with dependency resolution
- Chunked, versioned asset registry with hot-reload and async loading
- EnTT-backed world plugin with region streaming
- GLFW window plugin with input and Vulkan surface creation
- Vulkan hardware ray-traced renderer
- SoLoud + miniaudio audio backend
- A substantial ImGui editor: content browser, world outliner, material editor, log viewer, stats, viewport

## Design principles

Every subsystem follows the same rules, because the alternative — subsystems that know about each other — is what kills a plugin architecture.
- **The plugin owns the implementation. The core owns the handles.**
- **Data that crosses a boundary is trivially copyable.**
- **Per-frame state is pulled, not pushed.**
- **Optional capabilities are side-interfaces.**
- **Fixed internals, variable facades.**

## Getting started

- **Building the engine** — prerequisites, toolchain, and build instructions
- **Architecture overview** — the microkernel model, plugin lifecycle, and interfaces
- **Plugin authoring** — descriptors, phases, and dependency resolution
- **Roadmap** — what is planned, in progress, and deferred

## A note on the name

Gluttony is meant to be an engine that *eats* features — it consumes subsystems, ideas, and experiments. The goal is to keep feeding it until the list of things it can do is genuinely absurd. The name is the aspiration, not a diagnosis.
An engine that swallows everything has to digest it efficiently. Ambitious *and* fast — as long as the seams are designed with care.

*Expect missing features, occasional jank, and interfaces that don't do much yet. That is the point.*
)";

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
