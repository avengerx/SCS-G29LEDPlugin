#include "pch.h"
#include <stdio.h>
#include <share.h>
#include "log.h"

// TODO: Use ATS/ETS2 documents path (next to game.log.txt)
#define LOGDIR "c:\\cygwin\\var\\log\\"
#define LOGFILE "g29ledplugin.log"
#define LOGPATH LOGDIR LOGFILE
#define LOG_PREFIX "G29LedPlugin: "

scs_log_t game_log = nullptr;
std::mutex logfile_access;

static const size_t logprefix_len = strlen(LOG_PREFIX);

static void logSCS(scs_log_type_t tp, const char* const message, va_list args) {
    SYSTEMTIME st;
    char* parsed_message;
    int len;

    if (game_log != nullptr) {
        len = _vscprintf(message, args) + 1;
        if (len < 1) return;
        parsed_message = (char*)malloc((len + logprefix_len) * sizeof(char));
        if (!parsed_message) return;
        memcpy_s(parsed_message, logprefix_len, LOG_PREFIX, logprefix_len);
        if (!&parsed_message[logprefix_len]) return;
        vsprintf_s(&parsed_message[logprefix_len], len, message, args);
        game_log(tp, parsed_message);
        free(parsed_message);
    } else {
        GetSystemTime(&st);
        logfile_access.lock();
        FILE* loghandle = _fsopen(LOGPATH, "a", _SH_DENYWR);
        fprintf_s(loghandle, "%04d-%02d-%02d %02d:%02d:%02d.%03d UTC: ",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond,
            st.wMilliseconds);

        vfprintf(loghandle, message, args);

        fwrite("\n", 1, 1, loghandle);
        fclose(loghandle);
        logfile_access.unlock();
    }
}

static void logSCS(scs_log_type_t tp, const wchar_t* const message, va_list args) {
    SYSTEMTIME st;
    wchar_t* parsed_message;
    char* converted_message;
    int wlen;
    size_t sz;

    if (game_log != nullptr) {
        wlen = _vscwprintf(message, args) + 1;

        if (wlen < 1) return;

        parsed_message = (wchar_t*)malloc(wlen * sizeof(wchar_t));
        if (!parsed_message) return;
        vswprintf_s(parsed_message, wlen, message, args);

        if (wcstombs_s(&sz, nullptr, 0, parsed_message, _TRUNCATE) == S_OK) {
            converted_message = (char*)malloc(sz + logprefix_len);
            memcpy_s(converted_message, logprefix_len, LOG_PREFIX, logprefix_len);
            if (wcstombs_s(nullptr, &converted_message[logprefix_len], sz, parsed_message, _TRUNCATE) == S_OK)
                game_log(tp, converted_message);
            free(converted_message);
        }
        free(parsed_message);
    } else {
        GetSystemTime(&st);
        logfile_access.lock();
        FILE* loghandle = _fsopen(LOGPATH, "a", _SH_DENYWR);
        fwprintf_s(loghandle, L"%04d-%02d-%02d %02d:%02d:%02d.%03d UTC: ",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond,
            st.wMilliseconds);

        vfwprintf(loghandle, message, args);

        fwprintf_s(loghandle, L"\n");
        fclose(loghandle);
        logfile_access.unlock();
    }
}

void log(const char* message, ...) {
    va_list args;
    va_start(args, message);
    logSCS(SCS_LOG_TYPE_message, message, args);
    va_end(args);
}

void logErr(const char* message, ...) {
    va_list args;
    va_start(args, message);
    logSCS(SCS_LOG_TYPE_error, message, args);
    va_end(args);
}

void logWarn(const char* message, ...) {
    va_list args;
    va_start(args, message);
    logSCS(SCS_LOG_TYPE_warning, message, args);
    va_end(args);
}

void log(const wchar_t* message, ...) {
    va_list args;
    va_start(args, message);
    logSCS(SCS_LOG_TYPE_message, message, args);
    va_end(args);
}

void logErr(const wchar_t* message, ...) {
    va_list args;
    va_start(args, message);
    logSCS(SCS_LOG_TYPE_error, message, args);
    va_end(args);
}

void logWarn(const wchar_t* message, ...) {
    va_list args;
    va_start(args, message);
    logSCS(SCS_LOG_TYPE_warning, message, args);
    va_end(args);
}