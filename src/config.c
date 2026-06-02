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

    toml_free(root);

    if (!cfg->self_name[0]) {
        LOG_ERR("config missing [self].name");
        return -1;
    }

    LOG_INFO("config loaded: self=%s wm=%s machines=%d port=%d",
             cfg->self_name, cfg->self_wm, cfg->machine_count, cfg->port);
    return 0;
}

void config_dump(const glew_config_t *cfg)
{
    LOG_DBG("self: name=%s wm=%s", cfg->self_name, cfg->self_wm);
    LOG_DBG("network: port=%d", cfg->port);
    LOG_DBG("input: escape=%s", cfg->escape_key);

    for (int i = 0; i < cfg->machine_count; i++) {
        const machine_t *m = &cfg->machines[i];
        LOG_DBG("machine[%d]: name=%s addr=%s monitors=%d",
                i, m->name, m->address, m->monitor_count);
        for (int j = 0; j < m->monitor_count; j++)
            LOG_DBG("  monitor[%d]: %s", j, m->monitors[j]);
    }
}
