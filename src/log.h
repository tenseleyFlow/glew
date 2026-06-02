#ifndef GLEW_LOG_H
#define GLEW_LOG_H

#include <stdio.h>
#include <time.h>

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
} log_level_t;

void log_set_level(log_level_t level);
void log_msg(log_level_t level, const char *fmt, ...);

#define LOG_DBG(...)  log_msg(LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO(...) log_msg(LOG_INFO,  __VA_ARGS__)
#define LOG_WARN(...) log_msg(LOG_WARN,  __VA_ARGS__)
#define LOG_ERR(...)  log_msg(LOG_ERROR, __VA_ARGS__)

#endif
