#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Build the process-private GDI/COLR regression font from Chunkfive.ttf.

The existing outlines, cmap, hinting and metrics remain unchanged. Only names
are replaced, and COLR/CPAL tables color the existing A outline orange. This
keeps a complete, known GDI-loadable font instead of relying on small web-font
fixtures that the platform font installer may reject.

The generated font is a derivative under the same SIL Open Font License 1.1
as Chunkfive.ttf; see Chunkfive-license.txt. Its name is MicroGDIColor.
Run with --check to verify the checked-in fixture without writing it.
"""

import argparse
from pathlib import Path
import struct


FAMILY = "MicroGDIColor"
CHECKSUM_MAGIC = 0xB1B0AFBA


def checksum(data):
    data += b"\0" * (-len(data) % 4)
    return sum(struct.unpack(">%dI" % (len(data) // 4), data)) & 0xFFFFFFFF


def tables_from_sfnt(data):
    count = struct.unpack_from(">H", data, 4)[0]
    tables = {}
    for index in range(count):
        tag, expected, offset, length = struct.unpack_from(
            ">4sIII", data, 12 + 16 * index)
        table = data[offset:offset + length]
        assert len(table) == length
        checked = table[:8] + b"\0" * 4 + table[12:] if tag == b"head" else table
        assert checksum(checked) == expected, "Invalid table checksum: %r" % tag
        tables[tag] = table
    assert checksum(data) == CHECKSUM_MAGIC, "Invalid font checksum"
    return tables


def renamed_table(table):
    version, count, strings_offset = struct.unpack_from(">HHH", table)
    assert version == 0
    replacements = {
        1: FAMILY, 2: "Regular", 3: "1.000;test;" + FAMILY,
        4: FAMILY, 6: FAMILY, 16: FAMILY, 17: "Regular", 18: FAMILY,
    }
    records = []
    for index in range(count):
        platform, encoding, language, name, length, offset = struct.unpack_from(
            ">HHHHHH", table, 6 + 12 * index)
        value = table[strings_offset + offset:strings_offset + offset + length]
        if name in replacements:
            codec = "utf-16-be" if platform in (0, 3) else "mac_roman"
            value = replacements[name].encode(codec)
        records.append((platform, encoding, language, name, value))
    # Carry the derivative's license and provenance in readable font metadata.
    for platform, encoding, language in {(r[0], r[1], r[2]) for r in records}:
        codec = "utf-16-be" if platform in (0, 3) else "mac_roman"
        for name, value in (
                (13, "SIL Open Font License 1.1; see Chunkfive-license.txt. "
                     "Test-only derivative of the bundled ChunkFive font."),
                (14, "http://scripts.sil.org/OFL")):
            records.append((platform, encoding, language, name, value.encode(codec)))
    records.sort(key=lambda record: record[:4])
    strings = bytearray()
    directory = bytearray()
    for platform, encoding, language, name, value in records:
        directory += struct.pack(">HHHHHH", platform, encoding, language,
                                 name, len(value), len(strings))
        strings += value
    return struct.pack(">HHH", 0, len(records), 6 + 12 * len(records)) + directory + strings


def glyph_for_a(cmap):
    _, count = struct.unpack_from(">HH", cmap)
    for index in range(count):
        platform, encoding, offset = struct.unpack_from(">HHI", cmap, 4 + 8 * index)
        if platform != 3 or encoding != 1:
            continue
        table = cmap[offset:]
        assert struct.unpack_from(">H", table)[0] == 4
        segments = struct.unpack_from(">H", table, 6)[0] // 2
        for segment in range(segments):
            end = struct.unpack_from(">H", table, 14 + 2 * segment)[0]
            start = struct.unpack_from(">H", table, 16 + 2 * segments + 2 * segment)[0]
            if start <= ord("A") <= end:
                delta = struct.unpack_from(">h", table, 16 + 4 * segments + 2 * segment)[0]
                range_offset_pos = 16 + 6 * segments + 2 * segment
                range_offset = struct.unpack_from(">H", table, range_offset_pos)[0]
                if not range_offset:
                    return (ord("A") + delta) & 0xFFFF
                glyph = struct.unpack_from(
                    ">H", table, range_offset_pos + range_offset + 2 * (ord("A") - start))[0]
                return (glyph + delta) & 0xFFFF if glyph else 0
    raise ValueError("The input font does not map A in its Windows Unicode cmap")


def serialize(tables):
    head = bytearray(tables[b"head"])
    head[8:12] = b"\0" * 4
    tables = dict(tables)
    tables[b"head"] = bytes(head)
    count = len(tables)
    power = 1 << (count.bit_length() - 1)
    result = bytearray(struct.pack(">IHHHH", 0x10000, count, 16 * power,
                                   power.bit_length() - 1, 16 * (count - power)))
    result += b"\0" * (16 * count)
    head_offset = None
    for index, (tag, table) in enumerate(sorted(tables.items())):
        offset = len(result)
        struct.pack_into(">4sIII", result, 12 + 16 * index,
                         tag, checksum(table), offset, len(table))
        result += table + b"\0" * (-len(table) % 4)
        if tag == b"head":
            head_offset = offset
    struct.pack_into(">I", result, head_offset + 8,
                     (CHECKSUM_MAGIC - checksum(bytes(result))) & 0xFFFFFFFF)
    return bytes(result)


def generate(source):
    original = tables_from_sfnt(source)
    tables = dict(original)
    glyph = glyph_for_a(tables[b"cmap"])
    assert 0 < glyph < struct.unpack_from(">H", tables[b"maxp"], 4)[0]
    tables[b"name"] = renamed_table(tables[b"name"])
    tables[b"COLR"] = (struct.pack(">HHIIH", 0, 1, 14, 20, 1) +
                       struct.pack(">HHH", glyph, 0, 1) +
                       struct.pack(">HH", glyph, 0))
    tables[b"CPAL"] = struct.pack(">HHHHIHBBBB", 0, 1, 1, 1, 14, 0, 0, 128, 255, 255)
    result = serialize(tables)
    verified = tables_from_sfnt(result)
    for tag, value in original.items():
        if tag not in (b"head", b"name"):
            assert verified[tag] == value, "Changed source table: %r" % tag
    assert verified[b"head"][:8] == original[b"head"][:8]
    assert verified[b"head"][12:] == original[b"head"][12:]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    options = parser.parse_args()
    directory = Path(__file__).resolve().parent
    result = generate((directory / "Chunkfive.ttf").read_bytes())
    destination = directory / (FAMILY + ".ttf")
    if options.check:
        assert destination.read_bytes() == result, "Generated font differs from checked-in fixture"
        print("Verified %s: %d bytes, all SFNT checksums valid" % (destination.name, len(result)))
    else:
        destination.write_bytes(result)
        print("Generated %s: %d bytes, all SFNT checksums valid" % (destination.name, len(result)))


if __name__ == "__main__":
    main()
