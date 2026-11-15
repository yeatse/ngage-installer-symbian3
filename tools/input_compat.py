"""Compile N97 virtual-softkey lookup augmentation for the pinned NGI image."""
import hashlib
from pathlib import Path
import struct
import subprocess
from audio_compat import bytepair_image, replace_images
from component_identity import header_crc

INPUT_SHA256 = 'a066bd661eb8504c6b59db5d3b7ad6847ce3f359aa4840b2e438d7ec5e5af5b3'
CODE_BASE = 0x8000
CODE_SIZE = 0x3224
LOOKUP = 0x922f
VTABLE_SLOT = 0xaf64
INTERNAL_LOOKUP = 0x8d1b
INTERNAL_CALL = 0x8f1c
INTERNAL_STUB_OFFSET = 54
STUB_SIZE = 116


def compile_softkeys(sdk, root):
    output = root / 'build/input'
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(sdk / 'bin/clang'), '--target=armv5t-none-eabi', '-march=armv5t', '-c',
                    str(root / 'native/input/softkeys.S'), '-o', str(output / 'softkeys.o')], check=True)
    subprocess.run([str(sdk / 'bin/ld.lld'), '-m', 'armelf', '--image-base=0', '--entry=ngage_softkeys',
                    f'--defsym=ngage_original_lookup={LOOKUP:#x}',
                    f'--defsym=ngage_original_internal_lookup={INTERNAL_LOOKUP:#x}', f'-Ttext={CODE_BASE + CODE_SIZE:#x}',
                    str(output / 'softkeys.o'), '-o', str(output / 'softkeys.elf')], check=True)
    elf = (output / 'softkeys.elf').read_bytes()
    if elf[:6] != b'\x7fELF\x01\x01':
        raise ValueError('Expected ARM ELF32 stub')
    offset = struct.unpack_from('<I', elf, 32)[0]
    width, count, names = struct.unpack_from('<HHH', elf, 46)
    headers = [struct.unpack_from('<10I', elf, offset + i * width) for i in range(count)]
    strings = elf[headers[names][4]:headers[names][4] + headers[names][5]]
    text = next(h for h in headers if strings[h[0]:].split(b'\0', 1)[0] == b'.text')
    if text[3] != CODE_BASE + CODE_SIZE or text[5] != STUB_SIZE:
        raise ValueError('Unexpected softkey stub layout')
    stub = elf[text[4]:text[4] + text[5]]
    verify_stub(stub)
    return stub


def patch_input(raw, stub):
    if hashlib.sha256(raw).hexdigest() != INPUT_SHA256 or len(stub) != STUB_SIZE:
        raise ValueError('Unexpected input image/stub')
    image = bytearray(bytepair_image(raw))
    offset = struct.unpack_from('<I', image, 100)[0]
    code_size = struct.unpack_from('<I', image, 48)[0]
    code_base = struct.unpack_from('<I', image, 76)[0]
    slot = offset + VTABLE_SLOT - CODE_BASE
    if code_base != CODE_BASE or code_size != CODE_SIZE or struct.unpack_from('<I', image, slot)[0] != LOOKUP:
        raise ValueError('Unexpected key lookup vtable')
    relocation = struct.unpack_from('<I', image, 112)[0]
    section_size = struct.unpack_from('<I', image, relocation)[0]
    cursor, found = relocation + 8, False
    while cursor < relocation + 8 + section_size:
        page, length = struct.unpack_from('<II', image, cursor)
        if length < 8 or cursor + length > len(image):
            raise ValueError('Malformed input relocation block')
        for at in range(cursor + 8, cursor + length, 2):
            tag = struct.unpack_from('<H', image, at)[0]
            if page + (tag & 0xfff) == VTABLE_SLOT - CODE_BASE and tag >> 12 == 1:
                found = True
        cursor += length
    if not found:
        raise ValueError('Key lookup pointer lacks code relocation')
    # This existing code relocation also relocates the augmented Thumb entry.
    struct.pack_into('<I', image, slot, CODE_BASE + CODE_SIZE + 1)
    call = offset + INTERNAL_CALL - CODE_BASE
    if image[call:call + 4] != thumb_call(INTERNAL_CALL, INTERNAL_LOOKUP & ~1):
        raise ValueError('Unexpected internal lookup call')
    image[call:call + 4] = thumb_call(INTERNAL_CALL, CODE_BASE + CODE_SIZE + INTERNAL_STUB_OFFSET)
    insert = offset + code_size
    image[insert:insert] = stub
    struct.pack_into('<I', image, 48, code_size + len(stub))
    for field in (104, 108, 112, 116):
        old = struct.unpack_from('<I', image, field)[0]
        if old >= insert:
            struct.pack_into('<I', image, field, old + len(stub))
    struct.pack_into('<I', image, 124, len(image) - offset)
    struct.pack_into('<I', image, 20, header_crc(image, offset))
    return bytes(image)


