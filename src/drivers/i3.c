#include "driver.h"
#include "../log.h"
#include "../../deps/cJSON.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define I3_MAGIC       "i3-ipc"
#define I3_MAGIC_LEN   6
#define I3_HEADER_LEN  14

#define I3_MSG_COMMAND       0
#define I3_MSG_GET_WORKSPACES 1
#define I3_MSG_GET_TREE      4
#define I3_MSG_GET_VERSION   7

static int sock_fd = -1;

/* ── IPC protocol ───────────────────────────────────────────────── */

static int i3_send(int fd, uint32_t type, const char *payload, uint32_t len)
{
    uint8_t header[I3_HEADER_LEN];
    memcpy(header, I3_MAGIC, I3_MAGIC_LEN);
    memcpy(header + 6, &len, 4);
    memcpy(header + 10, &type, 4);

    if (write(fd, header, I3_HEADER_LEN) != I3_HEADER_LEN)
        return -1;
    if (len > 0 && write(fd, payload, len) != (ssize_t)len)
        return -1;
    return 0;
}

static char *read_exact(int fd, size_t n)
{
    char *buf = malloc(n + 1);
    if (!buf) return NULL;

    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, buf + got, n - got);
        if (r <= 0) { free(buf); return NULL; }
        got += r;
    }
    buf[n] = '\0';
    return buf;
}

static char *i3_recv(int fd, uint32_t *out_type)
{
    char header[I3_HEADER_LEN];
    size_t got = 0;
    while (got < I3_HEADER_LEN) {
        ssize_t r = read(fd, header + got, I3_HEADER_LEN - got);
        if (r <= 0) return NULL;
        got += r;
    }

    if (memcmp(header, I3_MAGIC, I3_MAGIC_LEN) != 0)
        return NULL;

    uint32_t len, type;
    memcpy(&len, header + 6, 4);
    memcpy(&type, header + 10, 4);

    if (out_type)
        *out_type = type;

    return read_exact(fd, len);
}

static cJSON *i3_request(uint32_t type, const char *payload)
{
    uint32_t plen = payload ? (uint32_t)strlen(payload) : 0;
    if (i3_send(sock_fd, type, payload, plen) != 0) {
        LOG_ERR("i3: send failed");
        return NULL;
    }

    uint32_t resp_type;
    char *resp = i3_recv(sock_fd, &resp_type);
    if (!resp) {
        LOG_ERR("i3: recv failed");
        return NULL;
    }

    cJSON *json = cJSON_Parse(resp);
    free(resp);
    return json;
}

static int i3_command(const char *cmd)
{
    cJSON *resp = i3_request(I3_MSG_COMMAND, cmd);
    if (!resp)
        return -1;

    int ok = 0;
    if (cJSON_IsArray(resp)) {
        cJSON *first = cJSON_GetArrayItem(resp, 0);
        cJSON *s = cJSON_GetObjectItem(first, "success");
        if (s && cJSON_IsTrue(s))
            ok = 1;
    }
    cJSON_Delete(resp);
    return ok ? 0 : -1;
}

/* ── Tree walking ───────────────────────────────────────────────── */

typedef enum {
    FOCUS_NOT_HERE,
    FOCUS_CAN_MOVE,
    FOCUS_AT_EDGE,
} focus_check_t;

static int node_id(cJSON *node)
{
    cJSON *id = cJSON_GetObjectItem(node, "id");
    return id ? (int)id->valuedouble : 0;
}

static const char *node_layout(cJSON *node)
{
    cJSON *l = cJSON_GetObjectItem(node, "layout");
    return (l && cJSON_IsString(l)) ? l->valuestring : "";
}

static const char *node_type(cJSON *node)
{
    cJSON *t = cJSON_GetObjectItem(node, "type");
    return (t && cJSON_IsString(t)) ? t->valuestring : "";
}

static int node_is_focused(cJSON *node)
{
    cJSON *f = cJSON_GetObjectItem(node, "focused");
    return f && cJSON_IsTrue(f);
}

static cJSON *node_children(cJSON *node)
{
    return cJSON_GetObjectItem(node, "nodes");
}

/*
 * Recursive edge check within a single output's container tree.
 * Returns whether the focused leaf can move in `dir`.
 */
