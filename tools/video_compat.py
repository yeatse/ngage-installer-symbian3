"""Route NGI video's MMF imports through the native video compatibility bridge."""
import hashlib
import struct

from audio_compat import bytepair_image, replace_images

VIDEO_SHA256 = '045ffdcfae2b702ec552ac715f7a4f45aca932937d24eb7907b38773900e5d04'
ORIGINAL_IMPORT = b'mediaclientvideo{000a0000}[101f4569].dll'
COMPAT_IMPORT = b'NgageVideoBridge{000a0000}[e000096a].dll'


def redirect_video_image(raw):
    if hashlib.sha256(raw).hexdigest() != VIDEO_SHA256:
        raise ValueError('Unexpected NGI video input')
    image = bytepair_image(raw)
    code_offset, import_offset = struct.unpack_from('<I', image, 100)[0], struct.unpack_from('<I', image, 108)[0]
    code_size = struct.unpack_from('<I', image, 48)[0]
    if import_offset < code_offset + code_size or image[import_offset:].lower().count(ORIGINAL_IMPORT) != 1:
        raise ValueError('Unexpected video import layout')
    start = import_offset + image[import_offset:].lower().index(ORIGINAL_IMPORT)
    result = image[:start] + COMPAT_IMPORT + image[start + len(ORIGINAL_IMPORT):]
    if len(result) != len(image) or result[code_offset:code_offset + code_size] != image[code_offset:code_offset + code_size]:
        raise ValueError('Video redirection changed executable code')
    return result


def adapt_video(package):
    return replace_images(package, VIDEO_SHA256, redirect_video_image, 'ngivideo.dll')
