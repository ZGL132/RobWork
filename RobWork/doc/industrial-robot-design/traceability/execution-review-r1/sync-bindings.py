"""EX-T11：按真实源码差异重钉片段/输入摘要，不改变既有语义审计或验收状态。"""
import difflib
import hashlib
import json
import re
from pathlib import Path
import subprocess

ROOT = Path.cwd()
BASE = '6a49e128c00f8cca3943083819ea59aea16a7be9'
DESIGN = 'RobWork/doc/industrial-robot-design/'
PRODUCT = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/'
TRACE = DESIGN + 'traceability/execution-review-r1/'
TASK = DESIGN + 'tasks/foundation/EX-T11.json'
MANIFEST = DESIGN + 'reuse/changes/EX-T11.json'

def read(path):
    return (ROOT / path).read_text(encoding='utf-8-sig')

def digest(value):
    normalized = value.removeprefix('\ufeff').replace('\r\n', '\n').rstrip('\n') + '\n'
    return hashlib.sha256(normalized.encode('utf-8')).hexdigest()

def write(path, value):
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    # testkit 历史矩阵采用单空格；保留各自格式，避免凭证重钉产生整库格式噪音。
    indent = 1 if path.endswith('/functions/testkit.json') else 2
    target.write_text(json.dumps(value, ensure_ascii=False, indent=indent) + '\n', encoding='utf-8')

changed = subprocess.check_output(['git', 'diff', '--name-only', BASE], cwd=ROOT).decode().splitlines()
changed += [PRODUCT + 'execution/test/WorkerWriterTest.cpp']
products = sorted(set(p for p in changed if p.startswith(PRODUCT)))
new_hash = {p: digest(read(p)) for p in products}
mappings = {}
for path in products:
    try:
        before = subprocess.check_output(['git', 'show', BASE + ':' + path], cwd=ROOT,
                                         stderr=subprocess.DEVNULL).decode('utf-8-sig').splitlines()
    except subprocess.CalledProcessError:
        continue
    after = read(path).splitlines()
    mappings[path] = (after, difflib.SequenceMatcher(a=before, b=after, autojunk=False).get_opcodes())

def remap(source):
    path = source['path']
    if path not in mappings:
        return
    after, operations = mappings[path]
    def bound(point, end=False):
        for tag, old_start, old_end, new_start, new_end in operations:
            if tag == 'insert' and old_start == point:
                return new_start if end else new_end
            if old_start <= point < old_end:
                if tag == 'equal':
                    return new_start + point - old_start
                return new_end if end and point > old_start else new_start
        return len(after)
    start = bound(source['startLine'] - 1)
    end = bound(source['endLine'], True)
    if end <= start:
        end = start + 1
    if source['symbol'] not in '\n'.join(after[start:end]):
        raise RuntimeError('symbol lost: ' + json.dumps(source, ensure_ascii=False))
    source.update(startLine=start + 1, endLine=end,
                  sha256=digest('\n'.join(after[start:end])))

updated = []
matrices = {}
for file in sorted((ROOT / (DESIGN + 'reuse/functions')).glob('*.json')):
    path = file.relative_to(ROOT).as_posix()
    matrix = json.loads(subprocess.check_output(['git', 'show', BASE + ':' + path], cwd=ROOT)
                        .decode('utf-8-sig'))
    original = json.dumps(matrix, ensure_ascii=False)
    for function in matrix['functions']:
        for source in function['sources']:
            remap(source)
    def repin(node):
        if isinstance(node, dict):
            if node.get('path') in new_hash and 'sha256' in node and 'startLine' not in node:
                node['sha256'] = new_hash[node['path']]
            for value in node.values(): repin(value)
        elif isinstance(node, list):
            for value in node: repin(value)
    repin(matrix)
    matrices[matrix['unit']] = matrix
    if original != json.dumps(matrix, ensure_ascii=False):
        write(path, matrix)
        updated.append(path)

matrix = matrices['execution']

