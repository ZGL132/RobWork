// REUSE-T42 矩阵生成脚本（一次性治理数据编制工具，非产品代码）。
// 依据 audit-contract §4 行格式与 reuse-contract.ps1 机器校验口径编制
// functions/requirements.json 的全量行：TemplateArray.cpp 全行铺面 +
// 其余 pending 行收口。区域摘要口径与 validate-reuse.ps1 Digest 一致
// （BOM 剥离、CRLF→LF、尾部换行规范）。
'use strict';
const fs = require('fs');
const crypto = require('crypto');

const ROOT = 'D:/10_Source_Repos/21_robot/RobWork';
const prod = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/';
const E = 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T42';

function digestPath(p) {
  let t = fs.readFileSync(ROOT + '/' + p, 'utf8');
  t = t.replace(/^\uFEFF/, '').replace(/\r\n/g, '\n');
  t = t.replace(/\n*$/, '\n');
  return crypto.createHash('sha256').update(Buffer.from(t, 'utf8')).digest('hex');
}
function digestRegion(lines, start, end) { // 1-based inclusive，与校验器一致
  const region = lines.slice(start - 1, end).join('\n');
  const norm = region.replace(/^/, '');
  const trimmed = norm.replace(/\n+$/, '') + '\n';
  return crypto.createHash('sha256').update(Buffer.from(trimmed, 'utf8')).digest('hex');
}

const taPath = prod + 'requirements/src/TemplateArray.cpp';
const taText = fs.readFileSync(ROOT + '/' + taPath, 'utf8').replace(/^\uFEFF/, '').replace(/\r\n/g, '\n');
if (taText.includes('\r')) throw new Error('unexpected CRLF in TemplateArray.cpp');
const taLines = taText.split('\n');
if (taLines[taLines.length - 1] === '') taLines.pop(); // 末尾换行产生的空串不计
const cmakeDigest = 'c2ac4f7e5d4b98db84f51fa1ec7643989c5f0bf3b22afef423870752a5e5612f';
const cmakeInput = { path: prod + 'requirements/CMakeLists.txt', sha256: cmakeDigest };
const nonOverlap = { path: E + '/nonoverlap-requirements.json', sha256: digestPath(E + '/nonoverlap-requirements.json') };

