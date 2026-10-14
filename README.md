# mhpatcher

PS2 homebrew for Monster Hunter online play:

- **Install patches**: writes the official online patch for Monster Hunter 2 (dos)
  and Monster Hunter G (Japan) into the game's save, sealed to the console it
  runs on.
- **Backup saves**: backs up and restores Monster Hunter saves between a memory
  card and a USB drive.

English and Japanese. NTSC and PAL consoles (480i/576i).

> Fan project, not affiliated with Capcom or Sony.

## Running

Use `build/mhpatcher.iso` or `build/mhpatcher.elf`:

- burnt to a disc (modchip, FreeMcBoot, FreeDVDBoot, ...)
- from OPL: the ISO from the games list, or the ELF from the Apps tab
- from uLaunchELF/wLaunchELF
- in PCSX2

When OPL runs the ISO from a USB (or iLink, MX4SIO, exFAT HDD) drive, its game
driver keeps the drive, so USB backups are not available; the tool says so. Patch
installation still works. From an APA HDD, a network share, the Apps tab or a disc,
both work.

## Install patches

Pick the memory card and the game. The patch is sealed to the console's i.Link
ID (shown top right), so it only works on that console: if the card is moved to
another PS2, install it again there. The original save is kept in memory and
written back if anything fails.

## Backup saves

Needs a USB drive formatted as FAT32 or exFAT. Each save is stored as one
`.psu` file, which uLaunchELF and mymc can also open; `.psu` files made by those
tools can be restored too.

| Game | Card folder |
|---|---|
| Monster Hunter 2 (dos) | `BISLPM-66280MH2` |
| Monster Hunter G (Japan) | `BISLPM-65869MHG` |
| Monster Hunter G (Korea) | `BKSLKA-25219MHG` |
| Monster Hunter (Japan) | `BISLPM-65495MH` |
| Monster Hunter (USA) | `BASLUS-20896MH` |
| Monster Hunter (Europe) | `BESLES-52707MH` |

Files on the drive, in `MHSAVES/`:

- `<folder>.psu`: the backup
- `<folder>.psu.bak`: the previous backup
- `<folder>_before_restore.psu`, `_before_restore_2.psu`, ...: what was on the
  card before each restore (never overwritten)

Every write to the card or the drive is read back and compared. A restore
checks the backup and the card's free space before writing anything, and puts
the original save back if writing fails. After restoring an MH2 or MHG save
made on another console, install the patch again.

## Building

Needs the [ps2dev](https://github.com/ps2dev/ps2dev) toolchain (gcc, ps2sdk,
gsKit), either in `toolchain/ps2dev` or wherever `$PS2DEV` points, plus `xxd`
and Python 3.

```sh
. ./env.sh
make          # build/mhpatcher.elf
make iso      # build/mhpatcher.iso
make hostfs   # build/mhpatcher-hostfs.elf: uses PCSX2's host folder as the
              # USB drive, since PCSX2 cannot emulate one
```

The UI text is prerendered into `src/ui/atlas_data.h`. After changing a string
in `tools/make_atlas.py` or the version in `src/version.h`, run `make atlas`
(needs Pillow and Noto Sans CJK, or a font set in `$FONT`).

## Tests

PC-side tests for backup, restore and patch installation against a simulated
memory card with injected faults, and a screenshot renderer for the UI:

```sh
make -C tests
tests/saves_test <saves-dir> <tmp-dir>
tests/ui_preview <saves-dir> <tmp-dir> <out-dir>
```

`<saves-dir>` holds save folders copied off a memory card, named as in the
table above.

## Layout

```
src/main.c    screens
src/app/      patch install, backup/restore, .psu format
src/dnas/     patch sealing and save encoding
src/crypto/   SHA-1, DES, RC6
src/ps2/      video, pad, memory card, USB, i.Link ID
src/ui/       generated text atlas and backdrop
data/         patch data, built into src/dnas/baked_data.h
tools/        atlas, backdrop, baked data and ISO generators
tests/        PC-side tests
```

## License

GPL-3.0-or-later, see [LICENSE](LICENSE).
