#pragma once

#include "core/application.h"

typedef struct game {

    appConfic AppConfic;
    bool8 (*initialize)(struct game* gameInst);
    bool8 (*update)(struct game* gameInst, float deltaTime);
    bool8 (*render)(struct game* gameInst, float deltaTime);
    void (*onResize)(struct game* gameInst, u32 width, u32 height);
    void* state;
    
} game;