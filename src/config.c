#include "config.h"
#include "log.h"
#include "../deps/toml.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *config_default_path(char *buf, size_t bufsz)
{
    const char *home = getenv("HOME");
    if (!home)
        return NULL;

    int n = snprintf(buf, bufsz, "%s/.config/glew/glew.toml", home);
    if (n < 0 || (size_t)n >= bufsz)
        return NULL;

    return buf;
}

static void copy_str(char *dst, size_t dstsz, toml_datum_t d)
{
    if (d.ok) {
        snprintf(dst, dstsz, "%s", d.u.s);
        free(d.u.s);
    }
}

static int parse_machines(toml_table_t *layout, glew_config_t *cfg)
{
    toml_array_t *machines = toml_array_in(layout, "machines");
    if (!machines)
        return 0;

    int n = toml_array_nelem(machines);
    if (n > GLEW_MAX_MACHINES) {
        LOG_WARN("truncating machine list to %d", GLEW_MAX_MACHINES);
        n = GLEW_MAX_MACHINES;
    }

    for (int i = 0; i < n; i++) {
        toml_table_t *m = toml_table_at(machines, i);
        if (!m)
            continue;

        machine_t *mc = &cfg->machines[cfg->machine_count];

        copy_str(mc->name, sizeof(mc->name), toml_string_in(m, "name"));
        copy_str(mc->address, sizeof(mc->address), toml_string_in(m, "address"));

        toml_array_t *mons = toml_array_in(m, "monitors");
        if (mons) {
            int nm = toml_array_nelem(mons);
            if (nm > GLEW_MAX_MONITORS)
                nm = GLEW_MAX_MONITORS;
            for (int j = 0; j < nm; j++) {
                toml_datum_t s = toml_string_at(mons, j);
                copy_str(mc->monitors[j], sizeof(mc->monitors[j]), s);
            }
            mc->monitor_count = nm;
        }

        cfg->machine_count++;
    }

    return 0;
}

