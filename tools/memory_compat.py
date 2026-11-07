"""Keep NGI memory pools working on the Symbian^3 hybrid heap."""
from audio_compat import replace_images
from orientation_compat import patch_code

MEMORY_SHA256 = '785a351ee5095d1f4ad42c5c9947bcb5565bf3be8b5945092d07e2db91109ded'
# Pool ownership read the classic RHeap bounds, but RHybridHeap only sets its top at construction,
# so cells past the first page were foreign and ReAlloc/Free failed. Use [chunk base, heap + max).
OWNS = (0x829c,
        bytes.fromhex('70b504000d0013d02068816c20008847a8420dd82068816c200088470600'
                      '2068c16c200088473018a842'),
        bytes.fromhex('70b50d00803004682600e06801680029 02d001f012e90600216a6118'
                      '0020b54202d38d4200d2012070bd'.replace(' ', '')))
# A pool is reserved by growing its first cell with ReAlloc(ENeverMove). RHybridHeap only grows
# such a cell into committed memory, so pool heaps are created fully committed (min = max).
COMMITTED = 0x8698, bytes.fromhex('00200121'), bytes.fromhex('01000020')


def patch_memory(raw):
    return patch_code(raw, MEMORY_SHA256, OWNS, COMMITTED)


def adapt_memory(package):
    return replace_images(package, MEMORY_SHA256, patch_memory, 'ngimemorymanagement.dll',
                          copies=1)
