
#include "glpch.h"

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
    static const char* LogFileName = "logFile.txt";
    static const char* LogCoreFileName = "logFileCORE.txt";
    static const char* LogMessageFormat = "[$B$T:$J$E] [$B$L$X - $A - $F:$G$E] $C";
    static const char* LogMessageFormat_BACKUP = "$B[$T] $L$E - $C";
    static const char* displayed_Path_Start = "src\\";
    static const char* displayed_FuncName_Start = "Gluttony::";
    static int Buffer_Level;

    int Log_Init(const char* LogCoreFile, const char* LogFile, const char* Format) {

        LogCoreFileName = LogCoreFile;
        LogFileName = LogFile;
        Set_Format(Format);

        std::ostringstream Init_Message;
        Init_Message.flush();

        SYSTEMTIME TimeLoc;
        GetLocalTime(&TimeLoc);

        Init_Message << "[" << std::setw(4) << std::setfill('0') << TimeLoc.wYear
            << "/" << std::setw(2) << std::setfill('0') << TimeLoc.wMonth
            << "/" << std::setw(2) << std::setfill('0') << TimeLoc.wDay
            << " - " << std::setw(2) << std::setfill('0') << TimeLoc.wHour
            << ":" << std::setw(2) << std::setfill('0') << TimeLoc.wMinute
            << ":" << std::setw(2) << std::setfill('0') << TimeLoc.wSecond << "]"
            << "  Log initialized" << std::endl

            << "   Inital Log Format: '" << Format << "'" << std::endl << "   Enabled Log Levels: ";

        static const char* loc_level_str[6] = { "Fatal", " + Error", " + Warn", " + Info", " + Debug", " + Trace" };
        for (int x = 0; x < LOG_LEVEL_ENABLED + 2; x++)
            Init_Message << loc_level_str[x];

        // Write the content to a file
        std::ofstream outputFile(LogFileName);
        if (outputFile.is_open()) {
            outputFile << Init_Message.str() << std::endl;
            outputFile.close();
        } else {
            std::cerr << "Error: Unable to open the file for writing." << std::endl;
            return -1;
        }

        GL_SEPERATOR_BIG;
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

    //
    void LogMsg(LogSeverityLevel level, const char* fileName, const char* funcName, int line, const char* message, ...) {

        if (strlen(message) == 0)
            return;

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
        char char_Buffer[MAX_MEASSGE_SIZE];
        size_t length = 0;
        char Format_Command;
        std::string result = "";                // Message for LogFile output
        std::string result_Intermediate = "";   // Message for Console output


        // Loop over Format string and build Final Message
        int FormatLen = static_cast<int>(strlen(LogMessageFormat));
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

                    // ------------------------------------  Source  -------------------------------------------------------------------------------
                    // Function Name
                case 'F':
                    // Copy File String to 
                    length = strlen(funcName);
                    strncpy_s(char_Buffer, funcName, length);
                    char_Buffer[length] = '\0';
                    Format_Filled << shorten_String(char_Buffer, displayed_FuncName_Start);
                    break;

                    // File Name
                case 'A':
                    // Copy File String to 
                    length = strlen(fileName);
                    strncpy_s(char_Buffer, fileName, length);
                    char_Buffer[length] = '\0';
                    Format_Filled << shorten_String(char_Buffer, displayed_Path_Start);
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

                    // Clock secunde
                case 'H':
                    Format_Filled << std::setw(2) << std::setfill('0') << TimeLoc.wHour;
                    break;

                    // Clock minute
                case 'M':
                    Format_Filled << std::setw(2) << std::setfill('0') << TimeLoc.wMinute;
                    break;

                    // Clock second
                case 'S':
                    Format_Filled << std::setw(2) << std::setfill('0') << TimeLoc.wSecond;

                    break;

                    // Clock millisec.
                case 'J':
                    Format_Filled << std::setw(3) << std::setfill('0') << TimeLoc.wMilliseconds;

                    break;

                    // ------------------------------------  Date  -------------------------------------------------------------------------------
                    // Data yyyy/mm/dd
                case 'N':
                    Format_Filled << std::setw(4) << std::setfill('0') << TimeLoc.wYear
                        << "/" << std::setw(2) << std::setfill('0') << TimeLoc.wMonth
                        << "/" << std::setw(2) << std::setfill('0') << TimeLoc.wDay;
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

        Format_Filled << "\0";

        result_Intermediate = Format_Filled.str();
        std::cout << result_Intermediate << std::endl;

        // Final Message for File
        result += Format_Filled.str();

        // Write the content to a file
        std::ofstream outputFile(LogFileName, std::ios::app);
        if (outputFile.is_open()) {
            outputFile << result << std::endl;
            outputFile.close();
        }
    }

    // Function to extract important funName
    inline char* shorten_String(char* funcName, const char* displayedFuncNameStart) {

        const char* position = strstr(funcName, displayedFuncNameStart);

        if (position != nullptr) {

            size_t remainingLength = strlen(position + strlen(displayedFuncNameStart));
            strncpy(funcName, position + strlen(displayedFuncNameStart), remainingLength);
            funcName[remainingLength] = '\0';
        }

        return funcName;
    }
}
