/**
 * mdl-urdf-import 黄金数据集生成脚本（testkit.md §4.4——脚本与产物同置、
 * 可独立重跑；contract-fixture——参考结果为手工构造，合法性由 modeling
 * 单元契约定义：units/modeling.md §6.3 URDF 字段映射表/§6.4 链型判定/§6.5
 * Xacro 受控子集）。
 *
 * 手工构造依据（逐条可回查单元卡）：
 *   - 忽略清单（3）：<gazebo> 外来扩展（§6.3"报告不解释"）＋
 *     <disable_collisions>（实现语义：顶层扫描入外来扩展面、全树收集入
 *     自碰撞候选——双登记面，ImportTest 包含式断言同口径）＋robot 级
 *     <material> 定义（元素名 "material 'gray'"——无落点无密度语义）；
 *   - 不支持清单（8）：<transmission>（R2）×1＋<limit velocity>（§4.3-A
 *     无落点，不发明 schema）×5（j1/j2/j4/j5/j6 携带 velocity）＋非单位
 *     scale（schema 无落点）×1＋package:// ROS URI（引导改相对路径）×1；
 *   - 待确认清单（2）：j2 缺 axis（MDL-11 默认局部 +X 待确认）＋j3 缺
 *     limit（MDL-06④ revolute 限位必填面）——subject 为定位主体稳定
 *     文本 "joint '<净化后名>'"（T05 实现语义，ImportTest 同源）；
 *   - 关节状态表（6）：六关节全部映射成功（无零轴/非法轴）；
 *   - 资源状态表（2）：meshes/l1.stl（recorded——依赖树存在叶）＋
 *     meshes/ghost.dae（missing——依赖树缺失叶，IO-RES-MISSING 事实）；
 *     package:// 不入依赖树（io 不可达）——仅不支持项承载；
 *   - 自碰撞候选（1）：<disable_collisions base/l1>——P-MDL-3 仅报告
 *     候选不写策略对象；
 *   - 传动回填候选（5）：<limit effort> 逐关节（j3 无 limit 行故无候选）；
 *   - 链型能力：六轴全旋转→FullTemplateRange（movableAxes=6、不含
 *     prismatic——§6.4 维度二）；Xacro 黄金样例三轴→BeyondTemplateRange
 *     （1~3 轴无模板语义——T05 §6.4 行语义澄清，类型保留草稿可编辑）；
 *   - 计数约定登记：mapped/defaults 两清单的条目粒度（每源元素→落点对
 *     一条）属实现细化，卡面不钉粒度——本数据集以 countAtLeast 下界
 *     ＋关键条目命中承载（精确计数面归上列八清单）。
 *
 * 重跑：node make_import_expectations.mjs（无时间戳/无随机源——产物与
 * 入库版本逐字节一致，NFR-COR-02）。
 */
'use strict';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const thisDir = path.dirname(fileURLToPath(import.meta.url));

