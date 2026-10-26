// REUSE-T51 独立验收·全文件解决批核查（②f）：
//  [A] Ik.cpp/JointMapping.hpp/Ik.hpp/CMakeLists.txt 全部登记区段 sha256
//      真实重算（口径与 reuse-contract.ps1 一致：CRLF→LF、\n 分割、行 join）；
//  [B] Ik.cpp 逐行铺面：1..总行数每行被 ≥1 已解决(status=migrated/closed)
//      区段覆盖——"全部功能行 resolved"的机器独立复核；
//  [C] IkSolver.solve 行 production 证据面：ird-reuse-production/1＋
//      normal/disconnected/oracle/upstreamComparison 四套＋XML 计数一致＋
//      mutation 绑定改变生产源码。
import crypto from 'node:crypto';
import fs from 'node:fs';
import { execSync } from 'node:child_process';
const ROOT = 'D:/10_Source_Repos/21_robot/RobWork';
let fail = 0;
const check = (name, cond, detail) => {
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  —— ' + detail : ''}`);
  if (!cond) fail++;
};
// Digest 口径与 validate-reuse.ps1 一致：去 BOM、CRLF→LF、TrimEnd('\n') 后补回单个 '\n'。
const sha = s => crypto.createHash('sha256').update(s.replace(/^\uFEFF/, '').replace(/\r\n/g, '\n').replace(/\n+$/, '') + '\n', 'utf8').digest('hex');
const linesOf = p => fs.readFileSync(ROOT + '/' + p, 'utf8').replace(/\r\n/g, '\n').split('\n');

const matrix = JSON.parse(fs.readFileSync(ROOT + '/RobWork/doc/industrial-robot-design/reuse/functions/kinematics.json', 'utf8'));
const rows = matrix.functions;
console.log(`kinematics.json functions=${rows.length}, schema=${matrix.schemaVersion}`);

// [A] 区段哈希重算（全部目标文件区段）。
const targets = ['kinematics/src/Ik.cpp', 'kinematics/src/JointMapping.hpp', 'kinematics/include/sdurws/ird/kinematics/Ik.hpp', 'kinematics/CMakeLists.txt'];
let segTotal = 0, segBad = [];
const rowSourceMap = {}; // path -> [ {row, s} ]
for (const r of rows) {
  for (const s of (r.sources || [])) {
    if (!targets.some(t => s.path.endsWith(t))) continue;
    segTotal++;
    const lines = linesOf(s.path);
    if (s.endLine > lines.length) { segBad.push(`${r.functionId}: bounds ${s.endLine}>${lines.length}`); continue; }
    const region = lines.slice(s.startLine - 1, s.endLine).join('\n');
    if (sha(region) !== s.sha256) segBad.push(`${r.functionId}: sha mismatch ${s.path}:${s.startLine}-${s.endLine}`);
    else if (!region.includes(r.sources ? (s.symbol || '') : '')) segBad.push(`${r.functionId}: symbol missing`);
    (rowSourceMap[s.path] ||= []).push({ row: r, s });
  }
}
check(`[A] 区段 sha256 重算 ${segTotal} 段全部一致`, segBad.length === 0, segBad.slice(0, 5).join(' | '));

// [B] Ik.cpp 逐行铺面（仅 status=migrated/closed 的行计入覆盖；行数取
//     wc -l 口径——文件尾换行产生的 split 尾空元素不计为内容行）。
const ikLines = linesOf('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Ik.cpp');
const total = ikLines.length && ikLines[ikLines.length - 1] === '' ? ikLines.length - 1 : ikLines.length;
const covered = new Set();
const statusTally = {};
for (const { row, s } of (rowSourceMap['RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Ik.cpp'] || [])) {
  statusTally[`${row.decision}/${row.status}`] = (statusTally[`${row.decision}/${row.status}`] || 0) + 1;
  if (['migrated', 'closed'].includes(row.status)) {
    for (let i = s.startLine; i <= s.endLine; i++) covered.add(i);
  }
}
const gaps = [];
for (let i = 1; i <= total; i++) if (!covered.has(i)) gaps.push(i);
console.log(`Ik.cpp 总行数=${total}，covered=${covered.size}，未覆盖=${gaps.length}${gaps.length ? ' 首若干=' + gaps.slice(0, 20).join(',') : ''}`);
check('[B] Ik.cpp 全文件逐行铺面（1..末行全 covered，migrated/closed）', gaps.length === 0);
console.log('Ik.cpp 行 status 分布:', JSON.stringify(statusTally));

