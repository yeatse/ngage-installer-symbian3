"""Verify the pinned service adaptation and its compressed-image boundary."""
import hashlib
import os
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from component_identity import header_crc
from e32_inflate import unpack
from installserver_compat import adapt_server, INPUT_SHA256


class ServiceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(os.environ.get('NGAGE_INSTALL_SERVICE',
                                   ROOT / 'downloads/ynstallserver-belle-fixture.exe'))
        if not path.exists():
            raise unittest.SkipTest('Fetch validation/hacked-x7.json before service checks')
        cls.raw = path.read_bytes()
        if hashlib.sha256(cls.raw).hexdigest() != INPUT_SHA256:
            raise ValueError('Unexpected service fixture')

    def test_decompression_matches_independent_cpp_decoder(self):
        self.assertEqual(hashlib.sha256(unpack(self.raw)).hexdigest(),
                         '92d89baab4512c8935ed349ed406658d3cb8d14ba309fc795dadec56e6d58860')

    def test_reject_truncated_compressed_stream(self):
        with self.assertRaises(ValueError):
            unpack(self.raw[:-1024])

    def test_preserve_service_identity_capabilities_and_original_code(self):
        original = unpack(self.raw)
        adapted = adapt_server(self.raw)
        self.assertEqual(adapted[:16], original[:16])
        self.assertEqual(adapted[128:144], original[128:144])
        call = 156 + 0x149ba
        code_end = 156 + 0x1c548
        self.assertEqual(adapted[156:call], original[156:call])
        self.assertEqual(adapted[call + 4:code_end], original[call + 4:code_end])
        self.assertEqual(adapted[code_end + 16:], original[code_end:])
        self.assertEqual(len(adapted), len(original) + 16)
        self.assertEqual(struct.unpack_from('<I', adapted, 20)[0], header_crc(adapted, 156))
        self.assertEqual(struct.unpack_from('<I', adapted, 28)[0], 0)

    def test_reject_unknown_service(self):
        with self.assertRaises(ValueError):
            adapt_server(bytes(len(self.raw)))
