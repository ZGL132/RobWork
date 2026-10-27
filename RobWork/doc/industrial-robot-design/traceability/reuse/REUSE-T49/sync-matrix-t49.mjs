// REUSE-T49 落地批矩阵同步脚本（本批产物——重跑可复现同一结果）。
// 职责：按治理 Digest 口径（BOM 剥离、CRLF→LF、单一尾换行——validate-reuse.ps1
// Digest 同款）重算本批改动产品文件摘要，执行：
//   1) foundation-governance.make_import_expectations.fixtureGeneration 转
//      business-specific/migrated（区段随修复重铺 46..154＋inputs 的
//      manifest.json 摘要级联重钉＋auditEvidence 增重放记录）；
//   2) 新增 foundation-governance.make_import_expectations.scriptHeader
//      头注/导入区段行覆盖载体（1..45——project.audit-t40/T41 整段载体先例形态）；
//   3) changes/REUSE-T49.json 登记 .mjs 凭证（治理摘要）。
// JSON 回写保持各文件原格式（foundation-governance 为 CRLF＋1 空格缩进、
// changes 为 LF＋1 空格缩进）。
'use strict';
import fs from 'node:fs';
import crypto from 'node:crypto';
import path from 'node:path';

const ROOT = path.resolve(import.meta.dirname, '../../../../../..');
const MJS = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/golden/mdl-urdf-import/1.0.0/generate/make_import_expectations.mjs';
const MANIFEST = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/golden/mdl-urdf-import/1.0.0/manifest.json';
const README = 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T49/README.md';
const REPLAY = 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T49/landing-generator-replay.json';
const FN = 'RobWork/doc/industrial-robot-design/reuse/functions/foundation-governance.json';
const CHANGES = 'RobWork/doc/industrial-robot-design/reuse/changes/REUSE-T49.json';
const MODELING_CMAKE = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/CMakeLists.txt';

function digest(text) {
  const t = text.replace(/^\uFEFF/, '').replace(/\r\n/g, '\n').replace(/\n+$/, '');
  return crypto.createHash('sha256').update(Buffer.from(t + '\n', 'utf8')).digest('hex');
}
function fileDigest(rel) { return digest(fs.readFileSync(path.join(ROOT, rel), 'utf8')); }
function linesOf(rel) {
  return fs.readFileSync(path.join(ROOT, rel), 'utf8').replace(/^\uFEFF/, '').replace(/\r\n/g, '\n').split('\n');
}
function regionDigest(rel, start, end) {
  return digest(linesOf(rel).slice(start - 1, end).join('\n'));
}
function loadJson(rel) {
  const raw = fs.readFileSync(path.join(ROOT, rel), 'utf8');
  return { j: JSON.parse(raw), crlf: raw.includes('\r\n') };
}
function saveJson(rel, obj, crlf) {
  let out = JSON.stringify(obj, null, 1) + '\n';
  if (crlf) out = out.replace(/\n/g, '\r\n');
  fs.writeFileSync(path.join(ROOT, rel), out);
}

const mjsDigest = fileDigest(MJS);
const manifestDigest = fileDigest(MANIFEST);
const readmeDigest = fileDigest(README);
const replayDigest = fileDigest(REPLAY);
const modelingCmakeDigest = fileDigest(MODELING_CMAKE);
const mjsLines = linesOf(MJS);
const importReportLine = mjsLines.findIndex((l) => l.includes('const importReport')) + 1;
if (importReportLine < 2) throw new Error('importReport marker not found');
let total = mjsLines.length;
while (total > 1 && mjsLines[total - 1].trim() === '') total--;

// ---- 1) fixtureGeneration 行迁移态 ----
{
  const { j, crlf } = loadJson(FN);
  const row = j.functions.find((f) => f.functionId === 'foundation-governance.make_import_expectations.fixtureGeneration');
  if (!row) throw new Error('fixtureGeneration row missing');
  row.sources = [
    {
      path: MJS,
      symbol: 'const importReport',
      qualifiedSymbol: 'ESModule::mdl-urdf-import::fixtureGeneration',
      startLine: importReportLine,
      endLine: total,
      sha256: regionDigest(MJS, importReportLine, total),
    },
  ];
  row.capabilityId = 'NONE';
  row.decision = 'business-specific';
  row.status = 'migrated';
  row.search = {
    command: '见batch16-search.json的实际检索、generator-replay.json运行和本批语义审读；REUSE-T49 落地批重放 landing-generator-replay.json（29/29 exit0、规范化漂移0、mdl-urdf-import 原始字节逐位一致）',
    conclusion: '测试供数/独立oracle与生产复用区分；具体上游限制及现有成熟工具见differences，未以零命中批准新核心。F-959 漂移随 REUSE-T49 落地批收口（所有者 2026-10-26 批量批准）',
  };
  row.differences = '顶层手工契约fixture用Node fs/path/url及JSON.stringify；F-959 修复（REUSE-T49 落地批）：生成器随 F01 期望面同步——unsupported 8→3、去 joint-velocity-limit×5、增 mappedVelocityCount=5 计数锚，头注计数约定同步；隔离重放产物与入库 expected 原始字节逐位一致，不重产旧错误黄金数据';
  row.auditBoundary = 'REUSE-T49 落地批（F-959）实施完成待独验：脚本＋manifest 完整性登记两件数据面改动，expected 数据零变化、零C++产品代码变化；testkit 121/121＋modeling 480/480＋contract 17/17＋ird_gates 零命中（landing-* 原件）；GoldenImportTest.cpp 既有过期文档注（T16 创建时即有）不属漂移链未改——留独立 cosmetic 缺陷归所有者；不证明 T01 完整或迁移完成';
  row.nonOverlapEvidence = { path: README, sha256: readmeDigest };
  row.inputs = row.inputs.map((b) => (b.path === MANIFEST ? { path: b.path, sha256: manifestDigest } : b));
  row.auditEvidence = [
    ...row.auditEvidence,
    { path: REPLAY, sha256: replayDigest },
    { path: README, sha256: readmeDigest },
  ];
  saveJson(FN, j, crlf);
}

