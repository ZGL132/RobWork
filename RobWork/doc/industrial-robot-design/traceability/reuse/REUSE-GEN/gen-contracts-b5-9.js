// REUSE-T46..T50 契约与 README 生成（一次性治理编制工具）。
'use strict';
const fs = require('fs');
const ROOT = 'D:/10_Source_Repos/21_robot/RobWork';
const GOV_A = { commit: '5c358c0fc7d76d1dcc9c8ac55823160b6891944d', path: 'RobWork/doc/industrial-robot-design/traceability/acceptance/GOV-T21-20261022-r2.json', reviewedCommit: '6eab38df81473b3d5248464f3f06d26b6f062fbc' };
const GOV_B = { commit: 'ff3472721cd78bbefbe5e1c6dd75bdd349a649be', path: 'RobWork/doc/industrial-robot-design/traceability/acceptance/GOV-T22-20261022-r3.json', reviewedCommit: 'e6f06e5b9451357b10001a4677b10b4ad9e454a6' };

const specs = [
  {
    id: 'REUSE-T46', unit: 'workflow', title: 'WP-19批次五·workflow单元审计收口：关停监督/门禁服务business-specific定案',
    reqs: ['WF-01', 'WF-03', 'NFR-COR-01'],
    designRef: 'units/workflow.md#1',
    summary: 'CloseSupervisor 关停监督 6 行（UTIL-THREAD）＋Gate 评估服务＋测试骨架/构建宏——上游 ThreadPool/ThreadTask 为通用原语，不承载业务关停协议（监督循环/关停序），policy §1.2 业务协议保留面；零产品变更。',
    outputs: 'workflow.json 9 行 pending 全量收口（business-specific，零产品变更）',
  },
  {
    id: 'REUSE-T47', unit: 'evidence', 'title': 'WP-19批次六·evidence单元审计收口：快照/切片字节原语族与裁定聚合定案',
    reqs: ['EV-01', 'EV-02', 'NFR-COR-01'],
    designRef: 'units/evidence.md#1',
    summary: 'Snapshot/Slice/Evidence/Evaluator 字节原语 24 行（UTIL-BINARCHIVE，含评审缺口3反查补登的 Evidence.cpp/Evaluator.cpp 复制面）＋aggregateVerdict＋测试骨架/构建宏——字节身份契约保留（REUSE-T43 跨包决策记录；快照编码身份权威留本单元不搬 core）；零产品变更。',
    outputs: 'evidence.json 26 行 pending 全量收口（business-specific，零产品变更）',
  },
  {
    id: 'REUSE-T48', unit: 'ui', title: 'WP-19批次七·ui单元审计收口：QtTextBillboard/测试骨架收口＋宿主面20行结构墙登记',
    reqs: ['UI-01', 'NFR-COR-01'],
    designRef: 'units/ui.md#1',
    summary: 'QtTextBillboard 5 行（HOST-TEXT）＋contract/gui 测试骨架＋构建宏收口 business-specific；HostView3DPreviewBackend（model/construct-model 类别命中）/HostView3DGateway（state 类别命中）/UiPlugin（CORE-EVENT＝F-967/T41 blocked 候选）20 行保持 pending＝REUSE-AD 结构墙（宿主例外封闭清单/权限表无对应授权，已解决记录要求精确 grant）——解阻＝所有者扩面裁决（T41 F-967 裁决同链）；零产品变更。',
    outputs: 'ui.json 28 行 pending 处置（8 收口＋20 结构墙登记）',
  },
  {
    id: 'REUSE-T49', unit: 'testkit', title: 'WP-19批次八·testkit单元审计收口：JsonLite/Check/ProcessRunner/Dataset定案＋F-959 blocked登记',
    reqs: ['NFR-COR-01', 'NFR-TST-01'],
    designRef: 'units/testkit.md#1',
    summary: 'JsonLite 21 行（上游无 JSON 库；Boost.JSON 属新依赖渠道需所有者批准，未批保留）、Check 7 行、ProcessRunner 11 行（Win32 薄封装）、Dataset 24 行、测试骨架/构建宏——全部 business-specific 收口；F-959（黄金生成脚本 make_import_expectations.mjs 写 unsupported:8/joint-velocity-limit:5 而入库 expected 为 unsupported:3+mappedVelocityCount:5，生成器自 15437a8da 后未改、expected 由 7a9f05168 更新）按 §13.3「先修契约再审差异，不能洗绿」保持 blocked，未获批不实施；零产品变更。',
    outputs: 'testkit.json 64 行 pending 全量收口（business-specific）＋F-959 blocked 登记',
  },
  {
    id: 'REUSE-T50', unit: 'drivetrain', title: 'WP-19批次九·drivetrain遗留收口：codec字节原语族定案＋catalog MATH-SVD补inverse符号',
    reqs: ['DT-01', 'NFR-COR-01'],
    designRef: 'units/drivetrain.md#1',
    summary: 'DriveTrainCodec ByteWriter/Reader 8 行（UTIL-BINARCHIVE）按字节身份契约保留（REUSE-T43 跨包决策记录；T12 已核 BINArchive 不等价）；catalog MATH-SVD LinearAlgebra.hpp upstream.symbols 补「inverse」（T12 §5.1 整合建议落实——T12 已生产消费 svd/inverse，符号在位实证）；T12 性能对比 NOT RUN/独验为所有者侧动作仅引用不重跑（WP-03 README 第12行）；零产品代码变更。',
    outputs: 'drivetrain.json 8 行 pending 全量收口＋catalog MATH-SVD 补 inverse 符号',
  },
];

