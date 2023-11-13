#pragma once

#include <string>
#include <sstream>

#include "core.h"

#undef ERROR

namespace Gluttony {

	enum LogSeverityLevel {
		Trace = 0,
		Debug = 1,
		Info = 2,
		Warn = 3,
		Error = 4,
		Fatal = 5,
		NUM_SEVERITYS = 6
	};

	GLUTTONY_API int Log_Init(const char* LogCoreFile, const char* LogFile, const char* Format);

	/*  Formating the LogMessages can be customised with the following tags
		to format all following Log Messages use: set_Formating(char* format);
		e.g. set_Formating("$B[$T] $L [$F]  $C$E")  or set_Formating("$BTime:[$M $S] $L $E ==> $C")

	$T		Time		hh:mm:ss
	$H		Hour	hh
	$M		Minute	mm
	$S		Secunde	ss
	$J		MilSec. mm

	$N		Date		yyyy:mm:dd:
	$Y		Date Year	yyyy
	$O		Date Month	mm
	$D		Date Day	dd

	$F		Func. Name	main, foo
	$A		File Name	main.c foo.c
	$G		Line		1, 42

	$L		LogLevel	[TRACE], [DEBUG] … [FATAL]
	$X		Alienment	add space for "INFO" & "WARN"
	$B		Color Begin	from here the color starts
	$E		Color End	from here the color ends
	$C		Text		Formated Message with variables*/
	void Set_Format(const char* newFormat);
	void Use_Format_Backup();
	
	// Define witch log levels should be written to log file directly and witch should be buffered
	//  0    =>   write all logs directly to log file
	//  1    =>   buffer: TRACE
	//  2    =>   buffer: TRACE + DEBUG
	//  3    =>   buffer: TRACE + DEBUG + INFO
	//  4    =>   buffer: TRACE + DEBUG + INFO + WARN
	void set_buffer_Level(int newLevel);
	char* shorten_File_Path(char* fullPath, const char* displayedPathStart);

	void GLUTTONY_API LogMsg(LogSeverityLevel level, const char* fileName, const char* funcName, int line, char* message, ...);
}

// This enables the verious levels of the logging function (FATAL & ERROR are always on)
//  0    =>   FATAL + ERROR
//  1    =>   FATAL + ERROR + WARN
//  2    =>   FATAL + ERROR + WARN + INFO
//  3    =>   FATAL + ERROR + WARN + INFO + DEBUG
//  4    =>   FATAL + ERROR + WARN + INFO + DEBUG + TRACE
#define LOG_LEVEL_ENABLED 4
#define LOG_CLIENT_LEVEL_ENABLED 4


#define MAX_MEASSGE_SIZE 1024

/*  ===================================================================================  Core Logging Macros  ===================================================================================*/

#define GL_LOG_CORE_Fatal(message, ...)			Gluttony::LogMsg(Gluttony::LogSeverityLevel::Fatal, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)
#define GL_LOG_CORE_Error(message, ...)			Gluttony::LogMsg(Gluttony::LogSeverityLevel::Error, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)

// define conditional log macro for WARNINGS
#if LOG_LEVEL_ENABLED >= 1
	#define GL_LOG_CORE_Warn(message, ...)		Gluttony::LogMsg(Gluttony::LogSeverityLevel::Warn, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)
#else
	#define GL_LOG_CORE_Warn(message, ...)
#endif

// define conditional log macro for INFO
#if LOG_LEVEL_ENABLED >= 2
	#define GL_LOG_CORE_Info(message, ...)		Gluttony::LogMsg(Gluttony::LogSeverityLevel::Info, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)
#else
	#define GL_LOG_CORE_Info(message, ...)
#endif

// define conditional log macro for DEBUG
#if LOG_LEVEL_ENABLED >= 3
	#define GL_LOG_CORE_Debug(message, ...)		Gluttony::LogMsg(Gluttony::LogSeverityLevel::Debug, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)
#else
	#define GL_LOG_CORE_Debug(message, ...)
#endif

