"""Compose unsigned component controllers without copying proprietary payloads."""
import struct
from sis import Field, Package, array, array_items, fields, sequence


def replace_block(package, transform):
    children = fields(package.controller.payload)
    if sum(f.kind == 28 for f in children) != 1:
        raise ValueError('Expected one root installation block')
    controller = Field(13, sequence(transform(f) if f.kind == 28 else f for f in children))
    return Package(package.uid_header, controller, package.data)


def retarget_drive(package, drive):
    def visit(f):
        if f.kind == 24:
            metadata = fields(f.payload[:-28])
            target = metadata[0].payload.decode('utf-16le')
            if target.startswith('!:'):
                metadata[0] = Field(1, (drive + target[1:]).encode('utf-16le'))
            return Field(24, sequence(metadata) + f.payload[-28:])
        if f.kind == 2:
            kind = struct.unpack_from('<I', f.payload)[0]
            return array(kind, [visit(c) for c in array_items(f)])
        if f.kind in (13, 26, 27, 28):
            return Field(f.kind, sequence(visit(c) for c in fields(f.payload)))
        return f
    return Package(package.uid_header, visit(package.controller), package.data)


def guard_missing_files(package, paths):
    from adapt_client import exists
    condition = None
    for path in paths:
        missing = Field(29, struct.pack('<II', 9, 0) + exists(path).encode())
        condition = missing if condition is None else Field(
            29, struct.pack('<II', 7, 0) + condition.encode() + missing.encode())
    if condition is None:
        raise ValueError('Expected at least one guarded path')
    def wrap(block):
        branch = Field(26, condition.encode() + block.encode() + array(27, []).encode())
        return Field(28, sequence([array(24, []), array(13, []), array(26, [branch])]))
    return replace_block(package, wrap)


def data_units(package):
    streams = fields(package.data.payload)
    if len(streams) != 1 or streams[0].kind != 2:
        raise ValueError('Unexpected SISData layout')
    return array_items(streams[0])


def merge_embedded(parent, children):
    parent_fields = fields(parent.controller.payload)
    if next(f for f in parent_fields if f.kind == 40).payload != bytes(4):
        raise ValueError('Parent data unit must be zero')
    units = data_units(parent)
    controllers = []
    for child in children:
        members = fields(child.controller.payload)
        if next(f for f in members if f.kind == 40).payload != bytes(4):
            raise ValueError('Child data unit must start at zero')
        if any(f.kind == 39 for f in members):
            raise ValueError('Cannot merge a signed child controller')
        members = [Field(40, struct.pack('<I', len(units))) if f.kind == 40 else f
                   for f in members]
        controllers.append(Field(13, sequence(members)))
        units.extend(data_units(child))
    def extend(block):
        parts = fields(block.payload)
        if len(parts) != 3:
            raise ValueError('Unexpected installation block layout')
        old_children = array_items(parts[1])
        parts[1] = array(13, old_children + controllers)
        return Field(28, sequence(parts))
    updated = replace_block(parent, extend)
    result = Package(updated.uid_header, updated.controller,
                     Field(30, array(31, units).encode()))
    if Package.parse(result.encode()) != result:
        raise ValueError('Merged package failed verification')
    return result
