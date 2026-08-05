#include "bgimg.h"
#include "wizard.h"

#include <fileioc.h>
#include <graphx.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Each image is a pair of AppVars so a full frame fits under the CE's
 * 64KB-per-variable limit. A hold the 16-byte name + top half, B the
 * bottom half. All writes stream straight into fileioc handles (no big
 * RAM staging buffer), then the vars are archived at the end. */
/* ------------------------------------------------------------------ */

static void var_name(uint8_t slot, char half, char *out) {
    out[0] = 'B'; out[1] = 'G'; out[2] = 'I'; out[3] = 'M'; out[4] = 'G';
    out[5] = '0' + slot;
    out[6] = half;
    out[7] = '\0';
}

static struct {
    uint8_t slot;
    size_t total;   /* pixel bytes received overall */
    size_t in_half; /* pixel bytes written into the current half-var */
    bool second_half;
    bool active;
    uint8_t handle;
} rx;

static void bgimg_Invalidate(void);

/* Deletes both halves of a slot. Returns true if anything was removed. */
bool bgimg_Delete(uint8_t slot) {
    bool any = false;
    char vn[8];
    var_name(slot, 'A', vn);
    if (ti_Delete(vn)) any = true;
    var_name(slot, 'B', vn);
    if (ti_Delete(vn)) any = true;
    bgimg_Invalidate();
    return any;
}

void bgimg_RxReset(void) {
    if (!rx.active) return;
    if (rx.handle) {
        ti_Close(rx.handle);
        rx.handle = 0;
    }
    uint8_t slot = rx.slot;
    rx.active = false;
    bgimg_Delete(slot);
}

bool bgimg_RxStart(uint8_t slot, const char *name) {
    if (slot >= BGIMG_SLOTS) return false;
    bgimg_RxReset();
    bgimg_Delete(slot);

    char vn[8];
    var_name(slot, 'A', vn);
    rx.handle = ti_Open(vn, "w+");
    if (!rx.handle) {
        bgimg_Delete(slot);
        return false;
    }

    /* Write the name header (fixed 16 bytes, NUL padded). */
    char hdr[BGIMG_NAME_MAX];
    memset(hdr, 0, sizeof(hdr));
    strncpy(hdr, name, BGIMG_NAME_MAX);
    if (ti_Write(hdr, 1, sizeof(hdr), rx.handle) != sizeof(hdr)) {
        ti_Close(rx.handle);
        rx.handle = 0;
        bgimg_Delete(slot);
        return false;
    }

    rx.slot = slot;
    rx.total = 0;
    rx.in_half = 0;
    rx.second_half = false;
    rx.active = true;
    return true;
}

bool bgimg_RxData(const uint8_t *data, size_t n) {
    if (!rx.active || !rx.handle) return false;

    while (n > 0) {
        if (rx.total == BGIMG_TOTAL) {
            /* Frame is already full -- the peer sent more than we expect. */
            bgimg_RxReset();
            return false;
        }
        if (rx.in_half == BGIMG_HALF && !rx.second_half) {
            /* Top half done: archive A immediately so only one ~38KB var is
             * ever in RAM at once (the 75KB LCD buffer + two RAM vars would
             * be far too close to the CE's ~154KB budget). */
            ti_SetArchiveStatus(true, rx.handle);
            ti_Close(rx.handle);
            rx.handle = 0;
            char vn[8];
            var_name(rx.slot, 'B', vn);
            rx.handle = ti_Open(vn, "w+");
            if (!rx.handle) {
                bgimg_RxReset();
                return false;
            }
            rx.second_half = true;
            rx.in_half = 0;
        }

        size_t room = BGIMG_HALF - rx.in_half;
        size_t take = n < room ? n : room;
        if (ti_Write(data, 1, take, rx.handle) != take) {
            bgimg_RxReset();
            return false;
        }
        data += take;
        n -= take;
        rx.in_half += take;
        rx.total += take;

        if (rx.total > BGIMG_TOTAL) {
            bgimg_RxReset();
            return false;
        }
    }
    return true;
}

