"""Check the missing-rights licence status adaptation of the pinned client."""
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
from licence_compat import (GAMETITLE_SHA256, CODE_BASE, NO_PERMISSION_BRANCH,
                            FULL_LICENCE_BRANCH, FULL_LICENCE_STORE, adapt_licence, patch_gametitle)
from merge_packages import data_units
from sis import Package, fields, array_items, decompress


def branch_target(site, halfword):
    return site + 4 + 2 * struct.unpack('b', bytes([halfword & 0xff]))[0]


class LicenceCompatibilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(os.environ.get('NGAGE_VALIDATION_INPUTS', ROOT / 'downloads')) / 'n97-ngage.sisx'
        if not path.exists():
            raise unittest.SkipTest('Fetch the pinned installer before licence checks')
        cls.package = Package.parse(adapt_client(extract_client(Package.parse(path.read_bytes())))[0])
        cls.raw = next(decompress(fields(f.payload)[0])
                       for unit in data_units(cls.package)
                       for f in array_items(fields(unit.payload)[0])
                       if hashlib.sha256(decompress(fields(f.payload)[0])).hexdigest() == GAMETITLE_SHA256)

    def test_only_the_no_permission_branch_changes(self):
        plain, patched = bytepair_image(self.raw), patch_gametitle(self.raw)
        site = struct.unpack_from('<I', plain, 100)[0] + NO_PERMISSION_BRANCH - CODE_BASE
        self.assertEqual(len(patched), len(plain))
        self.assertEqual([i for i in range(len(plain)) if plain[i] != patched[i]], [site])
        self.assertEqual(struct.unpack_from('<H', patched, site)[0], FULL_LICENCE_BRANCH)
        self.assertEqual(branch_target(NO_PERMISSION_BRANCH, FULL_LICENCE_BRANCH), FULL_LICENCE_STORE)
        self.assertEqual(struct.unpack_from('<I', patched, 20)[0], header_crc(patched, 156))

    def test_unknown_input_is_rejected(self):
        with self.assertRaises(ValueError):
            patch_gametitle(self.raw[:-1])

    def test_payload_and_all_descriptions_are_updated(self):
        result = adapt_licence(self.package)
        patched = patch_gametitle(self.raw)
        payloads = [decompress(fields(f.payload)[0]) for unit in data_units(result)
                    for f in array_items(fields(unit.payload)[0])]
        self.assertEqual(payloads.count(patched), 1)
        self.assertNotIn(self.raw, payloads)
        digests = []
        def walk(field):
            if field.kind == 24:
                meta = fields(field.payload[:-28])
                if meta[0].payload.decode('utf-16le').lower().endswith('\\gametitlemetadata.dll'):
                    digests.append(fields(next(f for f in meta if f.kind == 25).payload[4:])[0].payload)
            elif field.kind == 2:
                for child in array_items(field):
                    walk(child)
            elif field.kind in (13, 26, 27, 28):
                for child in fields(field.payload):
                    walk(child)
        walk(result.controller)
        self.assertEqual(digests, [hashlib.sha1(patched).digest()] * 3)


if __name__ == '__main__':
    unittest.main()
