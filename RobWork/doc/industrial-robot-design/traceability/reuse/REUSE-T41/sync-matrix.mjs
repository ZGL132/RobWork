// REUSE-T41 矩阵同步脚本（本批产物——重跑可复现同一结果）。
// 职责：按治理 Digest 口径（BOM 剥离、CRLF→LF、单一尾换行——validate-reuse.ps1
// Digest 同款）重算本包五个产品改动文件摘要，执行：
//   1) 全矩阵 inputs/auditEvidence 中上述文件摘要重钉（含 permissions.json 绑定）；
//   2) ui.audit-b32.eventBus.productionWiring 转 migrated（区段重定位）；
//   3) ui.CMake.ird_add_gtest 按 REUSE-T32 定案翻 business-specific/migrated；
//   4) 新增三行整文件行覆盖载体（ui.CMake.targetWiring / ui.IrdWorkbenchHostPlugin /
//      execution.EventBusContractHeader——SnapshotViewAdapter/KinSessionModelView 先例形态）；
//   5) foundation-governance L10 区段重定位（白名单尾部段扩展）。
// JSON 回写保持各文件原换行风格（foundation-governance/permissions 为 CRLF）。
import fs from 'node:fs';
import crypto from 'node:crypto';

const ROOT = 'D:/10_Source_Repos/21_robot/RobWork';
const P = {
  uiPluginCpp: 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp',
  uiPluginHpp: 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.hpp',
  uiCMake: 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/CMakeLists.txt',
  eventBusHpp: 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/include/sdurws/ird/execution/EventBus.hpp',
  whitelist: 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/cmake/ird_gates_whitelist.cmake',
  readme: 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T41/README.md',
  verification: 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T41/verification.json',
  gateReg: 'RobWork/doc/industrial-robot-design/traceability/wp18-t41-gate-registrations.md',
};
const FN = 'RobWork/doc/industrial-robot-design/reuse/functions/';

function digest(text) {
  const t = text.replace(/^\uFEFF/, '').replace(/\r\n/g, '\n').replace(/\n+$/, '');
  return crypto.createHash('sha256').update(Buffer.from(t + '\n', 'utf8')).digest('hex');
}
function fileDigest(rel) { return digest(fs.readFileSync(ROOT + '/' + rel, 'utf8')); }
function regionDigest(rel, start, end) {
  const lines = fs.readFileSync(ROOT + '/' + rel, 'utf8').replace(/^\uFEFF/, '').replace(/\r\n/g, '\n').split('\n');
  return digest(lines.slice(start - 1, end).join('\n'));
}
function lineOf(rel, needle) {
  const lines = fs.readFileSync(ROOT + '/' + rel, 'utf8').replace(/^\uFEFF/, '').replace(/\r\n/g, '\n').split('\n');
  const i = lines.findIndex((l) => l.includes(needle));
  if (i < 0) throw new Error('marker not found in ' + rel + ': ' + needle);
  return i + 1;
}
function loadJson(rel) {
  const raw = fs.readFileSync(ROOT + '/' + rel, 'utf8');
  return { j: JSON.parse(raw), crlf: raw.includes('\r\n') };
}
function saveJson(rel, obj, crlf) {
  let out = JSON.stringify(obj, null, 2) + '\n';
  if (crlf) out = out.replace(/\n/g, '\r\n');
  fs.writeFileSync(ROOT + '/' + rel, out);
}

const changedDigests = Object.fromEntries(
  [P.uiPluginCpp, P.uiPluginHpp, P.uiCMake, P.eventBusHpp, P.whitelist].map((p) => [p, fileDigest(p)]));
const readmeDigest = fileDigest(P.readme);