def add_region(relative, symbol, label, target, start=None, end=None):
    path = PRODUCT + 'execution/' + relative
    lines = read(path).splitlines()
    if start is None:
        start = next(i for i, line in enumerate(lines) if symbol in line)
        indent = len(lines[start]) - len(lines[start].lstrip())
        opened = next(i for i in range(start, len(lines)) if '{' in lines[i])
        end = next(i + 1 for i in range(opened + 1, len(lines))
                   if lines[i] in (' ' * indent + '}', ' ' * indent + '};'))
    region = '\n'.join(lines[start:end])
    assert symbol in region, (path, symbol)
    row = {
        'functionId': 'execution.EX_T11.' + label, 'ownerUnit': 'execution',
        'requirements': ['TASK-03', 'NFR-REL-02', 'NFR-MNT-01'],
        'sources': [{'path': path, 'symbol': symbol, 'startLine': start + 1,
                     'endLine': end, 'sha256': digest(region)}],
        'targets': [target], 'usageRole': 'owner', 'permissionRef': 'ARCH:3.6.1',
        'capabilityId': 'NONE', 'decision': 'business-specific', 'status': 'migrated',
        'search': {
            'command': 'rg -n "class ThreadPool|class ThreadTask|ThreadSafeQueue|subscribe|JobObject|CancelIoEx|ChannelPair" RobWork/RobWork/src RobWork/RobWorkSim/src RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core',
            'conclusion': 'ThreadPool/ThreadTask/ThreadSafeQueue 不含尝试接纳和原项目归档；core 身份/事件值继续直接消费。Win32 薄适配只管理本任务句柄与进程，不新增通用平台或数值核心；独立测试 oracle 保留。'},
        'differences': 'EX-T11：' + label + '；生命周期/字节和身份边界、既有权威调用及原始回归见本批记录；不扩支持范围，实施不等于独验。',
        'inputs': [{'path': path, 'sha256': new_hash[path]},
                   {'path': PRODUCT + 'execution/CMakeLists.txt',
                    'sha256': new_hash[PRODUCT + 'execution/CMakeLists.txt']}],
        'auditEvidence': [{'path': TRACE + 'README.md', 'sha256': digest(read(TRACE + 'README.md'))}],
        'nonOverlapEvidence': {'path': TRACE + 'README.md', 'sha256': digest(read(TRACE + 'README.md'))},
        'auditBoundary': '仅本轮变动功能区段；独立验收及原矩阵 pending 债务不由此关闭。'}
    matrix['functions'].append(row)

for relative, symbols, target in [
    ('src/WorkerSupervisor.cpp', ['void closeProcessResources()',
        'void WorkerSupervisor::requestCooperativeCancel(', 'void WorkerSupervisor::requestPause(',
        'WorkerSupervisor::aggregateJobMemoryBytes()', 'void WorkerSupervisor::poll()',
        'void WorkerSupervisor::handleFrame(', 'void WorkerSupervisor::processExit('], 'sdurws_ird_execution'),
    ('worker/main.cpp', ['bool send(', 'bool sendWithSeq(', 'bool sendFragmented(',
        'std::uint64_t nextSequence()', 'void commandReaderLoop(', 'void pollCommands(',
        'int runScript(', 'int APIENTRY wWinMain('], 'sdurws_ird_execution_worker'),
    ('src/win32/ProcessLauncher.cpp', ['bool RealProcessOps::createProcess(',
        'LaunchResult launchWorkerProcess('], 'sdurws_ird_execution'),
    ('src/win32/ChannelPair.cpp', ['ChannelPair::~ChannelPair()',
        'void ChannelPair::cancelReadAndWait()', 'std::vector<std::uint8_t> ChannelPair::drainAfterExit()'], 'sdurws_ird_execution'),
    ('src/win32/JobScope.cpp', ['void JobScope::reset()'], 'sdurws_ird_execution'),
    ('contract_test/ContractTestMain.cpp', ['static int runFaultWorkerChild('], 'sdurws_ird_execution_contract_test'),
]:
    for symbol in symbols:
        label = Path(relative).stem + '.' + re.sub(r'[^A-Za-z0-9_]+', '_', symbol).strip('_')
        add_region(relative, symbol, label, target)

