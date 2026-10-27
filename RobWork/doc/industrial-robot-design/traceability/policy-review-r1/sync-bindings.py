"""POL-T13：仅同步本批来源与摘要，保留原审计状态；从仓库根运行。

本脚本是一次性证据维护工具，不是准入门禁，不授予模型权限或关闭债务。
"""
import difflib
import hashlib
import json
import re
import subprocess
from functools import cache
from pathlib import Path

ROOT = Path.cwd()
BASE = 'fd198f54c0b66bc61dd64b29a04e2b1675f42dbc'
START = '59af1f9b449ee23fb78dfe547b3a7339df9d325d'
D = 'RobWork/doc/industrial-robot-design/'
P = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/'
E = D + 'traceability/policy-review-r1/'
TF = D + 'tasks/foundation/POL-T13.json'
MF = D + 'reuse/changes/POL-T13.json'

def read(path):
    return (ROOT / path).read_text(encoding='utf-8-sig')

@cache
def baseline(path):
    return subprocess.check_output(['git', 'show', BASE + ':' + path]).decode('utf-8-sig')

def digest(text):
    return hashlib.sha256((text.removeprefix('\ufeff').replace('\r\n', '\n').rstrip('\n') + '\n').encode()).hexdigest()

def write(path, obj):
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    # 保持既有矩阵的缩进风格，避免无关整文件格式变化。
    old = target.read_text(encoding='utf-8-sig') if target.exists() else ''
    indent = 1 if old.startswith('{\n "schemaVersion"') else 2
    target.write_bytes((json.dumps(obj, ensure_ascii=False, indent=indent) + '\n').replace('\n', '\r\n').encode())

changed = sorted(x for x in subprocess.check_output(['git', 'diff', '--name-only', BASE]).decode().splitlines() if x.startswith(P))
new_test = P + 'policy/test/PolicyDiagnosticsRegistryContractTest.cpp'
changed = sorted(set(changed) | {new_test})
hashes = {p: digest(read(p)) for p in changed if (ROOT / p).exists()}
hashes[E + 'IMPLEMENTATION.md'] = digest(read(E + 'IMPLEMENTATION.md'))
for path in [D+'units/'+u+'.md' for u in ('policy','runtime','kinematics','ui','diagnostics')] + [D+'development-task-breakdown.md']:
    hashes[path] = digest(read(path))

def remap(source):
    path = source['path']
    if path not in hashes or path not in changed or path == new_test:
        return
    before, after = baseline(path).splitlines(), read(path).splitlines()
    ops = difflib.SequenceMatcher(a=before, b=after, autojunk=False).get_opcodes()
    def bound(x, end=False):
        for tag, a, b, c, z in ops:
            if tag == 'insert' and a == x:
                return c if end else z
            if a <= x < b:
                return c + x - a if tag == 'equal' else z if end and x > a else c
        return len(after)
    start, end = bound(source['startLine'] - 1), bound(source['endLine'], True)
    end = max(end, start + 1)
    if path.endswith('diagnostics/src/DiagCodes.cpp'):
        source['symbol'] = source['symbol'].replace('array<BuiltinSpec, 88>', 'array<BuiltinSpec, 94>')
    if source['symbol'].lstrip().startswith(('/**', '//')):
        source['symbol'] = next(l.strip() for l in after[start:end] if l.strip())
    if source['symbol'] not in '\n'.join(after[start:end]):
        raise RuntimeError('source symbol lost: ' + str(source))
    source.update(startLine=start + 1, endLine=end, sha256=digest('\n'.join(after[start:end])))

matrices = {}
for path in (ROOT / (D + 'reuse/functions')).glob('*.json'):
    data = json.loads(baseline(path.relative_to(ROOT).as_posix()))
    for row in data['functions']:
        for source in row['sources']:
            if source['path'] in changed and not (ROOT / source['path']).exists():
                raise RuntimeError('removed source still bound: ' + row['functionId'])
            remap(source)
    matrices[data['unit']] = data

