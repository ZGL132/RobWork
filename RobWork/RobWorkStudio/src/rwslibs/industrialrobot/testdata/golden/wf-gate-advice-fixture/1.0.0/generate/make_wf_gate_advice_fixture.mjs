// =====================================================================
// make_wf_gate_advice_fixture.mjs —— workflow 门控/建议/级联映射黄金夹具
// 的独立参考生成器（WP-22-T13 登记，golden wf-gate-advice-fixture）。
//
// 设计依据（语义权威——生成器按文档逐条直写，与产品 C++ 实现零共享代码，
// 双实现互证：任一侧漂移即消费测试显性失败）：
//   - units/workflow.md §4.2（域↔阶段映射表——八键多对一，聚合阶段两域）、
//     §4.3（门控状态词表五产出值＋级联失效提示三要素）、§4.4（门控状态机
//     ——前序链/域未装配/缺项锁定/计算中/结果事实五规则序贯）、§5.2
//     （事件消费面——归档/失效重放时序）、§6.2（UX-01 四要素）、§6.3
//     （建议八规则优先级 R7>R5>R1>R4>R3>R2>R8>R6 与键形词表）
//   - ui.md §6.4（StageId 七阶段冻结序与 token 词形）、§6.5
//     （StageReadinessSnapshot/DomainReadinessItem 投影形状）
//   - evidence §8.1（InvalidationReason 词表值——原因透传零加工）
//
// 用法：node make_wf_gate_advice_fixture.mjs
//   读 ../inputs/gate-advice-fixtures.json → 写 ../expected/gate-advice-
//   expected.json，并打印各文件 SHA-256/sizeBytes（供 manifest integrity
//   登记复制——生成器本身不改写 manifest，登记面人工复核后落笔）。
//
// 确定性：纯函数计算＋固定键序输出（NFR-COR-02 同型）——同 inputs 必得
// 同 expected 字节。
// =====================================================================

