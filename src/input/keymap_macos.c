#ifdef __APPLE__

#include "keymap_macos.h"

/* XKB keysym values (subset) */
#define XK_BackSpace   0xff08
#define XK_Tab         0xff09
#define XK_Return      0xff0d
#define XK_Escape      0xff1b
#define XK_Delete      0xffff
#define XK_Home        0xff50
#define XK_Left        0xff51
#define XK_Up          0xff52
#define XK_Right       0xff53
#define XK_Down        0xff54
#define XK_Page_Up     0xff55
#define XK_Page_Down   0xff56
#define XK_End         0xff57
#define XK_F1          0xffbe
#define XK_F2          0xffbf
#define XK_F3          0xffc0
#define XK_F4          0xffc1
#define XK_F5          0xffc2
#define XK_F6          0xffc3
#define XK_F7          0xffc4
#define XK_F8          0xffc5
#define XK_F9          0xffc6
#define XK_F10         0xffc7
#define XK_F11         0xffc8
#define XK_F12         0xffc9
#define XK_Shift_L     0xffe1
#define XK_Shift_R     0xffe2
#define XK_Control_L   0xffe3
#define XK_Control_R   0xffe4
#define XK_Caps_Lock   0xffe5
#define XK_Alt_L       0xffe9
#define XK_Alt_R       0xffea
#define XK_Super_L     0xffeb
#define XK_Super_R     0xffec
#define XK_Scroll_Lock 0xff14
#define XK_space       0x0020

/* macOS virtual keycodes → XKB keysyms */
static const struct { CGKeyCode mac; uint32_t xk; } keymap[] = {
    { 0x00, 'a' }, { 0x01, 's' }, { 0x02, 'd' }, { 0x03, 'f' },
    { 0x04, 'h' }, { 0x05, 'g' }, { 0x06, 'z' }, { 0x07, 'x' },
    { 0x08, 'c' }, { 0x09, 'v' }, { 0x0B, 'b' }, { 0x0C, 'q' },
    { 0x0D, 'w' }, { 0x0E, 'e' }, { 0x0F, 'r' }, { 0x10, 'y' },
    { 0x11, 't' }, { 0x12, '1' }, { 0x13, '2' }, { 0x14, '3' },
    { 0x15, '4' }, { 0x16, '6' }, { 0x17, '5' }, { 0x18, '=' },
    { 0x19, '9' }, { 0x1A, '7' }, { 0x1B, '-' }, { 0x1C, '8' },
    { 0x1D, '0' }, { 0x1E, ']' }, { 0x1F, 'o' }, { 0x20, 'u' },
    { 0x21, '[' }, { 0x22, 'i' }, { 0x23, 'p' }, { 0x25, 'l' },
    { 0x26, 'j' }, { 0x27, '\'' }, { 0x28, 'k' }, { 0x29, ';' },
    { 0x2A, '\\' }, { 0x2B, ',' }, { 0x2C, '/' }, { 0x2D, 'n' },
    { 0x2E, 'm' }, { 0x2F, '.' }, { 0x32, '`' },
    /* special keys */
    { 0x24, XK_Return },    { 0x30, XK_Tab },
    { 0x31, XK_space },     { 0x33, XK_BackSpace },
    { 0x35, XK_Escape },    { 0x75, XK_Delete },
    { 0x73, XK_Home },      { 0x77, XK_End },
    { 0x74, XK_Page_Up },   { 0x79, XK_Page_Down },
    { 0x7B, XK_Left },      { 0x7C, XK_Right },
    { 0x7D, XK_Down },      { 0x7E, XK_Up },
    /* function keys */
    { 0x7A, XK_F1 },  { 0x78, XK_F2 },  { 0x63, XK_F3 },
    { 0x76, XK_F4 },  { 0x60, XK_F5 },  { 0x61, XK_F6 },
    { 0x62, XK_F7 },  { 0x64, XK_F8 },  { 0x65, XK_F9 },
    { 0x6D, XK_F10 }, { 0x67, XK_F11 }, { 0x6F, XK_F12 },
    /* modifiers */
    { 0x38, XK_Shift_L },   { 0x3C, XK_Shift_R },
    { 0x3B, XK_Control_L }, { 0x3E, XK_Control_R },
    { 0x3A, XK_Alt_L },     { 0x3D, XK_Alt_R },
    { 0x37, XK_Super_L },   { 0x36, XK_Super_R },
    { 0x39, XK_Caps_Lock },
    { 0x6B, XK_Scroll_Lock },
};

#define KEYMAP_SIZE (sizeof(keymap) / sizeof(keymap[0]))

uint32_t macos_keycode_to_keysym(CGKeyCode kc)
{
    for (size_t i = 0; i < KEYMAP_SIZE; i++) {
        if (keymap[i].mac == kc)
            return keymap[i].xk;
    }
    return 0;
}

CGKeyCode keysym_to_macos_keycode(uint32_t keysym)
{
    for (size_t i = 0; i < KEYMAP_SIZE; i++) {
        if (keymap[i].xk == keysym)
            return keymap[i].mac;
    }
    return 0xFFFF;
}

#endif /* __APPLE__ */