// ---- 1) 全矩阵摘要重钉 ----
const repin = {};
for (const name of fs.readdirSync(ROOT + '/' + FN)) {
  if (!name.endsWith('.json')) continue;
  const rel = FN + name;
  const { j, crlf } = loadJson(rel);
  let n = 0;
  for (const f of j.functions) {
    for (const arr of ['inputs', 'auditEvidence']) {
      for (const b of f[arr] || []) {
        if (b.path in changedDigests && b.sha256 !== changedDigests[b.path]) { b.sha256 = changedDigests[b.path]; n++; }
      }
    }
  }
  if (n) { saveJson(rel, j, crlf); repin[name] = n; }
}
{ // permissions.json（grants[].source/build 等绑定）
  const rel = 'RobWork/doc/industrial-robot-design/reuse/permissions.json';
  const { j, crlf } = loadJson(rel);
  let n = 0;
  const visit = (o) => {
    if (Array.isArray(o)) { o.forEach(visit); return; }
    if (o && typeof o === 'object') {
      if (typeof o.path === 'string' && o.path in changedDigests && typeof o.sha256 === 'string' && o.sha256 !== changedDigests[o.path]) { o.sha256 = changedDigests[o.path]; n++; }
      Object.values(o).forEach(visit);
    }
  };
  visit(j);
  if (n) { saveJson(rel, j, crlf); repin['permissions.json'] = n; }
}

// ---- 2) productionWiring 行迁移态 ----
{
  const rel = FN + 'ui.json';
  const { j, crlf } = loadJson(rel);
  const row = j.functions.find((f) => f.functionId === 'ui.audit-b32.eventBus.productionWiring');
  if (!row) throw new Error('productionWiring row missing');
  const busLine = lineOf(P.uiPluginCpp, 'm_eventBus = std::make_shared<execution::DomainEventBusImpl>()');
  const storeLine = lineOf(P.uiPluginCpp, 'auto storeBundle = std::make_shared<BundleCapturingStoreFactory>(');
  const revSinkLine = lineOf(P.uiPluginCpp, 'm_revisionSink = std::make_unique<DomainRevisionEventBridge>(');
  const revSubLine = lineOf(P.uiPluginCpp, 'm_revisionSubscription = m_eventBus->subscribe(*m_revisionSink);');
  const reqSinkLine = lineOf(P.uiPluginCpp, 'UI-T35 P2：需求域修订同步 sink');
  const reqSubEnd = lineOf(P.uiPluginCpp, 'm_requirementsRevisionSubscription =');
  const wiringLine = lineOf(P.uiPluginCpp, 'contentDeps.wiring.eventBus = m_eventBus;');
  row.sources = [
    { path: P.uiPluginCpp, symbol: 'm_eventBus = std::make_shared<execution::DomainEventBusImpl>();',
      qualifiedSymbol: 'sdurws::ird::ui::IrdWorkbenchHostPlugin::initialize',
      startLine: busLine - 10, endLine: storeLine - 1, sha256: regionDigest(P.uiPluginCpp, busLine - 10, storeLine - 1) },
    { path: P.uiPluginCpp, symbol: 'm_revisionSink =',
      qualifiedSymbol: 'sdurws::ird::ui::IrdWorkbenchHostPlugin::initialize',
      startLine: revSinkLine, endLine: revSubLine, sha256: regionDigest(P.uiPluginCpp, revSinkLine, revSubLine) },
    { path: P.uiPluginCpp, symbol: 'class RequirementsRevisionSyncSink',
      qualifiedSymbol: 'sdurws::ird::ui::IrdWorkbenchHostPlugin::initialize',
      startLine: reqSinkLine, endLine: reqSubEnd + 1, sha256: regionDigest(P.uiPluginCpp, reqSinkLine, reqSubEnd + 1) },
    { path: P.uiPluginCpp, symbol: 'contentDeps.wiring.eventBus = m_eventBus;',
      qualifiedSymbol: 'sdurws::ird::ui::IrdWorkbenchHostPlugin::initialize',
      startLine: wiringLine, endLine: wiringLine, sha256: regionDigest(P.uiPluginCpp, wiringLine, wiringLine) },
  ];
  row.capabilityId = 'NONE';
  row.decision = 'business-specific';
  row.status = 'migrated';
  row.search = {
    command: 'batch32/search.json及core-event-lifetime-audit.md；REUSE-T41 生产装配换核（execution::DomainEventBusImpl，units/execution.md §10.4/§13 交接）',
    conclusion: '上游Event原语不采用（同步listener表，无RAII/线程/队列/drain——CORE-EVENT维持candidate）；既有execution总线为生产实现，F-967 线程/退订差异经宿主投递线程转接修正后收口',
  };
  row.differences = 'F967 收口（REUSE-T41）：生产装配由越界的 core::ReferenceEventBus（非生产组件）换 execution::DomainEventBusImpl；回调自投递线程到达——适配器快照经 m_eventAdapterMutex 互斥读取、显式 Qt::QueuedConnection 转 UI 线程，迟到守卫（adapter==source/isClosed/projectId）与『事务发布持写锁不同步回查』语义不变；关停/析构＝句柄退订→waitForIdle→总线 drain join';
  row.auditBoundary = 'REUSE-T41 实施完成待独验：生产装配链四区段实改；GUI 真实路径（IRD_UI_PLUGIN_SMOKE 修订事件双 sink 投递）与受影响目标回归在案；不关闭 T01、不升级 CORE-EVENT、不关闭 F-967（独验归所有者）';
  row.nonOverlapEvidence = { path: P.readme, sha256: readmeDigest };
  row.inputs = row.inputs.map((b) => (b.path in changedDigests ? { ...b, sha256: changedDigests[b.path] } : b));
  saveJson(rel, j, crlf);
}

