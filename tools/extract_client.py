#!/usr/bin/env python3
"""Extract the embedded client SISX without executing the N97 installer."""
import argparse
from pathlib import Path
import struct

from sis import Package, array_items, decompress, fields


def descendants(field):
    yield field
    if field.kind == 2:
        children = array_items(field)
    elif field.kind in (13, 26, 27, 28):
        children = fields(field.payload)
    else:
        children = []
    for child in children:
        yield from descendants(child)


def extract_client(package):
    matches = []
    for field in descendants(package.controller):
        if field.kind != 24:
            continue
        if len(field.payload) < 28:
            raise ValueError('Truncated SISFileDescription')
        metadata = fields(field.payload[:-28])
        if not metadata or metadata[0].kind != 1:
            raise ValueError('Missing file target')
        target = metadata[0].payload.decode('utf-16le').replace('\\', '/').lower()
        if target == 'd:/private/2000a2ae/n-gage.sisx':
            matches.append(struct.unpack_from('<I', field.payload, len(field.payload) - 4)[0])
    if len(matches) != 1:
        raise ValueError('Expected exactly one embedded N-Gage client')
    indices = [f for f in fields(package.controller.payload) if f.kind == 40]
    if len(indices) != 1 or indices[0].payload != bytes(4):
        raise ValueError('Unexpected controller data unit index')
    data_arrays = fields(package.data.payload)
    if len(data_arrays) != 1 or data_arrays[0].kind != 2:
        raise ValueError('Unexpected SISData layout')
    units = array_items(data_arrays[0])
    if not units or units[0].kind != 31:
        raise ValueError('Missing data unit')
    unit_arrays = fields(units[0].payload)
    if len(unit_arrays) != 1 or unit_arrays[0].kind != 2:
        raise ValueError('Unexpected SISDataUnit layout')
    files = array_items(unit_arrays[0])
    file = files[matches[0]]
    if file.kind != 32:
        raise ValueError('Expected SISFileData')
    streams = fields(file.payload)
    if len(streams) != 1:
        raise ValueError('Unexpected SISFileData layout')
    raw = decompress(streams[0])
    Package.parse(raw)
    return raw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('installer', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    client = extract_client(Package.parse(args.installer.read_bytes()))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(client)
    print(f'Extracted and validated {len(client)} bytes to {args.output}')


if __name__ == '__main__':
    main()
