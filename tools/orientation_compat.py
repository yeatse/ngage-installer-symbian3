"""Present portrait N-Gage games unrotated and centred on landscape Symbian^3 screens."""
import hashlib
import struct
import subprocess

from audio_compat import bytepair_image, replace_images
from component_identity import header_crc

# The N97 variant is the one the adapted touch-profile condition selects on Symbian^3.
CONFIG_SHA256 = '8a2cdba6e60bad0f8b61148550de84e1b2df4659dbb627608a61856271c658e1'
ORIENTATION_SHA256 = 'b69f30bce5bb5a5156093fa54beaaf1ca94783ef86e9060872a328c46d80818e'
DISPLAY_SHA256 = '586c943e46fed9695c1c6cddec3cda6ec37e99efd0f100aff7f20e9a84bbe5bf'
BACKBUFFER_SHA256 = 'e879bd5a15ba145c4007f2eafb2bc1e9c1a519d3b39082e6319609d462b1741d'
CODE_BASE = 0x8000
# Every orientation request keeps the landscape app UI (CAknAppUiBase::SetOrientationL) instead of
# selecting a configured WS mode index, which is device-specific and selected portrait on an 808.
# The replaced mode selection code holds the stub reporting portrait/landscape changes; its two
# window-server imports are retargeted to the functions the stub calls.
ORIENTATION_ENTRY = 0x8244, bytes.fromhex('e069')
ORIENTATION_STUB = 0x8278, 0x82fe
ORIENTATION_SYMBOLS = {'ngage_coe_static': 0x8578, 'ngage_set_orientation': 0x8558,
                       'ngage_window_group_identifier': 0x85d0, 'ngage_send_event': 0x85d8,
                       'ngage_orientation_exit': 0x82ff}
# RWindowGroup::Identifier and RWsSession::SendEventToWindowGroup replace the WS mode queries.
ORIENTATION_IMPORTS = ((0x85d4, 398, 389), (0x85dc, 414, 45))
# ngidisplay's rotation query reports the orientation the game requested instead.
DISPLAY_ENTRY = 0x8a6c, bytes.fromhex('6846')
DISPLAY_STUB = 0x8ac0, 0x8afa
DISPLAY_SYMBOLS = {'ngage_runtime_interface': 0x97d8, 'ngage_display_map': 0x8a93}
# Portrait backbuffers keep their own width and height instead of being swapped to landscape.
DIMENSION_SWAP = 0xd3e4, struct.pack('<I', 0x1a000010), struct.pack('<I', 0xea000020)
# Every 16-bit backbuffer (RGB444, RGB555 and RGB565, low byte 0x10) is two bytes per pixel and is
# posted as KPostPlatformDependent | NGI format, which the posting stand-in decodes. NGI otherwise
# would treat RGB444 and RGB555 as four-byte RGBx888.
FORMAT = (
    (0xd194, bytes.fromhex('01c840e2'), bytes.fromhex('ffc000e2')),
    (0xd19c, bytes.fromhex('0700a0030650a0e103504602'),
     bytes.fromhex('015280e30700a0030000a0e1')),
    (0xd4a0, bytes.fromhex('01c840e2'), bytes.fromhex('ffc000e2')),
    (0xd4a8, bytes.fromhex('0700a0030750a0e103504702'),
     bytes.fromhex('015280e30700a0030000a0e1')),
    (0xe6c0, bytes.fromhex('01c840e2'), bytes.fromhex('ffc000e2')),
    (0xed94, bytes.fromhex('01c841e2'), bytes.fromhex('ffc001e2')),
    (0xef78, bytes.fromhex('01c841e2'), bytes.fromhex('ffc001e2')),
)
DISPLAY = '/hardware/graphics/displays/0'
DISPLAY_CONFIGURATIONS = DISPLAY + '/configurations'
ORIENTATIONS = DISPLAY + '/ngageorientations'
BACKBUFFERS = '/hardware/graphics/backbuffer/configurations/'
USER_OPTIONS = 3
PORTRAIT = 0


