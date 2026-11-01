"""Create a single-file unsigned component using the SDK's package structure."""
import binascii
import hashlib
import struct
from sis import Field, Package, array, fields, sequence
from merge_packages import replace_block


def single_file(template, raw, *, package_uid, name, target):
    parts = fields(template.controller.payload)
    info_index = next(i for i, f in enumerate(parts) if f.kind == 14)
    info = fields(parts[info_index].payload[:-2])
    info[0] = Field(9, struct.pack('<I', package_uid))
    info[2] = array(1, [Field(1, name.encode('utf-16le'))])
    parts[info_index] = Field(14, sequence(info) + parts[info_index].payload[-2:])
    header = struct.pack('<III', 0x10201a7a, 0, package_uid)
    crc = (binascii.crc_hqx(header[1:12:2], 0) << 16) | binascii.crc_hqx(header[:12:2], 0)
    package = Package(header + struct.pack('<I', crc), Field(13, sequence(parts)), template.data)
    metadata = [Field(1, target.encode('utf-16le')), Field(1, b'')]
    if raw[16:20] == b'EPOC' and len(raw) >= 156 and any(raw[136:144]):
        metadata.append(Field(41, raw[136:144]))
    metadata.append(Field(25, struct.pack('<I', 1)
                          + Field(37, hashlib.sha1(raw).digest()).encode()))
    description = Field(24, sequence(metadata) + struct.pack('<IIQQI', 1, 0, len(raw), len(raw), 0))
    block = Field(28, sequence([array(24, [description]), array(13, []), array(26, [])]))
    package = replace_block(package, lambda old: block)
    stream = Field(3, struct.pack('<IQ', 0, len(raw)) + raw)
    data = Field(30, array(31, [Field(31, array(32, [Field(32, stream.encode())]).encode())]).encode())
    result = Package(package.uid_header, package.controller, data)
    if Package.parse(result.encode()) != result:
        raise ValueError('Component package did not round-trip')
    return result
