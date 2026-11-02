"""Route NGI MMF imports through the native audio compatibility bridge."""
import hashlib
import struct

from component_identity import header_crc
from merge_packages import data_units
from sis import Field, Package, array, array_items, decompress, fields, sequence

AUDIO_SHA256 = '7803d9c064a12eaf20cf42f0b3ddc7e436993ba1950d70843641d5f95b664485'
ORIGINAL_IMPORT = b'MmfDevSound{000a0000}[100058cc].dll'
COMPAT_IMPORT = b'NgageSoundX{000a0000}[e0000964].dll'


def bytepair_image(raw):
    if len(raw) < 156 or raw[16:20] != b'EPOC':
        raise ValueError('Expected E32 image')
    if struct.unpack_from('<I', raw, 100)[0] != 156 or struct.unpack_from('<I', raw, 28)[0] != 0x102822aa:
        raise ValueError('Expected bytepair V image')
    expected = struct.unpack_from('<I', raw, 124)[0]
    if expected > 32 * 1024 * 1024:
        raise ValueError('Oversized bytepair image')
    result = bytearray(raw[:156])
    cursor = 156
    for _ in range(2):
        size, length, pages = struct.unpack_from('<IIH', raw, cursor)
        start = cursor
        cursor += 10
        sizes = struct.unpack_from('<' + 'H' * pages, raw, cursor)
        cursor += 2 * pages
        decoded = bytearray()
        for page_size in sizes:
            page = raw[cursor:cursor + page_size]
            if len(page) != page_size or not page:
                raise ValueError('Truncated bytepair page')
            cursor += page_size
            pairs = {}
            count, position, marker = page[0], 1, None
            if count:
                marker = page[position]; position += 1
                if count < 32:
                    for _ in range(count):
                        token, left, right = page[position:position + 3]
                        pairs[token] = (left, right); position += 3
                else:
                    bitmap = page[position:position + 32]; position += 32
                    for token in range(256):
                        if bitmap[token >> 3] & (1 << (token & 7)):
                            pairs[token] = tuple(page[position:position + 2]); position += 2
                    if len(pairs) != count:
                        raise ValueError('Bytepair token count mismatch')
            output = bytearray()
            while position < len(page):
                token = page[position]; position += 1
                if marker is not None and token == marker:
                    output.append(page[position]); position += 1
                    continue
                stack = [token]
                expansions = 0
                while stack:
                    token = stack.pop()
                    if token in pairs:
                        left, right = pairs[token]; stack.extend((right, left))
                    else:
                        output.append(token)
                    expansions += 1
                    if expansions > 65536 or len(output) > 4096:
                        raise ValueError('Invalid bytepair expansion')
            if len(output) > 4096:
                raise ValueError('Oversized bytepair page')
            decoded.extend(output)
        if len(decoded) != length or cursor - start != size or len(decoded) > expected:
            raise ValueError('Bytepair section size mismatch')
        result.extend(decoded)
    if cursor != len(raw) or len(result) != 156 + expected:
        raise ValueError('Bytepair image size mismatch')
    struct.pack_into('<I', result, 28, 0)
    struct.pack_into('<I', result, 20, header_crc(result, 156))
    return bytes(result)


def redirect_audio_image(raw):
    if hashlib.sha256(raw).hexdigest() != AUDIO_SHA256:
        raise ValueError('Unexpected NGI audio input')
    image = bytepair_image(raw)
    code_offset, import_offset = struct.unpack_from('<I', image, 100)[0], struct.unpack_from('<I', image, 108)[0]
    code_size = struct.unpack_from('<I', image, 48)[0]
    if import_offset < code_offset + code_size or image.count(ORIGINAL_IMPORT) != 1:
        raise ValueError('Unexpected audio import layout')
    result = image[:import_offset] + image[import_offset:].replace(ORIGINAL_IMPORT, COMPAT_IMPORT)
    if len(result) != len(image) or result[code_offset:code_offset + code_size] != image[code_offset:code_offset + code_size]:
        raise ValueError('Audio redirection changed executable code')
    return result


def adapt_audio(package):
    units, original, updated, payload_count = data_units(package), None, None, 0
    for ui, unit in enumerate(units):
        files = array_items(fields(unit.payload)[0])
        for fi, file in enumerate(files):
            raw = decompress(fields(file.payload)[0])
            if hashlib.sha256(raw).hexdigest() != AUDIO_SHA256:
                continue
            original, updated = raw, redirect_audio_image(raw)
            files[fi] = Field(32, Field(3, struct.pack('<IQ', 0, len(updated)) + updated).encode())
            payload_count += 1
        units[ui] = Field(31, array(32, files).encode())
    if payload_count != 2:
        raise ValueError('Expected both pinned NGI audio payloads')
    description_count = 0
    def rewrite(field):
        nonlocal description_count
        if field.kind == 24:
            meta = fields(field.payload[:-28])
            if not meta[0].payload.decode('utf-16le').lower().endswith('\\ngiaudio.dll'):
                return field
            old_hash = next(f for f in meta if f.kind == 25)
            if fields(old_hash.payload[4:])[0].payload != hashlib.sha1(original).digest():
                raise ValueError('Unexpected audio file description')
            meta = [Field(25, struct.pack('<I', 1) + Field(37, hashlib.sha1(updated).digest()).encode()) if f.kind == 25 else f for f in meta]
            operation, options, _, _, index = struct.unpack('<IIQQI', field.payload[-28:])
            description_count += 1
            return Field(24, sequence(meta) + struct.pack('<IIQQI', operation, options, len(updated), len(updated), index))
        if field.kind == 2:
            return array(struct.unpack_from('<I', field.payload)[0], [rewrite(f) for f in array_items(field)])
        if field.kind in (13, 26, 27, 28):
            return Field(field.kind, sequence(rewrite(f) for f in fields(field.payload)))
        return field
    controller = rewrite(package.controller)
    if description_count != payload_count:
        raise ValueError('Audio descriptions do not match payloads')
    result = Package(package.uid_header, controller, Field(30, array(31, units).encode()))
    Package.parse(result.encode())
    return result