def cardinality(data, at):
    value = data[at]
    if not value & 1:
        return value >> 1, at + 1
    if not value & 2:
        return struct.unpack_from('<H', data, at)[0] >> 2, at + 2
    return struct.unpack_from('<I', data, at)[0] >> 3, at + 4


def encode_cardinality(value):
    if value < 0x80:
        return bytes([value << 1])
    if value < 0x4000:
        return struct.pack('<H', (value << 2) | 1)
    return struct.pack('<I', (value << 3) | 3)


def parse_cre(data):
    """Split a central repository store into header, settings and trailer."""
    if struct.unpack_from('<III', data) != (0x10000037, 0, 0x10202be9):
        raise ValueError('Expected a central repository store')
    version, at = data[20], 25
    if version >= 3:
        at += 1
    at += 4

    def descriptor(at):
        length, at = cardinality(data, at)
        return at + (length >> 1)

    def policy(at):
        return descriptor(descriptor(at + 12))

    for _ in range(2):
        count = struct.unpack_from('<I', data, at)[0]; at += 4
        for _ in range(count):
            at = policy(at)
    at = descriptor(descriptor(at))
    if version >= 2:
        at += 8
    at += 4
    at += 4 + 16 * struct.unpack_from('<I', data, at)[0] + 8
    count = struct.unpack_from('<I', data, at)[0]
    head, at = data[:at], at + 4
    entries = []
    for _ in range(count):
        key, meta, kind = struct.unpack_from('<IIB', data, at); at += 9
        if kind == 0:
            value = struct.unpack_from('<i', data, at)[0]; at += 4
        elif kind == 1:
            value = struct.unpack_from('<d', data, at)[0]; at += 8
        elif kind == 2:
            length, at = cardinality(data, at)
            value = data[at:at + (length >> 1)].decode('utf-16le'); at += length >> 1
        else:
            raise ValueError('Unknown central repository setting type')
        entries.append([key, meta, kind, value])
    return head, entries, data[at:]


def build_cre(head, entries, tail):
    result = bytearray(head) + struct.pack('<I', len(entries))
    for key, meta, kind, value in entries:
        result += struct.pack('<IIB', key, meta, kind)
        if kind == 0:
            result += struct.pack('<i', value)
        elif kind == 1:
            result += struct.pack('<d', value)
        else:
            text = value.encode('utf-16le')
            result += encode_cardinality((len(text) << 1) | 1) + text
    return bytes(result + tail)


def sections(entries):
    """Map each NGI configuration path to its {name: value entry} pairs."""
    result, path = {}, None
    for index, (_, _, kind, value) in enumerate(entries):
        if kind == 2 and value.startswith('/'):
            path = value
            result.setdefault(path, {})
            continue
        if path and kind == 2 and index + 1 < len(entries):
            result[path][value] = entries[index + 1]
    return result


def add_portrait_configurations(entries, config):
    """Portrait games pick a display configuration of the native portrait size and quit without one."""
    configurations = config[DISPLAY_CONFIGURATIONS]
    count_entry = configurations['configurationscount']
    count, parent = count_entry[3], count_entry[0] & 0xffff0000
    width, height = config[DISPLAY]['nativewidth'][3], config[DISPLAY]['nativeheight'][3]
    existing = [config[f'{DISPLAY_CONFIGURATIONS}/{index}'] for index in range(count)]
    formats = [values['colorformat'][3] for values in existing]
    if any((values['width'][3], values['height'][3]) == (width, height) for values in existing):
        raise ValueError('Expected no portrait display configuration')
    used, section, added = {key >> 16 for key, _, _, _ in entries}, 0, []
    for colorformat in sorted(set(formats), key=formats.index):
        while section in used:
            section += 1
        used.add(section)
        key = section << 16
        added += [[key, 0, 2, f'{DISPLAY_CONFIGURATIONS}/{count}'], [key + 1, 0, 0, parent],
                  [key + 2, 0, 2, 'width'], [key + 3, 0, 0, width],
                  [key + 4, 0, 2, 'height'], [key + 5, 0, 0, height],
                  [key + 6, 0, 2, 'colorformat'], [key + 7, 0, 0, colorformat]]
        count += 1
    count_entry[3] = count
    return sorted(entries + added, key=lambda entry: entry[0])