# 新增用例和本轮修改的 C1 用例按各自 TEST 块登记，保留独立预期而不扩大旧审计结项。
for path in products:
    if '/test/' not in path and '/contract_test/' not in path:
        continue
    relative = path.split('/execution/', 1)[1]
    lines = read(path).splitlines()
    for index, line in enumerate(lines):
        if line.startswith(('TEST(', 'TEST_F(')) and ('EXR' in line or 'UnsubscribeDuringDelivery' in line):
            label = Path(relative).stem + '.' + re.sub(r'[^A-Za-z0-9_]+', '_', line).strip('_')
            add_region(relative, line, label, 'sdurws_ird_execution_contract_test'
                       if '/contract_test/' in path else 'sdurws_ird_execution_test')

writer = 'test/WorkerWriterTest.cpp'
writer_lines = read(PRODUCT + 'execution/' + writer).splitlines()
first_test = next(i for i, line in enumerate(writer_lines) if line.startswith('TEST('))
add_region(writer, 'struct AuditPipe', 'WorkerWriterTest.SameTranslationUnitTestSupport',
           'sdurws_ird_execution_test', 0, first_test)

# 本包触及文件的旧候选补当前适用判断；migrated 表示保留/适配方案已落地，绝非 closed/独验。
# 不改变其他文件的 pending，不将“调用数”作为依据，也不修改历史原始证据。
rejections = {
    'UTIL-BINARCHIVE': 'BINArchive::writeValue 写 sizeof(val) 原生表示，doWrite(string) 使用流插入长度；不能满足 IRDCHN/1 固定小端帧头、五元组定宽块及手工字节 oracle。保留当前私有协议编码，不新建通用 codec。',
    'CORE-EVENT': 'core ReferenceEventBus 是同步参考实现，缺少 execution 已批准的专用投递线程与 C1 精确订阅位等待边界。保留 REUSE-T41 已装配的唯一 execution 总线，不增加第三套总线。',
    'UTIL-THREAD': 'ThreadPool/ThreadTask/ThreadSafeQueue 不管理五元组、尝试接纳、原项目归档、Job 所有权和资源节流。保留 execution 业务编排，标准库/Win32 只作所需薄适配。',
    'TEST-HARNESS': '保留现有 gtest/testkit 入口；额外分派仅限本 exe 的自有故障/恢复子进程，测试框架不提供该产品协议，生产目标不链接 testkit。',
    'NONE': '保留 core 身份和状态、runtime 名称/编译 judge、evidence 判定/工厂、project 写协议；本段仅承担 execution 业务编排或测试 oracle，不新增数值核心、上游扩展或 owner。'
}
for function in matrix['functions']:
    if function['decision'] not in ('pending', 'proposal'):
        continue
    if not any(source['path'] in products for source in function['sources']):
        continue
    if function['decision'] == 'proposal':
        continue
    reason = rejections[function['capabilityId']]
    function.update(usageRole='owner', capabilityId='NONE', decision='business-specific', status='migrated')
    function['search'] = {
        'command': 'rg -n "writeValue|doWrite|class ThreadPool|class ThreadTask|class ThreadSafeQueue|ReferenceEventBus|removeSink" RobWork/RobWork/src/rw/common/BINArchive.hpp RobWork/RobWork/src/rw/common/BINArchive.cpp RobWork/RobWork/src/rw/common/ThreadPool.hpp RobWork/RobWork/src/rw/common/ThreadTask.hpp RobWork/RobWork/src/rw/common/ThreadSafeQueue.hpp RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/src/Events.cpp',
        'conclusion': reason}
    function['differences'] += '；EX-T11 当前适用性：' + reason + ' 实施落位，独立语义验收未完成。'
    function['nonOverlapEvidence'] = {'path': TRACE + 'README.md', 'sha256': digest(read(TRACE + 'README.md'))}
    function['auditBoundary'] = '仅本包改动文件的候选适用判断；保留原始独验，未标 closed 或修改任务状态。'