def region(unit, path, start, end, symbol, label, target, description):
    """登记新增的明确业务/测试区段，不用新行覆盖原 pending。"""
    lines = read(path).splitlines()
    cmake = P + unit + '/CMakeLists.txt'
    matrices[unit]['functions'].append({
        'functionId': unit + '.POL_T13.' + label, 'ownerUnit': unit,
        'requirements': ['ARC-05', 'NFR-COR-03'],
        'sources': [{'path': path, 'symbol': symbol, 'startLine': start, 'endLine': end,
                     'sha256': digest('\n'.join(lines[start-1:end]))}],
        'targets': [target], 'usageRole': 'owner', 'permissionRef': 'ARCH:3.6.1',
        'capabilityId': 'NONE', 'decision': 'business-specific', 'status': 'migrated',
        'search': {'command': '见traceability/policy-review-r1/IMPLEMENTATION.md定向检索',
                   'conclusion': description},
        'differences': 'POL-T13：' + description + '；不关闭原pending，不替代独立验收。',
        'inputs': [{'path': path, 'sha256': hashes[path]},
                   {'path': cmake, 'sha256': digest(read(cmake))}],
        'auditEvidence': [{'path': E + 'IMPLEMENTATION.md', 'sha256': hashes[E + 'IMPLEMENTATION.md']}],
        'nonOverlapEvidence': {'path': E + 'IMPLEMENTATION.md', 'sha256': hashes[E + 'IMPLEMENTATION.md']}})

# 新增词表检查不是几何算法；几何消费仍在原 COLLISION pending 行。
path = P + 'policy/include/sdurws/ird/policy/PolicySet.hpp'
lines = read(path).splitlines()
start = next(i+1 for i,l in enumerate(lines) if 'inline bool isKnownCollisionDomain' in l)
end = next(i+1 for i in range(start, len(lines)) if lines[i] == '}')
region('policy', path, start, end, 'isKnownCollisionDomain', 'KnownDomain', 'sdurws_ird_policy',
       '共享策略域词表检查；复用同一枚举，解析与字段工厂均拒绝表外值，无上游等价业务词表')

path = P + 'ui/plugin/KinEvaluationChannel.cpp'
region('ui', path, 659, 671, 'makePolicyCollisionSession', 'HostCollisionAssembly', 'sdurws_ird_ui_plugin',
       '宿主仅连接已发布快照、完整策略和既有kinematics/policy端口；无模型构造、规则投影或几何计算')

# 回归按单个 TEST 登记，夹具区段单独登记；真实几何直接使用上游 Box/Mesh。
for unit, rel, marker, target in [
    ('policy','policy/test/CollisionEvaluationTest.cpp','RealBackendSessionModelsAreIsolated', 'sdurws_ird_policy_test'),
    ('policy','policy/test/JointLimitsTest.cpp','Nonfinite', 'sdurws_ird_policy_test'),
    ('ui','ui/test/KinEvaluationChannelTest.cpp','HostCollisionUsesCompletePolicyRules', 'sdurws_ird_ui_test')]:
    path = P + rel
    lines = read(path).splitlines()
    # 用本批新增行确定边界，避免把已有测试重标为本轮语义审计。
    before = baseline(path).splitlines()
    added = [c for tag,a,b,c,z in difflib.SequenceMatcher(a=before,b=lines,autojunk=False).get_opcodes()
             if tag in ('insert','replace') and z-c > 10]
    if not added:
        raise RuntimeError('new test region missing: ' + path)
    start = min(added)
    starts = [i for i in range(start, len(lines)) if re.match(r'^TEST(?:_F)?\s*\(', lines[i])]
    boundaries = sorted(set([start] + starts + [len(lines)]))
    for a,b in zip(boundaries, boundaries[1:]):
        text = '\n'.join(lines[a:b])
        if not text.strip():
            continue
        symbol = next((l.strip() for l in lines[a:b] if re.match(r'^TEST(?:_F)?\s*\(', l)), None)
        if not symbol:
            symbol = next((l.strip() for l in lines[a:b] if l.strip() and not l.strip().startswith(('//','/*','*'))),
                          next(l.strip() for l in lines[a:b] if l.strip()))
        region(unit, path, a+1, b, symbol, 'Regression.L'+str(a+1), target,
               '评审反例回归及任务私有测试夹具，断言策略/覆盖/诊断语义；上游直接提供几何，测试不实现碰撞核心')

