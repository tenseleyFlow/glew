#include "driver.h"
#include "../log.h"
#include "../../deps/cJSON.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

static int sock_fd = -1;
static int focus_already_moved = 0;
static char gar_sock_path[256];

/* ── Connection management ──────────────────────────────────────── */

static int gar_connect(void)
{
    if (sock_fd >= 0) {
        close(sock_fd);
        sock_fd = -1;
    }

    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        LOG_ERR("gar: socket(): %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", gar_sock_path);

    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        LOG_ERR("gar: connect(%s): %s", gar_sock_path, strerror(errno));
        close(sock_fd);
        sock_fd = -1;
        return -1;
    }

    LOG_INFO("gar: connected to %s", gar_sock_path);
    return 0;
}

/* ── IPC protocol (newline-delimited JSON, gar native) ──────────── */

static cJSON *gar_send_and_recv(const char *json_str)
{
    size_t len = strlen(json_str);
    ssize_t w = write(sock_fd, json_str, len);
    if (w == (ssize_t)len)
        w = write(sock_fd, "\n", 1);

    if (w <= 0)
        return NULL;

    char buf[8192];
    size_t pos = 0;
    while (pos < sizeof(buf) - 1) {
        ssize_t r = read(sock_fd, buf + pos, 1);
        if (r <= 0)
            return NULL;
        if (buf[pos] == '\n')
            break;
        pos++;
    }
    buf[pos] = '\0';

    return cJSON_Parse(buf);
}

static cJSON *gar_request(const char *command, cJSON *args)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "command", command);
    if (args)
        cJSON_AddItemToObject(req, "args", args);
    else
        cJSON_AddItemToObject(req, "args", cJSON_CreateObject());

    char *json = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);

    cJSON *resp = gar_send_and_recv(json);
    if (resp) {
        free(json);
        return resp;
    }

    LOG_WARN("gar: request failed, reconnecting");
    if (gar_connect() != 0) {
        free(json);
        return NULL;
    }

    resp = gar_send_and_recv(json);
    free(json);
    return resp;
}

static int gar_focus(const char *dir_str)
{
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "direction", dir_str);

    cJSON *resp = gar_request("focus", args);
    if (!resp) return -1;

    cJSON *success = cJSON_GetObjectItem(resp, "success");
    int ok = success && cJSON_IsTrue(success);
    cJSON_Delete(resp);
    return ok ? 0 : -1;
}

static int gar_focus_monitor(const char *target)
{
    cJSON *args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "target", target);

    cJSON *resp = gar_request("focus_monitor", args);
    if (!resp) return -1;

    cJSON *success = cJSON_GetObjectItem(resp, "success");
    int ok = success && cJSON_IsTrue(success);
    cJSON_Delete(resp);
    return ok ? 0 : -1;
}

static int get_focused_id(void)
{
    cJSON *resp = gar_request("get_focused", NULL);
    if (!resp) return -1;

    cJSON *data = cJSON_GetObjectItem(resp, "data");
    if (!data) { cJSON_Delete(resp); return -1; }

    cJSON *id = cJSON_GetObjectItem(data, "id");
    int result = (id && cJSON_IsNumber(id)) ? id->valueint : -1;

    cJSON_Delete(resp);
    return result;
}

/* ── Driver interface ───────────────────────────────────────────── */

static int gar_drv_init(const char *socket_path)
{
    if (socket_path) {
        snprintf(gar_sock_path, sizeof(gar_sock_path), "%s", socket_path);
    } else {
        const char *xdg = getenv("XDG_RUNTIME_DIR");
        if (xdg)
            snprintf(gar_sock_path, sizeof(gar_sock_path), "%s/gar.sock", xdg);
        else
            snprintf(gar_sock_path, sizeof(gar_sock_path), "/tmp/gar.sock");
    }

    return gar_connect();
}

static void gar_drv_shutdown(void)
{
    if (sock_fd >= 0) {
        close(sock_fd);
        sock_fd = -1;
    }
}

static int gar_drv_can_focus(direction_t dir)
{
    focus_already_moved = 0;

    int before = get_focused_id();
    if (before < 0)
        return 0;

    gar_focus(direction_str(dir));

    int after = get_focused_id();
    if (after < 0)
        return 0;

    if (before != after) {
        focus_already_moved = 1;
        return 1;
    }
    return 0;
}