bool bgimg_RxEnd(void) {
    if (!rx.active) return false;
    if (rx.handle) {
        ti_Close(rx.handle);
        rx.handle = 0;
    }

    uint8_t slot = rx.slot;
    rx.active = false;

    if (rx.total != BGIMG_TOTAL) {
        bgimg_Delete(slot);
        return false;
    }

    /* Archive both halves so they survive a RAM reset. */
    char vn[8];
    var_name(slot, 'A', vn);
    uint8_t a = ti_Open(vn, "r");
    if (a) {
        ti_SetArchiveStatus(true, a);
        ti_Close(a);
    }
    var_name(slot, 'B', vn);
    uint8_t b = ti_Open(vn, "r");
    if (b) {
        ti_SetArchiveStatus(true, b);
        ti_Close(b);
    }
    bgimg_Invalidate();
    return true;
}

bool bgimg_RxActive(void) {
    return rx.active;
}

uint8_t bgimg_RxPercent(void) {
    if (!rx.active || rx.total > BGIMG_TOTAL) return 0;
    return (uint8_t)((uint32_t)rx.total * 100u / BGIMG_TOTAL);
}

bool bgimg_GetName(uint8_t slot, char *out, size_t out_sz) {
    if (slot >= BGIMG_SLOTS || !out_sz) return false;
    char vn[8];
    var_name(slot, 'A', vn);
    uint8_t h = ti_Open(vn, "r");
    if (!h) return false;

    char hdr[BGIMG_NAME_MAX];
    size_t got = ti_Read(hdr, 1, sizeof(hdr), h);
    ti_Close(h);
    if (got < 1) return false;

    size_t n = 0;
    while (n < sizeof(hdr) && hdr[n]) n++;
    if (n >= out_sz) n = out_sz - 1;
    memcpy(out, hdr, n);
    out[n] = '\0';
    return true;
}

bool bgimg_Exists(uint8_t slot) {
    if (slot >= BGIMG_SLOTS) return false;
    char vn[8];
    var_name(slot, 'A', vn);
    uint8_t a = ti_Open(vn, "r");
    if (a) ti_Close(a);
    var_name(slot, 'B', vn);
    uint8_t b = ti_Open(vn, "r");
    if (b) ti_Close(b);
    return a && b;
}

void bgimg_BuildSlotBitmap(char out[BGIMG_SLOTS + 1]) {
    for (uint8_t i = 0; i < BGIMG_SLOTS; i++)
        out[i] = bgimg_Exists(i) ? '1' : '0';
    out[BGIMG_SLOTS] = '\0';
}

/* ------------------------------------------------------------------ */
/* Drawing. The LCD blits whatever is in the draw buffer through the
 * live palette, and the uploaded pixels are already indices into that
 * palette, so drawing is just two straight memcpy's into gfx_vbuffer.
 * Archive data pointers are cached per slot (archived vars don't move);
 * anything that rewrites/deletes the vars invalidates the cache. */
/* ------------------------------------------------------------------ */

static int8_t cached_slot = -1;
static const uint8_t *cache_a = NULL;
static const uint8_t *cache_b = NULL;

static void bgimg_Invalidate(void) {
    cached_slot = -1;
    cache_a = NULL;
    cache_b = NULL;
}

void bgimg_PaletteLoad(void) {
    uint8_t pal[IMAGE_PAL_SIZE][4];
    wizard_ImagePalette(pal);
    for (uint8_t i = 0; i < IMAGE_PAL_SIZE; i++) {
        uint16_t color = gfx_RGBTo1555(pal[i][1], pal[i][2], pal[i][3]);
        gfx_SetPalette(&color, 2, pal[i][0]);
    }
}

bool bgimg_Draw(uint8_t slot) {
    if (slot >= BGIMG_SLOTS) return false;

    if (cached_slot != slot) {
        char vn[8];
        var_name(slot, 'A', vn);
        uint8_t a = ti_Open(vn, "r");
        if (!a) {
            bgimg_Invalidate();
            return false;
        }
        cache_a = ti_GetDataPtr(a);
        ti_Close(a);

        var_name(slot, 'B', vn);
        uint8_t b = ti_Open(vn, "r");
        if (!b) {
            bgimg_Invalidate();
            return false;
        }
        cache_b = ti_GetDataPtr(b);
        ti_Close(b);

        cached_slot = slot;
    }

    bgimg_PaletteLoad();
    uint8_t *vb = (uint8_t *)gfx_vbuffer;
    memcpy(vb, cache_a + BGIMG_NAME_MAX, BGIMG_HALF);
    memcpy(vb + BGIMG_HALF, cache_b, BGIMG_HALF);
    return true;
}