static focus_check_t check_subtree(cJSON *node, direction_t dir)
{
    cJSON *nodes = node_children(node);
    int ncount = nodes ? cJSON_GetArraySize(nodes) : 0;

    if (ncount == 0) {
        return node_is_focused(node) ? FOCUS_AT_EDGE : FOCUS_NOT_HERE;
    }

    int focused_idx = -1;
    focus_check_t child_result = FOCUS_NOT_HERE;

    for (int i = 0; i < ncount; i++) {
        focus_check_t r = check_subtree(cJSON_GetArrayItem(nodes, i), dir);
        if (r != FOCUS_NOT_HERE) {
            focused_idx = i;
            child_result = r;
            break;
        }
    }

    /* also check floating_nodes */
    if (focused_idx < 0) {
        cJSON *floating = cJSON_GetObjectItem(node, "floating_nodes");
        if (floating) {
            int fc = cJSON_GetArraySize(floating);
            for (int i = 0; i < fc; i++) {
                focus_check_t r = check_subtree(cJSON_GetArrayItem(floating, i), dir);
                if (r != FOCUS_NOT_HERE)
                    return r;
            }
        }
        return FOCUS_NOT_HERE;
    }

    if (child_result == FOCUS_CAN_MOVE)
        return FOCUS_CAN_MOVE;

    /* child is at its edge — can this parent provide a sibling? */
    const char *layout = node_layout(node);
    int is_horiz = (dir == DIR_LEFT || dir == DIR_RIGHT);
    int layout_matches = 0;

    if (is_horiz && strcmp(layout, "splith") == 0)   layout_matches = 1;
    if (!is_horiz && strcmp(layout, "splitv") == 0)   layout_matches = 1;
    if (is_horiz && strcmp(layout, "tabbed") == 0)    layout_matches = 1;
    if (!is_horiz && strcmp(layout, "stacked") == 0)  layout_matches = 1;

    if (!layout_matches)
        return FOCUS_AT_EDGE;

    if (dir == DIR_LEFT || dir == DIR_UP) {
        if (focused_idx > 0)
            return FOCUS_CAN_MOVE;
    } else {
        if (focused_idx < ncount - 1)
            return FOCUS_CAN_MOVE;
    }

    return FOCUS_AT_EDGE;
}

/*
 * Find the output (monitor) containing the focused window.
 * Returns the output node, or NULL.
 */
static cJSON *find_focused_output(cJSON *root)
{
    cJSON *outputs = node_children(root);
    if (!outputs) return NULL;

    int n = cJSON_GetArraySize(outputs);
    for (int i = 0; i < n; i++) {
        cJSON *out = cJSON_GetArrayItem(outputs, i);
        const char *name = cJSON_GetObjectItem(out, "name")->valuestring;
        if (strcmp(name, "__i3") == 0)
            continue;

        /* check if focus path leads into this output */
        cJSON *content = NULL;
        cJSON *out_nodes = node_children(out);
        if (!out_nodes) continue;
        int onc = cJSON_GetArraySize(out_nodes);
        for (int j = 0; j < onc; j++) {
            cJSON *c = cJSON_GetArrayItem(out_nodes, j);
            if (strcmp(node_type(c), "con") == 0 ||
                strcmp(node_type(c), "workspace") == 0) {
                content = c;
                break;
            }
        }

        if (!content) continue;

        if (check_subtree(content, DIR_LEFT) != FOCUS_NOT_HERE)
            return out;
    }
    return NULL;
}

/*
 * Check if there's an adjacent output in the given direction.
 * Uses output rect geometry for spatial adjacency.
 */
static int has_adjacent_output(cJSON *root, cJSON *current_output, direction_t dir)
{
    cJSON *cur_rect = cJSON_GetObjectItem(current_output, "rect");
    if (!cur_rect) return 0;

    int cx = cJSON_GetObjectItem(cur_rect, "x")->valueint;
    int cy = cJSON_GetObjectItem(cur_rect, "y")->valueint;

    cJSON *outputs = node_children(root);
    int n = cJSON_GetArraySize(outputs);

    for (int i = 0; i < n; i++) {
        cJSON *out = cJSON_GetArrayItem(outputs, i);
        if (out == current_output) continue;

        const char *name = cJSON_GetObjectItem(out, "name")->valuestring;
        if (strcmp(name, "__i3") == 0) continue;

        cJSON *r = cJSON_GetObjectItem(out, "rect");
        if (!r) continue;

        int ox = cJSON_GetObjectItem(r, "x")->valueint;
        int oy = cJSON_GetObjectItem(r, "y")->valueint;

        switch (dir) {
        case DIR_LEFT:  if (ox < cx) return 1; break;
        case DIR_RIGHT: if (ox > cx) return 1; break;
        case DIR_UP:    if (oy < cy) return 1; break;
        case DIR_DOWN:  if (oy > cy) return 1; break;
        }
    }
    return 0;
}

