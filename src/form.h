#ifndef NETCHAT_FORM_H
#define NETCHAT_FORM_H

#include <stdint.h>
#include <stdbool.h>
#include "ui.h"

typedef struct {
    const char *label;
    char *buf;        /* null-terminated, at least max+1 bytes */
    uint8_t max;      /* maximum characters (not counting the NUL) */
    bool masked;      /* true = show '*' instead of the real characters */
} form_field_t;

/* Shows a full-screen editable form. Navigation is like a PC form:
 * [up]/[down] switches between fields, [left]/[right] moves the text
 * cursor, [alpha] toggles letters/digits, [del] backspaces.
 * Returns 1 when the user pressed [enter] (submit), 0 when they pressed
 * [clear] (back/cancel) -- nothing is applied by this function itself. */
uint8_t form_run(const theme_t *t, const char *title,
                 form_field_t *fields, uint8_t num_fields);

/* Centered title + message, dismissed with [enter] or [clear]. */
void form_alert(const theme_t *t, const char *title, const char *message);

#endif
