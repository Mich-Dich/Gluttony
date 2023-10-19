#pragma once

#include "core/defines.h"

typedef struct platformState {

    void* internalState;
    
} platformState;

GLUTTONY_API bool8 platform_Startup(
    platformState* platState,
    const char* applicationName,
    int32 x,
    int32 y,
    int32 width,
    int32 heigh);

GLUTTONY_API void platform_Shutdown(platformState* state);
GLUTTONY_API bool8 platform_Push_messages(platformState* state);

void* platform_allocate_memory(u64 size, bool8 aligned);
void  platform_free_memory(void* block, bool8 aligned);
void* platform_zero_memory(void* block, u64 size);
void* platform_copy_memory(void* dest, const void* source, u64 size);
void* platform_set_memory(void* dest, int32 value, u64 size);

void platform_console_write(const char* message, u8 color);
void platform_console_write_error(const char* message, u8 color);

double platform_get_absolute_Time();

void platform_sleep(u64 ms);