/* ── Find edge-most leaf ────────────────────────────────────────── */

/*
 * Find the leaf node at the extreme edge of a subtree.
 * For DIR_RIGHT: find the rightmost leaf in the rightmost branch.
 */
static cJSON *find_edge_leaf(cJSON *node, direction_t dir)
{
    cJSON *nodes = node_children(node);
    int ncount = nodes ? cJSON_GetArraySize(nodes) : 0;

    if (ncount == 0)
        return node;

    const char *layout = node_layout(node);
    int is_horiz = (dir == DIR_LEFT || dir == DIR_RIGHT);
    int layout_matches = 0;

    if (is_horiz && strcmp(layout, "splith") == 0)   layout_matches = 1;
    if (!is_horiz && strcmp(layout, "splitv") == 0)   layout_matches = 1;
    if (is_horiz && strcmp(layout, "tabbed") == 0)    layout_matches = 1;
    if (!is_horiz && strcmp(layout, "stacked") == 0)  layout_matches = 1;

    int pick;
    if (layout_matches) {
        pick = (dir == DIR_RIGHT || dir == DIR_DOWN) ? ncount - 1 : 0;
    } else {
        /* layout doesn't split in this direction — follow focus order */
        cJSON *focus = cJSON_GetObjectItem(node, "focus");
        if (focus && cJSON_GetArraySize(focus) > 0) {
            int first_id = (int)cJSON_GetArrayItem(focus, 0)->valuedouble;
            for (int i = 0; i < ncount; i++) {
                if (node_id(cJSON_GetArrayItem(nodes, i)) == first_id) {
                    pick = i;
                    goto found;
                }
            }
        }
        pick = 0;
    }

found:
    return find_edge_leaf(cJSON_GetArrayItem(nodes, pick), dir);
}

/*
 * Find the edge-most output in a direction.
 */
static cJSON *find_edge_output(cJSON *root, direction_t dir)
{
    cJSON *outputs = node_children(root);
    int n = cJSON_GetArraySize(outputs);

    cJSON *best = NULL;
    int best_val = (dir == DIR_LEFT || dir == DIR_UP) ? INT32_MAX : INT32_MIN;

    for (int i = 0; i < n; i++) {
        cJSON *out = cJSON_GetArrayItem(outputs, i);
        const char *name = cJSON_GetObjectItem(out, "name")->valuestring;
        if (strcmp(name, "__i3") == 0) continue;

        cJSON *r = cJSON_GetObjectItem(out, "rect");
        if (!r) continue;

        int val = (dir == DIR_LEFT || dir == DIR_RIGHT)
            ? cJSON_GetObjectItem(r, "x")->valueint
            : cJSON_GetObjectItem(r, "y")->valueint;

        int better = 0;
        switch (dir) {
        case DIR_RIGHT: case DIR_DOWN: better = (val > best_val); break;
        case DIR_LEFT:  case DIR_UP:   better = (val < best_val); break;
        }

        if (!best || better) {
            best = out;
            best_val = val;
        }
    }
    return best;
}

/*
 * Find the content node within an output.
 */
static cJSON *output_content(cJSON *output)
{
    cJSON *nodes = node_children(output);
    if (!nodes) return NULL;
    int n = cJSON_GetArraySize(nodes);
    for (int i = 0; i < n; i++) {
        cJSON *c = cJSON_GetArrayItem(nodes, i);
        const char *t = node_type(c);
        if (strcmp(t, "con") == 0 || strcmp(t, "workspace") == 0)
            return c;
    }
    return NULL;
}

/* ── Driver interface ───────────────────────────────────────────── */

static char *detect_socket_path(void)
{
    const char *env = getenv("I3SOCK");
    if (env) return strdup(env);

    env = getenv("SWAYSOCK");
    if (env) return strdup(env);

    /* gar i3-compat socket */
    char gar_path[256];
    snprintf(gar_path, sizeof(gar_path), "/run/user/%d/gar-i3.sock", getuid());
    if (access(gar_path, F_OK) == 0)
        return strdup(gar_path);

    FILE *fp = popen("i3 --get-socketpath 2>/dev/null", "r");
    if (fp) {
        char buf[512];
        if (fgets(buf, sizeof(buf), fp)) {
            pclose(fp);
            buf[strcspn(buf, "\n")] = '\0';
            return strdup(buf);
        }
        pclose(fp);
    }
    return NULL;
}

