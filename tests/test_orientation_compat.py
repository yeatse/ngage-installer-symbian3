"""Check portrait presentation settings and the two pinned NGI instruction changes."""
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
from merge_packages import data_units
from orientation_compat import (BACKBUFFER_SHA256, CONFIG_SHA256, DIMENSION_SWAP, LANDSCAPE,
                                ORIENTATION_SHA256, adapt_orientation, build_cre, parse_cre,
                                patch_code, patch_config, sections)
from sis import Package, array_items, decompress, fields


def payloads(package):
    for unit in data_units(package):
        for item in array_items(fields(unit.payload)[0]):
            yield decompress(fields(item.payload)[0])


class OrientationCompatibilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(os.environ.get('NGAGE_VALIDATION_INPUTS', ROOT / 'downloads')) / 'n97-ngage.sisx'
        if not path.exists():
            raise unittest.SkipTest('Fetch the pinned installer before orientation checks')
        cls.package = Package.parse(adapt_client(extract_client(Package.parse(path.read_bytes())))[0])
        cls.raw = {}
        for raw in payloads(cls.package):
            cls.raw.setdefault(hashlib.sha256(raw).hexdigest(), raw)

    def test_every_display_configuration_round_trips(self):
        stores = [raw for raw in self.raw.values() if raw[:4] == struct.pack('<I', 0x10000037)
                  and struct.unpack_from('<I', raw, 21)[0] == 0x20001079]
        self.assertGreater(len(stores), 20)
        for raw in stores:
            self.assertEqual(build_cre(*parse_cre(raw)), raw)

    def test_portrait_backbuffers_fill_height_and_are_centred(self):
        original = sections(parse_cre(self.raw[CONFIG_SHA256])[1])
        patched = sections(parse_cre(patch_config(self.raw[CONFIG_SHA256]))[1])
        self.assertEqual(patched.keys(), original.keys())
        display = patched['/hardware/graphics/displays/0']
        self.assertEqual((display['nativewidth'][3], display['nativeheight'][3]), (240, 320))
        options = patched['/hardware/graphics/displays/0/ngageorientations']
        self.assertEqual(options['useroptions'][3], 3)
        self.assertEqual(options['wsinimodes'][3], '0x01, 0x02')
        game = patched['/hardware/graphics/backbuffer/configurations/0']
        self.assertEqual((game['width'][3], game['height'][3]), (240, 320))
        self.assertEqual(game['scalingsize'][3], '0x10E, 0x168')
        self.assertEqual(game['windowposition'][3], '0xB9, 0x0')
        for path, values in patched.items():
            if path.startswith('/hardware/graphics/backbuffer/configurations/') \
                    and values['orientationtype'][3] == 1:
                self.assertEqual(values, original[path])

    def test_only_the_pinned_instructions_change(self):
        for sha, (address, before, after) in ((ORIENTATION_SHA256, LANDSCAPE),
                                              (BACKBUFFER_SHA256, DIMENSION_SWAP)):
            patch = (address, before, after)
            plain, patched = bytepair_image(self.raw[sha]), patch_code(self.raw[sha], sha, patch)
            at = struct.unpack_from('<I', plain, 100)[0] + address - 0x8000
            expected = bytearray(plain); expected[at:at + len(before)] = after
            struct.pack_into('<I', expected, 20, header_crc(expected, 156))
            self.assertEqual(patched, bytes(expected))
            with self.assertRaises(ValueError):
                patch_code(self.raw[sha][:-1], sha, patch)

    def test_package_replaces_only_the_targeted_payloads(self):
        result = adapt_orientation(self.package)
        expected = {CONFIG_SHA256: 2, ORIENTATION_SHA256: 1, BACKBUFFER_SHA256: 2}
        changed = {}
        for old, new in zip(payloads(self.package), payloads(result), strict=True):
            sha = hashlib.sha256(old).hexdigest()
            if sha in expected:
                self.assertNotEqual(old, new)
                changed[sha] = changed.get(sha, 0) + 1
            else:
                self.assertEqual(old, new)
        self.assertEqual(changed, expected)


if __name__ == '__main__':
    unittest.main()
