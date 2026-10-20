"""记录被测源码、双模式构建绑定和程序摘要；不把任务分支记成主线交付。"""
import hashlib
import json
import pathlib
import re
import subprocess

out = pathlib.Path(__file__).resolve().parent
root = pathlib.Path(subprocess.check_output(
    ['git', '-C', str(out), 'rev-parse', '--show-toplevel'], encoding='utf-8').strip())


def git(*args):
    return subprocess.check_output(['git', '-C', str(root), *args], encoding='utf-8').strip()


baseline = dict(branch=git('branch', '--show-current'), parent=git('rev-parse', 'HEAD'),
                mergeParent=git('rev-parse', 'MERGE_HEAD'), main=git('rev-parse', 'redesign-main'),
                remoteMain=git('rev-parse', 'origin/redesign-main'), frozenMain=git('rev-parse', 'main'))
baseline['caches'] = {}
for mode, path in [('integration', root/'build/CMakeCache.txt'), ('standalone', out/'standalone/CMakeCache.txt')]:
    baseline['caches'][mode] = [line for line in path.read_text(encoding='utf-8').splitlines()
                              if line.startswith(('CMAKE_HOME_DIRECTORY:', 'RWS_BUILD_INDUSTRIALROBOT:'))]
(out/'baseline.json').write_text(json.dumps(baseline, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')

sources = git('diff', '--name-only', '402e5992', '--', 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot').splitlines()
sources.append('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/test/TestMainReport.cpp')
hashes = {path: hashlib.sha256((root/path).read_bytes()).hexdigest() for path in sorted(set(sources))}
binaries = [root/'build/RobWorkStudio/bin/Release'/('sdurws_ird_'+name+'_test.exe')
            for name in ('core', 'runtime', 'ui', 'ui_gui', 'optimization_contract')]
binaries.append(root/'build/RobWorkStudio/bin/Release/sdurws_ird_studio.exe')
binaries += list((out/'standalone').glob('*/Release/sdurws_ird_*_test.exe'))
for path in binaries:
    hashes[str(path.relative_to(root)).replace('\\', '/')] = hashlib.sha256(path.read_bytes()).hexdigest()
(out/'source-binary-hashes.json').write_text(json.dumps(hashes, indent=2)+'\n', encoding='utf-8')

tours = {}
for name in ('requirements-tour', 'modeling-tour', 'layout', 'auto'):
    lines = (out/(name+'.log')).read_text(encoding='utf-8-sig', errors='replace').splitlines()
    passes = [line for line in lines if re.search(r'\[ird-ui-smoke[^\]]*\] pass ', line)]
    failures = [line for line in lines if re.search(r'\[ird-ui-smoke[^\]]*\] (?:FAIL|fail) ', line)]
    assert not failures, (name, failures)
    tours[name] = dict(assertionsPassed=len(passes), png=len(list((out/name).glob('*.png'))),
                       doneMarkerPresent=any('] DONE' in line for line in lines),
                       failures=failures, tail=lines[-2:])
# modeling-tour尚未提供DONE；保留最后阶段实证，不能编造完成标记。
(out/'tour-summary.json').write_text(json.dumps(tours, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
