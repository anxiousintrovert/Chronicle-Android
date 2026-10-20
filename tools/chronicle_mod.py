#!/usr/bin/env python3
"""Offline Chronicle package tools. Never executes Lua or enables a package."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import stat
import tempfile
import uuid
import zipfile

CAPABILITIES = {
    "combat.rules.v1", "entities.spawn_tuning.v1",
    "assets.replace.v1", "scripts.lua.v1", "quests.progress.v1", "save.metadata.v1",
    "economy.write.v1", "events.map.v1", "events.tick.v1", "player.read.v1",
    "party.read.v1", "inventory.read.v1", "resources.read.v1", "logging.v1", "host.calls.v1", "plugins.native.v1", "plugins.native.v2", "events.custom.v1", "events.lifecycle.v1", "events.enemy_killed.v1", "services.native.v1", "services.command.v1", "services.query.v1", "menu.actions.v1", "menu.native.v1",
}
PERMISSIONS = {"economy.write", "game.read", "game.write", "resources.read", "native.extension"}
REQUIRED = {"schema_version", "id", "name", "version", "game", "files"}
FIELDS = REQUIRED | {"api_version", "enabled", "requires", "conflicts", "exclusive",
                     "requires_capabilities", "requires_operations", "dependency_versions", "permissions", "script", "quests", "resources", "native_library", "audio"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def unique(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"duplicate JSON key: {key}")
        result[key] = value
    return result


def identity(value):
    return isinstance(value, str) and re.fullmatch(r"[a-z][a-z0-9._-]{0,95}", value) is not None


def text(value):
    return isinstance(value, str) and 1 <= len(value.encode("utf-8")) <= 256 and all(ord(c) >= 32 for c in value)


def semver(value):
    require(isinstance(value, str) and len(value) <= 256, "version bounds require valid SemVer")
    match = re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?", value)
    require(match is not None, "version bounds require valid SemVer")
    pre = []
    if match[4] is not None:
        for part in match[4].split('.'):
            numeric = part.isascii() and part.isdigit()
            require(not numeric or len(part) == 1 or part[0] != '0', "numeric prerelease identifiers cannot have leading zeros")
            pre.append((0, int(part)) if numeric else (1, part))
    return tuple(int(match[i]) for i in (1, 2, 3)), not bool(pre), tuple(pre)


def validate_version_requirement(bounds):
    require(isinstance(bounds, dict) and bounds and bounds.keys() <= {'exact', 'minimum', 'before'}, "invalid dependency version constraint")
    require(all(text(v) for v in bounds.values()), "invalid dependency version value")
    if 'exact' in bounds:
        require(len(bounds) == 1, "exact version cannot be combined with range bounds")
        return
    for value in bounds.values():
        semver(value)
    if 'minimum' in bounds and 'before' in bounds:
        require(semver(bounds['minimum']) < semver(bounds['before']), "version range is empty")


def accepts_version(version, bounds):
    if 'exact' in bounds:
        return version == bounds['exact']
    try:
        value = semver(version)
        return ('minimum' not in bounds or value >= semver(bounds['minimum'])) and ('before' not in bounds or value < semver(bounds['before']))
    except ValueError:
        return False


def describe_version(bounds):
    if 'exact' in bounds:
        return 'exact ' + bounds['exact']
    return ' '.join(prefix + bounds[key] for key, prefix in [('minimum', '>='), ('before', '<')] if key in bounds)


def relative(value):
    require(isinstance(value, str) and 1 <= len(value.encode("utf-8")) <= 240, "path must be 1-240 UTF-8 bytes")
    value = value.replace("\\", "/")
    require(not any(ord(c) < 32 or c in ':*?"<>|' for c in value), f"unsafe path: {value}")
    require(all(p not in ("", ".", "..") and not p.endswith((".", " ")) for p in value.split("/")), f"unsafe path: {value}")
    return value


def contained(root, value):
    path = (root / relative(value)).resolve(strict=True)
    require(path.is_relative_to(root) and path.is_file(), f"file escapes package or is not regular: {value}")
    return path


def validate(root, data=None):
    root = Path(root).resolve(strict=True)
    manifest = contained(root, "mod.json")
    require(manifest.stat().st_size <= 256 * 1024, "manifest exceeds 256 KiB")
    mod = json.loads(manifest.read_text(encoding="utf-8-sig"), object_pairs_hook=unique,
                     parse_constant=lambda x: (_ for _ in ()).throw(ValueError(f"invalid JSON number: {x}")))
    require(isinstance(mod, dict), "manifest must be an object")
    require(REQUIRED <= mod.keys(), f"missing fields: {sorted(REQUIRED - mod.keys())}")
    require(mod.keys() <= FIELDS, f"unknown fields: {sorted(mod.keys() - FIELDS)}")
    require(type(mod["schema_version"]) is int and mod["schema_version"] == 1, "schema_version must be 1")
    require(identity(mod["id"]), "invalid package ID")
    require(all(text(mod[k]) for k in ("name", "version", "game")), "invalid name/version/game")
    require(mod["game"] == "chronicle", "game must be chronicle")
    if "enabled" in mod:
        require(type(mod["enabled"]) is bool, "enabled must be boolean (legacy; cannot auto-enable)")
    if "api_version" in mod:
        require(type(mod["api_version"]) is int and mod["api_version"] == 1, "api_version must be 1")
    for key in ("requires", "conflicts", "exclusive", "permissions", "requires_capabilities", "requires_operations"):
        ids = mod.get(key, [])
        require(isinstance(ids, list) and len(ids) <= 128 and all(identity(x) for x in ids), f"invalid {key}")
        require(len(set(ids)) == len(ids), f"duplicate {key}")
        if key in ("requires", "conflicts"):
            require(mod["id"] not in ids, f"self reference in {key}")
    constraints = mod.get('dependency_versions', {})
    require(isinstance(constraints, dict) and len(constraints) <= 128, "dependency_versions must contain at most 128 constraints")
    for dependency, bounds in constraints.items():
        require(dependency in mod.get('requires', []), f"version constraint requires declared dependency: {dependency}")
        validate_version_requirement(bounds)
    require(set(mod.get("permissions", [])) <= PERMISSIONS, "unsupported permission")
    require(set(mod.get("requires_capabilities", [])) <= CAPABILITIES, "unsupported required capability")
    operations = {op['name']: op for op in apis()} if mod.get('requires_operations') else {}
    for name in mod.get('requires_operations', []):
        require(name in operations, f"missing host operation: {name}")
        require(operations[name]['permission'] in mod.get('permissions', []), f"required operation {name} needs permission: {operations[name]['permission']}")
    if "native_library" in mod:
        require(mod.get("api_version") == 1 and "native.extension" in mod.get("permissions", []), "native library requires API 1 and native.extension")
        library = contained(root, mod["native_library"])
        require(library.suffix in (".dll", ".so", ".dylib") and 0 < library.stat().st_size <= 64*1024*1024, "invalid native library")
    if "script" in mod:
        require(mod.get("api_version") == 1 and text(mod["script"]), "script requires api_version 1 and path")
        script = contained(root, mod["script"])
        require(script.suffix == ".lua" and script.stat().st_size <= 1024 * 1024, "script must be .lua, at most 1 MiB")
    quests = mod.get("quests", [])
    require(isinstance(quests, list) and len(quests) <= 64, "at most 64 quests")
    seen = set()
    for quest in quests:
        require(isinstance(quest, dict) and {"id", "title", "description", "goal"} <= quest.keys(), "incomplete quest")
        require(quest.keys() <= {"id", "title", "description", "goal", "map"}, "unknown quest field")
        require(identity(quest["id"]) and quest["id"] not in seen, "invalid/duplicate quest ID")
        seen.add(quest["id"])
        require(text(quest["title"]) and text(quest["description"]), "invalid quest text")
        require(type(quest["goal"]) is int and 1 <= quest["goal"] <= 1000000, "quest goal out of range")
        if "map" in quest:
            require(type(quest["map"]) is int and 0 <= quest["map"] <= 999, "quest map out of range")
    resources = mod.get("resources", {})
    require(isinstance(resources, dict) and len(resources) <= 16, "resources must be an object with at most 16 entries")
    for key, source in resources.items():
        require(identity(key), "invalid resource ID")
        require(contained(root, source).stat().st_size <= 65536, "resource exceeds 64 KiB")
    audio = mod.get("audio", {})
    require(isinstance(audio, dict) and len(audio) <= 16, "audio must contain at most 16 named files")
    audio_bytes = 0
    for key, source in audio.items():
        require(identity(key) and len(key) <= 64, "invalid audio ID")
        path = contained(root, source)
        require(path.suffix.lower() in {".wav", ".flac", ".ogg"}, "audio requires WAV, FLAC or Ogg Vorbis")
        size = path.stat().st_size
        audio_bytes += size
        require(0 < size <= 32 * 1024 * 1024 and audio_bytes <= 64 * 1024 * 1024, "audio exceeds file/package size limits or is empty")
    files = mod["files"]
    require(isinstance(files, dict) and len(files) <= 4096, "files must contain at most 4096 mappings")
    targets = set()
    for target, source in files.items():
        target = relative(target).lower()
        require(all(ord(c) < 127 for c in target), "asset targets must be ASCII")
        require(target not in ("data.hd2", "languages.json") and not target.startswith("normalized/"), "reserved asset target")
        require(target not in targets, "duplicate normalized asset target")
        targets.add(target)
        replacement = contained(root, source)
        require(replacement.stat().st_size > 0, "empty replacement")
        if data:
            original = contained(Path(data).resolve(strict=True), target)
            require(original.stat().st_size == replacement.stat().st_size, f"asset size mismatch: {target}")
    return mod


def create(package_id, output):
    require(identity(package_id), "invalid package ID")
    root = Path(output)
    root.mkdir(parents=True, exist_ok=False)
    mod = {"schema_version": 1, "api_version": 1, "id": package_id,
           "name": package_id, "version": "1.0.0", "game": "chronicle",
           "requires_capabilities": ["scripts.lua.v1", "logging.v1"],
           "script": "main.lua", "files": {}}
    (root / "mod.json").write_text(json.dumps(mod, indent=2) + "\n", encoding="utf-8")
    (root / "main.lua").write_text('assert(chronicle.log("info", "Package loaded"))\nreturn {}\n', encoding="utf-8")
    (root / "README.md").write_text("Install this folder in <save>/mods, enable in Options > Mods, then restart.\n", encoding="utf-8")


def pack(root, output):
    root = Path(root).resolve(strict=True)
    mod = validate(root)
    output = Path(output).resolve()
    require(not output.is_relative_to(root), "archive must be outside the package")
    # Do not accidentally ship saves, credentials, git metadata, or arbitrary files.
    names = {"mod.json"} | set(mod["files"].values()) | set(mod.get("resources", {}).values()) | set(mod.get("audio", {}).values())
    if "script" in mod:
        names.add(mod["script"])
    if "native_library" in mod:
        names.add(mod["native_library"])
    names |= {n for n in ("README.md", "LICENSE", "LICENSE.txt", "CHANGELOG.md") if (root / n).is_file()}
    entries = []
    for name in sorted(names):
        normalized = relative(name)
        entries.append((contained(root, name), f"{mod['id']}/{normalized}"))
    with zipfile.ZipFile(output, "x", compression=zipfile.ZIP_DEFLATED) as archive:
        for path, name in entries:
            archive.write(path, name)


def apis():
    """Read the host's single operation table; never infer support from a domain name."""
    source = (Path(__file__).resolve().parents[1] / "port/src/mods/api.cpp").read_text(encoding="utf-8")
    return [{"name": name, "permission": permission, "description": description, "writes": writes == "true"}
            for name, permission, description, writes in re.findall(
                r'\{\s*"([a-z_.]+)"\s*,\s*"([a-z_.]+)"\s*,\s*"([^"\n]+)"\s*,\s*(true|false)\s*\}', source)]


