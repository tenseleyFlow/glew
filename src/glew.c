#include "config.h"
#include "log.h"
#include "version.h"
#include "net/proto.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/socket.h>
#include <sys/un.h>

static void usage(void)
{
    fprintf(stderr,
        "usage: glew [options] <command> [args]\n"
        "\n"
        "commands:\n"
        "  focus <left|right|up|down>   move focus in direction\n"
        "  status                       show connection status\n"
        "\n"
        "options:\n"
        "  -c, --config <path>   config file (default: ~/.config/glew/glew.toml)\n"
        "  -v, --verbose         enable debug logging\n"
        "  -V, --version         print version\n"
        "  -h, --help            show this help\n"
    );
}

static const struct option longopts[] = {
    { "config",  required_argument, NULL, 'c' },
    { "verbose", no_argument,       NULL, 'v' },
    { "version", no_argument,       NULL, 'V' },
    { "help",    no_argument,       NULL, 'h' },
    { NULL, 0, NULL, 0 },
};

static char *get_socket_path(const glew_config_t *cfg)
{
    static char path[256];
    const char *xdg = getenv("XDG_RUNTIME_DIR");
    if (xdg)
        snprintf(path, sizeof(path), "%s/glew.sock", xdg);
    else
        snprintf(path, sizeof(path), "/tmp/glew-%s.sock", cfg->self_name);
    return path;
}

static int connect_daemon(const glew_config_t *cfg)
{
    char *path = get_socket_path(cfg);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERR("socket(): %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        LOG_ERR("cannot connect to glewd at %s: %s", path, strerror(errno));
        LOG_ERR("is glewd running?");
        close(fd);
        return -1;
    }

    return fd;
}

static int send_msg(int fd, const glew_msg_t *msg)
{
    char *buf = NULL;
    size_t len = msg_serialize(msg, &buf);
    if (!len) return -1;

    ssize_t w = 0;
    size_t sent = 0;
    while (sent < len) {
        w = write(fd, buf + sent, len - sent);
        if (w <= 0) { free(buf); return -1; }
        sent += (size_t)w;
    }
    free(buf);
    return 0;
}

static int recv_msg(int fd, glew_msg_t *msg)
{
    char hdr[PROTO_HEADER_SIZE];
    size_t got = 0;
    while (got < PROTO_HEADER_SIZE) {
        ssize_t r = read(fd, hdr + got, PROTO_HEADER_SIZE - got);
        if (r <= 0) return -1;
        got += (size_t)r;
    }

    int32_t plen = proto_read_len(hdr, PROTO_HEADER_SIZE);
    if (plen <= 0 || plen > 65536) return -1;

    char *payload = malloc((size_t)plen);
    if (!payload) return -1;

    got = 0;
    while (got < (size_t)plen) {
        ssize_t r = read(fd, payload + got, (size_t)plen - got);
        if (r <= 0) { free(payload); return -1; }
        got += (size_t)r;
    }

    int rc = msg_parse(payload, (size_t)plen, msg);
    free(payload);
    return rc;
}

static int cmd_focus(const glew_config_t *cfg, const char *dir_str)
{
    int fd = connect_daemon(cfg);
    if (fd < 0) return 1;

    glew_msg_t req = { .type = MSG_FOCUS };
    snprintf(req.focus.direction, sizeof(req.focus.direction), "%s", dir_str);

    if (send_msg(fd, &req) != 0) {
        LOG_ERR("failed to send focus command");
        close(fd);
        return 1;
    }

    glew_msg_t reply;
    if (recv_msg(fd, &reply) != 0) {
        LOG_ERR("failed to read response");
        close(fd);
        return 1;
    }

    close(fd);

    if (reply.type != MSG_FOCUS_RESULT) {
        LOG_ERR("unexpected response type");
        return 1;
    }

    if (reply.focus_result.crossed) {
        LOG_INFO("focus crossed to %s", reply.focus_result.target);
        return 0;
    }

    return 0;
}

int main(int argc, char **argv)
{
    char config_path[512] = {0};
    int verbose = 0;

    int ch;
    while ((ch = getopt_long(argc, argv, "c:vVh", longopts, NULL)) != -1) {
        switch (ch) {
        case 'c':
            snprintf(config_path, sizeof(config_path), "%s", optarg);
            break;
        case 'v':
            verbose = 1;
            break;
        case 'V':
            printf("glew %s\n", GLEW_VERSION_STR);
            return 0;
        case 'h':
            usage();
            return 0;
        default:
            usage();
            return 1;
        }
    }

    if (verbose)
        log_set_level(LOG_DEBUG);

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        usage();
        return 1;
    }

    if (!config_path[0])
        config_default_path(config_path, sizeof(config_path));

    glew_config_t cfg;
    if (config_load(config_path, &cfg) != 0)
        return 1;

    const char *cmd = argv[0];

    if (strcmp(cmd, "focus") == 0) {
        if (argc < 2) {
            fprintf(stderr, "usage: glew focus <left|right|up|down>\n");
            return 1;
        }
        const char *dir = argv[1];
        if (strcmp(dir, "left") != 0 && strcmp(dir, "right") != 0 &&
            strcmp(dir, "up") != 0 && strcmp(dir, "down") != 0) {
            fprintf(stderr, "invalid direction: %s\n", dir);
            return 1;
        }
        return cmd_focus(&cfg, dir);

    } else if (strcmp(cmd, "status") == 0) {
        LOG_INFO("status (not yet implemented)");
        return 0;

    } else {
        fprintf(stderr, "unknown command: %s\n", cmd);
        usage();
        return 1;
    }
}
