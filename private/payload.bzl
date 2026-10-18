# SPDX-License-Identifier: Apache-2.0

"""Optional bounded ASCII-JSON filename channel, separate from scalar results."""

visibility("//...")

_HEX = "0123456789abcdef"
_ASCII = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"

def _decode_payload(names):
    """Decode basename chunks into a JSON value.

    Require contiguous indexes, ASCII-escaped JSON, and at most 1 MiB of payload.
    This protocol is separate from scalar answer markers; it does not check a
    consumer-specific schema.
    """
    chunks = {}
    total = 0
    for name in sorted(names):
        if "=" not in name:
            continue
        parts = name.split("=")
        if len(parts) != 2 or len(parts[0]) != 8 or not all([c in "0123456789" for c in parts[0].elems()]):
            fail("malformed payload index: " + name)
        index = int(parts[0])
        data = parts[1]
        if index in chunks or not data or len(data) > 180 or len(data) % 2 or not all([c in _HEX for c in data.elems()]):
            fail("duplicate index or invalid payload hex: " + name)
        total += len(data)
        if total > 2 * 1024 * 1024:
            fail("structured payload exceeds 1 MiB")
        chunks[index] = data
    if not chunks or sorted(chunks.keys()) != list(range(len(chunks))):
        fail("missing/noncontiguous structured payload chunks")
    text = []
    for index in range(len(chunks)):
        data = chunks[index]
        for offset in range(0, len(data), 2):
            byte = _HEX.index(data[offset]) * 16 + _HEX.index(data[offset + 1])
            if byte < 32 or byte > 126:
                fail("payload JSON must use ASCII escaping")
            text.append(_ASCII[byte - 32])
    return json.decode("".join(text))

def read_payload(expanded_directory):
    """Read structured JSON from an ExpandedDirectory in a map_directory callback.

    Reads chunk filenames only, without opening payload.json or other contents.
    """
    return _decode_payload([f.basename for f in expanded_directory.children])