def patch_config(raw):
    if hashlib.sha256(raw).hexdigest() != CONFIG_SHA256:
        raise ValueError('Unexpected NGI display configuration')
    head, entries, tail = parse_cre(raw)
    config = sections(entries)
    backbuffer = config['/hardware/graphics/backbuffer']
    screen_width = backbuffer['postingsurfacefullscreenwidth'][3]
    screen_height = backbuffer['postingsurfacefullscreenheight'][3]
    if screen_width <= screen_height:
        raise ValueError('Expected a landscape posting surface')
    # Games lay out for the native display size; describe the panel as portrait, matching its
    # orientation 0 at angle 0, so portrait games use their whole backbuffer height.
    display = config[DISPLAY]
    if display['nativewidth'][3] > display['nativeheight'][3]:
        display['nativewidth'][3], display['nativeheight'][3] = \
            display['nativeheight'][3], display['nativewidth'][3]
    options = config[ORIENTATIONS]
    options['useroptions'][3] = USER_OPTIONS
    entries = add_portrait_configurations(entries, config)
    scaled = 0
    for path, values in config.items():
        if not path.startswith(BACKBUFFERS) or values['orientationtype'][3] != PORTRAIT:
            continue
        width, height = values['width'][3], values['height'][3]
        scaled_width = (width * screen_height + height // 2) // height
        values['scalingsize'][3] = f'0x{scaled_width:X}, 0x{screen_height:X}'
        values['windowposition'][3] = f'0x{(screen_width - scaled_width) // 2:X}, 0x0'
        scaled += 1
    if not scaled:
        raise ValueError('No portrait backbuffer configurations')
    return build_cre(head, entries, tail)


def patch_code(raw, expected_sha, *patches):
    if hashlib.sha256(raw).hexdigest() != expected_sha:
        raise ValueError('Unexpected NGI display library')
    image = bytearray(bytepair_image(raw))
    if struct.unpack_from('<I', image, 76)[0] != CODE_BASE:
        raise ValueError('Unexpected NGI code base')
    for address, original, replacement in patches:
        at = struct.unpack_from('<I', image, 100)[0] + address - CODE_BASE
        if image[at:at + len(original)] != original:
            raise ValueError('Unexpected NGI display instructions')
        image[at:at + len(original)] = replacement
    struct.pack_into('<I', image, 20, header_crc(image, 156))
    return bytes(image)


def thumb_branch(source, target):
    delta = target - source - 4
    if delta & 1 or not -2048 <= delta < 2048:
        raise ValueError('Thumb branch out of range')
    return struct.pack('<H', 0xe000 | ((delta >> 1) & 0x7ff))


def compile_stub(sdk, root, name, address, symbols):
    output = root / 'build/orientation'
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(sdk / 'bin/clang'), '--target=armv5t-none-eabi', '-march=armv5t', '-c',
                    str(root / f'native/orientation/{name}.S'), '-o', str(output / f'{name}.o')],
                   check=True)
    subprocess.run([str(sdk / 'bin/ld.lld'), '-m', 'armelf', '--image-base=0', '-e', '0',
                    *(f'--defsym={symbol}={value:#x}' for symbol, value in symbols.items()),
                    f'-Ttext={address:#x}', str(output / f'{name}.o'), '-o',
                    str(output / f'{name}.elf')], check=True)
    elf = (output / f'{name}.elf').read_bytes()
    if elf[:6] != b'\x7fELF\x01\x01':
        raise ValueError('Expected ARM ELF32 stub')
    offset = struct.unpack_from('<I', elf, 32)[0]
    width, count, names = struct.unpack_from('<HHH', elf, 46)
    headers = [struct.unpack_from('<10I', elf, offset + i * width) for i in range(count)]
    strings = elf[headers[names][4]:headers[names][4] + headers[names][5]]
    text = next(h for h in headers if strings[h[0]:].split(b'\0', 1)[0] == b'.text')
    if text[3] != address:
        raise ValueError('Unexpected orientation stub address')
    return elf[text[4]:text[4] + text[5]]