# 既有矩阵未登记的相邻函数/内联类型也具名绑定，满足改动文件的完整归属要求。
def covered_lines(path):
    covered = set()
    for function in matrix['functions']:
        for source in function['sources']:
            if source['path'] == path:
                covered.update(range(source['startLine'], source['endLine'] + 1))
    return covered

for path in products:
    if '/execution/' not in path:
        continue
    relative = path.split('/execution/', 1)[1]
    lines = read(path).splitlines()
    target = ('sdurws_ird_execution_contract_test' if '/contract_test/' in path else
              'sdurws_ird_execution_test' if '/test/' in path else
              'sdurws_ird_execution_worker' if '/worker/' in path else 'sdurws_ird_execution')
    if path.endswith('.cpp'):
        for index, line in enumerate(lines):
            if not line or line[0].isspace() or line.startswith(('#', '/', '*', 'using ', 'namespace ', 'return ')):
                continue
            type_match = re.match(r'(class|struct|enum class)\s+[A-Za-z_][\w:]*', line)
            is_function = '(' in line and '=' not in line.split('(', 1)[0]
            if not type_match and not is_function:
                continue
            try:
                opening = next(i for i in range(index, min(len(lines), index + 15)) if '{' in lines[i])
                if ';' in '\n'.join(lines[index:opening]):
                    continue
                end = next(i + 1 for i in range(opening + 1, len(lines)) if lines[i] in ('}', '};'))
            except StopIteration:
                continue
            covered = covered_lines(path)
            if all(i + 1 in covered or not lines[i].strip() for i in range(index, end)):
                continue
            add_region(relative, line, Path(relative).stem + '.RetainedDefinition.L' + str(index + 1),
                       target, index, end)
    # 相邻注释、include、命名空间声明并入最近的真实功能区段，避免伪造注释功能行。
    # 每个区段仍绑定其可辨识符号；范围/摘要检查不替代 README 中的语义审查。
    sources = sorted((source for function in matrix['functions'] for source in function['sources']
                      if source['path'] == path), key=lambda source: source['startLine'])
    if not sources:
        contract_symbols = {
            'include/sdurws/ird/execution/ChannelProtocol.hpp': 'struct ChannelFrame',
            'include/sdurws/ird/execution/WorkerSupervisor.hpp': 'class WorkerSupervisor',
            'src/win32/ChannelPair.hpp': 'class ChannelPair',
            'src/win32/JobScope.hpp': 'class JobScope',
            'src/win32/ProcessLauncher.hpp': 'class IProcessOps',
        }
        if relative not in contract_symbols:
            raise RuntimeError('missing semantic source: ' + path)
        add_region(relative, contract_symbols[relative], Path(relative).stem + '.PublicOrPlatformContract',
                   target, 0, len(lines))
        continue
    sources[0]['startLine'] = 1
    frontier = sources[0]
    for source in sources[1:]:
        if source['startLine'] > frontier['endLine'] + 1:
            frontier['endLine'] = source['startLine'] - 1
        if source['endLine'] > frontier['endLine']:
            frontier = source
    frontier['endLine'] = len(lines)
    for source in sources:
        region = '\n'.join(lines[source['startLine'] - 1:source['endLine']])
        assert source['symbol'] in region, source
        source['sha256'] = digest(region)

path = DESIGN + 'reuse/functions/execution.json'
write(path, matrix)
if path not in updated: updated.append(path)

# 仅重钉已批准 HOST-WORKBENCH grant 的源码摘要；ID/类别/操作/范围保持逐字不变。
permissions_path = DESIGN + 'reuse/permissions.json'
permissions = json.loads(subprocess.check_output(['git', 'show', BASE + ':' + permissions_path],
                                                cwd=ROOT).decode('utf-8-sig'))
