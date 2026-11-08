#!/usr/bin/env python3
"""Build an unsigned Symbian^3 client using the N97 touch profile."""
import argparse
import hashlib
from pathlib import Path
import struct

from sis import Field, Package, array, array_items, fields, sequence

CLIENT_SHA256 = '321253b58a70e67d2de3b381e806dfa48b04e99e637ebc4730d6c42aa6306367'
SYMBIAN3_MARKER = r'z:\system\install\Series60v5.2.sis'
BELLE_MARKER = r'z:\system\install\Series60v5.3.sis'
N97_MACHINE_UID = 0x20014ddd
SYMBIAN3_PRODUCT_UID = 0x20022e6d
BELLE_PRODUCT_UID = 0x2003a678


def exists(path):
    return Field(29, struct.pack('<II', 10, 0)
                 + Field(1, path.encode('utf-16le')).encode())


def is_n97_comparison(field):
    if field.kind != 29 or len(field.payload) < 8:
        return False
    if struct.unpack_from('<I', field.payload)[0] != 1:
        return False
    operands = fields(field.payload[8:])
    return len(operands) == 2 and operands == [
        Field(29, struct.pack('<II', 15, 5)),
        Field(29, struct.pack('<II', 16, N97_MACHINE_UID))]


def rewrite(field, counts):
    if field.kind == 17:
        updated = product_prerequisite(field)
        if updated != field:
            counts['device_prerequisites'] += 1
        return updated
    if is_n97_comparison(field):
        counts['touch_profile_conditions'] += 1
        return Field(29, struct.pack('<II', 8, 0) + field.encode()
                     + Field(29, struct.pack('<II', 8, 0)
                             + exists(SYMBIAN3_MARKER).encode()
                             + exists(BELLE_MARKER).encode()).encode())
    if field.kind == 29:
        op = struct.unpack_from('<I', field.payload)[0]
        if 1 <= op <= 9 or op in (11, 12):
            return Field(29, field.payload[:8] + sequence(
                rewrite(child, counts) for child in fields(field.payload[8:])))
        return field
    if field.kind == 2:
        element_kind = struct.unpack_from('<I', field.payload)[0]
        return array(element_kind, [rewrite(child, counts)
                                   for child in array_items(field)])
    if field.kind in (13, 26, 27, 28):
        children = []
        for child in fields(field.payload):
            if field.kind == 13 and child.kind == 39:
                counts['removed_signatures'] += 1
                continue
            children.append(rewrite(child, counts))
        return Field(field.kind, sequence(children))
    return field


def product_prerequisite(field):
    arrays = fields(field.payload)
    if len(arrays) != 2:
        raise ValueError('Unexpected prerequisites layout')
    device_dependencies = array_items(arrays[0])
    dependency = fields(device_dependencies[0].payload) if device_dependencies else [
        Field(9, bytes(4)),
        Field(5, Field(4, struct.pack('<iii', 0, 0, 0)).encode()),
        array(1, [Field(1, 'S60ProductID'.encode('utf-16le'))])]
    alternatives = []
    for uid in (SYMBIAN3_PRODUCT_UID, BELLE_PRODUCT_UID):
        alternative = [Field(9, struct.pack('<I', uid)), *dependency[1:]]
        alternatives.append(Field(18, sequence(alternative)))
    return Field(17, sequence([
        array(18, alternatives), arrays[1]]))


def target_component(package):
    members = fields(package.controller.payload)
    if sum(f.kind == 17 for f in members) != 1:
        raise ValueError('Expected one component prerequisites field')
    return Package(package.uid_header, Field(13, sequence(
        product_prerequisite(f) if f.kind == 17 else f for f in members)), package.data)


def adapt_client(raw):
    if hashlib.sha256(raw).hexdigest() != CLIENT_SHA256:
        raise ValueError('Expected the pinned N97 v1.30(1541) client')
    original = Package.parse(raw)
    counts = {'touch_profile_conditions': 0, 'removed_signatures': 0,
              'device_prerequisites': 0}
    controller = rewrite(original.controller, counts)
    children = fields(controller.payload)
    if sum(child.kind == 17 for child in children) != 1:
        raise ValueError('Expected one root product prerequisite')
    info = next(child for child in children if child.kind == 14)
    members = [Field(4, struct.pack('<iii', 1, 30, 1553)) if f.kind == 4 else f
               for f in fields(info.payload[:-2])]
    children = [Field(14, sequence(members) + info.payload[-2:]) if child is info else child
                for child in children]
    if not counts['touch_profile_conditions'] or not counts['removed_signatures']:
        raise ValueError('Original controller did not match expected adaptation points')
    result = Package(original.uid_header, Field(13, sequence(children)), original.data)
    encoded = result.encode()
    if Package.parse(encoded) != result:
        raise ValueError('Output package verification failed')
    return encoded, counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('client', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    encoded, counts = adapt_client(args.client.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(encoded)
    print(f'Wrote {args.output}: {counts}')


if __name__ == '__main__':
    main()
