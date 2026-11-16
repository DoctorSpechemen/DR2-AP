"""Read and rebuild Dead Rising 2's text table (data/frontend/str_en.bcs).

Layout: uint32 count, then count ids (ascending), then count offsets from the start of the file, then the texts
(UTF-8, each ended by a zero byte), then zero padding.

Usage: python3 strings_tool.py <in.bcs> <out.bcs> [id=text ...]     adds or replaces texts
"""
import struct
import sys


def read_table(data: bytes):
    count = struct.unpack_from("<I", data, 0)[0]
    ids = struct.unpack_from(f"<{count}I", data, 4)
    offsets = struct.unpack_from(f"<{count}I", data, 4 + 4 * count)
    texts = {}
    end_of_texts = 0
    for text_id, offset in zip(ids, offsets):
        end = data.index(b"\0", offset)
        texts[text_id] = data[offset:end]
        end_of_texts = max(end_of_texts, end + 1)
    return texts, len(data) - end_of_texts   # texts, and how much zero padding follows them


def build_table(texts: dict, padding: int) -> bytes:
    ids = sorted(texts)
    count = len(ids)
    offset = 4 + 8 * count
    offsets, body = [], bytearray()
    for text_id in ids:
        offsets.append(offset + len(body))
        body += texts[text_id] + b"\0"
    return struct.pack(f"<I{count}I{count}I", count, *ids, *offsets) + bytes(body) + b"\0" * padding


if __name__ == "__main__":
    source = open(sys.argv[1], "rb").read()
    table, pad = read_table(source)
    assert build_table(table, pad) == source, "this file does not have the layout this tool expects"
    for change in sys.argv[3:]:
        key, text = change.split("=", 1)
        table[int(key)] = text.encode("utf-8")
    # Every language's table in the game is the same size, so keep the size: new texts use up some of the padding.
    result = build_table(table, 0)
    assert len(result) <= len(source), "the new texts do not fit in the padding"
    result += b"\0" * (len(source) - len(result))
    open(sys.argv[2], "wb").write(result)
    print(f"wrote {sys.argv[2]}: {len(table)} texts, {len(result)} bytes (was {len(source)})")
