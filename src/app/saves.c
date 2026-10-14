#include "saves.h"
#include "psu.h"
#include "../ps2/mcard.h"
#include "../ps2/usb.h"
#include <stdio.h>
#include <string.h>

/* card directory names */
static const char *const DIRS[SG_COUNT] = {
    "BISLPM-66280MH2",   /* MH2: main save, play00-02.bin, patch.bin, icons */
    "BISLPM-65869MHG",   /* MHG: main save (embeds the patch), icons        */
    "BKSLKA-25219MHG",   /* MHG Korea: same layout as the JP MHG save       */
    "BISLPM-65495MH",    /* MH1 JP                                          */
    "BASLUS-20896MH",    /* MH1 NA                                          */
    "BESLES-52707MH",    /* MH1 EU                                          */
};

#define USB_DIR      USB_ROOT "MHSAVES"
#define SV_FILE_MAX  0x40000     /* largest single file */
#define SV_DATA_MAX  0x80000     /* all files of one save */
#define SV_PSU_MAX   (SV_DATA_MAX + (PSU_MAX_FILES + 3) * (PSU_ENTRY + PSU_CLUSTER))

static uint8_t card_data[SV_DATA_MAX] __attribute__((aligned(64)));
static uint8_t psu_buf[SV_PSU_MAX]    __attribute__((aligned(64)));
static uint8_t safety_buf[SV_PSU_MAX] __attribute__((aligned(64)));
static uint8_t usb_verify[SV_PSU_MAX] __attribute__((aligned(64)));
static uint8_t mc_verify[SV_FILE_MAX] __attribute__((aligned(64)));

char save_detail[96];
static char safety_shown[96];

const char *save_dir(save_game g) { return DIRS[g]; }
int save_has_patch(save_game g) { return g == SG_MH2 || g == SG_MHG; }

static void usb_path(char *out, int n, save_game g, const char *suffix) {
    snprintf(out, n, "%s/%s%s", USB_DIR, DIRS[g], suffix);
}

/* Never overwrite a pre-restore copy: after a failed restore the card may
 * hold a broken save, and a retry must not replace the good copy with it. */
static int safety_suffix(save_game g, char *suffix, int n) {
    for (int i = 1; i < 100; i++) {
        if (i == 1) snprintf(suffix, n, "_before_restore.psu");
        else        snprintf(suffix, n, "_before_restore_%d.psu", i);
        char p[96];
        usb_path(p, sizeof p, g, suffix);
        if (usb_size(p) < 0) return 0;
    }
    return -1;
}

void save_safety_name(save_game g, char *out, int outlen) {
    char suffix[40];
    if (safety_suffix(g, suffix, sizeof suffix) < 0) snprintf(suffix, sizeof suffix, "_before_restore_?.psu");
    snprintf(out, outlen, "MHSAVES/%s%s", DIRS[g], suffix);
}

int save_kb_needed(const uint32_t *sizes, int nfiles) {
    int kb = (nfiles + 2 + 1) / 2;                 /* directory entries, 2 per KB */
    for (int i = 0; i < nfiles; i++) kb += (sizes[i] + 1023) / 1024;
    return kb;
}

static int psu_kb(const psu_save *s) {
    uint32_t sizes[PSU_MAX_FILES];
    for (int i = 0; i < s->nfiles; i++) sizes[i] = s->files[i].size;
    return save_kb_needed(sizes, s->nfiles);
}

/* removed, or present but not writable */
static save_result usb_fail(void) { return usb_ready() ? SV_E_USB_WRITE : SV_E_USB_READ; }

/* ---- scan --------------------------------------------------------------- */

int save_scan(int port, int on_card[SG_COUNT], int on_usb[SG_COUNT]) {
    /* also re-arms the card swap check */
    if (mc_status(port, NULL) == MC_ST_NONE) return -1;
    int usb = usb_ready();
    for (int g = 0; g < SG_COUNT; g++) {
        on_card[g] = mc_dir_info(port, DIRS[g], NULL) == 1;
        char p[96];
        usb_path(p, sizeof p, g, ".psu");
        on_usb[g] = usb && usb_size(p) > 0;
    }
    return 0;
}