def compile_orientation(sdk, root):
    stubs = (compile_stub(sdk, root, 'notify', ORIENTATION_STUB[0], ORIENTATION_SYMBOLS),
             compile_stub(sdk, root, 'display', DISPLAY_STUB[0], DISPLAY_SYMBOLS))
    verify_notify(stubs[0])
    verify_display(stubs[1])
    return stubs


def run_stub(stub, address, stop, registers, imports, memory=()):
    """Execute a stub with Python import handlers; returns the CPU at stop."""
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
    from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_SP
    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    cpu.mem_map(0x8000, 0x8000); cpu.mem_map(0x30000, 0x2000)
    cpu.mem_write(address, stub)
    for at, data in memory:
        cpu.mem_write(at, data)
    def call(cpu, at, size, data):
        if at in imports:
            result = imports[at](cpu, *(cpu.reg_read(UC_ARM_REG_R0 + i) for i in range(3)))
            cpu.reg_write(UC_ARM_REG_R0, result & 0xffffffff)
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
    cpu.hook_add(UC_HOOK_CODE, call)
    cpu.reg_write(UC_ARM_REG_SP, 0x31800)
    for register, value in registers.items():
        cpu.reg_write(register, value)
    cpu.emu_start(address | 1, stop, count=200)
    if cpu.reg_read(UC_ARM_REG_PC) != stop or cpu.reg_read(UC_ARM_REG_SP) != 0x31800:
        raise ValueError('Orientation stub exit/stack mismatch')
    return cpu


def verify_notify(stub):
    from unicorn.arm_const import UC_ARM_REG_R4, UC_ARM_REG_R5
    env, this, group = 0x30400, 0x30100, 0x77
    for old, new, reported in ((2, 1, True), (0, 1, True), (1, 2, True), (2, 8, False),
                               (1, 4, False), (0, 2, False), (2, 2, False)):
        calls = []
        def window_group(cpu, r0, r1, r2):
            if r0 != env + 0x38:
                raise ValueError('Wrong root window group')
            return group
        def send(cpu, r0, r1, r2):
            event = struct.unpack('<4I', cpu.mem_read(r2, 16))
            if (r0, r1, event) != (env + 0x2c, group, (20, 0, 0, 0)):
                raise ValueError('Wrong screen device change event')
            calls.append('event')
            return 0
        def orientation(cpu, r0, r1, r2):
            calls.append((r0, r1))
            return 0
        imports = {ORIENTATION_SYMBOLS['ngage_coe_static']: lambda *_: env,
                   ORIENTATION_SYMBOLS['ngage_set_orientation']: orientation,
                   ORIENTATION_SYMBOLS['ngage_window_group_identifier']: window_group,
                   ORIENTATION_SYMBOLS['ngage_send_event']: send}
        cpu = run_stub(stub, ORIENTATION_STUB[0], ORIENTATION_SYMBOLS['ngage_orientation_exit'] & ~1,
                       {UC_ARM_REG_R4: this, UC_ARM_REG_R5: new}, imports,
                       ((env + 0x24, struct.pack('<I', 0x30800)), (this + 0x14, struct.pack('<I', old))))
        expected = [(0x30800, 2)] + (['event'] if reported else [])
        if calls != expected or struct.unpack('<I', cpu.mem_read(this + 0x14, 4))[0] != new \
                or (cpu.reg_read(UC_ARM_REG_R4), cpu.reg_read(UC_ARM_REG_R5)) != (this, new):
            raise ValueError('Orientation notification mismatch')