def repin_permission(node):
    if isinstance(node, dict):
        if node.get('path') in new_hash and 'sha256' in node:
            node['sha256'] = new_hash[node['path']]
        for value in node.values(): repin_permission(value)
    elif isinstance(node, list):
        for value in node: repin_permission(value)
repin_permission(permissions)
write(permissions_path, permissions)
if permissions_path not in updated: updated.append(permissions_path)

template = json.loads(read(DESIGN + 'tasks/foundation/CORE-T12.json'))
task = {
    'schemaVersion': 'ird-task/1', 'taskId': 'EX-T11', 'unit': 'execution', 'masterWp': 'WP-B',
    'title': 'execution 第一轮评审整改：身份、IPC、进程生命周期和资源治理',
    'status': 'ready', 'branch': 'codex/core-review-fixes', 'dependsOn': [],
    'requirements': ['TASK-03', 'ARC-04', 'CON-01', 'NFR-REL-02', 'NFR-PERF-02', 'NFR-PERF-04', 'NFR-MNT-01'],
    'designRefs': ['units/execution.md#6', 'units/execution.md#9', 'units/execution.md#10', 'reuse/policy.md#1'],
    'allowedFiles': products + [TASK, MANIFEST, DESIGN + 'units/execution.md'] + updated + [TRACE + '**'],
    'forbiddenFiles': ['RobWork/RobWork/src/**', 'RobWork/RobWorkSim/src/**', 'vcpkg/**'],
    'outputs': ['已获授权整改、正式回归及隔离探针；原始证据和逐文件/功能凭证；待独立验收的本地提交'],
    'verify': [
        'cmake --build build --config Release --target sdurws_ird_execution_test sdurws_ird_execution_contract_test',
        'cmake --build build/execution-smoke --config Release --target sdurws_ird_execution_test sdurws_ird_execution_contract_test',
        'pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1 -TaskFile ' + TASK],
    'interUnit': False,
    'acceptance': [
        '按本批 README 的保留/删除/适配和职责说明，沿 core/runtime/evidence/project 既有权威修复，不新增数值核心、owner 或上游修改',
        'EXR-01/03/04/05/07/08/09/10/11/13/14 的反例、真实进程、固定交错及资源边界按原始证据核对；脚本替身不构成业务算法 worker 证据',
        'C1 与 EXR-10 局部签名按所有者明确批准实施；EXR-02/06/12 及 C2/C5 留待批准，不把未完成能力标通过',
        '集成/独立冒烟与直接消费者、取消预算、门禁及复用检查如实分列 passed/skipped/failed；本实施不自签独验'],
    'note': '所有者指定 C: 工作树 codex/core-review-fixes，覆盖旧 D: 调度条款；起点 ' + BASE +
            '。所有者要求按照审核建议修改，另明确批准 C1 与 EXR-10 局部签名；不改既有任务状态、不推进下一单元。UI 仅同步 C1 生命周期注释并验证直接消费，无新跨单元端口/链接边。最小契约仅供逐文件凭证和范围机器检查。',
    'governanceAcceptance': template['governanceAcceptance'],
    'reuseGateAcceptance': template['reuseGateAcceptance'],
    'reuse': {'policy': 'H1-H6', 'manifest': MANIFEST}
}
write(TASK, task)
write(MANIFEST, {'schemaVersion': 'ird-reuse-change/1', 'taskId': 'EX-T11', 'taskFile': TASK,
    'files': [{'path': p, 'sha256': new_hash[p], 'mode': 'business', 'capabilities': [],
               'boundary': 'EX-T11：保留 core 身份、runtime 映射/judge、evidence 工厂/判定、project 事务；修正 execution 编排/同步/进程资源，不改上游/预算/业务装配；批准及未完成边界见本批记录',
               'verify': TRACE + 'README.md'} for p in products]})
print('repinned matrices:', updated)
