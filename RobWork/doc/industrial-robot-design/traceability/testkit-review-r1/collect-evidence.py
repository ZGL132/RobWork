"""TK-T13：收集实际运行参数和送验输入，不生成或修改任何测试判定。"""
import argparse
import datetime
import hashlib
import json
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
repo = args.repo.resolve()
out = args.out.resolve()
base = '6dc1141ae1e4d0db61baf1e7ab8d2ed414669c9f'

def git(*parts):
    return subprocess.check_output(['git', '-C', str(repo), *parts]).decode().strip()

def normalized_digest(raw):
    text = raw.decode('utf-8-sig').replace('\r\n', '\n').rstrip('\n') + '\n'
    return hashlib.sha256(text.encode()).hexdigest()

changed = git('diff', '--name-only', base, '--', 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit').splitlines()
files = []
for name in changed:
    raw = (repo / name).read_bytes()
    files.append(dict(path=name, sha256=normalized_digest(raw), rawSha256=hashlib.sha256(raw).hexdigest()))
executables = []
for directory, names in [('build/RobWorkStudio/bin/Release', ['sdurws_ird_testkit_test',
        'sdurws_ird_testkit_lsnchild', 'sdurws_ird_testdata_lint', 'sdurws_ird_optimization_test',
        'sdurws_ird_policy_contract_test', 'sdurws_ird_project_contract_test']),
        ('build/core-smoke/testkit/Release', ['sdurws_ird_testkit_test', 'sdurws_ird_testkit_lsnchild'])]:
    for name in names:
        path = repo / directory / (name + '.exe')
        executables.append(dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
cache = {}
for name in ['build', 'build/core-smoke']:
    cache[name] = [line for line in (repo / name / 'CMakeCache.txt').read_text(encoding='utf-8-sig').splitlines()
        if line.startswith(('RWS_BUILD_INDUSTRIALROBOT:', 'CMAKE_HOME_DIRECTORY:', 'CMAKE_CACHEFILE_DIR:',
                            'CMAKE_GENERATOR:', 'CMAKE_GENERATOR_PLATFORM:'))]
snapshot = dict(base=base, headBeforeImplementationCommit=git('rev-parse', 'HEAD'),
    branch=git('branch', '--show-current'), capturedUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
    sourceScope='本包testkit工作区源码；提交后由显式Base/Head范围检查绑定送验SHA。',
    sourceFiles=files, executables=executables, cache=cache)
(out / 'implementation-inputs.json').write_text(json.dumps(snapshot, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

commands = []
for path in [out/'integration/results.json', out/'policy-final/results.json',
             out/'report-smoke/results.json', out/'report-integration/results.json',
             out/'headers/results.json', out/'tools/tool-results.json', out/'lint-final/tool-results.json']:
    for record in json.loads(path.read_text(encoding='utf-8-sig')):
        commands.append(dict(source=str(path.relative_to(out)), **record))
for label, targets, tree, log in [
        ('最终集成增量构建', ['sdurws_ird_testkit_test', 'sdurws_ird_optimization_test',
         'sdurws_ird_policy_test', 'sdurws_ird_project_contract_test'], 'build', 'build-integration-final.log'),
        ('policy真实契约目标补建', ['sdurws_ird_policy_contract_test'], 'build', 'build-policy-contract.log'),
        ('独立冒烟增量构建', ['sdurws_ird_testkit_test'], 'build/core-smoke', 'build-smoke-final.log')]:
    commands.append(dict(name=label, command=['cmake', '--build', tree, '--config', 'Release', '--target', *targets, '--', '/m:1'],
                         log=log, actualExit=0))
commands.append(dict(name='ird_gates及自测', command=['cmake', '-DIRD_ROOT='+str(repo/'RobWork/RobWorkStudio/src/rwslibs/industrialrobot'),
    '-DIRD_SELFTEST_DIR='+str(repo/'build/testkit-review-gates'), '-P', 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/cmake/ird_gates.cmake'],
    log='ird-gates.log', actualExit=0))
earlier = []
for name in ['assertions-red', 'io-red', 'data-red', 'format-red', 'group-green3', 'testkit', 'json-final', 'format-green', 'smoke-final']:
    log = (out / (name + '.log')).read_text(encoding='utf-8-sig', errors='replace')
    filter_line = next((line for line in log.splitlines() if line.startswith('Note: Google Test filter = ')), None)
    earlier.append(dict(log=name+'.log', filterEvidence=filter_line,
        commandEvidence='过滤参数保留于原输出；此前完整shell调用未另存命令JSON，不据此伪造完整参数。最终覆盖命令见上方独立驱动结果。'))
(out / 'commands.json').write_text(json.dumps(dict(cwd=str(repo), commands=commands, earlierLogs=earlier),
    ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print('source files', len(files), 'executables', len(executables), 'commands', len(commands))