// ---- 3) ui.CMake.ird_add_gtest 定案翻 T32 同款 ----
{
  const rel = FN + 'ui.json';
  const { j, crlf } = loadJson(rel);
  const row = j.functions.find((f) => f.functionId === 'ui.CMake.ird_add_gtest');
  if (!row) throw new Error('ird_add_gtest row missing');
  row.capabilityId = 'NONE';
  row.decision = 'business-specific';
  row.status = 'migrated';
  row.differences = '20单元同形薄宏（REUSE-T32 定案保留，migration §13.3 在案：ird_add_gtest 定案 business-specific 保留薄宏，TEST-HARNESS 保持 candidate）：上游ADD_RW_GTEST非CONFIG导出，不另建测试构建平台；本批（REUSE-T41）仅 ui_plugin 链接段增 sdurws_ird_execution 装配边，宏体零改动';
  row.nonOverlapEvidence = { path: P.readme, sha256: readmeDigest };
  saveJson(rel, j, crlf);
}

// ---- 4a) 新增 ui.CMake.targetWiring（ui/CMakeLists.txt 整文件行覆盖载体） ----
{
  const rel = FN + 'ui.json';
  const { j, crlf } = loadJson(rel);
  if (j.functions.some((f) => f.functionId === 'ui.CMake.targetWiring')) throw new Error('targetWiring already exists');
  const total = fs.readFileSync(ROOT + '/' + P.uiCMake, 'utf8').replace(/\r\n/g, '\n').split('\n').length;
  j.functions.push({
    functionId: 'ui.CMake.targetWiring',
    ownerUnit: 'ui',
    requirements: ['NFR-COR-01'],
    sources: [{
      path: P.uiCMake, symbol: 'sdurws_ird_ui_plugin', qualifiedSymbol: 'sdurws_ird_ui_plugin',
      startLine: 1, endLine: total, sha256: regionDigest(P.uiCMake, 1, total),
    }],
    targets: ['sdurws_ird_ui_plugin'],
    usageRole: 'owner',
    permissionRef: 'ARCH:3.6',
    capabilityId: 'NONE',
    decision: 'business-specific',
    status: 'migrated',
    search: {
      command: 'rg -n "target_link_libraries|add_library" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/CMakeLists.txt',
      conclusion: '宿主装配目标链接面整文件行覆盖载体（ui.SnapshotViewAdapter/ui.KinSessionModelView 整文件区段先例形态——行覆盖不充当语义完备证明）；ui 产品库零 execution 红线 O-31/C-3/4/5 不动',
    },
    differences: 'REUSE-T41：ui_plugin 链接段增 sdurws_ird_execution（WP-18/F-967 装配边——ird_gates_whitelist L10 同批登记，ASM-PLUG 先例）；宏体/测试目标零改动',
    inputs: [
      { path: P.uiCMake, sha256: changedDigests[P.uiCMake] },
      { path: P.uiPluginCpp, sha256: changedDigests[P.uiPluginCpp] },
    ],
    auditEvidence: [
      { path: P.gateReg, sha256: fileDigest(P.gateReg) },
      { path: P.readme, sha256: readmeDigest },
    ],
    nonOverlapEvidence: { path: P.readme, sha256: readmeDigest },
    auditBoundary: '仅 ui 单元构建接线面；不放宽 ird_gates 单元边（目标级边＝L10 登记面）；不证明 T01 完整或迁移完成',
  });
  saveJson(rel, j, crlf);
}

