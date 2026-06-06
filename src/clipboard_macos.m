#ifdef __APPLE__

#include "clipboard.h"

#import <AppKit/AppKit.h>
#include <stdlib.h>
#include <string.h>

char *clipboard_read(void)
{
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        NSString *str = [pb stringForType:NSPasteboardTypeString];
        if (!str) return NULL;

        const char *utf8 = [str UTF8String];
        if (!utf8) return NULL;

        size_t len = strlen(utf8);
        if (len > 1024 * 1024) return NULL;

        char *result = malloc(len + 1);
        if (!result) return NULL;
        memcpy(result, utf8, len + 1);
        return result;
    }
}

int clipboard_write(const char *text, size_t len)
{
    @autoreleasepool {
        if (len > 1024 * 1024) return -1;

        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        [pb clearContents];

        NSString *str = [[NSString alloc] initWithBytes:text
                                                 length:len
                                               encoding:NSUTF8StringEncoding];
        if (!str) return -1;

        BOOL ok = [pb setString:str forType:NSPasteboardTypeString];
        return ok ? 0 : -1;
    }
}

#endif
