// 覆盖清查会话 P0 核对：两次 coverage NOT PASS 报告的遗漏项在冻结矩阵(c27349a80)中的现状。
// 只读：解析 functions/*.json 的 sources/auditedSections/inputs/functionId/qualifiedSymbol，输出每项命中情况。
import { readFileSync, writeFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';

const reuseRoot = 'RobWork/doc/industrial-robot-design/reuse';
const outLines = [];

// 两次报告的 (unit, fileSubstr, symbol, gapId) 清单。fileSubstr 用可唯一区分的路径片段。
const gaps = [
  // --- FCOV（第二次全覆盖，冻结 b83fa7a3）---
  ['FCOV-01', 'project', 'TxEngine.cpp', ['commit', 'scanForRecovery']],
  ['FCOV-02', 'project', 'CommandServiceImpl.cpp', ['submit']],
  ['FCOV-03', 'project', 'QueryPortImpl.cpp', ['branchHistory']],
  ['FCOV-04', 'project', 'ArchiveServiceImpl.cpp', ['finalize']],
  ['FCOV-05', 'project', 'AtomicFile.cpp', ['writeThrough', 'publishNew', 'replaceExisting']],
  ['FCOV-06', 'policy', 'PolicyParsing.cpp', ['runParsePipeline']],
  ['FCOV-07', 'policy', 'JointLimits.cpp', ['evaluate']],
  ['FCOV-08', 'policy', 'CollisionEvaluator.cpp', ['expandRulesAndCheckConflicts']],
  ['FCOV-09', 'execution', 'worker/main.cpp', ['wWinMain']],
  ['FCOV-10', 'execution', 'Admission.cpp', ['runArchiveTransaction']],
  ['FCOV-11', 'execution', 'Controller.cpp', ['forceKillSequence']],
  ['FCOV-12', 'diagnostics', 'Confirmable.cpp', ['submitConfirmation']],
  ['FCOV-13', 'diagnostics', 'Redaction.cpp', ['redact']],
  ['FCOV-14', 'testkit', 'Dataset.cpp', ['load']],
  ['FCOV-15', 'testkit', 'SetCheck.hpp', ['checkSetEquivalent', 'tryAugmentAt']],
  ['FCOV-16', 'core', 'Compare.cpp', ['closeWithin']],
  ['FCOV-17', 'foundation-governance', 'ird_gates.cmake', ['ird_classify_target', 'ird_read_flat', 'ird_extract_blocks']],
  ['FCOV-18', 'foundation-governance', 'make_fk_equation.mjs', ['closedFormFk']],
  ['FCOV-19', 'foundation-governance', 'make_dyn_two_link_golden.mjs', ['rneaReference']],
  ['FCOV-20', 'foundation-governance', 'make_pol_collision_analytic.mjs', ['handDerived']],
  // 第二次报告同文件反例
  ['FCOV-S1', 'project', 'ObjectStore.cpp', ['scanObjects']],
  ['FCOV-S2', 'execution', 'WorkerSupervisor.cpp', ['handleHandshake']],
  ['FCOV-S3', 'execution', 'Scheduler.cpp', ['dispatchOne']],
  ['FCOV-S4', 'optimization', 'ParetoTest.cpp', ['makeCandidate']],
  ['FCOV-S5', 'core', 'Compare.hpp', ['make']],
  // --- COV（第一次覆盖审查，冻结 bb997f1c）---
  ['COV-01', 'trajectory', 'Planner.cpp', ['doInCollision', 'sampleStraightPath', 'makePathPlannerAdapter', 'planPtpWithObstacleAvoidance']],
  ['COV-02', 'optimization', 'Pareto.cpp', ['notWorse', 'strictlyBetter', 'dominates', 'buildFront']],
  ['COV-03', 'optimization', 'Generator.cpp', ['generateNextBatch', 'generateWithBuiltInStrategy']],
  ['COV-04', 'dynamics', 'PowerEnergy.cpp', ['compute']],
  ['COV-05', 'kinematics', 'Envelope.cpp', ['probeReachability', 'computeApproximateEnvelope']],
  ['COV-06', 'modeling', 'XacroExpand.cpp', ['escapeXmlText', 'escapeXmlAttribute', 'substituteText', 'expand']],
  ['COV-07', 'execution', 'ChannelProtocol.cpp', ['putU32', 'feed', 'accept', 'fragmentMessage']],
  ['COV-08', 'project', 'Codec.cpp', ['JsonParser', 'parseValue', 'parseString']],
  ['COV-09', 'reporting', 'ReportCodec.cpp', ['Writer', 'parseData', 'parseFull']],
  ['COV-10', 'testkit', 'JsonLite.cpp', ['skipWs', 'expectLiteral']],
  ['COV-11', 'testkit', 'Check.cpp', ['checkCloseWithin', 'checkAllCloseWithin']],
  ['COV-11b', 'testkit', 'ProcessRunner.cpp', ['quoteWindowsArg', 'buildEnvironmentBlock']],
  ['COV-12', 'dynamics', 'StatisticsTest.cpp', ['expectNearRel', 'rowAt']],
  ['COV-12b', 'modeling', 'XacroExpandTest.cpp', ['digestOf', 'existingNodeFor']],
  ['COV-15', 'ui', 'HostView3DPreviewBackend.cpp', ['draw', 'clear']],
  ['COV-16', 'workflow', 'CloseSupervisor.cpp', ['superviseLoop', 'start']],
];

function loadMatrix() {
  const units = {};
  for (const f of readdirSync(reuseRoot + '/functions')) {
    if (!f.endsWith('.json')) continue;
    const unit = f.replace('.json', '');
    const doc = JSON.parse(readFileSync(join(reuseRoot, 'functions', f), 'utf8'));
    units[unit] = doc.functions;
  }
  return units;
}

const matrix = loadMatrix();

for (const [gapId, unit, fileSub, symbols] of gaps) {
  const rows = matrix[unit] || [];
  // 该文件在矩阵中的所有行
  const fileRows = rows.filter(r =>
    JSON.stringify(r.sources ?? []).includes(fileSub) ||
    JSON.stringify(r.auditedSections ?? []).includes(fileSub) ||
    JSON.stringify(r.inputs ?? []).includes(fileSub));
  const srcRows = fileRows.filter(r =>
    JSON.stringify(r.sources ?? []).includes(fileSub) ||
    JSON.stringify(r.auditedSections ?? []).includes(fileSub));
  const symHits = [];
  for (const s of symbols) {
    const hit = fileRows.filter(r => JSON.stringify(r).includes(s));
    symHits.push(`${s}: ${hit.length ? 'HIT(' + hit.map(r => r.functionId).join(',') + ')' : 'MISS'}`);
  }
  const status = srcRows.length ? 'FILE-COVERED' : (fileRows.length ? 'INPUT-ONLY' : 'ABSENT');
  outLines.push(`${gapId}\t${unit}\t${fileSub}\t${status}\t${symHits.join('; ')}`);
}
writeFileSync('.cache/coverage-sweep/known-gaps-status.tsv', outLines.join('\n') + '\n');
console.log(outLines.join('\n'));