// define conditional log macro for REACE
#if LOG_LEVEL_ENABLED >= 4
	#define GL_LOG_CORE_Trace(message, ...)		Gluttony::LogMsg(Gluttony::LogSeverityLevel::Trace, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)
	// Insert a seperatioon line in Logoutput (-------)
	#define GL_SEPERATOR_CORE	Set_Format("$C");																												\
				GL_LOG_CORE_Trace("------------------------------------------------------------------------------------------------------------------------");	\
				Use_Format_Backup();

	// Insert a seperatioon line in Logoutput (=======)
	#define GL_SEPERATOR_BIG_CORE	Set_Format("$C");																											\
				GL_LOG_CORE_Trace("========================================================================================================================");	\
				Use_Format_Backup();
#else
	#define GL_LOG_CORE_Trace(message, ...)
	#define GL_SEPERATOR_CORE
	#define GL_SEPERATOR_BIG_CORE
#endif

/*  ===================================================================================  Client Logging Macros  ===================================================================================*/

#define GL_LOG_Fatal(message, ...)			Gluttony::LogMsg(Gluttony::LogSeverityLevel::Fatal, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)
#define GL_LOG_Error(message, ...)			Gluttony::LogMsg(Gluttony::LogSeverityLevel::Error, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)

// define conditional log macro for WARNINGS
#if LOG_CLIENT_LEVEL_ENABLED >= 1
	#define GL_LOG_Warn(message, ...)		Gluttony::LogMsg(Gluttony::LogSeverityLevel::Warn, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)
#else
	#define GL_LOG_Warn(message, ...)
#endif

// define conditional log macro for INFO
#if LOG_CLIENT_LEVEL_ENABLED >= 2
	#define GL_LOG_Info(message, ...)		Gluttony::LogMsg(Gluttony::LogSeverityLevel::Info, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)
#else
	#define GL_LOG_Info(message, ...)
#endif

// define conditional log macro for DEBUG
#if LOG_CLIENT_LEVEL_ENABLED >= 3
	#define GL_LOG_Debug(message, ...)		Gluttony::LogMsg(Gluttony::LogSeverityLevel::Debug, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)
#else
	#define GL_LOG_Debug(message, ...)
#endif

// define conditional log macro for REACE
#if LOG_CLIENT_LEVEL_ENABLED >= 4
	#define GL_LOG_Trace(message, ...)		Gluttony::LogMsg(Gluttony::LogSeverityLevel::Trace, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)
	// Insert a seperatioon line in Logoutput (-------)
	#define GL_SEPERATOR																																	\
			Set_Format("$C");																																\
			GL_LOG_Trace("------------------------------------------------------------------------------------------------------------------------");	\
			Use_Format_Backup();

	// Insert a seperatioon line in Logoutput (=======)
	#define GL_SEPERATOR_BIG																																\
			Set_Format("$C");																																\
			GL_LOG_Trace("========================================================================================================================");	\
			Use_Format_Backup();
#else
	#define GL_LOG_Trace(message, ...)
	#define GL_SEPERATOR
	#define GL_SEPERATOR_BIG
#endif

#define GL_LOG_CORE(LSLevel, message, ...)		GL_LOG_CORE_##LSLevel(message, __VA_ARGS__)
#define GL_LOG(LSLevel, message, ...)			GL_LOG_##LSLevel(message, __VA_ARGS__)

/*  ===================================================================================  Assertion & Validation  ===================================================================================*/

#define GL_ASSERT(expr, successMsg, failureMsg, ...)						\
	if (expr) {	GL_LOG_CORE_Trace(successMsg, __VA_ARGS__);					\
	} else {																\
		GL_LOG_CORE_Fatal(failureMsg, __VA_ARGS__);							\
		abort();															\
	}

#define GL_VALIDATE(expr, successMsg, failureMsg, RetVal, ...)				\
	if (expr) {	GL_LOG_CORE_Trace(failureMsg, __VA_ARGS__);					\
	} else {																\
		GL_LOG_CORE_Fatal(failureMsg, __VA_ARGS__);							\
		return RetVal;														\
	}
