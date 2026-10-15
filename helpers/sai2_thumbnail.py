"""Bounded reader for SAI2's saved JSSF thumbnail.

JSSF framing follows Wunkolo/libsai (MIT, see ADAPTER_NOTICES.md).
JPEG Huffman tables come from Pillow's baseline encoder, not the artwork.
This never reconstructs SAI layers or writes a SAI2 file.
"""
import io
from pathlib import Path
import struct


def read_header(path):
    with open(path, "rb") as stream:
        header = stream.read(64)
    if len(header) != 64 or header[:16] != b"SAI-CANVAS-TYPE0":
        raise ValueError("无效或截断的SAI2文件头")
    return {"width": struct.unpack_from("<I", header, 20)[0],
            "height": struct.unpack_from("<I", header, 24)[0],
            "chunks": struct.unpack_from("<I", header, 32)[0],
            "format_tag": header[60:64].decode("ascii", "replace")}


def jpeg_segment(marker, data):
    return bytes((255, marker)) + struct.pack(">H", len(data) + 2) + data


def standard_huffman_tables(channels):
    from PIL import Image
    buffer = io.BytesIO()
    Image.new("L" if channels == 1 else "RGB", (8, 8)).save(buffer, "JPEG", optimize=False)
    data, offset, result = buffer.getvalue(), 2, []
    while offset + 4 <= len(data):
        marker = data[offset + 1]
        length = struct.unpack_from(">H", data, offset + 2)[0]
        if marker == 0xC4:
            result.append(data[offset:offset + 2 + length])
        if marker == 0xDA:
            break
        offset += 2 + length
    return b"".join(result)


def decode_jssf(body):
    from PIL import Image
    if len(body) < 18 or body[8:12] != b"jssf":
        raise ValueError("不支持的SAI2缩略图编码")
    width, height, channels = struct.unpack_from("<HHH", body, 12)
    if not width or not height or width * height > 16_000_000 or channels not in (1, 3):
        raise ValueError("无效的SAI2缩略图尺寸或通道")
    offset = 18
    q_count = 1 if channels == 1 else 2
    if len(body) < offset + q_count * 64:
        raise ValueError("截断的SAI2量化表")
    qtables = b"".join(bytes((i,)) + body[offset + i * 64:offset + (i + 1) * 64] for i in range(q_count))
    offset += q_count * 64
    components = b"".join(bytes((i + 1, 0x11, int(i != 0))) for i in range(channels))
    scan = bytes((channels,)) + b"".join(bytes((i + 1, 0 if i == 0 else 0x11)) for i in range(channels)) + b"\x00\x3f\x00"
    jpeg = bytearray(b"\xff\xd8" + jpeg_segment(0xDB, qtables) + standard_huffman_tables(channels)
                     + jpeg_segment(0xC0, b"\x08" + struct.pack(">HHB", height, width, channels) + components)
                     + jpeg_segment(0xDD, struct.pack(">H", (width + 7) // 8)) + jpeg_segment(0xDA, scan))
    rows = (height + 7) // 8
    for row in range(rows):
        if offset + 2 > len(body):
            raise ValueError("截断的SAI2缩略图行")
        length = struct.unpack_from("<H", body, offset)[0]
        offset += 2
        if offset + length > len(body):
            raise ValueError("截断的SAI2缩略图流")
        jpeg.extend(body[offset:offset + length])
        offset += length
        if row + 1 < rows:
            jpeg.extend((255, 0xD0 | (row & 7)))
    jpeg.extend(b"\xff\xd9")
    image = Image.open(io.BytesIO(jpeg))
    image.load()
    return image


def extract_thumbnail(path):
    header = read_header(path)
    count = header["chunks"]
    file_size = Path(path).stat().st_size
    if count > 1_000_000 or 64 + count * 16 > file_size:
        raise ValueError("无效的SAI2块索引")
    with open(path, "rb") as stream:
        stream.seek(64)
        entries = [struct.unpack("<4sIQ", stream.read(16)) for _ in range(count)]
        offsets = sorted(set(item[2] for item in entries) | {file_size})
        for kind, _, offset in entries:
            if kind != b"thum":
                continue
            if offset < 64 + 16 * count or offset >= file_size:
                raise ValueError("无效的SAI2缩略图位置")
            end = next(value for value in offsets if value > offset)
            if end - offset > 32 * 1024 * 1024:
                raise ValueError("SAI2内嵌缩略图超过大小上限")
            stream.seek(offset)
            return decode_jssf(stream.read(end - offset))
    return None
