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

/* ── IPC protocol (newline-delimited JSON) ──────────────────────── */

static cJSON *tarmac_request(const char *command, const char *arg)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "command", command);
    if (arg) {
        cJSON *args = cJSON_AddArrayToObject(req, "args");
        cJSON_AddItemToArray(args, cJSON_CreateString(arg));
    }

    char *json = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);

    size_t len = strlen(json);
    /* send JSON + newline */
    ssize_t w = write(sock_fd, json, len);
    if (w == (ssize_t)len)
        w = write(sock_fd, "\n", 1);
    free(json);

    if (w <= 0) {
        LOG_ERR("tarmac: send failed: %s", strerror(errno));
        return NULL;
    }

    /* read response line */
    char buf[8192];
    size_t pos = 0;
    while (pos < sizeof(buf) - 1) {
        ssize_t r = read(sock_fd, buf + pos, 1);
        if (r <= 0) {
            LOG_ERR("tarmac: recv failed");
            return NULL;
        }
        if (buf[pos] == '\n')
            break;
        pos++;
    }
    buf[pos] = '\0';

    return cJSON_Parse(buf);
}

static int tarmac_command(const char *command, const char *arg)
{
    cJSON *resp = tarmac_request(command, arg);
    if (!resp) return -1;

    cJSON *success = cJSON_GetObjectItem(resp, "success");
    int ok = success && cJSON_IsTrue(success);
    if (!ok) {
        cJSON *err = cJSON_GetObjectItem(resp, "error");
        if (err && cJSON_IsString(err))
            LOG_ERR("tarmac: %s %s: %s", command, arg ? arg : "", err->valuestring);
    }
    cJSON_Delete(resp);
    return ok ? 0 : -1;
}

/* ── Query helpers ──────────────────────────────────────────────── */

/*
 * Get the currently focused window ID, or -1 if none.
 */
static int get_focused_id(void)
{
    cJSON *resp = tarmac_request("get-focused", NULL);
    if (!resp) return -1;

    cJSON *data = cJSON_GetObjectItem(resp, "data");
    if (!data) { cJSON_Delete(resp); return -1; }

    cJSON *id = cJSON_GetObjectItem(data, "id");
    int result = (id && cJSON_IsNumber(id)) ? id->valueint : -1;

    cJSON_Delete(resp);
    return result;
}

/* ── Driver interface ───────────────────────────────────────────── */

static int tarmac_drv_init(const char *socket_path)
{
    const char *path = socket_path;
    char auto_path[256];

    if (!path) {
        const char *env = getenv("TARMAC_SOCKET");
        if (env) {
            path = env;
        } else {
            const char *user = getenv("USER");
            if (!user) user = "unknown";
            snprintf(auto_path, sizeof(auto_path), "/tmp/tarmac-%s.sock", user);
            path = auto_path;
        }
    }

    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        LOG_ERR("tarmac: socket(): %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        LOG_ERR("tarmac: connect(%s): %s", path, strerror(errno));
        close(sock_fd);
        sock_fd = -1;
        return -1;
    }

    LOG_INFO("tarmac: connected to %s", path);
    return 0;
}

static void tarmac_drv_shutdown(void)
{
    if (sock_fd >= 0) {
        close(sock_fd);
        sock_fd = -1;
    }
}

static int tarmac_drv_can_focus(direction_t dir)
{
    focus_already_moved = 0;

    int before = get_focused_id();
    if (before < 0)
        return 0;

    tarmac_command("focus", direction_str(dir));

    int after = get_focused_id();
    if (after < 0)
        return 0;

    if (before != after) {
        focus_already_moved = 1;
        return 1;
    }
    return 0;
}

static int tarmac_drv_do_focus(direction_t dir)
{
    if (focus_already_moved) {
        focus_already_moved = 0;
        return 0;
    }
    return tarmac_command("focus", direction_str(dir));
}

static int tarmac_drv_focus_edge(direction_t dir)
{
    /*
     * Focus the edge-most window by repeatedly focusing in that
     * direction until focus stops moving.
     */
    int prev = get_focused_id();
    if (prev < 0) return -1;

    for (int i = 0; i < 64; i++) {
        tarmac_command("focus", direction_str(dir));
        int cur = get_focused_id();
        if (cur == prev)
            return 0;
        prev = cur;
    }
    return 0;
}

static int tarmac_drv_dispatch_action(const char *action)
{
    if (strcmp(action, "close") == 0)
        return tarmac_command("close", NULL);
    if (strcmp(action, "equalize") == 0)
        return tarmac_command("equalize", NULL);
    if (strcmp(action, "reload") == 0)
        return tarmac_command("reload", NULL);
    if (strcmp(action, "spawn-terminal") == 0)
        return tarmac_command("spawn-terminal", NULL);
    if (strcmp(action, "toggle-floating") == 0)
        return tarmac_command("toggle-floating", NULL);

    /* "move left|right|up|down" or "swap ..." */
    if (strncmp(action, "move ", 5) == 0) {
        return tarmac_command("swap", action + 5);
    }
    if (strncmp(action, "swap ", 5) == 0)
        return tarmac_command("swap", action + 5);

    /* "workspace N" */
    if (strncmp(action, "workspace ", 10) == 0)
        return tarmac_command("workspace", action + 10);

    /* "move-to-workspace N" */
    if (strncmp(action, "move-to-workspace ", 18) == 0)
        return tarmac_command("move-to-workspace", action + 18);

    return -1;
}

const wm_driver_t tarmac_driver = {
    .name             = "tarmac",
    .init             = tarmac_drv_init,
    .shutdown         = tarmac_drv_shutdown,
    .can_focus        = tarmac_drv_can_focus,
    .do_focus         = tarmac_drv_do_focus,
    .focus_edge       = tarmac_drv_focus_edge,
    .dispatch_action  = tarmac_drv_dispatch_action,
};
