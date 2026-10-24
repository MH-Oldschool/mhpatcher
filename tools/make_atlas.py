#!/usr/bin/env python3
# Renders every UI phrase (English and Japanese) and an ASCII strip into one
# 8-bit coverage texture, src/ui/atlas_data.h, with a lookup table.
# Set FONT to use a font other than the Noto Sans CJK candidates below.
#   python3 -I tools/make_atlas.py
import os, re, sys
from PIL import Image, ImageFont, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(HERE, "..", "src", "ui", "atlas_data.h")

FONT_CANDIDATES = [p for p in [
    os.environ.get("FONT"),
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc",
] if p]
def load_font(size):
    for p in FONT_CANDIDATES:
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size)
            except Exception:
                continue
    raise SystemExit("no CJK font found; install fonts-noto-cjk")

SZ_TITLE = 16
SZ_BODY  = 13
SZ_MONO  = 13
SZ_SMALL = 11

# the version string comes from src/version.h
_ver = open(os.path.join(HERE, "..", "src", "version.h")).read()
APP_VERSION = re.search(r'#define\s+APP_VERSION\s+"([^"]+)"', _ver).group(1)

# (ENUM, english, japanese, size)   size: 't' title / 'b' body / 's' small
STRINGS = [
    ("STR_VERSION",     "v" + APP_VERSION, "v" + APP_VERSION, 's'),
    ("STR_APP_TITLE",   "MONSTER HUNTER  Online Patch Installer", "モンスターハンター オンラインパッチ導入ツール", 't'),
    ("STR_CHOOSE_LANG", "Select language", "言語を選択してください", 't'),
    ("STR_LANG_EN",     "English", "English", 'b'),
    ("STR_LANG_JP",     "日本語", "日本語", 'b'),

    ("STR_ILINK_TITLE", "Console i.Link ID", "本体の i.Link ID", 't'),
    ("STR_ILINK_NOTE",  "This patch is sealed to THIS console. If the memory card is moved to",
                        "このパッチはこの本体専用に封印されます。メモリーカードを別の", 'b'),
    ("STR_ILINK_NOTE2", "another PS2, run this installer again on that console.",
                        "PS2で使う場合は、その本体で再度インストールしてください。", 'b'),

    ("STR_SELECT_MC",   "Select a memory card", "メモリーカードを選択", 't'),
    ("STR_MC_SLOT1",    "Memory Card  (Slot 1)", "メモリーカード（スロット1）", 'b'),
    ("STR_MC_SLOT2",    "Memory Card  (Slot 2)", "メモリーカード（スロット2）", 'b'),
    ("STR_MC_PRESENT",  "inserted", "挿入済み", 'b'),
    ("STR_MC_ABSENT",   "- not inserted -", "- 未挿入 -", 'b'),
    ("STR_MC_NONE",     "No memory card detected. Insert one and press Start to rescan.",
                        "メモリーカードが見つかりません。挿入してSTARTで再検索。", 'b'),

    ("STR_SCANNING",    "Scanning for save data...", "セーブデータを検索中...", 't'),
    ("STR_FOUND_TITLE", "Save data", "セーブデータ", 't'),
    ("STR_MH2_LABEL",   "Monster Hunter 2 (dos)", "モンスターハンター2 (ドス)", 'b'),
    ("STR_FOUND",       "FOUND", "検出", 'b'),
    ("STR_NOT_FOUND",   "not found", "未検出", 'b'),
    ("STR_NO_SAVES",    "No Monster Hunter 2 (dos), G [Japan] or [Europe] save data on this card.",
                        "このカードにMH2 (ドス)/MHG［日本版］/MH［欧州版］のセーブデータがありません。", 'b'),

    ("STR_CONFIRM_TTL", "Confirm installation", "インストールの確認", 't'),
    ("STR_BACKUP_WARN", "STRONGLY RECOMMENDED: back up your save data before patching.",
                        "【強く推奨】適用前にセーブデータのバックアップを。", 'b'),
    ("STR_CONFIRM_1",   "Install the official patch for:", "次のゲームへ公式パッチを導入します:", 'b'),
    ("STR_A_BACKUP",    "A backup of the save is kept in memory during patching.",
                        "適用中はメモリ内にセーブのバックアップを保持します。", 'b'),

    ("STR_INJECTING",   "Injecting patch...", "パッチを書き込み中...", 't'),
    ("STR_INSTALLING",  "Do not power off or remove the memory card.",
                        "電源を切ったりカードを抜かないでください。", 'b'),
    ("STR_OK_TITLE",    "Installation complete", "インストール完了", 't'),
    ("STR_OK_BODY",     "Start the game and load this save. The patch applies",
                        "ゲームを起動しこのセーブをロードしてください。パッチは", 'b'),
    ("STR_OK_BODY2",    "automatically when you play online.",
                        "オンラインプレイ時に自動で適用されます。", 'b'),
    ("STR_FAIL_TITLE",  "Installation failed", "インストール失敗", 't'),
    ("STR_FAIL_LOST",   "Writing failed and the original save could not be put back.",
                        "書き込みに失敗し、元のセーブを戻せませんでした。", 'b'),
    ("STR_FAIL_BODY",   "An error occurred. The save was restored from the backup.",
                        "エラーが発生しました。バックアップから復元しました。", 'b'),

    # patch removal
    ("STR_RMC_TITLE",   "Confirm removal", "削除の確認", 't'),
    ("STR_RMC_BODY",    "Remove the patch from:", "次のゲームからパッチを削除します:", 'b'),
    ("STR_REMOVING",    "Removing patch...", "パッチを削除中...", 't'),
    ("STR_RMOK_TITLE",  "Patch removed", "パッチ削除完了", 't'),
    ("STR_RMOK_BODY",   "The patch has been cleared from the save data.",
                        "セーブデータからパッチを削除しました。", 'b'),

    # main menu
    ("STR_MODE_TITLE",  "What would you like to do?", "実行する操作を選択してください", 't'),
    ("STR_MODE_INSTALL","Install patches", "パッチをインストール", 'b'),
    ("STR_MODE_BACKUP", "Backup saves", "セーブデータのバックアップ", 'b'),
    ("STR_MODE_REMOVE", "Remove patches", "パッチを削除", 'b'),

    # save backup / restore
    ("STR_MHGJ_LABEL",  "Monster Hunter G [Japan]", "モンスターハンターG［日本版］", 'b'),
    ("STR_MHGK_LABEL",  "Monster Hunter G [Korea]", "モンスターハンターG［韓国版］", 'b'),
    ("STR_MH1J_LABEL",  "Monster Hunter [Japan]", "モンスターハンター［日本版］", 'b'),
    ("STR_MH1U_LABEL",  "Monster Hunter [USA]", "モンスターハンター［北米版］", 'b'),
    ("STR_MH1E_LABEL",  "Monster Hunter [Europe]", "モンスターハンター［欧州版］", 'b'),
    ("STR_COL_CARD",    "Memory card", "メモリーカード", 'b'),
    ("STR_COL_USB",     "USB drive", "USBドライブ", 'b'),
    ("STR_USB_TITLE",   "USB drive", "USBドライブ", 't'),
    ("STR_USB_SEARCH",  "Looking for a USB drive...", "USBドライブを検索中...", 'b'),
    ("STR_USB_NONE",    "No usable USB drive was found.", "使用できるUSBドライブが見つかりません。", 'b'),
    ("STR_USB_NONE2",   "Connect a drive formatted as FAT32 or exFAT, then press START.",
                        "FAT32またはexFATでフォーマットしたドライブを接続し、STARTを押してください。", 'b'),
    ("STR_BK_TITLE",    "Save data backup", "セーブデータのバックアップ", 't'),
    ("STR_BK_NONE",     "No Monster Hunter save data on the memory card or the USB drive.",
                        "メモリーカードにもUSBドライブにもモンスターハンターのセーブがありません。", 'b'),
    ("STR_ACT_TITLE",   "Choose an action", "操作を選択してください", 't'),
    ("STR_ACT_BACKUP",  "Back up to the USB drive", "USBドライブへバックアップ", 'b'),
    ("STR_ACT_RESTORE", "Restore to the memory card", "メモリーカードへ復元", 'b'),
    ("STR_BKC_TITLE",   "Confirm backup", "バックアップの確認", 't'),
    ("STR_BKC_BODY",    "Copy this save from the memory card to the USB drive:",
                        "次のセーブをメモリーカードからUSBドライブへコピーします:", 'b'),
    ("STR_BKC_OVER",    "The backup already on the USB drive will be replaced.",
                        "USBドライブにある既存のバックアップは置き換えられます。", 'b'),
    ("STR_BKC_KEEP",    "The previous backup is kept on the drive as a .bak file.",
                        "以前のバックアップは .bak ファイルとして残ります。", 'b'),
    ("STR_RSC_TITLE",   "Confirm restore", "復元の確認", 't'),
    ("STR_RSC_BODY",    "Copy this save from the USB drive to the memory card:",
                        "次のセーブをUSBドライブからメモリーカードへコピーします:", 'b'),
    ("STR_RSC_OVER",    "WARNING: the save on the memory card will be REPLACED.",
                        "【注意】メモリーカード上のセーブは上書きされます。", 'b'),
    ("STR_RSC_SAFETY",  "Before that, the current save is copied to the USB drive as:",
                        "その前に、現在のセーブを次の名前でUSBドライブへ保存します:", 'b'),
    ("STR_BK_WORKING",  "Copying save data...", "セーブデータをコピー中...", 't'),
    ("STR_BK_NOREMOVE", "Do not power off or remove the memory card or the USB drive.",
                        "電源を切ったり、メモリーカードやUSBドライブを抜かないでください。", 'b'),
    ("STR_BKOK_TITLE",  "Backup complete", "バックアップ完了", 't'),
    ("STR_BKOK_BODY",   "The save was copied to the USB drive and verified:",
                        "セーブをUSBドライブへコピーし、検証しました:", 'b'),
    ("STR_RSOK_TITLE",  "Restore complete", "復元完了", 't'),
    ("STR_RSOK_BODY",   "The save was copied to the memory card and verified.",
                        "セーブをメモリーカードへコピーし、検証しました。", 'b'),
    ("STR_RSOK_PATCH",  "To play online on this console, install the patch again.",
                        "この本体でオンラインプレイするには、パッチを再インストールしてください。", 'b'),
    ("STR_BKF_TITLE",   "Operation failed", "処理に失敗しました", 't'),
    ("STR_E_USB_READ",  "The USB drive was removed or could not be read.",
                        "USBドライブが外されたか、読み込めませんでした。", 'b'),
    ("STR_E_USB_WRITE", "Could not write to the USB drive (full or write-protected).",
                        "USBドライブに書き込めません（容量不足または書き込み禁止）。", 'b'),
    ("STR_E_MC_GONE",   "The memory card was removed or changed.",
                        "メモリーカードが外されたか、交換されました。", 'b'),
    ("STR_E_MC_READ",   "The save could not be read from the memory card.",
                        "メモリーカードからセーブを読み込めませんでした。", 'b'),
    ("STR_E_MC_SPACE",  "Not enough free space on the memory card.",
                        "メモリーカードの空き容量が足りません。", 'b'),
    ("STR_E_BAD_BACKUP","The backup file is damaged or is not a save for this game.",
                        "バックアップファイルが壊れているか、このゲームのものではありません。", 'b'),
    ("STR_E_TOO_BIG",   "This save has an unexpected layout and was not copied.",
                        "このセーブは想定外の構成のため、コピーしませんでした。", 'b'),
    ("STR_E_MC_WRITE",  "Writing to the memory card failed. Your original save was put back.",
                        "メモリーカードへの書き込みに失敗しました。元のセーブを書き戻しました。", 'b'),
    ("STR_E_MC_WRITE2", "Writing to the memory card failed. The partial copy was removed.",
                        "メモリーカードへの書き込みに失敗しました。途中までのデータは削除しました。", 'b'),
    ("STR_E_USB_BUSY",  "The USB port is in use by another program (e.g. OPL running this disc from USB).",
                        "USBポートは他のプログラムが使用中です（例：OPLがUSBからこのディスクを起動中）。", 'b'),
    ("STR_E_USB_BUSY2", "For USB backups, start this tool from a disc, from OPL's Apps tab,",
                        "USBバックアップを使うには、ディスク、OPLのアプリ一覧、", 'b'),
    ("STR_E_USB_BUSY3", "or from OPL loading games from its HDD or network share.",
                        "またはHDD/ネットワークから起動したOPLで本ツールを起動してください。", 'b'),
    ("STR_E_NO_ILINK",  "The console's i.Link ID could not be read, so no patch can be installed.",
                        "本体のi.Link IDを読み取れないため、パッチをインストールできません。", 'b'),
    ("STR_E_DRIVER",    "The USB driver could not be started.", "USBドライバーを起動できませんでした。", 'b'),
    ("STR_E_MC_LOST",   "Writing failed and the original could not be put back. It is safe on the USB drive:",
                        "書き込みに失敗し、元のセーブを書き戻せませんでした。USBに保存済みです:", 'b'),
    ("STR_E_NOTHING",   "Nothing was changed on the memory card.",
                        "メモリーカードは変更されていません。", 'b'),

    ("STR_BTN_CONFIRM", "Confirm", "決定", 'b'),
    ("STR_BTN_BACK",    "Back", "戻る", 'b'),
    ("STR_BTN_RESCAN",  "Rescan", "再検索", 'b'),
]

