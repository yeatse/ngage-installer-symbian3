"""Route the NGI runtime through the legacy thread heap DLL."""
import hashlib
import struct

from audio_compat import bytepair_image, replace_images

RUNTIME_SHA256 = '532eb9a176181fc844b3cc72efaea41b61e9d510663f3e02a5d420e82b589047'
# The runtime imports only CloseSTDLIB (ordinal 1) from ESTLIB; ngheap forwards it.
ORIGINAL_IMPORT = b'estlib{000a0000}[10003b0b].dll'
COMPAT_IMPORT = b'ngheap{000a0000}[e0000968].dll'


def redirect_runtime_image(raw):
    if hashlib.sha256(raw).hexdigest() != RUNTIME_SHA256:
        raise ValueError('Unexpected NGI runtime input')
    image = bytepair_image(raw)
    code_offset, import_offset = struct.unpack_from('<I', image, 100)[0], struct.unpack_from('<I', image, 108)[0]
    code_size = struct.unpack_from('<I', image, 48)[0]
    if import_offset < code_offset + code_size or image.count(ORIGINAL_IMPORT) != 1:
        raise ValueError('Unexpected runtime import layout')
    result = image[:import_offset] + image[import_offset:].replace(ORIGINAL_IMPORT, COMPAT_IMPORT)
    if len(result) != len(image) or result[code_offset:code_offset + code_size] != image[code_offset:code_offset + code_size]:
        raise ValueError('Runtime redirection changed executable code')
    return result


def adapt_heap(package):
    return replace_images(package, RUNTIME_SHA256, redirect_runtime_image, 'runtime.dll')
