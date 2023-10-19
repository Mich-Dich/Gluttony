#include "logger.h"
#include "asserts.h"
#include "platform/platform.h"

//TODO: temp
#include <stdio.h>
#include <string.h>
#include <stdarg.h>


bool8 Init_Log() {

    //TODO: creat log file
    return TRUE;
}

void Shutdown_Log(){

    //TODO: clean logging & push queued log entries
}

void log_output(log_level level, const char* message, ...){

    // TODO: Include Time at start of log message
    const char* level_str[6] ={"FATAL]   ", "ERROR]   ", "WARNING] ", "INFO]    ", "DEBUG]   ", "TRACE]   "};
    bool8 isError = level < LL_WARN;

    // CAUTION - fixed size, do NOT make logs messages over 32K characters - still better than dynamic memory allocation
    const int32 msg_length = 32000;
    char message_formated[msg_length];
    memset(message_formated, 0, sizeof(message_formated));

    __builtin_va_list args_ptr;
    va_start(args_ptr, message);
    vsnprintf(message_formated, msg_length, message, args_ptr);
    va_end(args_ptr);

    char message_out[msg_length];
    sprintf(message_out, "%s%s\n", level_str[level], message_formated);

    // Platform specific output
    if (isError) {
        platform_console_write_error(message_out, level);
    } else {
        platform_console_write(message_out, level);
    }
    
}

void report_assert_failure(const char* expression, const char* message, const char* file, int32 line) {

    log_output(LL_FATAL, "Assertion Failed: [%s], Message: '%s', [Source - File: %s, Line:%d]\n", expression, message, file, line);
}
