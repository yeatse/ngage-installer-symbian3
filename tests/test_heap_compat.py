"""Check that the NGI runtime is routed to the legacy heap DLL without code changes."""
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
from extract_client import extract_client
from heap_compat import (COMPAT_IMPORT, ORIGINAL_IMPORT, RUNTIME_SHA256, adapt_heap,
                         redirect_runtime_image)
from sis import Package
from test_orientation_compat import payloads


class HeapCompatibilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(os.environ.get('NGAGE_VALIDATION_INPUTS', ROOT / 'downloads')) / 'n97-ngage.sisx'
        if not path.exists():
            raise unittest.SkipTest('Fetch the pinned installer before heap checks')
        cls.package = Package.parse(adapt_client(extract_client(Package.parse(path.read_bytes())))[0])
        cls.raw = next(raw for raw in payloads(cls.package)
                       if hashlib.sha256(raw).hexdigest() == RUNTIME_SHA256)

    def test_only_the_import_name_changes(self):
        plain, routed = bytepair_image(self.raw), redirect_runtime_image(self.raw)
        self.assertEqual(len(COMPAT_IMPORT), len(ORIGINAL_IMPORT))
        self.assertEqual(routed, plain.replace(ORIGINAL_IMPORT, COMPAT_IMPORT))
        offset, size = struct.unpack_from('<I', plain, 100)[0], struct.unpack_from('<I', plain, 48)[0]
        self.assertEqual(routed[offset:offset + size], plain[offset:offset + size])
        with self.assertRaises(ValueError):
            redirect_runtime_image(self.raw[:-1])

    def test_package_replaces_both_runtime_copies(self):
        result = adapt_heap(self.package)
        changed = 0
        for old, new in zip(payloads(self.package), payloads(result), strict=True):
            if hashlib.sha256(old).hexdigest() == RUNTIME_SHA256:
                self.assertEqual(new, redirect_runtime_image(old))
                changed += 1
            else:
                self.assertEqual(old, new)
        self.assertEqual(changed, 2)


if __name__ == '__main__':
    unittest.main()
