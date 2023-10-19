#include <core/logger.h>
#include <core/asserts.h>

// TODO: Test
#include <platform/platform.h>

int main(void) {
    
    GL_TRACE("Test message: %f", 3.1415f);
    GL_DEBUG("Test message: %f", 3.14f);
    GL_INFO("Test message: %.3f", 3.1415f);
    GL_WARNING("Test message: %.2f", 3.1415f);
    GL_ERROR("Test message: %.1f", 3.1415f);
    GL_FATAL("Test message: %.0f", 3.1415f);

    platformState state;
    if (platform_Startup(&state, "Gluttony Test", 100, 100, 1280, 720)) {

        while(TRUE) {
            platform_Push_messages(&state);
        }
    }

    platform_Shutdown(&state);

    return 0;
}