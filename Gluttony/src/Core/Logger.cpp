#include <stdio.h>
#include <cstdarg>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <windows.h>

#include "Logger.h"

namespace Gluttony {

	const char* SeverityNames[LogSeverityLevel::NUM_SEVERITYS]{ "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL" };
    const static WORD Color[NUM_SEVERITYS] = {
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
        FOREGROUND_INTENSITY | FOREGROUND_BLUE,
        FOREGROUND_INTENSITY | FOREGROUND_GREEN,
        FOREGROUND_RED | FOREGROUND_GREEN,
        FOREGROUND_RED,
        BACKGROUND_RED | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
    };
	static const char* LogFileName;
	static const char* LogCoreFileName;
	static const char* LogMessageFormat = "[$B$T:$J$E] [$B$L$X - $A - $F:$G$E] $C";
	static const char* LogMessageFormat_BACKUP = "$B[$T] $L$E - $C";
	static const char* displayed_Path_Start = "Gluttony";
	static int Buffer_Level;

	/*
	LogMessage::LogMessage(LogSeverityLevel level, const char* fileName, const char* funcName, int line) :
		LSLevel_(level), fileName_(fileName), funcName_(funcName), line_(line) {}

	LogMessage::~LogMessage() {

		printLogMessage();
	}

	LogMessageFatal::LogMessageFatal(const char* fileName, const char* funcName, int line) :
		LogMessage(FATAL, fileName, funcName, line) {}

	LogMessageFatal::~LogMessageFatal() {

		printLogMessage();
		// abort();
	}



	void LogMessage::printLogMessage() {

		//if (!str().empty())
		printf("[%s] [%s:%s:%d] %s\n", SeverityNames[LSLevel_], fileName_, funcName_, line_, str().c_str());
	}*/

	int Log_Init(const char* CoreFileName, const char* Format) {

		LogCoreFileName = CoreFileName;
		Set_Format(Format);
		return 0;
	}

	// change Format for all following messages
	void Set_Format(const char* newFormat) {

		LogMessageFormat_BACKUP = LogMessageFormat;
		LogMessageFormat = newFormat;
	}

	// Use previous Format
	void Use_Format_Backup() {

		const char* buffer = LogMessageFormat;
		LogMessageFormat = LogMessageFormat_BACKUP;
		LogMessageFormat_BACKUP = buffer;
	}

