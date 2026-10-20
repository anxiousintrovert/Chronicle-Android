"""Generate checked port copies of native headers/sources; never modify inputs."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import tempfile


class OverlayError(ValueError):
    pass


def relative_path(value):
    if not isinstance(value, str) or not value or "\\" in value or ":" in value:
        raise OverlayError("overlay paths must be relative POSIX paths")
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in (".", "..") for part in value.split("/")):
        raise OverlayError("overlay path escapes its tree")
    if str(path) != value or path.parts[0] not in ("include", "src"):
        raise OverlayError("overlay paths must be canonical paths under include/ or src/")
    return path


def linked(path):
    return path.is_symlink() or getattr(path, "is_junction", lambda: False)()


def canonical(data):
    return data.decode("utf-8").replace("\r\n", "\n")


def digest(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def prepare(source, manifest):
    """Validate every input before returning any output bytes."""
    if not isinstance(manifest, dict) or set(manifest) != {"schema_version", "files"}:
        raise OverlayError("manifest requires exactly schema_version and files")
    if type(manifest["schema_version"]) is not int or manifest["schema_version"] != 1:
        raise OverlayError("unsupported overlay schema")
    if not isinstance(manifest["files"], dict):
        raise OverlayError("files must be an object")
    result = {}
    for tree in ("include", "src"):
        root = source / tree
        if not root.is_dir() or linked(root):
            raise OverlayError(f"missing or linked native tree: {tree}")
        for base, directories, files in os.walk(root, followlinks=False):
            for name in directories + files:
                path = Path(base) / name
                if linked(path):
                    raise OverlayError(f"linked native input: {path}")
            for name in files:
                path = Path(base) / name
                result[path.relative_to(source).as_posix()] = path.read_bytes()
    for name, specification in manifest["files"].items():
        relative_path(name)
        if name not in result:
            raise OverlayError(f"missing patch input: {name}")
        if not isinstance(specification, dict) or set(specification) != {"sha256", "replacements"}:
            raise OverlayError(f"invalid patch specification: {name}")
        text = canonical(result[name])
        if digest(text) != specification["sha256"]:
            raise OverlayError(f"native source changed: {name}; review and repin the overlay")
        replacements = specification["replacements"]
        if not isinstance(replacements, list) or not replacements:
            raise OverlayError(f"empty replacement list: {name}")
        for replacement in replacements:
            if not isinstance(replacement, dict) or set(replacement) != {"old", "new", "count"}:
                raise OverlayError(f"invalid replacement: {name}")
            old, new, count = (replacement[key] for key in ("old", "new", "count"))
            if (not isinstance(old, str) or not old or not isinstance(new, str)
                    or type(count) is not int or count < 1 or "\r" in old or "\r" in new):
                raise OverlayError(f"invalid replacement values: {name}")
            if text.count(old) != count:
                raise OverlayError(f"replacement count mismatch: {name}: expected {count}, got {text.count(old)}")
            text = text.replace(old, new)
        result[name] = text.encode("utf-8")
    return result


def generate(source, output, manifest, check=False):
    source, output = Path(source).absolute(), Path(output).absolute()
    for path in (source, output):
        if any(linked(parent) for parent in (path, *path.parents)):
            raise OverlayError("overlay roots must not traverse symlinks or junctions")
    source, output = source.resolve(), output.resolve()
    if output == source or output.is_relative_to(source) or source.is_relative_to(output):
        raise OverlayError("input and output trees must not overlap")
    if any(part.casefold() == "ps2" for part in output.parts):
        raise OverlayError("protected ps2 directories cannot contain overlay outputs")
    payloads = prepare(source, manifest)
    for name in payloads:
        relative_path(name)
        if any(part.casefold() == "ps2" for part in PurePosixPath(name).parts):
            raise OverlayError("protected ps2 directories cannot be generated in an overlay")
    index = output / "overlay-files.json"
    if output.exists():
        if not index.is_file() or linked(index):
            raise OverlayError("existing output requires an overlay ownership index")
        previous = json.loads(index.read_text(encoding="utf-8"))
        if not isinstance(previous, list) or any(not isinstance(name, str) for name in previous):
            raise OverlayError("invalid overlay ownership index")
        for name in previous:
            relative_path(name)
        # Validate all destinations before writing, including old managed files.
        for name in set(previous) | payloads.keys():
            path = output / name
            if any(linked(parent) for parent in (path, *path.parents)):
                raise OverlayError(f"linked overlay destination: {name}")
            if path.exists() and not path.is_file():
                raise OverlayError(f"non-file overlay destination: {name}")
            if path.exists() and name not in previous:
                raise OverlayError(f"refusing to overwrite an unmanaged output: {name}")
    else:
        previous = []
    if check:
        return len(payloads)
    output.mkdir(parents=True, exist_ok=True)
    for name, data in payloads.items():
        path = output / name
        if path.is_file() and path.read_bytes() == data:
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        descriptor, temporary = tempfile.mkstemp(prefix=".overlay-", dir=path.parent)
        try:
            with os.fdopen(descriptor, "wb") as stream:
                stream.write(data)
            os.replace(temporary, path)
        finally:
            Path(temporary).unlink(missing_ok=True)
    for name in set(previous) - payloads.keys():
        (output / name).unlink(missing_ok=True)
    index.write_text(json.dumps(sorted(payloads), indent=2) + "\n", encoding="utf-8")
    return len(payloads)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--check", action="store_true", help="Validate without writing output")
    args = parser.parse_args()
    try:
        manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        count = generate(args.source_root, args.output_root, manifest, args.check)
    except (OverlayError, OSError, UnicodeError, json.JSONDecodeError) as error:
        parser.exit(1, f"native overlay: {error}\n")
    print(f"{'Validated' if args.check else 'Generated'} {count} native overlay files")


if __name__ == "__main__":
    main()
