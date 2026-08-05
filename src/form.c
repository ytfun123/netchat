#include "form.h"
#include "input.h"

#include <graphx.h>
#include <ti/getcsc.h>
#include <string.h>

#define FORM_TOP   40
#define FORM_LEFT  10
#define FORM_ROW_H 28
#define FORM_BOX_W 220
#define FORM_BOX_H 13
#define FORM_BUF_MAX 32 /* room for any field up to 31 chars + NUL */

static void draw_field(const theme_t *t, const form_field_t *f, uint8_t index,
                       bool active, uint8_t cursor) {
    uint24_t y = FORM_TOP + index * FORM_ROW_H;

    gfx_SetTextFGColor(t->compose_text);
    gfx_PrintStringXY(f->label, FORM_LEFT, y);

    gfx_SetColor(active ? t->my_bubble : t->compose_bg);
    gfx_FillRectangle(FORM_LEFT, y + 11, FORM_BOX_W, FORM_BOX_H);
    if (active) {
        gfx_SetColor(t->compose_text);
        gfx_Rectangle(FORM_LEFT - 1, y + 10, FORM_BOX_W + 2, FORM_BOX_H + 2);
    }

    char disp[FORM_BUF_MAX];
    uint8_t len = strlen(f->buf);
    uint8_t disp_len = len < FORM_BUF_MAX - 1 ? len : FORM_BUF_MAX - 1;
    for (uint8_t i = 0; i < disp_len; i++) disp[i] = f->masked ? '*' : f->buf[i];
    disp[disp_len] = '\0';

    uint8_t cv = cursor < disp_len ? cursor : disp_len;
    char before[FORM_BUF_MAX];
    for (uint8_t i = 0; i < cv; i++) before[i] = f->masked ? '*' : f->buf[i];
    before[cv] = '\0';

    gfx_SetTextFGColor(t->recv_text);
    gfx_PrintStringXY(disp, FORM_LEFT + 4, y + 13);
    if (active) {
        uint24_t cx = FORM_LEFT + 4 + gfx_GetStringWidth(before);
        gfx_SetTextFGColor(t->header);
        gfx_PrintStringXY("_", cx, y + 13);
    }
}

static void draw_form(const theme_t *t, const char *title,
                      const form_field_t *fields, uint8_t nfields,
                      uint8_t fi, uint8_t cursor) {
    gfx_FillScreen(t->bg);

    gfx_SetTextFGColor(t->compose_text);
    uint24_t w = gfx_GetStringWidth(title);
    gfx_PrintStringXY(title, (320 - w) / 2, 14);
    gfx_SetColor(t->border);
    gfx_FillRectangle(0, 27, 320, 1);

    for (uint8_t i = 0; i < nfields; i++)
        draw_field(t, &fields[i], i, i == fi, i == fi ? cursor : 0);

    gfx_SetTextFGColor(t->hint_text);
    gfx_PrintStringXY("[^/v] field  [</>] cursor  [alpha] mode  [del] backspace", 2, 222);
    gfx_PrintStringXY("[enter] ok  [clear] back", 2, 231);
    gfx_BlitBuffer();
}

uint8_t form_run(const theme_t *t, const char *title,
                 form_field_t *fields, uint8_t nfields) {
    uint8_t fi = 0;
    uint8_t cursor = 0;
    bool alpha_mode = true;

    while (1) {
        draw_form(t, title, fields, nfields, fi, cursor);
        uint8_t key = 0;
        while (!key) key = os_GetCSC();

        switch (key) {
            case sk_Up:
                if (fi > 0) { fi--; cursor = strlen(fields[fi].buf); }
                break;
            case sk_Down:
                if (fi < nfields - 1) { fi++; cursor = strlen(fields[fi].buf); }
                break;
            case sk_Left:
                if (cursor > 0) cursor--;
                break;
            case sk_Right:
                if (cursor < strlen(fields[fi].buf)) cursor++;
                break;
            case sk_Del:
                if (cursor > 0) {
                    memmove(&fields[fi].buf[cursor - 1], &fields[fi].buf[cursor],
                            strlen(fields[fi].buf) - cursor + 1);
                    cursor--;
                }
                break;
            case sk_Alpha:
                alpha_mode = !alpha_mode;
                break;
            case sk_Enter:
                return 1;
            case sk_Clear:
                return 0;
            default: {
                char c = input_KeyToChar(key, alpha_mode);
                if (c && strlen(fields[fi].buf) < fields[fi].max) {
                    memmove(&fields[fi].buf[cursor + 1], &fields[fi].buf[cursor],
                            strlen(fields[fi].buf) - cursor + 1);
                    fields[fi].buf[cursor] = c;
                    cursor++;
                }
                break;
            }
        }
    }
}

void form_alert(const theme_t *t, const char *title, const char *message) {
    gfx_FillScreen(t->bg);
    gfx_SetTextFGColor(t->compose_text);
    uint24_t w = gfx_GetStringWidth(title);
    gfx_PrintStringXY(title, (320 - w) / 2, 70);
    gfx_SetTextFGColor(t->recv_text);
    w = gfx_GetStringWidth(message);
    gfx_PrintStringXY(message, (320 - w) / 2, 100);
    gfx_SetTextFGColor(t->hint_text);
    gfx_PrintStringXY("[enter] or [clear] to continue",
                      (320 - gfx_GetStringWidth("[enter] or [clear] to continue")) / 2, 140);
    gfx_BlitBuffer();
    uint8_t key = 0;
    while (key != sk_Enter && key != sk_Clear) key = os_GetCSC();
}
