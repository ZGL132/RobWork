// WP-20/REUSE-T06 独立验收：final-mode-statistics.json 全量独立对账脚本
// 独立重算 functions/catalog/inventory 三层统计并与送验统计快照逐项比对；零依赖，仅读权威原件。
import fs from 'node:fs';

const ROOT = 'RobWork/doc/industrial-robot-design/reuse';
const claimed = JSON.parse(fs.readFileSync('RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T06/final-mode-statistics.json', 'utf8'));

const results = [];
const fail = (id, msg) => { results.push({ id, pass: false, msg }); };
const ok = (id, msg) => { results.push({ id, pass: true, msg }); };

// ---------- 1. functions 矩阵逐单元重算 ----------
const unitFiles = fs.readdirSync(`${ROOT}/functions`).filter(f => f.endsWith('.json'));
const units = {};
for (const f of unitFiles) {
  const j = JSON.parse(fs.readFileSync(`${ROOT}/functions/${f}`, 'utf8'));
  const rows = j.functions;
  const byStatus = {}, byDecision = {}, cross = {};
  for (const r of rows) {
    byStatus[r.status] = (byStatus[r.status] || 0) + 1;
    byDecision[r.decision] = (byDecision[r.decision] || 0) + 1;
    const k = `${r.decision}/${r.status}`;
    cross[k] = (cross[k] || 0) + 1;
  }
  units[j.unit] = { total: rows.length, byStatus, byDecision, cross };
}

let grandTotal = 0, sumStatus = {}, sumDecision = {}, sumCross = {};
for (const [u, s] of Object.entries(units)) {
  grandTotal += s.total;
  for (const [k, v] of Object.entries(s.byStatus)) sumStatus[k] = (sumStatus[k] || 0) + v;
  for (const [k, v] of Object.entries(s.byDecision)) sumDecision[k] = (sumDecision[k] || 0) + v;
  for (const [k, v] of Object.entries(s.cross)) sumCross[k] = (sumCross[k] || 0) + v;
}

// 1a. 单元清单与逐单元 total/byStatus/byDecision 比对
const claimedUnits = Object.keys(claimed.functions.byUnit);
const actualUnits = Object.keys(units).sort();
const claimedUnitsSorted = [...claimedUnits].sort();
if (JSON.stringify(claimedUnitsSorted) === JSON.stringify(actualUnits)) {
  ok('F-units-list', `functions 单元清单一致：${actualUnits.length} 个（${actualUnits.join(',')}）`);
} else {
  fail('F-units-list', `单元清单不一致：claimed=${claimedUnitsSorted.join(',')} actual=${actualUnits.join(',')}`);
}

let unitMismatches = [];
for (const u of actualUnits) {
  const c = claimed.functions.byUnit[u];
  const a = units[u];
  if (!c) { unitMismatches.push(`${u}: claimed 缺失`); continue; }
  if (c.total !== a.total) unitMismatches.push(`${u}: total claimed=${c.total} actual=${a.total}`);
  if (JSON.stringify(c.byStatus) !== JSON.stringify(a.byStatus)) unitMismatches.push(`${u}: byStatus claimed=${JSON.stringify(c.byStatus)} actual=${JSON.stringify(a.byStatus)}`);
  if (JSON.stringify(c.byDecision) !== JSON.stringify(a.byDecision)) unitMismatches.push(`${u}: byDecision claimed=${JSON.stringify(c.byDecision)} actual=${JSON.stringify(a.byDecision)}`);
}
if (unitMismatches.length === 0) {
  ok('F-per-unit', `全部 ${actualUnits.length} 单元 total/byStatus/byDecision 独立重算与快照逐项一致`);
} else {
  fail('F-per-unit', `单元级不一致：${unitMismatches.join('；')}`);
}

// 1b. grandTotal 与 status/decision 汇总
grandTotal === claimed.functions.grandTotal
  ? ok('F-grandTotal', `grandTotal=${grandTotal} 一致`)
  : fail('F-grandTotal', `grandTotal claimed=${claimed.functions.grandTotal} actual=${grandTotal}`);
JSON.stringify(claimed.functions.byStatus || {}) === '{}' // 快照未存全局 byStatus，跳过
JSON.stringify(sumDecision) === JSON.stringify(claimed.functions.decisionStatusCross ? null : null); // placeholder

// 快照只存 decisionStatusCross；此处比对交叉表
const claimedCross = claimed.functions.decisionStatusCross;
const crossMismatches = [];
const allKeys = new Set([...Object.keys(claimedCross), ...Object.keys(sumCross)]);
for (const k of allKeys) {
  if ((claimedCross[k] || 0) !== (sumCross[k] || 0)) crossMismatches.push(`${k}: claimed=${claimedCross[k] || 0} actual=${sumCross[k] || 0}`);
}
if (crossMismatches.length === 0) {
  ok('F-cross', `decision×status 交叉表 7 键独立重算一致：${JSON.stringify(sumCross)}`);
} else {
  fail('F-cross', `交叉表不一致：${crossMismatches.join('；')}`);
}

