#include "linklib.h"
#include "config.h"
#include "wizard.h"
#include "input.h"
#include "form.h"
#include "ui.h"
#include "bgimg.h"

#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/rtc.h>

#include <graphx.h>
#include <tice.h>
#include <ti/screen.h>
#include <ti/getcsc.h>

/* ----------------------------------------------------------------------
 * netchat: talks to the flasher site over a custom USB link (see
 * linklib.c). The browser is responsible for actually relaying messages
 * to/from a real chat server -- this program only knows how to speak the
 * local USB protocol and draw a chat UI.
 *
 * Wire format: newline-delimited UTF-8-ish text lines in both directions.
 * Lines starting with '#' are control lines (#CONFIG#, #TYPING#,
 * #STOPTYPING#) rather than chat messages -- the browser side treats them
 * specially instead of displaying them.
 *
 * The chat screen is a bubble UI: your own messages are right-aligned
 * colored bubbles, incoming messages are left-aligned gray bubbles, and
 * system notices are plain centered lines. "Bubbles" can be turned off in
 * settings to go back to plain text lines (with > / < prefixes). Theme
 * and bubble settings live in a separate AppVar (see ui.c) so they never
 * reset the wizard data.
 * ------------------------------------------------------------------- */

#define CHAR_HEIGHT     8
#define HISTORY_LINES   24
#define LINE_MAX        42   /* characters per stored/drawn line */
#define READ_CHUNK      64
#define WRITE_BUFFER_SIZE 640 /* large enough for the #PAL# line (516 chars) */
#define RECV_ASSEMBLY   600  /* holds a full line; long #BGDATA# lines need it */
#define COMPOSE_MAX     42

#define CHAT_TOP        18
#define CHAT_BOTTOM     206
#define CHAT_H          (CHAT_BOTTOM - CHAT_TOP)
#define COMPOSE_TOP     208

#define BUBBLE_MAX_W    280  /* widest wrapped bubble, px */
#define BUBBLE_PAD_X    6
#define BUBBLE_PAD_Y    3
#define BUBBLE_GAP      6
#define LINE_GAP        1
#define MAX_BUBBLE_LINES 8

/* Per-theme UI colors live in ui.h/ui.c, shared with the wizard and the
 * form widget. The palette indexes are all made exact by
 * wizard_LoadPalette (see wizard.c). */

enum { HIST_SYS = 0, HIST_RECV = 1, HIST_SENT = 2 };

static char history[HISTORY_LINES][LINE_MAX + 1];
static uint8_t history_type[HISTORY_LINES];
static uint8_t history_count = 0;

static void history_push(uint8_t type, const char *text) {
    if (history_count == HISTORY_LINES) {
        memmove(history[0], history[1], (HISTORY_LINES - 1) * (LINE_MAX + 1));
        memmove(&history_type[0], &history_type[1], HISTORY_LINES - 1);
        history_count--;
    }
    strncpy(history[history_count], text, LINE_MAX);
    history[history_count][LINE_MAX] = '\0';
    history_type[history_count] = type;
    history_count++;
}

/* Word-wraps `text` into at most MAX_BUBBLE_LINES lines that each fit
 * within BUBBLE_MAX_W. Returns the number of lines produced. */
static uint8_t wrap_words(char out[MAX_BUBBLE_LINES][LINE_MAX + 1], const char *text) {
    char work[LINE_MAX + 1];
    strncpy(work, text, LINE_MAX);
    work[LINE_MAX] = '\0';

    uint8_t n = 0;
    char *word = strtok(work, " ");
    while (word && n < MAX_BUBBLE_LINES) {
        if (n == 0 || out[n - 1][0] == '\0') {
            strncpy(out[n], word, LINE_MAX);
            out[n][LINE_MAX] = '\0';
            n++;
        } else {
            char test[LINE_MAX + 1];
            snprintf(test, sizeof(test), "%s %s", out[n - 1], word);
            if (strlen(test) <= LINE_MAX && gfx_GetStringWidth(test) <= BUBBLE_MAX_W) {
                strcpy(out[n - 1], test);
            } else {
                strncpy(out[n], word, LINE_MAX);
                out[n][LINE_MAX] = '\0';
                n++;
            }
        }
        word = strtok(NULL, " ");
    }
    return n;
}

static uint24_t bubble_height(const char *text) {
    char lines[MAX_BUBBLE_LINES][LINE_MAX + 1];
    uint8_t n = wrap_words(lines, text);
    return n * CHAR_HEIGHT + (n - 1) * LINE_GAP + 2 * BUBBLE_PAD_Y + BUBBLE_GAP;
}

