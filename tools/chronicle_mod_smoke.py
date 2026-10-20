#!/usr/bin/env python3
"""Run built-in PAL mod regressions in isolated processes and save profiles."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

SUITES = {
    'retail': ['SharedActionReachesBackFloorThroughNativeScriptBoundary', 'WiseOwlTransport',
               'ShipwreckTransport', 'TempleTransport', 'MoonSeaTransport', 'GalleryTransport', 'DemonShaftTransport'],
    'population': ['PublicMaximum' + name + 'Population' for name in
                   ('Cave', 'Forest', 'Shipwreck', 'Temple', 'MoonSea', 'Gallery', 'DemonShaft')],
    'chests': ['OwnedChestLootPreservesMapAndCrystal'] + ['OwnedChestLoot' + name for name in
               ('Forest', 'Shipwreck', 'Temple', 'MoonSea', 'Gallery', 'DemonShaft')],
    'lifecycle': ['MaximumPopulationReturnsToRetailLimits', 'QuitWithMaximumPopulation',
                  'ExceptionWithMaximumPopulation', 'PartialPopulationIsReported', 'RejectedManagedLayoutUsesRetail'],
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def classify_result(path, expected, returncode):
    """Exit zero alone is insufficient: empty filters and skipped tests exit zero."""
    if returncode != 0:
        return 'failed', f'process exited with code {returncode}'
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError) as error:
        return 'failed', f'missing or invalid test report: {error}'
    cases = list(root.iter('testcase'))
    if len(cases) != 1 or cases[0].get('classname', '') + '.' + cases[0].get('name', '') != expected:
        return 'failed', 'test report must contain exactly the requested scenario'
    case = cases[0]
    if case.find('failure') is not None or case.find('error') is not None:
        return 'failed', 'test report contains a failure'
    if case.find('skipped') is not None or case.get('result') == 'skipped':
        return 'skipped', 'scenario skipped; see process.log for the unmet prerequisite'
    if case.get('status') != 'run' or case.get('result') != 'completed':
        return 'failed', 'test report does not confirm completed execution'
    return 'passed', ''


def compiler_metadata(build):
    records = []
    for path in sorted((build / 'CMakeFiles').glob('*/CMakeCXXCompiler.cmake')):
        text = path.read_text(encoding='utf-8')
        fields = {}
        for key in ('CMAKE_CXX_COMPILER', 'CMAKE_CXX_COMPILER_ID', 'CMAKE_CXX_COMPILER_VERSION'):
            match = re.search(r'set\(' + key + r' "([^"\n]+)"\)', text)
            require(match is not None, f'incomplete compiler metadata: {path}')
            fields[key] = match.group(1)
        records.append(fields)
    require(records and all(record == records[0] for record in records),
            'missing or conflicting CMake C++ compiler metadata')
    require(records[0]['CMAKE_CXX_COMPILER_ID'] in ('Clang', 'AppleClang'),
            'the test build must use Clang; this runner does not build or switch compilers')
    return records[0]


def run_smoke(build, data, output, suites=None, timeout=120):
    build, data, output = (Path(p).resolve() for p in (build, data, output))
    require(math.isfinite(timeout) and timeout > 0, 'timeout must be finite and positive')
    require(data.is_dir(), 'data must be an existing extracted PAL data directory')
    require('ps2' not in (part.lower() for part in output.parts), 'output must not be inside a PS2 directory')
    require(output != data and data not in output.parents, 'output must be outside the game data tree')
    require(not output.exists(), 'output must be a new directory; previous evidence is never reused')
    suites = suites or list(SUITES)
    require(all(suite in SUITES for suite in suites), 'unknown smoke suite')
    names = list(dict.fromkeys('ModBackFloorRetail.' + name for suite in suites for name in SUITES[suite]))
    binary = build / ('darkcloud_tests.exe' if os.name == 'nt' else 'darkcloud_tests')
    require(binary.is_file(), f'missing test executable: {binary}')
    compiler = compiler_metadata(build)
    with binary.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    report = {'schema_version': 1, 'binary': str(binary),
              'binary_sha256': digest,
              'compiler_metadata': compiler, 'data': str(data), 'suites': suites,
              'scope': 'built-in PAL framework scenarios; not arbitrary package certification',
              'complete': False, 'passed': False,
              'scenarios': [{'name': name, 'status': 'pending'} for name in names]}
    output.mkdir(parents=True, exist_ok=False)

    def save():
        temporary = output / 'report.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        temporary.replace(output / 'report.json')

    save()
    for item in report['scenarios']:
        folder = output / item['name'].split('.', 1)[1]
        folder.mkdir()
        xml = folder / 'result.xml'
        env = {key: value for key, value in os.environ.items()
               if not key.upper().startswith('GTEST_') and not key.upper().startswith('DC_')}
        env.update(DC_DATA=str(data), DC_BACK_FLOOR_AUDIT=str(folder), DC_EXPANDED_BACK_FLOOR_AUDIT='1')
        item.update(status='running', directory=str(folder))
        save()
        start = time.monotonic()
        try:
            with (folder / 'process.log').open('wb') as log:
                result = subprocess.run([str(binary), '--gtest_filter=' + item['name'],
                                         '--gtest_output=xml:' + str(xml), '--gtest_color=no'],
                                        cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT,
                                        timeout=timeout, check=False)
            item['returncode'] = result.returncode
            item['status'], item['reason'] = classify_result(xml, item['name'], result.returncode)
        except subprocess.TimeoutExpired:
            item.update(status='timeout', reason=f'exceeded {timeout:g} seconds; process terminated')
        except OSError as error:
            item.update(status='failed', reason=str(error))
        item['seconds'] = round(time.monotonic() - start, 3)
        save()
        print(f"{item['status']}: {item['name']}", flush=True)
    report['complete'] = True
    report['passed'] = all(item['status'] == 'passed' for item in report['scenarios'])
    save()
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', required=True, help='existing Clang CMake test build directory')
    parser.add_argument('--data', required=True, help='legally supplied extracted PAL game data')
    parser.add_argument('--output', required=True, help='new directory for reports and isolated save profiles')
    parser.add_argument('--suite', action='append', choices=SUITES, help='repeatable; default runs all suites')
    parser.add_argument('--timeout', type=float, default=120, help='seconds per process; default 120')
    args = parser.parse_args(argv)
    try:
        report = run_smoke(args.build, args.data, args.output, args.suite, args.timeout)
        return 0 if report['passed'] else 1
    except (ValueError, OSError, UnicodeError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
