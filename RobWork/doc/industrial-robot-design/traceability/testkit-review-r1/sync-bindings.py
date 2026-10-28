"""TK-T13：从起始提交重钉本包功能区段；保留历史证据/批准，仅刷新实际输入。"""
import difflib
import hashlib
import json
import re
import subprocess
from pathlib import Path

root = Path.cwd()
design = 'RobWork/doc/industrial-robot-design/'
product = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/'
base = '6dc1141ae1e4d0db61baf1e7ab8d2ed414669c9f'
evidence = design + 'traceability/testkit-review-r1/'
taskfile = design + 'tasks/foundation/TK-T13.json'
manifest = design + 'reuse/changes/TK-T13.json'

def read(path):
    return (root / path).read_text(encoding='utf-8-sig')

def old(path):
    return subprocess.check_output(['git', 'show', base + ':' + path]).decode('utf-8-sig')

def digest(text):
    return hashlib.sha256((text.removeprefix('\ufeff').replace('\r\n', '\n').rstrip('\n') + '\n').encode()).hexdigest()

def write(path, obj):
    path = root / path
    original = path.read_bytes() if path.exists() else b''
    text = json.dumps(obj, ensure_ascii=False, indent=1 if '/functions/' in path.as_posix() else 2) + '\n'
    if b'\r\n' in original:
        text = text.replace('\n', '\r\n')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode())

changed = subprocess.check_output(['git', 'diff', '--name-only', base, '--', product]).decode().splitlines()
sources = [p for p in changed if p.endswith(('.cpp', '.hpp', '.ps1', '/CMakeLists.txt'))]
hashes = {p: digest(read(p)) for p in changed + [design + 'units/testkit.md']}
matrixpath = design + 'reuse/functions/testkit.json'
matrix = json.loads(old(matrixpath))

def remap(source):
    path = source['path']
    if path not in sources:
        return
    before, after = old(path).splitlines(), read(path).splitlines()
    ops = difflib.SequenceMatcher(a=before, b=after, autojunk=False).get_opcodes()
    def bound(n, end=False):
        for tag, a, b, c, z in ops:
            if tag == 'insert' and a == n:
                return c if end else z
            if a <= n < b:
                return c + n - a if tag == 'equal' else z if end and n > a else c
        return len(after)
    start, end = bound(source['startLine'] - 1), bound(source['endLine'], True)
    if end <= start:
        end = start + 1
    region = '\n'.join(after[start:end])
    if source['symbol'] not in region:
        raise RuntimeError('symbol lost: ' + str(source))
    source.update(startLine=start + 1, endLine=end, sha256=digest(region))

for function in matrix['functions']:
    for source in function['sources']:
        remap(source)
    if any(s['path'] in sources for s in function['sources']):
        # 旧审计留作历史；当前边界由本包真实修复/测试记录承接，不把旧PASS外推。
        previous = function.get('nonOverlapEvidence')
        if previous and previous not in function.setdefault('auditEvidence', []):
            function['auditEvidence'].append(previous)
        function['nonOverlapEvidence'] = dict(path=evidence+'README.md', sha256=digest(read(evidence+'README.md')))
        function['differences'] += '；TK-T13：第一轮确认边界修复，保留原算法/依赖/容差；当前格式授权、保留/删除及实际回归见整改记录，实施待独验。'

# 语义边界按真实函数/结构体/宏/测试定义；不以整文件占位行补覆盖。
definition = re.compile(r'^(?:TEST(?:_F|_P)?\(|(?:struct|class|inline)\b|#define IRD_|'
    r'(?:[\w:<>&*]+\s+)+[\w:~]+\(|(?:[\w:]+::)+(?:~?\w+|operator[^\(]+)\()')
extra = {
    'src/Fixture.cpp': ['bool ReproRecord::operator=='],
    'src/Dataset.cpp': ['[[noreturn]] void failField', 'bool DatasetManifest::operator=='],
}