/* Custom look-and-feel resolution: returns the palette slot to use for
 * the chat background / message text, or UI_COLOR_UNSET to fall back on
 * the theme's own color. The custom colors themselves are loaded into
 * the reserved slots by wizard_ApplyCustomColors at startup. */
static uint8_t chat_bg_slot(const netchat_ui_t *ui) {
    return ui->bg_color != UI_COLOR_UNSET ? UI_BG_INDEX : UI_COLOR_UNSET;
}

static uint8_t chat_text_slot(const netchat_ui_t *ui) {
    return ui->text_color != UI_COLOR_UNSET ? UI_TEXT_INDEX : UI_COLOR_UNSET;
}

static void draw_bubble(const theme_t *t, bool mine, const char *text, uint24_t *y,
                        uint8_t txt) {
    char lines[MAX_BUBBLE_LINES][LINE_MAX + 1];
    uint8_t n = wrap_words(lines, text);
    if (!n) return;

    uint24_t w = 0;
    for (uint8_t i = 0; i < n; i++) {
        uint24_t lw = gfx_GetStringWidth(lines[i]);
        if (lw > w) w = lw;
    }

    uint24_t h = n * CHAR_HEIGHT + (n - 1) * LINE_GAP + 2 * BUBBLE_PAD_Y;
    uint24_t x = mine ? (320 - BUBBLE_GAP - w - 2 * BUBBLE_PAD_X) : BUBBLE_PAD_X;

    gfx_SetColor(mine ? t->my_bubble : t->recv_bubble);
    gfx_FillRectangle(x, *y, w + 2 * BUBBLE_PAD_X, h);
    gfx_SetColor(t->border);
    gfx_Rectangle(x, *y, w + 2 * BUBBLE_PAD_X, h);

    gfx_SetTextFGColor(txt != UI_COLOR_UNSET ? txt : (mine ? t->my_text : t->recv_text));
    for (uint8_t i = 0; i < n; i++)
        gfx_PrintStringXY(lines[i], x + BUBBLE_PAD_X, *y + BUBBLE_PAD_Y + i * (CHAR_HEIGHT + LINE_GAP));

    *y += h + BUBBLE_GAP;
}

static void draw_sys(const theme_t *t, const char *text, uint24_t *y) {
    gfx_SetTextFGColor(t->sys);
    gfx_PrintStringXY(text, 2, *y);
    *y += CHAR_HEIGHT + 2;
}

static void draw_plain(const theme_t *t, uint8_t accent_index, uint8_t type,
                       const char *text, uint24_t y, uint8_t txt) {
    if (type == HIST_SENT) {
        if (txt != UI_COLOR_UNSET) {
            gfx_SetTextFGColor(wizard_palette[accent_index]);
            gfx_PrintStringXY("> ", 2, y);
            gfx_SetTextFGColor(txt);
            gfx_PrintStringXY(text, 14, y);
        } else {
            gfx_SetTextFGColor(wizard_palette[accent_index]);
            gfx_PrintStringXY("> ", 2, y);
            gfx_PrintStringXY(text, 14, y);
        }
    } else if (type == HIST_RECV) {
        if (txt != UI_COLOR_UNSET) {
            gfx_SetTextFGColor(t->recv_text);
            gfx_PrintStringXY("< ", 2, y);
            gfx_SetTextFGColor(txt);
            gfx_PrintStringXY(text, 14, y);
        } else {
            gfx_SetTextFGColor(t->recv_text);
            gfx_PrintStringXY("< ", 2, y);
            gfx_PrintStringXY(text, 14, y);
        }
    } else {
        gfx_SetTextFGColor(t->sys);
        gfx_PrintStringXY(text, 2, y);
    }
}

/* Draws the scrollable chat area. In bubble mode, shows the most recent
 * messages that fit on screen (measuring each bubble's wrapped height).
 * In plain mode, just shows the last HISTORY_LINES text lines. */
