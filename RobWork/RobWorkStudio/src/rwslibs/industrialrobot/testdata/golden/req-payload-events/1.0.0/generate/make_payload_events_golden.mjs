// =====================================================================
// make_payload_events_golden.mjs —— req-payload-events 数据集生成脚本
// （contract-fixture；units/requirements.md §10.1 数据集 4/4、§4.5 工况
// 负载/事件/适用范围、core SourcedValue 四态、§4.7 非法组合）。
//
// 用法：
//   node make_payload_events_golden.mjs   （脚本位于 <版本目录>/generate/ 下）
//
// 生成逻辑（按设计文档规则**独立转写**——与产品 C++ 断言互证）：
//   1. 读 inputs/payload-events.json；
//   2. 负载四态投影：逐字段 state token（provided/not-provided/
//      not-applicable/invalid）＋值字面（invalid 保留原串）——
//      "缺失≠0、非法不清洗"（NFR-COR-3）的黄金表达；
//   3. 事件表投影：type token＋绑定点 id＋durationS 字面（0.0 零值
//      字面保真——与缺失 null 严格区分）；
//   4. 负样例表转写（case→首违例码或登记面效果）；
//   5. 写 expected/payload-events-expected.json，打印 integrity 申报表。
// =====================================================================

import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const inputPath = join(here, "..", "inputs", "payload-events.json");
const outputPath = join(here, "..", "expected", "payload-events-expected.json");

const model = JSON.parse(readFileSync(inputPath, "utf8"));

// ---------- 负载四态投影（state token＋字面——与产品四态词表同源转写） ----------

const stateToken = {
  "provided": "provided",
  "not-provided": "not-provided",
  "not-applicable": "not-applicable",
  "invalid": "invalid",
};

function projectSourced(sv) {
  const projection = { state: stateToken[sv.state] };
  if (sv.state === "provided") { projection.value = sv.value; }
  if (sv.state === "invalid") { projection.raw = sv.raw; }
  return projection;
}

const payloadProjections = [];
for (const c of model.conditions) {
  c.payloads.forEach((pl, i) => {
    payloadProjections.push({
      condition: c.name,
      payloadIndex: i,
      toolRef: { objectId: pl.toolRef.objectId, tcpKey: pl.toolRef.tcpKey },
      mass: projectSourced(pl.mass),
      com: projectSourced(pl.com),
      inertia: projectSourced(pl.inertia),
    });
  });
}

// ---------- 事件表投影（绑定点名投影＋duration 字面/null 严格区分） ----------

const eventTable = [];
for (const c of model.conditions) {
  c.events.forEach((ev, i) => {
    eventTable.push({
      condition: c.name,
      seq: i + 1,
      type: ev.type,
      stationRef: ev.stationRef,
      durationS: ev.durationS,
      hasDuration: ev.durationS !== null,
    });
  });
}

// ---------- appliesTo 投影（D-REQ-5：绑定方向唯一——工况侧声明） ----------

const appliesToTable = model.conditions.map((c) => ({
  condition: c.name,
  scope: c.appliesTo.scope,
  stations: c.appliesTo.stations,
  stationCount: c.appliesTo.stations.length,
}));

// ---------- 负样例表转写 ----------

const negativeExpectations = model.negativeCases.map((n) => ({
  case: n.case,
  expectedErrorCode: n.expectedErrorCode,
  expectedEffect: n.expectedEffect ?? "construction-rejected",
  note: n.note,
}));

// ---------- 期望文件 ----------

const expected = {
  schema: "req-payload-events-expected/1",
  generatedBy: "generate/make_payload_events_golden.mjs",
  note: "负载事件黄金期望（contract-fixture——SourcedValue 四态全谱/事件绑定/适用范围独立转写；零值/近零/正负抵消样例置位）",
  payloadProjections,
  eventTable,
  appliesToTable,
  negativeExpectations,
  edgeCases: {
    zeroValue: "PE-full.payload[1].mass=0.0（零值 provided 合法——空载非缺失）与 Dwell durationS=0.0（零持续字面——与 null 严格区分）",
    nearZero: "PE-full.payload[0].com.z=1e-18（近零样本——IEEE754 位模式保真）",
    signCancellation: "PE-full.payload[0].com x+y=0（正负抵消——分量级保真）",
    invalidRaw: "PE-partial.payload[0].inertia invalid 保留原串 \"12,5\"（NFR-COR-3 不清洗）",
  },
};

mkdirSync(dirname(outputPath), { recursive: true });
writeFileSync(outputPath, JSON.stringify(expected, null, 2) + "\n", "utf8");

// ---------- manifest integrity 申报表 ----------

const files = [
  "inputs/payload-events.json",
  "expected/payload-events-expected.json",
  "generate/make_payload_events_golden.mjs",
];
console.log("integrity 申报表（path / sizeBytes / sha256）：");
for (const rel of files) {
  const abs = join(here, "..", rel);
  const bytes = readFileSync(abs);
  const sha = createHash("sha256").update(bytes).digest("hex");
  console.log(`${rel} ${bytes.length} ${sha}`);
}
console.log("expected 写出: " + outputPath);
