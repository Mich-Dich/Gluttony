#pragma once

#include "core/application.h"
#include "core/logger.h"
#include "game_types.h"

extern bool8 createGame(game* outGame);

// Main entry-point of application
int main(void) {
    
    // Try to Create the Game
    game gameInst;
    if(!createGame(&gameInst)) {

        GL_FATAL("Could not create game!");
        return -1;
    }

    // Make sure all function Pointers are set
    if(!gameInst.initialize || gameInst.update || gameInst.render || gameInst.onResize) {

        GL_FATAL("The game's function pointers musst be assigned!");
        return -2;
    }

    // Initialize
    if (!applicationCreate(&gameInst)) {
        
        GL_INFO("Application failed to create");
        return 1;
    }

    // Begin game loop
    if (!applicationRun()) {

        GL_INFO("Application did not shut down garacfully");
        return 2;
    }

    return 0;
}