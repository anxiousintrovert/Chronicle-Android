import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
import stat
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('chronicle_mod', Path(__file__).parents[1] / 'chronicle_mod.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class AuthorTools(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_dependency_diagnostics_trace_all_causes_without_cycle_duplicates(self):
        mods = self.root / 'mods'
        for name, dependencies in [('root', ['test.left', 'test.right']),
                                   ('left', ['test.leaf']), ('right', ['test.leaf']),
                                   ('leaf', ['test.root', 'test.missing'])]:
            folder = mods / name
            tool.create('test.' + name, folder)
            path = folder / 'mod.json'
            manifest = json.loads(path.read_text())
            manifest['requires'] = dependencies
            path.write_text(json.dumps(manifest))
        selected = ['test.root', 'test.left', 'test.right', 'test.leaf']
        report = tool.check_set(mods, selected)
        diagnostics = report['dependency_diagnostics']['test.root']
        missing = [item for item in diagnostics if 'not enabled' in item['reason']]
        self.assertEqual(missing, [{'path': ['test.root', 'test.left', 'test.leaf'],
                                   'reason': 'dependency not enabled: test.missing'}])
        self.assertEqual(len({tuple(item['path']) for item in diagnostics}),
                         len({item['path'][-1] for item in diagnostics}))
        self.assertEqual(report, tool.check_set(mods, reversed(selected)))
        self.assertFalse((mods / 'mods-profile.json').exists())
        self.assertEqual(tool.check_set(mods, [])['dependency_diagnostics'], {})

    def test_dependency_diagnostics_preserve_multiple_transitive_conflict_causes(self):
        mods = self.root / 'mods'
        for name, extra in [('root', {'requires': ['test.provider']}),
                            ('provider', {'requires': ['test.absent'], 'conflicts': ['test.other']}),
                            ('other', {})]:
            folder = mods / name
            tool.create('test.' + name, folder)
            path = folder / 'mod.json'
            manifest = json.loads(path.read_text())
            manifest.update(extra)
            path.write_text(json.dumps(manifest))
        report = tool.check_set(mods, ['test.root', 'test.provider', 'test.other', 'test.absent'])
        self.assertEqual(report['problems']['test.root'], ['blocked dependency: test.provider'])
        self.assertEqual(report['dependency_diagnostics']['test.root'], [
            {'path': ['test.root', 'test.provider'], 'reason': 'declared conflict: test.other'},
            {'path': ['test.root', 'test.provider', 'test.absent'], 'reason': 'missing or duplicate package ID'}])

    def test_save_requirements_distinguish_installed_versions_and_declared_migrations(self):
        import zlib
        mods = self.root / 'mods'
        for folder, owner, version, migrations in [
                ('exact', 'test.exact', '1', []),
                ('upgrade', 'test.upgrade', '2', [{'from': '1', 'defaults': {'new': True}}]),
                ('mismatch', 'test.mismatch', '3', [{'from': '2'}]),
                ('dup-a', 'test.duplicate', '1', []), ('dup-b', 'test.duplicate', '2', [])]:
            directory=mods/folder;tool.create(owner,directory)
            path=directory/'mod.json';manifest=json.loads(path.read_text())
            manifest.update(version=version,save_migrations=migrations);path.write_text(json.dumps(manifest))
        invalid=mods/'invalid';invalid.mkdir();(invalid/'mod.json').write_text('{}')
        required={key:'1' for key in ['test.exact','test.upgrade','test.mismatch','test.duplicate','test.missing']}
        metadata={'schema_version':1,'required':required,'mods':{key:{} for key in required}}
        raw=bytes(tool.SAVE_NATIVE_SIZE)+tool.SAVE_MARKER+json.dumps(metadata).encode()
        saved=self.root/'save.bin';saved.write_bytes(raw+zlib.crc32(raw).to_bytes(4,'little'))
        before={path:path.read_bytes() for path in self.root.rglob('*') if path.is_file()}
        report=tool.inspect_save_packages(saved,mods)
        self.assertEqual({item['id']:item['status'] for item in report['installed_requirements']}, {
            'test.exact':'exact_version','test.upgrade':'direct_migration_declared',
            'test.mismatch':'version_mismatch','test.duplicate':'duplicate_id','test.missing':'missing_or_invalid'})
        self.assertEqual(len(report['invalid_packages']),1)
        self.assertEqual(before,{path:path.read_bytes() for path in self.root.rglob('*') if path.is_file()})
        self.assertIn('not established',report['installation_scope'])

    def test_save_inspection_boundary_matches_native_writer(self):
        import re
        source = Path(tool.__file__).resolve().parents[1] / 'port/src/memorycardaccess.cpp'
        match = re.search(r'constexpr int kImageSize = (0x[0-9A-Fa-f]+);', source.read_text())
        self.assertIsNotNone(match)
        self.assertEqual(tool.SAVE_NATIVE_SIZE, int(match.group(1), 16))

    def test_inspect_save_verifies_entire_image_and_reports_required_versions(self):
        import zlib
        path = self.root / 'save.bin'
        native = bytes(tool.SAVE_NATIVE_SIZE)
        metadata = {'schema_version': 1, 'required': {'test.quest': '1.2.3'},
                    'mods': {'test.quest': {'values': {'secret': 'not in report'}}, 'test.retained': {}}}
        raw = native + tool.SAVE_MARKER + json.dumps(metadata).encode()
        image = raw + zlib.crc32(raw).to_bytes(4, 'little')
        path.write_bytes(image)
        result = tool.inspect_save(path)
        self.assertEqual(result['required'], {'test.quest': '1.2.3'})
        self.assertEqual(result['state_owners'], ['test.quest', 'test.retained'])
        self.assertEqual(result['checksum'], 'verified')
        self.assertNotIn('secret', json.dumps(result))
        self.assertEqual(path.read_bytes(), image)
        for index in [0, tool.SAVE_NATIVE_SIZE + len(tool.SAVE_MARKER), len(image)-1]:
            broken = bytearray(image); broken[index] ^= 1; path.write_bytes(broken)
            with self.assertRaisesRegex(ValueError, 'checksum'): tool.inspect_save(path)
        path.write_bytes(native)
        self.assertEqual(tool.inspect_save(path)['format'], 'native')
        self.assertEqual(tool.inspect_save(path)['checksum'], 'not_checked')

    def test_inspect_save_rejects_bad_boundaries_json_and_oversized_input(self):
        import zlib
        path = self.root / 'save.bin'
        for data in [b'', bytes(tool.SAVE_NATIVE_SIZE-1), bytes(tool.SAVE_NATIVE_SIZE)+b'unknown',
                     bytes(tool.SAVE_NATIVE_SIZE+len(tool.SAVE_MARKER)+tool.SAVE_METADATA_LIMIT+5)]:
            path.write_bytes(data)
            with self.assertRaises(ValueError): tool.inspect_save(path)
        for payload in [b'{}', b'{"schema_version":1,"schema_version":1}',
                        b'{"schema_version":true,"mods":{},"required":{}}',
                        b'{"schema_version":1,"mods":{},"required":{"test.mod":42}}',
                        b'{"schema_version":1,"mods":{"test.mod":[]},"required":{}}',
                        b'{"schema_version":1,"mods":{},"required":{},"bad":NaN}',
                        b'['*2000+b']'*2000]:
            raw=bytes(tool.SAVE_NATIVE_SIZE)+tool.SAVE_MARKER+payload
            path.write_bytes(raw+zlib.crc32(raw).to_bytes(4,'little'))
            with self.assertRaises(ValueError): tool.inspect_save(path)

    def test_quest_template_pack_install_preserves_script_and_stays_disabled(self):
        folder = self.root / "authored"
        self.assertEqual(tool.main(["new", "test.hunt", "--template", "quest", "--output", str(folder)]), 0)
        manifest = tool.validate(folder)
        self.assertEqual(manifest["id"], "test.hunt")
        self.assertNotIn("enabled", manifest)
        self.assertIn("quests.rewards.v1", manifest["requires_capabilities"])
        source = Path(tool.__file__).resolve().parents[1] / "examples/mods/dungeon-contract/main.lua"
        self.assertEqual((folder / "main.lua").read_text(encoding="utf-8"), source.read_text(encoding="utf-8"))
        archive = self.root / "quest.zip"
        tool.pack(folder, archive)
        result = tool.install(archive, self.root / "mods")
        self.assertTrue(result["profile_unchanged"])
        self.assertFalse((self.root / "mods-profile.json").exists())
        self.assertEqual(tool.validate(self.root / "mods/test.hunt"), manifest)
        self.assertEqual((folder / "main.lua").read_bytes(), (self.root / "mods/test.hunt/main.lua").read_bytes())
        self.assertNotIn("examples/mods/dungeon-contract", (folder / "README.md").read_text())

    def test_new_template_rejects_unknown_existing_and_ps2_before_writing(self):
        for template, output in [("missing", self.root / "missing"),
                                 ("quest", self.root / "PS2" / "new"),
                                 ("basic", self.root / "ps2" / "new")]:
            with self.assertRaises(ValueError):
                tool.create("test.hunt", output, template)
            self.assertFalse(output.exists())
        self.assertFalse((self.root / "PS2").exists())
        existing = self.root / "existing"
        existing.mkdir()
        (existing / "keep").write_text("unchanged")
        with self.assertRaises(FileExistsError):
            tool.create("test.hunt", existing, "quest")
        self.assertEqual(list(existing.iterdir()), [existing / "keep"])

    def package(self, name, **changes):
        folder = self.root / 'mods' / name
        tool.create(name, folder)
        manifest = folder / 'mod.json'
        data = json.loads(manifest.read_text())
        data.update(changes)
        manifest.write_text(json.dumps(data))
        return folder

    def test_full_texture_packages_pack_install_and_conflict(self):
        import struct, zlib
        def chunk(kind, data):
            return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
        png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',2,1,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b'\x00'+bytes([255,0,0,255])*2))+chunk(b'IEND',b'')
        target={'name':'atlas','original_sha256':'a'*64,'source':'image.png'}
        first=self.package('test.one',textures=[target],requires_capabilities=['assets.textures.v1'])
        second=self.package('test.two',textures=[dict(target,name='ATLAS')])
        for folder in [first,second]:(folder/'image.png').write_bytes(png);tool.validate(folder)
        archive=self.root/'textures.zip';tool.pack(first,archive)
        with zipfile.ZipFile(archive) as package:self.assertIn('test.one/image.png',package.namelist())
        tool.install(archive,self.root/'installed')
        result=tool.check_set(self.root/'mods',['test.one','test.two'])
        self.assertIn('texture conflict',str(result))
        manifest=first/'mod.json';value=json.loads(manifest.read_text());value['textures']*=2;manifest.write_text(json.dumps(value))
        with self.assertRaises(ValueError):tool.validate(first)

    def test_skill_operations_are_discoverable_and_permission_checked(self):
        names=['skills.register','skills.list','skills.get','skills.unlock','skills.set_rank']
        operations={entry['name']:entry for entry in tool.apis()}
        for name in names:
            self.assertEqual(operations[name]['writes'],name not in ['skills.list','skills.get'])
        folder=self.package('test.skills',requires_capabilities=['skills.trees.v1','skills.effects.v1'],requires_operations=names,permissions=['game.read','game.write'])
        self.assertEqual(tool.validate(folder)['requires_operations'],names)

    def test_dynamic_quest_operations_are_explicit_writes(self):
        names=['quests.register','quests.advance','quests.start','quests.fail','quests.resume'];operations={entry['name']:entry for entry in tool.apis()}
        for name in names:
            self.assertTrue(operations[name]['writes']);self.assertEqual(operations[name]['permission'],'game.write')
        folder=self.package('test.dynamic',requires_capabilities=['quests.dynamic.v1','quests.objectives.v1','quests.lifecycle.v1','quests.rewards.v1','quests.events.v1','quests.ordering.v1','quests.failure.v1','quests.custom_events.v1'],requires_operations=names,permissions=['game.write'])
        self.assertEqual(tool.validate(folder)['requires_operations'],names)
        manifest=folder/'mod.json';value=json.loads(manifest.read_text());value['permissions']=['game.read'];manifest.write_text(json.dumps(value))
        with self.assertRaises(ValueError):tool.validate(folder)

    def test_shared_quest_queries_are_discoverable_and_require_read_permission(self):
        names=['quests.list','quests.get'];operations={entry['name']:entry for entry in tool.apis()}
        for name in names:
            self.assertFalse(operations[name]['writes']);self.assertEqual(operations[name]['permission'],'game.read')
        folder=self.package('test.quests',requires_capabilities=['quests.read.v1'],requires_operations=names,permissions=['game.read'])
        self.assertEqual(tool.validate(folder)['requires_operations'],names)
        manifest=folder/'mod.json';value=json.loads(manifest.read_text());value['permissions']=[];manifest.write_text(json.dumps(value))
        with self.assertRaises(ValueError):tool.validate(folder)

    def test_save_migrations_validate_and_survive_pack_install(self):
        rules=[{'from':'1','rename_values':{'old':'new'},'rename_quests':{'old.quest':'new.quest'},
                'remove_values':['obsolete'],'defaults':{'new':99,'introduced':True}}]
        folder=self.package('test.migration',version='2',save_migrations=rules,requires_capabilities=['save.migrations.v1'])
        self.assertEqual(tool.validate(folder)['save_migrations'],rules)
        archive=self.root/'migration.zip';tool.pack(folder,archive)
        with zipfile.ZipFile(archive) as packed:
            self.assertEqual(json.loads(packed.read('test.migration/mod.json'))['save_migrations'],rules)
        installed=self.root/'installed';tool.install(archive,installed)
        self.assertEqual(tool.validate(installed/'test.migration')['save_migrations'],rules)
        self.assertFalse((installed/'mods-profile.json').exists())
        for invalid in [[{'from':'2'}],[{'from':'1'},{'from':'1'}],[{'from':'1','unknown':True}],
                        [{'from':'1','rename_values':{'a':'x','b':'x'}}],
                        [{'from':'1','rename_values':{'a':'b'},'remove_values':['b']}],
                        [{'from':'1','defaults':{'bad':[]}}],
                        [{'from':'1','remove_values':['x'],'defaults':{'x':1}}]]:
            with self.subTest(invalid=invalid),self.assertRaises(ValueError):tool.validate_save_migrations(invalid,'2')

    def test_combat_rules_are_discoverable_and_permission_checked(self):
        names = ['combat.rules_get', 'combat.rule_set', 'combat.rule_remove']
        operations = {entry['name']: entry for entry in tool.apis()}
        self.assertFalse(operations[names[0]]['writes'])
        self.assertTrue(operations[names[1]]['writes'])
        folder = self.package('test.combat', requires_capabilities=['combat.rules.v1'],
                              requires_operations=names, permissions=['game.read', 'game.write'])
        self.assertEqual(tool.validate(folder)['requires_operations'], names)
        manifest = folder / 'mod.json'
        data = json.loads(manifest.read_text())
        data['permissions'] = ['game.read']
        manifest.write_text(json.dumps(data))
        with self.assertRaises(ValueError):
            tool.validate(folder)

    def test_starter_is_disabled_and_archive_contains_only_declared_files(self):
        folder = self.package('test.one')
        self.assertNotIn('enabled', tool.validate(folder))
        (folder / 'private.txt').write_text('do not ship')
        archive = self.root / 'package.zip'
        tool.pack(folder, archive)
        with zipfile.ZipFile(archive) as packed:
            self.assertEqual(set(packed.namelist()), {'test.one/mod.json', 'test.one/main.lua', 'test.one/README.md'})
        with self.assertRaises(FileExistsError):
            tool.pack(folder, archive)

    def test_audio_assets_pack_and_validate_separately_from_script_resources(self):
        folder = self.package('test.audio', audio={'tone': 'tone.WAV'})
        (folder / 'tone.WAV').write_bytes(b'x' * 70000)
        self.assertNotIn('enabled', tool.validate(folder))
        archive = self.root / 'audio.zip'
        tool.pack(folder, archive)
        with zipfile.ZipFile(archive) as packed:
            self.assertIn('test.audio/tone.WAV', packed.namelist())
        destination = self.root / 'installed-audio'
        tool.install(archive, destination)
        self.assertEqual((destination / 'test.audio/tone.WAV').read_bytes(), b'x' * 70000)
        (folder / 'tone.WAV').write_bytes(b'')
        with self.assertRaises(ValueError):
            tool.validate(folder)
        manifest = json.loads((folder / 'mod.json').read_text())
        manifest['audio'] = {'tone': '../escape.wav'}
        (folder / 'mod.json').write_text(json.dumps(manifest))
        with self.assertRaises(ValueError):
            tool.validate(folder)

    def test_path_escape_and_duplicate_json_rejected(self):
        folder = self.package('test.one', resources={'bad': '../outside.txt'})
        with self.assertRaises(ValueError):
            tool.validate(folder)
        (folder / 'mod.json').write_text('{"id":"test.one","id":"test.two"}')
        with self.assertRaisesRegex(ValueError, 'duplicate JSON key'):
            tool.validate(folder)

    def test_dependency_conflict_plan_never_enables_or_writes_profile(self):
        self.package('test.one', exclusive=['combat.formula'])
        self.package('test.two', exclusive=['combat.formula'])
        self.package('test.three', requires=['test.one'])
        empty = tool.check_set(self.root / 'mods', [])
        self.assertFalse(empty['problems'])
        result = tool.check_set(self.root / 'mods', ['test.one', 'test.two', 'test.three'])
        self.assertIn('exclusive feature', result['problems']['test.one'][0])
        self.assertIn('blocked dependency', result['problems']['test.three'][0])
        self.assertFalse(list(self.root.rglob('mods-profile.json')))

    def test_dependency_cycles_and_missing_ids_reported(self):
        self.package('test.one', requires=['test.two'])
        self.package('test.two', requires=['test.one'])
        result = tool.check_set(self.root / 'mods', ['test.one', 'test.two', 'test.missing'])
        self.assertIn('dependency cycle', result['problems']['test.one'])
        self.assertTrue(result['problems']['test.missing'])

    def test_native_is_packaged_but_never_trusted_by_cli(self):
        folder = self.package('test.one', native_library='plugin.dll', permissions=['native.extension'])
        (folder / 'plugin.dll').write_bytes(b'not executed')
        archive = self.root / 'package.zip'
        tool.pack(folder, archive)
        with zipfile.ZipFile(archive) as packed:
            self.assertIn('test.one/plugin.dll', packed.namelist())
        result = tool.check_set(self.root / 'mods', ['test.one'])
        self.assertIn('native trust', result['problems']['test.one'][0])

    def test_catalog_has_real_operations_and_does_not_claim_future_hooks(self):
        operations = {op['name']: op for op in tool.apis()}
        self.assertEqual(operations['player.heal']['permission'], 'game.write')
        self.assertTrue(operations['economy.spend']['writes'])
        self.assertEqual(operations['entities.spawn']['permission'], 'game.write')
        self.assertTrue(operations['entities.spawn']['writes'])
        self.assertFalse(operations['entities.spawn_check']['writes'])
        self.assertNotIn('entities.register_model', operations)

    def test_required_operations_need_implementation_and_permission(self):
        missing = self.package('test.missing', requires_operations=['entities.register_model'])
        with self.assertRaisesRegex(ValueError, 'missing host operation'):
            tool.validate(missing)
        denied = self.package('test.denied', requires_operations=['story.flag_set'])
        with self.assertRaisesRegex(ValueError, 'needs permission'):
            tool.validate(denied)
        ready = self.package('test.ready', requires_operations=['story.flag_set'], permissions=['game.write'])
        self.assertEqual(tool.validate(ready)['id'], 'test.ready')
        tuned = self.package('test.tuned', requires_operations=['entities.spawn'],
                             requires_capabilities=['entities.spawn_tuning.v1'], permissions=['game.write'])
        self.assertEqual(tool.validate(tuned)['id'], 'test.tuned')

    def test_seed_comparison_rejects_rng_differences_and_truncation(self):
        baseline, candidate = self.root / 'baseline.jsonl', self.root / 'candidate.jsonl'
        baseline.write_bytes(b'{"seed":1,"next_rng":42}\n{"seed":2,"next_rng":19}\n')
        candidate.write_bytes(baseline.read_bytes())
        self.assertTrue(tool.compare_seeds(baseline, [candidate])['candidates'][0]['matches'])
        candidate.write_bytes(b'{"seed":1,"next_rng":43}\n{"seed":2,"next_rng":19}\n')
        mismatch = tool.compare_seeds(baseline, [candidate])['candidates'][0]
        self.assertFalse(mismatch['matches'])
        self.assertEqual(mismatch['first_differing_record'], 1)
        candidate.write_bytes(b'{"seed":1,"next_rng":42}\n')
        mismatch = tool.compare_seeds(baseline, [candidate])['candidates'][0]
        self.assertFalse(mismatch['matches'])
        self.assertEqual(mismatch['first_differing_record'], 2)

    def test_map_size_comparison_keeps_raw_failure_and_all_other_fields(self):
        baseline, candidate = self.root / 'baseline.jsonl', self.root / 'candidate.jsonl'
        row = {'map_bytes': 1000, 'grid': 20, 'rooms': [[1, 2, 3, 4]], 'cells': [[1, 2, 3, 0]], 'next_rng': 42}
        baseline.write_text(json.dumps(row) + '\n')
        row['map_bytes'] = 1960
        candidate.write_text(json.dumps(row) + '\n')
        report = tool.compare_seeds(baseline, [candidate], True)['candidates'][0]
        self.assertFalse(report['matches'])
        self.assertTrue(report['map_size_only'])
        self.assertEqual(report['map_sizes'], {'baseline': [1000], 'candidate': [1960]})
        for field, bad in [('grid', 48), ('next_rng', 43), ('rooms', []), ('cells', [[1, 2, 3, 0], [21, 2, 3, 0]])]:
            changed = dict(row, **{field: bad})
            candidate.write_text(json.dumps(changed) + '\n')
            report = tool.compare_seeds(baseline, [candidate], True)['candidates'][0]
            self.assertFalse(report['map_size_only'])
            self.assertEqual(report['first_non_size_difference'], 1)
        candidate.write_text('')
        self.assertFalse(tool.compare_seeds(baseline, [candidate], True)['candidates'][0]['map_size_only'])
        candidate.write_text(json.dumps(dict(row, map_bytes=True)))
        with self.assertRaisesRegex(ValueError, 'positive integer map_bytes'):
            tool.compare_seeds(baseline, [candidate], True)

    def test_dependency_versions_block_transitively_and_exact_legacy_versions_work(self):
        self.package('test.provider', version='2.0.0')
        self.package('test.consumer', requires=['test.provider'], dependency_versions={'test.provider': {'minimum': '1.2.0', 'before': '2.0.0'}})
        self.package('test.dependent', requires=['test.consumer'])
        report = tool.check_set(self.root / 'mods', ['test.provider', 'test.consumer', 'test.dependent'])
        self.assertFalse(report['problems']['test.provider'])
        self.assertIn('installed 2.0.0', report['problems']['test.consumer'][0])
        self.assertIn('blocked dependency', report['problems']['test.dependent'][0])
        self.assertTrue(tool.accepts_version('1.2.0+build', {'minimum': '1.2.0', 'before': '2.0.0'}))
        self.assertFalse(tool.accepts_version('1.2.0-rc.1', {'minimum': '1.2.0'}))
        self.assertTrue(tool.accepts_version('legacy-v1', {'exact': 'legacy-v1'}))
        self.assertFalse(tool.accepts_version('1.0.0+two', {'exact': '1.0.0+one'}))

    def test_semver_numeric_precedence_and_invalid_constraints(self):
        ordered = ['1.0.0-alpha', '1.0.0-alpha.1', '1.0.0-alpha.beta', '1.0.0-beta', '1.0.0-beta.2', '1.0.0-beta.11', '1.0.0-rc.1', '1.0.0', '1.0.1', '1.2.0', '1.10.0', '2.0.0']
        self.assertEqual(sorted(ordered, key=tool.semver), ordered)
        self.assertEqual(tool.semver('1.0.0+one'), tool.semver('1.0.0+two'))
        for invalid in ['1', '01.2.3', '1.2.3-01', '1.2.3-', '1.2.3+', '1.2.3-alpha..beta', '1.2.3+one+two', '1.2.3.4', '1.2.3 ']:
            with self.assertRaises(ValueError):
                tool.semver(invalid)
        for invalid in [{}, {'exact': '1', 'minimum': '1.0.0'}, {'minimum': '2.0.0', 'before': '1.0.0'}]:
            with self.assertRaises(ValueError):
                tool.validate_version_requirement(invalid)
        folder = self.package('test.undeclared', dependency_versions={'test.missing': {'exact': '1'}})
        with self.assertRaisesRegex(ValueError, 'requires declared dependency'):
            tool.validate(folder)

    def test_install_update_keeps_backup_and_never_touches_profile(self):
        package = self.package('test.one')
        first = self.root / 'first.zip'
        tool.pack(package, first)
        destination = self.root / 'installed'
        profile = self.root / 'mods-profile.json'
        profile.write_text('{"enabled":{"test.one":false}}')
        result = tool.install(first, destination)
        self.assertIsNone(result['backup'])
        with self.assertRaisesRegex(ValueError, 'already exists'):
            tool.install(first, destination)
        manifest = json.loads((package / 'mod.json').read_text())
        manifest['version'] = '2.0.0'
        (package / 'mod.json').write_text(json.dumps(manifest))
        second = self.root / 'second.zip'
        tool.pack(package, second)
        result = tool.install(second, destination, replace=True)
        self.assertEqual(tool.validate(destination / 'test.one')['version'], '2.0.0')
        self.assertEqual(tool.validate(Path(result['backup']))['version'], '1.0.0')
        self.assertEqual(profile.read_text(), '{"enabled":{"test.one":false}}')
        self.assertFalse(tool.check_set(destination, [])['invalid_packages'])

    def test_installer_rejects_traversal_symlinks_and_duplicate_paths(self):
        for label, entries in [('traversal', [('test.one/../../escape', b'bad')]),
                               ('duplicate', [('test.one/mod.json', b'{}'), ('test.one/MOD.JSON', b'{}')]),
                               ('device', [('test.one/NUL.txt', b'bad')])]:
            archive = self.root / (label + '.zip')
            with zipfile.ZipFile(archive, 'w') as z:
                for name, value in entries:
                    z.writestr(name, value)
            with self.assertRaises(ValueError):
                tool.install(archive, self.root / 'installed')
        archive = self.root / 'symlink.zip'
        with zipfile.ZipFile(archive, 'w') as z:
            link = zipfile.ZipInfo('test.one/link')
            link.create_system = 3
            link.external_attr = (stat.S_IFLNK | 0o777) << 16
            z.writestr(link, '../../outside')
        with self.assertRaisesRegex(ValueError, 'special files'):
            tool.install(archive, self.root / 'installed')
        self.assertEqual(list((self.root / 'installed').iterdir()), [])
        self.assertFalse((self.root / 'escape').exists())

    def test_failed_update_publication_restores_old_package(self):
        package = self.package('test.one')
        archive = self.root / 'package.zip'
        tool.pack(package, archive)
        destination = self.root / 'installed'
        tool.install(archive, destination)
        original_rename = Path.rename
        def fail_publication(path, target):
            if path.parent.name.startswith('.install-'):
                raise OSError('simulated publication failure')
            return original_rename(path, target)
        with patch.object(Path, 'rename', fail_publication):
            with self.assertRaisesRegex(OSError, 'publication failure'):
                tool.install(archive, destination, replace=True)
        self.assertEqual(tool.validate(destination / 'test.one')['version'], '1.0.0')
        self.assertEqual(list((destination / '.backups').iterdir()), [])


    def mesh_package(self, name, targets):
        folder = self.package(name, meshes=targets)
        geometry = {'schema_version': 1, 'vertices': [[0,0,0,0,0,1,0,0],[1,0,0,0,0,1,1,0],[0,1,0,0,0,1,0,1]], 'triangles': [[0,0,1,2]]}
        (folder / 'hero.mesh.json').write_text(json.dumps(geometry))
        return folder

    def test_mesh_pack_install_keeps_declared_geometry_and_checks_selected_targets(self):
        folder = self.mesh_package('test.mesh', [{'texture': 'HERO', 'native_vertices': 3, 'source': 'hero.mesh.json'}])
        self.assertEqual(len(tool.validate(folder)['meshes']), 1)
        archive = self.root / 'mesh.zip'
        tool.pack(folder, archive)
        with zipfile.ZipFile(archive) as data:
            self.assertIn('test.mesh/hero.mesh.json', data.namelist())
        destination = self.root / 'installed'
        tool.install(archive, destination)
        self.assertTrue((destination / 'test.mesh/hero.mesh.json').is_file())
        report = tool.check_set(destination, ['test.mesh'])
        self.assertEqual(report['problems']['test.mesh'], [])
        self.assertIn('assets.mesh_replace.v1', tool.CAPABILITIES)

    def test_mesh_conflicts_match_case_and_overlapping_counts_only(self):
        self.mesh_package('test.any', [{'texture': 'HERO', 'source': 'hero.mesh.json'}])
        self.mesh_package('test.three', [{'texture': 'hero', 'native_vertices': 3, 'source': 'hero.mesh.json'}])
        self.mesh_package('test.four', [{'texture': 'hero', 'native_vertices': 4, 'source': 'hero.mesh.json'}])
        result = tool.check_set(self.root / 'mods', ['test.any', 'test.three', 'test.four'])
        self.assertEqual(sum('mesh conflict' in p for p in result['problems']['test.any']), 2)
        self.assertEqual(sum('mesh conflict' in p for p in result['problems']['test.three']), 1)
        result = tool.check_set(self.root / 'mods', ['test.three', 'test.four'])
        self.assertFalse(any('mesh conflict' in p for problems in result['problems'].values() for p in problems))

    def test_mesh_validation_rejects_ambiguous_targets_bad_geometry_and_unsafe_paths(self):
        target = {'texture': 'hero', 'source': 'hero.mesh.json'}
        folder = self.mesh_package('test.mesh', [target])
        manifest_path = folder / 'mod.json'
        manifest = json.loads(manifest_path.read_text())
        for targets in ([target, target], [dict(target, native_vertices=True)], [dict(target, native_vertices=0)],
                        [dict(target, texture='../hero')], [dict(target, source='../escape')], [dict(target, bones=True)]):
            manifest['meshes'] = targets
            manifest_path.write_text(json.dumps(manifest))
            with self.assertRaises(ValueError):
                tool.validate(folder)
        manifest['meshes'] = [target]
        manifest_path.write_text(json.dumps(manifest))
        (folder / 'hero.mesh.json').write_text('{}')
        with self.assertRaises(ValueError):
            tool.validate(folder)


if __name__ == '__main__':
    unittest.main()