path = new_test
lines = read(path).splitlines()
region('policy', path, 1, len(lines), 'EveryEmittedCodeIsAccepted_POL_T13', 'DiagnosticRegistryContract',
       'sdurws_ird_policy_contract_test', '逐生产诊断码经正式registry/factory创建；只验证对端码表闭包，不复制注册表')

for rel, marker, symbol, label, target, description in [
    ('kinematics/src/Collision.cpp', 'IkCollisionVerdict PolicyCollisionSessionAdapter::evaluate',
     'PolicyCollisionSessionAdapter::evaluate', 'CoverageVerdict', 'sdurws_ird_kinematics',
     '将policy正式结果映射为运动学淘汰/缺证事实，覆盖不足不得安全通过；无几何计算'),
    ('kinematics/test/CollisionPortTest.cpp', 'TEST (CollisionPort, MissingGeometryCannotBecomeEvaluated',
     'MissingGeometryCannotBecomeEvaluated_POL_T13', 'MissingGeometryRegression', 'sdurws_ird_kinematics_test',
     '用端口替身验证不完整覆盖到EvidenceMissing的契约映射，真实几何证据另见policy回归'),
    ('kinematics/test/AnalysisConfigTest.cpp', 'TEST(KinAnalysisConfig, ConfigDigestEntersSliceIdNotBaseline',
     'ConfigDigestEntersSliceIdNotBaseline_WP15T10_ACC2', 'SnapshotFixtureClosure', 'sdurws_ird_kinematics_test',
     '修正测试夹具来源闭包，两个配置分别绑定对应冻结快照；不改变evidence产品规则')]:
    path = P + rel
    lines = read(path).splitlines()
    start = next(i+1 for i,l in enumerate(lines) if l.startswith(marker))
    end = next(i+1 for i in range(start,len(lines)) if lines[i] == '}')
    region('kinematics', path, start, end, symbol, label, target, description)

# 对确已变化的说明纠正当前事实，历史证据原样保留。
for row in matrices['policy']['functions']:
    if row['functionId'] == 'policy.RobWorkCollisionEvaluator.createSession':
        row['differences'] += '；POL-T13：evaluator仅持描述符，各createSession独立构造上游后端和会话锁，不再共享几何缓存。'
    if row['functionId'] == 'policy.CollisionQuery.CollisionEvaluationSession.buildEvaluationHalf':
        row['differences'] += '；POL-T13：改为会话私有后端、逐几何addModel真实返回值决定完整覆盖；同帧多Object与距离能力边界仍待独验，不将摘要刷新视为关闭。'
for row in matrices['foundation-governance']['functions']:
    if row['functionId'].endswith(('L5_IRD_R5_EXEMPT_TARGETS','L9_IRD_TEST_TARGET_EDGES')):
        row['differences'] += '；POL-T13：撤销runtime proximity豁免，仅登记policy_contract_test→diagnostics精确测试边，产品依赖规则不变。'
for row in matrices['ui']['functions']:
    if row['functionId'] == 'ui.SnapshotViewAdapter':
        row['differences'] += '；POL-T13新增既有场景源端口转发：共享只读WorkCell别名保活、编译/名称身份、首末帧按名称表scope选择，不拼名、不构造模型。此原整文件行不替代新增入口的单独语义说明。'

repins, changed_matrices = {}, []
permission_path = D + 'reuse/permissions.json'
bindings = [(D+'reuse/functions/'+u+'.json',data,u) for u,data in matrices.items()]
bindings.append((permission_path,json.loads(baseline(permission_path)),'permissions-existing-bindings'))
for path, data, unit in bindings:
    count = [0]
    def visit(node):
        if isinstance(node, dict):
            if node.get('path') in hashes and 'sha256' in node and 'startLine' not in node:
                value = hashes[node['path']]
                if value != node['sha256']:
                    node['sha256'] = value
                    count[0] += 1
            for value in node.values(): visit(value)
        elif isinstance(node,list):
            for value in node: visit(value)
    visit(data)
    if data != json.loads(baseline(path)):
        write(path, data)
        changed_matrices.append(path)
        repins[unit] = count[0]