// —— TemplateArray.cpp 区域表（start 由前一行 end+1 推出；首行=1）——
// [endLine, symbol, functionId, cap, decision, role, reqRefs, searchKey, differences]
const R = [
  [51, 'TemplateArray.hpp', 'requirements.TemplateArray.fileHead', 'NONE', 'business-specific', 'owner', ['NFR-COR-01'], 'S1S2', '文件头/命名空间/确定性约定（IEEE754、%.17g 无损往返、strtod 全消费、冲突升序去重）——产品数据身份契约，上游无对应物（S1/S2 零命中）。'],
  [60, 'formatNumber', 'requirements.formatNumber', 'NONE', 'business-specific', 'owner', ['NFR-COR-01'], 'S1S2', '数学数值文本化 %.17g（IEEE754 double 无损往返格式——provenance 快照协议面），上游 Vector3D 无产品文本格式。'],
  [75, 'parseNumber', 'requirements.parseNumber', 'NONE', 'business-specific', 'owner', ['NFR-COR-01'], 'S1S2', 'strtod 全消费解析＋确定性文本约定，属需求参数文本协议；上游 StringUtil 无全消费数值协议同义物。'],
  [81, 'formatVec3', 'requirements.formatVec3', 'NONE', 'business-specific', 'owner', ['NFR-COR-01'], 'S1S2', '%.17g 三分量文本化（provenance 快照协议）；上游 Vector3D 无产品文本格式。'],
  [105, 'parseVec3', 'requirements.parseVec3', 'NONE', 'business-specific', 'owner', ['NFR-COR-01'], 'S1S2', '三分量全消费解析（协议面），非数学核心。'],
  [118, 'paramValue', 'requirements.paramValue', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', 'GenerationProvenance 参数读取（产品 provenance 模型字段），上游无 provenance 概念（S2 零命中）。'],
  [135, 'sourceIdsOf', 'requirements.sourceIdsOf', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', '产品来源 ObjectId 列表读取；上游无 ObjectId/provenance 模型。'],
  [148, 'refFrameToText', 'requirements.refFrameToText', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '需求参考系文本编码（产品引用协议）。'],
  [185, 'refFrameFromText', 'requirements.refFrameFromText', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '需求参考系文本解码（产品引用协议）。'],
  [199, 'makeDiag', 'requirements.makeDiag', 'NONE', 'business-specific', 'owner', ['NFR-COR-02'], 'S1', '产品诊断记录装配（diagnostics 契约承载，本单元恒不写诊断）。'],
  [210, 'errorBatch', 'requirements.errorBatch', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '产品 EditBatch 错误面（I-REQ 契约），非上游能力。'],
  [221, 'finiteVec3', 'requirements.finiteVec3', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '产品参数有限性拒绝面（NaN/Inf 拒绝归业务入口原子拒绝——非 REUSE-T37 通用数值工具面，业务拒绝分支承载）。'],
  [232, 'reflectPosition', 'requirements.reflectPosition', 'MATH-ROT', 'business-specific', 'adapter', ['REQ-11', 'AT-24'], 'S4', '镜像反射 p′=p−2(n·p)n 是 §7.2 业务反射规则（等距变换、det=−1）；上游无反射原语（S4 零命中），Vector3D 值运算仅作承载，不能为复用把反射当 proper rotation。'],
  [251, 'rotationFromRpy', 'requirements.rotationFromRpy', 'MATH-ROT', 'direct', 'adapter', ['REQ-11', 'AT-24'], 'ROT', '已换上游：rw::math::RPY<double>(yaw,pitch,roll).toRotation3D()（字段换序边界单点承载，modeling/rpymath::rpyToRotation 同型、REUSE-T11 已闭）。逐元素解析式与原手写 Rz·Ry·Rx 展开恒等且乘加序一致——REUSE-T42 探针 12 用例（含 ±π/2 与近万向锁）IEEE754 位模式逐位相等；黄金镜像/数据集摘要零扰动（normal 221 项全过、oracle 2 项黄金过、断开 2 项真实红）。'],
  [261, 'reflectionMatrix', 'requirements.reflectionMatrix', 'MATH-ROT', 'business-specific', 'adapter', ['REQ-11', 'AT-24'], 'S4', 'M=I−2nnᵀ 为 det=−1 反射矩阵——Rotation3D 类型系统不能表达（非 proper rotation），上游无通用 3×3 反射构造（S4 零命中）；MATH-ROT 登记面为旋转值类型，不覆盖。'],
  [294, 'matMul', 'requirements.matMul', 'MATH-ROT', 'business-specific', 'adapter', ['REQ-11', 'AT-24'], 'S4', '固定 3 阶乘法承载 M·R·M 反射共轭（M 非旋转，Rotation3D operator* 类型前提不成立）；乘加序固定为确定性契约（黄金元素规则 σiσj 依据），上游无 det=−1 共轭能力。'],
  [344, 'reflectRpy', 'requirements.reflectRpy', 'MATH-ROT', 'business-specific', 'adapter', ['REQ-11', 'AT-24'], 'ROTPROBE', 'MRM 共轭＋Z-Y-X 反解为 §7.2 业务镜像规则＋F-582 修复契约（roll′=0、yaw 按符号分支）；上游 RPY(R,eps) 差异实证：①万向锁带宽 eps=1e-5 远宽于产品 1e-12（|cp|∈[1.41e-6,1e-5] 区间上游吸附退化表示、产品保留一般表示——REQ-10 的 1e-8rad 回归禁止吸附）；②退化轴约定不同（上游置绕Z=0、产品置 roll=0）——同矩阵表示不同，条目字段/摘要随表示变化；③上游退化提取元素 atan2(r12,r22) 与产品 atan2(R[1][2],R[1][1]) 不同。probe-r6（REUSE-T01-api-r6）实测确认。反解保留产品唯一职责。'],
  [355, 'methodTagOfGenerator', 'requirements.methodTagOfGenerator', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', '产品 provenance methodTag 语法（[a-z0-9./_-]）承载，上游无。'],
  [370, 'generatedSource', 'requirements.generatedSource', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', '产品 D-REQ-4 来源标记规则（UserProvided＋methodTag），上游无。'],
  [386, 'disambiguateName', 'requirements.disambiguateName', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '批次内确定性试缀消歧（产品 I-REQ-3 边界），上游无同名语义（S1 零命中）。'],
  [397, 'appendCommonParams', 'requirements.appendCommonParams', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', '产品 provenance 参数快照协议。'],
  [409, 'contentEqualIgnoringIdentity', 'requirements.contentEqualIgnoringIdentity', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '产品 TaskPoint 身份无关等值（批次冲突清单语义）。'],
  [422, 'appendRefFrameParam', 'requirements.appendRefFrameParam', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', '产品 provenance 参考系参数协议。'],
  [431, 'operator==', 'requirements.EditBatchEquality', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '产品 EditBatch 值等值（契约面）。'],
  [442, 'operator==', 'requirements.RegenerateOutcomeEquality', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '产品 RegenerateOutcome 值等值（契约面）。'],
  [488, 'defaultTemplateParams', 'requirements.defaultTemplateParams', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '三类模板默认参数（§7.2 产品业务默认值契约）。'],
  [508, 'defaultProcessTag', 'requirements.defaultProcessTag', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '产品工艺标记默认值（业务契约）。'],
  [525, 'lowerHyphenToken', 'requirements.lowerHyphenToken', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', '产品标识文本化约定（methodTag 连字符形态）。'],
  [531, 'templateGeneratorId', 'requirements.templateGeneratorId', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', '产品生成器标识语法（冒号形态）。'],
  [542, 'arrayGeneratorId', 'requirements.arrayGeneratorId', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', '产品阵列生成器标识语法。'],
  [654, 'applyTemplate', 'requirements.applyTemplate', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '模板装配业务（三类生成器公共面——I-REQ-10 单点执行）；上游无需求模板概念（S1 零命中）。'],
  [731, 'mirrorPoint', 'requirements.mirrorPoint', 'NONE', 'business-specific', 'owner', ['REQ-11', 'AT-24'], 'S4', '镜像点几何业务规则（§7.2 点/姿态反射＋rule 保留判定）；反射数学面归 reflectPosition/reflectRpy 行，业务编排为产品唯一职责。'],
  [817, 'applyMirror', 'requirements.applyMirror', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '批量镜像业务（来源选择、消歧、provenance 快照）；上游无。'],
  [1137, 'applyArray', 'requirements.applyArray', 'NONE', 'business-specific', 'owner', ['REQ-11', 'AT-24'], 'S1', '线性/矩形/圆形阵列生成业务（产品网格语义）；上游无网格模板生成（S1 零命中）。'],
  [1207, 'templateParamsOf', 'requirements.templateParamsOf', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', 'provenance 参数反解（产品会话协议 regenerate 入参）。'],
  [1228, 'mirrorPlaneOf', 'requirements.mirrorPlaneOf', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', '镜像面参数反解（产品协议）。'],
  [1309, 'arrayParamsOf', 'requirements.arrayParamsOf', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S2', '阵列参数反解（产品协议）。'],
  [1476, 'regenerate', 'requirements.regenerate', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '再生业务（AC4 无活传播、unlink 后再生——产品编辑器契约）。'],
  [taLines.length, 'unlinkGenerator', 'requirements.unlinkGenerator', 'NONE', 'business-specific', 'owner', ['REQ-11'], 'S1', '生成器解链业务（D-REQ-4 普通条目可手改的承载面）。'],
];

const SEARCHES = {
  S1: 'rg -n "TemplateArray|templateArray|generateGrid|gridGenerat" RobWork/RobWork/src RobWork/RobWorkSim/src',
  S2: 'rg -n "ValueProvenance|methodTag|generatedSource" RobWork/RobWork/src RobWork/RobWorkSim/src',
  S1S2: 'rg -n "TemplateArray|templateArray|generateGrid|gridGenerat" RobWork/RobWork/src RobWork/RobWorkSim/src；rg -n "ValueProvenance|methodTag|generatedSource" RobWork/RobWork/src RobWork/RobWorkSim/src',
  S3: 'rg -n "SamplingPlan|normalizeSampling|samplingPlan" RobWork/RobWork/src RobWork/RobWorkSim/src',
  S4: 'rg -n "reflectionMatrix|reflectRpy|mirror.*conjugat" RobWork/RobWork/src RobWork/RobWorkSim/src',
  ROT: 'rg -n "RPY|toRotation3D|isProperRotation|operator\\*|angle" RobWork/RobWork/src/rw/math/RPY.hpp RobWork/RobWork/src/rw/math/RPY.cpp RobWork/RobWork/src/rw/math/Rotation3D.hpp',
  ROTPROBE: 'rg -n "RPY\\(|toRotation3D" RobWork/RobWork/src/rw/math/RPY.hpp RobWork/RobWork/src/rw/math/RPY.cpp（＋probe-r6 实测：REUSE-T01-api-r6）',
};
const SEARCH_CONCL = {
  S1: '上游无需求模板/阵列网格生成同义物（exit=1 零命中）——产品模板业务专属。',
  S2: '上游无 ValueProvenance/生成器方法标记同义物（exit=1 零命中）——provenance 协议产品专属。',
  S1S2: '上游无需求模板/阵列网格生成、ValueProvenance/生成器方法标记同义物（S1/S2 双检索 exit=1 零命中）——数据身份契约与文本协议产品专属。',
  S3: '上游无采样计划规范化/计划摘要同义物（exit=1 零命中）——产品计划契约专属。',
  S4: '上游无反射矩阵/镜像共轭同义物（exit=1 零命中）——det=−1 反射为产品业务变换。',
  ROT: '上游 RPY 正变换/反解/旋转值运算在位（MATH-ROT verified 登记面）——正变换换核，反解差异见行 differences。',
  ROTPROBE: '上游 RPY 反解存在但退化约定/带宽与产品契约不同（probe-r6 实测＋行 differences 三点差异）——保留产品反解。',
};

function mkRow(start, def, extra) {
  const [end, symbol, functionId, cap, decision, role, reqs, skey, diffs] = def;
  const row = {
    functionId,
    ownerUnit: 'requirements',
    requirements: reqs,
    sources: [{ path: taPath, symbol, qualifiedSymbol: '(anon)::' + symbol, startLine: start, endLine: end, sha256: digestRegion(taLines, start, end) }],
    targets: ['sdurws_ird_requirements'],
    usageRole: role,
    permissionRef: 'ARCH:3.6',
    capabilityId: cap,
    decision,
    status: 'migrated',
    search: { command: SEARCHES[skey], conclusion: SEARCH_CONCL[skey] },
    differences: diffs,
    inputs: [cmakeInput],
  };
  if (decision === 'business-specific') row.nonOverlapEvidence = nonOverlap;
  if (functionId === 'requirements.rotationFromRpy') {
    row.production = { entry: 'rotationFromRpy', upstream: 'toRotation3D', consumer: 'reflectRpy', target: 'sdurws_ird_requirements' };
    row.evidence = { path: E + '/production/requirements.rotationFromRpy.json', sha256: digestPath(E + '/production/requirements.rotationFromRpy.json') };
    row.auditEvidence = [
      { path: E + '/search-batch.txt', sha256: digestPath(E + '/search-batch.txt') },
      { path: E + '/run/upstreamComparison.xml', sha256: digestPath(E + '/run/upstreamComparison.xml') },
    ];
  }
  if (extra) Object.assign(row, extra);
  return row;
}

// 构建 TemplateArray 全铺面行
const rows = [];
// direct 行生产消费者绑定（audit-contract §6：entry 与 consumer 均须在行 sources 符号内）——
// rotationFromRpy 的生产消费者是 reflectRpy（唯一调用点），该区域同时归属自身 business-specific 行。
function directRowConsumerBinding(rows){
  const d=rows.find(r=>r.functionId==='requirements.rotationFromRpy');
  const rr=rows.find(r=>r.functionId==='requirements.reflectRpy');
  d.sources.push(JSON.parse(JSON.stringify(rr.sources[0])));
}
let start = 1;
for (const def of R) {
  rows.push(mkRow(start, def));
  start = def[0] + 1;
}
if (start - 1 !== taLines.length) throw new Error('coverage gap: last end ' + (start - 1) + ' != ' + taLines.length);

// —— 其余行（文件未变，区域与摘要沿用现值）——
function pendingRow(id, srcs, targets, role, cap, decision, reqs, skey, diffs, extra) {
  const row = {
    functionId: id,
    ownerUnit: 'requirements',
    requirements: reqs,
    sources: srcs,
    targets,
    usageRole: role,
    permissionRef: 'ARCH:3.6',
    capabilityId: cap,
    decision,
    status: (decision === 'pending' || decision === 'proposal') ? decision : 'migrated',
    search: { command: SEARCHES[skey], conclusion: SEARCH_CONCL[skey] },
    differences: diffs,
    inputs: [cmakeInput],
  };
  if (decision === 'business-specific') row.nonOverlapEvidence = nonOverlap;
  if (extra) Object.assign(row, extra);
  return row;
}

function src(path, symbol, s, e, knownSha) {
  return { path, symbol, qualifiedSymbol: symbol, startLine: s, endLine: e, sha256: knownSha || digestRegion(fs.readFileSync(ROOT + '/' + path, 'utf8').replace(/^\uFEFF/, '').replace(/\r\n/g, '\n').split('\n'), s, e) };
}

// Sampling.cpp（消费一致性已核：evidence/Snapshot.hpp plannedPosition/plannedPoseSamples 同分母面、
// Evidence.hpp:1126 分母来源一致要求；SamplingTest 75U/0U＋GoldenDatasetsContractTest:813 钉 V-02/饱和语义）
const sSamplingA = src(prod + 'requirements/src/Sampling.cpp', 'SamplingPlanBuilder::buildPlan', 35, 83);
const sSamplingB = src(prod + 'requirements/src/Sampling.cpp', 'SamplingPlanBuilder::digest', 85, 130);
rows.push(pendingRow('requirements.SamplingPlanBuilder.buildPlan', [sSamplingA], ['sdurws_ird_requirements'], 'owner', 'NONE', 'business-specific', ['KIN-04', 'V-02'], 'S3',
  '构造规范化采样计划（区域盒非退化/R1 Box 形态锚点/姿态计数≥1），规范化单源复用 WorkRegionService::normalizeSampling——不执行随机/姿态采样、不复制生成器；上游无采样计划概念（S3 零命中）。'));
rows.push(pendingRow('requirements.SamplingPlanBuilder.digest', [sSamplingB], ['sdurws_ird_requirements'], 'owner', 'NONE', 'business-specific', ['KIN-04', 'V-02'], 'S3',
  '分母面消费一致性已核（原 pending 待核项关闭）：Random 计划分母=randomCount（批34 F7/A-18）、Grid=饱和乘积（R3 审计 O-1 上界 0xFFFFFFFE 钉）、零样本 0 合法（V-02）——SamplingTest:190(75U)/:220(0U)/:225(Random zero) 与 GoldenDatasetsContractTest:813 真实断言；下游 evidence/Snapshot.hpp plannedPositionSamples/plannedPoseSamples 与 Evidence.hpp:1126「与快照采样计划一致（分母来源）」同面引用，无第二分母。canonical 摘要复用 core ContentDigester（REUSE-T13/T15 面），不另写 SHA 核心。'));

// PanelWidget/CommandFlows（文件未改——行收口不改文件；targetEditRotation 保持 pending）
const pw = prod + 'requirements/plugin/RequirementsPanelWidget.cpp';
const cf = prod + 'requirements/plugin/RequirementsCommandFlows.cpp';
rows.push(pendingRow('requirements.targetEditRotation', [src(pw, 'targetEditRotation', 72, 78)], ['sdurws_ird_requirements_plugin'], 'adapter', 'MATH-ROT', 'pending', ['REQ-11'], 'ROT',
  '交互位姿正变换与 rotationFromRpy 同族（RPY(yaw,pitch,roll).toRotation3D() 逐位等价候选，REUSE-T42 探针已证恒等）。保持 pending 原因＝覆盖墙：RequirementsPanelWidget.cpp 4165 行，任何改动触发复用门禁对该文件的全行铺面＋全文件解决（T33/T36/T40 结构同型），超出本批边界；后续 UI 批次全行铺面后按候选落地。'));
rows.push(pendingRow('requirements.targetEditRpy', [src(pw, 'targetEditRpy', 80, 86)], ['sdurws_ird_requirements_plugin'], 'adapter', 'MATH-ROT', 'business-specific', ['REQ-11'], 'ROTPROBE',
  '交互反解保留：hypot 首列余弦＋cp>1e-12 万向锁分支（roll=0、yaw=atan2(−R01,R11) 确定性特例）；上游 RPY(R,eps=1e-5) 带宽吸附与退化轴约定差异同 reflectRpy 行三点实证（probe-r6）——1e-8rad 偏离禁止吸附（REQ-10），保留产品反解。'));
rows.push(pendingRow('requirements.finiteTargetEditPose', [src(pw, 'finiteTargetEditPose', 88, 104)], ['sdurws_ird_requirements_plugin'], 'adapter', 'MATH-ROT', 'business-specific', ['REQ-11'], 'ROT',
  '交互端口位姿业务校验：m/rad、world 系、正交右手 1e-8 容差与 det=+1 业务判定；上游 isProperRotation 容差语义与产品 1e-8 契约不同（REUSE-T42 探针 v1 general-3 实证上游自身容差在极端幅值判 false），不满足产品契约，保留产品校验。'));
rows.push(pendingRow('requirements.rotationToRpyPresentation', [src(cf, 'rotationToRpyPresentation', 75, 85)], ['sdurws_ird_requirements_plugin'], 'adapter', 'MATH-ROT', 'business-specific', ['REQ-10'], 'ROTPROBE',
  'TCP 捕获展示反解：1e-8rad 偏离不得吸附万向锁（WP-14-T12/REQ-10 数值回归）——上游 eps=1e-5 吸附带直接违约；退化分支 roll=0、yaw=atan2(−R01,R11) 工程惯例为摘要约定。冒烟外联缺供历史不构成豁免（构建供给已修）。probe-r6 确认表示差异，保留产品反解。'));

// 测试骨架与 CMake（同族先例：selection.contract_test 行、REUSE-T32 ird_add_gtest 定案）
rows.push(pendingRow('requirements.contract_test.ContractTestMain.main', [src(prod + 'requirements/contract_test/ContractTestMain.cpp', 'main', 19, 26)], ['sdurws_ird_requirements_contract_test'], 'adapter', 'TEST-HARNESS', 'business-specific', ['NFR-COR-01'], 'S1',
  'GTest 与现有 testkit 薄入口；保留原生 JSON/XML/退出码，禁止双 main 或强接 RobWork 全局环境（同 selection.contract_test 已闭行同族判）。'));
rows.push(pendingRow('requirements.gui_test.TestMainReport.main', [src(prod + 'requirements/gui_test/TestMainReport.cpp', 'main', 25, 37)], ['sdurws_ird_requirements_gui_test'], 'adapter', 'TEST-HARNESS', 'business-specific', ['NFR-COR-01'], 'S1',
  'GTest 与现有 testkit 薄入口；保留原生 JSON/XML/退出码，禁止双 main 或强接 RobWork 全局环境。Widgets 进程；Windows 平台一次一 exe。'));
rows.push(pendingRow('requirements.CMake.ird_add_gtest', [src(prod + 'requirements/CMakeLists.txt', 'ird_add_gtest', 473, 485)], ['sdurws_ird_requirements_test', 'sdurws_ird_requirements_contract_test', 'sdurws_ird_requirements_gui_test'], 'adapter', 'TEST-HARNESS', 'business-specific', ['NFR-COR-01'], 'S1',
  '20 单元同形薄宏；上游宏非 CONFIG 导出，原样共享一个产品 CMake include，不另建测试构建平台（REUSE-T32 定案同族判）。'));

directRowConsumerBinding(rows);

// —— 保留既有 resolved 行（requirements.test.TestMainReport.main）并排序输出 ——
const old = JSON.parse(fs.readFileSync(ROOT + '/' + 'RobWork/doc/industrial-robot-design/reuse/functions/requirements.json', 'utf8'));
const keep = old.functions.filter(r => r.functionId === 'requirements.test.TestMainReport.main');
if (keep.length !== 1) throw new Error('resolved anchor row not found');

const out = { schemaVersion: old.schemaVersion, unit: old.unit, functions: [...rows, ...keep] };
fs.writeFileSync(ROOT + '/' + 'RobWork/doc/industrial-robot-design/reuse/functions/requirements.json', JSON.stringify(out, null, 1) + '\n');
console.log('requirements.json written:', out.functions.length, 'rows');