static int i3_drv_init(const char *socket_path)
{
    char *path = NULL;
    if (socket_path) {
        path = strdup(socket_path);
    } else {
        path = detect_socket_path();
    }

    if (!path) {
        LOG_ERR("i3: cannot determine socket path");
        return -1;
    }

    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        LOG_ERR("i3: socket(): %s", strerror(errno));
        free(path);
        return -1;
    }

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        LOG_ERR("i3: connect(%s): %s", path, strerror(errno));
        close(sock_fd);
        sock_fd = -1;
        free(path);
        return -1;
    }

    LOG_INFO("i3: connected to %s", path);
    free(path);
    return 0;
}

static void i3_drv_shutdown(void)
{
    if (sock_fd >= 0) {
        close(sock_fd);
        sock_fd = -1;
    }
}

static int i3_drv_can_focus(direction_t dir)
{
    cJSON *tree = i3_request(I3_MSG_GET_TREE, NULL);
    if (!tree) return 0;

    cJSON *focused_output = find_focused_output(tree);
    if (!focused_output) {
        cJSON_Delete(tree);
        return 0;
    }

    cJSON *content = output_content(focused_output);
    if (!content) {
        cJSON_Delete(tree);
        return 0;
    }

    focus_check_t result = check_subtree(content, dir);

    int can = 0;
    if (result == FOCUS_CAN_MOVE) {
        can = 1;
    } else if (result == FOCUS_AT_EDGE) {
        can = has_adjacent_output(tree, focused_output, dir);
    }

    cJSON_Delete(tree);
    return can;
}

static int i3_drv_do_focus(direction_t dir)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "focus %s", direction_str(dir));
    return i3_command(cmd);
}

static int i3_drv_focus_edge(direction_t dir)
{
    cJSON *tree = i3_request(I3_MSG_GET_TREE, NULL);
    if (!tree) return -1;

    cJSON *output = find_edge_output(tree, dir);
    if (!output) {
        cJSON_Delete(tree);
        return -1;
    }

    cJSON *content = output_content(output);
    if (!content) {
        cJSON_Delete(tree);
        return -1;
    }

    /* find focused workspace within this output's content */
    cJSON *workspace = NULL;
    cJSON *cnodes = node_children(content);
    if (cnodes) {
        cJSON *focus_order = cJSON_GetObjectItem(content, "focus");
        if (focus_order && cJSON_GetArraySize(focus_order) > 0) {
            int first_id = (int)cJSON_GetArrayItem(focus_order, 0)->valuedouble;
            int wc = cJSON_GetArraySize(cnodes);
            for (int i = 0; i < wc; i++) {
                if (node_id(cJSON_GetArrayItem(cnodes, i)) == first_id) {
                    workspace = cJSON_GetArrayItem(cnodes, i);
                    break;
                }
            }
        }
        if (!workspace && cJSON_GetArraySize(cnodes) > 0)
            workspace = cJSON_GetArrayItem(cnodes, 0);
    }

    if (!workspace) {
        cJSON_Delete(tree);
        return -1;
    }

    cJSON *leaf = find_edge_leaf(workspace, dir);
    if (!leaf) {
        cJSON_Delete(tree);
        return -1;
    }

    int id = node_id(leaf);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "[con_id=%d] focus", id);

    cJSON_Delete(tree);
    return i3_command(cmd);
}

static int i3_drv_dispatch_action(const char *action)
{
    if (strcmp(action, "spawn-terminal") == 0)
        return i3_command("exec $TERMINAL");
    if (strcmp(action, "close") == 0)
        return i3_command("kill");
    if (strcmp(action, "reload") == 0)
        return i3_command("reload");

    /* "workspace N" → i3 "workspace N" (same syntax) */
    if (strncmp(action, "workspace ", 10) == 0) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "workspace %s", action + 10);
        return i3_command(cmd);
    }

    /* "move-to-workspace N" → i3 "move container to workspace N" */
    if (strncmp(action, "move-to-workspace ", 18) == 0) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "move container to workspace %s", action + 18);
        return i3_command(cmd);
    }

    return -1;
}

const wm_driver_t i3_driver = {
    .name            = "i3",
    .init            = i3_drv_init,
    .shutdown        = i3_drv_shutdown,
    .can_focus       = i3_drv_can_focus,
    .do_focus        = i3_drv_do_focus,
    .focus_edge      = i3_drv_focus_edge,
    .dispatch_action = i3_drv_dispatch_action,
};
