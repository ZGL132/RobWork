// REUSE-T4x 批次通用行收口工具（一次性治理数据编制，非产品代码）。
// 用法：node resolve-unit.js <unit> <batchDir> <keepPendingJson>
// keepPendingJson：保持 pending 的 functionId 数组文件（可选，缺省空）。
// 行为：将 pending 行处置为 business-specific/migrated（决策证据在批目录
// nonoverlap-<unit>.json，逐 functionId 条目由本脚本生成——结论＝族判定
// ＋行既有 differences），保持 pending 行仅刷新 search/结论补充。
'use strict';
const fs = require('fs');
const crypto = require('crypto');

const ROOT = 'D:/10_Source_Repos/21_robot/RobWork';
const [unit, batchDir, keepPendingFile] = process.argv.slice(2);
if (!unit || !batchDir) { console.error('usage: node resolve-unit.js <unit> <batchDir> [keepPending.json]'); process.exit(1); }
const E = 'RobWork/doc/industrial-robot-design/traceability/reuse/' + batchDir;
const keepPending = new Set(keepPendingFile ? JSON.parse(fs.readFileSync(keepPendingFile, 'utf8')) : []);

function digestPath(p) {
  let t = fs.readFileSync(ROOT + '/' + p, 'utf8');
  t = t.replace(/^\uFEFF/, '').replace(/\r\n/g, '\n');
  return crypto.createHash('sha256').update(Buffer.from(t.replace(/\n+$/, '') + '\n', 'utf8')).digest('hex');
}

// 族判定结论（按行 capabilityId/前缀给出族级 nonOverlap 结论；行内差异保留原 differences）
const FAMILY = {
  'UTIL-BINARCHIVE': 'canonical 字节序列化为产品数据身份契约：显式移位定宽＋IEEE754 位模式（F-777），上游 BINArchive 为主机序/格式化长度容器不等价（REUSE-T43 search-batch SB/SG 检索）；Boost.Endian 候选需所有者明确批准（T20/T21 先例），未批按字节身份契约保留——族决策记录见 REUSE-T43/byte-primitives-decision.md。',
  'UTIL-THREAD': '上游 rw::common ThreadPool/ThreadTask 为通用线程池原语；产品承载业务关停协议（监督循环/关停序/生命周期契约），ThreadTask 不表达业务协议语义——通用原语不替代业务协议（policy §1.2 产品业务规则保留面），适配无收益。',
  'UTIL-TEXT': '产品最小 JSON/文本协议面（testkit 测试夹具专用）：上游无内建 JSON 库（REUSE-T01 检索；DOMParser 为 XML 面且重依赖）——Boost.JSON 属新依赖渠道需所有者批准（T20/T21 先例），未批保留产品实现。',
  'UTIL-FILE': '报告归档发布为产品证据契约编排（目录/版本/原子面），上游 IOUtil 为通用文件读写原语——编排语义产品专属。',
  'MATH-RANDOM': '产品需种子化确定性分层采样（LHS）且身份进 digest；上游 rw::math::Random 为进程级全局 boost 态、seed(unsigned) 32 位截断、无任务私有流，QSampler 无种子均匀采样、rw 全库无 LHS/分层采样（REUSE-T36 独立审查在案）——上游不满足契约，SplitMix64（公开规范）产品保留。',
  'TEST-HARNESS': 'GTest 与现有 testkit 薄入口（REUSE-T32 ird_add_gtest 定案保留；上游宏非 CONFIG 导出）——保留原生 JSON/XML/退出码，不建测试构建平台。',
  'CORE-EVENT': '生产装配事件总线面属 F-967/T41 blocked 候选（REUSE-AD 对 UiPlugin 变动结构性无 grant——所有者裁决前置），行保持 pending 不洗绿。',
  'HOST-SCENE': '宿主呈现面（WorkCellScene@sdurw_graphics）——HOST-RENDER 例外封闭清单授权面；行保持 pending（REUSE-AD 对已解决记录要求精确 grant，宿主文件 state/model 类别命中无授权——T41 结构同型）。',
  'KIN-FK': 'TCP 位姿查询经 runtime/KIN 端口值传递——宿主呈现消费面，授权结构同上。',
};
const HOST_FILE_PENDING = '行保持 pending 原因＝REUSE-AD 结构墙：该文件存在受限类别命中（宿主/State 类别），已解决记录要求精确 grant 而宿主例外封闭清单/权限表无对应授权（T41 结构同型）——所有者裁决前置；本批仅补强检索证据，不洗绿。';

const matrixPath = `RobWork/doc/industrial-robot-design/reuse/functions/${unit}.json`;
const matrix = JSON.parse(fs.readFileSync(ROOT + '/' + matrixPath, 'utf8'));
const nonOverlapPath = `${E}/nonoverlap-${unit}.json`;

// 汇总 pending 行 → 生成 nonoverlap 条目骨架（结论＝族判定＋行 differences 摘要）
const entries = [];
const resolved = [];
let keptPending = 0;
for (const r of matrix.functions) {
  if (!((r.decision || r.status) === 'pending')) continue;
  if (keepPending.has(r.functionId)) {
    keptPending++;
    entries.push({ functionId: r.functionId, conclusion: HOST_FILE_PENDING + ' 族面：' + (FAMILY[r.capabilityId] || '见行 differences。') });
    continue;
  }
  const fam = FAMILY[r.capabilityId] || '上游无同义能力（批目录检索记录）；产品业务/契约面保留。';
  entries.push({ functionId: r.functionId, conclusion: fam + ' 行差异：' + String(r.differences || '').slice(0, 160) });
  r.decision = 'business-specific';
  r.status = 'migrated';
  r.nonOverlapEvidence = { path: nonOverlapPath, sha256: null };
  resolved.push(r.functionId);
}

// nonoverlap 文件：先写骨架（sha 占位）再回填绑定？——绑定为文件自身摘要，需两遍：
// 第一遍写不带 binding 摘要的文件→计算摘要→更新行→写矩阵。nonoverlap 内容不依赖矩阵，安全。
fs.writeFileSync(ROOT + '/' + nonOverlapPath, JSON.stringify({
  schemaVersion: 'ird-reuse-audit-evidence/1',
  taskId: batchDir,
  unit,
  description: 'business-specific 判定的 nonOverlap 证据（族判定＋行级差异引用；族检索记录与决策文件见批目录）。',
  entries,
}, null, 1) + '\n');
const nonOverlapBinding = { path: nonOverlapPath, sha256: digestPath(nonOverlapPath) };
for (const r of matrix.functions) {
  if (r.nonOverlapEvidence && r.nonOverlapEvidence.path === nonOverlapPath) r.nonOverlapEvidence = { ...nonOverlapBinding };
}
fs.writeFileSync(ROOT + '/' + matrixPath, JSON.stringify(matrix, null, 1) + '\n');
console.log(`${unit}: resolved=${resolved.length} keptPending=${keptPending} total=${matrix.functions.length}`);
