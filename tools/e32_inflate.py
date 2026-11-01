"""Bounded decoder for the MSB-first Huffman stream used by Symbian E32.

Format reference: EKA2L1 common/flate.cpp and Symbian's image compression format.
This is not the RFC 1951 bit stream accepted by Python's zlib module.
"""
import struct


class Bits:
    def __init__(self, data):
        self.data, self.position = data, 0

    def read(self, count):
        result = 0
        for _ in range(count):
            if self.position >= len(self.data) * 8:
                raise ValueError('Truncated E32 compressed stream')
            result = result * 2 + ((self.data[self.position // 8] >> (7 - self.position % 8)) & 1)
            self.position += 1
        return result


class Huffman:
    def __init__(self, lengths):
        self.codes = {}
        code = 0
        for size in range(1, 28):
            for symbol, length in enumerate(lengths):
                if length == size:
                    if code >= 1 << size:
                        raise ValueError('Oversubscribed E32 Huffman tree')
                    self.codes[size, code] = symbol
                    code += 1
            code *= 2

    def read(self, bits):
        code = 0
        for size in range(1, 28):
            code = code * 2 + bits.read(1)
            if (size, code) in self.codes:
                return self.codes[size, code]
        raise ValueError('Invalid E32 Huffman symbol')


def inflate(data, expected):
    if not 0 <= expected <= 16 * 1024 * 1024:
        raise ValueError('E32 expansion limit exceeded')
    bits = Bits(data)
    meta = Huffman([2, 3, 2, 3, 4, 4, 5, 6, 6, 6, 7, 7, 7, 7,
                    8, 8, 8, 9, 10, 11, 12, 14, 15, 15, 15, 15, 15, 16, 16])
    lengths, order, last, repeat = [], list(range(28)), 0, 0
    while len(lengths) + repeat < 329:
        value = meta.read(bits)
        if value < 2:
            repeat = repeat * 2 + value + 1
        else:
            lengths.extend([last] * repeat)
            repeat = 0
            index = value - 1
            order[0] = last
            last = order[index]
            order[1:index + 1] = order[:index]
            lengths.append(last)
    lengths.extend([last] * repeat)
    if len(lengths) != 329:
        raise ValueError('Invalid E32 Huffman length run')
    literals, distances = Huffman(lengths[:285]), Huffman(lengths[285:])
    result = bytearray()

    def magnitude(code):
        if code < 8:
            return code
        extra = (code >> 2) - 1
        return ((code - 4 * extra) << extra) | bits.read(extra)

    while True:
        value = literals.read(bits)
        if value == 284:
            break
        if value < 256:
            result.append(value)
        else:
            length = magnitude(value - 256) + 3
            distance = magnitude(distances.read(bits)) + 1
            if distance > len(result) or len(result) + length > expected:
                raise ValueError('Invalid E32 history reference')
            for _ in range(length):
                result.append(result[-distance])
        if len(result) > expected:
            raise ValueError('E32 expansion exceeds declared size')
    if len(result) != expected:
        raise ValueError('E32 uncompressed length mismatch')
    return bytes(result)


def unpack(raw):
    if len(raw) < 156 or raw[16:20] != b'EPOC':
        raise ValueError('Expected E32 V header')
    offset = struct.unpack_from('<I', raw, 100)[0]
    compression = struct.unpack_from('<I', raw, 28)[0]
    if compression != 0x101f7afc or offset != 156:
        raise ValueError('Expected E32 deflate image with V header')
    expected = struct.unpack_from('<I', raw, 124)[0]
    return raw[:offset] + inflate(raw[offset:], expected)
