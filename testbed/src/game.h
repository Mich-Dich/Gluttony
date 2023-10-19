#pragma once

#include <core/defines.h>
#include <game_types.h>

typedef struct gameState {

    float deftaTime;

} gameState;


bool8 gameInitialize(game* gameInst);
bool8 gameUpdate(game* gameInst, float deltaTime);
bool8 gameRender(game* gameInst, float deltaTime);
void gameOnResize(game* gameInst, u32 width, u32 height);

