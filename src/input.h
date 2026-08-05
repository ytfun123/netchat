#ifndef NETCHAT_INPUT_H
#define NETCHAT_INPUT_H

#include <stdint.h>
#include <stdbool.h>

/* Translates a physical key (from os_GetCSC) into a printable character,
 * given the current alpha-mode state. Returns 0 if the key doesn't map to
 * a printable character in that mode (arrows, function keys, etc -- the
 * caller handles those separately by checking the raw key code first). */
char input_KeyToChar(uint8_t key, bool alpha_mode);

#endif
