"""Apply the component identities required by the existing N-Gage and SWI APIs."""
import binascii
import hashlib
import struct

from sis import Field, Package, array, array_items, decompress, fields, sequence
from merge_packages import replace_block, data_units
def header_crc(raw, size):
    header = bytearray(raw[:size])
    struct.pack_into('<I', header, 20, 0xc90fdaa2)
    crc = 0
    for value in header:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0xedb88320 if crc & 1 else 0)
    return crc


def patch_image(raw, expected_uid, *, uid2=0, uid3=None, sid=None, capabilities=0):
    if len(raw) < 156 or raw[16:20] != b'EPOC':
        raise ValueError('Expected SDK E32 image')
    if struct.unpack_from('<III', raw) != (0x1000007a, 0, expected_uid):
        raise ValueError('Unexpected input UID type')
    size = struct.unpack_from('<I', raw, 100)[0]
    if size != 156 or struct.unpack_from('<I', raw, 28)[0] != 0:
        raise ValueError('Expected uncompressed SDK V header')
    if header_crc(raw, size) != struct.unpack_from('<I', raw, 20)[0]:
        raise ValueError('Input E32 header CRC mismatch')
    result = bytearray(raw)
    struct.pack_into('<II', result, 4, uid2, uid3 if uid3 is not None else expected_uid)
    uid_crc = (binascii.crc_hqx(result[1:12:2], 0) << 16) | binascii.crc_hqx(result[:12:2], 0)
    struct.pack_into('<I', result, 12, uid_crc)
    struct.pack_into('<I', result, 128, sid if sid is not None else expected_uid)
    struct.pack_into('<Q', result, 136, capabilities)
    struct.pack_into('<I', result, 20, header_crc(result, size))
    if result[size:] != raw[size:]:
        raise ValueError('Component identity update changed executable code')
    return bytes(result)


def replace_image(package, original, updated):
    if len(original) != len(updated):
        raise ValueError('Identity update changed image size')
    units = data_units(package)
    if len(units) != 1:
        raise ValueError('Expected a single SDK data unit')
    file_array = fields(units[0].payload)
    if len(file_array) != 1:
        raise ValueError('Unexpected SDK data unit')
    files = array_items(file_array[0])
    matches = [i for i, f in enumerate(files)
               if decompress(fields(f.payload)[0]) == original]
    if len(matches) != 1:
        raise ValueError('Expected exactly one matching executable payload')
    index = matches[0]
    files[index] = Field(32, Field(3, struct.pack('<IQ', 0, len(updated)) + updated).encode())
    capability = updated[136:144]
    changed = 0
    def fix_block(block):
        nonlocal changed
        parts = fields(block.payload)
        descriptions = array_items(parts[0])
        for i, description in enumerate(descriptions):
            if struct.unpack_from('<I', description.payload, len(description.payload) - 4)[0] != index:
                continue
            metadata = fields(description.payload[:-28])
            if not metadata[0].payload.decode('utf-16le').lower().endswith('.exe'):
                raise ValueError('Matched payload is not an executable')
            metadata = [f for f in metadata if f.kind != 41]
            hash_index = next(i for i, f in enumerate(metadata) if f.kind == 25)
            metadata[hash_index] = Field(25, struct.pack('<I', 1)
                                         + Field(37, hashlib.sha1(updated).digest()).encode())
            if any(capability):
                metadata.insert(hash_index, Field(41, capability))
            descriptions[i] = Field(24, sequence(metadata) + description.payload[-28:])
            changed += 1
        parts[0] = array(24, descriptions)
        return Field(28, sequence(parts))
    changed_package = replace_block(package, fix_block)
    if changed != 1:
        raise ValueError('Expected exactly one executable description')
    result = Package(package.uid_header, changed_package.controller,
                     Field(30, array(31, [Field(31, array(32, files).encode())]).encode()))
    Package.parse(result.encode())
    return result