def verify_display(stub):
    from unicorn.arm_const import UC_ARM_REG_R1, UC_ARM_REG_R7
    interface, table, get, release = 0x30100, 0x30140, 0x30200, 0x30210
    for available, current, expected in ((True, 1, 1), (True, 8, 8), (True, 0, 2), (True, 0x10, 2),
                                         (True, 3, 2), (True, 0x1534368, 2),
                                         (False, 1, 2)):
        released = []
        def query(cpu, r0, r1, r2):
            if r0 != 0x1000631:
                raise ValueError('Wrong orientation interface')
            if not available:
                return -1
            cpu.mem_write(r1, struct.pack('<I', interface))
            return 0
        def current_orientation(cpu, r0, r1, r2):
            if r0 != interface:
                raise ValueError('Wrong orientation object')
            cpu.mem_write(r1, struct.pack('<I', current))
            return 0
        imports = {DISPLAY_SYMBOLS['ngage_runtime_interface']: query, get: current_orientation,
                   release: lambda cpu, r0, r1, r2: released.append(r0) or 0}
        vtable = bytearray(0x20)
        struct.pack_into('<I', vtable, 4, release); struct.pack_into('<I', vtable, 0x18, get)
        cpu = run_stub(stub, DISPLAY_STUB[0], DISPLAY_SYMBOLS['ngage_display_map'] & ~1, {}, imports,
                       ((interface, struct.pack('<I', table)), (table, bytes(vtable))))
        if (cpu.reg_read(UC_ARM_REG_R1), cpu.reg_read(UC_ARM_REG_R7)) != (expected, 1) \
                or released != ([interface] if available else []):
            raise ValueError('Display orientation mismatch')


def code_relocations(image):
    relocation = struct.unpack_from('<I', image, 104)[0]
    if not relocation:
        return set()
    size, result, cursor = struct.unpack_from('<I', image, relocation)[0], set(), relocation + 8
    while cursor < relocation + 8 + size:
        page, length = struct.unpack_from('<II', image, cursor)
        if length < 8:
            raise ValueError('Malformed code relocation block')
        for at in range(cursor + 8, cursor + length, 2):
            tag = struct.unpack_from('<H', image, at)[0]
            if tag >> 12:
                result.add(CODE_BASE + page + (tag & 0xfff))
        cursor += length
    return result


def patch_stub(raw, expected_sha, entry, region, stub, imports=()):
    """Branch from entry to a stub that replaces the unreachable code in region."""
    start, end = region
    if len(stub) > end - start or any(start - 3 <= at < end for at in
                                      code_relocations(bytepair_image(raw))):
        raise ValueError('Orientation stub does not fit its code region')
    patches = [(entry[0], entry[1], thumb_branch(entry[0], start))]
    plain = bytepair_image(raw)
    at = struct.unpack_from('<I', plain, 100)[0] + start - CODE_BASE
    patches.append((start, plain[at:at + len(stub)], stub))
    patches += [(address, struct.pack('<I', old), struct.pack('<I', new))
                for address, old, new in imports]
    return patch_code(raw, expected_sha, *patches)


def adapt_orientation(package, stubs):
    notify, display = stubs
    package = replace_images(package, CONFIG_SHA256, patch_config, '20001079.cre',
                             variants=True)
    package = replace_images(package, ORIENTATION_SHA256,
                             lambda raw: patch_stub(raw, ORIENTATION_SHA256, ORIENTATION_ENTRY,
                                                    ORIENTATION_STUB, notify, ORIENTATION_IMPORTS),
                             'ngigameorientation.dll', copies=1)
    package = replace_images(package, DISPLAY_SHA256,
                             lambda raw: patch_stub(raw, DISPLAY_SHA256, DISPLAY_ENTRY,
                                                    DISPLAY_STUB, display),
                             'ngidisplay.dll')
    return replace_images(package, BACKBUFFER_SHA256,
                          lambda raw: patch_code(raw, BACKBUFFER_SHA256, DIMENSION_SWAP,
                                                         *FORMAT),
                          'ngibackbuffer.dll')
