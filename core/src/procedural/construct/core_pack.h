#pragma once

#include "procedural/construct/registry.h"

// Procedural construction: the built-in "core" pack. Always active, registered through the same API as every other pack, so any
// part of it can be replaced by a pack that extends "core" (or wrapped / overridden through stages and policies).
//
//   stages      core.interpret -> core.expand -> core.realize -> core.finish -> core.output
//   passes      primary -> secondary -> detail (detail is LOD droppable)
//   schema      core.radius, core.hidden, core.group, core.uv, core.hard, core.material
//   modes       copy, interpolate, reduce_min / max / sum / avg, decay, boundary, drop
//   realizers   guides without a role become geometry: volumes / surfaces as their cage, curves as tubes (core.radius),
//               points as markers. Priority -1000: any pack realizer wins
//   policies    core.default_outputs: one "render" output, merged shells, everything



namespace GLT::construct {

    inline constexpr std::string_view                   CORE_PACK = "core";

    void register_core_pack(i_registry& registry, std::string_view owner);

}
