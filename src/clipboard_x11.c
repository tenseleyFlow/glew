#ifndef __APPLE__

#include "clipboard.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *clipboard_read(void)
{
    FILE *proc = popen("xclip -selection clipboard -o 2>/dev/null", "r");
    if (!proc) {
        proc = popen("xsel --clipboard --output 2>/dev/null", "r");
        if (!proc) return NULL;
    }

    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) { pclose(proc); return NULL; }

    size_t n;
    while ((n = fread(buf + len, 1, cap - len - 1, proc)) > 0) {
        len += n;
        if (len + 1 >= cap) {
            if (cap >= 1024 * 1024) break;
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) break;
            buf = nb;
        }
    }
    pclose(proc);

    if (len == 0) { free(buf); return NULL; }
    buf[len] = '\0';
    return buf;
}

int clipboard_write(const char *text, size_t len)
{
    FILE *proc = popen("xclip -selection clipboard 2>/dev/null", "w");
    if (!proc) {
        proc = popen("xsel --clipboard --input 2>/dev/null", "w");
        if (!proc) {
            LOG_ERR("clipboard: xclip/xsel not available");
            return -1;
        }
    }
    fwrite(text, 1, len, proc);
    int rc = pclose(proc);
    return rc == 0 ? 0 : -1;
}

#endif
