// PBC+ port: there is no file system for a log on the device. The PC test
// build defines PBC_LOG_STDOUT to see the messages.
#include <stdarg.h>
#include <stdio.h>

#include "logger.h"

void Logger_Initialize(const char* pathPrefix) { (void)pathPrefix; }

void Logger_LogOutput(const char * func, size_t line, const char * format, ...)
{
#ifdef PBC_LOG_STDOUT
    va_list args;
    va_start(args, format);
    printf("%s:%zu: ", func, line);
    vprintf(format, args);
    va_end(args);
#else
    (void)func; (void)line; (void)format;
#endif
}

void Logger_Write(const char * format, ...)
{
#ifdef PBC_LOG_STDOUT
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
#else
    (void)format;
#endif
}

void Logger_WriteUnformatted(const char * message)
{
#ifdef PBC_LOG_STDOUT
    fputs(message, stdout);
#else
    (void)message;
#endif
}

void Logger_Exit() { }