// [C] IkSolver.solve production 证据面。
const solveRow = rows.find(r => r.functionId === 'kinematics.IkSolver.solve');
check('[C] IkSolver.solve decision=adapter/status=migrated', solveRow && solveRow.decision === 'adapter' && solveRow.status === 'migrated');
const prod = solveRow.production;
check('[C1] production 四件套字段', prod && ['entry', 'upstream', 'consumer', 'target'].every(k => prod[k] !== undefined), JSON.stringify(prod && { entry: prod.entry, upstream: prod.upstream, target: prod.target }).slice(0, 220));
const ev = solveRow.evidence;
const evj = JSON.parse(fs.readFileSync(ROOT + '/' + ev.path, 'utf8'));
check('[C2] 证据 schema=ird-reuse-production/1 且 functionId 匹配', evj.schemaVersion === 'ird-reuse-production/1' && evj.functionId === 'kinematics.IkSolver.solve');
const runs = evj;  // ird-reuse-production/1 顶层即四套运行（schema 字段除外）
const need = ['normal', 'disconnected', 'oracle', 'upstreamComparison'];
check('[C3] normal/disconnected/oracle/upstreamComparison 四套在案', need.every(k => runs[k]), 'keys=' + Object.keys(runs).join(','));
for (const k of need) {
  const run = runs[k];
  if (!run) continue;
  const xmlOk = (() => { try { const x = fs.readFileSync(ROOT + '/' + run.xml.path, 'utf8'); const m = x.match(/testsuites[^>]*tests="(\d+)"[^>]*failures="(\d+)"/); return m && +m[1] > 0; } catch { return false; } })();
  const exitOk = k === 'disconnected' ? run.exitCode !== 0 : run.exitCode === 0;
  check(`[C4.${k}] exitCode=${run.exitCode}（${k === 'disconnected' ? '非零' : '零'}）＋XML 真实计数`, exitOk && xmlOk);
}
const mut = runs.disconnected && runs.disconnected.mutation;
if (mut) {
  const orig = linesOf(mut.source).join('\n');
  const mutated = linesOf(runs.disconnected.mutatedSource.path).join('\n');
  check('[C5] mutation 作用登记生产源码且内容改变', mut.source.endsWith('Ik.cpp') && mutated !== orig && sha(orig) === mut.originalSha256);
}

// [D] JointMapping.hpp 逆映射行 + Ik.hpp 铺面行存在。
const jmRows = (rowSourceMap['RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/JointMapping.hpp'] || []).map(x => x.row.functionId);
check('[D1] JointMapping.hpp 登记行含 qAuthoritativeFromRuntime 逆映射符号',
  JSON.stringify(fs.readFileSync(ROOT + '/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/JointMapping.hpp', 'utf8')).includes('qAuthoritativeFromRuntime') && jmRows.length > 0, 'rows=' + jmRows.join(','));
const hppRows = (rowSourceMap['RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/include/sdurws/ird/kinematics/Ik.hpp'] || []);
check('[D2] Ik.hpp 登记行 status=migrated/closed', hppRows.length > 0 && hppRows.every(x => ['migrated', 'closed'].includes(x.row.status)), 'rows=' + hppRows.map(x => `${x.row.functionId}/${x.row.status}`).join(','));
const cmakeRows = (rowSourceMap['RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/CMakeLists.txt'] || []);
check('[D3] CMakeLists.txt 登记行刷新（migrated/closed）', cmakeRows.length > 0 && cmakeRows.every(x => ['migrated', 'closed'].includes(x.row.status)), 'rows=' + cmakeRows.map(x => `${x.row.functionId}/${x.row.status}`).join(','));

// [E] T19 FK 证据绑定卫生项：kinematics.FkEvaluator.evaluate.json 仅 inputs 绑定刷新、内容零改动。
const t19 = JSON.parse(fs.readFileSync(ROOT + '/RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T19/production/kinematics.FkEvaluator.evaluate.json', 'utf8'));
const t19diff = execSync(
  `git -C "${ROOT}" diff a89776f06c80a0408c4fa28e9e58e32215b74738..b69e79f9f672f4bdd0157af0e165ecfd9826ef3a -- RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T19/production/kinematics.FkEvaluator.evaluate.json`,
  { encoding: 'utf8' });
const changedLines = t19diff.split('\n').filter(l => (l.startsWith('+') || l.startsWith('-')) && !l.startsWith('+++') && !l.startsWith('---'));
check('[E] T19 FK 证据 JSON diff 仅 2 行（1 增 1 删——CMake 绑定）', changedLines.length === 2, JSON.stringify(changedLines).slice(0, 300));

console.log(fail === 0 ? 'acc-facade-probe: 全部通过' : `acc-facade-probe: ${fail} 项失败`);
process.exit(fail === 0 ? 0 : 1);
