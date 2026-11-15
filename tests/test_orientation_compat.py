"""Check portrait presentation settings and the pinned NGI instruction changes."""
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
from orientation_compat import (BACKBUFFER_SHA256, CONFIG_SHA256, DIMENSION_SWAP, DISPLAY_ENTRY, WINDOW,
                                DISPLAY_SHA256, DISPLAY_STUB, FORMAT, ORIENTATION_ENTRY,
                                VISIBLE_ENTRY, VISIBLE_STUB,
                                ORIENTATION_IMPORTS, ORIENTATION_SHA256, ORIENTATION_STUB,
                                adapt_orientation, build_cre, compile_orientation, parse_cre,
                                patch_code, patch_config, patch_stub, sections)
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
        sdk = Path(os.environ.get('NGAGE_SDK', '/nonexistent'))
        if not (sdk / 'bin/clang').exists():
            raise unittest.SkipTest('NGAGE_SDK is needed to compile the orientation stubs')
        cls.stubs = compile_orientation(sdk, ROOT)
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
        added = {f'/hardware/graphics/displays/0/configurations/{index}': (240, 320, colorformat)
                 for index, colorformat in ((4, 65552), (5, 262176))}
        self.assertEqual(patched.keys(), original.keys() | added.keys())
        for path, (width, height, colorformat) in added.items():
            values = patched[path]
            self.assertEqual((values['width'][3], values['height'][3], values['colorformat'][3]),
                             (width, height, colorformat))
        configurations = patched['/hardware/graphics/displays/0/configurations']
        self.assertEqual(configurations['configurationscount'][3], 6)
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
        sha = BACKBUFFER_SHA256
        patches = (DIMENSION_SWAP, *WINDOW, *FORMAT)
        plain, patched = bytepair_image(self.raw[sha]), patch_code(self.raw[sha], sha, *patches)
        expected = bytearray(plain)
        for address, before, after in patches:
            at = struct.unpack_from('<I', plain, 100)[0] + address - 0x8000
            self.assertEqual(expected[at:at + len(before)], before)
            expected[at:at + len(before)] = after
        struct.pack_into('<I', expected, 20, header_crc(expected, 156))
        self.assertEqual(patched, bytes(expected))
        with self.assertRaises(ValueError):
            patch_code(self.raw[sha][:-1], sha, *patches)

    def test_orientation_stubs_replace_only_unreachable_code(self):
        notify, display, visible = self.stubs
        for sha, entry, region, stub, imports in (
                (ORIENTATION_SHA256, ORIENTATION_ENTRY, ORIENTATION_STUB, notify, ORIENTATION_IMPORTS),
                (DISPLAY_SHA256, DISPLAY_ENTRY, DISPLAY_STUB, display, ())):
            plain = bytepair_image(self.raw[sha])
            patched = patch_stub(self.raw[sha], sha, entry, region, stub, imports)
            code = struct.unpack_from('<I', plain, 100)[0] - 0x8000
            changed = {at + 0x8000 - struct.unpack_from('<I', plain, 100)[0]
                       for at in range(len(plain)) if plain[at] != patched[at]} - set(range(20, 24))
            allowed = set(range(entry[0], entry[0] + 2)) | set(range(region[0], region[1]))
            for address, _, _ in imports:
                allowed |= set(range(address, address + 4))
            self.assertLessEqual(changed, allowed)
            self.assertEqual(patched[code + region[0]:code + region[0] + len(stub)], stub)
            for address, old, new in imports:
                self.assertEqual(struct.unpack_from('<I', plain, code + address)[0], old)
                self.assertEqual(struct.unpack_from('<I', patched, code + address)[0], new)
            with self.assertRaises(ValueError):
                patch_stub(self.raw[sha], sha, entry, region, stub + bytes(region[1] - region[0]))

    def test_visible_stub_shares_the_display_library(self):
        _, display, visible = self.stubs
        raw = self.raw[DISPLAY_SHA256]
        plain = bytepair_image(raw)
        single = patch_stub(raw, DISPLAY_SHA256, DISPLAY_ENTRY, DISPLAY_STUB, display)
        both = patch_stub(raw, DISPLAY_SHA256, DISPLAY_ENTRY, DISPLAY_STUB, display,
                          more=((VISIBLE_ENTRY, VISIBLE_STUB, visible),))
        offset = struct.unpack_from('<I', plain, 100)[0] - 0x8000
        changed = {at - offset for at in range(len(single)) if single[at] != both[at]}
        self.assertLessEqual(changed, set(range(VISIBLE_ENTRY[0], VISIBLE_ENTRY[0] + 2))
                             | set(range(*VISIBLE_STUB)) | {at - offset for at in range(20, 24)})
        start = offset + VISIBLE_STUB[0]
        self.assertEqual(both[start:start + len(visible)], visible)
        self.assertLessEqual(len(visible), VISIBLE_STUB[1] - VISIBLE_STUB[0])
        with self.assertRaises(ValueError):
            patch_stub(raw, DISPLAY_SHA256, DISPLAY_ENTRY, DISPLAY_STUB, display,
                       more=((VISIBLE_ENTRY, VISIBLE_STUB, visible + bytes(4)),))

    def test_package_replaces_only_the_targeted_payloads(self):
        result = adapt_orientation(self.package, self.stubs)
        expected = {CONFIG_SHA256: 2, ORIENTATION_SHA256: 1, DISPLAY_SHA256: 2,
                    BACKBUFFER_SHA256: 2}
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
