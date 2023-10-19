#include "application.h"
#include "game_types.h"
#include "logger.h"
#include "platform/platform.h"

typedef struct appState {

    game* gameInst;
    bool8 isRunning;
    bool8 isSuspended;
    platformState platform;
    int16 width;
    int16 height;
    double lastTime;

} appState;

static bool8 initialized = FALSE;
static appState AppState;

bool8 applicationCreate(game* gameInst) {

    // Savegard abainst mulltiple Creates
    if (initialized) {
        GL_ERROR("applicationCate() called more than once");
        return FALSE;
    }

    AppState.gameInst = gameInst;

    // Initialize subsystems
    Init_Log();

    // Test LoggingSystem
    GL_TRACE("Test message: %f", 3.1415f);
    GL_DEBUG("Test message: %f", 3.14f);
    GL_INFO("Test message: %.3f", 3.1415f);
    GL_WARNING("Test message: %.2f", 3.1415f);
    GL_ERROR("Test message: %.1f", 3.1415f);
    GL_FATAL("Test message: %.0f", 3.1415f);

    AppState.isRunning = TRUE;
    AppState.isSuspended = FALSE;

    // 
    if (!platform_Startup(
        &AppState.platform,
        gameInst->AppConfic.name, 
        gameInst->AppConfic.statPosX, 
        gameInst->AppConfic.statPosY, 
        gameInst->AppConfic.statWidth, 
        gameInst->AppConfic.statHeight)) 
            return FALSE;
    
    if (!AppState.gameInst->initialize(AppState.gameInst)) {

        GL_FATAL("Game Failed to Initialize");
        return FALSE;
    }

    AppState.gameInst->onResize(AppState.gameInst, AppState.width, AppState.height);

    initialized = TRUE;
    return TRUE;
}

bool8 applicationRun() {

    // Main game loop
    while(AppState.isRunning) {

        if (!platform_Push_messages(&AppState.platform)) 
            AppState.isRunning = FALSE;

        if (!AppState.isSuspended) {
            
            // Call games update routine
            if(!AppState.gameInst->update(AppState.gameInst, (double)0)) {

                // Handle errors
                GL_FATAL("Game update failed, Shutting down");
                AppState.isRunning = FALSE;
                break;
            }
            
            // Call games render routine
            if(!AppState.gameInst->render(AppState.gameInst, (double)0)) {

                // Handle errors
                GL_FATAL("Game render failed, Shutting down");
                AppState.isRunning = FALSE;
                break;
            }
        }
        
    }

    // make sure [isRunning] is set to false. 
    AppState.isRunning = FALSE;

    // Shutdown game loop
    platform_Shutdown(&AppState.platform);
    return TRUE;
} 