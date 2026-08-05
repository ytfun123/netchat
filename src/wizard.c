#include "wizard.h"
#include "form.h"

#include <graphx.h>
#include <ti/getcsc.h>
#include <string.h>
#include <stdio.h>

/* RGB332-ish swatches picked from graphx's default 8bpp palette. */
const uint8_t wizard_palette[PALETTE_SIZE] = {
    0xE0, /* Red     */
    0x1C, /* Green   */
    0x03, /* Blue    */
    0xFC, /* Yellow  */
    0x1F, /* Cyan    */
    0xE3, /* Magenta */
    0xEC, /* Orange  */
    0x83, /* Purple  */
};

const char *const wizard_palette_names[PALETTE_SIZE] = {
    "Red", "Green", "Blue", "Yellow", "Cyan", "Magenta", "Orange", "Purple"
};

/* 12 colors offered by the settings "Background color" / "Text color"
 * pickers. All high-contrast picks that stay readable on the calc
 * screen. Index 0-11, kept in sync with pick_names below. */
const uint8_t pick_rgb[PICK_COLORS][3] = {
    {0xFF, 0xFF, 0xFF}, /* White       */
    {0x18, 0x18, 0x18}, /* Black       */
    {0xD0, 0xD0, 0xD0}, /* Light gray  */
    {0x60, 0x60, 0x60}, /* Dark gray   */
    {0x9E, 0xC5, 0xFE}, /* Light blue  */
    {0x2E, 0x4F, 0x9E}, /* Medium blue */
    {0x20, 0x60, 0xE0}, /* Blue        */
    {0x50, 0xC0, 0x80}, /* Green       */
    {0xE0, 0x60, 0x60}, /* Red         */
    {0xE0, 0xA0, 0x20}, /* Orange      */
    {0xC0, 0x80, 0xE0}, /* Purple      */
    {0xE0, 0xD0, 0x90}, /* Cream       */
};

static const char *const pick_names[PICK_COLORS] = {
    "White", "Black", "Light gray", "Dark gray", "Light blue", "Medium blue",
    "Blue", "Green", "Red", "Orange", "Purple", "Cream"
};

/* RGB888 equivalents of the palette above, for the browser side (which
 * can't make sense of an RGB332 palette byte on its own). Keep in sync
 * with wizard_palette. */
const char *const wizard_palette_hex[PALETTE_SIZE] = {
    "e02020", "20c020", "2060e0", "e0e020",
    "20c0e0", "e020c0", "e08020", "8020c0"
};

/* Exact RGB888 values for the accent colors above (same order). Shared by
 * wizard_LoadPalette and the background-image palette (wizard_ImagePalette). */
static const uint8_t pal_rgb[PALETTE_SIZE][3] = {
    {0xE0, 0x20, 0x20},
    {0x20, 0xC0, 0x20},
    {0x20, 0x60, 0xE0},
    {0xE0, 0xE0, 0x20},
    {0x20, 0xC0, 0xE0},
    {0xE0, 0x20, 0xC0},
    {0xE0, 0x80, 0x20},
    {0x80, 0x20, 0xC0},
};

/* The graphx default palette is not RGB332 -- 0x03 actually renders dark
 * green and 0x1C renders blue, so swatches showed the wrong colors. Fix it
 * by overriding just our 8 color entries with the exact RGB values from
 * wizard_palette_hex. The remaining entries are the grays/whites/blacks
 * used by the chat UI (0xE6/0x19/0x94) plus the two bubble blues (0xB6 for
 * the light theme, 0x96 for the dark theme) -- those are made exact too so
 * the chat screen and the themes look right on every calculator. */
static void override_color(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    uint16_t color = gfx_RGBTo1555(r, g, b);
    gfx_SetPalette(&color, 2, index);
}

void wizard_LoadPalette(void) {
    for (uint8_t i = 0; i < PALETTE_SIZE; i++)
        override_color(wizard_palette[i], pal_rgb[i][0], pal_rgb[i][1], pal_rgb[i][2]);

    override_color(0xE6, 0xD8, 0xD8, 0xD8); /* light gray: light-theme recv bubble */
    override_color(0x19, 0x20, 0x20, 0x20); /* near-black: dark-theme background */
    override_color(0x94, 0x3C, 0x3C, 0x3C); /* mid-dark gray: dark-theme recv bubble */
    override_color(0xB6, 0x9E, 0xC5, 0xFE); /* light blue: light-theme sent bubble */
    override_color(0x96, 0x2E, 0x4F, 0x9E); /* medium blue: dark-theme sent bubble */
}

void wizard_ImagePalette(uint8_t (*out)[4]) {
    for (uint8_t i = 0; i < PALETTE_SIZE; i++) {
        out[i][0] = wizard_palette[i];
        out[i][1] = pal_rgb[i][0];
        out[i][2] = pal_rgb[i][1];
        out[i][3] = pal_rgb[i][2];
    }
    for (uint8_t i = 0; i < PICK_COLORS; i++) {
        out[PALETTE_SIZE + i][0] = 0xC0 + i;
        out[PALETTE_SIZE + i][1] = pick_rgb[i][0];
        out[PALETTE_SIZE + i][2] = pick_rgb[i][1];
        out[PALETTE_SIZE + i][3] = pick_rgb[i][2];
    }
}