// ---- 4b) 新增 ui.IrdWorkbenchHostPlugin（UiPlugin.cpp/hpp 整文件行覆盖载体） ----
{
  const rel = FN + 'ui.json';
  const { j, crlf } = loadJson(rel);
  if (j.functions.some((f) => f.functionId === 'ui.IrdWorkbenchHostPlugin')) throw new Error('host row already exists');
  const cppTotal = fs.readFileSync(ROOT + '/' + P.uiPluginCpp, 'utf8').replace(/\r\n/g, '\n').split('\n').length;
  const hppTotal = fs.readFileSync(ROOT + '/' + P.uiPluginHpp, 'utf8').replace(/\r\n/g, '\n').split('\n').length;
  j.functions.push({
    functionId: 'ui.IrdWorkbenchHostPlugin',
    ownerUnit: 'ui',
    requirements: ['SA-18'],
    sources: [
      { path: P.uiPluginCpp, symbol: 'IrdWorkbenchHostPlugin', qualifiedSymbol: 'sdurws::ird::ui::IrdWorkbenchHostPlugin',
        startLine: 1, endLine: cppTotal, sha256: regionDigest(P.uiPluginCpp, 1, cppTotal) },
      { path: P.uiPluginHpp, symbol: 'IrdWorkbenchHostPlugin', qualifiedSymbol: 'sdurws::ird::ui::IrdWorkbenchHostPlugin',
        startLine: 1, endLine: hppTotal, sha256: regionDigest(P.uiPluginHpp, 1, hppTotal) },
    ],
    targets: ['sdurws_ird_ui_plugin'],
    usageRole: 'owner',
    permissionRef: 'ARCH:3.6',
    capabilityId: 'NONE',
    decision: 'business-specific',
    status: 'migrated',
    search: {
      command: 'rg -n "class IrdWorkbenchHostPlugin" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.hpp',
      conclusion: '宿主插件装配体整文件行覆盖载体（ui.KinSessionModelView 整文件区段先例形态）；语义分工由各功能行（productionWiring 等）承载，行覆盖不充当语义完备证明',
    },
    differences: 'REUSE-T41（F-967）：装配体为 L5 应用壳装配层（O-31 装配特权边承载者）——本批总线装配换核＋投递线程转接＋关停 drain 序；域互不依赖（R-1）不因装配边破坏',
    inputs: [
      { path: P.uiPluginCpp, sha256: changedDigests[P.uiPluginCpp] },
      { path: P.uiPluginHpp, sha256: changedDigests[P.uiPluginHpp] },
      { path: P.uiCMake, sha256: changedDigests[P.uiCMake] },
    ],
    auditEvidence: [{ path: P.readme, sha256: readmeDigest }],
    nonOverlapEvidence: { path: P.readme, sha256: readmeDigest },
    auditBoundary: '宿主装配体业务定性（SA-18/B1-SPEC 装配层）；不授新权限、不证明 T01 完整或迁移完成',
  });
  saveJson(rel, j, crlf);
}

