"""Check E32 identity changes without requiring the cross compiler."""
import binascii
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from component_identity import header_crc, patch_image
from adapt_client import target_component, SYMBIAN3_PRODUCT_UID, BELLE_PRODUCT_UID
from merge_packages import merge_files
from native_package import package_files, single_file
from sis import Field, Package, array, array_items, fields, sequence


def package(uid):
    block = Field(28, sequence([array(24, []), array(13, []), array(26, [])]))
    header = struct.pack('<III', 0x10201a7a, 0, uid)
    crc = (binascii.crc_hqx(header[1:12:2], 0) << 16) | binascii.crc_hqx(header[:12:2], 0)
    return Package(header + struct.pack('<I', crc), Field(13, block.encode() + Field(40, bytes(4)).encode()),
                   Field(30, array(31, [Field(31, array(32, []).encode())]).encode()))


class ComponentTests(unittest.TestCase):
    def test_background_service_has_only_an_executable_without_install_autostart(self):
        base = package(1)
        info = Field(14, sequence([Field(9, struct.pack('<I', 1)), Field(1, b''),
                                  array(1, [Field(1, b'')])]) + bytes(2))
        template = Package(base.uid_header, Field(13, info.encode() + base.controller.payload), base.data)
        result = single_file(template, self.image(), package_uid=0xe0000961,
                             name='Background service', target=r'C:\sys\bin\ngage_touch.exe')
        block = next(f for f in fields(result.controller.payload) if f.kind == 28)
        descriptions = array_items(fields(block.payload)[0])
        self.assertEqual(len(descriptions), 1)
        description = descriptions[0]
        self.assertEqual(fields(description.payload[:-28])[0].payload.decode('utf-16le'),
                         r'C:\sys\bin\ngage_touch.exe')
        self.assertEqual(struct.unpack_from('<II', description.payload, len(description.payload)-28),
                         (1, 0))
        self.assertEqual(Package.parse(result.encode()), result)

    def test_multi_file_component_indexes_each_file_in_its_data_unit(self):
        base = package(1)
        info = Field(14, sequence([Field(9, struct.pack('<I', 1)), Field(1, b''),
                                  array(1, [Field(1, b'')])]) + bytes(2))
        template = Package(base.uid_header, Field(13, info.encode() + base.controller.payload), base.data)
        files = [(r'C:\sys\bin\ngdatrecognizer.dll', self.image()),
                 (r'C:\resource\plugins\ngdatrecognizer.rsc', b'resource')]
        result = package_files(template, files, package_uid=0xe0000967, name='Recognizer')
        block = next(f for f in fields(result.controller.payload) if f.kind == 28)
        descriptions = array_items(fields(block.payload)[0])
        targets = [fields(d.payload[:-28])[0].payload.decode('utf-16le') for d in descriptions]
        indices = [struct.unpack_from('<I', d.payload, len(d.payload) - 4)[0] for d in descriptions]
        self.assertEqual(targets, [target for target, _ in files])
        self.assertEqual(indices, [0, 1])
        unit = array_items(fields(result.data.payload)[0])[0]
        streams = array_items(fields(unit.payload)[0])
        contents = [fields(s.payload)[0].payload[12:] for s in streams]
        self.assertEqual(contents, [raw for _, raw in files])
        self.assertEqual(Package.parse(result.encode()), result)

    def test_sdk_components_do_not_leave_the_target_device_list_empty(self):
        original = package(1)
        prerequisites = Field(17, sequence([array(18, []), array(18, [])]))
        original = Package(original.uid_header,
                           Field(13, prerequisites.encode() + original.controller.payload), original.data)
        updated = target_component(original)
        target = next(f for f in fields(updated.controller.payload) if f.kind == 17)
        uids = [struct.unpack('<I', fields(d.payload)[0].payload)[0]
                for d in array_items(fields(target.payload)[0])]
        self.assertEqual(uids, [SYMBIAN3_PRODUCT_UID, BELLE_PRODUCT_UID])
        self.assertEqual(updated.data, original.data)

    def test_component_files_join_the_parent_package(self):
        base = package(1)
        info = Field(14, sequence([Field(9, struct.pack('<I', 1)), Field(1, b''),
                                  array(1, [Field(1, b'')])]) + bytes(2))
        template = Package(base.uid_header, Field(13, info.encode() + base.controller.payload), base.data)
        parent = package_files(template, [(r'C:\parent.txt', b'parent')], package_uid=1, name='Parent')
        first = single_file(template, b'first', package_uid=2, name='First', target=r'C:\first.txt')
        second = package_files(template, [(r'C:\a.txt', b'a'), (r'C:\b.txt', b'b')],
                               package_uid=3, name='Second')
        combined = merge_files(parent, [first, second])
        block = next(f for f in fields(combined.controller.payload) if f.kind == 28)
        files, embedded, _ = fields(block.payload)
        self.assertEqual(array_items(embedded), [])
        descriptions = array_items(files)
        targets = [fields(d.payload[:-28])[0].payload.decode('utf-16le') for d in descriptions]
        indices = [struct.unpack_from('<I', d.payload, len(d.payload) - 4)[0] for d in descriptions]
        self.assertEqual(targets, [r'C:\parent.txt', r'C:\first.txt', r'C:\a.txt', r'C:\b.txt'])
        self.assertEqual(indices, [0, 1, 2, 3])
        units = array_items(fields(combined.data.payload)[0])
        self.assertEqual(len(units), 1)
        streams = array_items(fields(units[0].payload)[0])
        self.assertEqual([fields(s.payload)[0].payload[12:] for s in streams],
                         [b'parent', b'first', b'a', b'b'])
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

    def test_library_identity_retains_the_dll_type(self):
        raw = bytearray(self.image())
        struct.pack_into('<II', raw, 0, 0x10000079, 0x1000008d)
        struct.pack_into('<I', raw, 20, header_crc(raw, 156))
        result = patch_image(raw, 0xe0000960, uid2=0x1000008d,
                             expected_uid1=0x10000079, expected_uid2=0x1000008d,
                             capabilities=0xfffff)
        self.assertEqual(struct.unpack_from('<II', result), (0x10000079, 0x1000008d))
        self.assertEqual(result[156:], raw[156:])

