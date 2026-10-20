"""Stage an allowlisted Windows candidate; never certify or publish a release."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path

RUNTIME = ('darkcloud.exe', 'dcdata.exe', 'libc++.dll', 'libunwind.dll',
           'libwinpthread-1.dll', 'SDL3.dll')


def verify_runtime_dlls(build, dependencies):
    llvm = dependencies / 'llvm/llvm-mingw-20260922-ucrt-x86_64/x86_64-w64-mingw32/bin'
    originals = {name: llvm / name for name in RUNTIME[2:] if name != 'SDL3.dll'}
    originals['SDL3.dll'] = dependencies / 'sdl/SDL3-3.4.18/x86_64-w64-mingw32/bin/SDL3.dll'
    for name, original in originals.items():
        if hashlib.sha256((build / name).read_bytes()).digest() != hashlib.sha256(original.read_bytes()).digest():
            raise ValueError(f'Runtime DLL does not match the documented dependency: {name}')


def outside_ps2(path):
    path = Path(path).absolute()
    if any(part.lower() == 'ps2' for part in (*path.parts, *path.resolve().parts)):
        raise ValueError('PS2 paths are forbidden')
    return path


def add_file(files, destination, source):
    source = Path(source)
    if not source.is_file() or source.is_symlink():
        raise ValueError(f'Missing or linked input: {source}')
    if destination in files:
        raise ValueError(f'Duplicate destination: {destination}')
    files[destination] = source


def plan(source, build, dependencies):
    verify_runtime_dlls(build, dependencies)
    files = {}
    for name in RUNTIME:
        add_file(files, name, build / name)
    # Select source-controlled runtime assets, never recurse through a build tree.
    tracked = subprocess.check_output(
        ['git', '-C', str(source), 'ls-files', '-z', 'port/lang', 'port/glyphs'],
        text=True).split('\0')
    for name in filter(None, tracked):
        path = Path(name)
        if path.parent == Path('port/lang') and path.suffix == '.json':
            add_file(files, path.relative_to('port').as_posix(), source / path)
    add_file(files, 'glyphs/glyphs.json', source / 'port/glyphs/glyphs.json')
    styles = json.loads((source / 'port/glyphs/glyphs.json').read_text())['styles']
    for atlas in sorted({style['atlas'] for style in styles.values()}):
        if Path(atlas).name != atlas or '/' in atlas or '\\' in atlas or ':' in atlas:
            raise ValueError('Unsafe glyph atlas path')
        add_file(files, 'glyphs/' + atlas, source / 'port/glyphs' / atlas)
    for original, target in [('DarkCloudCompendium.ttf', 'font.ttf'),
                             ('DarkCloudCompendiumThin.ttf', 'font_thin.ttf')]:
        add_file(files, 'lang/' + target, source / 'tools/font' / original)
    for path in sorted((source / 'docs').glob('MOD*.md')):
        add_file(files, 'docs/' + path.name, path)
    for name in ('WINDOWS.md', 'PC.md', 'GLYPHS.md'):
        add_file(files, 'docs/' + name, source / 'docs' / name)
    notices = {
        'CREDITS': source / 'CREDITS',
        'Lua.txt': source / 'port/third_party/lua/LUA-LICENSE.txt',
        'Kazarov.txt': source / 'tools/kazarov/LICENSE',
        'Kazarov-retail.txt': source / 'port/third_party/kazarov-retail-LICENSE',
        'Fonts.md': source / 'tools/font/README.md',
        'Glyph-provenance.md': source / 'docs/GLYPHS.md',
        'JSON.txt': build / '_deps/nlohmann_json-src/LICENSE.MIT',
        'simdjson.txt': build / '_deps/simdjson-src/LICENSE',
        'simdjson-MIT.txt': build / '_deps/simdjson-src/LICENSE-MIT',
        'fastgltf.md': build / '_deps/fastgltf-src/LICENSE.md',
    }
    llvm = dependencies / 'llvm/llvm-mingw-20260922-ucrt-x86_64'
    notices['LLVM.txt'] = llvm / 'LICENSE.TXT'
    for name in ('COPYING', 'COPYING.MinGW-w64.txt', 'COPYING.MinGW-w64-runtime.txt',
                 'COPYING.winpthreads.txt'):
        notices['mingw/' + name] = llvm / 'x86_64-w64-mingw32/share/mingw32' / name
    notices['SDL.txt'] = dependencies / 'sdl/SDL3-3.4.18/LICENSE.txt'
    # These single-file libraries contain their notices in the actual pinned source.
    for name in ('dr_wav.h', 'dr_flac.h', 'stb_vorbis.c', 'stb_truetype.h', 'stb_image.h'):
        notices['embedded/' + name + '.txt'] = build / 'kz_port/vendor' / name
    for name, path in notices.items():
        add_file(files, 'notices/' + name, path)
    return files


def stage(files, output, provenance):
    output = outside_ps2(output)
    if output.exists():
        raise FileExistsError(output)
    if not output.parent.is_dir():
        raise ValueError('Output parent must exist')
    # Validate the complete plan before making the output directory.
    for name, path in files.items():
        if (Path(name).is_absolute() or '..' in Path(name).parts
                or any(part.lower() == 'ps2' for part in Path(name).parts) or '\\' in name
                or ':' in name or not path.is_file() or path.is_symlink()):
            raise ValueError(f'Invalid planned file: {name}')
    output.mkdir()
    manifest = {'schema_version': 1, 'release_certified': False,
                'provenance': provenance, 'files': {}}
    for name, path in sorted(files.items()):
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        manifest['files'][name] = {'bytes': target.stat().st_size,
                                 'sha256': hashlib.sha256(target.read_bytes()).hexdigest()}
    (output / 'bundle.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', required=True)
    parser.add_argument('--dependencies', required=True, help='Windows deps directory')
    parser.add_argument('--output', required=True, help='New directory under an existing parent')
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    build = Path(args.build).resolve(strict=True)
    dependencies = Path(args.dependencies).resolve(strict=True)
    cache = (build / 'CMakeCache.txt').read_text()
    home = re.search(r'^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$', cache, re.MULTILINE)
    if home is None or Path(home.group(1).strip()).resolve() != source:
        raise ValueError('Build directory belongs to a different source checkout')
    compiler_records = {}
    for language in ('C', 'CXX'):
        records = list((build / 'CMakeFiles').glob(f'*/CMake{language}Compiler.cmake'))
        if len(records) != 1:
            raise ValueError(f'Ambiguous or missing {language} compiler record')
        record = records[0].read_text()
        if not re.search(rf'set\(CMAKE_{language}_COMPILER_ID "Clang"\)', record):
            raise ValueError('Bundle requires Clang for both C and C++')
        compiler_records[language] = record
    git = lambda *args: subprocess.check_output(['git', '-C', str(source), *args], text=True).strip()
    provenance = {'checkout_commit': git('rev-parse', 'HEAD'),
                  'checkout_dirty': bool(git('status', '--porcelain')),
                  'kazarov_commit': git('-C', 'tools/kazarov', 'rev-parse', 'HEAD'),
                  'compiler_records': compiler_records,
                  'build_directory': str(build),
                  'limits': 'Checkout identity is not proof of binary provenance. Build this checkout first. '
                            'Acceptance tests and notice review remain required; see docs/MOD_FIRST_RELEASE.md '
                            'and notices/Glyph-provenance.md. No release archive is published.'}
    manifest = stage(plan(source, build, dependencies), args.output, provenance)
    print(f"Staged {len(manifest['files'])} files; release certification remains pending.")


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