const basePlaceholder = '__BASE__';
for (const s of specs) {
  const contract = {
    schemaVersion: 'ird-task/1',
    taskId: s.id,
    unit: s.unit,
    masterWp: 'WP-A',
    title: s.title,
    status: 'ready',
    branch: 'redesign-main',
    dependsOn: ['GOV-T21', 'GOV-T22'],
    requirements: s.reqs,
    designRefs: ['reuse/migration.md#13.3', 'reuse/policy.md#1.2', s.designRef],
    verify: [
      `pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-task.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/${s.id}.json`,
      'cmake -DIRD_ROOT=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot -P RobWork/RobWorkStudio/src/rwslibs/industrialrobot/cmake/ird_gates.cmake',
      `pwsh -NoProfile -File RobWork/scripts/industrialrobot/verify-task-scope.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/${s.id}.json -Base ${basePlaceholder} -Head <送验SHA>`,
    ],
    allowedFiles: [
      `RobWork/doc/industrial-robot-design/tasks/${s.id}.json`,
      `RobWork/doc/industrial-robot-design/reuse/changes/${s.id}.json`,
      `RobWork/doc/industrial-robot-design/reuse/functions/${s.unit}.json`,
      'RobWork/doc/industrial-robot-design/reuse/migration.md',
      `RobWork/doc/industrial-robot-design/traceability/reuse/${s.id}/**`,
      'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-GEN/**',
    ],
    forbiddenFiles: ['RobWork/RobWork/src/**', 'RobWork/RobWorkSim/src/**', 'vcpkg/**', 'main'],
    outputs: [s.outputs],
    interUnit: false,
    acceptance: [
      '零产品变更审计收口：pending 行按族判定（字节身份契约/业务协议保留/上游无同义物/测试骨架定案）处置为 business-specific，逐 functionId nonOverlap 条目＋检索记录在案；不洗绿（ui 20 行/testkit F-959 保持 pending/blocked 并登记结构墙与解阻路径）',
      'catalog 同步（如涉本批）：REUSE-T50 MATH-SVD LinearAlgebra.hpp symbols 补 inverse（T12 §5.1 建议落实，符号在位实证）；其余批次零 catalog 变化',
      '门禁：validate-docs -TaskFile（含全量复用检查）与 verify-task-scope 显式 Base/Head 全绿；ird_gates 零命中（零产品变更无构建扩大面）',
    ],
    note: `migration §13.3 WP-19 批次（${s.unit}）。masterWp=WP-A 按 T29/T30 先例。起始 SHA=${basePlaceholder}（上一批次收口提交）。范围＝§13.3 WP-19 行 ${s.unit} 面。${s.summary}。所有者保留裁决项：${s.id === 'REUSE-T48' ? '①宿主面 20 行 grant/例外扩面（T41 F-967 同链裁决）' : ''}${s.id === 'REUSE-T49' ? 'F-959 黄金生成脚本修复契约（§13.3 明令先修契约再审差异，未获批不得自批——blocked 登记）与 Boost.JSON 渠道批准' : ''}${s.id === 'REUSE-T47' || s.id === 'REUSE-T50' ? 'Boost.Endian 字节原语替换渠道（REUSE-T43 决策记录）' : ''}。实施完成待独验；独立验收由所有者另开会话，实施者不自签。`,
    governanceAcceptance: GOV_A,
    reuseGateAcceptance: GOV_B,
    reuse: { policy: 'H1-H6', manifest: `RobWork/doc/industrial-robot-design/reuse/changes/${s.id}.json` },
  };
  fs.writeFileSync(`${ROOT}/RobWork/doc/industrial-robot-design/tasks/${s.id}.json`, JSON.stringify(contract, null, 1) + '\n');
  const readme = [
    `# ${s.id} — WP-19 批次：${s.unit} 单元审计收口`, '',
    '起始 SHA 见任务契约 note（批次串行上一收口提交）。状态：实施完成待独验。**零产品代码变更**（REUSE-T50 仅 catalog 数据面）。', '',
    '## 判定摘要', '',
    s.summary, '',
    '## 矩阵与凭证', '',
    `- functions/${s.unit}.json 全量收口（收口/保留明细以 validator 输出与 nonoverlap 文件为准）；`,
    `- nonoverlap-${s.unit}.json：逐 functionId 条目（族判定＋行差异引用＋结构墙/解阻说明）；`,
    `- changes/${s.id}.json：空产品变更清单（纯审计批次）；search-batch.txt：族检索记录。`, '',
    '## 所有者保留裁决项', '',
    s.id === 'REUSE-T48' ? '- 宿主面 20 行（PreviewBackend/Gateway/UiPlugin）：REUSE-AD 结构墙——解阻＝所有者扩面裁决（T41 F-967 同链）。' : '',
    s.id === 'REUSE-T49' ? '- F-959：黄金生成脚本先修契约再审差异（§13.3），未获批不实施，blocked 登记。Boost.JSON 渠道批准同属所有者。' : '',
    (s.id === 'REUSE-T47' || s.id === 'REUSE-T50') ? '- Boost.Endian 字节原语替换渠道：REUSE-T43 决策记录（批准前置＝字节基线捕获）。' : '',
  ].filter(Boolean).join('\n');
  fs.writeFileSync(`${ROOT}/RobWork/doc/industrial-robot-design/traceability/reuse/${s.id}/README.md`, readme + '\n');
  console.log(`${s.id} contract+README written`);
}
