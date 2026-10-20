#!/usr/bin/env python3
"""Offline triangle-mesh preparation; does not install or enable a mod."""
import argparse
import json
import math
import re
import os
import struct
from pathlib import Path
import tempfile

MAX_BYTES = 16 * 1024 * 1024
MAX_VERTICES = 100000
MAX_TRIANGLES = 200000


def require(condition, message):
    if not condition:
        raise ValueError(message)


def finite(value, bound=1000000):
    return type(value) in (int, float) and abs(value) <= bound and math.isfinite(value)


def validate(mesh):
    require(isinstance(mesh, dict) and set(mesh) == {'schema_version', 'vertices', 'triangles'}, 'invalid mesh fields')
    require(type(mesh['schema_version']) is int and mesh['schema_version'] == 1, 'unsupported mesh schema')
    vertices, triangles = mesh['vertices'], mesh['triangles']
    require(isinstance(vertices, list) and 3 <= len(vertices) <= MAX_VERTICES, 'vertex count must be 3..100000')
    require(isinstance(triangles, list) and 1 <= len(triangles) <= MAX_TRIANGLES, 'triangle count must be 1..200000')
    renderer_vertices = []
    for vertex in vertices:
        require(isinstance(vertex, list) and len(vertex) == 8 and all(finite(x) for x in vertex), 'vertex requires bounded finite position, normal and UV')
        renderer_vertex = list(struct.unpack('<8f', struct.pack('<8f', *vertex)))
        renderer_vertices.append(renderer_vertex)
        length = math.sqrt(sum(x*x for x in renderer_vertex[3:6]))
        require(abs(length - 1) <= 0.001, 'vertex normal must be unit length')
    for triangle in triangles:
        require(isinstance(triangle, list) and len(triangle) == 4 and all(type(x) is int for x in triangle), 'triangle requires integer strip,a,b,c')
        strip, *indices = triangle
        require(0 <= strip <= 255 and all(0 <= i < len(vertices) for i in indices), 'triangle strip or vertex index out of range')
        require(len(set(indices)) == 3, 'triangle repeats a vertex')
        a,b,c = [renderer_vertices[i][:3] for i in indices]
        u,v = [[point[k]-a[k] for k in range(3)] for point in (b,c)]
        cross = [u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
        require(sum(x*x for x in cross) > 1e-20, 'triangle degenerates at renderer precision')
    return mesh


def read_text(path):
    path = Path(path)
    with path.open('rb') as stream:
        raw = stream.read(MAX_BYTES + 1)
    require(0 < len(raw) <= MAX_BYTES, 'mesh source must be 1 byte..16 MiB')
    return raw.decode('utf-8-sig', errors='strict')


def inspect(path):
    def unique(pairs):
        result = {}
        for key,value in pairs:
            require(key not in result, 'duplicate mesh field: ' + key)
            result[key] = value
        return result
    mesh = validate(json.loads(read_text(path), object_pairs_hook=unique))
    return {'schema_version': 1, 'vertices': len(mesh['vertices']), 'triangles': len(mesh['triangles']),
            'strips': sorted({t[0] for t in mesh['triangles']}),
            'bounds': {'min': [min(v[a] for v in mesh['vertices']) for a in range(3)],
                       'max': [max(v[a] for v in mesh['vertices']) for a in range(3)]}}


def from_obj(path):
    positions, normals, uvs, vertices, triangles, known = [], [], [], [], [], {}
    strip = 0
    def index(text, source):
        try:
            number = int(text)
        except ValueError:
            raise ValueError('OBJ index must be an integer') from None
        require(number != 0, 'OBJ indices cannot be zero')
        result = number - 1 if number > 0 else len(source) + number
        require(0 <= result < len(source), 'OBJ index outside data available at this face')
        return result
    def numbers(parts, count):
        require(len(parts) == count, 'unexpected OBJ attribute components')
        values = [float(x) for x in parts]
        require(all(finite(x) for x in values), 'OBJ attribute must be finite and bounded')
        return values
    for line_number, line in enumerate(read_text(path).splitlines(), 1):
        parts = line.split('#',1)[0].split()
        if not parts:
            continue
        kind, args = parts[0], parts[1:]
        try:
            if kind == 'v': positions.append(numbers(args,3))
            elif kind == 'vt': uvs.append(numbers(args,2))
            elif kind == 'vn':
                normal = numbers(args,3); length = math.sqrt(sum(x*x for x in normal))
                require(length > 1e-10, 'zero OBJ normal')
                normals.append([x/length for x in normal])
            elif kind == 'usemtl':
                require(len(args) == 1 and args[0].startswith('strip') and args[0][5:].isascii() and args[0][5:].isdigit(), 'OBJ materials must be strip0..strip255')
                strip = int(args[0][5:]); require(strip <= 255, 'OBJ strip out of range')
            elif kind == 'f':
                require(len(args) == 3, 'triangulate OBJ faces before import')
                refs = []
                for arg in args:
                    fields = arg.split('/')
                    require(1 <= len(fields) <= 3 and fields[0], 'invalid OBJ face reference')
                    refs.append((index(fields[0],positions), index(fields[1],uvs) if len(fields)>1 and fields[1] else None,
                                 index(fields[2],normals) if len(fields)>2 and fields[2] else None))
                a,b,c = [positions[r[0]] for r in refs]
                u,v = [[point[k]-a[k] for k in range(3)] for point in (b,c)]
                face = [u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
                length = math.sqrt(sum(x*x for x in face)); require(length > 1e-10,'degenerate OBJ triangle')
                face = [x/length for x in face]
                result = [strip]
                for pi,ti,ni in refs:
                    vertex = positions[pi] + (normals[ni] if ni is not None else face) + (uvs[ti] if ti is not None else [0.0,0.0])
                    key = tuple(vertex)
                    if key not in known:
                        known[key] = len(vertices); vertices.append(vertex)
                    result.append(known[key])
                triangles.append(result)
            elif kind not in ('o','g','s','mtllib'):
                raise ValueError('unsupported OBJ record: ' + kind)
            require(max(len(positions),len(normals),len(uvs),len(vertices)) <= MAX_VERTICES and len(triangles) <= MAX_TRIANGLES, 'OBJ exceeds mesh limits')
        except (ValueError, OverflowError) as error:
            raise ValueError(f'OBJ line {line_number}: {error}') from None
    return validate({'schema_version':1,'vertices':vertices,'triangles':triangles})


def from_glb(path, mesh_index=0):
    """Import selected mesh-local geometry, never scene transforms or external files."""
    with Path(path).open('rb') as stream:
        raw = stream.read(MAX_BYTES + 1)
    require(20 <= len(raw) <= MAX_BYTES, 'GLB source must be 20 bytes..16 MiB')
    magic, version, length = struct.unpack_from('<III', raw)
    require(magic == 0x46546c67 and version == 2 and length == len(raw), 'invalid GLB 2 header/length')
    chunks = []
    offset = 12
    while offset < len(raw):
        require(offset + 8 <= len(raw), 'truncated GLB chunk header')
        size, kind = struct.unpack_from('<II', raw, offset)
        offset += 8
        require(size % 4 == 0 and offset + size <= len(raw), 'invalid GLB chunk length/alignment')
        chunks.append((kind, raw[offset:offset + size])); offset += size
    require(len(chunks) == 2 and chunks[0][0] == 0x4e4f534a and chunks[1][0] == 0x004e4942,
            'GLB requires exactly JSON then BIN chunks')
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, 'duplicate GLB JSON field: ' + key)
            result[key] = value
        return result
    doc = json.loads(chunks[0][1].decode('utf-8'), object_pairs_hook=unique)
    require(isinstance(doc, dict) and isinstance(doc.get('asset'), dict) and doc['asset'].get('version') == '2.0', 'GLB requires glTF 2.0')
    require(doc['asset'].get('minVersion', '2.0') == '2.0', 'unsupported minimum glTF version')
    require(not doc.get('extensionsRequired') and not doc.get('extensionsUsed'), 'GLB extensions are unsupported')
    require(not doc.get('skins') and not doc.get('animations'), 'GLB skin/animation import is unsupported; export geometry only')
    def integer(value, low, high, label):
        require(type(value) is int and low <= value <= high, 'invalid GLB ' + label)
        return value
    def entry(values, index, label):
        require(isinstance(values, list), 'invalid GLB ' + label + ' array')
        index = integer(index, 0, len(values)-1, label + ' index')
        require(isinstance(values[index], dict), 'invalid GLB ' + label)
        require(not values[index].get('extensions'), 'GLB extensions are unsupported')
        return values[index]
    buffers = doc.get('buffers')
    buffer = entry(buffers, 0, 'buffer')
    require(len(buffers) == 1 and 'uri' not in buffer, 'GLB requires one embedded buffer; external/data URIs are unsupported')
    binary = chunks[1][1]
    size = integer(buffer.get('byteLength'), 1, len(binary), 'buffer length')
    require(len(binary)-size <= 3, 'invalid GLB BIN padding')
    binary = binary[:size]
    def accessor(index, shape, types, limit, attribute=False):
        item = entry(doc.get('accessors'), index, 'accessor')
        require('sparse' not in item, 'sparse GLB accessors are unsupported')
        require(item.get('type') == shape, 'unexpected GLB accessor shape')
        component = item.get('componentType')
        require(type(component) is int and component in types, 'unsupported GLB component type')
        normalized = item.get('normalized', False)
        require(type(normalized) is bool and (not normalized or component in (5121,5123)), 'invalid GLB normalization')
        count = integer(item.get('count'), 1, limit, 'accessor count')
        view = entry(doc.get('bufferViews'), item.get('bufferView'), 'buffer view')
        require(type(view.get('buffer')) is int and view['buffer'] == 0, 'invalid GLB buffer reference')
        start = integer(view.get('byteOffset', 0), 0, len(binary), 'view offset')
        length = integer(view.get('byteLength'), 1, len(binary)-start, 'view length')
        local = integer(item.get('byteOffset', 0), 0, length, 'accessor offset')
        fmt, width = {5121:('B',1),5123:('H',2),5125:('I',4),5126:('f',4)}[component]
        dimensions = {'SCALAR':1,'VEC2':2,'VEC3':3}[shape]
        packed = width*dimensions
        stride = view.get('byteStride', packed)
        integer(stride, packed, 252 if 'byteStride' in view else packed, 'stride')
        require('byteStride' not in view or (attribute and stride % 4 == 0), 'invalid GLB interleaved stride')
        require(local % width == 0 and (start+local) % width == 0 and (not attribute or (local % 4 == 0 and stride % 4 == 0)), 'misaligned GLB accessor')
        require(local+(count-1)*stride+packed <= length, 'GLB accessor exceeds buffer view')
        result = [list(struct.unpack_from('<'+fmt*dimensions, binary, start+local+i*stride)) for i in range(count)]
        if normalized:
            divisor = 255 if component == 5121 else 65535
            result = [[v/divisor for v in row] for row in result]
        return result
    selected = entry(doc.get('meshes'), mesh_index, 'mesh')
    require('weights' not in selected, 'GLB morph weights are unsupported')
    primitives = selected.get('primitives')
    require(isinstance(primitives, list) and 1 <= len(primitives) <= 256, 'GLB needs 1..256 primitives')
    vertices, triangles, known = [], [], {}
    source_vertices = 0
    for primitive in primitives:
        require(isinstance(primitive, dict), 'invalid GLB primitive')
        require(not primitive.get('extensions') and 'targets' not in primitive, 'GLB primitive extensions/morph targets are unsupported')
        require(type(primitive.get('mode',4)) is int and primitive.get('mode',4) == 4, 'GLB requires triangle primitives')
        attributes = primitive.get('attributes')
        require(isinstance(attributes, dict) and 'POSITION' in attributes and set(attributes) <= {'POSITION','NORMAL','TEXCOORD_0'}, 'unsupported GLB vertex attributes')
        positions = accessor(attributes['POSITION'], 'VEC3', (5126,), MAX_VERTICES, True)
        source_vertices += len(positions)
        require(source_vertices <= MAX_VERTICES, 'GLB source exceeds vertex budget')
        normals = accessor(attributes['NORMAL'], 'VEC3', (5126,), MAX_VERTICES, True) if 'NORMAL' in attributes else None
        uvs = accessor(attributes['TEXCOORD_0'], 'VEC2', (5126,5121,5123), MAX_VERTICES, True) if 'TEXCOORD_0' in attributes else None
        for values in (normals, uvs):
            require(values is None or len(values) == len(positions), 'GLB attribute counts differ')
        require(all(all(finite(x) for x in row) for values in (positions,normals,uvs) if values is not None for row in values), 'GLB attributes must be bounded and finite')
        if 'TEXCOORD_0' in attributes:
            uv_accessor = entry(doc.get('accessors'), attributes['TEXCOORD_0'], 'accessor')
            require(uv_accessor['componentType'] == 5126 or uv_accessor.get('normalized') is True, 'integer GLB UVs must be normalized')
        indices = [x[0] for x in accessor(primitive['indices'], 'SCALAR', (5121,5123,5125), MAX_TRIANGLES*3)] if 'indices' in primitive else list(range(len(positions)))
        if 'indices' in primitive:
            require(not entry(doc.get('accessors'), primitive['indices'], 'accessor').get('normalized',False), 'GLB indices cannot be normalized')
        require(len(indices) % 3 == 0 and len(triangles)+len(indices)//3 <= MAX_TRIANGLES, 'GLB triangle count invalid/exceeds budget')
        require(all(type(i) is int and 0 <= i < len(positions) for i in indices), 'GLB index outside vertex data')
        strip = 0
        if 'material' in primitive:
            material = entry(doc.get('materials'), primitive['material'], 'material')
            name = material.get('name','')
            require(isinstance(name,str) and re.fullmatch(r'strip(?:0|[1-9][0-9]{0,2})',name) is not None, 'GLB materials must be named strip0..strip255')
            strip = integer(int(name[5:]),0,255,'material strip')
        for offset in range(0,len(indices),3):
            face_indices = indices[offset:offset+3]
            a,b,c = [positions[i] for i in face_indices]
            u,v = [[p[k]-a[k] for k in range(3)] for p in (b,c)]
            face = [u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
            length = math.sqrt(sum(x*x for x in face))
            require(length > 1e-10, 'degenerate GLB triangle')
            face = [x/length for x in face]
            triangle = [strip]
            for i in face_indices:
                normal = normals[i] if normals is not None else face
                length = math.sqrt(sum(x*x for x in normal))
                require(length > 1e-10, 'zero GLB normal')
                vertex = positions[i]+[x/length for x in normal]+(uvs[i] if uvs is not None else [0.0,0.0])
                key = tuple(vertex)
                if key not in known:
                    require(len(vertices)<MAX_VERTICES, 'GLB converted vertex budget exceeded')
                    known[key]=len(vertices);vertices.append(vertex)
                triangle.append(known[key])
            triangles.append(triangle)
    return validate({'schema_version':1,'vertices':vertices,'triangles':triangles})


def write_new(mesh, output):
    payload = (json.dumps(validate(mesh),allow_nan=False,separators=(',',':'))+'\n').encode('utf-8')
    return write_payload(payload, output)


def write_payload(payload, output):
    require(len(payload) <= MAX_BYTES, 'converted mesh exceeds 16 MiB')
    require(not any(part.lower() == 'ps2' for part in Path(output).parts), 'PS2 directories are read-only')
    output = Path(output).resolve()
    require(not any(part.lower() == 'ps2' for part in output.parts), 'PS2 directories are read-only')
    # Publish an already-complete file without overwriting an existing asset.
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, prefix='.chronicle-mesh-', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.link(temporary, output)
    finally:
        if temporary is not None:
            temporary.unlink()
    return output


def read_snapshot(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, 'duplicate snapshot field: ' + key)
            result[key] = value
        return result
    snapshot = json.loads(read_text(path), object_pairs_hook=unique)
    fields = {'schema_version', 'native_vertices', 'textures', 'mesh', 'fallback_normals', 'dropped_degenerate_triangles'}
    require(isinstance(snapshot, dict) and set(snapshot) == fields, 'invalid model snapshot fields')
    require(type(snapshot['schema_version']) is int and snapshot['schema_version'] == 1, 'unsupported model snapshot schema')
    require(type(snapshot['native_vertices']) is int and 1 <= snapshot['native_vertices'] <= MAX_VERTICES, 'invalid native vertex count')
    textures = snapshot['textures']
    require(isinstance(textures, list) and len(textures) <= 256 and all(isinstance(x, str) and 1 <= len(x) <= 32 for x in textures), 'invalid snapshot texture names')
    for key, limit in [('fallback_normals', MAX_VERTICES), ('dropped_degenerate_triangles', MAX_TRIANGLES)]:
        require(type(snapshot[key]) is int and 0 <= snapshot[key] <= limit, 'invalid snapshot export count')
    validate(snapshot['mesh'])
    return snapshot


def targets(directory):
    directory = Path(directory)
    require(directory.is_dir(), 'model discovery directory does not exist')
    paths = sorted(directory.glob('*.model.json'))
    require(len(paths) <= 256, 'discovery directory exceeds 256 snapshots')
    result = []
    for path in paths:
        snapshot = read_snapshot(path)
        candidates = sorted({t.lower() for t in snapshot['textures'] if re.fullmatch(r'[A-Za-z0-9_.-]{1,31}', t)})
        result.append({'snapshot': path.name, 'native_vertices': snapshot['native_vertices'],
                       'textures': snapshot['textures'], 'targetable_textures': candidates,
                       'vertices': len(snapshot['mesh']['vertices']), 'triangles': len(snapshot['mesh']['triangles']),
                       'fallback_normals': snapshot['fallback_normals'],
                       'dropped_degenerate_triangles': snapshot['dropped_degenerate_triangles']})
    return result


def export_obj(snapshot, output):
    geometry = snapshot['mesh']
    validate(geometry)
    lines = ['# Chronicle discovered geometry; native target metadata stays in the model snapshot.']
    number = lambda x: format(x, '.9g')
    for vertex in geometry['vertices']:
        lines.append('v ' + ' '.join(number(x) for x in vertex[:3]))
    for vertex in geometry['vertices']:
        lines.append('vt ' + ' '.join(number(x) for x in vertex[6:]))
    for vertex in geometry['vertices']:
        lines.append('vn ' + ' '.join(number(x) for x in vertex[3:6]))
    previous = None
    for strip, *indices in geometry['triangles']:
        if strip != previous:
            lines.append('usemtl strip' + str(strip))
            previous = strip
        lines.append('f ' + ' '.join(f'{i+1}/{i+1}/{i+1}' for i in indices))
    return write_payload(('\n'.join(lines) + '\n').encode('utf-8'), output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command',required=True)
    convert = commands.add_parser('obj'); convert.add_argument('input',type=Path); convert.add_argument('output',type=Path)
    glb = commands.add_parser('glb'); glb.add_argument('input',type=Path); glb.add_argument('output',type=Path); glb.add_argument('--mesh',type=int,default=0)
    check = commands.add_parser('inspect'); check.add_argument('input',type=Path)
    listing = commands.add_parser('targets'); listing.add_argument('directory', type=Path)
    for name in ('extract', 'export-obj'):
        command = commands.add_parser(name); command.add_argument('input', type=Path); command.add_argument('output', type=Path)
    args = parser.parse_args()
    try:
        if args.command in ('obj', 'glb'):
            geometry = from_obj(args.input) if args.command == 'obj' else from_glb(args.input, args.mesh)
            output = write_new(geometry,args.output)
            print(json.dumps({'output':str(output),**inspect(output)},indent=2))
        elif args.command == 'targets':
            print(json.dumps(targets(args.directory), indent=2))
        elif args.command in ('extract', 'export-obj'):
            snapshot = read_snapshot(args.input)
            output = write_new(snapshot['mesh'], args.output) if args.command == 'extract' else export_obj(snapshot, args.output)
            print(json.dumps({'output': str(output), 'native_vertices': snapshot['native_vertices'], 'textures': snapshot['textures']}, indent=2))
        else:
            print(json.dumps(inspect(args.input),indent=2))
        return 0
    except (ValueError,OSError,UnicodeError,RecursionError) as error:
        parser.exit(1,str(error)+'\n')

if __name__ == '__main__':
    main()