int config_load(const char *path, glew_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->port = 9437;
    snprintf(cfg->escape_key, sizeof(cfg->escape_key), "Scroll_Lock");

    FILE *fp = fopen(path, "r");
    if (!fp) {
        LOG_ERR("cannot open config: %s", path);
        return -1;
    }

    char errbuf[256];
    toml_table_t *root = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);

    if (!root) {
        LOG_ERR("config parse error: %s", errbuf);
        return -1;
    }

    toml_table_t *self = toml_table_in(root, "self");
    if (self) {
        copy_str(cfg->self_name, sizeof(cfg->self_name),
                 toml_string_in(self, "name"));
        copy_str(cfg->self_wm, sizeof(cfg->self_wm),
                 toml_string_in(self, "wm"));
        copy_str(cfg->mod_key, sizeof(cfg->mod_key),
                 toml_string_in(self, "mod_key"));
    }

    toml_table_t *layout = toml_table_in(root, "layout");
    if (layout)
        parse_machines(layout, cfg);

    toml_table_t *network = toml_table_in(root, "network");
    if (network) {
        toml_datum_t p = toml_int_in(network, "port");
        if (p.ok)
            cfg->port = (int)p.u.i;
        copy_str(cfg->secret, sizeof(cfg->secret),
                 toml_string_in(network, "secret"));
    }

    toml_table_t *input = toml_table_in(root, "input");
    if (input)
        copy_str(cfg->escape_key, sizeof(cfg->escape_key),
                 toml_string_in(input, "escape_key"));

    /* parse [actions] table */
    toml_table_t *actions = toml_table_in(root, "actions");
    if (actions) {
        int n = toml_table_ntab(actions) + toml_table_nkval(actions);
        if (n > GLEW_MAX_ACTIONS)
            n = GLEW_MAX_ACTIONS;

        for (int i = 0; ; i++) {
            const char *key = toml_key_in(actions, i);
            if (!key) break;
            if (cfg->action_count >= GLEW_MAX_ACTIONS) break;

            toml_datum_t val = toml_string_in(actions, key);
            if (!val.ok) continue;

            action_binding_t *ab = &cfg->actions[cfg->action_count];
            ab->extra_mods = 0;

            const char *kname = key;
            for (;;) {
                if (strncmp(kname, "shift+", 6) == 0) {
                    ab->extra_mods |= (1 << 0);
                    kname += 6;
                } else if (strncmp(kname, "ctrl+", 5) == 0) {
                    ab->extra_mods |= (1 << 2);
                    kname += 5;
                } else if (strncmp(kname, "alt+", 4) == 0) {
                    ab->extra_mods |= (1 << 3);
                    kname += 4;
                } else {
                    break;
                }
            }

            /* convert key name to keysym */
            if (strcmp(kname, "return") == 0)       ab->keysym = 0xff0d;
            else if (strcmp(kname, "space") == 0)    ab->keysym = 0x0020;
            else if (strcmp(kname, "tab") == 0)      ab->keysym = 0xff09;
            else if (strcmp(kname, "escape") == 0)   ab->keysym = 0xff1b;
            else if (strcmp(kname, "backspace") == 0) ab->keysym = 0xff08;
            else if (strcmp(kname, "delete") == 0)   ab->keysym = 0xffff;
            else if (strcmp(kname, "left") == 0)     ab->keysym = 0xff51;
            else if (strcmp(kname, "up") == 0)       ab->keysym = 0xff52;
            else if (strcmp(kname, "right") == 0)    ab->keysym = 0xff53;
            else if (strcmp(kname, "down") == 0)     ab->keysym = 0xff54;
            else if (strlen(kname) == 1)             ab->keysym = (uint32_t)kname[0];
            else { free(val.u.s); continue; }

            snprintf(ab->action, sizeof(ab->action), "%s", val.u.s);
            free(val.u.s);
            cfg->action_count++;
        }
    }

    toml_free(root);

    if (!cfg->self_name[0]) {
        LOG_ERR("config missing [self].name");
        return -1;
    }

    if (!cfg->mod_key[0])
        snprintf(cfg->mod_key, sizeof(cfg->mod_key), "super");

    LOG_INFO("config loaded: self=%s wm=%s mod=%s machines=%d actions=%d port=%d",
             cfg->self_name, cfg->self_wm, cfg->mod_key,
             cfg->machine_count, cfg->action_count, cfg->port);
    return 0;
}

const char *config_find_action(const glew_config_t *cfg, uint32_t keysym,
                               uint32_t extra_mods)
{
    for (int i = 0; i < cfg->action_count; i++) {
        if (cfg->actions[i].keysym == keysym &&
            cfg->actions[i].extra_mods == extra_mods)
            return cfg->actions[i].action;
    }
    return NULL;
}

uint32_t config_mod_bit(const glew_config_t *cfg)
{
    if (strcmp(cfg->mod_key, "alt") == 0 || strcmp(cfg->mod_key, "option") == 0)
        return (1 << 3);
    return (1 << 6); /* super is default */
}

void config_dump(const glew_config_t *cfg)
{
    LOG_DBG("self: name=%s wm=%s mod=%s", cfg->self_name, cfg->self_wm, cfg->mod_key);
    LOG_DBG("network: port=%d", cfg->port);
    LOG_DBG("input: escape=%s", cfg->escape_key);
    LOG_DBG("actions: %d bindings", cfg->action_count);
    for (int i = 0; i < cfg->action_count; i++)
        LOG_DBG("  action[%d]: keysym=0x%x mods=0x%x → %s",
                i, cfg->actions[i].keysym, cfg->actions[i].extra_mods,
                cfg->actions[i].action);

    for (int i = 0; i < cfg->machine_count; i++) {
        const machine_t *m = &cfg->machines[i];
        LOG_DBG("machine[%d]: name=%s addr=%s monitors=%d",
                i, m->name, m->address, m->monitor_count);
        for (int j = 0; j < m->monitor_count; j++)
            LOG_DBG("  monitor[%d]: %s", j, m->monitors[j]);
    }
}