template = json.loads(read(D + 'tasks/foundation/CORE-T12.json'))
write(TF, {
    'schemaVersion':'ird-task/1','taskId':'POL-T13','unit':'policy','masterWp':'WP-G',
    'title':'policy专项评审整改：会话隔离、宿主唯一入口、数值与诊断闭包',
    'status':'ready','branch':'codex/core-review-fixes','dependsOn':[],
    'requirements':['ARC-05','CON-01','KIN-05','NFR-COR-03','NFR-COR-05'],
    'designRefs':['units/policy.md#6','units/policy.md#7','units/runtime.md#4','units/kinematics.md#4','units/diagnostics.md#4','reuse/policy.md#1'],
    'allowedFiles':changed + [D+'units/'+u+'.md' for u in ('policy','runtime','kinematics','ui','diagnostics')]
        + [D+'development-task-breakdown.md',TF,MF]+changed_matrices+[E+'**'],
    'forbiddenFiles':['RobWork/RobWork/src/**','RobWork/RobWorkSim/src/**','vcpkg/**'],
    'outputs':['12项确定缺陷修复、正式回归与原始证据；逐文件及功能绑定；本地可回滚提交，待所有者独立验收'],
    'verify':['cmake --build build --config Release --target ird_gates',
              'pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1 -TaskFile '+TF],
    'interUnit':False,
    'acceptance':['真实独立双盒场景、有限性/生命周期/枚举/几何接收/最小值短路/过滤留痕/限位边界及正式诊断闭包回归',
                  '同步/覆盖/单点/批量统一policy入口，删除runtime规则与计算副本；模型owner不变，不新写核心、不放宽门禁',
                  '集成、独立冒烟与直接消费者回归，原始XML/JSON/日志齐全；性能微基准仅提供测量，不冒充既定预算验收',
                  '保留pending/proposal及真实门禁阻断；不自签独验，Group/完整角色/真实距离/bool严格编码待原裁决'],
    'note':'用户要求按照专项评审建议修改；指定C盘工作树覆盖旧主线调度。实施源码起点'+START+
        '；期间他会话提交b8602fb00/fd198f54c的PRJ-T19验收登记，无产品源码变化；本包提交范围基点'+BASE+
        '，不把他会话登记纳入本包。最小契约仅为H6与范围核验，ready不代表独验通过。',
    'governanceAcceptance':template['governanceAcceptance'],'reuseGateAcceptance':template['reuseGateAcceptance'],
    'reuse':{'policy':'H1-H6','manifest':MF}})
