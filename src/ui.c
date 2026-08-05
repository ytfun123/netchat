#include "ui.h"

#include <fileioc.h>

#define UI_VAR_NAME "NCCGUI"
#define UI_MAGIC 0xD8 /* bumped when the on-disk layout changes */

const char *const theme_names[NUM_THEMES] = {
    "Light", "Dark"
};

/* UI colors, split into a light and a dark theme. The palette indexes
 * used here (0xE6, 0x19, 0x94, 0xB6, 0x96, ...) are made exact by
 * wizard_LoadPalette. */
const theme_t themes[NUM_THEMES] = {
    /* Light */
    { 0xFF, 0x11, 0xE6, 0xB6, 0x00, 0xE6, 0x00, 0xB5, 0xD6, 0x00, 0x10, 0x00, 0x1C, 0xE0 },
    /* Dark */
    { 0x19, 0x00, 0xE6, 0x96, 0xE6, 0x94, 0xE6, 0x94, 0x00, 0xE6, 0x94, 0x00, 0x1C, 0xE0 },
};

static void ui_Defaults(netchat_ui_t *ui) {
    ui->theme = 0;
    ui->bubbles = 1;
    ui->bg_color = UI_COLOR_UNSET;
    ui->text_color = UI_COLOR_UNSET;
    ui->bg_image = BGIMG_NONE;
}

bool ui_Load(netchat_ui_t *ui) {
    ui_Defaults(ui);

    uint8_t handle = ti_Open(UI_VAR_NAME, "r");
    if (!handle) return false;

    uint8_t magic = 0;
    ti_Read(&magic, 1, 1, handle);

    if (magic == UI_MAGIC) {
        ti_Read(&ui->theme, 1, 1, handle);
        ti_Read(&ui->bubbles, 1, 1, handle);
        ti_Read(&ui->bg_color, 1, 1, handle);
        ti_Read(&ui->text_color, 1, 1, handle);
        /* bg_image was appended later; a var saved by an older build is
         * shorter, so a failed read just leaves the default. */
        if (ti_Read(&ui->bg_image, 1, 1, handle) != 1)
            ui->bg_image = BGIMG_NONE;
    }

    /* Keep the UI settings in flash too, so a RAM reset doesn't forget
     * the user's look-and-feel choices. */
    if (!ti_IsArchived(handle))
        ti_SetArchiveStatus(true, handle);

    ti_Close(handle);
    return true;
}

bool ui_Save(const netchat_ui_t *ui) {
    uint8_t handle = ti_Open(UI_VAR_NAME, "w+");
    if (!handle) return false;

    uint8_t magic = UI_MAGIC;
    ti_Write(&magic, 1, 1, handle);
    ti_Write(&ui->theme, 1, 1, handle);
    ti_Write(&ui->bubbles, 1, 1, handle);
    ti_Write(&ui->bg_color, 1, 1, handle);
    ti_Write(&ui->text_color, 1, 1, handle);
    ti_Write(&ui->bg_image, 1, 1, handle);

    ti_SetArchiveStatus(true, handle);

    ti_Close(handle);
    return true;
}