/* ---- card side ---------------------------------------------------------- */

#define SV_EMPTY (-1)   /* directory exists but is empty */

/* Reads every file of the save into card_data. Returns SV_OK, SV_EMPTY or an
 * error. */
static int read_card_save(int port, save_game g, psu_save *s) {
    static mc_entry ents[PSU_MAX_FILES];
    mc_entry dir;
    memset(s, 0, sizeof *s);

    int e = mc_dir_info(port, DIRS[g], &dir);
    if (e < 0) return mc_status(port, NULL) != MC_ST_SAME ? SV_E_MC_GONE : SV_E_MC_READ;
    if (e == 0) return SV_E_MC_READ;

    int n = mc_list(port, DIRS[g], ents, PSU_MAX_FILES);
    if (n == MC_LIST_SUBDIR || n == MC_LIST_FULL) return SV_E_TOO_BIG;
    if (n < 0) return SV_E_MC_READ;
    if (n == 0) return SV_EMPTY;

    strcpy(s->name, DIRS[g]);
    s->mode = dir.attr;
    memcpy(s->created, dir.created, 8);
    memcpy(s->modified, dir.modified, 8);

    uint32_t off = 0;
    for (int i = 0; i < n; i++) {
        const mc_entry *m = &ents[i];
        if (!psu_name_ok(m->name) || m->size > SV_FILE_MAX) return SV_E_TOO_BIG;
        if (off + m->size > SV_DATA_MAX) return SV_E_TOO_BIG;
        char path[80];
        snprintf(path, sizeof path, "/%s/%.31s", DIRS[g], m->name);
        uint8_t *dst = card_data + off;
        if (mc_read_exact(port, path, dst, m->size) != (int)m->size) return SV_E_MC_READ;
        /* two reads must agree, out of up to four */
        if (mc_read_exact(port, path, mc_verify, m->size) != (int)m->size) return SV_E_MC_READ;
        if (memcmp(dst, mc_verify, m->size)) {
            if (mc_read_exact(port, path, mc_verify, m->size) != (int)m->size) return SV_E_MC_READ;
            if (memcmp(dst, mc_verify, m->size)) {
                if (mc_read_exact(port, path, dst, m->size) != (int)m->size) return SV_E_MC_READ;
                if (memcmp(dst, mc_verify, m->size)) { mc_note_error("unstable", path, 0); return SV_E_MC_READ; }
            }
        }

        psu_file *f = &s->files[i];
        strcpy(f->name, m->name);
        f->size = m->size;
        f->mode = m->attr;
        memcpy(f->created, m->created, 8);
        memcpy(f->modified, m->modified, 8);
        f->data = dst;
        off = (off + m->size + 63) & ~63u;
    }
    s->nfiles = n;
    return SV_OK;
}

/* Deletes the files, keeps the directory. */
static int clear_dir(int port, save_game g) {
    static mc_entry ents[PSU_MAX_FILES];
    int n = mc_list(port, DIRS[g], ents, PSU_MAX_FILES);
    if (n < 0) return -1;
    int ok = 1;
    for (int i = 0; i < n; i++) {
        char path[80];
        snprintf(path, sizeof path, "/%s/%.31s", DIRS[g], ents[i].name);
        if (mc_remove(port, path) < 0) ok = 0;
    }
    return ok ? 0 : -1;
}

/* 1 if the card holds exactly these bytes; reads twice before saying no. */
static int card_matches(int port, const char *path, const uint8_t *data, uint32_t size) {
    for (int r = 0; r < 2; r++)
        if (mc_read_exact(port, path, mc_verify, size) == (int)size && !memcmp(mc_verify, data, size))
            return 1;
    return 0;
}

