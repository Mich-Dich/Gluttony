#include "game.h"
#include <entry_point.h>

// TODO: Remove This
#include <platform/platform.h>

// define func to create game
bool8 CreateGame(game* outGame) {
    

    appConfic confic;
    outGame->AppConfic.statPosX = 100;
    outGame->AppConfic.statPosY = 100;
    outGame->AppConfic.statWidth = 1280;
    outGame->AppConfic.statHeight = 720;
    outGame->AppConfic.name = "Gluttony Test Window";
    outGame->initialize = gameInitialize;
    outGame->update = gameUpdate;
    outGame->render = gameRender;
    outGame->onResize = gameOnResize;

    outGame->state = platform_allocate_memory(sizeof(gameState), FALSE);

    return TRUE;
}