static int gar_drv_do_focus(direction_t dir)
{
    if (focus_already_moved) {
        focus_already_moved = 0;
        return 0;
    }
    return gar_focus(direction_str(dir));
}

static int gar_drv_focus_edge(direction_t dir)
{
    const char *mon_dir = (dir == DIR_LEFT || dir == DIR_UP) ? "left" : "right";

    /* first: navigate to the edge monitor */
    for (int i = 0; i < 8; i++) {
        if (gar_focus_monitor(mon_dir) != 0)
            break;
    }

    /* then: navigate to the edge window within that monitor */
    int prev = get_focused_id();
    if (prev < 0) return -1;

    for (int i = 0; i < 64; i++) {
        gar_focus(direction_str(dir));
        int cur = get_focused_id();
        if (cur == prev)
            return 0;
        prev = cur;
    }
    return 0;
}

static int gar_drv_dispatch_action(const char *action)
{
    if (strcmp(action, "close") == 0) {
        cJSON *resp = gar_request("close", NULL);
        if (!resp) return -1;
        cJSON_Delete(resp);
        return 0;
    }
    if (strcmp(action, "equalize") == 0) {
        cJSON *resp = gar_request("equalize", NULL);
        if (!resp) return -1;
        cJSON_Delete(resp);
        return 0;
    }
    if (strcmp(action, "toggle-floating") == 0) {
        cJSON *resp = gar_request("toggle_floating", NULL);
        if (!resp) return -1;
        cJSON_Delete(resp);
        return 0;
    }
    if (strcmp(action, "reload") == 0) {
        cJSON *resp = gar_request("reload", NULL);
        if (!resp) return -1;
        cJSON_Delete(resp);
        return 0;
    }
    /* exec and spawn-terminal — route through gar's exec IPC */
    if (strncmp(action, "exec ", 5) == 0) {
        cJSON *args = cJSON_CreateObject();
        cJSON_AddStringToObject(args, "command", action + 5);
        cJSON *resp = gar_request("exec", args);
        if (!resp) return -1;
        cJSON_Delete(resp);
        return 0;
    }
    if (strcmp(action, "spawn-terminal") == 0) {
        cJSON *args = cJSON_CreateObject();
        cJSON_AddStringToObject(args, "command", "garterm");
        cJSON *resp = gar_request("exec", args);
        if (!resp) return -1;
        cJSON_Delete(resp);
        return 0;
    }

    /* "workspace N" */
    if (strncmp(action, "workspace ", 10) == 0) {
        int n = atoi(action + 10);
        if (n < 1) return -1;
        cJSON *args = cJSON_CreateObject();
        cJSON_AddNumberToObject(args, "number", n);
        cJSON *resp = gar_request("workspace", args);
        if (!resp) return -1;
        cJSON_Delete(resp);
        return 0;
    }

    /* "resize left|right|up|down" */
    if (strncmp(action, "resize ", 7) == 0) {
        cJSON *args = cJSON_CreateObject();
        cJSON_AddStringToObject(args, "direction", action + 7);
        cJSON *resp = gar_request("resize", args);
        if (!resp) return -1;
        cJSON_Delete(resp);
        return 0;
    }

    /* "move left|right|up|down" or "swap left|right|up|down" */
    if (strncmp(action, "move ", 5) == 0 || strncmp(action, "swap ", 5) == 0) {
        const char *dir_str = action + (action[0] == 'm' ? 5 : 5);
        cJSON *args = cJSON_CreateObject();
        cJSON_AddStringToObject(args, "direction", dir_str);
        cJSON *resp = gar_request("swap", args);
        if (!resp) return -1;
        cJSON_Delete(resp);
        return 0;
    }

    /* "move-to-workspace N" */
    if (strncmp(action, "move-to-workspace ", 18) == 0) {
        int n = atoi(action + 18);
        if (n < 1) return -1;
        cJSON *args = cJSON_CreateObject();
        cJSON_AddNumberToObject(args, "number", n);
        cJSON *resp = gar_request("move_to_workspace", args);
        if (!resp) return -1;
        cJSON_Delete(resp);
        return 0;
    }

    return -1;
}

const wm_driver_t gar_driver = {
    .name            = "gar",
    .init            = gar_drv_init,
    .shutdown        = gar_drv_shutdown,
    .can_focus       = gar_drv_can_focus,
    .do_focus        = gar_drv_do_focus,
    .focus_edge      = gar_drv_focus_edge,
    .dispatch_action = gar_drv_dispatch_action,
};
