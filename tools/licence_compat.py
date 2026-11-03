"""Report full licence status for exported games whose rights cannot be reissued."""
import hashlib
import struct

from audio_compat import bytepair_image, replace_images

GAMETITLE_SHA256 = 'f9940e5e943097b040cc7d69c61f7db5d74ad626248a2edd79b22c84b18763ec'
CODE_BASE = 0x8000
# CGameTitleMetadata licence query: with no DRM permission it branches to store
# status 0 (trial); retarget it to the existing store of status 1 (valid rights).
NO_PERMISSION_BRANCH = 0x8646
ORIGINAL_BRANCH = 0xd01b  # beq 0x8680: str r7(=0), [r4]
FULL_LICENCE_BRANCH = 0xd012  # beq 0x866e: movs r0, #1; str r0, [r4]
FULL_LICENCE_STORE = 0x866e
EXPECTED_STORE = bytes.fromhex('012020600398')  # movs r0,#1; str r0,[r4]; ldr r0,[sp,#0xc]


def patch_gametitle(raw):
    if hashlib.sha256(raw).hexdigest() != GAMETITLE_SHA256:
        raise ValueError('Unexpected gametitlemetadata input')
    image = bytearray(bytepair_image(raw))
    code_offset = struct.unpack_from('<I', image, 100)[0]
    if struct.unpack_from('<I', image, 76)[0] != CODE_BASE:
        raise ValueError('Unexpected gametitlemetadata code base')
    site = code_offset + NO_PERMISSION_BRANCH - CODE_BASE
    store = code_offset + FULL_LICENCE_STORE - CODE_BASE
    if struct.unpack_from('<H', image, site)[0] != ORIGINAL_BRANCH or image[store:store + 6] != EXPECTED_STORE:
        raise ValueError('Unexpected gametitlemetadata licence query')
    struct.pack_into('<H', image, site, FULL_LICENCE_BRANCH)
    return bytes(image)


def adapt_licence(package):
    return replace_images(package, GAMETITLE_SHA256, patch_gametitle,
                          'gametitlemetadata.dll', copies=1, descriptions=3)
