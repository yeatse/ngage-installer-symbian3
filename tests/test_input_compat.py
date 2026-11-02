"""Execute the compiled key adapter and verify the pinned E32 augmentation."""
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
from input_compat import (INPUT_SHA256, CODE_BASE, CODE_SIZE, VTABLE_SLOT, INTERNAL_CALL,
                          INTERNAL_STUB_OFFSET, compile_softkeys, patch_input, adapt_input, verify_stub, thumb_call)
from merge_packages import data_units
from sis import Package, fields, array_items, decompress


class InputCompatibilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = Path(os.environ.get('NGAGE_SDK', '/nonexistent'))
        if not (sdk / 'bin/clang').exists():
            raise unittest.SkipTest('NGAGE_SDK is needed to test compiled Thumb code')
        inputs = Path(os.environ.get('NGAGE_VALIDATION_INPUTS', ROOT / 'downloads'))
        source = inputs / 'n97-ngage.sisx'
        if not source.exists():
            raise unittest.SkipTest('Fetch the pinned installer before input checks')
        cls.package = Package.parse(adapt_client(extract_client(Package.parse(source.read_bytes())))[0])
        cls.raw = next(decompress(fields(f.payload)[0]) for unit in data_units(cls.package)
                       for f in array_items(fields(unit.payload)[0])
                       if hashlib.sha256(decompress(fields(f.payload)[0])).hexdigest() == INPUT_SHA256)
        cls.stub = compile_softkeys(sdk, ROOT)

    def test_compiled_code_handles_both_input_abis_and_preserves_other_keys(self):
        # Includes register/stack preservation and fallback execution in an ARM CPU.
        verify_stub(self.stub)
        damaged = bytearray(self.stub); damaged[0] = 0xa6
        with self.assertRaises(ValueError): verify_stub(bytes(damaged))

    def test_only_lookup_pointer_and_internal_call_change_in_original_code(self):
        original = bytepair_image(self.raw)
        patched = patch_input(self.raw, self.stub)
        offset = struct.unpack_from('<I', original, 100)[0]
        expected = bytearray(original[offset:offset + CODE_SIZE])
        struct.pack_into('<I', expected, VTABLE_SLOT - CODE_BASE, CODE_BASE + CODE_SIZE + 1)
        at = INTERNAL_CALL - CODE_BASE
        expected[at:at + 4] = thumb_call(INTERNAL_CALL, CODE_BASE + CODE_SIZE + INTERNAL_STUB_OFFSET)
        self.assertEqual(patched[offset:offset + CODE_SIZE], expected)
        self.assertEqual(patched[offset + CODE_SIZE:offset + CODE_SIZE + len(self.stub)], self.stub)
        self.assertEqual(patched[offset + CODE_SIZE + len(self.stub):], original[offset + CODE_SIZE:])
        self.assertEqual(struct.unpack_from('<I', patched, 48)[0], CODE_SIZE + len(self.stub))
        self.assertEqual(struct.unpack_from('<I', patched, 20)[0], header_crc(patched, offset))
        with self.assertRaises(ValueError): patch_input(self.raw[:-1], self.stub)

    def test_both_installer_branches_contain_the_augmented_input(self):
        result = adapt_input(self.package, self.stub)
        changed = 0
        for old_unit, new_unit in zip(data_units(self.package), data_units(result), strict=True):
            for old, new in zip(array_items(fields(old_unit.payload)[0]), array_items(fields(new_unit.payload)[0]), strict=True):
                raw = decompress(fields(old.payload)[0])
                if hashlib.sha256(raw).hexdigest() == INPUT_SHA256:
                    self.assertEqual(decompress(fields(new.payload)[0]), patch_input(raw, self.stub)); changed += 1
                else: self.assertEqual(old, new)
        self.assertEqual(changed, 2)
        self.assertEqual(Package.parse(result.encode()), result)
