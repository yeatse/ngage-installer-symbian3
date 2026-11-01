"""Integration checks against the hash-pinned upstream installer."""
import hashlib
import json
import os
from pathlib import Path
import sys
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from extract_client import extract_client
from extract_client import descendants
from adapt_client import (adapt_client, SYMBIAN3_MARKER, SYMBIAN3_PRODUCT_UID,
                          BELLE_MARKER, BELLE_PRODUCT_UID)
from sis import Package, fields, array_items


class OriginalInstallerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        directory = Path(os.environ.get('NGAGE_VALIDATION_INPUTS', ROOT / 'downloads'))
        path = directory / 'n97-ngage.sisx'
        if not path.exists():
            raise unittest.SkipTest('Run tools/fetch_inputs.py before integration tests')
        cls.original = path.read_bytes()
        entry = next(i for i in json.loads((ROOT / 'validation/inputs.json').read_text())
                     if i['filename'] == path.name)
        if hashlib.sha256(cls.original).hexdigest() != entry['sha256']:
            raise ValueError('Integration fixture does not match pinned upstream input')

    def test_embedded_client_identity(self):
        client = extract_client(Package.parse(self.original))
        self.assertEqual(hashlib.sha256(client).hexdigest(),
                         '321253b58a70e67d2de3b381e806dfa48b04e99e637ebc4730d6c42aa6306367')
        Package.parse(client)

    def test_repacking_preserves_signed_controller_and_data(self):
        outer = Package.parse(self.original)
        client = Package.parse(extract_client(outer))
        for package in (outer, client):
            with self.subTest(uid=package.uid_header.hex()):
                rebuilt = Package.parse(package.encode())
                self.assertEqual(rebuilt.controller, package.controller)
                self.assertEqual(rebuilt.data, package.data)
                self.assertEqual(rebuilt.uid_header, package.uid_header)

    def test_corruption_is_rejected(self):
        for offset in (8, len(self.original) - 5):
            with self.subTest(offset=offset):
                corrupted = bytearray(self.original)
                corrupted[offset] ^= 1
                with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                    Package.parse(bytes(corrupted))

    def test_truncation_is_rejected(self):
        for count in (1, 4, 32, len(self.original) - 16):
            with self.subTest(count=count):
                with self.assertRaises(ValueError):
                    Package.parse(self.original[:-count])

    def test_symbian3_adaptation_preserves_binaries_and_removes_signatures(self):
        raw = extract_client(Package.parse(self.original))
        encoded, report = adapt_client(raw)
        original = Package.parse(raw)
        adapted = Package.parse(encoded)
        self.assertEqual(adapted.data, original.data)
        self.assertFalse(any(f.kind == 39 for f in descendants(adapted.controller)))
        self.assertEqual(report, {'touch_profile_conditions': 25, 'removed_signatures': 30,
                                  'device_prerequisites': 15})
        self.assertEqual(adapted.controller.payload.count(SYMBIAN3_MARKER.encode('utf-16le')), 25)
        self.assertEqual(adapted.controller.payload.count(BELLE_MARKER.encode('utf-16le')), 25)
        pre = next(f for f in fields(adapted.controller.payload) if f.kind == 17)
        device = array_items(fields(pre.payload)[0])[0]
        uid = fields(device.payload)[0]
        self.assertEqual(struct.unpack('<I', uid.payload)[0], SYMBIAN3_PRODUCT_UID)
        self.assertEqual(adapt_client(raw)[0], encoded)

    def test_every_embedded_product_declaration_supports_belle(self):
        original = Package.parse(extract_client(Package.parse(self.original)))
        adapted = Package.parse(adapt_client(extract_client(Package.parse(self.original)))[0])
        before = [f for f in descendants(original.controller) if f.kind == 17]
        after = [f for f in descendants(adapted.controller) if f.kind == 17]
        self.assertEqual(len(before), len(after))
        for old, new in zip(before, after):
            old_arrays, new_arrays = fields(old.payload), fields(new.payload)
            self.assertEqual(old_arrays[1], new_arrays[1])
            devices = array_items(new_arrays[0])
            uids = [struct.unpack('<I', fields(d.payload)[0].payload)[0] for d in devices]
            self.assertEqual(uids, [SYMBIAN3_PRODUCT_UID, BELLE_PRODUCT_UID])

    def test_adaptation_rejects_an_unrecognized_source(self):
        with self.assertRaisesRegex(ValueError, 'pinned N97'):
            adapt_client(self.original)


if __name__ == '__main__':
    unittest.main()
