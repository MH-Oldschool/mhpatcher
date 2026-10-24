/* Tests for backup, restore and patch install against a simulated memory card
 * (in RAM, with injected faults) and a host directory as the USB drive.
 *
 *   ./saves_test <saves-dir> <usb-dir> [psu-dir]
 *
 * saves-dir holds one directory per game save copied off a memory card
 * (BISLPM-66280MH2/, BISLPM-65869MHG/, ...). The optional psu-dir holds .psu
 * files made by another tool, which must restore byte for byte. */
#include "../src/app/saves.h"
#include "../src/app/psu.h"
#include "../src/ps2/mcard.h"
#include "../src/ps2/usb.h"
#include "fake_io.h"
#include "../src/app/install.h"
#include "../src/dnas/savecodec.h"
#include "../src/dnas/outer_key.h"
#include "../src/dnas/baked_data.h"
#include "../src/crypto/rc6.h"
#include "../src/dnas/patcher.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- tests -------------------------------------------------------------- */

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *root;

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s saves-dir usb-dir [psu-dir]\n", argv[0]); return 2; }
    root = argv[1];
    snprintf(usb_base, sizeof usb_base, "%s", argv[2]);
    mkdir(usb_base, 0777);

    /* 1. backup + restore round trip of every supported game, real data */
    for (int g = 0; g < SG_COUNT; g++) {
        const char *d = save_dir(g);
        card_reset(); usb_reset();
        card_load(root, d, 0x11);
        uint32_t sig = card_sig(d);
        int oc[SG_COUNT], ou[SG_COUNT];
        CHECK(save_scan(0, oc, ou) == 0 && oc[g] && !ou[g], "%s scan before", d);
        CHECK(save_backup(0, g) == SV_OK, "%s backup", d);
        CHECK(save_scan(0, oc, ou) == 0 && ou[g], "%s scan after", d);
        char nm[64]; snprintf(nm, sizeof nm, "%s.psu", d);
        CHECK(usb_exists(nm), "%s psu exists", d);
        CHECK(card_sig(d) == sig, "%s card untouched by backup", d);

        /* restore onto an empty card */
        card_reset(); usb_present = 1;
        CHECK(save_scan(0, oc, ou) == 0 && !oc[g] && ou[g], "%s scan empty card", d);
        CHECK(save_restore(0, g) == SV_OK, "%s restore to empty card", d);
        CHECK(card_sig(d) == sig, "%s restored bytes identical", d);
        CHECK(find(d, NULL) && find(d, NULL)->created[0] == 0x11, "%s dir dates restored", d);
        snprintf(nm, sizeof nm, "%s_before_restore.psu", d);
        CHECK(!usb_exists(nm), "%s no safety copy when card had no save", d);
    }
    printf("round trips done\n");

    const save_game G = SG_MH2;
    const char *D = save_dir(G);
    int oc[SG_COUNT], ou[SG_COUNT];
    char psu[64], bak[64], tmp[64], safety[64];
    snprintf(psu, sizeof psu, "%s.psu", D);
    snprintf(bak, sizeof bak, "%s.psu.bak", D);
    snprintf(tmp, sizeof tmp, "%s.psu.tmp", D);
    snprintf(safety, sizeof safety, "%s_before_restore.psu", D);

    /* 2. second backup keeps the previous one as .bak */
    card_reset(); usb_reset(); card_load(root, D, 1);
    save_scan(0, oc, ou);
    CHECK(save_backup(0, G) == SV_OK, "first backup");
    uint32_t first = usb_file_crc(psu);
    card[1].data[0] ^= 1;                                   /* the save changes */
    CHECK(save_backup(0, G) == SV_OK, "second backup");
    CHECK(usb_file_crc(bak) == first, "previous backup kept as .bak");
    CHECK(usb_file_crc(psu) != first, "new backup in place");
    CHECK(!usb_exists(tmp), "no .tmp left");

    /* 3. no rename support on the drive: still a verified final file */
    usb_no_rename = 1;
    card[1].data[0] ^= 2;
    CHECK(save_backup(0, G) == SV_OK, "backup without rename");
    CHECK(!usb_exists(tmp), "no .tmp left (no rename)");
    usb_no_rename = 0;

    /* 4. drive full: error, the existing backup survives, no .tmp */
    uint32_t before = usb_file_crc(psu);
    usb_full_after = usb_written + 1000;
    CHECK(save_backup(0, G) == SV_E_USB_WRITE, "drive full -> USB_WRITE");
    CHECK(usb_file_crc(psu) == before, "existing backup intact after full drive");
    CHECK(!usb_exists(tmp), "partial .tmp removed");
    usb_full_after = -1;

    /* 5. drive removed */
    usb_present = 0;
    CHECK(save_backup(0, G) == SV_E_USB_READ, "backup, drive gone -> USB_READ");
    CHECK(save_restore(0, G) == SV_E_USB_READ, "restore, drive gone -> USB_READ");
    usb_present = 1;

    /* 6. card swapped / removed since the scan */
    card_swapped = 1;
    CHECK(save_backup(0, G) == SV_E_MC_GONE, "backup after card swap -> MC_GONE");
    card_present = 0;
    CHECK(save_restore(0, G) == SV_E_MC_GONE, "restore without card -> MC_GONE");
    card_present = 1;

    /* 7. one flaky card read is out-voted (two agreeing reads win) and the
     * backup holds the true bytes; a card that never reads the same twice
     * is refused */
    {
        uint32_t want_sig = card_sig(D);
        corrupt_read_at = read_calls + 1; corrupt_read_count = 1;
        CHECK(save_backup(0, G) == SV_OK, "one flaky read -> still OK");
        corrupt_read_at = -1;
        card_reset(); usb_present = 1;
        save_scan(0, oc, ou);
        CHECK(save_restore(0, G) == SV_OK && card_sig(D) == want_sig, "flaky-read backup restores the true bytes");
        corrupt_read_at = read_calls + 1; corrupt_read_count = 1000;
        CHECK(save_backup(0, G) == SV_E_MC_READ, "persistently unstable reads -> MC_READ");
        CHECK(strstr(mc_last_error(), "unstable") != NULL, "card error names the cause: %s", mc_last_error());
        corrupt_read_at = -1; corrupt_read_count = 1;
    }

    /* 8. restore over an existing, different save: safety copy + replace */
    card_reset(); usb_reset(); card_load(root, D, 1);
    save_scan(0, oc, ou);
    CHECK(save_backup(0, G) == SV_OK, "backup A");
    uint32_t sigA = card_sig(D);
    card[1].data[5] ^= 0xff; card[3].data[7] ^= 0xff;     /* card now holds B */
    uint32_t sigB = card_sig(D);
    save_scan(0, oc, ou);
    CHECK(save_restore(0, G) == SV_OK, "restore A over B");
    CHECK(card_sig(D) == sigA, "card holds A");
    CHECK(usb_exists(safety), "safety copy of B written");

    /* the safety copy really is B: restore it via the normal path */
    char cmd[2048];
    snprintf(cmd, sizeof cmd, "cp '%s/MHSAVES/%s' '%s/MHSAVES/%s'", usb_base, safety, usb_base, psu);
    if (system(cmd)) {}
    CHECK(save_restore(0, G) == SV_OK, "restore safety copy");
    CHECK(card_sig(D) == sigB, "safety copy restores B exactly");

    /* 9. write fails mid-restore: original written back */
    card_reset(); usb_reset(); card_load(root, D, 1);
    save_scan(0, oc, ou);
    save_backup(0, G);
    card[2].data[9] ^= 0x55;                              /* card holds B */
    sigB = card_sig(D);
    /* a single failed write is retried and the restore succeeds */
    {
        uint32_t keep = card_sig(D);
        fail_write_from = write_calls + 2; fail_write_count = 1;
        CHECK(save_restore(0, G) == SV_OK, "one transient write failure -> retried, OK");
        CHECK(card_sig(D) != keep, "restore applied after retry");
        fail_write_from = -1;
        /* put B back for the rollback test */
        snprintf(cmd, sizeof cmd, "rm -f '%s/MHSAVES/%s'_before_restore*.psu", usb_base, D);
        if (system(cmd)) {}
        card_reset(); card_load(root, D, 1); card[2].data[9] ^= 0x55;
        save_scan(0, oc, ou);
    }
    /* the same file failing on both attempts: original written back */
    fail_write_from = write_calls + 2; fail_write_count = 2;
    CHECK(save_restore(0, G) == SV_E_MC_WRITE, "file fails twice -> MC_WRITE");
    CHECK(card_sig(D) == sigB, "original save written back after failure");
    CHECK(mc_last_error()[0] != 0, "card error recorded: %s", mc_last_error());
    fail_write_from = -1;

    /* 10. card keeps failing, rollback impossible: MC_LOST, original on USB */
    snprintf(cmd, sizeof cmd, "rm -f '%s/MHSAVES/%s'_before_restore*.psu", usb_base, D);
    if (system(cmd)) {}
    fail_write_from = write_calls + 1; fail_write_count = -1;
    CHECK(save_restore(0, G) == SV_E_MC_LOST, "persistent write failure -> MC_LOST");
    CHECK(strstr(save_detail, "_before_restore.psu") != NULL, "detail names the safety copy");
    fail_write_from = -1;
    uint32_t safetyB = usb_file_crc(safety);
    /* retrying the restore must not replace the good copy of B with the
     * half-written card contents */
    CHECK(save_restore(0, G) == SV_OK, "retry restore after MC_LOST");
    CHECK(usb_file_crc(safety) == safetyB, "first safety copy (B) kept on retry");
    { char s2[64]; snprintf(s2, sizeof s2, "%s_before_restore_2.psu", D);
      CHECK(usb_exists(s2), "retry used a new safety name"); }
    snprintf(cmd, sizeof cmd, "cp '%s/MHSAVES/%s' '%s/MHSAVES/%s'", usb_base, safety, usb_base, psu);
    if (system(cmd)) {}
    CHECK(save_restore(0, G) == SV_OK && card_sig(D) == sigB, "save recovered from the safety copy");

    /* 11. failure when the card had no save: partial copy removed */
    card_reset(); usb_present = 1;
    fail_write_from = 1; fail_write_count = -1;
    CHECK(save_restore(0, G) == SV_E_MC_WRITE_NEW, "fresh restore failure -> MC_WRITE_NEW");
    CHECK(!find(D, NULL) && card_sig(D) == 0, "no partial directory left");
    fail_write_from = -1;

    /* 12. not enough space: nothing touched */
    card_reset(); card_load(root, "BISLPM-65495MH", 3);
    card_capacity_kb = used_kb() + 50;
    uint32_t other = card_sig("BISLPM-65495MH");
    save_scan(0, oc, ou);
    CHECK(save_restore(0, G) == SV_E_MC_SPACE, "small card -> MC_SPACE");
    CHECK(!find(D, NULL) && card_sig("BISLPM-65495MH") == other, "card untouched on MC_SPACE");
    CHECK(strstr(save_detail, "KB") != NULL, "space detail: %s", save_detail);

    /* 13. damaged / foreign backups are refused before touching the card */
    static uint8_t b[0x200000];
    char p[768]; snprintf(p, sizeof p, "%s/MHSAVES/%s", usb_base, psu);
    card_reset(); card_load(root, D, 1);
    uint32_t sigC = card_sig(D);
    save_scan(0, oc, ou);
    FILE *f = fopen(p, "rb"); size_t n = fread(b, 1, sizeof b, f); fclose(f);
    b[n / 2] ^= 1;                                          /* bit flip */
    f = fopen(p, "wb"); fwrite(b, 1, n, f); fclose(f);
    CHECK(save_restore(0, G) == SV_E_BAD_BACKUP, "bit-flipped backup -> BAD_BACKUP");
    b[n / 2] ^= 1;
    f = fopen(p, "wb"); fwrite(b, 1, n - 3000, f); fclose(f);  /* truncated */
    CHECK(save_restore(0, G) == SV_E_BAD_BACKUP, "truncated backup -> BAD_BACKUP");
    f = fopen(p, "wb"); fwrite(b, 1, 700, f); fclose(f);
    CHECK(save_restore(0, G) == SV_E_BAD_BACKUP, "tiny file -> BAD_BACKUP");
    CHECK(card_sig(D) == sigC, "card untouched by refused backups");

    /* another game's backup under this game's name */
    card_reset(); usb_reset(); card_load(root, "BASLUS-20896MH", 1);
    save_scan(0, oc, ou);
    save_backup(0, SG_MH1U);
    snprintf(cmd, sizeof cmd, "cp '%s/MHSAVES/BASLUS-20896MH.psu' '%s/MHSAVES/BESLES-52707MH.psu'", usb_base, usb_base);
    if (system(cmd)) {}
    CHECK(save_restore(0, SG_MH1E) == SV_E_BAD_BACKUP, "foreign game's backup -> BAD_BACKUP");
    CHECK(!find("BESLES-52707MH", NULL), "no EU dir created from NA backup");

    /* 14. card save with a subdirectory is not touched */
    card_reset(); card_load(root, D, 1);
    { node *x = alloc_node(); strcpy(x->dir, D); strcpy(x->name, "sub"); x->is_dir = 1; }
    save_scan(0, oc, ou);
    CHECK(save_backup(0, G) == SV_E_TOO_BIG, "subdirectory -> TOO_BIG");

    /* 15. untagged .psu files made by another tool (mymcplus) restore too */
    if (argc > 3) {
        for (int g = 0; g < SG_COUNT; g++) {
            const char *d = save_dir(g);
            card_reset(); card_load(root, d, 1);
            uint32_t want = card_sig(d);
            card_reset(); usb_reset(); usb_mkdir(USB_ROOT "MHSAVES");
            snprintf(cmd, sizeof cmd, "cp '%s/%s.psu' '%s/MHSAVES/'", argv[3], d, usb_base);
            CHECK(system(cmd) == 0, "copy foreign psu");
            save_scan(0, oc, ou);
            CHECK(ou[g] && save_restore(0, g) == SV_OK, "%s: restore foreign .psu", d);
            CHECK(card_sig(d) == want, "%s: foreign .psu restores identical bytes", d);
        }
    }

    /* 16. patch install (MH2 / MHG / MH1E) with transient and persistent card faults */
    {
        static const uint8_t id[8] = { 0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0x00, 0x42 };
        static uint8_t ref[0x20000];
        const char *P2 = "/" MH2_SAVE_DIR "/" MH2_SAVE_FILE;
        node *pb;

        card_reset(); card_load(root, save_dir(SG_MH2), 1);
        CHECK(install_game(GAME_MH2, 0, id) == INS_OK, "MH2 install, clean card");
        pb = find(MH2_SAVE_DIR, MH2_SAVE_FILE);
        CHECK(pb && pb->size == MH2_PATCHBIN_SIZE, "patch.bin written");
        memcpy(ref, pb->data, MH2_PATCHBIN_SIZE);              /* the correct result */

        card_reset(); card_load(root, save_dir(SG_MH2), 1);
        fail_write_from = write_calls; fail_write_count = 1;
        CHECK(install_game(GAME_MH2, 0, id) == INS_OK, "MH2: one failed write -> retried, OK");
        pb = find(MH2_SAVE_DIR, MH2_SAVE_FILE);
        CHECK(pb && !memcmp(pb->data, ref, MH2_PATCHBIN_SIZE), "MH2: retried patch.bin correct");
        fail_write_from = -1;

        card_reset(); card_load(root, save_dir(SG_MH2), 1);
        corrupt_read_at = read_calls + 1; corrupt_read_count = 1;     /* first verify read */
        CHECK(install_game(GAME_MH2, 0, id) == INS_OK, "MH2: one bad verify read -> re-read, OK");
        corrupt_read_at = -1;

        card_reset(); card_load(root, save_dir(SG_MH2), 1);
        pb = find(MH2_SAVE_DIR, MH2_SAVE_FILE);
        uint32_t orig = crc32(0, pb->data, pb->size);
        fail_write_from = write_calls; fail_write_count = 2;          /* both attempts fail */
        CHECK(install_game(GAME_MH2, 0, id) == INS_ERR_WRITE, "MH2: write fails twice -> INS_ERR_WRITE");
        pb = find(MH2_SAVE_DIR, MH2_SAVE_FILE);
        CHECK(pb && crc32(0, pb->data, pb->size) == orig, "MH2: original patch.bin put back");
        CHECK(mc_last_error()[0] != 0, "MH2: card error recorded");
        fail_write_from = -1;

        card_reset(); card_load(root, save_dir(SG_MH2), 1);
        fail_write_from = write_calls; fail_write_count = -1;         /* card never writes */
        CHECK(install_game(GAME_MH2, 0, id) == INS_ERR_VERIFY_LOST, "MH2: dead card -> VERIFY_LOST");
        fail_write_from = -1;

        card_reset(); card_load(root, save_dir(SG_MH2), 1);
        mc_remove(0, P2);                                             /* first-ever patch */
        fail_write_from = write_calls; fail_write_count = 2;
        CHECK(install_game(GAME_MH2, 0, id) == INS_ERR_WRITE, "MH2 first patch: fails twice");
        CHECK(!find(MH2_SAVE_DIR, MH2_SAVE_FILE), "MH2 first patch: half-written patch.bin removed");
        fail_write_from = -1;

        card_reset(); card_load(root, save_dir(SG_MHG), 1);
        node *mg = find(MHG_SAVE_DIR, MHG_SAVE_FILE);
        uint32_t morig = crc32(0, mg->data, mg->size);
        fail_write_from = write_calls; fail_write_count = 1;
        CHECK(install_game(GAME_MHG, 0, id) == INS_OK, "MHG: one failed write -> retried, OK");
        fail_write_from = -1;
        card_reset(); card_load(root, save_dir(SG_MHG), 1);
        fail_write_from = write_calls; fail_write_count = 2;
        CHECK(install_game(GAME_MHG, 0, id) == INS_ERR_WRITE, "MHG: write fails twice -> INS_ERR_WRITE");
        mg = find(MHG_SAVE_DIR, MHG_SAVE_FILE);
        CHECK(mg && crc32(0, mg->data, mg->size) == morig, "MHG: original save put back");
        fail_write_from = -1;

        /* MH1E: the official blob goes into the save, sealed to `id`; nothing else changes */
        static uint8_t before[MH1E_SAVE_SIZE], after[MH1E_SAVE_SIZE];
        card_reset(); card_load(root, save_dir(SG_MH1E), 1);
        node *me = find(MH1E_SAVE_DIR, MH1E_SAVE_FILE);
        CHECK(me && me->size == MH1E_SAVE_SIZE, "MH1E: save present");
        memcpy(before, me->data, MH1E_SAVE_SIZE);
        uint32_t eorig = crc32(0, me->data, me->size);
        CHECK(mh_save_decode(before, MH1E_SAVE_WORDS), "MH1E: original save decodes");
        CHECK(install_game(GAME_MH1E, 0, id) == INS_OK, "MH1E install");
        me = find(MH1E_SAVE_DIR, MH1E_SAVE_FILE);
        memcpy(after, me->data, MH1E_SAVE_SIZE);
        CHECK(mh_save_decode(after, MH1E_SAVE_WORDS), "MH1E: written save decodes, checksum OK");
        CHECK(!memcmp(after + 0x208, mh1e_blob, 0x180) &&
              !memcmp(after + 0x208 + 0x200, mh1e_blob + 0x200, MH1E_BLOB_LEN - 0x200),
              "MH1E: official blob in place outside the session header");
        CHECK(!memcmp(after + 0x10208, mh1e_meta, 0x40), "MH1E: patch header (0509261742, 1001)");
        uint8_t okey[32], hdr[128];
        dnas_outer_key(id, okey);
        rc6_cbc_decrypt(okey, okey + 16, after + 0x208 + 0x180, hdr, 128);
        CHECK(!memcmp(hdr, mh1e_hdr_plain, 128), "MH1E: session header sealed to this console");
        memcpy(after + 0x208, before + 0x208, MH1E_BLOB_LEN);
        memcpy(after + 0x10208, before + 0x10208, 0x40);
        CHECK(!memcmp(after + 8, before + 8, MH1E_SAVE_SIZE - 8), "MH1E: rest of the save untouched");

        card_reset(); card_load(root, save_dir(SG_MH1E), 1);
        fail_write_from = write_calls; fail_write_count = 2;
        CHECK(install_game(GAME_MH1E, 0, id) == INS_ERR_WRITE, "MH1E: write fails twice -> INS_ERR_WRITE");
        me = find(MH1E_SAVE_DIR, MH1E_SAVE_FILE);
        CHECK(me && crc32(0, me->data, me->size) == eorig, "MH1E: original save put back");
        fail_write_from = -1;

        card_reset(); card_load(root, save_dir(SG_MH1E), 1);
        me = find(MH1E_SAVE_DIR, MH1E_SAVE_FILE);
        me->data[100] ^= 1;                                           /* checksum no longer matches */
        CHECK(install_game(GAME_MH1E, 0, id) == INS_ERR_NO_SAVE, "MH1E: damaged save -> INS_ERR_NO_SAVE");
    }

    /* 17. patch removal: back to the state the game gives a new save */
    {
        static const uint8_t id[8] = { 0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0x00, 0x42 };
        static uint8_t orig[0x14000], got[0x14000];
        static const struct { mh_game game; save_game sg; const char *dir, *file, *name; int size; size_t words; } G[] = {
            { GAME_MHG,  SG_MHG,  MHG_SAVE_DIR,  MHG_SAVE_FILE,  "0411210839", MHG_SAVE_SIZE,  MHG_SAVE_WORDS },
            { GAME_MH1E, SG_MH1E, MH1E_SAVE_DIR, MH1E_SAVE_FILE, "0502180939", MH1E_SAVE_SIZE, MH1E_SAVE_WORDS },
        };
        for (int g = 0; g < 2; g++) {
            const char *n = G[g].dir;
            card_reset(); card_load(root, save_dir(G[g].sg), 1);
            node *f = find(G[g].dir, G[g].file);
            memcpy(orig, f->data, G[g].size);
            CHECK(mh_save_decode(orig, G[g].words), "%s: save decodes", n);
            CHECK(install_game(G[g].game, 0, id) == INS_OK, "%s: install before removal", n);
            CHECK(remove_game(G[g].game, 0) == INS_OK, "%s: remove", n);
            f = find(G[g].dir, G[g].file);
            CHECK(f && f->size == (uint32_t)G[g].size, "%s: save size kept", n);
            memcpy(got, f->data, G[g].size);
            CHECK(mh_save_decode(got, G[g].words), "%s: removed save decodes, checksum OK", n);
            int blank = 1;
            for (int i = 0x208; i < 0x10208; i++) blank &= got[i] == 0;
            CHECK(blank, "%s: patch area cleared", n);
            uint8_t fresh[0x40] = { 0 };
            memcpy(fresh, G[g].name, 10); memcpy(fresh + 10, "1000", 4);
            CHECK(!memcmp(got + 0x10208, fresh, 0x40), "%s: header = build stamp, 1000, length 0", n);
            CHECK(!memcmp(got + 8, orig + 8, 0x200) &&
                  !memcmp(got + 0x10248, orig + 0x10248, G[g].size - 0x10248), "%s: rest of the save untouched", n);

            int writes = write_calls;
            CHECK(remove_game(G[g].game, 0) == INS_OK && write_calls == writes, "%s: removing again writes nothing", n);

            card_reset(); card_load(root, save_dir(G[g].sg), 1);
            CHECK(install_game(G[g].game, 0, id) == INS_OK, "%s: install", n);
            f = find(G[g].dir, G[g].file);
            uint32_t patched = crc32(0, f->data, f->size);
            fail_write_from = write_calls; fail_write_count = 2;
            CHECK(remove_game(G[g].game, 0) == INS_ERR_WRITE, "%s: removal write fails twice -> INS_ERR_WRITE", n);
            f = find(G[g].dir, G[g].file);
            CHECK(f && crc32(0, f->data, f->size) == patched, "%s: patched save put back", n);
            fail_write_from = -1;

            card_reset(); card_load(root, save_dir(G[g].sg), 1);
            f = find(G[g].dir, G[g].file);
            f->data[100] ^= 1;
            writes = write_calls;
            CHECK(remove_game(G[g].game, 0) == INS_ERR_NO_SAVE && write_calls == writes,
                  "%s: damaged save -> INS_ERR_NO_SAVE, nothing written", n);
        }
        /* MH2: patch.bin becomes the file a new save gets */
        static uint8_t pay[MH2_PAYLOAD_SIZE];
        const char *P2 = "/" MH2_SAVE_DIR "/" MH2_SAVE_FILE;
        card_reset(); card_load(root, save_dir(SG_MH2), 1);
        CHECK(install_game(GAME_MH2, 0, id) == INS_OK, "MH2: install before removal");
        CHECK(remove_game(GAME_MH2, 0) == INS_OK, "MH2: remove");
        node *pb = find(MH2_SAVE_DIR, MH2_SAVE_FILE);
        CHECK(pb && pb->size == MH2_PATCHBIN_SIZE, "MH2: patch.bin kept, full size");
        CHECK(pb && !mh2_patchbin_decode(pb->data, pay), "MH2: removed patch.bin fails its checksum, as a new save's does");
        CHECK(!memcmp(pay, "06011622081000", 14) && !pay[0x14] && patcher_mh2_unpatched(pay),
              "MH2: payload = build stamp, 1000, length 0");
        CHECK(pb && !memcmp(pb->data, "\x01\x00\xd7\x26\x54\x68", 6), "MH2: the game's new-save header and seed");
        int zeros = 1;
        for (int i = 0x7d00; pb && i < 0x8250; i++) zeros &= !pb->data[i];
        CHECK(pb && zeros && pb->data[0x7cff] && pb->data[0x8250], "MH2: the new save's zero run at 0x7d00..0x824f");
        int writes = write_calls;
        CHECK(remove_game(GAME_MH2, 0) == INS_OK && write_calls == writes, "MH2: removing again writes nothing");

        card_reset(); card_load(root, save_dir(SG_MH2), 1);
        mc_remove(0, P2);
        CHECK(remove_game(GAME_MH2, 0) == INS_OK && find(MH2_SAVE_DIR, MH2_SAVE_FILE),
              "MH2: missing patch.bin -> the new-save file is written");

        card_reset(); card_load(root, save_dir(SG_MH2), 1);
        CHECK(install_game(GAME_MH2, 0, id) == INS_OK, "MH2: install");
        pb = find(MH2_SAVE_DIR, MH2_SAVE_FILE);
        uint32_t patched = crc32(0, pb->data, pb->size);
        fail_write_from = write_calls; fail_write_count = 2;
        CHECK(remove_game(GAME_MH2, 0) == INS_ERR_WRITE, "MH2: removal write fails twice -> INS_ERR_WRITE");
        pb = find(MH2_SAVE_DIR, MH2_SAVE_FILE);
        CHECK(pb && crc32(0, pb->data, pb->size) == patched, "MH2: patched patch.bin put back");
        fail_write_from = -1;

        card_reset(); card_load(root, save_dir(SG_MH2), 1);
        mc_remove(0, P2);
        fail_write_from = write_calls; fail_write_count = 2;
        CHECK(remove_game(GAME_MH2, 0) == INS_ERR_WRITE && !find(MH2_SAVE_DIR, MH2_SAVE_FILE),
              "MH2: no patch.bin and the write fails -> nothing left behind");
        fail_write_from = -1;

        /* MH2: an unreadable patch.bin is never overwritten or removed */
        for (int rm = 0; rm < 2; rm++) {
            card_reset(); card_load(root, save_dir(SG_MH2), 1);
            pb = find(MH2_SAVE_DIR, MH2_SAVE_FILE);
            uint32_t before = crc32(0, pb->data, pb->size);
            writes = write_calls; fail_read_count = 1;
            install_result r = rm ? remove_game(GAME_MH2, 0) : install_game(GAME_MH2, 0, id);
            fail_read_count = 0;
            pb = find(MH2_SAVE_DIR, MH2_SAVE_FILE);
            CHECK(r == INS_ERR_NO_SAVE && write_calls == writes && pb && crc32(0, pb->data, pb->size) == before,
                  "MH2 %s: read error -> INS_ERR_NO_SAVE, patch.bin untouched", rm ? "remove" : "install");
        }

        /* a never-patched save is given back exactly: install + remove */
        for (int g = 0; g < 2; g++) {
            card_reset(); card_load(root, save_dir(G[g].sg), 1);
            node *f = find(G[g].dir, G[g].file);
            memcpy(orig, f->data, G[g].size);
            memcpy(got, orig, G[g].size);
            mh_save_decode(got, G[g].words);
            if (got[0x10208 + 0x14] != 0) continue;
            install_game(G[g].game, 0, id); remove_game(G[g].game, 0);
            f = find(G[g].dir, G[g].file);
            CHECK(!memcmp(f->data, orig, G[g].size), "%s: install + remove gives the never-patched file back, byte for byte", G[g].dir);
        }
    }

    /* 18. leave one real backup of each game for external validation */
    usb_reset();
    for (int g = 0; g < SG_COUNT; g++) { card_reset(); card_load(root, save_dir(g), 7); save_scan(0, oc, ou); save_backup(0, g); }

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