import { readFileSync, writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const inputsPath = join(here, "..", "inputs", "gate-advice-fixtures.json");
const expectedPath = join(here, "..", "expected", "gate-advice-expected.json");

// ---------------------------------------------------------------------
// 冻结词表（§4.2/ui §6.4/§6.3 逐字——生成器侧唯一登记处）
// ---------------------------------------------------------------------

// 七阶段冻结序（ui §6.4——token 词形）。
const STAGES = ["modeling", "requirements", "kinematics", "trajectory-dynamics",
  "selection", "optimization", "reporting"];
const stageIndex = (t) => STAGES.indexOf(t);

// 域注册键 → 所属阶段（§4.2 表八行——多对一，聚合阶段两域）。
const DOMAIN_STAGE = {
  "modeling": "modeling",
  "requirements": "requirements",
  "kinematics": "kinematics",
  "trajectory": "trajectory-dynamics",
  "dynamics": "trajectory-dynamics",
  "selection": "selection",
  "optimization": "optimization",
  "reporting": "reporting",
};

// 阶段的聚合域键清单（§4.2 表"聚合域投影"列——按表列序）。
const STAGE_DOMAINS = {
  "modeling": ["modeling"],
  "requirements": ["requirements"],
  "kinematics": ["kinematics"],
  "trajectory-dynamics": ["trajectory", "dynamics"],
  "selection": ["selection"],
  "optimization": ["optimization"],
  "reporting": ["reporting"],
};

// 门控状态词形（ui §6.4 StageViewStatus token——workflow evaluate 只产五值，
// view-only 不产）。
const STATUS = { completed: "completed", inProgress: "in-progress",
  blocked: "blocked", unavailable: "unavailable", notStarted: "not-started" };

// 文案键形（workflow.md §6.3/ui §3.5——键半区唯一词形）。
const keyGoal = (s) => `stage.${s}.goal`;
const keyBlocked = (s) => `stage.${s}.gate.blocked`;
const keyUnlock = (s) => `stage.${s}.gate.unlock`;
const keyNotReached = (s) => `stage.${s}.gate.not-reached`;
const keyDomainUnavailable = (s) => `stage.${s}.gate.domain-unavailable`;
const keyMissingDomain = (s, d) => `stage.${s}.gate.missing-domain.${d}`;
const keyStaleMark = (s) => `stage.${s}.gate.results-stale`;
const keyAdviceTitle = (a) => `advice.${a}.title`;

// 建议八规则动作 token（§6.3 actionKey 冻结词形）与触发谓词求值所需事实。
const ACTION = { r1: "fix-inputs", r2: "run-evaluation", r3: "review-active-task",
  r4: "recompute-downstream", r5: "resolve-findings", r6: "advance-stage",
  r7: "recover-session", r8: "readonly-notice" };

// ---------------------------------------------------------------------
// 事件窗口重放（§5.2）——推导七阶段"结果事实"
// hasResult[s]＝本阶段聚合域出现过归档；staleHints[s]＝失效提示映射
// （键＝`${stageToken}|${domainKey}`——同键覆盖＝最新原因；归档清空失效）。
// 域键经 §4.2 映射导航；词表外域键属输入缺陷（生成器 fail-fast）。
// ---------------------------------------------------------------------
function replayEvents(events) {
  const hasResult = new Array(7).fill(false);
  const stale = new Map(); // key -> {stage, domainKey, reasons[]}
  for (const ev of events) {
    const touched = [...new Set(ev.domainKeys.map((d) => {
      const s = DOMAIN_STAGE[d];
      if (s === undefined) throw new Error(`词表外域键: ${d}`);
      return stageIndex(s);
    }))];
    for (const idx of touched) {
      if (ev.kind === "result-archived") {
        hasResult[idx] = true;
        // 归档清空本阶段全部失效提示（新结果覆盖旧失效——重算完成即恢复）。
        for (const k of [...stale.keys()]) {
          if (stale.get(k).stage === STAGES[idx]) stale.delete(k);
        }
      } else if (ev.kind === "dependency-invalidated") {
        if (!hasResult[idx]) continue; // 无结果阶段无可失效（§5.2）
        for (const d of ev.domainKeys) {
          if (stageIndex(DOMAIN_STAGE[d]) !== idx) continue;
          stale.set(`${STAGES[idx]}|${d}`, {
            stage: STAGES[idx], domainKey: d,
            reasons: ev.reasonTokens.slice(),
            actionKey: ACTION.r4,
          });
        }
      } // 其余事件类别对门控无可见效果（§5.2 处置表）
    }
  }
  return { hasResult, stale };
}

// stale 提示的确定性输出序＝(阶段序, 域键字典序)。
function sortedStale(staleMap, stageToken) {
  return [...staleMap.values()]
    .filter((h) => stageToken === undefined || h.stage === stageToken)
    .sort((a, b) => stageIndex(a.stage) - stageIndex(b.stage)
      || (a.domainKey < b.domainKey ? -1 : a.domainKey > b.domainKey ? 1 : 0));
}

// ---------------------------------------------------------------------
// 七阶段门控判定（§4.4 状态机的纯函数重放——五规则序贯，逐阶段推进）
// ---------------------------------------------------------------------
function evaluateCase(caseData) {
  // 快照级 epoch 注入（§6.5：快照携带构建时会话纪元）——夹具输入以算例级
  // epoch 为各快照同拍汇聚的纪元（同拍汇聚＝同纪元；快照级显式值优先，
  // 供未来多纪元板算例扩展）。
  const snaps = caseData.snapshots.map((s) => ({ ...s, epoch: s.epoch ?? caseData.epoch }));
  const snapByStage = new Map(snaps.map((s) => [s.stage, s]));
  // 输入自检：七板序与冻结序一致（组板契约）。
  snaps.forEach((s, i) => {
    if (s.stage !== STAGES[i]) throw new Error(`快照板序位 ${i} 与冻结序不符`);
  });
  // I-WF-4：任一快照 epoch 低于水位 → 拒绝判定（调用方错误 fail-fast）。
  for (const s of snaps) {
    if (s.epoch < caseData.lastEpoch) return { throws: true };
  }
  const epoch = Math.max(...snaps.map((s) => s.epoch));
  const { hasResult, stale } = replayEvents(caseData.events);

  const decisions = [];
  for (let i = 0; i < 7; ++i) {
    const stage = STAGES[i];
    const snap = snapByStage.get(stage);
    const decision = { stage, status: null, unlocked: false,
      reasonKeys: [], unlockHintKey: null, missingItemKeys: [],
      staleHints: [], epoch };

    // 规则 1：前序链（I-WF-1）——序位最小的 Blocked 前序 → NotStarted 锁定。
    const blockedPrefix = decisions.find((d, j) => j < i && d.status === STATUS.blocked);
    if (blockedPrefix) {
      decision.status = STATUS.notStarted;
      decision.reasonKeys = [keyNotReached(blockedPrefix.stage)];
      decision.unlockHintKey = keyUnlock(blockedPrefix.stage);
      decisions.push(decision);
      continue;
    }

    // 规则 2：域未装配 → Unavailable（聚合词表任一域无投影条目）。
    const present = new Set(snap.domains.map((d) => d.domainKey));
    const missingDomains = STAGE_DOMAINS[stage].filter((d) => !present.has(d));
    if (missingDomains.length > 0) {
      decision.status = STATUS.unavailable;
      decision.reasonKeys = [keyDomainUnavailable(stage),
        ...missingDomains.map((d) => keyMissingDomain(stage, d))];
      decision.unlockHintKey = keyUnlock(stage);
      decisions.push(decision);
      continue;
    }

    // 规则 3：缺项锁定 → Blocked（缺项完全采信域自报，按聚合域序拼接）。
    const byKey = new Map(snap.domains.map((d) => [d.domainKey, d]));
    let anyIncomplete = false;
    let hasActiveTask = false;
    const missingItems = [];
    for (const d of STAGE_DOMAINS[stage]) {
      const item = byKey.get(d);
      if (!item.inputComplete) anyIncomplete = true;
      missingItems.push(...item.missingItemKeys);
      if (item.hasActiveTask) hasActiveTask = true;
    }
    if (anyIncomplete || missingItems.length > 0) {
      decision.status = STATUS.blocked;
      decision.missingItemKeys = missingItems;
      decision.reasonKeys = [keyBlocked(stage)];
      decision.unlockHintKey = keyUnlock(stage);
      // 缺项与结果过期并存（§4.4 输入回退转移——两个独立事实同时呈现）。
      decision.staleHints = sortedStale(stale, stage);
      decisions.push(decision);
      continue;
    }

    // 规则 4：计算中 → InProgress（入口可进入）。
    if (hasActiveTask) {
      decision.status = STATUS.inProgress;
      decision.unlocked = true;
      decisions.push(decision);
      continue;
    }

    // 规则 5：结果事实（无结果 → NotStarted 解锁；有结果 → Completed，
    // 失效提示并存携带——不自动重算）。
    decision.unlocked = true;
    if (hasResult[i]) {
      decision.status = STATUS.completed;
      decision.staleHints = sortedStale(stale, stage);
    } else {
      decision.status = STATUS.notStarted;
    }
    decisions.push(decision);
  }
  return { throws: false, epoch, decisions };
}

// ---------------------------------------------------------------------
// 建议引擎（§6.3 八规则——优先级序装配，产出全部命中规则）
// ---------------------------------------------------------------------
function adviseFor(decisions, snap, adviceSpec) {
  const idx = stageIndex(adviceSpec.stage);
  const decision = decisions[idx];
  const out = {
    id: adviceSpec.id,
    goalKey: keyGoal(adviceSpec.stage),
    missingItemKeys: decision.missingItemKeys.slice(),
    problemKeys: [],
    steps: [],
  };
  // 要素③：门控原因键＋results-stale 标记键＋阻塞诊断键（去重保序）。
  const pushUnique = (arr, keys) => {
    for (const k of keys) if (!arr.includes(k)) arr.push(k);
  };
  pushUnique(out.problemKeys, decision.reasonKeys);
  if (decision.staleHints.length > 0) out.problemKeys.push(keyStaleMark(adviceSpec.stage));
  pushUnique(out.problemKeys, adviceSpec.blockingDiagnosticKeys);

  const step = (actionKey, targetStage, targetObjectId, detailKeys) => ({
    actionKey, targetStage, targetObjectId,
    titleKey: keyAdviceTitle(actionKey), detailKeys,
  });

  // R7 处理恢复项（最高优先）。
  if (adviceSpec.recoveryBannerKeys.length > 0) {
    out.steps.push(step(ACTION.r7, adviceSpec.stage, "", adviceSpec.recoveryBannerKeys.slice()));
  }
  // R5 处理阻塞诊断。
  if (adviceSpec.blockingDiagnosticKeys.length > 0) {
    out.steps.push(step(ACTION.r5, adviceSpec.stage, "", adviceSpec.blockingDiagnosticKeys.slice()));
  }
  // R1 补全输入（Blocked 且缺项非空）。
  if (decision.status === STATUS.blocked && decision.missingItemKeys.length > 0) {
    out.steps.push(step(ACTION.r1, adviceSpec.stage, "", decision.missingItemKeys.slice()));
  }
  // R4 复算下游（每条失效提示一条；对象定位＝受影响域键）。
  for (const h of decision.staleHints) {
    out.steps.push(step(ACTION.r4, h.stage, h.domainKey, [keyStaleMark(adviceSpec.stage)]));
  }
  // R3 等待/查看任务（投影事实——不以门控状态为准）。
  const hasActiveTask = snap.domains.some((d) => d.hasActiveTask);
  if (hasActiveTask) out.steps.push(step(ACTION.r3, adviceSpec.stage, "", []));
  // R2 发起计算（输入齐备＋无活动任务＋结果缺失）。
  const inputsComplete = snap.domains.every((d) => d.inputComplete);
  if (inputsComplete && decision.missingItemKeys.length === 0 && !hasActiveTask
    && !adviceSpec.hasArchivedResult) {
    out.steps.push(step(ACTION.r2, adviceSpec.stage, "", []));
  }
  // R8 只读提示（详情＝解锁条件键——无则空）。
  if (adviceSpec.sessionReadOnly) {
    out.steps.push(step(ACTION.r8, adviceSpec.stage, "",
      decision.unlockHintKey === null ? [] : [decision.unlockHintKey]));
  }
  // R6 前进下一阶段（Completed 且下一阶段 NotStarted；末阶段不触发）。
  if (decision.status === STATUS.completed && idx + 1 < decisions.length
    && decisions[idx + 1].status === STATUS.notStarted) {
    out.steps.push(step(ACTION.r6, STAGES[idx + 1], "", []));
  }
  return out;
}

// ---------------------------------------------------------------------
// 级联失效映射（§4.3 D-WF-2）——受影响域→提示＋下游 Completed 逐域级联
// 输出序＝(阶段序, 域键字典序)。
// ---------------------------------------------------------------------
function mapInvalidation(decisions, inv, reasons) {
  const hints = new Map(); // `${stageIdx}|${domainKey}` -> hint
  for (const d of inv.signalDomainKeys) {
    const s = DOMAIN_STAGE[d];
    if (s === undefined) throw new Error(`词表外域键: ${d}`);
    hints.set(`${stageIndex(s)}|${d}`, { stage: s, domainKey: d,
      reasons: reasons.slice(), actionKey: ACTION.r4 });
  }
  const affected = [...new Set([...hints.keys()].map((k) => Number(k.split("|")[0])))];
  for (const a of affected) {
    for (let t = a + 1; t < 7; ++t) {
      if (decisions[t].status !== STATUS.completed) continue;
      for (const d of STAGE_DOMAINS[STAGES[t]]) {
        hints.set(`${t}|${d}`, { stage: STAGES[t], domainKey: d,
          reasons: reasons.slice(), actionKey: ACTION.r4 });
      }
    }
  }
  return [...hints.values()].sort((x, y) =>
    stageIndex(x.stage) - stageIndex(y.stage)
    || (x.domainKey < y.domainKey ? -1 : x.domainKey > y.domainKey ? 1 : 0));
}

// ---------------------------------------------------------------------
// 主流程：逐算例计算期望 → 组装 expected → 打印完整性摘要
// ---------------------------------------------------------------------
const inputs = JSON.parse(readFileSync(inputsPath, "utf8"));
// 快照级 epoch 注入（与 evaluateCase 同规则——建议引擎读同一注入后快照）。
const snapsOf = (c) => c.snapshots.map((s) => ({ ...s, epoch: s.epoch ?? c.epoch }));
const cases = inputs.cases.map((c) => {
  const gating = evaluateCase(c);
  const outCase = { id: c.id, gating };
  const decisions = gating.throws ? [] : gating.decisions;
  outCase.advice = (c.advice ?? []).map((spec) =>
    adviseFor(decisions, snapsOf(c).find((s) => s.stage === spec.stage), spec));
  if (c.invalidation !== null && c.invalidation !== undefined && !gating.throws) {
    // 原因词表校验（词表外 token 即夹具缺陷 fail-fast）——expected 只承载
    // token 词形数组（消费侧经同一词表重组结构化原因，键形解释单点）。
    for (const v of c.invalidation.reasonTokens) {
      const hit = inputs.reasonTokenVocabulary.values.find((r) => r.value === v);
      if (hit === undefined) throw new Error(`原因词表外 token: ${v}`);
    }
    // 快照 epoch 对齐（I-WF-4）：失效映射要求快照不低于门控 epoch——取同板。
    outCase.invalidation = {
      hints: mapInvalidation(decisions, c.invalidation,
        c.invalidation.reasonTokens.slice()),
    };
  } else {
    outCase.invalidation = null;
  }
  outCase.replayDeterministic = true; // 纯函数面双跑一致（消费测试逐算例复核）
  return outCase;
});

const expected = {
  schema: "wf-gate-advice-expected/1",
  note: "workflow 门控/建议/级联映射黄金期望——由本生成器按 units/workflow.md §4.3/§4.4/§5.2/§6.3 冻结语义独立重实现产出（与产品 C++ 双实现互证）。判定面全部为布尔/枚举/键串事实：status 词形＝ui §6.4 token；键形＝workflow §6.3 词表；staleHints/级联提示输出序＝(阶段序, 域键字典序)；原因词条按 evidence 词表值透传（value/key 词形）。throws=true＝期望 WorkflowError 拒绝（I-WF-4 旧投影水位违约——消费测试断言异常轨）。",
  cases,
};
writeFileSync(expectedPath, JSON.stringify(expected, null, 2) + "\n", "utf8");

// 完整性摘要（manifest integrity 登记用——人工复核后落笔 manifest）。
const sha = (p) => {
  const buf = readFileSync(p);
  return { path: p, sha256: createHash("sha256").update(buf).digest("hex"),
    sizeBytes: buf.byteLength };
};
for (const rel of ["inputs/gate-advice-fixtures.json", "expected/gate-advice-expected.json",
  "generate/make_wf_gate_advice_fixture.mjs"]) {
  const s = sha(join(here, "..", rel));
  console.log(`${rel}\n  sha256=${s.sha256}\n  sizeBytes=${s.sizeBytes}`);
}
console.log(`cases=${cases.length} expected written.`);
