#ifndef NETCHAT_BGIMG_H
#define NETCHAT_BGIMG_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Background images uploaded from the site. Images are 320x240 8bpp
 * palette-indexed; the site quantizes them against the app's fixed 20
 * color image palette (wizard_ImagePalette), so the calc just draws the
 * stored indices through its live LCD palette. */
#define BGIMG_SLOTS 6
#define BGIMG_W 320
#define BGIMG_H 240
#define BGIMG_NAME_MAX 16
#define BGIMG_HALF (BGIMG_W * (BGIMG_H / 2)) /* 38400 */
#define BGIMG_TOTAL (BGIMG_W * BGIMG_H)      /* 76800 */

/* A full frame is 76,800 bytes, which exceeds the CE's 64KB per-variable
 * limit, so each image lives in TWO archived AppVars:
 *   BGIMG<n>A = 16-byte name + top half of the pixels
 *   BGIMG<n>B = bottom half of the pixels
 * The "bgnetchat folder" is just this BGIMG* naming group. */

/* --- streaming receive (fed by #BGIMG# / #BGDATA# / #BGEND# /
 *     #BGCANCEL# lines in ingest()) --- */
void bgimg_RxReset(void);   /* abort any in-progress receive (deletes partials) */
bool bgimg_RxStart(uint8_t slot, const char *name);
bool bgimg_RxData(const uint8_t *data, size_t n); /* false = write failed, aborted */
bool bgimg_RxEnd(void);     /* false = size mismatch, partials deleted */
bool bgimg_RxActive(void);
uint8_t bgimg_RxPercent(void);

/* --- slot management --- */
bool bgimg_GetName(uint8_t slot, char *out, size_t out_sz);
bool bgimg_Exists(uint8_t slot);
bool bgimg_Delete(uint8_t slot);
void bgimg_BuildSlotBitmap(char out[BGIMG_SLOTS + 1]);

/* Draws the image into the current graphx buffer (draw buffer). Caches the
 * archive data pointers per slot; returns false if the slot has no image so
 * the caller can fall back to the theme/custom background color. Also
 * re-applies the 20-color image palette (0xC0-0xCB picker slots can be
 * left dirty by the settings color pickers, which reuse them as scratch). */
bool bgimg_Draw(uint8_t slot);

/* Applies wizard_ImagePalette to the live LCD palette. Called by bgimg_Draw
 * before each blit; exposed for the settings screen if it ever needs to show
 * an image without a full redraw. */
void bgimg_PaletteLoad(void);

#endif
