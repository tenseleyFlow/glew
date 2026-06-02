#include "driver.h"
#include <string.h>

static const char *dir_names[] = {
    [DIR_LEFT]  = "left",
    [DIR_RIGHT] = "right",
    [DIR_UP]    = "up",
    [DIR_DOWN]  = "down",
};

const char *direction_str(direction_t dir)
{
    if (dir >= 0 && dir <= DIR_DOWN)
        return dir_names[dir];
    return "unknown";
}

direction_t direction_parse(const char *s)
{
    if (strcmp(s, "left") == 0)  return DIR_LEFT;
    if (strcmp(s, "right") == 0) return DIR_RIGHT;
    if (strcmp(s, "up") == 0)    return DIR_UP;
    if (strcmp(s, "down") == 0)  return DIR_DOWN;
    return DIR_LEFT;
}

direction_t direction_opposite(direction_t dir)
{
    switch (dir) {
    case DIR_LEFT:  return DIR_RIGHT;
    case DIR_RIGHT: return DIR_LEFT;
    case DIR_UP:    return DIR_DOWN;
    case DIR_DOWN:  return DIR_UP;
    }
    return DIR_LEFT;
}
