/* mhpatcher: Monster Hunter online patch installer and save backup tool.
 *
 *   language -> main menu
 *     Install patches: memory card -> scan -> game -> confirm -> install
 *     Backup saves:    memory card -> USB drive -> scan -> game ->
 *                      back up / restore -> confirm -> copy */
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libcdvd.h>
#include <string.h>
#include <stdio.h>

#include "ps2/video.h"
#include "ps2/pad.h"
#include "ps2/nvm_ilink.h"
#include "ps2/mcard.h"
#include "ps2/usb.h"
#include "dnas/patcher.h"
#include "app/install.h"
#include "app/saves.h"

typedef enum {
    ST_LANG, ST_MODE, ST_NO_ILINK, ST_MCSEL, ST_NO_MC, ST_SCAN,
    ST_GAMESEL, ST_NO_SAVES, ST_CONFIRM, ST_INSTALL, ST_DONE, ST_FAIL,
    /* save backup */
    ST_USB_WAIT, ST_USB_NONE, ST_BK_SCAN, ST_BK_LIST, ST_BK_ACTION,
    ST_BK_CONFIRM, ST_BK_RUN, ST_BK_DONE, ST_BK_FAIL
} state;

typedef enum { MODE_INSTALL = 0, MODE_BACKUP = 1 } app_mode;
static app_mode mode = MODE_INSTALL;

/* Set by screens that flip the frame themselves before blocking I/O. */
static int frame_done;

static int usb_frames;      /* frames on the USB screen, 0 = just entered */

static ui_lang lang = LANG_EN;
static ilink_info ilink;
static int sel_port = 0;
static int have_mh2 = 0, have_mhg = 0;
static int game_cur = 0;
static mh_game sel_game = GAME_MH2;
static install_result last_result;

static char ilink_hex[20];

