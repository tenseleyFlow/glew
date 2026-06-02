#include "config.h"
#include "log.h"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static void usage(void)
{
    fprintf(stderr,
        "usage: glewd [options]\n"
        "\n"
        "options:\n"
        "  -c, --config <path>   config file (default: ~/.config/glew/glew.toml)\n"
        "  -f, --foreground      run in foreground (default)\n"
        "  -v, --verbose         enable debug logging\n"
        "  -V, --version         print version\n"
        "  -h, --help            show this help\n"
    );
}

static const struct option longopts[] = {
    { "config",     required_argument, NULL, 'c' },
    { "foreground", no_argument,       NULL, 'f' },
    { "verbose",    no_argument,       NULL, 'v' },
    { "version",    no_argument,       NULL, 'V' },
    { "help",       no_argument,       NULL, 'h' },
    { NULL, 0, NULL, 0 },
};

int main(int argc, char **argv)
{
    char config_path[512] = {0};
    int verbose = 0;

    int ch;
    while ((ch = getopt_long(argc, argv, "c:fvVh", longopts, NULL)) != -1) {
        switch (ch) {
        case 'c':
            snprintf(config_path, sizeof(config_path), "%s", optarg);
            break;
        case 'f':
            break;
        case 'v':
            verbose = 1;
            break;
        case 'V':
            printf("glewd %s\n", GLEW_VERSION_STR);
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

    if (!config_path[0])
        config_default_path(config_path, sizeof(config_path));

    glew_config_t cfg;
    if (config_load(config_path, &cfg) != 0)
        return 1;

    if (verbose)
        config_dump(&cfg);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    LOG_INFO("glewd %s starting (self=%s, wm=%s, port=%d)",
             GLEW_VERSION_STR, cfg.self_name, cfg.self_wm, cfg.port);

    /* TODO: sprint 2 — libuv event loop, TCP server, peer connections */
    while (g_running) {
        /* placeholder — will be replaced by uv_run() */
        pause();
    }

    LOG_INFO("glewd shutting down");
    return 0;
}
