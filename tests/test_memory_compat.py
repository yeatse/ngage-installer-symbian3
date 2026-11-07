"""Check the pinned NGI memory management instruction changes."""
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
from memory_compat import COMMITTED, MEMORY_SHA256, OWNS, adapt_memory, patch_memory
from sis import Package
from test_orientation_compat import payloads


class MemoryCompatibilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(os.environ.get('NGAGE_VALIDATION_INPUTS', ROOT / 'downloads')) / 'n97-ngage.sisx'
        if not path.exists():
            raise unittest.SkipTest('Fetch the pinned installer before memory checks')
        cls.package = Package.parse(adapt_client(extract_client(Package.parse(path.read_bytes())))[0])
        cls.raw = next(raw for raw in payloads(cls.package)
                       if hashlib.sha256(raw).hexdigest() == MEMORY_SHA256)

    def test_only_the_pinned_instructions_change(self):
        plain, expected = bytepair_image(self.raw), bytearray(bytepair_image(self.raw))
        for address, before, after in (OWNS, COMMITTED):
            self.assertEqual(len(before), len(after))
            at = struct.unpack_from('<I', plain, 100)[0] + address - 0x8000
            self.assertEqual(expected[at:at + len(before)], before)
            expected[at:at + len(before)] = after
        struct.pack_into('<I', expected, 20, header_crc(expected, 156))
        self.assertEqual(patch_memory(self.raw), bytes(expected))
        with self.assertRaises(ValueError):
            patch_memory(self.raw[:-1])

    def test_package_replaces_only_the_memory_library(self):
        result = adapt_memory(self.package)
        changed = 0
        for old, new in zip(payloads(self.package), payloads(result), strict=True):
            if hashlib.sha256(old).hexdigest() == MEMORY_SHA256:
                self.assertEqual(new, patch_memory(old))
                changed += 1
            else:
                self.assertEqual(old, new)
        self.assertEqual(changed, 1)


if __name__ == '__main__':
    unittest.main()