def check_set(root, enabled, data=None):
    """Offline plan only. No profile writes, native loading, Lua or game execution."""
    packages, errors = {}, []
    for folder in sorted(Path(root).iterdir()):
        if not folder.is_dir() or folder.name.startswith('.'):
            continue
        try:
            mod = validate(folder, data)
            packages.setdefault(mod['id'], []).append(mod)
        except (ValueError, OSError, UnicodeError) as error:
            errors.append(f"{folder.name}: {error}")
    selected = set(enabled)
    problems = {key: [] for key in sorted(selected)}
    for key in sorted(selected):
        candidates = packages.get(key, [])
        if len(candidates) != 1:
            problems[key].append('missing or duplicate package ID')
            continue
        mod = candidates[0]
        if 'native_library' in mod:
            problems[key].append('native trust must be reviewed in the game manager')
        for dependency in mod.get('requires', []):
            if dependency not in selected:
                problems[key].append(f'dependency not enabled: {dependency}')
            elif dependency in mod.get('dependency_versions', {}) and len(packages.get(dependency, [])) == 1:
                bounds = mod['dependency_versions'][dependency]
                installed = packages[dependency][0]['version']
                if not accepts_version(installed, bounds):
                    problems[key].append(f"dependency {dependency} requires {describe_version(bounds)}; installed {installed}")
        for other in sorted(selected - {key}):
            if len(packages.get(other, [])) != 1:
                continue
            candidate = packages[other][0]
            if other in mod.get('conflicts', []) or key in candidate.get('conflicts', []):
                problems[key].append(f'declared conflict: {other}')
            for claim in sorted(set(mod.get('exclusive', [])) & set(candidate.get('exclusive', []))):
                problems[key].append(f'exclusive feature {claim}: {other}')
            left = {relative(p).lower() for p in mod['files']}
            right = {relative(p).lower() for p in candidate['files']}
            for target in sorted(left & right):
                problems[key].append(f'asset conflict {target}: {other}')
    visiting, visited = [], set()
    def visit(key):
        if key in visiting:
            for member in visiting[visiting.index(key):]:
                problems[member].append('dependency cycle')
            return
        if key in visited or len(packages.get(key, [])) != 1:
            return
        visiting.append(key)
        for dependency in packages[key][0].get('requires', []):
            if dependency in selected:
                visit(dependency)
        visiting.pop()
        visited.add(key)
    for key in sorted(selected):
        visit(key)
    changed = True
    while changed:
        changed = False
        for key in sorted(selected):
            if problems[key] or len(packages.get(key, [])) != 1:
                continue
            for dependency in packages[key][0].get('requires', []):
                if problems.get(dependency):
                    problems[key].append(f'blocked dependency: {dependency}')
                    changed = True
                    break
    return {'enabled': sorted(selected), 'problems': problems, 'invalid_packages': errors,
            'scope': 'offline format/conflict check; actual assets, scripts and native trust require in-game validation'}


