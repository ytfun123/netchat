#ifndef NETCHAT_UI_H
#define NETCHAT_UI_H

#include <stdint.h>
#include <stdbool.h>

/* UI preferences live in their own AppVar (separate from the NCCFG
 * config) so changing the look of the app never re-triggers the setup
 * wizard or risks corrupting account data. */

#define NUM_THEMES 2

/* Look-and-feel overrides. bg_color/text_color index into the color
 * picker table (see wizard.c); UI_COLOR_UNSET means "use the theme's
 * own color". bg_image indexes a stored background image slot (0-5),
 * BGIMG_NONE = no image (theme/custom color is used). */
#define UI_COLOR_UNSET 0xFF
#define BGIMG_NONE 0xFF
#define PICK_COLORS 12
#define UI_BG_INDEX 0xE2   /* palette slot for the custom background */
#define UI_TEXT_INDEX 0xE1 /* palette slot for the custom text color */

typedef struct {
    uint8_t theme;   /* 0 = Light, 1 = Dark */
    uint8_t bubbles; /* 0 = plain lines, 1 = chat bubbles */
    uint8_t bg_color;   /* custom chat background (0-11), UI_COLOR_UNSET = theme */
    uint8_t text_color; /* custom message text (0-11), UI_COLOR_UNSET = theme */
    uint8_t bg_image;   /* background image slot (0-5), BGIMG_NONE = none */
} netchat_ui_t;

extern const char *const theme_names[NUM_THEMES];

/* Per-theme UI colors. The palette indexes used here are all made exact
 * by wizard_LoadPalette (see wizard.c). */
typedef struct {
    uint8_t bg, header, header_text;
    uint8_t my_bubble, my_text;
    uint8_t recv_bubble, recv_text;
    uint8_t sys;
    uint8_t compose_bg, compose_text, hint_text;
    uint8_t border;
    uint8_t dot_online, dot_offline;
} theme_t;

extern const theme_t themes[NUM_THEMES];

bool ui_Load(netchat_ui_t *ui);
bool ui_Save(const netchat_ui_t *ui);

#endif
