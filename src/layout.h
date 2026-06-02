#ifndef GLEW_LAYOUT_H
#define GLEW_LAYOUT_H

#include "config.h"
#include "drivers/driver.h"

typedef struct {
    const glew_config_t *cfg;
    int self_index;
} layout_t;

int  layout_init(layout_t *lay, const glew_config_t *cfg);

/*
 * Given a direction overflow, return the target machine index
 * and the direction from which focus enters the target.
 * Returns 0 on success (target found), -1 if no neighbor in that direction.
 */
int  layout_resolve(const layout_t *lay, direction_t dir,
                    int *target_idx, direction_t *enter_dir);

const machine_t *layout_machine(const layout_t *lay, int idx);
const machine_t *layout_self(const layout_t *lay);

#endif
