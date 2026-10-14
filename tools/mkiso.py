#!/usr/bin/env python3
# Minimal ISO9660 image builder for a bootable PS2 disc: a single root
# directory holding SYSTEM.CNF and the ELF. No external tools required.
#
#   python3 -I tools/mkiso.py <ELF> <OUT.iso>
#
# PS2 boots by reading BOOT2 from SYSTEM.CNF; filenames carry the ";1" version.
import sys, os, struct

SECTOR = 2048

def both16(v):  return struct.pack('<H', v) + struct.pack('>H', v)
def both32(v):  return struct.pack('<I', v) + struct.pack('>I', v)

def dtime():
    # 7-byte dir record timestamp: years since 1900, mon, day, h, m, s, gmt
    return bytes([125, 1, 1, 0, 0, 0, 0])

def vdtime():
    # 17-byte volume timestamp "YYYYMMDDHHMMSScc" + gmt
    return b"2026010100000000" + bytes([0])

def dir_record(name_bytes, lba, length, is_dir):
    flags = 0x02 if is_dir else 0x00
    rec = bytearray()
    rec += b'\x00'                 # length placeholder
    rec += b'\x00'                 # ext attr length
    rec += both32(lba)
    rec += both32(length)
    rec += dtime()
    rec += bytes([flags])
    rec += b'\x00'                 # file unit size
    rec += b'\x00'                 # interleave gap
    rec += both16(1)               # volume sequence number
    rec += bytes([len(name_bytes)])
    rec += name_bytes
    if len(rec) % 2:               # pad to even
        rec += b'\x00'
    rec[0] = len(rec)
    return bytes(rec)

def main():
    if len(sys.argv) != 3:
        sys.exit("usage: mkiso.py <ELF> <OUT.iso>")
    elf_path, out_path = sys.argv[1], sys.argv[2]
    elf = open(elf_path, 'rb').read()

    system_cnf = (
        "BOOT2 = cdrom0:\\MHPATCH.ELF;1\r\n"
        "VER = 1.00\r\n"
        "VMODE = NTSC\r\n"
    ).encode('ascii')

    def nsec(n): return (n + SECTOR - 1) // SECTOR

    # Fixed layout (LBA):
    LBA_PVD, LBA_VDST, LBA_LPATH, LBA_MPATH, LBA_ROOT = 16, 17, 18, 19, 20
    lba_cnf = 21
    cnf_secs = nsec(len(system_cnf))
    lba_elf = lba_cnf + cnf_secs
    elf_secs = nsec(len(elf))
    total = lba_elf + elf_secs

    # ---- root directory (one sector): '.', '..', SYSTEM.CNF, MHPATCH.ELF;1
    root = bytearray()
    root += dir_record(b'\x00', LBA_ROOT, SECTOR, True)    # .
    root += dir_record(b'\x01', LBA_ROOT, SECTOR, True)    # ..
    root += dir_record(b'SYSTEM.CNF;1', lba_cnf, len(system_cnf), False)
    root += dir_record(b'MHPATCH.ELF;1', lba_elf, len(elf), False)
    assert len(root) <= SECTOR, "root dir too large for one sector"
    root += b'\x00' * (SECTOR - len(root))

    # root directory record used inside the PVD (name = 0x00, 34 bytes)
    root_dr = dir_record(b'\x00', LBA_ROOT, SECTOR, True)
    root_dr = root_dr + b'\x00' * (34 - len(root_dr))

    # ---- path tables (just the root) ----
    def path_table(le):
        # dir id len=1, ext=0, extent, parent=1, id=0x00, pad
        ext = struct.pack('<I' if le else '>I', LBA_ROOT)
        parent = struct.pack('<H' if le else '>H', 1)
        rec = bytes([1, 0]) + ext + parent + b'\x00' + b'\x00'  # name 0x00 + pad
        return rec
    lpath = path_table(True)
    mpath = path_table(False)
    path_size = len(lpath)

    # ---- PVD ----
    def strpad(s, n):
        b = s.encode('ascii'); return b[:n] + b' ' * (n - len(b))
    pvd = bytearray()
    pvd += bytes([1]) + b'CD001' + bytes([1]) + b'\x00'
    pvd += strpad('PLAYSTATION', 32)       # system identifier
    pvd += strpad('MHPATCH', 32)           # volume identifier
    pvd += b'\x00' * 8
    pvd += both32(total)                   # volume space size (sectors)
    pvd += b'\x00' * 32
    pvd += both16(1)                       # volume set size
    pvd += both16(1)                       # volume sequence number
    pvd += both16(SECTOR)                  # logical block size
    pvd += both32(path_size)               # path table size
    pvd += struct.pack('<I', LBA_LPATH)    # type-L path table
    pvd += struct.pack('<I', 0)            # optional L
    pvd += struct.pack('>I', LBA_MPATH)    # type-M path table
    pvd += struct.pack('>I', 0)            # optional M
    pvd += root_dr                         # root directory record (34)
    pvd += strpad('', 128)                 # volume set id
    pvd += strpad('MH ONLINE PATCH', 128)  # publisher
    pvd += strpad('MH ONLINE PATCH', 128)  # data preparer
    pvd += strpad('MHPATCH', 128)          # application id
    pvd += strpad('', 37) + strpad('', 37) + strpad('', 37)  # copyright/abstract/biblio
    pvd += vdtime() + vdtime() + vdtime() + vdtime()         # 4 timestamps (17 each)
    pvd += bytes([1])                      # file structure version
    pvd += b'\x00'
    pvd += b'\x00' * 512                   # application used
    pvd += b'\x00' * 653                   # reserved
    assert len(pvd) == SECTOR, f"PVD size {len(pvd)}"

    # ---- VDST ----
    vdst = bytes([255]) + b'CD001' + bytes([1]) + b'\x00' * (SECTOR - 7)

    # ---- assemble image ----
    img = bytearray(total * SECTOR)
    def put(lba, data):
        img[lba*SECTOR:lba*SECTOR + len(data)] = data
    put(LBA_PVD, pvd)
    put(LBA_VDST, vdst)
    put(LBA_LPATH, lpath)
    put(LBA_MPATH, mpath)
    put(LBA_ROOT, root)
    put(lba_cnf, system_cnf)
    put(lba_elf, elf)

    open(out_path, 'wb').write(img)
    print(f"wrote {out_path}: {total} sectors ({total*SECTOR} bytes), "
          f"ELF {len(elf)} B at LBA {lba_elf}")

if __name__ == '__main__':
    main()
