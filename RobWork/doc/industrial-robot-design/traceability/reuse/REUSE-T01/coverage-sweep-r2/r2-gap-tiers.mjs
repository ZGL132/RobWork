// R2 三档核对：A=字符串命中（矩阵行文本含符号子串）；B=语义覆盖（符号实际定义行落在该文件
// 某sources/auditedSections区段[startLine,endLine]内）；C=独验引用（acceptance归档文本出现
// functionId或"文件+符号"）。三档严格区分，不互相冒充。
import { readFileSync, writeFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';

const reuseRoot = 'RobWork/doc/industrial-robot-design/reuse';
const prodRoot = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot';
const accRoot = 'RobWork/doc/industrial-robot-design/traceability/acceptance';

// (gapId, unit, fileSuffix(相对prodRoot的唯一片段), symbol, 定义匹配正则)
const gaps = [
  ['FCOV-09', 'execution', 'execution/worker/main.cpp', 'wWinMain'],
  ['FCOV-11', 'execution', 'execution/src/Controller.cpp', 'forceKillSequence'],
  ['FCOV-12', 'diagnostics', 'diagnostics/src/Confirmable.cpp', 'submitConfirmation'],
  ['FCOV-13', 'diagnostics', 'diagnostics/src/Redaction.cpp', 'redact'],
  ['FCOV-17a', 'foundation-governance', 'cmake/ird_gates.cmake', 'ird_classify_target'],
  ['FCOV-17b', 'foundation-governance', 'cmake/ird_gates.cmake', 'ird_read_flat'],
  ['FCOV-17c', 'foundation-governance', 'cmake/ird_gates.cmake', 'ird_extract_blocks'],
  ['FCOV-18', 'foundation-governance', 'testdata/golden/rt-fk-equation/1.0.0/generate/make_fk_equation.mjs', 'closedFormFk'],
  ['FCOV-20', 'foundation-governance', 'testdata/golden/pol-collision-analytic/1.0.0/generate/make_pol_collision_analytic.mjs', 'handDerived'],
  ['FCOV-S1', 'project', 'project/src/ObjectStore.cpp', 'scanObjects'],
  ['FCOV-S2', 'execution', 'execution/src/WorkerSupervisor.cpp', 'handleHandshake'],
  ['FCOV-S3', 'execution', 'execution/src/Scheduler.cpp', 'dispatchOne'],
  ['COV-07p', 'execution', 'execution/src/ChannelProtocol.cpp', 'putU32'],
  // 旋转/位姿与FK链定向新增
  ['ROT-01', 'modeling', 'modeling/src/RpyMath.hpp', 'rpyToEaa'],
  ['ROT-02', 'runtime', 'runtime/src/BaseWorldTransform.cpp', 'BaseWorldTransform'],
  ['FK-01', 'runtime', 'runtime/src/CompilerImpl.cpp', 'buildCanonicalModel'],
  ['FK-02', 'kinematics', 'kinematics/include/sdurws/ird/kinematics/Fk.hpp', 'Fk'],
  ['FK-03', 'kinematics', 'kinematics/src/Evaluators.cpp', 'solveItem'],
  ['FK-04', 'kinematics', 'kinematics/src/Workspace.cpp', 'generateJointSpaceConfigurations'],
  ['FK-05', 'kinematics', 'kinematics/src/PoseReach.cpp', 'computePoseReachCoverage'],
];

function loadMatrix() {
  const units = {};
  for (const f of readdirSync(reuseRoot + '/functions')) {
    if (!f.endsWith('.json')) continue;
    units[f.replace('.json', '')] = JSON.parse(readFileSync(join(reuseRoot, 'functions', f), 'utf8')).functions;
  }
  return units;
}
const matrix = loadMatrix();

// acceptance 归档文本聚合（浅层引用判定用）
let accText = '';
for (const d of readdirSync(accRoot)) {
  try {
    for (const f of readdirSync(join(accRoot, d))) {
      if (/\.(md|json|txt|log)$/.test(f)) accText += readFileSync(join(accRoot, d, f), 'utf8');
    }
  } catch { /* 文件非目录 */ }
}

function findDefLine(absPath, sym) {
  let txt;
  try { txt = readFileSync(absPath, 'utf8'); } catch { return null; }
  const lines = txt.split('\n');
  // 优先：返回类型/函数名定义形态；兜底：任意出现
  const defRe = new RegExp(`(^|[^\\w:])${sym}\\s*\\(|(?:function|const)\\s+${sym}\\s*[=(]`);
  for (let i = 0; i < lines.length; i++) if (defRe.test(lines[i])) return i + 1;
  return null;
}

const out = [];
for (const [gapId, unit, fileSuffix, symbol] of gaps) {
  const rows = matrix[unit] || [];
  const fileRows = rows.filter(r => JSON.stringify(r).includes(fileSuffix));
  const tierA = fileRows.filter(r => JSON.stringify(r.sources ?? []).includes(fileSuffix) || JSON.stringify(r.auditedSections ?? []).includes(fileSuffix));
  // B: 语义区段
  const abs = join(prodRoot, fileSuffix);
  const defLine = findDefLine(abs, symbol);
  let coveredBy = null;
  if (defLine) {
    outer: for (const r of fileRows) {
      for (const coll of [r.sources ?? [], r.auditedSections ?? []]) {
        for (const s of coll) {
          const p = typeof s === 'string' ? s : s.path ?? '';
          if (!p.includes(fileSuffix)) continue;
          const a = typeof s === 'string' ? null : s;
          if (a && a.startLine != null && defLine >= a.startLine && defLine <= (a.endLine ?? a.startLine)) { coveredBy = r.functionId; break outer; }
        }
      }
    }
  }
  // C: 独验引用（文件名或functionId出现在acceptance文本）
  const baseFile = fileSuffix.split('/').pop();
  const cFile = accText.includes(baseFile);
  const cIds = fileRows.filter(r => accText.includes(r.functionId)).map(r => r.functionId);
  const tier = coveredBy ? 'B-语义覆盖' : (tierA.length ? 'A-仅字符串命中' : (fileRows.length ? 'I-仅inputs' : 'ABSENT-零登记'));
  out.push([gapId, unit, fileSuffix.split('/').slice(-2).join('/'), symbol, `def@${defLine ?? '?'}`, tier, coveredBy ?? '-', cFile ? 'acceptance有文件引用' : 'acceptance无引用', cIds.slice(0, 3).join('|')].join('\t'));
}
writeFileSync('.cache/coverage-sweep/r2-gap-tiers.tsv', out.join('\n') + '\n');
console.log(['gap', 'unit', 'file', 'symbol', 'defLine', 'tier', 'coveredBy', 'accFile', 'accIds'].join('\t'));
console.log(out.join('\n'));
