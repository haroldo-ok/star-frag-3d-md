#!/usr/bin/env python3
"""rom_sanity.py — static checks on a built Mega Drive / Genesis ROM.

    python3 rom_sanity.py out/rom.bin [--strict] [--elf out/release/rom.out]

Checks the cartridge header that SGDK builds from src/rom_header.c:

* console name at $100 starts with "SEGA"
* ROM end address at $1A4 vs the real file size. SGDK's header template
  declares a fixed size (often $0FFFFF or $3FFFFF) while sizebnd pads the file
  to the next 128 KB boundary, so the two drift apart as content grows. Most
  emulators ignore the field, but a header claiming more ROM than exists has
  caused ILLEGAL INSTRUCTION crashes on real hardware / stricter emulators
  (seen in haroldo-ok/tuxemon-md). Reported as a WARNING; --strict makes it
  an error.
* checksum at $18E. Two formats exist and this reports both:
    - SGDK format (what `sizebnd -checksum` writes and SYS_isChecksumOk()
      verifies): XOR of every big-endian 32-bit word of the image with the
      checksum field zeroed, folded to 16 bits (low ^ high).
    - classic Sega format (what emulators and some flashcarts display):
      16-bit sum of the big-endian words from $200 to the end.
  An SGDK ROM therefore normally shows "classic: mismatch" in emulators —
  harmless unless your own code or a loader checks the classic value.
* SRAM declaration ("RA" at $1B0) and its address range.
* ROMs over 4 MB: how they will be mapped (see toolchain-and-build.md).
* --elf: disassembles the linked ELF and fails on 68020-only long branches
  (`bra.l`/`bsr.l`/`bcc.l`, opcode words $6xFF). On a 68000, $60FF decodes as
  `bra.s` with displacement -1, a jump to an odd address: black screen before
  the VDP is set up. Seen with SGDK 1.70 + Ubuntu's gcc-m68k-linux-gnu when the
  LTO-built library was assembled without -m68000 (haroldo-ok/elifoot2-md).

Exit status: 0 = OK (warnings allowed), 1 = error.
"""
import re
import shutil
import struct
import subprocess
import sys

OBJDUMPS = ("m68k-elf-objdump", "m68k-linux-gnu-objdump")


def be16(b, o):
    return struct.unpack(">H", b[o:o + 2])[0]


def be32(b, o):
    return struct.unpack(">I", b[o:o + 4])[0]


def sgdk_checksum(rom):
    data = bytearray(rom)
    data[0x18E] = data[0x18F] = 0
    if len(data) % 4:
        data += b"\0" * (4 - len(data) % 4)
    acc = 0
    for (w,) in struct.iter_unpack(">I", bytes(data)):
        acc ^= w
    return (acc ^ (acc >> 16)) & 0xFFFF


def classic_checksum(rom):
    body = rom[0x200:]
    if len(body) % 2:
        body += b"\0"
    return sum(w for (w,) in struct.iter_unpack(">H", body)) & 0xFFFF


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    path, strict = argv[1], "--strict" in argv[2:]
    elf = argv[argv.index("--elf") + 1] if "--elf" in argv[2:-1] else None
    rom = open(path, "rb").read()
    errors, warnings = [], []

    if len(rom) < 0x200:
        print(f"FAIL: {path} is only {len(rom)} bytes — no header")
        return 1

    console = rom[0x100:0x110].decode("ascii", "replace")
    name = rom[0x150:0x180].decode("ascii", "replace").strip()
    region = rom[0x1F0:0x1F3].decode("ascii", "replace")
    print(f"ROM       : {path} ({len(rom)} bytes = {len(rom) / 1024:.0f} KB)")
    print(f"console   : {console!r}   title: {name!r}   region: {region!r}")
    if not console.startswith("SEGA"):
        errors.append("console name at $100 does not start with 'SEGA' (TMSS consoles refuse to boot)")

    start, end = be32(rom, 0x1A0), be32(rom, 0x1A4)
    actual_end = len(rom) - 1
    print(f"ROM range : ${start:06X}-${end:06X} declared, file ends at ${actual_end:06X}")
    if end != actual_end:
        msg = (f"header ROM end ${end:06X} != file end ${actual_end:06X}; set the ROM-end "
               f"field in src/rom_header.c to 0x{actual_end:06X} (re-check after content grows)")
        (errors if (strict or end < actual_end) else warnings).append(msg)

    stored = be16(rom, 0x18E)
    sg, cl = sgdk_checksum(rom), classic_checksum(rom)
    print(f"checksum  : stored ${stored:04X}   SGDK-format ${sg:04X} "
          f"({'match' if stored == sg else 'mismatch'})   classic ${cl:04X} "
          f"({'match' if stored == cl else 'mismatch'})")
    if stored not in (sg, cl):
        warnings.append("checksum matches neither format — run `sizebnd -checksum` "
                        "(makefile.gen does) after every change to the image")

    if rom[0x1B0:0x1B2] == b"RA":
        stype, s0, s1 = rom[0x1B2], be32(rom, 0x1B4), be32(rom, 0x1B8)
        # type byte: $40 = battery-backed; ($18 bits) $18 odd bytes, $10 even, $00 16-bit
        lanes = {0x18: "odd addresses", 0x10: "even addresses", 0x00: "16-bit"}.get(stype & 0x18, "?")
        nbytes = (s1 - s0) // 2 + 1 if lanes != "16-bit" else s1 - s0 + 1
        print(f"SRAM      : declared, type ${stype:02X} "
              f"({'battery-backed' if stype & 0x40 else 'volatile'}, {lanes}), "
              f"${s0:06X}-${s1:06X} = {nbytes} bytes")
        if 0x200000 <= s0 < len(rom):
            print("NOTE      : ROM is larger than 2 MB, so SRAM shares the $200000 window "
                  "with ROM. Touch SRAM only between SRAM_enable()/SRAM_disable() (the "
                  "$A130F1 switch) and never read ROM data above $200000 while it is on.")
    else:
        print("SRAM      : not declared")

    if len(rom) > 0x400000:
        print(f"MAPPING   : {len(rom) / 1048576:.2f} MB is past the 4 MB a plain cartridge "
              f"maps. Genesis Plus GX maps it linearly (up to 10 MB) with a normal console "
              f"name, or switches to the Everdrive SSF mapper for 'SEGA SSF'. Reach data above "
              f"$400000 through SSF bank registers ($A130F3-$A130FF, SYS_setBank) or probe for "
              f"it; other emulators and carts differ.")

    if elf:
        tool = next((t for t in OBJDUMPS if shutil.which(t)), None)
        if not tool:
            warnings.append("--elf given but no m68k objdump found; 68020-branch scan skipped")
        else:
            dis = subprocess.run([tool, "-d", elf], capture_output=True, text=True).stdout
            bad = re.findall(r"^\s*([0-9a-f]+):\s+6[0-9a-f]ff\b.*$", dis, re.M)
            print(f"68020 ops : {len(bad)} long branch(es) ($6xFF) in {elf}")
            if bad:
                errors.append(f"{len(bad)} 68020-only long branches (first at ${int(bad[0], 16):06X}); "
                              "rebuild libmd.a/game with -m68000 at every assembly step "
                              "(or -Wa,--disp-size-default-16 without LTO)")

    for w in warnings:
        print("WARNING:", w)
    for e in errors:
        print("FAIL:", e)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
