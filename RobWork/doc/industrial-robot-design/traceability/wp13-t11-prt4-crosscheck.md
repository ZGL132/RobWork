# P-RT-4 交叉核对留痕——安装预设轴向（WP-13-T11 兑现行）

- 登记日期：2026-09-25
- 任务：tasks/foundation/WP-13-T11.json（acceptance 第 3 条）
- 裁决依据：DTB §4 O-16（已裁决 2026-09-22，WP-13-T01 同批）——units/modeling.md
  采纳 runtime 侧锚点：`installationPresetRotation()` 为安装预设旋转矩阵的
  **唯一权威产出点**，modeling 只存参数不存矩阵。
- 对照面：
  - runtime：units/runtime.md §6.2（安装预设的精确定义表）＋
    `runtime/include/sdurws/ird/runtime/BaseWorldTransform.hpp`（文件头
    "★ P-RT-4 冻结留痕"段——本头即冻结锚点）。
  - modeling：units/modeling.md §4.3（basePlacement 行）、§7.6 末段
    （基座—世界变换隔离）、§9.1（字段映射表 base 行——"不在此计算
    R_world_base"）。

## 1. 冻结结论（双方一致，无分歧）

| 预设 token | R_world_base（runtime.md §6.2 冻结值） | 语义（基座 Z 轴指向） |
| --- | --- | --- |
| `ground` | I（单位阵） | +Z_W（竖直向上）——MDL-22 默认（V15-04） |
| `inverted` | R_x(π)＝diag(1,−1,−1) | −Z_W（竖直向下，吊装）——P-RT-4 轴向选择 |
| `wall` | R_y(π/2)＝[[0,0,1],[0,1,0],[−1,0,0]] | 水平（壁/侧装）——P-RT-4 轴向选择 |
| `custom` | 由 customEaa（EAA，rad）经 `rotationFromCustomEaa()` 换算 | 任意 |

- 唯一权威产出点＝`runtime::installationPresetRotation()`（预设矩阵）与
  `runtime::rotationFromCustomEaa()`（custom 的 EAA→R 换算）——两者均位于
  `runtime BaseWorldTransform.hpp`；若建模侧用户口径与本定义分歧，按 §15.3
  P-RT-4 处置约束"以建模侧为准并回改 runtime 文档"，届时只改该函数一处
  （单一权威，调用方零改动）。

## 2. modeling 侧履行点（WP-13-T11 实现核对）

1. **只存参数不存矩阵**：`modeling::BasePlacement`（RobotDesign.hpp）仅含
   `preset`（runtime::InstallationPresetToken——词表直接复用，不另设词表）、
   `customEaa`（SourcedValue，rad）、`basePosition`（SourcedValue，m）——
   无任何旋转矩阵字段；Codec（Codec.cpp）对该三字段编码，无矩阵字节。
2. **编辑边界**：`applyBasePlacementEdit`（Template.hpp/.cpp，本任务落位）
   接受三预设＋custom（customEaa 必填）＋basePosition 编辑；I-MDL-7 非法
   组合在映射层就地拒绝（custom 缺 customEaa；preset≠ground 而 R=I——
   恒等判定逐元素 1×10⁻¹²，与 runtime `checkPresetConsistency` 的
   InputInvalid 同口径；正交性判定复用值模型 I-MDL-7 单一实现）。
3. **不计算/不缓存/不二次叠加**（M-11/§7.6）：编辑流仅在拒绝判定时调用
   runtime 权威换算做只读校验，产物只含三参数；V-12 建模侧输入面断言＝
   Description.base 只含 preset/customEaa/basePosition（§9.1 字段映射表
   base 行），无任何预乘旋转矩阵（场景对象 worldPose 不预乘安装旋转的
   断言已由 WP-13-T10 ACC3 落位）。

## 3. 执行证据（WP-13-T11 全新捕获）

- `MdlBasePlacementEdit.Prt4PresetAxisSingleAuthority_WP13T11_ACC3`
  （modeling/test/BasePlacementEditTest.cpp）：编辑产物经 Description.base
  同款字段映射交 runtime 解析——inverted/wall/ground 的 R 与 §6.2 冻结值
  逐元素一致（测试内独立抄写），且与 `installationPresetRotation()` 输出
  逐元素一致；位置分量原样透传（m）。
- `MdlBasePlacementEdit.SingleFieldCompileIntoRWorldBase_WP13T11_ACC4`：
  custom EAA 编辑值经 runtime 唯一编译进 R_world_base（手算 R_z(π/2)
  对照，1×10⁻¹² 容差）；`checkWorldBaseTransform`/`checkPresetConsistency`
  对编译产物全部通过。
- 留痕：traceability/builds/wp13-t11/（gtest XML＋ird-test-report.json
  两模式全量 193/193、144/144；上两用例 requirementIds=[MDL-22]、
  atIds=[AT-37]）。

## 4. 卡缺陷注记（契约 note ⑤，随本留痕一并登记）

units/modeling.md 多处引用"§7.7（隔离声明）"，但该卡无 §7.7 独立节——
内容实为 §7.6 末"基座—世界变换隔离"要点（任务契约按真实锚点 #7.6 引用）。
单元卡 §15 v0.13 增量已同步登记：建议后续增量修订补 §7.7 节标题或订正
卡内引用（本任务 allowedFiles 不含对引用语义的裁决，未代为改写）。
