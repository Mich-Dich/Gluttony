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


	int Log_Init(const char* CoreFileName, const char* Format);

	/*  Formating the LogMessages can be customised with the following tags
		to format all following Log Messages use: set_Formating(char* format);
		e.g. set_Formating("$B[$T] $L [$F]  $C$E")  or set_Formating("$BTime:[$M $S] $L $E ==> $C")

	$T		Time		hh:mm:ss
	$H		Time Hour	hh
	$M		Time Min.	mm
	$S		Time Sec.	ss

	$N		Date		yyyy:mm:dd:
	$Y		Date Year	yyyy
	$O		Date Month	mm
	$D		Date Day	dd

	$L		LogLevel	[TRACE], [DEBUG] … [FATAL]
	$X		Alienment	add space for "INFO" & "WARN"
	$F		Func. Name	main, foo
	$A		File Name	main.c foo.c
	$G		Line		1, 42
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

/*	class __declspec(dllexport) LogMessage : public  std::ostringstream {

	public:
		LogMessage(LogSeverityLevel level, const char* fileName, const char* funcName, int line);
		~LogMessage();

	protected:
		void printLogMessage();

	private:
		LogSeverityLevel LSLevel_;
		const char* fileName_;
		const char* funcName_;
		int line_;
	};

	class GLUTTONY_API LogMessageFatal : public LogMessage {

	public:
		LogMessageFatal(const char* fileName, const char* funcName, int line);
		~LogMessageFatal();
	};*/
}

#define MAX_MEASSGE_SIZE 1024

#define GL_LOG_TRACE(message)			LogMsgGluttony::LogSeverityLevel::TRACE,__FILE__,__FUNCTION__,__LINE__, message)
#define GL_LOG_DEBUG(message)			Gluttony::LogMessage(Gluttony::LogSeverityLevel::DEBUG,__FILE__,__FUNCTION__,__LINE__).flush() << message
#define GL_LOG_INFO(message)			Gluttony::LogMessage(Gluttony::LogSeverityLevel::INFO,__FILE__,__FUNCTION__,__LINE__).flush() << message
#define GL_LOG_WARN(message)			Gluttony::LogMessage(Gluttony::LogSeverityLevel::WARN,__FILE__,__FUNCTION__,__LINE__).flush() << message
#define GL_LOG_ERROR(message)			Gluttony::LogMessage(Gluttony::LogSeverityLevel::ERROR,__FILE__,__FUNCTION__,__LINE__).flush() << message
#define GL_LOG_FATAL(message)			//Gluttony::LogMessageFatal(__FILE__,__FUNCTION__,__LINE__).flush()

#define GL_LOG(LSLevel, message)				GL_LOG_##LSLevel(message)

#define GL_LOG_CORE_Trace(message, ...)		Gluttony::LogMsg(Gluttony::LogSeverityLevel::Trace, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)
#define GL_LOG_CORE_Debug(message, ...)		Gluttony::LogMsg(Gluttony::LogSeverityLevel::Debug, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)
#define GL_LOG_CORE_Info(message, ...)		Gluttony::LogMsg(Gluttony::LogSeverityLevel::Info, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)
#define GL_LOG_CORE_Warn(message, ...)		Gluttony::LogMsg(Gluttony::LogSeverityLevel::Warn, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)
#define GL_LOG_CORE_Error(message, ...)		Gluttony::LogMsg(Gluttony::LogSeverityLevel::Error, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)
#define GL_LOG_CORE_Fatal(message, ...)		Gluttony::LogMsg(Gluttony::LogSeverityLevel::Fatal, __FILE__, __FUNCTION__, __LINE__, message, __VA_ARGS__)

#define GL_LOG_CORE(LSLevel, message, ...)		GL_LOG_CORE_##LSLevel(message, __VA_ARGS__)

#define GL_ASSERT(expr)								\
	if (expr, successMsg, failureMsg) {				\
		GL_LOG_CORE_TRACE << successMsg				\
	} else {										\
		GL_LOG_CORE_FATAL << failureMsg;			\
	}
