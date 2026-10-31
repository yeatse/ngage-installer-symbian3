"""Integration checks against the hash-pinned upstream installer."""
import hashlib
import json
import os
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from extract_client import extract_client
from sis import Package


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


if __name__ == '__main__':
    unittest.main()