def adapt_input(package, stub):
    return replace_images(package, INPUT_SHA256, lambda raw: patch_input(raw, stub), 'ngiinput.dll')


def thumb_call(source, target):
    delta = target - source - 4
    if delta & 1 or not -(1 << 22) <= delta < (1 << 22):
        raise ValueError('Thumb call out of range')
    return struct.pack('<HH', 0xf000 | ((delta >> 12) & 0x7ff), 0xf800 | ((delta >> 1) & 0x7ff))


def verify_stub(stub):
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
    from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R4, UC_ARM_REG_R11, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC
    for internal in (False, True):
        for key, expected in ((0xa4, 0x1000), (0xa5, 0x2000), (0xf842, 0x1000), (0xf843, 0x2000), (0x60, 0x4000),
                              (0x10, 0x5a), (0x35, 0x5a), (0x61, 0x5a)):
            cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
            cpu.mem_map(0x1000, 0x1000); cpu.mem_map(0x8000, 0x8000); cpu.mem_map(0x20000, 0x1000)
            cpu.mem_write(CODE_BASE + CODE_SIZE, stub)
            cpu.mem_write(LOOKUP & ~1, bytes.fromhex('5a207047'))
            cpu.mem_write(INTERNAL_LOOKUP & ~1, bytes.fromhex('5a2066217047'))
            cpu.reg_write(UC_ARM_REG_R0, 0x20100); cpu.reg_write(UC_ARM_REG_R1, key)
            cpu.reg_write(UC_ARM_REG_SP, 0x20800); cpu.reg_write(UC_ARM_REG_LR, 0x1001)
            for register in range(UC_ARM_REG_R4, UC_ARM_REG_R11 + 1): cpu.reg_write(register, 0x400 + register)
            entry = CODE_BASE + CODE_SIZE + (INTERNAL_STUB_OFFSET if internal else 0)
            cpu.emu_start(entry | 1, 0x1000, count=100)
            if cpu.reg_read(UC_ARM_REG_PC) != 0x1000 or cpu.reg_read(UC_ARM_REG_R0) != expected or cpu.reg_read(UC_ARM_REG_SP) != 0x20800:
                raise ValueError('Softkey stub result/stack mismatch')
            if internal and cpu.reg_read(UC_ARM_REG_R1) != (0x66 if expected == 0x5a else 0):
                raise ValueError('Internal key high word mismatch')
            for register in range(UC_ARM_REG_R4, UC_ARM_REG_R11 + 1):
                if cpu.reg_read(register) != 0x400 + register: raise ValueError('Softkey stub changed a saved register')


KEYBOARD_ORIENTATIONS = ('/hardware/input/devices/0/orientation_0', '/hardware/input/devices/0/orientation_90')
PRIMARY, SECONDARY = 0x01000000, 0x02000000
# The N97 profile's keypad digits 1 to 0 are Q to P, * is J and # is H.
N97_KEYPAD = dict(zip(b'QWERTYUIOP', (1 << digit for digit in range(10)))) | {ord('J'): 0x400, ord('H'): 0x800}
# Symbian^3 QWERTY phones (E6, E7) take the keypad from the left of the keyboard instead, in three rows of
# three with 0 below them, and Q and A as the action keys beside Enter and Backspace.
QWERTY_KEYPAD = dict(zip(b'WERSDFXCVZ', (1 << digit for digit in range(10)))) | {ord('J'): 0x400, ord('H'): 0x800}
QWERTY_ACTIONS = {ord('Q'): PRIMARY, ord('A'): SECONDARY}


def parse_key_table(text):
    values = [int(value, 16) for value in text.split(',')]
    if len(values) % 2:
        raise ValueError('Odd key translation table')
    return dict(zip(values[::2], values[1::2]))


def format_key_table(table):
    return ', '.join(f'0x{scan:04x}, 0x{bits:08x}' for scan, bits in sorted(table.items()))


def remap_keyboard(config):
    """Rewrite the hardware keyboard's game-key translation in the N97 configuration sections."""
    for path in KEYBOARD_ORIENTATIONS:
        values = config[path]
        table = parse_key_table(values['keytranslationdata'][3])
        if len(table) != values['keytranslationrows'][3] or any(table.get(scan) != bits for scan, bits in N97_KEYPAD.items()) \
                or table.get(0x03) != PRIMARY or table.get(0x01) != SECONDARY:
            raise ValueError('Unexpected N97 key translation table')
        for scan in N97_KEYPAD:
            del table[scan]
        if any(scan in table for scan in QWERTY_KEYPAD.keys() | QWERTY_ACTIONS.keys()):
            raise ValueError('QWERTY game keys are already translated')
        table |= QWERTY_KEYPAD | QWERTY_ACTIONS
        values['keytranslationdata'][3] = format_key_table(table)
        values['keytranslationrows'][3] = len(table)
