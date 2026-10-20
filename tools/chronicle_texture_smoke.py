#!/usr/bin/env python3
"""Run isolated PAL full-resolution texture baseline, replacement, disabled and conflict scenarios."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
import zlib
from chronicle_texture import read_snapshot

TEST='ModTextureRetail.LoadedPlayerTexturePreservesCoordinatesAndFullPixels'

def encode_png(width,height,pixels):
    raw=b''.join(b'\x00'+pixels[y*width*4:(y+1)*width*4] for y in range(height))
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')

def upscale_4k(pixels):
    # Nearest-neighbor 16x expansion of the captured 256x256 RGBA source.
    # This deliberately adds no invented detail: it tests the full-resolution path deterministically.
    if len(pixels)!=256*256*4:raise ValueError('4K fixture needs a 256x256 RGBA capture')
    rows=[b''.join(pixels[(y*256+x)*4:(y*256+x+1)*4]*16 for x in range(256)) for y in range(256)]
    return b''.join(row*16 for row in rows)

def run(build,data,output):
    build=Path(build).resolve(strict=True);data=Path(data).resolve(strict=True);output=Path(output).absolute()
    if any(p.lower()=='ps2' for p in output.parts) or any(p.lower()=='ps2' for p in output.parent.resolve(strict=True).parts):raise ValueError('PS2 output is forbidden')
    output.mkdir(exist_ok=False)
    executable=build/('darkcloud_tests.exe' if os.name=='nt' else 'darkcloud_tests')
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('DC_','GTEST_'))}
    env['DC_DATA']=str(data)
    results=[];target=None;original_pixels=None
    for name in ['baseline','replacement','upscaled4k','disabled','conflict']:
        folder=output/name;folder.mkdir()
        if target:
            ids=['audit.texture']+(['audit.conflict'] if name=='conflict' else [])
            for id in ids:
                package=folder/'mods'/id;package.mkdir(parents=True)
                if name=='upscaled4k':
                    pixels=upscale_4k(original_pixels);image=encode_png(4096,4096,pixels)
                    (folder/'expected-rgba.sha256').write_text(hashlib.sha256(pixels).hexdigest())
                else:image=encode_png(512,512,bytes([255,0,255,255])*512*512)
                (package/'image.png').write_bytes(image)
                manifest={'schema_version':1,'id':id,'name':id,'version':'1','game':'chronicle','files':{},'requires_capabilities':['assets.textures.v1'],
                          'textures':[dict(target,source='image.png')]}
                (package/'mod.json').write_text(json.dumps(manifest))
            (folder/'mods-profile.json').write_text(json.dumps({'schema_version':1,'enabled':{id:name!='disabled' for id in ids}}))
        env['DC_TEXTURE_AUDIT']=str(folder);env['DC_TEXTURE_EXPECT']='4096' if name=='upscaled4k' else '512' if name=='replacement' else '256'
        xml=folder/'tests.xml'
        with (folder/'run.log').open('wb') as log:
            process=subprocess.run([str(executable),'--gtest_filter='+TEST,'--gtest_output=xml:'+str(xml)],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=240)
        if process.returncode!=0:raise ValueError(f'{name} failed; see {folder / "run.log"}')
        report=ET.parse(xml).getroot()
        if int(report.get('tests','0'))!=1 or int(report.get('failures','0')) or int(report.get('disabled','0')) or any(node.tag=='skipped' for node in report.iter()):raise ValueError(f'{name} did not pass its retail test')
        result=json.loads((folder/'texture-result.json').read_text())
        if result['active_targets'] != (1 if name in ['replacement','upscaled4k'] else 0):raise ValueError(f'{name} activated unexpected texture targets')
        result['case']=name;results.append(result)
        if name=='baseline':
            matches=[read_snapshot(path) for path in (folder/'captures').glob('*.texture')]
            matches=[item for item in matches if item[0]['name']=='c01d03' and item[0]['width']==256 and item[0]['height']==256]
            if len(matches)!=1:raise ValueError('player texture discovery was ambiguous')
            target={key:matches[0][0][key] for key in ['name','original_sha256']};original_pixels=matches[0][1]
        print('passed: '+name,flush=True)
    (output/'summary.json').write_text(json.dumps(results,indent=2))
    return results

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--build',required=True);parser.add_argument('--data',required=True);parser.add_argument('--output',required=True);args=parser.parse_args()
    try:run(args.build,args.data,args.output)
    except (ValueError,OSError,subprocess.TimeoutExpired) as error:parser.exit(1,str(error)+'\n')
