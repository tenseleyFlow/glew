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

/* ── IPC protocol (newline-delimited JSON, gar native) ──────────── */

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

    size_t len = strlen(json);
    ssize_t w = write(sock_fd, json, len);
    if (w == (ssize_t)len)
        w = write(sock_fd, "\n", 1);
    free(json);

    if (w <= 0) {
        LOG_ERR("gar: send failed: %s", strerror(errno));
        return NULL;
    }

    char buf[8192];
    size_t pos = 0;
    while (pos < sizeof(buf) - 1) {
        ssize_t r = read(sock_fd, buf + pos, 1);
        if (r <= 0) {
            LOG_ERR("gar: recv failed");
            return NULL;
        }
        if (buf[pos] == '\n')
            break;
        pos++;
    }
    buf[pos] = '\0';

    return cJSON_Parse(buf);
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
    const char *path = socket_path;
    char auto_path[256];

    if (!path) {
        const char *xdg = getenv("XDG_RUNTIME_DIR");
        if (xdg) {
            snprintf(auto_path, sizeof(auto_path), "%s/gar.sock", xdg);
        } else {
            snprintf(auto_path, sizeof(auto_path), "/tmp/gar.sock");
        }
        path = auto_path;
    }

    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        LOG_ERR("gar: socket(): %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        LOG_ERR("gar: connect(%s): %s", path, strerror(errno));
        close(sock_fd);
        sock_fd = -1;
        return -1;
    }

    LOG_INFO("gar: connected to %s", path);
    return 0;
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

const wm_driver_t gar_driver = {
    .name       = "gar",
    .init       = gar_drv_init,
    .shutdown   = gar_drv_shutdown,
    .can_focus  = gar_drv_can_focus,
    .do_focus   = gar_drv_do_focus,
    .focus_edge = gar_drv_focus_edge,
};