/* Header bar for the color step: progress dots + a centered title. */
static void wizard_header(uint8_t step, uint8_t total, const char *title, const theme_t *t) {
    gfx_FillScreen(t->bg);

    uint24_t dot_w = 24, gap = 6;
    uint24_t total_w = total * dot_w + (total - 1) * gap;
    uint24_t x = (320 - total_w) / 2;
    for (uint8_t i = 0; i < total; i++) {
        gfx_SetColor(i < step ? 0x10 : (i == step ? 0x1F : 0xD6));
        gfx_FillRectangle(x, 12, dot_w, 4);
        x += dot_w + gap;
    }

    gfx_SetTextFGColor(t->compose_text);
    gfx_SetTextScale(1, 1);
    uint24_t text_w = gfx_GetStringWidth(title);
    gfx_PrintStringXY(title, (320 - text_w) / 2, 34);
}

bool wizard_SetUsername(netchat_config_t *config, const theme_t *t) {
    char buf[USERNAME_MAX + 1] = "";
    while (1) {
        form_field_t fields[] = {
            { "Username", buf, USERNAME_MAX, false },
        };
        if (!form_run(t, "What should we call you?", fields, 1))
            return false; /* [clear] = cancel setup */
        if (buf[0]) break;
        form_alert(t, "Step 1 of 3", "Username can't be empty");
    }
    strncpy(config->username, buf, USERNAME_MAX);
    config->username[USERNAME_MAX] = '\0';
    return true;
}

bool wizard_SetColor(netchat_config_t *config, const theme_t *t) {
    uint8_t selected = 0;

    while (1) {
        wizard_header(1, 3, "Pick a color", t);

        /* 4x2 grid of swatches, number-keyed */
        uint24_t sw = 60, sh = 44, gap = 10;
        uint24_t grid_w = 4 * sw + 3 * gap;
        uint24_t x0 = (320 - grid_w) / 2, y0 = 70;

        for (uint8_t i = 0; i < PALETTE_SIZE; i++) {
            uint24_t col = i % 4, row = i / 4;
            uint24_t x = x0 + col * (sw + gap);
            uint24_t y = y0 + row * (sh + gap);

            gfx_SetColor(wizard_palette[i]);
            gfx_FillRectangle(x, y, sw, sh);

            if (i == selected) {
                gfx_SetColor(0x00);
                gfx_Rectangle(x - 2, y - 2, sw + 4, sh + 4);
                gfx_Rectangle(x - 3, y - 3, sw + 6, sh + 6);
            }

            gfx_SetTextFGColor(0x00);
            char label[4];
            sprintf(label, "%d", i + 1);
            gfx_PrintStringXY(label, x + 4, y + 4);
        }

        gfx_SetTextFGColor(t->compose_text);
        char sub[48];
        sprintf(sub, "%s -- [enter] ok  [clear] back", wizard_palette_names[selected]);
        uint24_t sub_w = gfx_GetStringWidth(sub);
        gfx_PrintStringXY(sub, (320 - sub_w) / 2, y0 + 2 * (sh + gap) + 10);

        gfx_BlitBuffer();

        uint8_t key = os_GetCSC();
        if (key >= sk_1 && key <= sk_8) {
            selected = key - sk_1;
        } else if (key == sk_Right && selected < PALETTE_SIZE - 1) {
            selected++;
        } else if (key == sk_Left && selected > 0) {
            selected--;
        } else if (key == sk_Down && selected + 4 < PALETTE_SIZE) {
            selected += 4;
        } else if (key == sk_Up && selected >= 4) {
            selected -= 4;
        } else if (key == sk_Enter) {
            config->color_index = selected;
            return true;
        } else if (key == sk_Clear) {
            return false; /* back / cancel */
        }
    }
}

bool wizard_SetPassword(netchat_config_t *config, const theme_t *t) {
    char pass[PASSWORD_MAX + 1] = "";
    char confirm[PASSWORD_MAX + 1] = "";
    while (1) {
        form_field_t fields[] = {
            { "Password", pass, PASSWORD_MAX, true },
            { "Confirm", confirm, PASSWORD_MAX, true },
        };
        if (!form_run(t, "Set a password (optional)", fields, 2))
            return false; /* [clear] = cancel setup */
        if (!strcmp(pass, confirm)) break;
        form_alert(t, "Step 3 of 3", "Passwords don't match");
        pass[0] = '\0';
        confirm[0] = '\0';
    }
    strncpy(config->password, pass, PASSWORD_MAX);
    config->password[PASSWORD_MAX] = '\0';
    return true;
}

