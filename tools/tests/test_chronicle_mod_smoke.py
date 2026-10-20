import contextlib
import io
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('smoke', Path(__file__).parents[1] / 'chronicle_mod_smoke.py')
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


class RetailSmoke(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / 'build'
        metadata = self.build / 'CMakeFiles' / '1' / 'CMakeCXXCompiler.cmake'
        metadata.parent.mkdir(parents=True)
        metadata.write_text('set(CMAKE_CXX_COMPILER "clang++")\nset(CMAKE_CXX_COMPILER_ID "Clang")\nset(CMAKE_CXX_COMPILER_VERSION "23.1.2")\n')
        self.metadata = metadata
        (self.build / ('darkcloud_tests.exe' if os.name == 'nt' else 'darkcloud_tests')).write_bytes(b'fixture')
        self.data = self.root / 'data'
        self.data.mkdir()
        self.output = self.root / 'results'

    def invoke(self, **kwargs):
        with contextlib.redirect_stdout(io.StringIO()):
            return smoke.run_smoke(self.build, self.data, self.output, ['retail'], **kwargs)

    def fake_run(self, args, **kwargs):
        name = args[1].split('=', 1)[1]
        suite, case = name.split('.')
        xml = Path(args[2].split('xml:', 1)[1])
        xml.write_text(f'<testsuites><testsuite><testcase classname="{suite}" name="{case}" status="run" result="completed"/></testsuite></testsuites>')
        return subprocess.CompletedProcess(args, 0)

    def test_isolation_identity_and_environment(self):
        with patch.dict(os.environ, {'GTEST_FILTER': 'Other.*', 'GTEST_TOTAL_SHARDS': '2', 'DC_DATA': 'wrong', 'DC_OTHER_AUDIT': 'wrong'}), patch.object(smoke.subprocess, 'run', side_effect=self.fake_run) as run:
            report = self.invoke()
        self.assertTrue(report['passed'])
        self.assertEqual(len(run.call_args_list), 7)
        folders = set()
        for call in run.call_args_list:
            env = call.kwargs['env']
            self.assertNotIn('GTEST_TOTAL_SHARDS', env)
            self.assertNotIn('GTEST_FILTER', env)
            self.assertNotIn('DC_OTHER_AUDIT', env)
            self.assertEqual(env['DC_DATA'], str(self.data.resolve()))
            folders.add(str(call.kwargs['cwd']))
        self.assertEqual(len(folders), 7)
        self.assertEqual(json.loads((self.output / 'report.json').read_text())['binary_sha256'], report['binary_sha256'])
        with self.assertRaisesRegex(ValueError, 'new directory'):
            self.invoke()

    def test_skip_missing_empty_and_wrong_test_never_pass(self):
        xml = self.root / 'result.xml'
        for content, expected in [('<testsuites/>', 'failed'),
            ('<testsuite><testcase/></testsuite>', 'failed'),
            ('<testsuite><testcase classname="S" name="T" status="run" result="skipped"><skipped/></testcase></testsuite>', 'skipped'),
            ('<testsuite><testcase classname="S" name="T" status="notrun" result="suppressed"/></testsuite>', 'failed'),
            ('<testsuite><testcase classname="S" name="T" status="run" result="completed"><failure/></testcase></testsuite>', 'failed')]:
            xml.write_text(content)
            self.assertEqual(smoke.classify_result(xml, 'S.T', 0)[0], expected)
        xml.unlink()
        self.assertEqual(smoke.classify_result(xml, 'S.T', 0)[0], 'failed')
        self.assertEqual(smoke.classify_result(xml, 'S.T', 1)[0], 'failed')

    def test_mixed_suites_select_native_profile_and_keep_render_evidence(self):
        def run(args, **kwargs):
            folder = Path(kwargs['cwd'])
            (folder / 'lighting-1.png').write_bytes(b'fixture-image')
            return self.fake_run(args, **kwargs)
        with patch.dict(os.environ, {'DC_GEORAMA_REQUEST_AUDIT': 'stale',
                                     'DC_GEORAMA_PLACEMENT_AUDIT': 'stale',
                                     'DC_BACK_FLOOR_AUDIT': 'stale'}), \
                patch.object(smoke.subprocess, 'run', side_effect=run) as calls, \
                contextlib.redirect_stdout(io.StringIO()):
            report = smoke.run_smoke(self.build, self.data, self.output,
                                     ['georama', 'rendering', 'georama'])
        self.assertTrue(report['passed'])
        self.assertEqual(len(report['scenarios']), 7)
        for item, call in zip(report['scenarios'], calls.call_args_list):
            expected = smoke.SCENARIO_PROFILES.get(item['name'], 'DC_BACK_FLOOR_AUDIT')
            env = call.kwargs['env']
            self.assertEqual(env[expected], item['directory'])
            for other in {'DC_GEORAMA_REQUEST_AUDIT', 'DC_GEORAMA_PLACEMENT_AUDIT',
                          'DC_BACK_FLOOR_AUDIT'} - {expected}:
                self.assertNotIn(other, env)
            self.assertEqual('DC_EXPANDED_BACK_FLOOR_AUDIT' in env, expected == 'DC_BACK_FLOOR_AUDIT')
            self.assertEqual(item['profile_environment'], expected)
            self.assertEqual(len(item['images']), 1)
            self.assertTrue((self.output / item['images'][0]).is_file())

    def test_timeout_is_recorded_and_other_scenarios_continue(self):
        attempts = 0
        def run(args, **kwargs):
            nonlocal attempts
            attempts += 1
            if attempts == 1:
                raise subprocess.TimeoutExpired(args, 1)
            return self.fake_run(args, **kwargs)
        with patch.object(smoke.subprocess, 'run', side_effect=run):
            report = self.invoke(timeout=1)
        self.assertTrue(report['complete'])
        self.assertFalse(report['passed'])
        self.assertEqual(report['scenarios'][0]['status'], 'timeout')
        self.assertEqual(report['scenarios'][-1]['status'], 'passed')

    def test_compiler_guard_runs_before_any_output(self):
        self.metadata.write_text(self.metadata.read_text().replace('"Clang"', '"GNU"'))
        with self.assertRaisesRegex(ValueError, 'must use Clang'):
            self.invoke()
        self.assertFalse(self.output.exists())

    def test_output_and_timeout_validation(self):
        for path in (self.data / 'audit', self.root / 'ps2' / 'audit'):
            with self.assertRaises(ValueError):
                smoke.run_smoke(self.build, self.data, path)
            self.assertFalse(path.exists())
        for timeout in (0, -1, float('nan'), float('inf')):
            with self.assertRaises(ValueError):
                self.invoke(timeout=timeout)
        self.assertFalse(self.output.exists())

    def test_launch_failure_is_not_a_pass(self):
        with patch.object(smoke.subprocess, 'run', side_effect=OSError('missing DLL')):
            report = self.invoke()
        self.assertFalse(report['passed'])
        self.assertTrue(all(item['status'] == 'failed' for item in report['scenarios']))


if __name__ == '__main__':
    unittest.main()
