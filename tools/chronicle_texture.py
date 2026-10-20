#!/usr/bin/env python3
"""Inspect original texture captures and export lossless RGBA PNGs; no external dependencies."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import zlib


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result: raise ValueError("duplicate snapshot field")
        result[key] = value
    return result


def read_snapshot(path):
    path = Path(path)
    if not path.is_file() or not 1 <= path.stat().st_size <= 64*1024*1024+4096:
        raise ValueError("snapshot file size exceeds limits")
    with path.open('rb') as stream:
        line = stream.readline(4097)
        if len(line) > 4096 or not line.endswith(b'\n'): raise ValueError("invalid snapshot header")
        info = json.loads(line, object_pairs_hook=unique)
        if not isinstance(info, dict) or set(info) != {'schema_version','name','width','height','original_sha256','encoding'}:
            raise ValueError("invalid snapshot fields")
        if type(info['schema_version']) is not int or info['schema_version'] != 1 or info['encoding'] != 'rgba8':
            raise ValueError("unsupported snapshot format")
        if not isinstance(info['name'],str) or not re.fullmatch(r'[a-z0-9_.-]{1,31}',info['name']): raise ValueError("invalid texture name")
        w,h=info['width'],info['height']
        if type(w) is not int or type(h) is not int or not 0<w<=8192 or not 0<h<=8192 or w*h>16*1024*1024:
            raise ValueError("snapshot dimensions exceed limits")
        pixels=stream.read(w*h*4+1)
        if len(pixels)!=w*h*4: raise ValueError("snapshot pixel length mismatch")
    digest=hashlib.sha256(f'chronicle-rgba-v1:{w}:{h}:'.encode('ascii')+pixels).hexdigest()
    if digest!=info['original_sha256']: raise ValueError("snapshot content hash mismatch")
    return info,pixels


def export_png(snapshot, output):
    info,pixels=read_snapshot(snapshot)
    output=Path(output)
    # Resolve the parent before any write, including aliases into PS2 directories.
    if any(part.lower()=='ps2' for part in output.parts):
        raise ValueError("texture export may not write within a PS2 directory")
    parent=output.parent.resolve(strict=True)
    if any(part.lower()=='ps2' for part in parent.parts):
        raise ValueError("texture export may not write within a PS2 directory")
    output=parent/output.name
    w,h=info['width'],info['height']
    raw=b''.join(b'\x00'+pixels[y*w*4:(y+1)*w*4] for y in range(h))
    def chunk(kind,data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
    with output.open('xb') as stream: stream.write(png)
    return {'name':info['name'],'original_sha256':info['original_sha256'],'source':output.name}


def create_package(snapshot, image, package_id, output):
    spec=importlib.util.spec_from_file_location('chronicle_mod',Path(__file__).with_name('chronicle_mod.py'))
    author=importlib.util.module_from_spec(spec);spec.loader.exec_module(author)
    if not author.identity(package_id):raise ValueError("invalid package ID")
    info,_=read_snapshot(snapshot)
    output=Path(output)
    if any(part.lower()=='ps2' for part in output.absolute().parts):raise ValueError("PS2 output is forbidden")
    parent=output.parent.resolve(strict=True)
    if any(part.lower()=='ps2' for part in parent.parts):raise ValueError("PS2 output is forbidden")
    output=parent/output.name
    image=Path(image)
    if not image.is_file():raise ValueError("replacement image must be a regular file")
    with image.open('rb') as stream:png=stream.read(32*1024*1024+1)
    width,height=author.validate_png_header(png[:33],len(png))
    manifest={'schema_version':1,'api_version':1,'id':package_id,'name':package_id,
              'version':'1.0.0','game':'chronicle','files':{},
              'requires_capabilities':['assets.textures.v1'],
              'textures':[{'name':info['name'],'original_sha256':info['original_sha256'],'source':'replacement.png'}]}
    output.mkdir(exist_ok=False)
    (output/'replacement.png').write_bytes(png)
    (output/'README.md').write_text(
        '# '+package_id+'\n\nTexture replacement for '+info['name']+'.\n\n'
        'Install this folder under your test save mods directory. Enable in the shared\n'
        'mod manager and restart. New packages are disabled by default. Conflicting\n'
        'target replacements are blocked; load order does not choose a winner.\n\n'
        'Original logical size: '+str(info['width'])+'x'+str(info['height'])+'.\n'
        'Replacement pixel size: '+str(width)+'x'+str(height)+'.\n'
        'The PNG is copied unchanged. This tool checks its header and limits; runtime\n'
        'activation fully decodes it. Test rendering and disable/restart in-game.\n',encoding='utf-8')
    # Publish the manifest last so an interrupted write cannot activate a partial package.
    (output/'mod.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    return {'package':str(output),'id':package_id,'target':manifest['textures'][0],
            'pixel_width':width,'pixel_height':height,'profile_unchanged':True,
            'validation':'PNG header only; runtime decoding and gameplay validation still required'}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    commands=parser.add_subparsers(dest='command',required=True)
    inspect=commands.add_parser('inspect');inspect.add_argument('snapshot')
    export=commands.add_parser('export');export.add_argument('snapshot');export.add_argument('output')
    package=commands.add_parser('package',help='create a disabled texture package from a capture and replacement PNG')
    package.add_argument('snapshot');package.add_argument('--image',required=True)
    package.add_argument('--id',required=True);package.add_argument('--output',required=True)
    args=parser.parse_args()
    try:
        if args.command=='package':value=create_package(args.snapshot,args.image,args.id,args.output)
        else:value=read_snapshot(args.snapshot)[0] if args.command=='inspect' else export_png(args.snapshot,args.output)
        print(json.dumps(value,indent=2))
    except (ValueError,OSError) as error: parser.exit(1,str(error)+'\n')

if __name__=='__main__': main()
