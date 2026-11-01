"""Make the pinned Belle SWI server accept a provider without a filename.

Swi::MSisDataProvider is a supported input mode. It deliberately has no filename;
CSisHelperServer::GetSisFileNameL leaves with KErrNotFound for this mode. Preserve
all other errors and the ordinary filename path. This runs entirely in the guest.
"""
import hashlib
import struct
from component_identity import header_crc
from e32_inflate import unpack

INPUT_SHA256 = '979f20e48b3c6b9766acc17b54f354112113380cb66033bc65757f96a6e83bcd'


def branch(site, target, exchange=False):
    origin = (site + 4) & ~3 if exchange else site + 4
    delta = target - origin
    if delta & 1 or not -(1 << 22) <= delta < (1 << 22):
        raise ValueError('Thumb branch target out of range')
    return struct.pack('<HH', 0xf000 | ((delta >> 12) & 0x7ff),
                       (0xe800 if exchange else 0xf800) | ((delta >> 1) & 0x7ff))


def adapt_server(raw):
    if hashlib.sha256(raw).hexdigest() != INPUT_SHA256:
        raise ValueError('Unrecognized Belle installation service')
    image = bytearray(unpack(raw))
    if header_crc(image, 156) != struct.unpack_from('<I', image, 20)[0]:
        raise ValueError('Input E32 header checksum mismatch')
    code_size = struct.unpack_from('<I', image, 48)[0]
    if code_size != 0x1c548 or struct.unpack_from('<I', image, 52)[0] != 0:
        raise ValueError('Unexpected SWI server section layout')
    call = 156 + 0x149ba
    if image[call:call + 4] != bytes.fromhex('01f0f0ee'):
        raise ValueError('GetSisFileNameL error handler did not match')
    # push {r1,lr}; adds r0,#1; beq done; subs r0,#1;
    # blx User::LeaveIfError; done: pop {r1,pc}; nop
    trampoline = (bytes.fromhex('02b5013002d00138')
                  + branch(code_size + 8, 0x1679c, exchange=True)
                  + bytes.fromhex('02bdc046'))
    image[call:call + 4] = branch(0x149ba, code_size)
    image[156 + code_size:156 + code_size] = trampoline
    for offset in (48, 96, 108, 112, 124):
        old = struct.unpack_from('<I', image, offset)[0]
        struct.pack_into('<I', image, offset, old + len(trampoline))
    struct.pack_into('<I', image, 28, 0)  # emit an ordinary uncompressed E32 image
    struct.pack_into('<I', image, 20, header_crc(image, 156))
    return bytes(image)