// 1c. 契约 outputs 声明的派生数字复核
const derived = {
  migrated: (sumCross['business-specific/migrated'] || 0) + (sumCross['adapter/migrated'] || 0) + (sumCross['direct/migrated'] || 0),
  businessMigrated: sumCross['business-specific/migrated'] || 0,
  adapterMigrated: sumCross['adapter/migrated'] || 0,
  directMigrated: sumCross['direct/migrated'] || 0,
  closed: sumCross['adapter/closed'] || 0,
  audited: sumCross['business-specific/audited'] || 0,
  proposal: sumCross['proposal/proposal'] || 0,
  pending: sumCross['pending/pending'] || 0
};
const dChecks = [
  [`migrated=${derived.migrated}（=1630）`, derived.migrated === 1630],
  [`business-specific/migrated=${derived.businessMigrated}（=1594）`, derived.businessMigrated === 1594],
  [`adapter/migrated=${derived.adapterMigrated}（=30）`, derived.adapterMigrated === 30],
  [`direct/migrated=${derived.directMigrated}（=6）`, derived.directMigrated === 6],
  [`closed=${derived.closed}（=2）`, derived.closed === 2],
  [`audited=${derived.audited}（=3）`, derived.audited === 3],
  [`proposal=${derived.proposal}（=2）`, derived.proposal === 2],
  [`pending=${derived.pending}（=594）`, derived.pending === 594],
  [`1630+2+3+2+594=${1630 + 2 + 3 + 2 + 594}（=grandTotal）`, 1630 + 2 + 3 + 2 + 594 === grandTotal]
];
const dBad = dChecks.filter(([, p]) => !p).map(([m]) => m);
if (dBad.length === 0) ok('F-derived', `outputs 派生数字全部复核通过：${dChecks.map(([m]) => m).join('，')}`);
else fail('F-derived', `派生数字不符：${dBad.join('；')}`);

// 1d. 抽样对账（sampling）：抽 3 单元逐行人工核对样本
const samples = [];
const krow = JSON.parse(fs.readFileSync(`${ROOT}/functions/kinematics.json`, 'utf8')).functions.find(r => r.functionId === 'kinematics.RigView');
samples.push([`kinematics.RigView status=${krow?.status} decision=${krow?.decision} capability=${krow?.capabilityId}（组A测试替身保留）`, krow?.status === 'pending' && krow?.decision === 'pending' && krow?.capabilityId === 'KIN-FK']);
const drow = JSON.parse(fs.readFileSync(`${ROOT}/functions/dynamics.json`, 'utf8')).functions.find(r => /rnea/i.test(r.functionId) && r.status === 'migrated');
samples.push([`dynamics RNEA 迁移行存在且 status=migrated（${drow?.functionId}）`, !!drow]);
const frows = JSON.parse(fs.readFileSync(`${ROOT}/functions/foundation-governance.json`, 'utf8')).functions;
samples.push([`foundation-governance 全 ${frows.length} 行中 pending=${frows.filter(r => r.status === 'pending').length}（=123 组A保留）`, frows.filter(r => r.status === 'pending').length === 123]);
const sBad = samples.filter(([, p]) => !p).map(([m]) => m);
if (sBad.length === 0) ok('F-sample', `抽样行核对 ${samples.length}/3 通过：${samples.map(([m]) => m).join('；')}`);
else fail('F-sample', `抽样不符：${sBad.join('；')}`);

// ---------- 2. catalog 对账 ----------
const cat = JSON.parse(fs.readFileSync(`${ROOT}/catalog.json`, 'utf8'));
const caps = cat.capabilities || cat.catalog || cat; // 兼容 schema
const capList = Array.isArray(caps) ? caps : (caps.capabilities || []);
const byReview = {};
for (const c of capList) byReview[c.review?.status || c.reviewStatus || c.status] = (byReview[c.review?.status || c.reviewStatus || c.status] || 0) + 1;
const verifiedIds = capList.filter(c => (c.review?.status || c.reviewStatus || c.status) === 'verified').map(c => c.id || c.capabilityId).sort();
const candidateIds = capList.filter(c => (c.review?.status || c.reviewStatus || c.status) === 'candidate').map(c => c.id || c.capabilityId).sort();
const cClaims = claimed.catalog;
const catOk = capList.length === cClaims.total && (byReview['verified'] || 0) === cClaims.byReviewStatus.verified && (byReview['candidate'] || 0) === cClaims.byReviewStatus.candidate;
const vOk = JSON.stringify(verifiedIds) === JSON.stringify([...cClaims.verified].sort());
const cdOk = JSON.stringify(candidateIds) === JSON.stringify([...cClaims.candidate].sort());
if (catOk && vOk && cdOk) ok('C-catalog', `catalog 独立重算一致：total=${capList.length} verified=${byReview['verified']} candidate=${byReview['candidate']}，verified/candidate 清单逐项吻合`);
else fail('C-catalog', `catalog 不一致：total=${capList.length}/${cClaims.total} byReview=${JSON.stringify(byReview)} verifiedListMatch=${vOk} candidateListMatch=${cdOk}`);

// ---------- 3. inventory 对账 ----------
const inv = JSON.parse(fs.readFileSync(`${ROOT}/inventory.json`, 'utf8'));
const rowList = inv.files;
const byCls = {};
for (const r of rowList) byCls[r.classification || r.class] = (byCls[r.classification || r.class] || 0) + 1;
const iOk = rowList.length === claimed.inventory.total
  && (byCls['audit-required'] || 0) === claimed.inventory.byClassification['audit-required']
  && (byCls['migration-debt'] || 0) === claimed.inventory.byClassification['migration-debt']
  && (byCls['existing-adapter'] || 0) === claimed.inventory.byClassification['existing-adapter'];
if (iOk) ok('I-inventory', `inventory 独立重算一致：total=${rowList.length}，classification 分布=${JSON.stringify(byCls)}`);
else fail('I-inventory', `inventory 不一致：total=${rowList.length}/${claimed.inventory.total} byCls=${JSON.stringify(byCls)}`);

// ---------- 输出 ----------
let failed = 0;
for (const r of results) {
  console.log(`[${r.pass ? 'PASS' : 'FAIL'}] ${r.id}: ${r.msg}`);
  if (!r.pass) failed++;
}
console.log(`SUMMARY: ${results.length - failed}/${results.length} PASS, ${failed} FAIL`);
process.exit(failed === 0 ? 0 : 1);
