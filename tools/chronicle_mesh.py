#!/usr/bin/env python3
"""Offline triangle-mesh preparation; does not install or enable a mod."""
import argparse
import json
import math
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


def write_new(mesh, output):
    payload = (json.dumps(validate(mesh),allow_nan=False,separators=(',',':'))+'\n').encode('utf-8')
    require(len(payload) <= MAX_BYTES, 'converted mesh exceeds 16 MiB')
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command',required=True)
    convert = commands.add_parser('obj'); convert.add_argument('input',type=Path); convert.add_argument('output',type=Path)
    check = commands.add_parser('inspect'); check.add_argument('input',type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'obj':
            output = write_new(from_obj(args.input),args.output)
            print(json.dumps({'output':str(output),**inspect(output)},indent=2))
        else:
            print(json.dumps(inspect(args.input),indent=2))
        return 0
    except (ValueError,OSError,UnicodeError,RecursionError) as error:
        parser.exit(1,str(error)+'\n')

if __name__ == '__main__':
    main()
