#pragma once

#include "defines.h"

struct game;

typedef struct appConfic {

    int16 statPosX;
    int16 statPosY;
    int16 statWidth;
    int16 statHeight;
    char* name;

} appConfic;

GLUTTONY_API bool8 applicationCreate(struct game* gameInst);
GLUTTONY_API bool8 applicationRun();