// ---- 4c) 新增 execution.EventBusContractHeader（EventBus.hpp 整文件行覆盖载体） ----
{
  const rel = FN + 'execution.json';
  const { j, crlf } = loadJson(rel);
  if (j.functions.some((f) => f.functionId === 'execution.EventBusContractHeader')) throw new Error('contract header row already exists');
  const total = fs.readFileSync(ROOT + '/' + P.eventBusHpp, 'utf8').replace(/\r\n/g, '\n').split('\n').length;
  j.functions.push({
    functionId: 'execution.EventBusContractHeader',
    ownerUnit: 'execution',
    requirements: ['TASK-03'],
    sources: [{
      path: P.eventBusHpp, symbol: 'DomainEventBusImpl', qualifiedSymbol: 'sdurws::ird::execution::DomainEventBusImpl',
      startLine: 1, endLine: total, sha256: regionDigest(P.eventBusHpp, 1, total),
    }],
    targets: ['sdurws_ird_execution'],
    usageRole: 'owner',
    permissionRef: 'ARCH:3.6',
    capabilityId: 'NONE',
    decision: 'business-specific',
    status: 'migrated',
    search: {
      command: 'rg -n "class DomainEventBusImpl" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/include/sdurws/ird/execution/EventBus.hpp',
      conclusion: '总线契约头整文件行覆盖载体（ui.KinSessionModelView 整文件区段先例形态）；契约语义由 §10.4 头注与既有 audit-b32.EventBus.* 实现行承载',
    },
    differences: 'REUSE-T41（F-967 批注面）：subscribe 注释更正订阅位删除语义——core ReferenceEventBus::removeSink 全删（Events.cpp std::remove），本实现单订阅位删除；行为零变化，纯注释更正',
    inputs: [
      { path: P.eventBusHpp, sha256: changedDigests[P.eventBusHpp] },
      { path: 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/CMakeLists.txt', sha256: fileDigest('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/CMakeLists.txt') },
    ],
    auditEvidence: [{ path: P.readme, sha256: readmeDigest }],
    nonOverlapEvidence: { path: P.readme, sha256: readmeDigest },
    auditBoundary: '仅契约头批注面；不触碰 EventBus.cpp（11 行 pending 审计行全文件解决超出本包边界——待裁决登记）；不证明迁移完成',
  });
  saveJson(rel, j, crlf);
}

// ---- 5) L10 区段重定位 ----
{
  const rel = FN + 'foundation-governance.json';
  const { j, crlf } = loadJson(rel);
  const row = j.functions.find((f) => f.functionId === 'foundation-governance.ird_gates_whitelist.L10_IRD_TARGET_LEVEL_EDGES');
  if (!row) throw new Error('L10 row missing');
  const start = 580;
  const total = fs.readFileSync(ROOT + '/' + P.whitelist, 'utf8').replace(/\r\n/g, '\n').split('\n').length;
  let end = total;
  while (end > start && fs.readFileSync(ROOT + '/' + P.whitelist, 'utf8').replace(/\r\n/g, '\n').split('\n')[end - 1].trim() === '') end--;
  row.sources[0].startLine = start;
  row.sources[0].endLine = end;
  row.sources[0].sha256 = regionDigest(P.whitelist, start, end);
  row.differences = 'ird_gates 依赖红线门禁数据面静态登记（本批 F-958 段＝IRD_TOOL_TARGET_EDGES/T1 豁免增列 lsnchild 工具边，testdata_lint 同型，非放宽）；REUSE-T41 增段＝ui_plugin→sdurws_ird_execution 目标级装配边（WP-18/F-967，ASM-PLUG 先例——登记与源面修改同批）';
  row.inputs = row.inputs.map((b) => (b.path in changedDigests ? { ...b, sha256: changedDigests[b.path] } : b));
  saveJson(rel, j, crlf);
}

console.log('repin counts:', JSON.stringify(repin));
console.log('sync done');
