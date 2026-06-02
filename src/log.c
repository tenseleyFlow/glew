#include "log.h"
#include <stdarg.h>
#include <string.h>

static log_level_t g_level = LOG_INFO;

static const char *level_str[] = {
    [LOG_DEBUG] = "DBG",
    [LOG_INFO]  = "INF",
    [LOG_WARN]  = "WRN",
    [LOG_ERROR] = "ERR",
};

void log_set_level(log_level_t level)
{
    g_level = level;
}

void log_msg(log_level_t level, const char *fmt, ...)
{
    if (level < g_level)
        return;

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    char timebuf[20];
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &tm);

    fprintf(stderr, "[%s %s] ", timebuf, level_str[level]);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fputc('\n', stderr);
}
