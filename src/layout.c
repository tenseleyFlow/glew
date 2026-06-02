#include "layout.h"
#include "log.h"

#include <string.h>

int layout_init(layout_t *lay, const glew_config_t *cfg)
{
    lay->cfg = cfg;
    lay->self_index = -1;

    for (int i = 0; i < cfg->machine_count; i++) {
        if (strcmp(cfg->machines[i].name, cfg->self_name) == 0) {
            lay->self_index = i;
            break;
        }
    }

    if (lay->self_index < 0) {
        LOG_ERR("self '%s' not found in layout machines", cfg->self_name);
        return -1;
    }

    LOG_DBG("layout: self='%s' at index %d of %d machines",
            cfg->self_name, lay->self_index, cfg->machine_count);
    return 0;
}

int layout_resolve(const layout_t *lay, direction_t dir,
                   int *target_idx, direction_t *enter_dir)
{
    int idx = lay->self_index;

    switch (dir) {
    case DIR_LEFT:
        if (idx <= 0)
            return -1;
        *target_idx = idx - 1;
        *enter_dir = DIR_RIGHT;
        return 0;

    case DIR_RIGHT:
        if (idx >= lay->cfg->machine_count - 1)
            return -1;
        *target_idx = idx + 1;
        *enter_dir = DIR_LEFT;
        return 0;

    case DIR_UP:
    case DIR_DOWN:
        /* vertical layout not yet supported across machines */
        return -1;
    }

    return -1;
}

const machine_t *layout_machine(const layout_t *lay, int idx)
{
    if (idx < 0 || idx >= lay->cfg->machine_count)
        return NULL;
    return &lay->cfg->machines[idx];
}

const machine_t *layout_self(const layout_t *lay)
{
    return layout_machine(lay, lay->self_index);
}