static void load_modules(void) {
    SifInitRpc(0);
    sceCdInit(SCECdINoD);
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:MCMAN", 0, NULL);
    SifLoadModule("rom0:MCSERV", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
}

static void hexid(const uint8_t *id, char *out) {
    static const char H[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 8; i++) { out[i*2] = H[id[i] >> 4]; out[i*2+1] = H[id[i] & 15]; }
    out[16] = 0;
}

/* ---- layout ------------------------------------------------------------- */
#define UI_L     40.0f        /* content area left edge */
#define UI_W     560.0f       /* content area width */
#define ROW_H    28.0f
#define ROW_GAP  8.0f
#define BODY_PAD 14.0f
#define AREA_TOP 56.0f        /* panels are centred between these */
#define AREA_BOT 392.0f

static float g_py;            /* top of the current panel's content */

static void header(atlas_str title) {
    vid_scrim(0, 0, 640, 26, COL_HEADER);
    vid_rect(0, 26, 640, 1, COL_GOLD);
    vid_phrase(STR_APP_TITLE, lang, 12, 4, COL_GOLD);
    char note[40];
    snprintf(note, sizeof note, "i.Link ID  %s", ilink.ok ? ilink_hex : "----------------");
    vid_text(note, 640 - 12 - vid_text_w(note), 8, COL_BONE);
    int th = vid_phrase_h(title, lang);
    vid_rect(UI_L, 34, 3, th, COL_GOLD);
    vid_phrase(title, lang, UI_L + 10, 34, COL_BONE);
    const char *cr = "mholdschool.com";
    vid_text(cr, 320 - vid_text_w(cr) / 2.0f, 428, COL_GOLD);
    /* version, bottom right, on the credit's baseline */
    float base = 428 + ATLAS_ASCII_BASELINE;
    vid_phrase(STR_VERSION, lang, 640 - 12 - vid_phrase_w(STR_VERSION, lang),
               base - atlas_baseline[STR_VERSION][lang], COL_WHITE);
}
/* Panel for `ch` pixels of content, centred vertically. */
static void body(float ch) {
    float h = ch + 2 * BODY_PAD;
    float py = AREA_TOP + (AREA_BOT - AREA_TOP - h) / 2.0f;
    if (py < AREA_TOP) py = AREA_TOP;
    vid_scrim(UI_L - 10, py, UI_W + 20, h, COL_PANELT);
    vid_rect(UI_L - 10, py, UI_W + 20, 2, COL_GOLD);
    vid_rect(UI_L - 10, py + h - 2, UI_W + 20, 2, COL_GOLD);
    g_py = py + BODY_PAD;
}
static float rows_h(int n) { return n * ROW_H + (n - 1) * ROW_GAP; }
static float row_y(int i)  { return g_py + i * (ROW_H + ROW_GAP); }

/* "[btn] label" with the prompt and the phrase on a shared baseline `by`. */
static float footer_item(float x, float by, const char *prompt, atlas_str label) {
    vid_text(prompt, x, by - 16, COL_GOLD);
    x += vid_text_w(prompt);
    int h = vid_phrase_h(label, lang);
    vid_phrase(label, lang, x, by - (h - 3), COL_BONE);
    return x + vid_phrase_w(label, lang) + 22;
}
static void footer(int confirm, int back, const char *extra) {
    float by = 420, x = UI_L;
    if (confirm) x = footer_item(x, by, "[O] ", STR_BTN_CONFIRM);
    if (back)    x = footer_item(x, by, "[X] ", STR_BTN_BACK);
    if (extra) {
        vid_text(extra, x, by - 16, COL_GOLD); x += vid_text_w(extra) + 6;
        footer_item(x, by, "", STR_BTN_RESCAN);
    }
}

static void menu_row(float x, float y, float w, int selected) {
    vid_scrim(x, y, w, ROW_H, selected ? COL_SELT : COL_PANELT);
    vid_rect(x, y, selected ? 5 : 2, ROW_H, selected ? COL_GOLD : COL_DIM);
    if (selected) { vid_rect(x, y, w, 1, COL_GOLD); vid_rect(x, y + ROW_H - 1, w, 1, COL_GOLD); }
}

/* ---- screens ------------------------------------------------------------ */
static const char *err_name(install_result r) {
    switch (r) {
        case INS_OK:              return "ok";
        case INS_ERR_NO_SAVE:     return "no valid save on card";
        case INS_ERR_BUILD:       return "patch build error";
        case INS_ERR_WRITE:       return "memory card write failed";
        case INS_ERR_VERIFY:      return "verify mismatch (save restored)";
        case INS_ERR_VERIFY_LOST: return "verify failed, restore failed";
    }
    return "unknown";
}

static void row_label(int i, atlas_str label, uint32_t c) {
    vid_phrase(label, lang, UI_L + 18, row_y(i) + 7, c);
}

static state scr_lang(unsigned k) {
    static int cur = 0;
    header(STR_CHOOSE_LANG);
    body(rows_h(2));
    menu_row(UI_L, row_y(0), UI_W, cur == 0);
    menu_row(UI_L, row_y(1), UI_W, cur == 1);
    vid_phrase(STR_LANG_EN, LANG_EN, UI_L + 18, row_y(0) + 7, cur == 0 ? COL_BONE : COL_DIM);
    vid_phrase(STR_LANG_JP, LANG_JP, UI_L + 18, row_y(1) + 7, cur == 1 ? COL_BONE : COL_DIM);
    footer(1, 0, NULL);
    if (k & BTN_UP)   cur = 0;
    if (k & BTN_DOWN) cur = 1;
    if (k & BTN_CIRCLE) { lang = cur ? LANG_JP : LANG_EN; return ST_MODE; }
    return ST_LANG;
}

/* mcGetInfo blocks, so card presence is polled on entry and on START only. */
static int mc_p0, mc_p1, mc_polled;
static void mc_repoll(int *cur) {
    mc_p0 = mc_present(0);
    mc_p1 = mc_present(1);
    if (cur) *cur = mc_p0 ? 0 : (mc_p1 ? 1 : 0);
    mc_polled = 1;
}

static state scr_mcsel(unsigned k) {
    static int cur = 0;
    if (!mc_polled) mc_repoll(&cur);
    if (!mc_p0 && !mc_p1) { mc_polled = 0; return ST_NO_MC; }
    header(STR_SELECT_MC);
    body(rows_h(2));
    menu_row(UI_L, row_y(0), UI_W, cur == 0);
    menu_row(UI_L, row_y(1), UI_W, cur == 1);
    row_label(0, STR_MC_SLOT1, mc_p0 ? COL_BONE : COL_DIM);
    row_label(1, STR_MC_SLOT2, mc_p1 ? COL_BONE : COL_DIM);
    vid_phrase(mc_p0 ? STR_MC_PRESENT : STR_MC_ABSENT, lang, UI_L + UI_W - 150, row_y(0) + 7, mc_p0 ? COL_GREEN : COL_DIM);
    vid_phrase(mc_p1 ? STR_MC_PRESENT : STR_MC_ABSENT, lang, UI_L + UI_W - 150, row_y(1) + 7, mc_p1 ? COL_GREEN : COL_DIM);
    footer(1, 1, "[START]");
    if ((k & BTN_UP)   && mc_p0) cur = 0;
    if ((k & BTN_DOWN) && mc_p1) cur = 1;
    if (k & BTN_START) mc_polled = 0;
    if (k & BTN_CROSS) { mc_polled = 0; return ST_MODE; }
    if (k & BTN_CIRCLE) {
        if ((cur == 0 && mc_p0) || (cur == 1 && mc_p1)) {
            sel_port = cur; mc_polled = 0; usb_frames = 0;
            return mode == MODE_INSTALL ? ST_SCAN : ST_USB_WAIT;
        }
    }
    return ST_MCSEL;
}

static state scr_no_mc(unsigned k) {
    header(STR_SELECT_MC);
    body(22);
    vid_phrase(STR_MC_NONE, lang, UI_L, g_py + 3, COL_TEXT);
    footer(0, 1, "START");
    if (k & BTN_CROSS) return ST_MODE;
    if (k & BTN_START) return ST_MCSEL;
    return ST_NO_MC;
}

static state scr_scan(unsigned k) {
    (void)k;
    header(STR_SCANNING);
    body(22);
    vid_phrase(STR_SCANNING, lang, UI_L, g_py + 3, COL_TEXT);
    vid_end();
    frame_done = 1;
    have_mh2 = mc_has_mh2(sel_port);
    have_mhg = mc_has_mhg(sel_port);
    if (!have_mh2 && !have_mhg) return ST_NO_SAVES;
    sel_game = have_mh2 ? GAME_MH2 : GAME_MHG;
    game_cur = have_mh2 ? 0 : 1;
    return ST_GAMESEL;
}

static state scr_gamesel(unsigned k) {
    header(STR_FOUND_TITLE);
    body(rows_h(2));
    menu_row(UI_L, row_y(0), UI_W, game_cur == 0 && have_mh2);
    menu_row(UI_L, row_y(1), UI_W, game_cur == 1 && have_mhg);
    row_label(0, STR_MH2_LABEL, have_mh2 ? COL_BONE : COL_DIM);
    row_label(1, STR_MHG_LABEL, have_mhg ? COL_BONE : COL_DIM);
    vid_phrase(have_mh2 ? STR_FOUND : STR_NOT_FOUND, lang, UI_L + UI_W - 120, row_y(0) + 7, have_mh2 ? COL_GREEN : COL_DIM);
    vid_phrase(have_mhg ? STR_FOUND : STR_NOT_FOUND, lang, UI_L + UI_W - 120, row_y(1) + 7, have_mhg ? COL_GREEN : COL_DIM);
    footer(1, 1, NULL);

    if ((k & BTN_UP)   && have_mh2) game_cur = 0;
    if ((k & BTN_DOWN) && have_mhg) game_cur = 1;
    if (k & BTN_CROSS) return ST_MCSEL;
    if (k & BTN_CIRCLE) {
        if (game_cur == 0 && have_mh2) { sel_game = GAME_MH2; return ST_CONFIRM; }
        if (game_cur == 1 && have_mhg) { sel_game = GAME_MHG; return ST_CONFIRM; }
    }
    return ST_GAMESEL;
}

static state scr_no_saves(unsigned k) {
    header(STR_FOUND_TITLE);
    body(22);
    vid_phrase(STR_NO_SAVES, lang, UI_L, g_py + 3, COL_TEXT);
    footer(0, 1, NULL);
    if (k & BTN_CROSS) return ST_MCSEL;
    return ST_NO_SAVES;
}

static state scr_confirm(unsigned k) {
    header(STR_CONFIRM_TTL);
    body(150);
    float y = g_py;
    vid_scrim(UI_L, y, UI_W, 40, COL_SELT);
    vid_rect(UI_L, y, 5, 40, COL_RED);
    vid_phrase(STR_BACKUP_WARN, lang, UI_L + 14, y + 11, COL_BONE);
    vid_phrase(STR_CONFIRM_1, lang, UI_L, y + 64, COL_TEXT);
    menu_row(UI_L, y + 90, UI_W, 1);
    vid_phrase(sel_game == GAME_MH2 ? STR_MH2_LABEL : STR_MHG_LABEL, lang, UI_L + 18, y + 97, COL_GOLD);
    vid_phrase(STR_A_BACKUP, lang, UI_L, y + 134, COL_DIM);
    footer(1, 1, NULL);
    if (k & BTN_CROSS)  return ST_GAMESEL;
    if (k & BTN_CIRCLE) return ST_INSTALL;
    return ST_CONFIRM;
}

static state scr_install(unsigned k) {
    (void)k;
    header(STR_INJECTING);
    body(24);
    vid_phrase(STR_INSTALLING, lang, UI_L, g_py + 3, COL_DIM);
    vid_end();
    frame_done = 1;
    last_result = install_game(sel_game, sel_port, ilink.id);
    return (last_result == INS_OK) ? ST_DONE : ST_FAIL;
}

static state scr_done(unsigned k) {
    header(STR_OK_TITLE);
    body(124);
    float y = g_py;
    vid_rect(UI_L, y, 4, 42, COL_GREEN);
    vid_phrase(STR_OK_BODY,  lang, UI_L + 14, y,      COL_BONE);
    vid_phrase(STR_OK_BODY2, lang, UI_L + 14, y + 24, COL_BONE);
    vid_phrase(STR_ILINK_NOTE,  lang, UI_L, y + 84,  COL_DIM);
    vid_phrase(STR_ILINK_NOTE2, lang, UI_L, y + 106, COL_DIM);
    footer(1, 0, NULL);
    if (k & BTN_CIRCLE) return ST_MCSEL;
    return ST_DONE;
}

static state scr_fail(unsigned k) {
    header(STR_FAIL_TITLE);
    body(mc_last_error()[0] ? 84 : 60);
    float y = g_py;
    vid_rect(UI_L, y, 4, 20, COL_RED);
    vid_phrase(STR_FAIL_BODY, lang, UI_L + 14, y, COL_BONE);
    char code[48];
    snprintf(code, sizeof code, "[%d] %s", (int)last_result, err_name(last_result));
    vid_text(code, UI_L, y + 40, COL_GOLD);
    if (mc_last_error()[0]) vid_text(mc_last_error(), UI_L, y + 64, COL_DIM);
    footer(1, 0, NULL);
    if (k & BTN_CIRCLE) return ST_GAMESEL;
    return ST_FAIL;
}

/* ---- main menu ---------------------------------------------------------- */

static state scr_mode(unsigned k) {
    static int cur = 0;
    header(STR_MODE_TITLE);
    body(rows_h(2));
    menu_row(UI_L, row_y(0), UI_W, cur == 0);
    menu_row(UI_L, row_y(1), UI_W, cur == 1);
    row_label(0, STR_MODE_INSTALL, cur == 0 ? COL_BONE : COL_DIM);
    row_label(1, STR_MODE_BACKUP,  cur == 1 ? COL_BONE : COL_DIM);
    footer(1, 1, NULL);
    if (k & BTN_UP)   cur = 0;
    if (k & BTN_DOWN) cur = 1;
    if (k & BTN_CROSS) return ST_LANG;
    if (k & BTN_CIRCLE) {
        mode = cur ? MODE_BACKUP : MODE_INSTALL;
        /* the patch is sealed to the i.Link ID */
        if (mode == MODE_INSTALL && !ilink.ok) return ST_NO_ILINK;
        return ST_MCSEL;
    }
    return ST_MODE;
}

static state scr_no_ilink(unsigned k) {
    header(STR_ILINK_TITLE);
    body(20);
    vid_rect(UI_L, g_py, 4, 20, COL_RED);
    vid_phrase(STR_E_NO_ILINK, lang, UI_L + 14, g_py + 3, COL_BONE);
    footer(0, 1, NULL);
    if (k & BTN_CROSS) return ST_MODE;
    return ST_NO_ILINK;
}

/* ---- save backup -------------------------------------------------------- */

static const atlas_str SG_LABEL[SG_COUNT] = {
    STR_MH2_LABEL, STR_MHGJ_LABEL, STR_MHGK_LABEL, STR_MH1J_LABEL, STR_MH1U_LABEL, STR_MH1E_LABEL
};
static int bk_card[SG_COUNT], bk_usb[SG_COUNT];
static int bk_cur;
static save_game bk_game;
static int bk_restore;                 /* 0 = card to USB, 1 = USB to card */
static int bk_act_cur;
static save_result bk_result;
static int usb_driver_ok = 1;
static int drive_frames;
static char bk_safety[96];             /* name of the pre-restore copy */

#define USB_WAIT_FRAMES   300          /* ~5 s for a drive to enumerate */
#define USB_DRIVER_FRAMES 600          /* ~10 s for the drivers to load */
#define COL_CARD_X (UI_L + 300)
#define COL_USB_X  (UI_L + 430)

static state scr_usb_wait(unsigned k) {
    (void)k;
    header(STR_USB_TITLE);
    body(22);
    vid_phrase(STR_USB_SEARCH, lang, UI_L, g_py + 3, COL_TEXT);
    if (usb_frames == 0) { usb_init_start(); drive_frames = 0; }
    usb_frames++;
    int st = usb_init_state();
    if (st == USB_INIT_FAILED || st == USB_INIT_BUSY ||
        (st == USB_INIT_PENDING && usb_frames > USB_DRIVER_FRAMES)) {
        usb_driver_ok = 0;
        return ST_USB_NONE;
    }
    if (st != USB_INIT_OK) return ST_USB_WAIT;
    usb_driver_ok = 1;
    drive_frames++;
    if (drive_frames % 15 == 1 && usb_ready()) return ST_BK_SCAN;
    if (drive_frames > USB_WAIT_FRAMES) return ST_USB_NONE;
    return ST_USB_WAIT;
}

static state scr_usb_none(unsigned k) {
    int busy = usb_init_state() == USB_INIT_BUSY;
    header(STR_USB_TITLE);
    int has_detail = (busy || !usb_driver_ok) && usb_init_detail()[0];
    body((busy ? 82 : usb_driver_ok ? 50 : 22) + (has_detail ? 26 : 0));
    float y = g_py;
    vid_rect(UI_L, y, 4, 20, COL_RED);
    if (busy) {
        vid_phrase(STR_E_USB_BUSY,  lang, UI_L + 14, y + 3, COL_BONE);
        vid_phrase(STR_E_USB_BUSY2, lang, UI_L, y + 37, COL_TEXT);
        vid_phrase(STR_E_USB_BUSY3, lang, UI_L, y + 61, COL_TEXT);
    } else if (!usb_driver_ok) {
        vid_phrase(STR_E_DRIVER, lang, UI_L + 14, y + 3, COL_BONE);
    } else {
        vid_phrase(STR_USB_NONE, lang, UI_L + 14, y + 3, COL_BONE);
        vid_phrase(STR_USB_NONE2, lang, UI_L, y + 31, COL_TEXT);
    }
    const char *detail = usb_init_detail();
    if ((busy || !usb_driver_ok) && detail[0])
        vid_text(detail, UI_L + 14, g_py + (busy ? 82 : 26), COL_GOLD);
    int retry = !busy && usb_init_state() != USB_INIT_FAILED;
    footer(0, 1, retry ? "[START]" : NULL);
    if (k & BTN_CROSS) { usb_frames = 0; return ST_MCSEL; }
    if ((k & BTN_START) && retry) { usb_frames = 1; drive_frames = 0; return ST_USB_WAIT; }
    return ST_USB_NONE;
}

static int bk_any(void) {
    for (int g = 0; g < SG_COUNT; g++) if (bk_card[g] || bk_usb[g]) return 1;
    return 0;
}

static state scr_bk_scan(unsigned k) {
    (void)k;
    header(STR_SCANNING);
    body(22);
    vid_phrase(STR_SCANNING, lang, UI_L, g_py + 3, COL_TEXT);
    vid_end();
    frame_done = 1;
    if (save_scan(sel_port, bk_card, bk_usb) < 0) return ST_MCSEL;
    if (!usb_ready()) { usb_frames = 1; drive_frames = 0; return ST_USB_NONE; }
    if (!(bk_card[bk_cur] || bk_usb[bk_cur]))
        for (int g = SG_COUNT - 1; g >= 0; g--) if (bk_card[g] || bk_usb[g]) bk_cur = g;
    return ST_BK_LIST;
}

static void status_word(int found, float x, float y) {
    vid_phrase(found ? STR_FOUND : STR_NOT_FOUND, lang, x, y, found ? COL_GREEN : COL_DIM);
}

static state scr_bk_list(unsigned k) {
    int any = bk_any();
    header(STR_BK_TITLE);
    body(22 + rows_h(SG_COUNT) + (any ? 0 : 34));
    vid_phrase(STR_COL_CARD, lang, COL_CARD_X, g_py, COL_GOLD);
    vid_phrase(STR_COL_USB,  lang, COL_USB_X,  g_py, COL_GOLD);
    g_py += 22;
    for (int g = 0; g < SG_COUNT; g++) {
        int avail = bk_card[g] || bk_usb[g];
        menu_row(UI_L, row_y(g), UI_W, avail && g == bk_cur);
        row_label(g, SG_LABEL[g], avail ? COL_BONE : COL_DIM);
        status_word(bk_card[g], COL_CARD_X, row_y(g) + 7);
        status_word(bk_usb[g],  COL_USB_X,  row_y(g) + 7);
    }
    if (!any) vid_phrase(STR_BK_NONE, lang, UI_L, row_y(SG_COUNT) + 4, COL_TEXT);
    footer(any, 1, "[START]");

    if (k & BTN_UP)
        for (int g = bk_cur - 1; g >= 0; g--) if (bk_card[g] || bk_usb[g]) { bk_cur = g; break; }
    if (k & BTN_DOWN)
        for (int g = bk_cur + 1; g < SG_COUNT; g++) if (bk_card[g] || bk_usb[g]) { bk_cur = g; break; }
    if (k & BTN_START) return ST_BK_SCAN;
    if (k & BTN_CROSS) return ST_MCSEL;
    if ((k & BTN_CIRCLE) && any && (bk_card[bk_cur] || bk_usb[bk_cur])) {
        bk_game = bk_cur;
        bk_act_cur = bk_card[bk_game] ? 0 : 1;
        return ST_BK_ACTION;
    }
    return ST_BK_LIST;
}

static state scr_bk_action(unsigned k) {
    int can[2] = { bk_card[bk_game], bk_usb[bk_game] };
    header(STR_ACT_TITLE);
    body(ROW_H + 14 + rows_h(2));
    menu_row(UI_L, g_py, UI_W, 1);
    vid_phrase(SG_LABEL[bk_game], lang, UI_L + 18, g_py + 7, COL_GOLD);
    g_py += ROW_H + 14;
    for (int i = 0; i < 2; i++) {
        menu_row(UI_L, row_y(i), UI_W, can[i] && bk_act_cur == i);
        row_label(i, i ? STR_ACT_RESTORE : STR_ACT_BACKUP, can[i] ? COL_BONE : COL_DIM);
    }
    footer(1, 1, NULL);
    if ((k & BTN_UP)   && can[0]) bk_act_cur = 0;
    if ((k & BTN_DOWN) && can[1]) bk_act_cur = 1;
    if (k & BTN_CROSS) return ST_BK_LIST;
    if ((k & BTN_CIRCLE) && can[bk_act_cur]) {
        bk_restore = bk_act_cur;
        if (bk_restore && bk_card[bk_game]) save_safety_name(bk_game, bk_safety, sizeof bk_safety);
        return ST_BK_CONFIRM;
    }
    return ST_BK_ACTION;
}

static state scr_bk_confirm(unsigned k) {
    char line[96];
    int over = bk_restore ? bk_card[bk_game] : bk_usb[bk_game];
    header(bk_restore ? STR_RSC_TITLE : STR_BKC_TITLE);
    body(over ? 52 + 112 : 98);
    float y = g_py;
    if (over) {
        vid_scrim(UI_L, y, UI_W, 40, COL_SELT);
        vid_rect(UI_L, y, 5, 40, COL_RED);
        vid_phrase(bk_restore ? STR_RSC_OVER : STR_BKC_OVER, lang, UI_L + 14, y + 11, COL_BONE);
        y += 52;
    }
    vid_phrase(bk_restore ? STR_RSC_BODY : STR_BKC_BODY, lang, UI_L, y + 2, COL_TEXT);
    menu_row(UI_L, y + 28, UI_W, 1);
    vid_phrase(SG_LABEL[bk_game], lang, UI_L + 18, y + 35, COL_GOLD);
    if (bk_restore && over) {
        vid_phrase(STR_RSC_SAFETY, lang, UI_L, y + 70, COL_DIM);
        vid_text(bk_safety, UI_L + 14, y + 90, COL_GOLD);
    } else if (!bk_restore && over) {
        vid_phrase(STR_BKC_KEEP, lang, UI_L, y + 70, COL_DIM);
        snprintf(line, sizeof line, "MHSAVES/%s.psu", save_dir(bk_game));
        vid_text(line, UI_L + 14, y + 90, COL_GOLD);
    } else {
        snprintf(line, sizeof line, bk_restore ? "MHSAVES/%s.psu  ->  /%s" : "/%s  ->  MHSAVES/%s.psu",
                 save_dir(bk_game), save_dir(bk_game));
        vid_text(line, UI_L + 14, y + 74, COL_GOLD);
    }
    footer(1, 1, NULL);
    if (k & BTN_CROSS)  return ST_BK_ACTION;
    if (k & BTN_CIRCLE) return ST_BK_RUN;
    return ST_BK_CONFIRM;
}

static state scr_bk_run(unsigned k) {
    (void)k;
    header(STR_BK_WORKING);
    body(24);
    vid_phrase(STR_BK_NOREMOVE, lang, UI_L, g_py + 3, COL_DIM);
    vid_end();
    frame_done = 1;
    bk_result = bk_restore ? save_restore(sel_port, bk_game) : save_backup(sel_port, bk_game);
    return bk_result == SV_OK ? ST_BK_DONE : ST_BK_FAIL;
}

static state scr_bk_done(unsigned k) {
    int patch_note = bk_restore && save_has_patch(bk_game);
    header(bk_restore ? STR_RSOK_TITLE : STR_BKOK_TITLE);
    body(bk_restore ? (patch_note ? 50 : 22) : 50);
    float y = g_py;
    vid_rect(UI_L, y, 4, 20, COL_GREEN);
    vid_phrase(bk_restore ? STR_RSOK_BODY : STR_BKOK_BODY, lang, UI_L + 14, y + 3, COL_BONE);
    if (!bk_restore) vid_text(save_detail, UI_L + 14, y + 30, COL_GOLD);
    if (patch_note) vid_phrase(STR_RSOK_PATCH, lang, UI_L, y + 34, COL_DIM);
    footer(1, 0, NULL);
    if (k & BTN_CIRCLE) return ST_BK_SCAN;
    return ST_BK_DONE;
}

static atlas_str bk_err_phrase(save_result r) {
    switch (r) {
        case SV_E_USB_READ:     return STR_E_USB_READ;
        case SV_E_USB_WRITE:    return STR_E_USB_WRITE;
        case SV_E_MC_GONE:      return STR_E_MC_GONE;
        case SV_E_MC_READ:      return STR_E_MC_READ;
        case SV_E_MC_SPACE:     return STR_E_MC_SPACE;
        case SV_E_BAD_BACKUP:   return STR_E_BAD_BACKUP;
        case SV_E_TOO_BIG:      return STR_E_TOO_BIG;
        case SV_E_MC_WRITE:     return STR_E_MC_WRITE;
        case SV_E_MC_WRITE_NEW: return STR_E_MC_WRITE2;
        case SV_E_MC_LOST:      return STR_E_MC_LOST;
        default:                return STR_FAIL_BODY;
    }
}

static state scr_bk_fail(unsigned k) {
    /* a backup never writes to the card */
    int untouched = !bk_restore || !(bk_result == SV_E_MC_WRITE ||
                                     bk_result == SV_E_MC_WRITE_NEW || bk_result == SV_E_MC_LOST);
    const char *detail = save_detail[0] ? save_detail : mc_last_error();
    int has_detail = detail[0] != 0;
    header(STR_BKF_TITLE);
    body(20 + (has_detail ? 28 : 0) + (untouched ? 28 : 0) + 28);
    float y = g_py;
    vid_rect(UI_L, y, 4, 20, COL_RED);
    vid_phrase(bk_err_phrase(bk_result), lang, UI_L + 14, y + 3, COL_BONE);
    y += 28;
    if (has_detail) { vid_text(detail, UI_L + 14, y, COL_GOLD); y += 28; }
    if (untouched)  { vid_phrase(STR_E_NOTHING, lang, UI_L, y + 3, COL_DIM); y += 28; }
    char code[24];
    snprintf(code, sizeof code, "[%d]", (int)bk_result);
    vid_text(code, UI_L, y, COL_DIM);
    footer(1, 0, NULL);
    if (k & BTN_CIRCLE) return ST_BK_SCAN;
    return ST_BK_FAIL;
}

int main(void) {
    load_modules();
    vid_init();
    pad_init();
    mc_init();
    nvm_read_ilink(&ilink);
    hexid(ilink.id, ilink_hex);

    state st = ST_LANG;
    for (;;) {
        unsigned k = pad_poll();
        vid_begin();
        state ns = st;
        switch (st) {
            case ST_LANG:     ns = scr_lang(k);     break;
            case ST_MODE:     ns = scr_mode(k);     break;
            case ST_NO_ILINK: ns = scr_no_ilink(k); break;
            case ST_MCSEL:    ns = scr_mcsel(k);    break;
            case ST_NO_MC:    ns = scr_no_mc(k);    break;
            case ST_SCAN:     ns = scr_scan(k);     break;
            case ST_GAMESEL:  ns = scr_gamesel(k);  break;
            case ST_NO_SAVES: ns = scr_no_saves(k); break;
            case ST_CONFIRM:  ns = scr_confirm(k);  break;
            case ST_INSTALL:  ns = scr_install(k);  break;
            case ST_DONE:     ns = scr_done(k);     break;
            case ST_FAIL:     ns = scr_fail(k);     break;
            case ST_USB_WAIT:   ns = scr_usb_wait(k);   break;
            case ST_USB_NONE:   ns = scr_usb_none(k);   break;
            case ST_BK_SCAN:    ns = scr_bk_scan(k);    break;
            case ST_BK_LIST:    ns = scr_bk_list(k);    break;
            case ST_BK_ACTION:  ns = scr_bk_action(k);  break;
            case ST_BK_CONFIRM: ns = scr_bk_confirm(k); break;
            case ST_BK_RUN:     ns = scr_bk_run(k);     break;
            case ST_BK_DONE:    ns = scr_bk_done(k);    break;
            case ST_BK_FAIL:    ns = scr_bk_fail(k);    break;
        }
        if (!frame_done) vid_end();
        frame_done = 0;
        st = ns;
    }
    return 0;
}
