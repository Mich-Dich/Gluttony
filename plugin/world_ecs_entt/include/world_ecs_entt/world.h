
#pragma once



// Umbrella include. The old monolithic ecs_world_plugin is gone:
//   - ecs_world_plugin.h  → thin factory (plugin lifecycle, codec, asset handler)
//   - ecs_world.h         → per-instance world implementation
#include "ecs_world.h"
#include "ecs_world_plugin.h"
