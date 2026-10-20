"""Rehearse texture install/toggle/conflict/restart through the shipped shared menu."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from chronicle_texture import create_package

OPEN_MANAGER = '0\n30 start\n35\n100 start\n105\n300 up\n305\n350 cross\n355\n500 l1\n505\n550 cross\n555\n'


def require(value, message):
    if not value:
        raise ValueError(message)


def run(bundle, data, snapshot, image, output):
    bundle, data = Path(bundle).resolve(strict=True), Path(data).resolve(strict=True)
    output = Path(output).absolute()
    require(not output.exists() and output.parent.is_dir(), 'Output must be new with an existing parent')
    require(all(p.lower() != 'ps2' for p in (*output.parts, *output.resolve().parts)), 'PS2 output forbidden')
    require(not output.resolve().is_relative_to(data) and not output.resolve().is_relative_to(bundle),
            'Evidence must be outside game data and bundle')
    manifest = json.loads((bundle / 'bundle.json').read_text())
    for name, item in manifest['files'].items():
        path = (bundle / name).resolve(strict=True)
        require(path.is_relative_to(bundle), 'Unsafe bundle manifest path')
        require(hashlib.sha256(path.read_bytes()).hexdigest() == item['sha256'], f'Bundle changed: {name}')
    require(not (bundle / 'mods').exists(), 'Bundle must not contain preinstalled mods')
    output.mkdir()
    save = output / 'save'
    env = {key: value for key, value in os.environ.items()
           if not key.upper().startswith(('DC_', 'GTEST_'))}
    if os.name == 'nt':
        windows = os.environ['SystemRoot']
        env['PATH'] = os.pathsep.join([str(Path(windows) / 'System32'), windows])
    tool = Path(__file__).with_name('chronicle_mod.py')
    result = {'schema_version': 1, 'complete': False, 'passed': False,
              'binary_sha256': manifest['files']['darkcloud.exe']['sha256'],
              'scope': 'Scripted controller input via title/shared menu; startup jump only; no profile writes by runner',
              'steps': []}

    def record():
        (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')

    def phase(name, extra='', expected=(), scene=False):
        script = output / (name + '.input')
        script.write_text('0\n' if scene else OPEN_MANAGER + extra)
        with (output / (name + '.log')).open('wb') as log:
            process = subprocess.run([str(bundle / 'darkcloud.exe'), '--offscreen', '--fast-load',
                                      '--jump', 'edit:0' if scene else 'title', '--frames', '650',
                                      '--data', str(data), '--save', str(save), '--input', str(script),
                                      '--screenshot', str(output / (name + '.png'))],
                                     cwd=bundle, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=120)
        require(process.returncode == 0, f'{name}: game failed; see log')
        report = json.loads((save / 'mods-report.json').read_text())
        shutil.copyfile(save / 'mods-report.json', output / (name + '-report.json'))
        if (save / 'mods-profile.json').exists():
            shutil.copyfile(save / 'mods-profile.json', output / (name + '-profile.json'))
        require(not report['errors'], f'{name}: report errors')
        # The report describes startup activation, not the menu's next-launch preview.
        require(report['load_order'] == list(expected), f'{name}: unexpected activation: {report}')
        result['steps'].append({'name': name, 'load_order': report['load_order'],
                                'states': {mod['id']: mod['state'] for mod in report['mods']}})
        record()
        print('passed: ' + name, flush=True)
        return report

    def install(identity):
        package = output / identity
        create_package(snapshot, image, identity, package)
        archive = output / (identity + '.zip')
        for args in [('pack', str(package), '--output', str(archive)),
                     ('install', str(archive), '--mods', str(save / 'mods'))]:
            subprocess.run([sys.executable, str(tool), *args], check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    record()
    phase('baseline', scene=True)
    install('beta.first')
    require(not (save / 'mods-profile.json').exists(), 'Installation created an enable profile')
    phase('installed-disabled')
    phase('enable-preview', '600 cross\n605\n')
    require(json.loads((save / 'mods-profile.json').read_text())['enabled']['beta.first'], 'Toggle was not saved')
    phase('enabled-restart', expected=['beta.first'])
    phase('enabled-scene', expected=['beta.first'], scene=True)
    install('beta.second')
    phase('second-installed-disabled', expected=['beta.first'])
    phase('conflict-preview', '580 down\n585\n600 cross\n605\n', ['beta.first'])
    conflict = phase('conflict-restart')
    require(len(conflict['mods']) == 2 and all(mod['state'] == 'blocked' and
            any('also replaced by' in problem for problem in mod['problems']) for mod in conflict['mods']),
            'Both owners must report a conflict reason')
    phase('conflict-scene', scene=True)
    phase('disable-conflict-preview', '580 down\n585\n600 cross\n605\n')
    phase('recovered-restart', expected=['beta.first'])
    phase('disable-original-preview', '600 cross\n605\n', ['beta.first'])
    phase('disabled-restart')
    phase('disabled-scene', scene=True)
    profile = json.loads((save / 'mods-profile.json').read_text())
    require(profile['enabled'] == {'beta.first': False, 'beta.second': False}, 'Final profile is not disabled')
    result.update(complete=True, passed=True,
                  visual_review='Review baseline/enabled/conflict/disabled scene PNGs separately; report success does not prove rendered replacement.')
    record()
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('bundle', 'data', 'snapshot', 'image', 'output'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    try:
        run(args.bundle, args.data, args.snapshot, args.image, args.output)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, str(error) + '\n')
