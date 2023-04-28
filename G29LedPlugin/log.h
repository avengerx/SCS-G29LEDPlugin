#ifndef __LOG_H_INCLUDED__
#define __LOG_H_INCLUDED__
#include <mutex>

extern scs_log_t game_log;
extern std::mutex logfile_access;
void log(const char* const message, ...);
void log(const wchar_t* const message, ...);
void logErr(const char* const message, ...);
void logErr(const wchar_t* const message, ...);
void logWarn(const char* const message, ...);
void logWarn(const wchar_t* const message, ...);

#endif