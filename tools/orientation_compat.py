"""Present portrait N-Gage games unrotated and centred on landscape Symbian^3 screens."""
import hashlib
import struct

from audio_compat import bytepair_image, replace_images
from component_identity import header_crc

# The N97 variant is the one the adapted touch-profile condition selects on Symbian^3.
CONFIG_SHA256 = '8a2cdba6e60bad0f8b61148550de84e1b2df4659dbb627608a61856271c658e1'
ORIENTATION_SHA256 = 'b69f30bce5bb5a5156093fa54beaaf1ca94783ef86e9060872a328c46d80818e'
BACKBUFFER_SHA256 = 'e879bd5a15ba145c4007f2eafb2bc1e9c1a519d3b39082e6319609d462b1741d'
CODE_BASE = 0x8000
# Every orientation request becomes CAknAppUiBase::SetOrientationL(landscape). The configured
# WS mode indexes are device-specific: the X7's second mode is portrait.
LANDSCAPE = 0x8244, bytes.fromhex('e0690526f6430028'), bytes.fromhex('00f098e9406a10e0')
# Portrait backbuffers keep their own width and height instead of being swapped to landscape.
DIMENSION_SWAP = 0xd3e4, struct.pack('<I', 0x1a000010), struct.pack('<I', 0xea000020)
DISPLAY = '/hardware/graphics/displays/0'
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


def patch_code(raw, expected_sha, patch):
    if hashlib.sha256(raw).hexdigest() != expected_sha:
        raise ValueError('Unexpected NGI display library')
    image = bytearray(bytepair_image(raw))
    if struct.unpack_from('<I', image, 76)[0] != CODE_BASE:
        raise ValueError('Unexpected NGI code base')
    address, original, replacement = patch
    at = struct.unpack_from('<I', image, 100)[0] + address - CODE_BASE
    if image[at:at + len(original)] != original:
        raise ValueError('Unexpected NGI display instructions')
    image[at:at + len(original)] = replacement
    struct.pack_into('<I', image, 20, header_crc(image, 156))
    return bytes(image)


def adapt_orientation(package):
    package = replace_images(package, CONFIG_SHA256, patch_config, '20001079.cre',
                             variants=True)
    package = replace_images(package, ORIENTATION_SHA256,
                             lambda raw: patch_code(raw, ORIENTATION_SHA256, LANDSCAPE),
                             'ngigameorientation.dll', copies=1)
    return replace_images(package, BACKBUFFER_SHA256,
                          lambda raw: patch_code(raw, BACKBUFFER_SHA256, DIMENSION_SWAP),
                          'ngibackbuffer.dll')
