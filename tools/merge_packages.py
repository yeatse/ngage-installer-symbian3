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


def root_block(package):
    return fields(next(f for f in fields(package.controller.payload) if f.kind == 28).payload)


def merge_files(parent, children):
    """Install each child's files as part of the parent package instead of an embedded one."""
    parent_fields = fields(parent.controller.payload)
    if next(f for f in parent_fields if f.kind == 40).payload != bytes(4):
        raise ValueError('Parent data unit must be zero')
    units = data_units(parent)
    streams = array_items(fields(units[0].payload)[0])
    descriptions = []
    for child in children:
        members = fields(child.controller.payload)
        if any(f.kind == 39 for f in members):
            raise ValueError('Cannot merge a signed child controller')
        files, embedded, conditions = root_block(child)
        if array_items(embedded) or array_items(conditions):
            raise ValueError('Child components must install files unconditionally')
        child_units = data_units(child)
        if len(child_units) != 1:
            raise ValueError('Child components must have one data unit')
        child_streams = array_items(fields(child_units[0].payload)[0])
        for description in array_items(files):
            index = struct.unpack_from('<I', description.payload, len(description.payload) - 4)[0]
            if index >= len(child_streams):
                raise ValueError('Child file index outside its data unit')
            payload = description.payload[:-4] + struct.pack('<I', len(streams) + index)
            descriptions.append(Field(24, payload))
        streams = streams + child_streams
    units[0] = Field(31, array(32, streams).encode())
    def extend(block):
        parts = fields(block.payload)
        if len(parts) != 3:
            raise ValueError('Unexpected installation block layout')
        parts[0] = array(24, array_items(parts[0]) + descriptions)
        return Field(28, sequence(parts))
    updated = replace_block(parent, extend)
    result = Package(updated.uid_header, updated.controller,
                     Field(30, array(31, units).encode()))
    if Package.parse(result.encode()) != result:
        raise ValueError('Merged package failed verification')
    return result