bool wizard_Run(netchat_config_t *config, const theme_t *t) {
    memset(config, 0, sizeof(*config));

    if (!wizard_SetUsername(config, t)) return false;
    if (!wizard_SetColor(config, t)) return false;
    if (!wizard_SetPassword(config, t)) return false;

    gfx_FillScreen(t->bg);
    gfx_SetTextFGColor(t->compose_text);
    char line[40];
    sprintf(line, "Welcome, %s!", config->username);
    uint24_t w = gfx_GetStringWidth(line);
    gfx_PrintStringXY(line, (320 - w) / 2, 80);
    gfx_SetTextFGColor(t->hint_text);
    gfx_PrintStringXY("[enter] to start", (320 - gfx_GetStringWidth("[enter] to start")) / 2, 120);
    gfx_BlitBuffer();

    uint8_t key = 0;
    while (key != sk_Enter && key != sk_Clear) key = os_GetCSC();
    return true;
}

/* Scratch palette slots for the settings swatch grid. One entry per
 * swatch, loaded up front, so each swatch keeps its exact color while it
 * is on screen. The CE LCD renders VRAM through the live palette, so all
 * swatches must use distinct entries -- reusing a single slot (as the old
 * code did) made every swatch flicker through the last-assigned color. */
#define PICKER_SLOT_BASE 0xC0

/* Shared swatch-grid picker for the settings menu. Each swatch is drawn
 * with its own scratch slot (PICKER_SLOT_BASE + i), so the grid previews
 * the exact colors that end up in use. On [enter] the picked RGB is
 * written into the real palette slot `slot` (UI_BG_INDEX / UI_TEXT_INDEX),
 * which is what the chat screen reads. */
uint8_t wizard_PickColor(const theme_t *t, uint8_t slot, const char *title,
                         const uint8_t (*rgb)[3], uint8_t n, uint8_t initial) {
    uint8_t selected = initial < n ? initial : 0;

    for (uint8_t i = 0; i < n; i++)
        override_color(PICKER_SLOT_BASE + i, rgb[i][0], rgb[i][1], rgb[i][2]);

    while (1) {
        gfx_FillScreen(t->bg);
        gfx_SetTextFGColor(t->compose_text);
        gfx_SetTextScale(1, 1);
        uint24_t tw = gfx_GetStringWidth(title);
        gfx_PrintStringXY(title, (320 - tw) / 2, 18);

        /* 4-column grid of swatches, number-keyed (1-9, 0 = 10) */
        uint24_t sw = 64, sh = 44, gap = 10;
        uint24_t cols = 4;
        uint24_t rows = (n + cols - 1) / cols;
        uint24_t grid_w = cols * sw + (cols - 1) * gap;
        uint24_t x0 = (320 - grid_w) / 2, y0 = 50;

        for (uint8_t i = 0; i < n; i++) {
            uint24_t col = i % cols, row = i / cols;
            uint24_t x = x0 + col * (sw + gap);
            uint24_t y = y0 + row * (sh + gap);

            gfx_SetColor(PICKER_SLOT_BASE + i);
            gfx_FillRectangle(x, y, sw, sh);

            if (i == selected) {
                gfx_SetColor(0x00);
                gfx_Rectangle(x - 2, y - 2, sw + 4, sh + 4);
                gfx_Rectangle(x - 3, y - 3, sw + 6, sh + 6);
            }

            gfx_SetTextFGColor(0x00);
            char label[4];
            sprintf(label, "%d", i + 1);
            gfx_PrintStringXY(label, x + 4, y + 4);
        }

        gfx_SetTextFGColor(t->compose_text);
        char sub[48];
        sprintf(sub, "%s -- [enter] ok  [clear] back", pick_names[selected]);
        uint24_t sub_w = gfx_GetStringWidth(sub);
        gfx_PrintStringXY(sub, (320 - sub_w) / 2, y0 + rows * (sh + gap) + 6);

        gfx_BlitBuffer();

        uint8_t key = os_GetCSC();
        if (key >= sk_1 && key <= sk_0) {
            uint8_t idx = key - sk_1;
            if (idx < n) selected = idx;
        } else if (key == sk_Right && selected < n - 1) {
            selected++;
        } else if (key == sk_Left && selected > 0) {
            selected--;
        } else if (key == sk_Down && selected + cols < n) {
            selected += cols;
        } else if (key == sk_Up && selected >= cols) {
            selected -= cols;
        } else if (key == sk_Enter) {
            override_color(slot, rgb[selected][0], rgb[selected][1], rgb[selected][2]);
            return selected;
        } else if (key == sk_Clear) {
            return UI_COLOR_UNSET;
        }
    }
}

void wizard_ApplyCustomColors(const netchat_ui_t *ui) {
    if (ui->bg_color != UI_COLOR_UNSET) {
        const uint8_t *c = pick_rgb[ui->bg_color];
        override_color(UI_BG_INDEX, c[0], c[1], c[2]);
    }
    if (ui->text_color != UI_COLOR_UNSET) {
        const uint8_t *c = pick_rgb[ui->text_color];
        override_color(UI_TEXT_INDEX, c[0], c[1], c[2]);
    }
}