# ASCII strip for dynamic text (ids, paths, error codes)
ASCII_FIRST = 0x20
ASCII_LAST  = 0x7e

def main():
    ft_title = load_font(SZ_TITLE)
    ft_body  = load_font(SZ_BODY)
    ft_mono  = load_font(SZ_MONO)
    ft_small = load_font(SZ_SMALL)

    # measure helper
    scratch = Image.new("RGBA", (8, 8))
    d0 = ImageDraw.Draw(scratch)
    def measure(font, text):
        if not text:
            return (1, font.size + 4)
        l, t, r, b = d0.textbbox((0, 0), text, font=font)
        return (r - l + 2, b - t + 2), (l, t)

    ATLAS_W = 1024
    pad = 2
    # layout rows top-down
    entries = []   # (enum, lang, x, y, w, h)
    x = pad; y = pad; rowh = 0

    def place(w, h):
        nonlocal x, y, rowh
        if x + w + pad > ATLAS_W:
            x = pad; y += rowh + pad; rowh = 0
        rx, ry = x, y
        x += w + pad
        rowh = max(rowh, h)
        return rx, ry

    # (x, y, text, font, offset, baseline): phrases are placed by their bounding
    # box, ASCII glyphs on a shared baseline
    renders = []
    baselines = {}   # (enum, lang) -> baseline offset from the rect top
    # tallest first, so each shelf holds phrases of one height
    phrases = []
    for enum, en, jp, sz in STRINGS:
        font = {'t': ft_title, 's': ft_small}.get(sz, ft_body)
        for lang, text in (("EN", en), ("JP", jp)):
            phrases.append((enum, lang, text, font, measure(font, text)))
    phrases.sort(key=lambda p: -p[4][0][1])
    for enum, lang, text, font, ((w, h), off) in phrases:
        rx, ry = place(w, h)
        entries.append((enum, lang, rx, ry, w, h))
        renders.append((rx, ry, text, font, off, None))
        # drawn at (rx - off.x + 1, ry - off.y + 1) with the default "la"
        # anchor, whose baseline is one ascent below the drawing origin
        baselines[(enum, lang)] = 1 + font.getmetrics()[0] - off[1]

    # fixed cells for packing, but real advance widths for proportional text
    asc, desc = ft_mono.getmetrics()
    cellh = asc + desc + 2
    cellw = 0
    advance = []
    for c in range(ASCII_FIRST, ASCII_LAST + 1):
        adv = max(int(round(ft_mono.getlength(chr(c)))), 3)
        advance.append(adv)
        cellw = max(cellw, adv)
    cellw += 2
    nchars = ASCII_LAST - ASCII_FIRST + 1
    # lay the strip out in several rows so it never exceeds the atlas width
    ascii_cols = (ATLAS_W - 2 * pad) // cellw
    ascii_rows = (nchars + ascii_cols - 1) // ascii_cols
    ascii_x, ascii_y = place(ascii_cols * cellw, ascii_rows * cellh)
    for i, c in enumerate(range(ASCII_FIRST, ASCII_LAST + 1)):
        cx = ascii_x + (i % ascii_cols) * cellw
        cy = ascii_y + (i // ascii_cols) * cellh
        renders.append((cx, cy, chr(c), ft_mono, None, asc))

    ATLAS_H = 1
    while ATLAS_H < y + rowh + pad:
        ATLAS_H *= 2
    if ATLAS_H > 1024:
        raise SystemExit(f"atlas height {ATLAS_H} exceeds GS 1024 limit; shorten strings")

    # 8-bit coverage, drawn through a grey ramp CLUT and tinted per draw
    img = Image.new("L", (ATLAS_W, ATLAS_H), 0)
    draw = ImageDraw.Draw(img)
    for rx, ry, text, font, off, baseline in renders:
        if baseline is None:
            draw.text((rx - off[0] + 1, ry - off[1] + 1), text, font=font, fill=255)
        else:
            draw.text((rx + 1, ry + 1 + baseline), text, font=font, fill=255, anchor="ls")

    # Emit C header
    px = img.tobytes()  # one byte per pixel (alpha/coverage)
    with open(OUT, "w") as f:
        f.write("/* Generated by tools/make_atlas.py. */\n")
        f.write("/* 8-bit coverage; upload as GS_PSM_T8 with a white-ramp CLUT. */\n")
        f.write("#ifndef ATLAS_DATA_H\n#define ATLAS_DATA_H\n#include <stdint.h>\n\n")
        f.write(f"#define ATLAS_W {ATLAS_W}\n#define ATLAS_H {ATLAS_H}\n")
        f.write(f"#define ATLAS_ASCII_FIRST {ASCII_FIRST}\n#define ATLAS_ASCII_LAST {ASCII_LAST}\n")
        f.write(f"#define ATLAS_ASCII_X {ascii_x}\n#define ATLAS_ASCII_Y {ascii_y}\n")
        f.write(f"#define ATLAS_ASCII_CELLW {cellw}\n#define ATLAS_ASCII_CELLH {cellh}\n")
        f.write(f"#define ATLAS_ASCII_COLS {ascii_cols}\n")
        f.write(f"#define ATLAS_ASCII_BASELINE {1 + asc}  /* baseline below a glyph cell's top */\n")
        f.write(f"static const uint8_t atlas_ascii_w[{nchars}] = {{ "
                + ",".join(str(a) for a in advance) + " };\n\n")
        # enum ids
        f.write("typedef enum {\n")
        for i, (enum, *_rest) in enumerate(STRINGS):
            f.write(f"  {enum} = {i},\n")
        f.write(f"  STR_COUNT = {len(STRINGS)}\n}} atlas_str;\n\n")
        # rect table: [id][lang] -> x,y,w,h  (lang 0=EN,1=JP)
        f.write("typedef struct { uint16_t x, y, w, h; } atlas_rect;\n")
        f.write("static const atlas_rect atlas_rects[STR_COUNT][2] = {\n")
        by = {}
        for enum, lang, rx, ry, w, h in entries:
            by.setdefault(enum, {})[lang] = (rx, ry, w, h)
        for enum, *_ in STRINGS:
            e = by[enum]["EN"]; j = by[enum]["JP"]
            f.write(f"  {{ {{{e[0]},{e[1]},{e[2]},{e[3]}}}, {{{j[0]},{j[1]},{j[2]},{j[3]}}} }},\n")
        f.write("};\n\n")
        f.write("/* baseline of each phrase, in pixels below its rect top: [id][lang] */\n")
        f.write("static const uint8_t atlas_baseline[STR_COUNT][2] = {\n")
        for enum, *_ in STRINGS:
            f.write(f"  {{ {baselines[(enum, 'EN')]}, {baselines[(enum, 'JP')]} }},\n")
        f.write("};\n\n")
        f.write(f"static const uint8_t atlas_alpha[{len(px)}] = {{\n")
        for i in range(0, len(px), 40):
            f.write(" " + ",".join(str(b) for b in px[i:i+40]) + ",\n")
        f.write("};\n#endif\n")
    print(f"wrote {OUT}: atlas {ATLAS_W}x{ATLAS_H} (8bpp), {len(STRINGS)} strings, {len(px)} px bytes")

if __name__ == "__main__":
    main()