/* Writes each file and verifies it; a file that fails is written once more. */
static int put_files(int port, save_game g, const psu_save *s) {
    for (int i = 0; i < s->nfiles; i++) {
        const psu_file *f = &s->files[i];
        char path[80];
        snprintf(path, sizeof path, "/%s/%.31s", DIRS[g], f->name);
        int ok = 0;
        for (int w = 0; w < 2 && !ok; w++) {
            if (w) mc_remove(port, path);
            ok = mc_write_new(port, path, f->data, f->size) == (int)f->size &&
                 card_matches(port, path, f->data, f->size);
        }
        if (!ok) { if (!mc_last_error()[0]) mc_note_error("verify", path, 0); return -1; }
        mc_set_dates(port, path, f->created, f->modified);   /* best effort */
    }
    return 0;
}

/* The directory must hold exactly these files and sizes. */
static int check_dir(int port, save_game g, const psu_save *s) {
    static mc_entry ents[PSU_MAX_FILES];
    int n = mc_list(port, DIRS[g], ents, PSU_MAX_FILES);
    if (n != s->nfiles) return -1;
    for (int i = 0; i < s->nfiles; i++) {
        int found = 0;
        for (int j = 0; j < n; j++)
            if (!strcmp(ents[j].name, s->files[i].name) && ents[j].size == s->files[i].size) found = 1;
        if (!found) return -1;
    }
    return 0;
}

/* ---- USB side ----------------------------------------------------------- */


static save_result usb_put_verified(const char *path, const uint8_t *buf, uint32_t len) {
    if (usb_write(path, buf, len) != (int)len) return usb_fail();
    int got = usb_read(path, usb_verify, sizeof usb_verify);
    if (got != (int)len || memcmp(usb_verify, buf, len)) return usb_fail();
    return SV_OK;
}

/* ---- backup: card -> USB ------------------------------------------------ */

save_result save_backup(int port, save_game g) {
    save_detail[0] = 0;
    mc_clear_error();
    if (mc_status(port, NULL) != MC_ST_SAME) return SV_E_MC_GONE;

    psu_save s;
    int r = read_card_save(port, g, &s);
    if (r == SV_EMPTY) return SV_E_MC_READ;
    if (r != SV_OK) return r;

    uint32_t len = psu_build(&s, psu_buf, sizeof psu_buf);
    if (!len) return SV_E_TOO_BIG;
    psu_save chk;
    if (psu_parse(psu_buf, len, &chk) != PSU_OK || chk.nfiles != s.nfiles) return SV_E_TOO_BIG;

    if (!usb_ready()) return SV_E_USB_READ;
    if (usb_mkdir(USB_DIR) < 0) return usb_fail();

    char final[80], tmp[80], bak[80];
    usb_path(final, sizeof final, g, ".psu");
    usb_path(tmp, sizeof tmp, g, ".psu.tmp");
    usb_path(bak, sizeof bak, g, ".psu.bak");

    r = usb_put_verified(tmp, psu_buf, len);
    if (r != SV_OK) { usb_remove(tmp); return r; }

    /* old backup becomes .bak, then the new one moves in */
    if (usb_size(final) >= 0) {
        usb_remove(bak);
        usb_rename(final, bak);
    }
    if (usb_rename(tmp, final) < 0) {
        /* no rename: write the final name, keep .tmp until it verifies */
        r = usb_put_verified(final, psu_buf, len);
        if (r != SV_OK) return r;
        usb_remove(tmp);
    }
    if (usb_size(final) != (int)len) return usb_fail();

    snprintf(save_detail, sizeof save_detail, "MHSAVES/%s.psu", DIRS[g]);
    return SV_OK;
}

/* ---- restore: USB -> card ----------------------------------------------- */

static int has_file(const psu_save *s, const char *name) {
    for (int i = 0; i < s->nfiles; i++)
        if (!strcmp(s->files[i].name, name)) return 1;
    return 0;
}

