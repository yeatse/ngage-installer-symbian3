"""Check diagnostic import routing without changing NGI executable instructions."""
import hashlib
import os
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from adapt_client import adapt_client
from audio_diagnostic import (AUDIO_SHA256, ORIGINAL_IMPORT, DIAGNOSTIC_IMPORT,
                              bytepair_image, redirect_audio_image, instrument_audio)
from component_identity import header_crc
from extract_client import extract_client
from merge_packages import data_units
from sis import Package, fields, array_items, decompress


class AudioDiagnosticTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(os.environ.get('NGAGE_VALIDATION_INPUTS', ROOT / 'downloads')) / 'n97-ngage.sisx'
        if not path.exists():
            raise unittest.SkipTest('Fetch the pinned installer before audio checks')
        cls.package = Package.parse(adapt_client(extract_client(Package.parse(path.read_bytes())))[0])
        cls.raw = next(decompress(fields(f.payload)[0])
                       for unit in data_units(cls.package)
                       for f in array_items(fields(unit.payload)[0])
                       if hashlib.sha256(decompress(fields(f.payload)[0])).hexdigest() == AUDIO_SHA256)

    def test_audio_instructions_are_unchanged(self):
        plain, routed = bytepair_image(self.raw), redirect_audio_image(self.raw)
        offset, size = struct.unpack_from('<I', plain, 100)[0], struct.unpack_from('<I', plain, 48)[0]
        self.assertEqual(routed[offset:offset + size], plain[offset:offset + size])
        self.assertEqual(len(routed), len(plain))
        self.assertEqual(routed, plain.replace(ORIGINAL_IMPORT, DIAGNOSTIC_IMPORT))
        self.assertEqual(struct.unpack_from('<I', routed, 20)[0], header_crc(routed, 156))

    def test_unknown_or_truncated_audio_is_rejected(self):
        with self.assertRaises(ValueError):
            redirect_audio_image(self.raw[:-1])
        with self.assertRaises(ValueError):
            bytepair_image(self.raw[:-1])

    def test_both_copies_and_file_hashes_are_updated(self):
        result = instrument_audio(self.package)
        changed = 0
        for old_unit, new_unit in zip(data_units(self.package), data_units(result), strict=True):
            for old, new in zip(array_items(fields(old_unit.payload)[0]), array_items(fields(new_unit.payload)[0]), strict=True):
                raw = decompress(fields(old.payload)[0])
                if hashlib.sha256(raw).hexdigest() == AUDIO_SHA256:
                    self.assertEqual(decompress(fields(new.payload)[0]), redirect_audio_image(raw))
                    changed += 1
                else:
                    self.assertEqual(old, new)
        self.assertEqual(changed, 2)
        descriptions = []
        def walk(field):
            if field.kind == 24:
                meta = fields(field.payload[:-28])
                if meta[0].payload.decode('utf-16le').lower().endswith('\\ngiaudio.dll'):
                    descriptions.append(field)
            elif field.kind == 2:
                for child in array_items(field): walk(child)
            elif field.kind in (13, 26, 27, 28):
                for child in fields(field.payload): walk(child)
        walk(result.controller)
        self.assertEqual(len(descriptions), 2)
        updated = redirect_audio_image(self.raw)
        for description in descriptions:
            meta = fields(description.payload[:-28])
            digest = next(f for f in meta if f.kind == 25)
            self.assertEqual(fields(digest.payload[4:])[0].payload, hashlib.sha1(updated).digest())
            self.assertEqual(struct.unpack('<IIQQI', description.payload[-28:]), (1, 32768, len(updated), len(updated), 0))
        self.assertEqual(Package.parse(result.encode()), result)
