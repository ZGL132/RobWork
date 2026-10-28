"""testkit 第一轮整改工具回归：独立退出码 oracle；只写本次专属临时树。"""
import argparse
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--pwsh', required=True)
parser.add_argument('--only', choices=['lint', 'scan'])
args = parser.parse_args()
repo = args.repo.resolve()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
unit = repo / 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit'
data = unit.parent / 'testdata'
results = []

def run(name, command, expected):
    if args.only and not name.startswith(args.only + '-'):
        return
    completed = subprocess.run([str(x) for x in command], cwd=repo,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
    (out / (name + '.log')).write_bytes(completed.stdout)
    results.append(dict(name=name, command=[str(x) for x in command],
                        expectedExit=expected, actualExit=completed.returncode,
                        status='PASS' if completed.returncode == expected else 'FAIL'))

with tempfile.TemporaryDirectory(prefix='ird-testkit-tools-') as temporary:
    root = Path(temporary)
    for name, relative, expected in [
        ('clean', 'include/sdurws/ird/core/Identity.hpp', 0),
        ('library', 'lib/sdurws_ird_testkit.lib', 1),
        ('header', 'include/sdurws/ird/testkit/Fixture.hpp', 1),
        ('dataset', 'share/testdata/input.json', 1),
        ('near-name', 'include/testdata_notes/sdurws_ird_testkit_notes.txt', 1),
        ('near-directory', 'include/sdurws/ird/testkit_notes/Foo.hpp', 0),
        ('near-library', 'lib/example_sdurws_ird_testkit.lib', 0),
        ('missing-root', None, 2)]:
        install = root / name
        if relative:
            path = install / relative
            path.parent.mkdir(parents=True)
            path.write_text('isolated fixture', encoding='utf-8')
        run('scan-' + name, [args.pwsh, '-NoProfile', '-File',
            unit / 'tools/install-scan/ird-install-scan.ps1', '-InstallRoot', install], expected)

    base = root / 'data'
    family = Path('golden/pol-collision-analytic')
    shutil.copytree(data / family, base / family)
    shutil.copytree(data / 'tolerance/pol-analytic', base / 'tolerance/pol-analytic')
    shutil.copyfile(data / 'requirements-ids.json', base / 'requirements-ids.json')
    manifest = base / family / '1.0.0/manifest.json'
    original = manifest.read_text(encoding='utf-8')
    exe = repo / 'build/RobWorkStudio/bin/Release/sdurws_ird_testdata_lint.exe'
    command = [exe, '--root', base]
    run('lint-default-dict', command, 0)
    (base / 'tolerance').rename(base / 'hidden-tolerance')
    run('lint-missing-profile', command, 1)
    (base / 'hidden-tolerance').rename(base / 'tolerance')
    value = json.loads(original)
    value['coveredAt'] = ['AT-999']
    manifest.write_text(json.dumps(value, ensure_ascii=False), encoding='utf-8')
    run('lint-unknown-at', command, 1)
    value = json.loads(original)
    value.pop('parameters', None)
    manifest.write_text(json.dumps(value, ensure_ascii=False), encoding='utf-8')
    run('lint-optional-parameters', command, 0)
    (base / 'requirements-ids.json').write_text('{}', encoding='utf-8')
    run('lint-malformed-dictionary', command, 1)
    run('lint-missing-root', [exe, '--root', root / 'absent-data'], 2)

(out / 'tool-results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
for result in results:
    print(result['status'], result['name'], result['actualExit'])
raise SystemExit(any(x['status'] != 'PASS' for x in results))