static void draw_history(const netchat_ui_t *ui, const theme_t *t, uint8_t accent_index,
                         uint8_t txt) {
    if (!ui->bubbles) {
        uint8_t start = history_count > HISTORY_LINES ? history_count - HISTORY_LINES : 0;
        uint24_t y = CHAT_TOP;
        for (uint8_t i = start; i < history_count; i++) {
            draw_plain(t, accent_index, history_type[i], history[i], y, txt);
            y += CHAR_HEIGHT;
        }
        return;
    }

    uint24_t used = 0;
    uint8_t start = history_count;
    for (int16_t i = history_count - 1; i >= 0; i--) {
        uint24_t h = history_type[i] == HIST_SYS ? CHAR_HEIGHT + 2 : bubble_height(history[i]);
        if (used + h > CHAT_H) break;
        used += h;
        start = i;
    }

    uint24_t y = CHAT_TOP;
    for (uint8_t i = start; i < history_count; i++) {
        if (history_type[i] == HIST_SYS) draw_sys(t, history[i], &y);
        else draw_bubble(t, history_type[i] == HIST_SENT, history[i], &y, txt);
    }
}

static void redraw(bool connected, const netchat_ui_t *ui, const theme_t *t,
                   const char *compose_buf, bool alpha_mode,
                   const char *peer_typing, uint8_t anim_frame, uint8_t accent_index,
                   bool use_bgimg, bool bg_active, uint8_t bg_pct) {
    if (use_bgimg) {
        gfx_SetColor(0);
        gfx_FillScreen(0);
        bgimg_Draw(ui->bg_image);
        /* Dim the image with the chat background color so message text stays
         * readable (every 3rd scanline). */
        gfx_SetColor(chat_bg_slot(ui) != UI_COLOR_UNSET ? chat_bg_slot(ui) : t->bg);
        for (uint16_t y = 0; y < 240; y += 3)
            gfx_FillRectangle(0, y, 320, 1);
    } else {
        gfx_FillScreen(chat_bg_slot(ui) != UI_COLOR_UNSET ? chat_bg_slot(ui) : t->bg);
    }

    /* Header bar */
    gfx_SetColor(t->header);
    gfx_FillRectangle(0, 0, 320, 14);
    gfx_SetTextFGColor(t->header_text);
    gfx_PrintStringXY("netchat", 4, 3);

    gfx_SetColor(connected ? t->dot_online : t->dot_offline);
    gfx_FillCircle(310, 7, 4);

    if (peer_typing[0]) {
        /* Discord-style bouncing dots near the status area. */
        uint8_t bounce = anim_frame % 6;
        for (uint8_t d = 0; d < 3; d++) {
            uint8_t lift = (bounce == d || bounce == d + 3) ? 2 : 0;
            gfx_SetColor(t->header_text);
            gfx_FillCircle(280 + d * 7, 7 - lift, 2);
        }
    }

    if (bg_active) {
        /* Thin progress strip for an incoming background image. */
        gfx_SetColor(t->header);
        gfx_FillRectangle(0, 14, 320, 4);
        gfx_SetColor(wizard_palette[accent_index]);
        gfx_FillRectangle(0, 14, (320 * bg_pct) / 100, 4);
        gfx_SetColor(t->border);
        gfx_Rectangle(0, 14, 320, 4);
    }

    draw_history(ui, t, accent_index, chat_text_slot(ui));

    /* Live compose line -- always visible, updates as you type without
     * blocking anything else on screen. Left accent strip uses the
     * user's chosen color. */
    gfx_SetColor(t->compose_bg);
    gfx_FillRectangle(0, COMPOSE_TOP, 320, 12);
    gfx_SetColor(wizard_palette[accent_index]);
    gfx_FillRectangle(0, COMPOSE_TOP, 3, 12);

    gfx_SetTextFGColor(t->compose_text);
    gfx_SetTextXY(6, COMPOSE_TOP + 2);
    gfx_PrintString(alpha_mode ? "ABC>" : "123>");
    gfx_PrintString(compose_buf);
    gfx_PrintString("_"); /* cursor */

    gfx_SetTextFGColor(t->hint_text);
    gfx_SetTextXY(2, COMPOSE_TOP + 14);
    gfx_PrintString("[alpha] letters  [del] backspace");
    gfx_SetTextXY(2, COMPOSE_TOP + 23);
    gfx_PrintString("[mode] settings  [clear+clear] quit");

    gfx_BlitBuffer();
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void queue_text(char *write_buffer, uint16_t *write_index, size_t cap, const char *text);

static void ingest(char *assembly, size_t *assembly_len, netchat_config_t *config,
                   bool *config_dirty, char *peer_username, size_t peer_username_sz,
                   char *write_buffer, uint16_t *write_index, size_t write_cap) {
    size_t start = 0;
    for (size_t i = 0; i < *assembly_len; i++) {
        if (assembly[i] == '\n' || assembly[i] == '\r') {
            size_t len = i - start;
            if (len > 0) {
                static char line[RECV_ASSEMBLY + 1];
                if (len > RECV_ASSEMBLY) len = RECV_ASSEMBLY;
                memcpy(line, &assembly[start], len);
                line[len] = '\0';

                /* Background-image transfer lines are long and handled whole
                 * (the browser pauses relaying chat while one is in flight).
                 * While a receive is active, every non-image line is ignored
                 * so a stray message can't corrupt the stream. */
                if (!strncmp(line, "#BGIMG#", 7)) {
                    uint8_t slot = (uint8_t)(line[7] - '0');
                    if (slot < BGIMG_SLOTS && line[8] == '|')
                        if (bgimg_RxStart(slot, line + 9))
                            history_push(HIST_SYS, "Receiving background image...");
                } else if (!strncmp(line, "#BGDATA#", 8)) {
                    size_t hexlen = len - 8;
                    static uint8_t tmp[300];
                    size_t j = 0;
                    for (size_t k = 0; k + 1 < hexlen; k += 2) {
                        int hi = hex_val(line[8 + k]);
                        int lo = hex_val(line[8 + k + 1]);
                        if (hi < 0 || lo < 0) { j = 0; break; }
                        tmp[j++] = (uint8_t)((hi << 4) | lo);
                    }
                    if (j > 0) bgimg_RxData(tmp, j);
                } else if (!strcmp(line, "#BGEND#")) {
                    if (bgimg_RxEnd())
                        history_push(HIST_SYS, "Background image saved");
                    else
                        history_push(HIST_SYS, "Image receive failed");
                } else if (!strcmp(line, "#BGCANCEL#")) {
                    bgimg_RxReset();
                    history_push(HIST_SYS, "Image upload cancelled");
                } else if (!strcmp(line, "#BGLIST#")) {
                    char bm[BGIMG_SLOTS + 1];
                    bgimg_BuildSlotBitmap(bm);
                    char rep[BGIMG_SLOTS + 10]; /* "#BGSLOTS#" (9) + bitmap + NUL */
                    sprintf(rep, "#BGSLOTS#%s", bm);
                    queue_text(write_buffer, write_index, write_cap, rep);
                } else if (!bgimg_RxActive()) {
                    /* Short, message/control path. */
                    if (len > LINE_MAX) len = LINE_MAX;
                    line[len] = '\0';

                    if (!strncmp(line, "#TYPING#", 8)) {
                        strncpy(peer_username, line + 8, peer_username_sz - 1);
                        peer_username[peer_username_sz - 1] = '\0';
                    } else if (!strcmp(line, "#STOPTYPING#")) {
                        peer_username[0] = '\0';
                    } else if (!strncmp(line, "#SETID#", 7)) {
                        strncpy(config->device_token, line + 7, TOKEN_MAX);
                        config->device_token[TOKEN_MAX] = '\0';
                        *config_dirty = true;
                    } else if (!strncmp(line, "#SETNAME#", 9)) {
                        strncpy(config->username, line + 9, USERNAME_MAX);
                        config->username[USERNAME_MAX] = '\0';
                        *config_dirty = true;
                    } else if (!strncmp(line, "#SETPEER#", 9)) {
                        /* Only written when the browser's Peer ID actually
                         * changed, so this doesn't wear out flash on every
                         * reconnect -- effectively "saved once". */
                        strncpy(config->peer_id, line + 9, PEER_ID_MAX);
                        config->peer_id[PEER_ID_MAX] = '\0';
                        *config_dirty = true;
                    } else if (!strncmp(line, "#SENT#", 6)) {
                        /* Echo of a message the user typed in the browser --
                         * show it on our side as a sent bubble, not received. */
                        history_push(HIST_SENT, line + 6);
                    } else {
                        history_push(HIST_RECV, line);
                        peer_username[0] = '\0'; /* they sent, so they're not "typing" anymore */
                    }
                }
            }
            start = i + 1;
        }
    }
    size_t remaining = *assembly_len - start;
    memmove(assembly, &assembly[start], remaining);
    *assembly_len = remaining;
}

static void queue_text(char *write_buffer, uint16_t *write_index, size_t cap, const char *text) {
    size_t len = strlen(text);
    if (len > cap - *write_index - 1)
        len = cap - *write_index - 1;
    if ((int)len <= 0) return;
    memcpy(&write_buffer[*write_index], text, len);
    *write_index += len;
    write_buffer[(*write_index)++] = '\n';
}

/* Generates a 5-char Peer ID the first time (matches the browser's
 * charset). The calc owns this ID from then on -- it's written to flash
 * below and only ever replaced if the browser reports a collision. */
static void generate_peer_id(char *out, size_t out_sz) {
    static const char chars[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
    size_t n = out_sz - 1;
    if (n > 5) n = 5;
    srand(rtc_Time());
    for (size_t i = 0; i < n; i++)
        out[i] = chars[rand() % 32];
    out[n] = '\0';
}

/* Verified change-username form: you must know the current username AND
 * password to change it, so someone with a stolen calculator can't take
 * over the account. Same-screen arrow navigation, [clear] to back out. */
static void settings_change_username(netchat_config_t *config, const theme_t *t) {
    char cur_user[USERNAME_MAX + 1] = "";
    char cur_pass[PASSWORD_MAX + 1] = "";
    char new_user[USERNAME_MAX + 1] = "";
    while (1) {
        form_field_t fields[] = {
            { "Current username", cur_user, USERNAME_MAX, false },
            { "Current password", cur_pass, PASSWORD_MAX, true },
            { "New username", new_user, USERNAME_MAX, false },
        };
        if (!form_run(t, "Change Username", fields, 3)) return;
        if (strcmp(cur_user, config->username) || strcmp(cur_pass, config->password)) {
            form_alert(t, "Denied", "Wrong username or password");
            cur_user[0] = cur_pass[0] = new_user[0] = '\0';
            continue;
        }
        if (!new_user[0]) {
            form_alert(t, "Oops", "Username can't be empty");
            continue;
        }
        break;
    }
    strncpy(config->username, new_user, USERNAME_MAX);
    config->username[USERNAME_MAX] = '\0';
    config_Save(config);
    form_alert(t, "Done", "Username changed");
}

/* Same idea for the password: prove you know the current one, then type
 * (and confirm) the new one. All three fields live on one screen. */
static void settings_change_password(netchat_config_t *config, const theme_t *t) {
    char cur_pass[PASSWORD_MAX + 1] = "";
    char new_pass[PASSWORD_MAX + 1] = "";
    char confirm[PASSWORD_MAX + 1] = "";
    while (1) {
        form_field_t fields[] = {
            { "Current password", cur_pass, PASSWORD_MAX, true },
            { "New password", new_pass, PASSWORD_MAX, true },
            { "Confirm new", confirm, PASSWORD_MAX, true },
        };
        if (!form_run(t, "Change Password", fields, 3)) return;
        if (strcmp(cur_pass, config->password)) {
            form_alert(t, "Denied", "Wrong password");
            cur_pass[0] = new_pass[0] = confirm[0] = '\0';
            continue;
        }
        if (strcmp(new_pass, confirm)) {
            form_alert(t, "Oops", "New passwords don't match");
            new_pass[0] = confirm[0] = '\0';
            continue;
        }
        break;
    }
    strncpy(config->password, new_pass, PASSWORD_MAX);
    config->password[PASSWORD_MAX] = '\0';
    config_Save(config);
    form_alert(t, "Done", "Password changed");
}

/* Background image manager: list the 6 slots, pick the active one, or
 * delete an image. Images themselves are received from the site over the
 * link cable (see bgimg.c). */
static void settings_background_image(netchat_ui_t *ui) {
    const theme_t *t = &themes[ui->theme];
    int8_t cursor = 0;
    bool done = false;
    while (!done) {
        gfx_FillScreen(chat_bg_slot(ui) != UI_COLOR_UNSET ? chat_bg_slot(ui) : t->bg);
        gfx_SetTextFGColor(t->compose_text);
        gfx_PrintStringXY("Background image", 92, 10);

        for (uint8_t i = 0; i < BGIMG_SLOTS; i++) {
            uint24_t y = 32 + i * 26;
            if (i == (uint8_t)cursor) {
                gfx_SetColor(t->header);
                gfx_FillRectangle(4, y - 3, 312, 20);
            }
            char name[BGIMG_NAME_MAX + 1];
            if (bgimg_GetName(i, name, sizeof(name))) {
                char line[8 + BGIMG_NAME_MAX];
                sprintf(line, "%u: %s", i, name);
                gfx_SetTextFGColor(i == ui->bg_image ? t->dot_online : t->compose_text);
                gfx_PrintStringXY(line, 10, y);
            } else {
                char line[12];
                sprintf(line, "%u: <empty>", i);
                gfx_SetTextFGColor(t->hint_text);
                gfx_PrintStringXY(line, 10, y);
            }
        }

        char cur[24 + BGIMG_NAME_MAX];
        char cur_name[BGIMG_NAME_MAX + 1];
        if (ui->bg_image != BGIMG_NONE && bgimg_GetName(ui->bg_image, cur_name, sizeof(cur_name)))
            sprintf(cur, "current: %s", cur_name);
        else
            sprintf(cur, "current: none");
        gfx_SetTextFGColor(t->sys);
        gfx_PrintStringXY(cur, 4, 208);
        gfx_SetTextFGColor(t->hint_text);
        gfx_PrintStringXY("[up/down] move  [enter] set  [del] delete", 2, 224);
        gfx_BlitBuffer();

        uint8_t key = 0;
        while (!key) key = os_GetCSC();

        switch (key) {
            case sk_Up:
                if (cursor > 0) cursor--;
                break;
            case sk_Down:
                if (cursor < BGIMG_SLOTS - 1) cursor++;
                break;
            case sk_Enter:
                if (bgimg_Exists((uint8_t)cursor)) {
                    ui->bg_image = (uint8_t)cursor;
                    ui_Save(ui);
                }
                done = true;
                break;
            case sk_Del:
                if (bgimg_Exists((uint8_t)cursor)) {
                    gfx_SetTextFGColor(t->compose_text);
                    gfx_PrintStringXY("Delete this image?", 90, 100);
                    gfx_SetTextFGColor(t->hint_text);
                    gfx_PrintStringXY("[enter] yes  [clear] no", 82, 120);
                    gfx_BlitBuffer();
                    uint8_t k2 = 0;
                    while (!k2) k2 = os_GetCSC();
                    if (k2 == sk_Enter) {
                        bgimg_Delete((uint8_t)cursor);
                        if (ui->bg_image == (uint8_t)cursor) {
                            ui->bg_image = BGIMG_NONE;
                            ui_Save(ui);
                        }
                    }
                }
                break;
            case sk_Clear:
            case sk_Mode:
                done = true;
                break;
        }
    }
}

/* Small settings menu, reachable any time via [mode]. Lets you redo any
 * one wizard step without wiping the others, plus the look-and-feel
 * options (theme, bubbles, background/text colors). Every sub-screen has
 * [clear] as its back button, and the menu itself exits to chat with
 * [clear]/[mode]/8. */
static void settings_menu(netchat_config_t *config, netchat_ui_t *ui) {
    const theme_t *t = &themes[ui->theme];
    bool done = false;
    while (!done) {
        gfx_FillScreen(chat_bg_slot(ui) != UI_COLOR_UNSET ? chat_bg_slot(ui) : t->bg);
        gfx_SetTextFGColor(t->compose_text);
        gfx_PrintStringXY("Settings", 120, 20);
        gfx_PrintStringXY("1: Change username", 40, 60);
        gfx_PrintStringXY("2: Change color", 40, 80);
        gfx_PrintStringXY("3: Change password", 40, 100);

        char line[40];
        sprintf(line, "4: Theme: %s", theme_names[ui->theme]);
        gfx_PrintStringXY(line, 40, 60);
        sprintf(line, "5: Bubbles: %s", ui->bubbles ? "On" : "Off");
        gfx_PrintStringXY(line, 40, 76);
        gfx_PrintStringXY("6: Background color", 40, 92);
        gfx_PrintStringXY("7: Text color", 40, 108);
        gfx_PrintStringXY("8: Background image", 40, 124);
        gfx_PrintStringXY("9: Back to chat", 40, 206);
        gfx_SetTextFGColor(t->hint_text);
        gfx_PrintStringXY("[clear] back", 2, 228);
        gfx_BlitBuffer();

        uint8_t key = 0;
        while (!key) key = os_GetCSC();

        switch (key) {
            case sk_1: settings_change_username(config, t); break;
            case sk_2: if (wizard_SetColor(config, t)) config_Save(config); break;
            case sk_3: settings_change_password(config, t); break;
            case sk_4:
                ui->theme = (ui->theme + 1) % NUM_THEMES;
                ui_Save(ui);
                t = &themes[ui->theme];
                break;
            case sk_5:
                ui->bubbles = !ui->bubbles;
                ui_Save(ui);
                break;
            case sk_6: {
                uint8_t picked = wizard_PickColor(
                    t, UI_BG_INDEX, "Background color", pick_rgb, PICK_COLORS,
                    ui->bg_color != UI_COLOR_UNSET ? ui->bg_color : 0);
                if (picked != UI_COLOR_UNSET) {
                    ui->bg_color = picked;
                    ui_Save(ui);
                }
                break;
            }
            case sk_7: {
                uint8_t picked = wizard_PickColor(
                    t, UI_TEXT_INDEX, "Text color", pick_rgb, PICK_COLORS,
                    ui->text_color != UI_COLOR_UNSET ? ui->text_color : 0);
                if (picked != UI_COLOR_UNSET) {
                    ui->text_color = picked;
                    ui_Save(ui);
                }
                break;
            }
            case sk_8:
                settings_background_image(ui);
                break;
            case sk_9:
            case sk_Mode:
            case sk_Clear:
                done = true;
                break;
        }
    }
}

int main(void) {
    bool connected = false;
    bool config_sent = false;
    uint16_t write_index = 0;
    size_t read_size = 0, write_size = 0;
    static char read_buffer[READ_CHUNK];
    static char write_buffer[WRITE_BUFFER_SIZE];
    /* A long line (e.g. #BGDATA#) spans several 64-byte reads, and the
     * read that finishes it can arrive with up to READ_CHUNK bytes still
     * pending, so leave slack beyond RECV_ASSEMBLY. */
    static char assembly[RECV_ASSEMBLY + READ_CHUNK + 1];
    size_t assembly_len = 0;
    netchat_config_t config;
    netchat_ui_t ui;

    char compose_buf[COMPOSE_MAX + 1] = "";
    uint8_t compose_len = 0;
    bool alpha_mode = true;
    bool typing_sent = false;
    char peer_typing[USERNAME_MAX + 1] = "";
    bool clear_armed = false; /* first [clear] with empty buffer arms quit; second confirms */
    uint8_t anim_frame = 0;
    uint16_t anim_tick = 0;

    gfx_Begin();
    wizard_LoadPalette();
    gfx_SetDrawBuffer();

    ui_Load(&ui);
    wizard_ApplyCustomColors(&ui);

    if (!config_Load(&config)) {
        /* First run (or the config was wiped). Completing the wizard saves
         * the config to flash archive; cancelling at any step exits cleanly
         * so nothing is half-written. */
        if (!wizard_Run(&config, &themes[ui.theme])) {
            gfx_End();
            return 0;
        }
        config_Save(&config);
    }

    history_push(HIST_SYS, "netchat -- waiting for the site...");
    redraw(connected, &ui, &themes[ui.theme], compose_buf, alpha_mode,
           peer_typing, anim_frame, config.color_index,
           ui.bg_image != BGIMG_NONE && bgimg_Exists(ui.bg_image),
           bgimg_RxActive(), bgimg_RxPercent());

    while (true) {
        link_devices_t *devices = (link_devices_t *)link_Devices();
        bool now_connected = devices->count;
        bool needs_redraw = false;

        if (now_connected && !connected) {
            history_push(HIST_SYS, "-- connected --");
            config_sent = false;
            bgimg_RxReset();
            needs_redraw = true;
        } else if (!now_connected && connected) {
            history_push(HIST_SYS, "-- disconnected --");
            bgimg_RxReset();
            needs_redraw = true;
        }
        connected = now_connected;

        if (connected && !config_sent) {
            /* If the calc has no Peer ID yet (never had one, or reset to
             * empty), generate and archive one right here so the device is
             * permanently identifiable. */
            if (!config.peer_id[0]) {
                generate_peer_id(config.peer_id, sizeof(config.peer_id));
                config_Save(&config);
            }
            char config_line[180];
            snprintf(config_line, sizeof(config_line),
                      "#CONFIG#username=%s;color=%s;token=%s;password=%s;peer_id=%s",
                      config.username, wizard_palette_hex[config.color_index],
                      config.device_token, config.password, config.peer_id);
            queue_text(write_buffer, &write_index, sizeof(write_buffer), config_line);

            /* Offer the 20-color image palette so the browser can quantize
             * background images to exactly the indices the calc can draw.
             * Format: #PAL#<count>#<idx:rrggbb,...> */
            {
                static char pal_hex[IMAGE_PAL_SIZE * 9 + 1];
                static char pal_line[IMAGE_PAL_SIZE * 9 + 12];
                uint8_t pal[IMAGE_PAL_SIZE][4];
                wizard_ImagePalette(pal);
                int pos = 0;
                for (int i = 0; i < IMAGE_PAL_SIZE; i++)
                    pos += sprintf(pal_hex + pos, "%02x:%02x%02x%02x,",
                                   pal[i][0], pal[i][1], pal[i][2], pal[i][3]);
                pal_hex[pos - 1] = '\0'; /* drop trailing comma */
                sprintf(pal_line, "#PAL#%d#%s", IMAGE_PAL_SIZE, pal_hex);
                queue_text(write_buffer, &write_index, sizeof(write_buffer), pal_line);
            }
            config_sent = true;
        }

        /* Pull in any completed read, requeue a fresh one. */
        if (read_size != LINK_PENDING) {
            if (read_size > 0) {
                if (assembly_len + read_size > sizeof(assembly)) assembly_len = 0;
                memcpy(&assembly[assembly_len], read_buffer, read_size);
                assembly_len += read_size;
                bool config_dirty = false;
                ingest(assembly, &assembly_len, &config, &config_dirty, peer_typing, sizeof(peer_typing),
                       write_buffer, &write_index, sizeof(write_buffer));
                if (config_dirty) config_Save(&config);
                needs_redraw = true;
            }
            read_size = 0;
            if (connected) {
                read_size = sizeof(read_buffer);
                link_QueueRead(devices->ids[0], read_buffer, &read_size);
            }
        }

        /* Flush any completed write, advance the write queue. */
        if (write_size != LINK_PENDING) {
            if (write_size > 0) {
                write_index -= write_size;
                memmove(write_buffer, &write_buffer[write_size], write_index);
            }
            write_size = 0;
            if (connected && write_index) {
                write_size = write_index;
                link_QueueWrite(devices->ids[0], write_buffer, &write_size);
            }
        }

        uint8_t key = os_GetCSC();

        if (key) {
            if (key == sk_Mode) {
                settings_menu(&config, &ui);
                needs_redraw = true;
            } else if (key == sk_Alpha) {
                alpha_mode = !alpha_mode;
                needs_redraw = true;
            } else if (key == sk_Del) {
                if (compose_len > 0) {
                    compose_buf[--compose_len] = '\0';
                    needs_redraw = true;
                }
            } else if (key == sk_Clear) {
                if (compose_len > 0) {
                    compose_len = 0;
                    compose_buf[0] = '\0';
                    clear_armed = false;
                    needs_redraw = true;
                } else if (clear_armed) {
                    break; /* second [clear] on an empty line quits */
                } else {
                    clear_armed = true;
                }
            } else if (key == sk_Enter) {
                if (bgimg_RxActive()) {
                    compose_len = 0;
                    compose_buf[0] = '\0';
                    history_push(HIST_SYS, "(receiving image)");
                } else if (compose_len > 0 && connected) {
                    history_push(HIST_SENT, compose_buf);
                    queue_text(write_buffer, &write_index, sizeof(write_buffer), compose_buf);
                } else if (compose_len > 0 && !connected) {
                    history_push(HIST_SYS, "(not connected, message dropped)");
                }
                compose_len = 0;
                compose_buf[0] = '\0';
                needs_redraw = true;
            } else {
                char c = input_KeyToChar(key, alpha_mode);
                if (c && compose_len < COMPOSE_MAX) {
                    compose_buf[compose_len++] = c;
                    compose_buf[compose_len] = '\0';
                    needs_redraw = true;
                }
            }

            if (key != sk_Clear) clear_armed = false;
        }

        /* Typing indicator: fire once on the empty->nonempty transition,
         * and once more on the nonempty->empty transition (send, clear,
         * or backspacing to nothing). */
        if (connected && !bgimg_RxActive()) {
            if (compose_len > 0 && !typing_sent) {
                char line[16 + USERNAME_MAX];
                snprintf(line, sizeof(line), "#TYPING#%s", config.username);
                queue_text(write_buffer, &write_index, sizeof(write_buffer), line);
                typing_sent = true;
            } else if (compose_len == 0 && typing_sent) {
                queue_text(write_buffer, &write_index, sizeof(write_buffer), "#STOPTYPING#");
                typing_sent = false;
            }
        }

        if (peer_typing[0]) {
            anim_tick++;
            if (anim_tick % 400 == 0) {
                anim_frame++;
                needs_redraw = true;
            }
        }

        if (needs_redraw) redraw(connected, &ui, &themes[ui.theme], compose_buf, alpha_mode,
                                 peer_typing, anim_frame, config.color_index,
                                 ui.bg_image != BGIMG_NONE && bgimg_Exists(ui.bg_image),
                                 bgimg_RxActive(), bgimg_RxPercent());
    }

    gfx_End();
    return 0;
}
