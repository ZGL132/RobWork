"""PRJ-T19: rebind only changed sources; preserve every existing audit decision.

Run from repository root after the implementation record is stable. This is a
batch evidence helper, not a gate, and never grants permissions or closes debt.
"""
import difflib
import hashlib
import json
import re
import subprocess
from functools import cache
from pathlib import Path

ROOT = Path.cwd()
BASE = '412e173b9079fa43a1f0ef50fe07d38e852b1940'
D = 'RobWork/doc/industrial-robot-design/'
P = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/project/'
E = D + 'traceability/project-review-r1/'
TF = D + 'tasks/foundation/PRJ-T19.json'
MF = D + 'reuse/changes/PRJ-T19.json'

def read(path):
    return (ROOT / path).read_text(encoding='utf-8-sig')

@cache
def baseline(path):
    return subprocess.check_output(['git', 'show', BASE + ':' + path]).decode('utf-8-sig')

def digest(text):
    return hashlib.sha256((text.removeprefix('\ufeff').replace('\r\n', '\n').rstrip('\n') + '\n').encode()).hexdigest()

def write(path, obj):
    dest = ROOT / path
    dest.parent.mkdir(parents=True, exist_ok=True)
    indent = 1 if path == D+'reuse/functions/project.json' or str(path).replace('\\', '/') == D+'reuse/functions/project.json' else 2
    dest.write_bytes((json.dumps(obj, ensure_ascii=False, indent=indent) + '\n').replace('\n', '\r\n').encode())

changed = sorted(set(x for x in subprocess.check_output(
    ['git', 'diff', '--name-only', BASE]).decode().splitlines() if x.startswith(P))
    | {P + 'test/ReviewRegressionTest.cpp'})
hashes = {path: digest(read(path)) for path in changed + [D + 'units/project.md', E + 'IMPLEMENTATION.md']}

def remap(source):
    path = source['path']
    if path not in changed:
        return
    before = baseline(path).replace('\r\n', '\n').splitlines()
    after = read(path).splitlines()
    ops = difflib.SequenceMatcher(a=before, b=after, autojunk=False).get_opcodes()
    def bound(x, end=False):
        for tag, a, b, c, z in ops:
            if tag == 'insert' and a == x:
                return c if end else z
            if a <= x < b:
                if tag == 'equal':
                    return c + x - a
                return z if end and x > a else c
        return len(after)
    start, end = bound(source['startLine'] - 1), bound(source['endLine'], True)
    if end <= start:
        end = start + 1
    if source['symbol'] not in '\n'.join(after[start:end]):
        raise RuntimeError('source symbol lost: ' + str(source))
    source.update(startLine=start + 1, endLine=end, sha256=digest('\n'.join(after[start:end])))

matrix = json.loads(baseline(D + 'reuse/functions/project.json'))
for row in matrix['functions']:
    for source in row['sources']:
        remap(source)

def region(path, start, end, symbol, label, target='sdurws_ird_project'):
    lines = read(path).splitlines()
    matrix['functions'].append({
        'functionId': 'project.PRJ_T19.' + label, 'ownerUnit': 'project',
        'requirements': ['CON-01', 'CON-02', 'PM-08'],
        'sources': [{'path': path, 'symbol': symbol, 'startLine': start, 'endLine': end,
                     'sha256': digest('\n'.join(lines[start-1:end]))}],
        'targets': [target], 'usageRole': 'owner', 'permissionRef': 'ARCH:3.6.1',
        'capabilityId': 'NONE', 'decision': 'business-specific', 'status': 'migrated',
        'search': {'command': '见traceability/project-review-r1/IMPLEMENTATION.md的定向检索与复用边界',
                   'conclusion': '产品持久化不变量编排及回归；复用现有core/codec/index/Win32/testkit，不引入通用平台'},
        'differences': 'PRJ-T19：' + label + '；用户授权评审整改，保持格式与公共签名；此行不关闭旧pending、不冒称独立验收。',
        'inputs': [{'path': path, 'sha256': hashes[path]}, {'path': P+'CMakeLists.txt', 'sha256': hashes[P+'CMakeLists.txt']}],
        'auditEvidence': [{'path': E+'IMPLEMENTATION.md', 'sha256': hashes[E+'IMPLEMENTATION.md']}],
        'nonOverlapEvidence': {'path': E+'IMPLEMENTATION.md', 'sha256': hashes[E+'IMPLEMENTATION.md']}})

# New helpers are individually identified; pre-existing unresolved functions keep
# their original status even when the owner's defect repair touches their file.
for name, symbol, marker in [
    ('src/Codec.cpp', 'isCommandTypeToken', 'bool isCommandTypeToken'),
    ('src/RevisionIndex.cpp', 'RevisionIndex::referencesObject', 'bool RevisionIndex::referencesObject'),
    ('src/ProjectStoreImpl.cpp', 'ProjectStoreImpl::refreshCommittedViewLocked', 'void ProjectStoreImpl::refreshCommittedViewLocked')]:
    lines = read(P+name).splitlines()
    start = next(i+1 for i, line in enumerate(lines) if line.startswith(marker))
    end = next(i+1 for i in range(start, len(lines)) if lines[i] == '}')
    region(P+name, start, end, symbol, symbol.replace('::', '.'))

