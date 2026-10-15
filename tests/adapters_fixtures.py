"""Original synthetic fixtures; no artwork is copied into the public repository."""
from pathlib import Path
import io
import re
import struct


def sai2_file(path, chunks, width=1, height=1, flags=0x100, tag=b"norm"):
    header = bytearray(64)
    header[:16] = b"SAI-CANVAS-TYPE0"
    struct.pack_into("<III", header, 16, flags, width, height)
    struct.pack_into("<I", header, 32, len(chunks))
    header[60:64] = tag
    table, bodies, offset = bytearray(), bytearray(), 64 + 16 * len(chunks)
    for kind, body in chunks:
        table.extend(struct.pack("<4sIQ", kind, 0, offset))
        bodies.extend(body)
        offset += len(body)
    Path(path).write_bytes(header + table + bodies)


def dpcm_row(channels):
    bits = []
    def put(value, length):
        bits.extend((value >> i) & 1 for i in range(length))
    for values in channels:
        for value in values:
            if value == 0:
                put(1, 1); put(0, 1)
                continue
            opcode = (abs(value) + 1).bit_length() - 1
            put(0, opcode // 2); put(1, 1); put(opcode % 2, 1)
            put(abs(value) + 1 - (1 << opcode), opcode)
            put(int(value < 0), 1)
    output = bytearray((len(bits) + 7) // 8)
    for i, bit in enumerate(bits):
        output[i // 8] |= bit << (i % 8)
    return bytes(output)


def sai2_red(path, width=17, height=9, transparent=False):
    channels = 4 if transparent else 3
    rows = []
    for y in range(height):
        values = [[0] * width for _ in range(channels)]
        if y == 0:
            values[2][0] = 255
            if transparent:
                values[3][0] = 128
        rows.append(dpcm_row(values))
    tile = b"\xff\x00" + b"".join(rows)
    body = b"dpcm" + struct.pack("<I", len(tile)) + tile + b"\xff\x01"
    body += b"\0" * (-len(body) % 4)
    sai2_file(path, [(b"intg", body)], width, height, 0x2000 if transparent else 0x100)


def jssf_thumbnail():
    from PIL import Image
    buffer = io.BytesIO()
    Image.new("RGB", (8, 8), (20, 160, 240)).save(buffer, "JPEG", subsampling=0, optimize=False, quality=85)
    data = buffer.getvalue()
    offset, qtables, scan = 2, {}, b""
    while offset + 4 < len(data):
        marker, length = data[offset + 1], struct.unpack_from(">H", data, offset + 2)[0]
        if marker == 0xDB:
            content = data[offset + 4:offset + 2 + length]
            for i in range(0, len(content), 65):
                qtables[content[i]] = content[i + 1:i + 65]
        if marker == 0xDA:
            scan = data[offset + 2 + length:-2]
            break
        offset += 2 + length
    return struct.pack("<II4sHHH", 8, 8, b"jssf", 8, 8, 3) + qtables[0] + qtables[1] + struct.pack("<H", len(scan)) + scan


def sai1_thumbnail(path, libsai_root):
    """Create a tiny encrypted VFS with a BM32 thumbnail and canvas header.

    Format/encryption documented by MIT Wunkolo/libsai, whose pinned source is
    a build dependency. Test artwork is a new 2x2 red/green/blue/transparent image.
    """
    key_source = (Path(libsai_root) / "source/keys.cpp").read_text()
    start = key_source.index("User")
    keys = [int(value, 16) for value in re.findall(r"0x[0-9A-Fa-f]+", key_source[start:])[:256]]
    mask = (1 << 32) - 1
    pages = [bytearray(4096) for _ in range(5)]
    rgba = bytes((255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 40, 50, 60, 0))
    bgra = bytearray(rgba)
    for i in range(0, len(bgra), 4):
        bgra[i], bgra[i + 2] = bgra[i + 2], bgra[i]
    thumb = struct.pack("<II4s", 2, 2, b"BM32") + bgra
    canvas = struct.pack("<III", 16, 1024, 768)
    for i, (name, page_index, data) in enumerate(((b"thumbnail", 3, thumb), (b"canvas", 4, canvas))):
        struct.pack_into("<I32sBBBBIIQQ", pages[2], i * 64, 1, name, 0, 0, 0x80, 0, page_index, len(data), 0, 0)
        pages[page_index][:len(data)] = data
    def checksum(data):
        value = 0
        for number in struct.unpack("<1024I", data):
            value = (((value << 1) | (value >> 31)) & mask) ^ number
        return value | 1
    for index in range(1, 5):
        struct.pack_into("<II", pages[0], index * 8, checksum(pages[index]), 0)
    def key_sum(previous):
        return sum(keys[(previous >> shift) & 255] for shift in (0, 8, 16, 24)) & mask
    encrypted = bytearray()
    for index, page in enumerate(pages):
        previous = 0 if index == 0 else struct.unpack_from("<I", pages[0], index * 8)[0]
        for number in struct.unpack("<1024I", page):
            if index == 0:
                rotated = ((number << 16) | (number >> 16)) & mask
                cipher = rotated ^ previous ^ key_sum(previous)
            else:
                cipher = (number + (previous ^ key_sum(previous))) & mask
            encrypted.extend(struct.pack("<I", cipher))
            previous = cipher
    Path(path).write_bytes(encrypted)
    return rgba
