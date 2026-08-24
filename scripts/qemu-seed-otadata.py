#!/usr/bin/env python3
"""Seed the otadata partition of a merged flash image.

QEMU's flash model dies on the first write the guest issues, and on a virgin
image that write comes from the bootloader: with no factory partition and an
erased otadata, bootloader_utility.c takes set_actual_ota_seq() ->
write_otadata() before it hands control to the app. The process exits with
status 5, after "Loaded app from partition at offset 0x10000" and before
"Disabling RNG early entropy source...", with nothing on stderr.

Writing a valid otadata entry into the image up front removes the need for that
write. The entry says "boot slot 0, image already marked valid", which is what
the bootloader would have written anyway.

  typedef struct {
      uint32_t ota_seq;
      uint8_t  seq_label[20];
      uint32_t ota_state;
      uint32_t crc;        // CRC32 of ota_seq only
  } esp_ota_select_entry_t;

The CRC is esp_rom_crc32_le(UINT32_MAX, &ota_seq, 4). That routine inverts the
running register on entry and on exit, so seeding it with UINT32_MAX starts the
register at zero -- the opposite of what zlib.crc32(data) does. Passing
0xFFFFFFFF as zlib's initial value lines the two up; computing it the obvious
way instead yields 0x6567bcb8 where the ROM wants 0x9a984347, and the
bootloader then rejects the entry and writes its own (back to exit 5).
"""

import argparse
import struct
import sys
import zlib

# Both otadata copies live in the 8 KB partition, one per 4 KB sector.
OTADATA_SECTOR_SIZE = 0x1000
ESP_OTA_IMG_VALID = 2


def otadata_entry(ota_seq: int, state: int = ESP_OTA_IMG_VALID) -> bytes:
    seq = struct.pack("<I", ota_seq)
    crc = zlib.crc32(seq, 0xFFFFFFFF) & 0xFFFFFFFF
    return seq + b"\xff" * 20 + struct.pack("<II", state, crc)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", help="merged flash image to patch in place")
    ap.add_argument("--offset", default="0xe000", help="otadata partition offset (default 0xe000)")
    ap.add_argument("--seq", type=int, default=1, help="ota_seq to write; 1 selects ota_0 (default 1)")
    args = ap.parse_args()

    offset = int(args.offset, 0)
    entry = otadata_entry(args.seq)

    with open(args.image, "r+b") as f:
        f.seek(0, 2)
        if f.tell() < offset + OTADATA_SECTOR_SIZE:
            print(f"error: {args.image} is smaller than the otadata partition at {args.offset}", file=sys.stderr)
            return 1
        # Only the first copy is written. The bootloader picks the higher valid
        # ota_seq and ignores an erased second copy, so leaving it at 0xff is
        # both valid and one less thing to keep consistent.
        f.seek(offset)
        f.write(entry)

    print(f"seeded otadata at {args.offset}: ota_seq={args.seq} crc=0x{struct.unpack('<I', entry[-4:])[0]:08x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