def semantic_regions(path):
    lines = read(path).splitlines()
    relative = path.removeprefix(product)
    if path.endswith('.ps1'):
        scan = next(i + 1 for i, line in enumerate(lines) if line.startswith('$forbiddenPatterns ='))
        result = next(i + 1 for i, line in enumerate(lines) if line.startswith('# ---- 结论'))
        return [(1, scan - 1, 'param('), (scan, result - 1, '$forbiddenPatterns'),
                (result, len(lines), '$hits.Count')]
    definitions = []
    for i, line in enumerate(lines):
        parser_method = relative == 'src/JsonLite.cpp' and line.startswith('    ') and (
            definition.match(line[4:]) or line.startswith('    [[noreturn]] void fail('))
        if (definition.match(line) or parser_method or any(line.startswith(x) for x in extra.get(relative, []))):
            # 多行 inline 返回类型的下一行名称属于同一函数，不另切 region。
            if line.startswith('inline std::optional<'):
                symbol = lines[i + 1].strip().split('(')[0]
            else:
                symbol = line.strip()
            # class/struct 是值或测试helper的完整语义块；其内部方法随类覆盖。
            start = i
            j = i - 1
            while j >= 0 and not lines[j].strip():
                j -= 1
            if j >= 0 and lines[j].strip().endswith('*/'):
                while j >= 0 and '/*' not in lines[j]:
                    j -= 1
                start = max(j, 0)
            elif j >= 0 and lines[j].lstrip().startswith('//'):
                while j >= 0 and (not lines[j].strip() or lines[j].lstrip().startswith('//')):
                    j -= 1
                start = j + 1
            if start and lines[start - 1].startswith('template'):
                start -= 1
            definitions.append((start + 1, symbol))
    if not definitions:
        raise RuntimeError('needs explicit semantic boundaries: ' + path)
    # 开头 include/namespace 注释属于首个真实功能，文件尾属于最后功能。
    definitions[0] = (1, definitions[0][1])
    return [(a, definitions[i + 1][0] - 1 if i + 1 < len(definitions) else len(lines), symbol)
            for i, (a, symbol) in enumerate(definitions)]

added = []
for path in sources:
    if path.endswith('/CMakeLists.txt'):
        continue  # 已有按目标注册分段，重钉后逐行检查。
    covered = set()
    for function in matrix['functions']:
        for source in function['sources']:
            if source['path'] == path:
                covered.update(range(source['startLine'], source['endLine'] + 1))
    lines = read(path).splitlines()
    for start, end, symbol in semantic_regions(path):
        if start > end:
            raise RuntimeError('overlapping semantic boundary: ' + path + ':' + str(start))
        new_helper = (path.endswith('/src/Dataset.cpp') and ' requireUnsigned(' in symbol) or (
            path.endswith('/src/Report.cpp') and ' comparisonNumber(' in symbol)
        if not new_helper and not any(lines[n-1].strip() and n not in covered for n in range(start, end + 1)):
            continue
        region = '\n'.join(lines[start-1:end])
        if symbol not in region:
            raise RuntimeError('missing symbol in new region: ' + symbol)
        target = 'sdurws_ird_testkit_test' if '/test/' in path else (
            'sdurws_ird_testdata_lint' if '/tools/testdata/' in path else
            'sdurws_ird_testkit_lsnchild' if '/tools/lsnchild/' in path else 'sdurws_ird_testkit')
        function = dict(functionId='testkit.TK_T13.' + path.removeprefix(product).replace('/', '.').replace('.', '_') + '.L' + str(start),
            ownerUnit='testkit', requirements=['NFR-COR-01', 'NFR-COR-02', 'NFR-COR-03'],
            sources=[dict(path=path, symbol=symbol, startLine=start, endLine=end, sha256=digest(region))],
            targets=[target], usageRole='owner', permissionRef='ARCH:3.6.1', capabilityId='NONE',
            decision='business-specific', status='migrated',
            search=dict(command='见 traceability/testkit-review-r1/reuse-search.log 与 reuse-string-search.log',
                conclusion='复用 core 唯一比较/单位/摘要、GoogleTest、Boost既有匹配及标准库原语；上游 XML/宽松字符串转换不等价于受限 JSON。非新增解析/匹配/业务算法平台。'),
            differences='TK-T13 第一轮评审整改：本区段为测试数据、断言、报告或受控进程支撑；保留/删除/适配、格式授权及消费者边界见实施记录。migrated只记录本包实现，任务ready待独验，不关闭旧迁移债务。',
            inputs=[dict(path=path, sha256=digest(read(path))), dict(path=product+'CMakeLists.txt', sha256=hashes[product+'CMakeLists.txt'])],
            auditEvidence=[dict(path=evidence+'README.md', sha256=digest(read(evidence+'README.md')))],
            nonOverlapEvidence=dict(path=evidence+'README.md', sha256=digest(read(evidence+'README.md'))))
        matrix['functions'].append(function)
        covered.update(range(start, end + 1))
        added.append(dict(path=path, start=start, end=end, symbol=symbol))
    missing = [n for n, line in enumerate(lines, 1) if line.strip() and n not in covered]
    if missing:
        raise RuntimeError('uncovered ' + path + ': ' + str(missing[:10]))