path = P + 'test/ReviewRegressionTest.cpp'
lines = read(path).splitlines()
boundaries = [1] + [i+1 for i, line in enumerate(lines) if re.match(r'^(?:std::string readBytes|class |TEST_F\()', line)] + [len(lines)+1]
for start, stop in zip(boundaries, boundaries[1:]):
    text = '\n'.join(lines[start-1:stop-1])
    symbol = next((line.strip() for line in lines[start-1:stop-1] if line.startswith('TEST_F(')), None)
    if not symbol:
        symbol = next(x for x in ['writeBytes', 'readBytes', 'NoteHandler', 'ThrowBus', 'ThrowSink', 'class ProjectReviewRegression'] if x in text)
    region(path, start, stop-1, symbol, 'ReviewRegression.L'+str(start), 'sdurws_ird_project_test')

write(D+'reuse/functions/project.json', matrix)
repins = {}
for path in (ROOT / (D+'reuse/functions')).glob('*.json'):
    data = json.loads(path.read_text(encoding='utf-8-sig'))
    count = [0]
    def visit(node):
        if isinstance(node, dict):
            if node.get('path') in hashes and 'sha256' in node and 'startLine' not in node:
                value = hashes[node['path']]
                if value != node['sha256']:
                    node['sha256'] = value
                    count[0] += 1
            for value in node.values(): visit(value)
        elif isinstance(node, list):
            for value in node: visit(value)
    visit(data)
    if count[0]:
        write(path.relative_to(ROOT), data)
        repins[path.name] = count[0]

template = json.loads(read(D+'tasks/foundation/CORE-T12.json'))
matrix_paths = subprocess.check_output(['git', 'diff', '--name-only', BASE, '--', D+'reuse/functions/']).decode().splitlines()
matrix_paths = sorted(set(matrix_paths) | {D+'reuse/functions/project.json'})
write(TF, {
    'schemaVersion': 'ird-task/1', 'taskId': 'PRJ-T19', 'unit': 'project', 'masterWp': 'WP-C',
    'title': 'project首轮评审整改：提交事实、只读隔离、恢复闭包与归档完整性',
    'status': 'ready', 'branch': 'codex/core-review-fixes', 'dependsOn': [],
    'requirements': ['ARC-04', 'CON-01', 'CON-02', 'CON-04', 'PM-01', 'PM-04', 'PM-07', 'PM-08', 'PM-16', 'NFR-REL-01'],
    'designRefs': ['units/project.md#4', 'units/project.md#7', 'units/project.md#8', 'units/project.md#10', 'reuse/policy.md#1'],
    'allowedFiles': changed + [D+'units/project.md', TF, MF] + sorted(set(matrix_paths)) + [E+'**'],
    'forbiddenFiles': ['RobWork/RobWork/src/**', 'RobWork/RobWorkSim/src/**', 'vcpkg/**'],
    'outputs': ['project实施、回归、原始证据和绑定；本地可回滚提交；待所有者另开独验'],
    'verify': ['cmake --build build --config Release --target sdurws_ird_project_test_report sdurws_ird_project_contract_test_report',
               'cmake --build build/core-smoke --config Release --target sdurws_ird_project_test',
               'pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1 -TaskFile '+TF],
    'interUnit': False,
    'acceptance': ['13项确定反例以真实文件/API断言回归，提交前失败保持旧HEAD、提交后观察失败仍Committed',
                   '只读打开与草稿读取不删除writer在途文件；跨进程查询整体刷新闭包且对象查询不暴露残留',
                   '完整project套件各一次、定向真实进程与归档消费者、独立构建及性能/门禁保留真实结果，不以FAIL冒充PASS',
                   '保留core/Win32/Boost.JSON供给、格式和公共签名；旧pending/诊断提案不自行裁决，独验不自签'],
    'note': '所有者明确指定本C盘工作树和分支，覆盖旧redesign-main流程；起点'+BASE+'。用户2026-10-28要求按已完成评审修改。此最小契约仅供H6逐文件凭证与范围机器核验，不是重复总台账；status不代表验收通过。旧pending造成准入阻断时保留真实FAIL，不放宽门禁。',
    'governanceAcceptance': template['governanceAcceptance'], 'reuseGateAcceptance': template['reuseGateAcceptance'],
    'reuse': {'policy': 'H1-H6', 'manifest': MF}})
write(MF, {'schemaVersion': 'ird-reuse-change/1', 'taskId': 'PRJ-T19', 'taskFile': TF,
    'files': [{'path': path, 'sha256': hashes[path], 'mode': 'business', 'capabilities': [],
               'boundary': 'PRJ-T19：产品修订/恢复/引用/归档编排和回归；保留已有core摘要身份、Win32原语及编码契约，不授予旧pending新批准，详见本批实施记录',
               'verify': E+'IMPLEMENTATION.md'} for path in changed]})
print('source files:', len(changed), 'input repins:', repins)
unresolved = sorted({row['functionId'] for row in matrix['functions']
    if row['status'] not in ('migrated', 'closed') and any(s['path'] in changed for s in row['sources'])})
print('preserved unresolved:', len(unresolved), *unresolved, sep='\n')
coverage = {}
for path in changed:
    covered = set()
    for row in matrix['functions']:
        for source in row['sources']:
            if source['path'] == path:
                covered.update(range(source['startLine'], source['endLine']+1))
    missing = [i+1 for i, line in enumerate(read(path).splitlines()) if line.strip() and i+1 not in covered]
    if missing:
        coverage[path] = missing
write(E+'binding-audit.json', {'base': BASE, 'preservedUnresolved': unresolved, 'uncoveredNonemptyLines': coverage,
    'note': '此结构核查不授予旧pending批准，不能替代正式复用门禁或独立语义验收'})
print('files with uncovered lines:', len(coverage))
