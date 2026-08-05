#include "input.h"

#include <ti/getcsc.h>

/* Alpha-mode letter table -- copied verbatim from the CE toolchain's own
 * usbdrvce/link_library example (src/main.c), which is itself verified
 * against the real TI-84 Plus CE physical keyboard layout. Do not "fix"
 * entries here without checking that source first. */
static const char alpha_map[] = {
    [sk_Math  ] = 'A',
    [sk_Apps  ] = 'B',
    [sk_Prgm  ] = 'C',
    [sk_Recip ] = 'D',
    [sk_Sin   ] = 'E',
    [sk_Cos   ] = 'F',
    [sk_Tan   ] = 'G',
    [sk_Power ] = 'H',
    [sk_Square] = 'I',
    [sk_Comma ] = 'J',
    [sk_LParen] = 'K',
    [sk_RParen] = 'L',
    [sk_Div   ] = 'M',
    [sk_Log   ] = 'N',
    [sk_7     ] = 'O',
    [sk_8     ] = 'P',
    [sk_9     ] = 'Q',
    [sk_Mul   ] = 'R',
    [sk_Ln    ] = 'S',
    [sk_4     ] = 'T',
    [sk_5     ] = 'U',
    [sk_6     ] = 'V',
    [sk_Sub   ] = 'W',
    [sk_Store ] = 'X',
    [sk_1     ] = 'Y',
    [sk_2     ] = 'Z',
    [sk_Add   ] = '"',
    [sk_0     ] = ' ',
    [sk_DecPnt] = ':',
    [sk_Chs   ] = '?',
};

/* Non-alpha mode: number keys give digits, a handful of others give basic
 * punctuation useful for chat. */
static const char digit_map[] = {
    [sk_0     ] = '0',
    [sk_1     ] = '1',
    [sk_2     ] = '2',
    [sk_3     ] = '3',
    [sk_4     ] = '4',
    [sk_5     ] = '5',
    [sk_6     ] = '6',
    [sk_7     ] = '7',
    [sk_8     ] = '8',
    [sk_9     ] = '9',
    [sk_DecPnt] = '.',
    [sk_Comma ] = ',',
    [sk_Chs   ] = '-',
};

char input_KeyToChar(uint8_t key, bool alpha_mode) {
    const char *table = alpha_mode ? alpha_map : digit_map;
    size_t table_size = alpha_mode ? sizeof(alpha_map) : sizeof(digit_map);
    if (key >= table_size) return 0;
    return table[key];
}