def install(archive, mods_root, replace=False, data=None):
    """Offline install of our pack format; never changes enablement or trust."""
    root = Path(mods_root).resolve()
    root.mkdir(parents=True, exist_ok=True)
    require(root.is_dir(), "mods root must be a directory")
    # TemporaryDirectory owns only the newly created hidden directory below root.
    with tempfile.TemporaryDirectory(prefix='.install-', dir=root) as temp:
        stage = Path(temp).resolve()
        require(stage.parent == root, "staging directory escaped mods root")
        seen, files, roots, total = set(), [], set(), 0
        with zipfile.ZipFile(archive) as packed:
            entries = packed.infolist()
            require(0 < len(entries) <= 8192, "archive must contain 1-8192 entries")
            for entry in entries:
                name = relative(entry.filename[:-1] if entry.is_dir() else entry.filename)
                for part in name.split('/'):
                    device = part.split('.')[0].upper()
                    require(device not in {'CON', 'PRN', 'AUX', 'NUL'} and re.fullmatch(r'(COM|LPT)[1-9]', device) is None, 'archive contains a reserved device name')
                key = name.casefold()
                require(key not in seen, f"duplicate archive path: {name}")
                seen.add(key)
                roots.add(name.split('/')[0])
                kind = stat.S_IFMT(entry.external_attr >> 16)
                require(kind in (0, stat.S_IFDIR if entry.is_dir() else stat.S_IFREG), 'archive links and special files are not allowed')
                require(not entry.flag_bits & 1 and entry.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED), 'unsupported encrypted/compression format')
                require(0 <= entry.file_size <= 64 * 1024 * 1024, 'archive member exceeds 64 MiB')
                total += entry.file_size
                require(total <= 512 * 1024 * 1024, 'archive expands beyond 512 MiB')
                target = stage / name
                require(target.resolve().is_relative_to(stage), 'archive path escaped staging directory')
                if entry.is_dir():
                    target.mkdir(parents=True, exist_ok=True)
                else:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with target.open('xb') as output:
                        output.write(packed.read(entry))
                    files.append(name)
        require(len(roots) == 1, 'archive must contain one package folder')
        package_root = stage / next(iter(roots))
        mod = validate(package_root, data)
        require(package_root.name == mod['id'], 'archive folder must match package ID')
        declared = {'mod.json', 'README.md', 'LICENSE', 'LICENSE.txt', 'CHANGELOG.md'} | set(mod['files'].values()) | set(mod.get('resources', {}).values()) | set(mod.get('audio', {}).values())
        declared |= {mod[key] for key in ('script', 'native_library') if key in mod}
        allowed = {mod['id'] + '/' + relative(name) for name in declared}
        require(set(files) <= allowed, 'archive contains undeclared files; use the pack command')
        target = root / mod['id']
        require(not target.is_symlink() and target.resolve().parent == root, 'installed package path must stay directly under mods root')
        backup = None
        if target.exists():
            require(replace, 'package already exists; use --replace for a backed-up update')
            previous = validate(target)
            require(previous['id'] == mod['id'], 'installed package identity does not match')
            backups = root / '.backups'
            require(not backups.is_symlink() and backups.resolve() == backups, 'backup directory must not be a symlink')
            backups.mkdir(exist_ok=True)
            backup = backups / (mod['id'] + '-' + uuid.uuid4().hex)
            require(backup.resolve().parent == backups, 'backup path escaped mods root')
            target.rename(backup)
        try:
            package_root.rename(target)
        except OSError:
            if backup is not None and not target.exists():
                backup.rename(target)
            raise
        return {'installed': str(target), 'id': mod['id'], 'version': mod['version'],
                'backup': str(backup) if backup else None, 'profile_unchanged': True,
                'next_step': 'Restart Chronicle. No profile entries were created; existing enable/trust selections remain unchanged. Review conflicts and native trust in the mod manager.'}