save_result save_restore(int port, save_game g) {
    save_detail[0] = 0;
    mc_clear_error();
    if (mc_status(port, NULL) != MC_ST_SAME) return SV_E_MC_GONE;
    if (!usb_ready()) return SV_E_USB_READ;

    /* 1. read the backup (twice, compared) and validate it completely */
    char final[80];
    usb_path(final, sizeof final, g, ".psu");
    int len = usb_read(final, psu_buf, sizeof psu_buf);
    if (len == USB_TOO_BIG) return SV_E_TOO_BIG;
    if (len < 0) return SV_E_USB_READ;
    if (usb_read(final, usb_verify, sizeof usb_verify) != len || memcmp(psu_buf, usb_verify, len))
        return SV_E_USB_READ;

    psu_save ns;
    if (psu_parse(psu_buf, len, &ns) != PSU_OK) return SV_E_BAD_BACKUP;
    if (strcmp(ns.name, DIRS[g])) return SV_E_BAD_BACKUP;           /* another game's save */
    if (!has_file(&ns, "icon.sys") || !has_file(&ns, DIRS[g]))       /* not a complete save */
        return SV_E_BAD_BACKUP;
    uint32_t total = 0;
    for (int i = 0; i < ns.nfiles; i++) {
        if (ns.files[i].size > SV_FILE_MAX) return SV_E_TOO_BIG;
        total += (ns.files[i].size + 63) & ~63u;
    }
    if (total > SV_DATA_MAX) return SV_E_TOO_BIG;

    /* 2. what is on the card now */
    psu_save os;
    int exists = mc_dir_info(port, DIRS[g], NULL);
    if (exists < 0) return mc_status(port, NULL) != MC_ST_SAME ? SV_E_MC_GONE : SV_E_MC_READ;
    int have_old = 0;
    if (exists) {
        int r = read_card_save(port, g, &os);
        if (r == SV_OK) have_old = 1;
        else if (r != SV_EMPTY) return r;
    }

    /* 3. free space: the old files are deleted before the new ones are written */
    int free_kb = 0;
    if (mc_status(port, &free_kb) != MC_ST_SAME) return SV_E_MC_GONE;
    int need = psu_kb(&ns) - (have_old ? psu_kb(&os) : 0) + 2;   /* +2 KB margin */
    if (need > free_kb) {
        snprintf(save_detail, sizeof save_detail, "need %d KB, free %d KB", need, free_kb);
        return SV_E_MC_SPACE;
    }

    /* 4. safety copy of the current save on the drive, verified */
    if (have_old) {
        uint32_t olen = psu_build(&os, safety_buf, sizeof safety_buf);
        if (!olen) return SV_E_TOO_BIG;
        if (usb_mkdir(USB_DIR) < 0) return usb_fail();
        char suffix[40], safety[96];
        if (safety_suffix(g, suffix, sizeof suffix) < 0) return SV_E_USB_WRITE;
        usb_path(safety, sizeof safety, g, suffix);
        int r = usb_put_verified(safety, safety_buf, olen);
        if (r != SV_OK) { usb_remove(safety); return r; }
        snprintf(safety_shown, sizeof safety_shown, "MHSAVES/%s%s", DIRS[g], suffix);
    }

    /* 5. last check before the first card write */
    if (mc_status(port, NULL) != MC_ST_SAME) return SV_E_MC_GONE;

    char dirpath[48];
    snprintf(dirpath, sizeof dirpath, "/%s", DIRS[g]);
    if (!exists) {
        if (mc_mkdir(port, dirpath) < 0) { mc_remove(port, dirpath); return SV_E_MC_WRITE_NEW; }
    } else if (clear_dir(port, g) < 0) {
        goto rollback;
    }
    if (put_files(port, g, &ns) < 0 || check_dir(port, g, &ns) < 0) goto rollback;
    mc_set_dates(port, dirpath, ns.created, ns.modified);
    return SV_OK;

rollback:
    if (!have_old) {
        clear_dir(port, g);
        if (!exists) mc_remove(port, dirpath);
        return SV_E_MC_WRITE_NEW;
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        clear_dir(port, g);
        if (put_files(port, g, &os) == 0 && check_dir(port, g, &os) == 0) return SV_E_MC_WRITE;
    }
    snprintf(save_detail, sizeof save_detail, "%s", safety_shown);
    return SV_E_MC_LOST;
}
