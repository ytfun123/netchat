#ifndef NETCHAT_WIZARD_H
#define NETCHAT_WIZARD_H

#include "config.h"
#include "ui.h"

#define PALETTE_SIZE 8
extern const uint8_t wizard_palette[PALETTE_SIZE];
extern const char *const wizard_palette_names[PALETTE_SIZE];
extern const char *const wizard_palette_hex[PALETTE_SIZE];

/* Overrides the graphx default palette entries so the 8 color indexes
 * (0xE0, 0x1C, 0x03, ...) render the exact RGB colors the browser shows,
 * plus the grays and bubble blues used by the chat UI. Call once after
 * every gfx_Begin(). */
void wizard_LoadPalette(void);

/* Runs the full first-time setup flow (username, color, password),
 * filling in `config`. Blocking -- takes over the screen via graphx like
 * the rest of the UI. Returns false if the user cancelled (pressed
 * [clear]) at any step, in which case nothing was saved.
 *
 * Each individual step returns false when cancelled -- the settings menu
 * uses that as "back to the menu without changing anything". */
bool wizard_Run(netchat_config_t *config, const theme_t *t);
bool wizard_SetUsername(netchat_config_t *config, const theme_t *t);
bool wizard_SetColor(netchat_config_t *config, const theme_t *t);
bool wizard_SetPassword(netchat_config_t *config, const theme_t *t);

/* Generic swatch-grid color picker. Shows `n` colors from `rgb`
 * (RGB888, 4 per row), each rendered through its own scratch palette
 * slot so the preview matches the final color; returns the picked index
 * or UI_COLOR_UNSET if the user pressed [clear]. On [enter] the picked
 * color is written into the real slot `slot`. `initial` is the starting
 * selection. */
uint8_t wizard_PickColor(const theme_t *t, uint8_t slot, const char *title,
                         const uint8_t (*rgb)[3], uint8_t n, uint8_t initial);

/* Applies the user's custom background/text colors (ui->bg_color /
 * ui->text_color) to the reserved palette slots. Call after
 * wizard_LoadPalette + ui_Load. Does nothing for UI_COLOR_UNSET. */
void wizard_ApplyCustomColors(const netchat_ui_t *ui);

/* 12-color RGB888 table behind the Background/Text color pickers. */
extern const uint8_t pick_rgb[PICK_COLORS][3];

/* The complete set of colors background images are quantized against:
 * PALETTE_SIZE accent colors + PICK_COLORS picker colors, 20 total.
 * These are the only palette entries the image pixels reference, so the
 * slots are stable and the calc can re-apply them whenever it draws an
 * image (see bgimg.c). */
#define IMAGE_PAL_SIZE (PALETTE_SIZE + PICK_COLORS)

/* Fills out[N][4] with (index, r, g, b) for the image palette above.
 * Indexes for the accents are their real palette bytes (wizard_palette);
 * picker colors use the reserved scratch block 0xC0-0xCB. */
void wizard_ImagePalette(uint8_t (*out)[4]);

#endif
