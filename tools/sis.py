"""Lossless SISX field handling for the supplied N97 installer.

Unknown fields remain opaque. Installation predicates are never evaluated here.
"""
from dataclasses import dataclass
import binascii
import struct
import zlib


@dataclass(frozen=True)
class Field:
    kind: int
    payload: bytes

    def encode(self, array_element=False):
        size = len(self.payload)
        if size >= 0x80000000:
            raise ValueError('Field exceeds supported output size')
        prefix = b'' if array_element else struct.pack('<I', self.kind)
        return prefix + struct.pack('<I', size) + self.payload + bytes(-size % 4)


def fields(data, element_kind=None):
    offset = 0
    result = []
    while offset < len(data):
        if len(data) - offset < (8 if element_kind is None else 4):
            raise ValueError('Truncated field header')
        if element_kind is None:
            kind = struct.unpack_from('<I', data, offset)[0]
            offset += 4
        else:
            kind = element_kind
        size = struct.unpack_from('<I', data, offset)[0]
        offset += 4
        if size & 0x80000000:
            if len(data) - offset < 4:
                raise ValueError('Truncated extended field length')
            size = (size & 0x7fffffff) | (struct.unpack_from('<I', data, offset)[0] << 31)
            offset += 4
        end = offset + size
        aligned_end = end + (-size % 4)
        if aligned_end > len(data):
            raise ValueError('Truncated field payload or padding')
        if any(data[end:aligned_end]):
            raise ValueError('Nonzero SIS field padding')
        result.append(Field(kind, data[offset:end]))
        offset = aligned_end
    return result


def sequence(items):
    return b''.join(item.encode() for item in items)


def array(kind, items):
    if any(item.kind != kind for item in items):
        raise ValueError('Mixed array element types')
    return Field(2, struct.pack('<I', kind) + b''.join(
        item.encode(array_element=True) for item in items))


def array_items(field):
    if field.kind != 2 or len(field.payload) < 4:
        raise ValueError('Expected an array')
    kind = struct.unpack_from('<I', field.payload)[0]
    return fields(field.payload[4:], element_kind=kind)


def compressed(data):
    return Field(3, struct.pack('<IQ', 1, len(data)) + zlib.compress(data, 9))


def decompress(field, limit=128 * 1024 * 1024):
    if field.kind != 3 or len(field.payload) < 12:
        raise ValueError('Expected a compressed field')
    mode, size = struct.unpack_from('<IQ', field.payload)
    if size > limit:
        raise ValueError('Uncompressed stream exceeds limit')
    if mode == 0:
        raw = field.payload[12:]
    elif mode == 1:
        decoder = zlib.decompressobj()
        raw = decoder.decompress(field.payload[12:], size + 1)
        if not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
            raise ValueError('Invalid compressed stream')
    else:
        raise ValueError(f'Unsupported SIS compression mode: {mode}')
    if len(raw) != size:
        raise ValueError('Uncompressed size mismatch')
    return raw


def checksum(field):
    return binascii.crc_hqx(field.encode(), 0)


@dataclass(frozen=True)
class Package:
    uid_header: bytes
    controller: Field
    data: Field

    @classmethod
    def parse(cls, data):
        if len(data) < 24 or data[:4] != struct.pack('<I', 0x10201a7a):
            raise ValueError('Expected a SISX package')
        uid_crc = (binascii.crc_hqx(data[1:12:2], 0) << 16) | binascii.crc_hqx(data[:12:2], 0)
        if struct.unpack_from('<I', data, 12)[0] != uid_crc:
            raise ValueError('SIS UID checksum mismatch')
        roots = fields(data[16:])
        if len(roots) != 1 or roots[0].kind != 12:
            raise ValueError('Expected SISContents')
        parts = fields(roots[0].payload)
        if [p.kind for p in parts] != [34, 35, 3, 30]:
            raise ValueError('Unsupported SISContents profile')
        for saved, actual in [(parts[0], parts[2]), (parts[1], parts[3])]:
            if saved.payload != struct.pack('<H', checksum(actual)):
                raise ValueError('SIS checksum mismatch')
        controllers = fields(decompress(parts[2]))
        if len(controllers) != 1 or controllers[0].kind != 13:
            raise ValueError('Expected SISController')
        return cls(data[:16], controllers[0], parts[3])

    def encode(self):
        controller = compressed(self.controller.encode())
        parts = [Field(34, struct.pack('<H', checksum(controller))),
                 Field(35, struct.pack('<H', checksum(self.data))),
                 controller, self.data]
        return self.uid_header + Field(12, sequence(parts)).encode()