const importReport = {
  schemaVersion: 'mdl-urdf-import-report/1',
  note: 'V-06 期望面（contract-fixture 手工构造——§6.3 映射表逐行语义；计数约定见 generate 脚本头注）',
  counts: {
    jointStatuses: 6,
    jointStatusesAllMapped: true,
    resources: 2,
    resourceStates: { 'meshes/l1.stl': 'recorded', 'meshes/ghost.dae': 'missing' },
    missingResourceDigestHexEmpty: true,
    selfCollisionCandidates: 1,
    selfCollisionPair: { link1: 'base', link2: 'l1' },
    drivetrainCandidates: 5,
    drivetrainCandidateJoints: ['j1', 'j2', 'j4', 'j5', 'j6'],
    pendingConfirms: 2,
    pendingKinds: [
      { kind: 'axis-default-plus-x', subject: "joint 'j2'" },
      { kind: 'limit-missing', subject: "joint 'j3'" },
    ],
    ignored: 3,
    ignoredElements: ['gazebo', 'disable_collisions', "material 'gray'"],
    unsupported: 8,
    unsupportedKinds: [
      { kind: 'transmission', count: 1 },
      { kind: 'joint-velocity-limit', count: 5 },
      { kind: 'mesh-scale', count: 1 },
      { kind: 'ros-uri', count: 1 },
    ],
    mappedCountAtLeast: 6,
    defaultsCountAtLeast: 3,
    submittable: true,
    noNamedBlockingError: true,
  },
  draft: {
    displayName: 'golden arm',
    rootLocalNameCandidate: 'golden_arm',
    jointCount: 6,
    linkCount: 7,
    jointTypes: ['Revolute', 'Revolute', 'Revolute', 'Revolute', 'Revolute', 'Revolute'],
    j1AxisNormalized: [0.0, 1.0, 1.0 - 1.0],
    j1AxisNote: '源 "0 2 0" 归一化（MDL-11 任意有限非零轴）',
    j1BoundsRad: [-1.57, 1.57],
    j1OriginZM: 0.2,
    j2AxisDefaultPlusX: [1.0, 0.0, 0.0],
    j2AxisNote: '缺 axis——默认局部 +X 入待确认草稿（MDL-11）',
    l1MassKg: 2.0,
    l1InertiaIxx: 0.01,
    l1InertiaIxy: 0.002,
    l1MaterialId: 'steel_gray',
    l1DensityState: 'not-provided',
    l2BodyState: 'not-provided',
    basePlacementPreset: 'ground',
    resourceManifestSize: 2,
    resourceManifestStates: { 'meshes/l1.stl': 'recorded', 'meshes/ghost.dae': 'recorded' },
    missingCarriedAsRecorded: true,
  },
  chainCapability: {
    kind: 'FullTemplateRange',
    movableAxes: 6,
    containsPrismatic: false,
  },
  diagnostics: {
    resMissingPresent: true,
    resMissingCode: 'IO-RES-MISSING',
    templateRangeAbsent: true,
    templateRangeNote: '六轴全旋转不产 TEMPLATE-RANGE 提示（超出范围才提示）',
  },
};

const xacroSamples = {
  schemaVersion: 'mdl-urdf-import-xacro/1',
  note: 'AT-31 Xacro 黄金样例期望（§6.5 受控子集——property/宏/参数声明式替换；V-09 include 循环透传面）',
  goldenMacro: {
    expansionSucceeds: true,
    parameterList: [
      { name: 'link_lift_m', valueText: '0.25', source: 'property' },
    ],
    parameterListNote: '无用户 substitutions——参数列表仅文档属性（定义序）',
    importJointCount: 3,
    importJointNames: ['j1', 'j2', 'j3'],
    importBoundsRad: [[-1.0, 1.0], [-2.0, 2.0], [0.0, 3.0]],
    importOriginZM: 0.25,
    importOriginNote: '宏体内 ${link_lift_m} 经调用点作用域解析（文档属性）',
    chainCapability: { kind: 'BeyondTemplateRange', movableAxes: 3, containsPrismatic: false },
    chainCapabilityNote: '三轴无模板语义（T05 §6.4 行语义澄清）——TEMPLATE-RANGE 提示在案',
    templateRangeDiagnosticPresent: true,
    submittable: true,
    substitutionMappedNotes: 0,
    substitutionNote: '空 substitutions——映射清单零 "xacro-substitution/" 前缀条目（确定性）',
    provenanceDigestEqualsOriginalXacro: true,
    xacroSourceResourceRowRecorded: true,
  },
  cycle: {
    expansionFails: true,
    expectedErrorCode: 'IncludeCycle',
    expectedDiagnosticCode: 'IO-FORMAT-XML-CYCLE',
    expandedBytesAbsent: true,
    noDraftProduced: true,
    note: 'include 相互循环（a→b→a）——io 契约本应拦截；本服务防御性复核透传 V-09 码面（无草稿、无对象写入）',
  },
};

fs.writeFileSync(
  path.join(thisDir, '..', 'expected', 'import-report.json'),
  JSON.stringify(importReport, null, 2) + '\n', 'utf8');
fs.writeFileSync(
  path.join(thisDir, '..', 'expected', 'xacro-samples.json'),
  JSON.stringify(xacroSamples, null, 2) + '\n', 'utf8');
console.log('mdl-urdf-import: expected/import-report.json + expected/xacro-samples.json 已生成');
