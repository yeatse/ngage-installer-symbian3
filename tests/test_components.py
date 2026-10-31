"""Check E32 identity changes without requiring the cross compiler."""
import binascii
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from component_identity import header_crc, patch_image
from merge_packages import guard_missing_files, merge_embedded
from sis import Field, Package, array, array_items, fields, sequence


def package(uid):
    block = Field(28, sequence([array(24, []), array(13, []), array(26, [])]))
    header = struct.pack('<III', 0x10201a7a, 0, uid)
    crc = (binascii.crc_hqx(header[1:12:2], 0) << 16) | binascii.crc_hqx(header[:12:2], 0)
    return Package(header + struct.pack('<I', crc), Field(13, block.encode() + Field(40, bytes(4)).encode()),
                   Field(30, array(31, [Field(31, array(32, []).encode())]).encode()))


class ComponentTests(unittest.TestCase):
    def test_embedded_controllers_address_their_own_data_units(self):
        combined = merge_embedded(package(1), [package(2), package(3)])
        block = fields(combined.controller.payload)[0]
        children = array_items(fields(block.payload)[1])
        indices = [struct.unpack('<I', next(f for f in fields(c.payload) if f.kind == 40).payload)[0]
                   for c in children]
        self.assertEqual(indices, [1, 2])
        self.assertEqual(len(array_items(fields(combined.data.payload)[0])), 3)
        self.assertEqual(Package.parse(combined.encode()), combined)

    def image(self):
        raw = bytearray(156)
        struct.pack_into('<III', raw, 0, 0x1000007a, 0, 0xe0000960)
        raw[16:20] = b'EPOC'
        struct.pack_into('<I', raw, 100, 156)
        struct.pack_into('<I', raw, 20, header_crc(raw, 156))
        return bytes(raw) + b'executable code must stay identical'

    def test_capabilities_identity_and_checksums(self):
        original = self.image()
        result = patch_image(original, 0xe0000960, capabilities=6144)
        self.assertEqual(result[156:], original[156:])
        self.assertEqual(struct.unpack_from('<Q', result, 136)[0], 6144)
        self.assertEqual(struct.unpack_from('<I', result, 128)[0], 0xe0000960)
        self.assertEqual(struct.unpack_from('<I', result, 20)[0], header_crc(result, 156))
        uid_crc = (binascii.crc_hqx(result[1:12:2], 0) << 16) | binascii.crc_hqx(result[:12:2], 0)
        self.assertEqual(struct.unpack_from('<I', result, 12)[0], uid_crc)

    def test_reject_wrong_identity_or_corrupt_header(self):
        with self.assertRaises(ValueError):
            patch_image(self.image(), 0xe0000972)
        raw = bytearray(self.image())
        raw[80] ^= 1
        with self.assertRaises(ValueError):
            patch_image(raw, 0xe0000960)

    def test_guard_requires_every_alias_to_be_missing(self):
        guarded = guard_missing_files(package(1), [r'C:\sys\bin\YnstallServer.exe',
                                                   r'Z:\sys\bin\YnstallServer.exe'])
        block = fields(guarded.controller.payload)[0]
        branch = array_items(fields(block.payload)[2])[0]
        expression = fields(branch.payload)[0]
        self.assertEqual(struct.unpack_from('<I', expression.payload)[0], 7)  # AND
        for missing in fields(expression.payload[8:]):
            self.assertEqual(struct.unpack_from('<I', missing.payload)[0], 9)  # NOT