	// deside with messages should be bufferd and witch are written to file instantly
	void set_buffer_Level(int newLevel) {


	}
	void LogMsg(LogSeverityLevel level, const char* fileName, const char* funcName, int line, char* message, ...) {

        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

        // Get the current local system time
        SYSTEMTIME TimeLoc;
        GetLocalTime(&TimeLoc);

        // Format Vars into message
        va_list args;
        va_start(args, message);
            int bufferSize = vsnprintf(nullptr, 0, message, args);
            char* message_Formated = new char[bufferSize + 1];
            vsnprintf(message_Formated, bufferSize + 1, message, args);
        va_end(args);

        // Create Buffer vars
        std::ostringstream Format_Filled;
        Format_Filled.flush();
        char locFileName[MAX_MEASSGE_SIZE];
        size_t length = 0;
        char Format_Command;
        std::string result = "";                // Message for LogFile output
        std::string result_Intermediate = "";   // Message for Console output


        // Loop over Format string and build Final Message
        int FormatLen = strlen(LogMessageFormat);
        for (int x = 0; x < FormatLen; x++) {

            if (LogMessageFormat[x] == '$' && x + 1 < FormatLen) {

                Format_Command = LogMessageFormat[x + 1];
                switch (Format_Command) {

                // ------------------------------------  Basic Info  -------------------------------------------------------------------------------
                // Color Start
                case 'B':
                    // Send Message Formated until now && set Console Coloring for following messages
                    result_Intermediate = Format_Filled.str();
                    result += Format_Filled.str();
                    std::cout << result_Intermediate;

                    Format_Filled.str("");
                    SetConsoleTextAttribute(hConsole, Color[level]);
                    break;

                // Color End
                case 'E':
                    // Send Message Formated until now && set Console Coloring for following messages
                    result_Intermediate = Format_Filled.str();
                    result += Format_Filled.str();
                    std::cout << result_Intermediate;

                    Format_Filled.str("");
                    SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                    break;

                // input text (message)
                case 'C':
                    Format_Filled << message_Formated;
                    break;

                // Log Level
                case 'L':
                    Format_Filled << SeverityNames[level];
                    break;

                // Alignment
                case 'X':
                    if (level == Gluttony::LogSeverityLevel::Info || level == Gluttony::LogSeverityLevel::Warn) { Format_Filled << " "; }
                    break;
                
                // Function Name
                case 'F':
                    Format_Filled << funcName;
                    break;

                // File Name
                case 'A':
                    // Copy File String to 
                    length = strlen(fileName);
                    strncpy(locFileName, fileName, length);
                    locFileName[length] = '\0';
                    Format_Filled << shorten_File_Path(locFileName, displayed_Path_Start);
                    break;

                // Line
                case 'G':
                    Format_Filled << line;
                    break;

                // ------------------------------------  Time  -------------------------------------------------------------------------------
                // Clock hh:mm:ss
                case 'T':
                    Format_Filled << std::setw(2) << std::setfill('0') << TimeLoc.wHour
                        << ":" << std::setw(2) << std::setfill('0') << TimeLoc.wMinute
                        << ":" << std::setw(2) << std::setfill('0') << TimeLoc.wSecond;
                    break;

                // Clock ss
                case 'H':
                    Format_Filled << std::setw(2) << std::setfill('0') << TimeLoc.wHour;
                    break;

                // Clock mm
                case 'M':
                    Format_Filled << std::setw(2) << std::setfill('0') << TimeLoc.wMinute;
                    break;

                // Clock ss
                case 'S':
                    Format_Filled << std::setw(2) << std::setfill('0') << TimeLoc.wSecond;

                    break;

                // Clock ss
                case 'J':
                    Format_Filled << std::setw(2) << std::setfill('0') << TimeLoc.wMilliseconds;

                    break;

                // ------------------------------------  Date  -------------------------------------------------------------------------------
                // Data yyyy/mm/dd
                case 'N':
                    Format_Filled << std::setw(4) << std::setfill('0') << TimeLoc.wYear
                        << "-" << std::setw(2) << std::setfill('0') << TimeLoc.wMonth 
                        << "-" << std::setw(2) << std::setfill('0') << TimeLoc.wDay;
                    break;

                // Year
                case 'Y':
                    Format_Filled << std::setw(4) << std::setfill('0') << TimeLoc.wYear;
                    break;

                // Month
                case 'O':
                    Format_Filled << std::setw(2) << std::setfill('0') << TimeLoc.wMonth;
                    break;

                // Day
                case 'D':
                    Format_Filled << std::setw(2) << std::setfill('0') << TimeLoc.wDay;
                    break;

                // ------------------------------------  Default  -------------------------------------------------------------------------------
                default:
                    break;
                }

                x++;
            }
            
            else {

                Format_Filled << LogMessageFormat[x];
            }
        }


        result_Intermediate = Format_Filled.str();
        std::cout << result_Intermediate << std::endl;

        // Final Message for File
        result += Format_Filled.str();
        //std::cout << std::endl << result << std::endl;

        
        // Write the content to a file
        std::ofstream outputFile("LogFile.txt");
        if (outputFile.is_open()) {
            outputFile << result;
            outputFile.close();
            //std::cout << "Content successfully written to %s" << LogFileName << std::endl;
        } else {
            std::cerr << "Error: Unable to open the file for writing." << std::endl;
        }

		// Don't forget to free the allocated memory
	}

	// Function to extract the displayed path
	char* shorten_File_Path(char* fullPath, const char* displayedPathStart) {

		const char* position = strstr(fullPath, displayedPathStart);

		if (position != nullptr) {
			strcpy(fullPath, position);
			return fullPath;
		}

		return strdup(fullPath);
	}
}
