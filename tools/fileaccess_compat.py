"""Make NGI DRM-file size queries work with the Symbian^3 OMA DRM agent."""
from audio_compat import replace_images
from orientation_compat import patch_code

FILEACCESS_SHA256 = '8cce910bbad0a2f5e57269a7fec26d4771e965a82efc2a367c5b41cd19d8eca2'
# Games size DCF content with Seek(end); the Symbian^3 agent returns the offset unchanged for
# CData::Seek(ESeekEnd), so resolve end-relative seeks from DataSizeL as a clamped absolute seek.
SEEK = (0xacec,
        bytes.fromhex('08b5c06a0b00002800d108bd002a03d0022a03d1032102e0012100e0'
                      '02216a46009300f06aee009808bd'),
        bytes.fromhex('08b50b00816ec06a00280cd0022a04d15b188b4200dd0b000022511c'
                      '6a46009300f06aee009808bdc046'))


def patch_fileaccess(raw):
    return patch_code(raw, FILEACCESS_SHA256, SEEK)


def adapt_fileaccess(package):
    return replace_images(package, FILEACCESS_SHA256, patch_fileaccess, 'ngifileaccess.dll',
                          copies=1)
