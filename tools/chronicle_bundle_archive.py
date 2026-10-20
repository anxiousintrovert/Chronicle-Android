"""Verify a staged bundle and create a deterministic local candidate ZIP, not a release."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile

from chronicle_windows_bundle import outside_ps2


def digest(data):
    return hashlib.sha256(data).hexdigest()


def verified_files(bundle):
    bundle = Path(bundle).resolve(strict=True)
    manifest_path = bundle / 'bundle.json'
    if manifest_path.is_symlink():
        raise ValueError('Linked manifest is not allowed')
    raw = manifest_path.read_bytes()
    manifest = json.loads(raw)
    if manifest.get('schema_version') != 1 or not isinstance(manifest.get('files'), dict):
        raise ValueError('Unsupported bundle manifest')
    result = {'bundle.json': raw}
    folded = {'bundle.json'}
    for name, expected in manifest['files'].items():
        path = PurePosixPath(name)
        if (not name or path.is_absolute() or path.as_posix() != name or
                any(part in ('.', '..') or part.lower() == 'ps2' for part in path.parts) or
                '\\' in name or ':' in name or name.casefold() in folded):
            raise ValueError(f'Unsafe or duplicate manifest path: {name}')
        folded.add(name.casefold())
        source = bundle / name
        if any((bundle.joinpath(*path.parts[:i])).is_symlink() for i in range(1, len(path.parts) + 1)):
            raise ValueError(f'Linked bundle input: {name}')
        if not source.resolve(strict=True).is_relative_to(bundle):
            raise ValueError(f'Escaped bundle input: {name}')
        content = source.read_bytes()
        if len(content) != expected['bytes'] or digest(content) != expected['sha256']:
            raise ValueError(f'Bundle content changed: {name}')
        result[name] = content
    actual = set()
    for path in bundle.rglob('*'):
        if path.is_symlink():
            raise ValueError(f'Linked bundle entry: {path}')
        if path.is_file():
            actual.add(path.relative_to(bundle).as_posix())
    if actual != set(result):
        raise ValueError('Bundle contains unlisted or missing files')
    return result


def archive(bundle, output):
    bundle = Path(bundle).resolve(strict=True)
    output = outside_ps2(output)
    checksum = output.with_name(output.name + '.sha256')
    if output.resolve().is_relative_to(bundle):
        raise ValueError('Archive must be outside the bundle')
    if not output.parent.is_dir() or output.exists() or checksum.exists():
        raise ValueError('Output parent must exist; archive and checksum must be new')
    # Retain verified bytes so source changes during ZIP writing cannot change the archive.
    files = verified_files(bundle)
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zipped:
        for name, data in sorted(files.items()):
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.create_system = 3
            entry.external_attr = 0o100644 << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            zipped.writestr(entry, data, compresslevel=9)
    with zipfile.ZipFile(output) as zipped:
        if zipped.testzip() is not None or set(zipped.namelist()) != set(files):
            raise ValueError('Archive verification failed; do not distribute it')
        for name, data in files.items():
            if zipped.read(name) != data:
                raise ValueError(f'Archive readback mismatch: {name}')
    value = digest(output.read_bytes())
    with checksum.open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(value + '  ' + output.name + '\n')
    return {'archive': str(output), 'sha256': value, 'files': len(files),
            'release_certified': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(archive(args.bundle, args.output), indent=2))
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
        parser.exit(1, str(error) + '\n')
