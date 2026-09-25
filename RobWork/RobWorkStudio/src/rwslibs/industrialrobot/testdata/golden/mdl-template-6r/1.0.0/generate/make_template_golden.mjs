/**
 * mdl-template-6r 黄金数据集生成脚本（testkit.md §4.4——脚本与产物同置、
 * 可独立重跑）。
 *
 * 数据集语义：六轴通用串联模板（generic-6r）的参数锁定——T-MDL-1 表值
 * （units/modeling.md §5.1，D-MDL-7 设计默认值）＋模板草稿面的期望值。
 * 本数据集 kind=contract-fixture：参考结果为手工构造（表值从单元卡 §5.1
 * 原文逐格转抄——不读产品代码；消费测试以期望值反查实现，方向为
 * "文档→数据→实现"），合法性由 modeling 单元契约定义。
 *
 * 独立性声明（testkit §4.4 口径）：本脚本零依赖产品代码（仅 Node 内置
 * 模块）；数值为 §5.1 表 T-MDL-1 的字面转写与 Math.PI 的最近双精度值。
 * 消费方（GoldenTemplateTest）以本 expected 反查 sixAxisTemplateDefaults()
 * 与 createDraft 产物——表值漂移在此显性失败。
 *
 * 重跑：node make_template_golden.mjs（自本目录执行；产物与入库版本
 * 逐字节一致——无时间戳/无随机源，NFR-COR-02）。
 */
'use strict';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

// ESM 下 __dirname 等价物（脚本自定位——重跑不依赖工作目录）。
const thisDir = path.dirname(fileURLToPath(import.meta.url));

// π 与 2π 的最近双精度字面（与 C++ 侧 3.14159265358979323846 字面同值
// ——IEEE 754 double 最近舍入，位级一致；比较用 EXPECT_DOUBLE_EQ）。
const PI = Math.PI;

// T-MDL-1 表值（units/modeling.md §5.1——六行逐格转写；单位 rad／rad·s⁻¹
// ／rad·s⁻²；axis 为连杆系下单位轴）。J6 行程恰 4π＝附录 D 第 11 项阈值
// 边界（行程≤阈值含于合规侧）。
const axisDefaults = [
  { joint: 'j1', type: 'Revolute', axis: [0, 0, 1], zeroOffset: 0.0,
    bounds: [-PI, PI], maxVelocity: PI, maxAcceleration: 2 * PI },
  { joint: 'j2', type: 'Revolute', axis: [0, 1, 0], zeroOffset: 0.0,
    bounds: [-PI / 2, PI / 2], maxVelocity: PI, maxAcceleration: 2 * PI },
  { joint: 'j3', type: 'Revolute', axis: [0, 1, 0], zeroOffset: 0.0,
    bounds: [-PI, PI / 2], maxVelocity: PI, maxAcceleration: 2 * PI },
  { joint: 'j4', type: 'Revolute', axis: [1, 0, 0], zeroOffset: 0.0,
    bounds: [-PI, PI], maxVelocity: 2 * PI, maxAcceleration: 4 * PI },
  { joint: 'j5', type: 'Revolute', axis: [0, 1, 0], zeroOffset: 0.0,
    bounds: [-PI / 2, PI / 2], maxVelocity: 2 * PI, maxAcceleration: 4 * PI },
  { joint: 'j6', type: 'Revolute', axis: [1, 0, 0], zeroOffset: 0.0,
    bounds: [-2 * PI, 2 * PI], maxVelocity: 2 * PI, maxAcceleration: 4 * PI },
];

// 模板草稿面期望（§5.1 建链规则：6×Revolute＋7 连杆；关节名 j<序>、
// 连杆名 base/l<序>；origin 种子恒位姿——表未登记 origin 列；BasePlacement
// 预设地面＋零位（MDL-22 V15-04 默认值在模板层填入）；材料种子钢 7850
// kg/m³——§5.3 密度默认表单点权威；workingRange 全行 NotApplicable）。
const expected = {
  note: 'mdl-template-6r 黄金参数（T-MDL-1 表值锁定＋草稿面期望；手工构造——contract-fixture）',
  descriptor: {
    templateId: 'generic-6r',
    displayName: '六轴通用串联',
    axisCount: 6,
    authority: 'Explicit',
    enabled: true,
  },
  axisDefaults,
  draftExpectations: {
    displayName: 'golden6r',
    authority: 'Explicit',
    jointCount: 6,
    linkCount: 7,
    jointNames: ['j1', 'j2', 'j3', 'j4', 'j5', 'j6'],
    linkNames: ['base', 'l1', 'l2', 'l3', 'l4', 'l5', 'l6'],
    jointTypes: ['Revolute', 'Revolute', 'Revolute', 'Revolute', 'Revolute', 'Revolute'],
    originSeed: { position: [0, 0, 0], rotation: 'identity' },
    basePlacement: { preset: 'ground', basePosition: [0, 0, 0], customEaa: null },
    j6TravelRad: 4 * PI,
    workingRangeState: 'not-applicable',
    materialSeed: { materialId: 'steel', densityKgPerM3: 7850.0 },
  },
};

// 七轴模板登记面期望（V-02——仅登记不启用：P-03 冻结前 enabled=false，
// 创建入口阻止；描述符清单三行序 generic-6r/generic-7r/custom-chain）。
const sevenAxisExpectations = {
  descriptor: {
    templateId: 'generic-7r',
    displayName: '七轴串联',
    axisCount: 7,
    enabled: false,
  },
  createDraftExpected: {
    outcomeError: 'TemplateDisabled',
    diagnosticCode: 'MDL-TEMPLATE-DISABLED',
    diagnosticCount: 1,
    noDraftProduced: true,
    noSilentFallbackTo6r: true,
  },
};

const out = {
  schemaVersion: 'mdl-template-6r/1',
  generatedBy: 'generate/make_template_golden.mjs',
  expected,
  sevenAxisExpectations,
};

fs.writeFileSync(
  path.join(thisDir, '..', 'expected', 'six-axis-defaults.json'),
  JSON.stringify(out, null, 2) + '\n',
  'utf8');
console.log('mdl-template-6r: expected/six-axis-defaults.json 已生成（与入库版本逐字节一致）');
