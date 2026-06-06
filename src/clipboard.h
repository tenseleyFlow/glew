#ifndef GLEW_CLIPBOARD_H
#define GLEW_CLIPBOARD_H

#include <stddef.h>

/* Read the system clipboard. Returns malloc'd string or NULL.
 * Caller must free the returned pointer. */
char *clipboard_read(void);

/* Write text to the system clipboard. */
int clipboard_write(const char *text, size_t len);

#endif
