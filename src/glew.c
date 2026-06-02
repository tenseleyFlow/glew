#include "config.h"
#include "log.h"
#include "version.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>

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

    if (verbose)
        config_dump(&cfg);

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
        /* TODO: sprint 1 — connect to glewd, issue focus command */
        LOG_INFO("focus %s (not yet implemented)", dir);
        return 0;

    } else if (strcmp(cmd, "status") == 0) {
        /* TODO: sprint 2 — query glewd for peer status */
        LOG_INFO("status (not yet implemented)");
        return 0;

    } else {
        fprintf(stderr, "unknown command: %s\n", cmd);
        usage();
        return 1;
    }
}