def compare_seeds(baseline, candidates, allow_map_size_change=False):
    """Keep byte comparisons; optionally diagnose only map storage-size differences."""
    original = Path(baseline).read_bytes()
    records = original.splitlines()
    require(bool(records), "baseline seed export is empty")
    for line in records:
        require(isinstance(json.loads(line), dict), "seed records must be JSON objects")
    result = {'baseline': str(baseline), 'cases': len(records), 'sha256': hashlib.sha256(original).hexdigest(), 'candidates': []}
    for path in candidates:
        data = Path(path).read_bytes()
        lines = data.splitlines()
        first = next((i + 1 for i in range(max(len(records), len(lines)))
                      if i >= len(records) or i >= len(lines) or records[i] != lines[i]), None)
        result['candidates'].append({'path': str(path), 'matches': data == original,
                                    'cases': len(lines), 'first_differing_record': first,
                                    'sha256': hashlib.sha256(data).hexdigest()})
        if allow_map_size_change:
            left, right = [json.loads(line) for line in records], [json.loads(line) for line in lines]
            for record in left + right:
                require(isinstance(record, dict) and type(record.get('map_bytes')) is int
                        and record['map_bytes'] > 0, 'size comparison requires positive integer map_bytes in every record')
            sizes = {'baseline': sorted({record['map_bytes'] for record in left}),
                     'candidate': sorted({record['map_bytes'] for record in right})}
            for record in left + right:
                del record['map_bytes']
            different = next((i + 1 for i in range(max(len(left), len(right)))
                              if i >= len(left) or i >= len(right) or left[i] != right[i]), None)
            result['candidates'][-1].update(map_size_only=different is None,
                                            map_sizes=sizes, first_non_size_difference=different)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    new = commands.add_parser("new", help="create a disabled starter package")
    new.add_argument("id"); new.add_argument("--output", required=True)
    for command in ("check", "inspect"):
        cmd = commands.add_parser(command)
        cmd.add_argument("package"); cmd.add_argument("--data", help="optional logical asset tree for exact-path size checks")
    archive = commands.add_parser("pack", help="validate and create an unpack-before-install ZIP")
    archive.add_argument("package"); archive.add_argument("--output", required=True)
    installer = commands.add_parser("install", help="install a package ZIP offline; optional backed-up replacement")
    installer.add_argument("archive"); installer.add_argument("--mods", required=True)
    installer.add_argument("--replace", action="store_true"); installer.add_argument("--data")
    commands.add_parser("capabilities")
    commands.add_parser("apis", help="print implemented host operations and permissions")
    doctor = commands.add_parser("check-set", help="preview selected package conflicts without enabling anything")
    doctor.add_argument("root"); doctor.add_argument("--enable", action="append", default=[])
    doctor.add_argument("--data")
    seeds = commands.add_parser("compare-seeds", help="compare complete same-seed audit exports byte for byte")
    seeds.add_argument("baseline"); seeds.add_argument("candidates", nargs="+")
    seeds.add_argument("--allow-map-size-change", action="store_true",
                       help="also compare all parsed fields except map_bytes; retain raw hashes and mismatch results")
    args = parser.parse_args(argv)
    try:
        if args.command == "compare-seeds":
            report = compare_seeds(args.baseline, args.candidates, args.allow_map_size_change)
            print(json.dumps(report, indent=2))
            return int(any(not (item['matches'] or (args.allow_map_size_change and item['map_size_only']))
                           for item in report['candidates']))
        if args.command == "new":
            create(args.id, args.output)
        elif args.command == "install":
            print(json.dumps(install(args.archive, args.mods, args.replace, args.data), indent=2))
        elif args.command == "pack":
            pack(args.package, args.output)
        elif args.command == "capabilities":
            print("\n".join(sorted(CAPABILITIES)))
        elif args.command == "apis":
            print(json.dumps(apis(), indent=2))
        elif args.command == "check-set":
            report = check_set(args.root, args.enable, args.data)
            print(json.dumps(report, indent=2))
            return int(bool(report['invalid_packages']) or any(report['problems'].values()))
        else:
            mod = validate(args.package, args.data)
            print(json.dumps(mod, indent=2) if args.command == "inspect" else f"Package format OK: {mod['id']}")
            print("Offline only: Lua was not executed; validate installed conflicts and actual assets in the game.")
            if mod["files"] and not args.data:
                print("Asset target existence/size not checked: no --data logical tree supplied.")
        return 0
    except (ValueError, OSError, UnicodeError, zipfile.BadZipFile) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
