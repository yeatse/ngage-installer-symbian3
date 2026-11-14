"""Create a single-file unsigned component using the SDK's package structure."""
import binascii
import hashlib
import struct
from sis import Field, Package, array, fields, sequence
from merge_packages import replace_block


# The phone skips an embedded package whose version is already installed, so every change to a
# component needs a higher build number than the last one that reached a phone.
REVISION = 2001


def single_file(template, raw, *, package_uid, name, target, run_after_install=False):
    return package_files(template, [(target, raw)], package_uid=package_uid, name=name,
                         run_after_install=run_after_install)


def package_files(template, files, *, package_uid, name, run_after_install=False):
    parts = fields(template.controller.payload)
    info_index = next(i for i, f in enumerate(parts) if f.kind == 14)
    info = fields(parts[info_index].payload[:-2])
    info[0] = Field(9, struct.pack('<I', package_uid))
    info[2] = array(1, [Field(1, name.encode('utf-16le'))])
    info[4] = Field(4, info[4].payload[:8] + struct.pack('<I', REVISION))
    parts[info_index] = Field(14, sequence(info) + parts[info_index].payload[-2:])
    header = struct.pack('<III', 0x10201a7a, 0, package_uid)
    crc = (binascii.crc_hqx(header[1:12:2], 0) << 16) | binascii.crc_hqx(header[:12:2], 0)
    package = Package(header + struct.pack('<I', crc), Field(13, sequence(parts)), template.data)
    descriptions = []
    streams = []
    for index, (target, raw) in enumerate(files):
        metadata = [Field(1, target.encode('utf-16le')), Field(1, b'')]
        if raw[16:20] == b'EPOC' and len(raw) >= 156 and any(raw[136:144]):
            metadata.append(Field(41, raw[136:144]))
        metadata.append(Field(25, struct.pack('<I', 1)
                              + Field(37, hashlib.sha1(raw).digest()).encode()))
        operation, options = (2, 1 << 1) if run_after_install else (1, 0)
        descriptions.append(Field(24, sequence(metadata) + struct.pack(
            '<IIQQI', operation, options, len(raw), len(raw), index)))
        stream = Field(3, struct.pack('<IQ', 0, len(raw)) + raw)
        streams.append(Field(32, stream.encode()))
    block = Field(28, sequence([array(24, descriptions), array(13, []), array(26, [])]))
    package = replace_block(package, lambda old: block)
    data = Field(30, array(31, [Field(31, array(32, streams).encode())]).encode())
    result = Package(package.uid_header, package.controller, data)
    if Package.parse(result.encode()) != result:
        raise ValueError('Component package did not round-trip')
    return result
