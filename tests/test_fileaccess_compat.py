"""Check the pinned NGI file access seek change."""
import hashlib
import os
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from adapt_client import adapt_client
from audio_compat import bytepair_image
from component_identity import header_crc
from extract_client import extract_client
from fileaccess_compat import FILEACCESS_SHA256, SEEK, adapt_fileaccess, patch_fileaccess
from sis import Package
from test_orientation_compat import payloads


class FileAccessCompatibilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(os.environ.get('NGAGE_VALIDATION_INPUTS', ROOT / 'downloads')) / 'n97-ngage.sisx'
        if not path.exists():
            raise unittest.SkipTest('Fetch the pinned installer before file access checks')
        cls.package = Package.parse(adapt_client(extract_client(Package.parse(path.read_bytes())))[0])
        cls.raw = next(raw for raw in payloads(cls.package)
                       if hashlib.sha256(raw).hexdigest() == FILEACCESS_SHA256)

    def test_only_the_pinned_instructions_change(self):
        plain, expected = bytepair_image(self.raw), bytearray(bytepair_image(self.raw))
        for address, before, after in (SEEK,):
            self.assertEqual(len(before), len(after))
            at = struct.unpack_from('<I', plain, 100)[0] + address - 0x8000
            self.assertEqual(expected[at:at + len(before)], before)
            expected[at:at + len(before)] = after
        struct.pack_into('<I', expected, 20, header_crc(expected, 156))
        self.assertEqual(patch_fileaccess(self.raw), bytes(expected))
        with self.assertRaises(ValueError):
            patch_fileaccess(self.raw[:-1])

    def test_package_replaces_only_the_file_access_library(self):
        result = adapt_fileaccess(self.package)
        changed = 0
        for old, new in zip(payloads(self.package), payloads(result), strict=True):
            if hashlib.sha256(old).hexdigest() == FILEACCESS_SHA256:
                self.assertEqual(new, patch_fileaccess(old))
                changed += 1
            else:
                self.assertEqual(old, new)
        self.assertEqual(changed, 1)


if __name__ == '__main__':
    unittest.main()