boundaries = {
    'cmake/ird_gates_whitelist.cmake': '撤销runtime proximity豁免，仅增加正式诊断对端契约的精确测试边；引擎不变',
    'diagnostics/include/sdurws/ird/diagnostics/DiagCodes.hpp': '同步正式内置码表94项声明，不另建诊断通道',
    'diagnostics/src/DiagCodes.cpp': '正式注册policy生产遗漏六码，复用既有分类/严重度/工厂语义',
    'diagnostics/test/StableCodeRegistryTest.cpp': '同步94项全码集合与policy29项计数断言',
    'kinematics/src/Collision.cpp': '仅在完整无碰撞覆盖时返回Evaluated，缺几何保持EvidenceMissing；计算仍归policy',
    'kinematics/test/AnalysisConfigTest.cpp': '修正测试切片的来源快照闭包，不改变evidence产品规则',
    'kinematics/test/CollisionPortTest.cpp': '回归真实缺几何场景及端口覆盖判定，不将替身冒充几何计算证明',
    'policy/CMakeLists.txt': '注册正式诊断契约源与仅测试侧链接，不给产品policy反链diagnostics',
    'policy/include/sdurws/ird/policy/CollisionEvaluator.hpp': '删除评估器级后端与锁，说明会话私有生命周期，公开判定接口不变',
    'policy/include/sdurws/ird/policy/Diagnostics.hpp': '工程范围沿正式既有码拼写，不新增第二码表',
    'policy/include/sdurws/ird/policy/JointLimits.hpp': '同步正式诊断码说明，保持限位业务契约',
    'policy/include/sdurws/ird/policy/PolicySet.hpp': '解析与直接工厂共用碰撞域词表校验，不实现上游计算',
    'policy/src/CollisionQuery.cpp': '直接消费上游逐几何addModel/inCollision/distance；补有限性、存活、真实覆盖与摘要留痕，无新几何内核',
    'policy/src/JointLimits.cpp': '修正有限端点/算术可表示性与比值计算，结构错误不可降格为可确认工程超限',
    'policy/src/PolicyParsing.cpp': '调用共享域词表拒绝表外枚举，继续收集既有解析错误',
    'policy/src/RobWorkCollisionEvaluator.cpp': '各会话直接构造上游ProximityStrategyRW，避免跨WorkCell Frame ID几何缓存串用',
    'policy/test/CollisionEvaluationTest.cpp': '真实上游盒/mesh与协议替身回归分开；解析oracle不复用生产变换/规则展开',
    'policy/test/JointLimitsTest.cpp': '非有限、宽度溢出与次正规端点/中点回归，不放宽容差',
    'policy/test/PolicyDiagnosticsRegistryContractTest.cpp': '逐生产码通过正式registry/factory，无替身注册表',
    'runtime/CMakeLists.txt': '移除重复碰撞源、测试与直接proximity/PQP链接；模型/名称owner仍归runtime',
    'runtime/include/sdurws/ird/runtime/CollisionSession.hpp': '删除被宿主唯一policy入口替代的重复碰撞公共设施声明',
    'runtime/src/CollisionSession.cpp': '删除Object-only规则和直接碰撞副本，不保留旧fallback',
    'runtime/test/CollisionSessionTest.cpp': '随重复设施删除专属测试，由policy真实回归及宿主接线回归承接',
    'ui/plugin/KinEvaluationChannel.cpp': '四入口共用既有policy/kinematics会话；只读快照别名和精确名称表转发，无模型构造',
    'ui/plugin/KinEvaluationChannel.hpp': '声明插件私有装配缝，保持域/role/mandatory完整策略进入policy',
    'ui/test/KinEvaluationChannelTest.cpp': '真实发布快照验证宿主域/邻接/必检及缺证；不冒充完整几何或GUI人工验收',
}
write(MF,{'schemaVersion':'ird-reuse-change/1','taskId':'POL-T13','taskFile':TF,
    'files':[{'path':p,'sha256':hashes.get(p,'deleted'),
              'mode':'remove' if p not in hashes else 'governance' if p.endswith(('CMakeLists.txt','.cmake'))
                  else 'adapter' if p.endswith(('policy/src/CollisionQuery.cpp','policy/src/RobWorkCollisionEvaluator.cpp')) else 'business',
              'capabilities':['COLLISION'] if p.endswith(('policy/src/CollisionQuery.cpp','policy/src/RobWorkCollisionEvaluator.cpp')) else [],
              'boundary':'POL-T13：'+boundaries[p[len(P):]]+'；不授新核心、不关闭旧pending，具体证据见实施记录。',
              'verify':E+'IMPLEMENTATION.md'} for p in changed]})
unresolved = sorted({r['functionId'] for data in matrices.values() for r in data['functions']
    if r['status'] not in ('migrated','closed') and any(s['path'] in changed for s in r['sources'])})
gaps = {}
for path in changed:
    if path not in hashes: continue
    covered = set()
    for data in matrices.values():
        for row in data['functions']:
            for source in row['sources']:
                if source['path'] == path: covered.update(range(source['startLine'],source['endLine']+1))
    missing = [i+1 for i,l in enumerate(read(path).splitlines()) if l.strip() and i+1 not in covered]
    if missing: gaps[path] = missing
write(E+'binding-audit.json',{'implementationStart':START,'scopeBase':BASE,'preservedUnresolved':unresolved,
    'uncoveredNonemptyLines':gaps,'repins':repins,
    'note':'只报绑定结构与未决项，不授权限、不以访问器/整文件覆盖冒充几何语义关闭；正式门禁结果另见验证记录。'})
print('product files:',len(changed),'matrices:',len(changed_matrices),'preserved unresolved:',len(unresolved),'files with coverage gaps:',len(gaps))
