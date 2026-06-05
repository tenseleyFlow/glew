#ifndef GLEW_CONFIG_H
#define GLEW_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#define GLEW_MAX_MACHINES  16
#define GLEW_MAX_MONITORS  8
#define GLEW_MAX_NAME      64
#define GLEW_MAX_ADDR      256
#define GLEW_MAX_ACTIONS   64

typedef struct {
    char name[GLEW_MAX_NAME];
    char address[GLEW_MAX_ADDR];
    char monitors[GLEW_MAX_MONITORS][GLEW_MAX_NAME];
    int  monitor_count;
} machine_t;

typedef struct {
    uint32_t keysym;
    int      shift;
    char     action[128];
} action_binding_t;

typedef struct {
    /* [self] */
    char      self_name[GLEW_MAX_NAME];
    char      self_wm[GLEW_MAX_NAME];
    char      mod_key[GLEW_MAX_NAME];

    /* [layout] */
    machine_t machines[GLEW_MAX_MACHINES];
    int       machine_count;

    /* [network] */
    int       port;
    char      secret[256];

    /* [input] */
    char      escape_key[GLEW_MAX_NAME];

    /* [actions] — mod+key → action mappings */
    action_binding_t actions[GLEW_MAX_ACTIONS];
    int              action_count;
} glew_config_t;

/* look up an action for a keysym + shift state. returns NULL if no match. */
const char *config_find_action(const glew_config_t *cfg, uint32_t keysym, int shift);

/* convert mod_key string ("super", "alt") to a bitmask for mods field comparison */
uint32_t config_mod_bit(const glew_config_t *cfg);

/*
 * Parse config from the given TOML file path.
 * Returns 0 on success, -1 on error (logs details).
 */
int config_load(const char *path, glew_config_t *cfg);

/*
 * Return the default config file path (~/.config/glew/glew.toml).
 * Writes into buf, returns buf on success, NULL if the path doesn't fit.
 */
char *config_default_path(char *buf, size_t bufsz);

void config_dump(const glew_config_t *cfg);

#endif
