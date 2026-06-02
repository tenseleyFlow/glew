#ifndef GLEW_KEYMAP_MACOS_H
#define GLEW_KEYMAP_MACOS_H

#ifdef __APPLE__

#include <CoreGraphics/CoreGraphics.h>
#include <stdint.h>

uint32_t  macos_keycode_to_keysym(CGKeyCode kc);
CGKeyCode keysym_to_macos_keycode(uint32_t keysym);

#endif /* __APPLE__ */
#endif