write(matrixpath, matrix)

# 只重钉实际变动文件的输入摘要；不改历史原始报告或生产正向证据。
repins = {}
for path in (root / (design + 'reuse/functions')).glob('*.json'):
    value = json.loads(path.read_text(encoding='utf-8-sig'))
    count = [0]
    def visit(node):
        if isinstance(node, dict):
            if node.get('path') in hashes and 'sha256' in node and 'startLine' not in node:
                new = hashes[node['path']]
                if node['sha256'] != new:
                    node['sha256'] = new
                    count[0] += 1
            for child in node.values():
                visit(child)
        elif isinstance(node, list):
            for child in node:
                visit(child)
    visit(value)
    if count[0] or path.name == 'foundation-governance.json':
        write(path.relative_to(root), value)
        repins[path.name] = count[0]

template = json.loads(read(design+'tasks/foundation/CORE-T12.json'))
allowed = changed + [taskfile, manifest,
    design+'units/testkit.md', design+'traceability/findings.json', evidence+'**']
allowed += subprocess.check_output(['git', 'diff', '--name-only', base, '--',
    design+'reuse/functions/']).decode().splitlines()
task = dict(schemaVersion='ird-task/1', taskId='TK-T13', unit='testkit', masterWp='WP-B',
    title='testkit第一轮评审整改：断言、数据完整性、精确种子与报告边界', status='ready',
    branch='codex/core-review-fixes', dependsOn=[],
    requirements=['NFR-COR-01', 'NFR-COR-02', 'NFR-COR-03', 'NFR-SEC-05'],
    designRefs=['units/testkit.md#4', 'units/testkit.md#5', 'units/testkit.md#6', 'units/testkit.md#7', 'reuse/policy.md#1'],
    allowedFiles=sorted(set(allowed)), forbiddenFiles=['RobWork/RobWork/src/**', 'RobWork/RobWorkSim/src/**', 'vcpkg/**'],
    outputs=['修复实现、正式回归、原始日志/XML/原生报告及逐文件/逐功能凭证；本地提交待所有者另开会话独验'],
    verify=['cmake --build build --config Release --target sdurws_ird_testkit_test',
        'cmake --build build/core-smoke --config Release --target sdurws_ird_testkit_test',
        'pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1 -TaskFile '+taskfile],
    interUnit=False, acceptance=[
        'R01/R02假通过反例先红后绿，身份隔离、单次求值、首错位保持core C4及Boost匹配唯一入口；实际身份消费者定向回归',
        'R04/R05/R09及F015～18的schema/量纲/整数字段/generate完整性/lint字典与引用得到真实正负例；正式expected及容差未改',
        '经所有者批准R03/R06：完整uint64整数、非有限字符串/缺失null；标准JSON解析器核对真实listener子进程JSON/XML/退出码；locale保留次正规与负零',
        'R08/R10/R12按环境覆盖、只读分发扫描和既有P95定义修复；相关集成/冒烟/消费者、公共头及门禁据真实结果留痕；不自签独验'],
    note='所有者明确指定本分支/工作树并要求按第一轮评审修改，覆盖旧只读评审和redesign-main现场要求。起始SHA='+base+'。R03/R06格式问题获明确答复“采用上述方案”；不扩充依赖/容差/批准状态，不修改core及其他单元实现。新增最小契约为既有凭证机器校验所需，状态ready不代表独验通过；旧F015～18保持open待独验。',
    governanceAcceptance=template['governanceAcceptance'], reuseGateAcceptance=template['reuseGateAcceptance'],
    reuse=dict(policy='H1-H6', manifest=manifest))
write(taskfile, task)
write(manifest, dict(schemaVersion='ird-reuse-change/1', taskId='TK-T13', taskFile=taskfile,
    files=[dict(path=p, sha256=hashes[p], mode='business', capabilities=[],
        boundary='TK-T13：修复现有测试基础契约，复用core/GoogleTest/Boost与系统原语；不新增算法平台、依赖或权限。逐功能边界、格式批准和回归见实施记录。',
        verify=evidence+'README.md') for p in sources]))
print(json.dumps(dict(changedSources=len(sources), addedRegions=added, repins=repins, functions=len(matrix['functions'])), ensure_ascii=False, indent=2))
