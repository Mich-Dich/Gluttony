#pragma once

#include "defines.h"

#define GL_ASSERTIOS_ENABLED

#ifdef   GL_ASSERTIOS_ENABLED
    #if _MSC_VER
        #include <intrin.h>
        #define debugBreak() __debugbreak()
    #else
        #define debugBreak() __buildin_trap()
    #endif

    GLUTTONY_API void report_assert_failure(const char* expression, const char* message, const char* file, int32 line);

    // Assertion Macro without message
    #define GL_ASSERT(expr) {                                               \
            if (expr) { }                                                   \
            else {                                                          \
                report_assert_failure(#expr, "", __FILE__, __LINE__);       \
                debugBreak();                                               \
            }                                                               \
        }                                                                   \
        
    // Assertion Macro
    #define GL_ASSERT_MEG(expr, message) {                                  \
            if (expr) { }                                                   \
            else {                                                          \
                report_assert_failure(#expr, message, __FILE__, __LINE__);  \
                debugBreak();                                               \
            }                                                               \
        } 
        
    // Assertion Macro only for debugging
    #ifdef _DEBUG
        #define GL_ASSERT_DEBUG(expr) {                                     \
                if (expr) { }                                               \
                else {                                                      \
                    report_assert_failure(#expr, "", __FILE__, __LINE__);   \
                    debugBreak();                                           \
                }                                                           \
            }
    #else
        #define GL_ASSERT_DEBUG(expr)
    #endif

#else
    #define GL_ASSERT(expr)                 // Disabled
    #define GL_ASSERT_MEG(expr, message)    // Disabled
    #define GL_ASSERT_DEBUG(expr)           // Disabled
#endif