// ---- 2) scriptHeader 行覆盖载体（头注/导入区段） ----
{
  const { j, crlf } = loadJson(FN);
  if (j.functions.some((f) => f.functionId === 'foundation-governance.make_import_expectations.scriptHeader')) {
    throw new Error('scriptHeader row already exists');
  }
  j.functions.push({
    functionId: 'foundation-governance.make_import_expectations.scriptHeader',
    ownerUnit: 'foundation-governance',
    requirements: ['MDL-01', 'MDL-19'],
    sources: [
      {
        path: MJS,
        symbol: 'make_import_expectations',
        qualifiedSymbol: 'ESModule::mdl-urdf-import::scriptHeader',
        startLine: 1,
        endLine: importReportLine - 1,
        sha256: regionDigest(MJS, 1, importReportLine - 1),
      },
    ],
    targets: ['sdurws_ird_modeling_test'],
    usageRole: 'adapter',
    permissionRef: 'ARCH:3.6.1',
    capabilityId: 'NONE',
    decision: 'business-specific',
    status: 'migrated',
    search: {
      command: 'rg -n "make_import_expectations" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/golden/mdl-urdf-import/1.0.0/generate/make_import_expectations.mjs',
      conclusion: '生成器头注/导入区段行覆盖载体（F-959 落地批——改动文件全行区段覆盖规则；project.audit-t40 逐区段载体先例形态）；targets＝黄金 JSON 实际消费测试目标（T01 targetRelation=generated-artifact-consumer 在案口径）；行覆盖不充当语义完备证明',
    },
    differences: '脚本头注计数约定（F-959 同步：不支持清单 8→3＋F01 速度限位映射锚说明）与 node:fs/url/path 导入、thisDir 定位；注释与导入面，非上游算法核',
    inputs: [
      { path: MJS, sha256: mjsDigest },
      { path: MANIFEST, sha256: manifestDigest },
      { path: MODELING_CMAKE, sha256: modelingCmakeDigest },
    ],
    auditEvidence: [
      { path: REPLAY, sha256: replayDigest },
      { path: README, sha256: readmeDigest },
    ],
    nonOverlapEvidence: { path: README, sha256: readmeDigest },
    auditBoundary: '仅生成器头注/导入区段覆盖载体（REUSE-T49 落地批）；功能语义由 fixtureGeneration 行承载；不证明 T01 完整或迁移完成',
  });
  saveJson(FN, j, crlf);
}

// ---- 3) changes/REUSE-T49.json 凭证 ----
{
  const { j, crlf } = loadJson(CHANGES);
  if (!j.files.some((f) => f.path === MJS)) {
    j.files.push({
      path: MJS,
      sha256: mjsDigest,
      mode: 'business',
      capabilities: [],
      boundary: 'F-959 黄金生成脚本修复落地（所有者 2026-10-26 批量批准落地裁决表第 1~4 项，原 blocked 解阻）：生成器随 2026-10-20 F01 期望面同步——counts.unsupported 8→3、去 joint-velocity-limit×5、增 mappedVelocityCount=5＋mappedVelocityNote（与入库 expected 逐字段一致），头注计数约定同步；隔离重放（replay-generators-t49.mjs，29 数据集）产物与 expected 原始字节逐位一致、29/29 exit0。expected 数据零变化、零C++产品代码变化——按 T01 审计口径同步过期脚本，不重产旧错误黄金数据洗绿；testkit/modeling 三套件与 ird_gates 证据见 REUSE-T49/landing-*。',
      verify: 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T49/README.md',
    });
  }
  saveJson(CHANGES, j, crlf);
}

console.log('sync done:', JSON.stringify({
  mjsDigest, manifestDigest, readmeDigest, replayDigest, importReportLine, total,
}));
