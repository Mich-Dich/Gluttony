#pragma once

#include "defines.h"

#define LOG_ENABLED_WARN 1
#define LOG_ENABLED_INFO 1
#define LOG_ENABLED_DEBUG 1
#define LOG_ENABLED_TRACE 1

// Override log levels for release
#if GL_RELEASE
    #define LOG_ENABLED_DEBUG 0
    #define LOG_ENABLED_TRACE 0
#endif

typedef enum log_level {

    LL_FATAL = 0,
    LL_ERROR = 1,
    LL_WARN = 2,
    LL_INFO = 3,
    LL_DEBUG = 4,
    LL_TRACE = 5
} log_level;

bool8 Init_Log();
void Shutdown_Log();

GLUTTONY_API void log_output(log_level level, const char* message, ...);


// define log macro for FATAL
#ifndef GL_FATAL
    #define GL_FATAL(message, ...) log_output(LL_FATAL, message, ##__VA_ARGS__);
#endif

// define log macro for ERRORS
#ifndef GL_ERROR
    #define GL_ERROR(message, ...) log_output(LL_ERROR, message, ##__VA_ARGS__);
#endif

// define conditional log macro for WARNINGS
#if LOG_ENABLED_WARN == 1
    #define GL_WARNING(message, ...) log_output(LL_WARN, message, ##__VA_ARGS__);
#else
    #define GL_WARN(message, ...)
#endif

// define conditional log macro for INFO
#if LOG_ENABLED_INFO == 1
    #define GL_INFO(message, ...) log_output(LL_INFO, message, ##__VA_ARGS__);
#else
    #define GL_INFO(message, ...)
#endif

// define conditional log macro for WARNINGS
#if LOG_ENABLED_DEBUG == 1
    #define GL_DEBUG(message, ...) log_output(LL_DEBUG, message, ##__VA_ARGS__);
#else
    #define GL_DEBUG(message, ...)
#endif

// define conditional log macro for INFO
#if LOG_ENABLED_TRACE == 1
    #define GL_TRACE(message, ...) log_output(LL_TRACE, message, ##__VA_ARGS__);
#else
    #define GL_TRACE(message, ...)
#endif
