"""Compose unsigned component controllers without copying proprietary payloads."""
import struct
from sis import Field, Package, array, array_items, fields, sequence


def replace_block(package, transform):
    children = fields(package.controller.payload)
    if sum(f.kind == 28 for f in children) != 1:
        raise ValueError('Expected one root installation block')
    controller = Field(13, sequence(transform(f) if f.kind == 28 else f for f in children))
    return Package(package.uid_header, controller, package.data)


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
