# REUSE-T42 — WP-19 批次一：requirements 单元复用收口

起始 SHA `caea72c7bed3cf1fc2b432a3ce3ea1086006fe7a`（分支 redesign-main 主工作树串行）。状态：实施完成待独验。

## 范围与判定

- **G3（同族反解移交项）**：TemplateArray.cpp 反解族处置分三面——
  1. 正变换 `rotationFromRpy` 换上游 `rw::math::RPY<double>(yaw,pitch,roll).toRotation3D()`（MATH-ROT 直接 API；与 modeling/rpymath::rpyToRotation 同型，REUSE-T11 已闭）。上游逐元素解析式与原手写 Rz·Ry·Rx 展开恒等且乘加序一致——探针（`run/upstreamComparison.xml`）12 用例（黄金输入、零旋转、±π/2 精确万向锁、±1e-13 近万向锁、一般姿态）IEEE754 位模式逐位相等，黄金模板/数据集摘要零扰动。
  2. 反解 `reflectRpy` 保留产品唯一职责。上游 `RPY(R, eps=1e-5)` 三点差异实证：①退化判定带宽 1e-5 远宽于产品 1e-12——|cp|∈[1.41e-6,1e-5] 区间上游吸附退化表示，REQ-10 的 1e-8rad 数值回归禁止该吸附；②退化轴约定不同（上游置绕 Z=0、产品置 roll=0）——同一矩阵两种表示，模板条目字段/摘要随表示变化；③退化提取元素不同（上游 atan2(r12,r22) vs 产品 atan2(R[1][2],R[1][1])）。probe-r6（REUSE-T01-api-r6）同证旋转往返正确但表示差异。
  3. `modeling/RpyMath.hpp` 为 modeling 私有头（closures.json MATH-ROT 冻结面），红线禁跨单元私有头——requirements 不能引用，差异证据据此闭合「为何不能接 RpyMath」。
- **消费一致性待核项关闭（Sampling.digest）**：分母面（Random→count、Grid→饱和乘积 0xFFFFFFFE 上界、零样本 0 合法 V-02）由 SamplingTest（75U/0U/Random-zero）与 GoldenDatasetsContractTest:813 真实断言钉扎；下游 evidence/Snapshot.hpp `plannedPositionSamples/plannedPoseSamples` 与 Evidence.hpp:1126「与快照采样计划一致（分母来源）」同面引用，无第二分母。
- **覆盖墙登记**：`targetEditRotation`（RequirementsPanelWidget.cpp，4165 行）保持 pending——任何改动触发复用门禁对该文件全行铺面＋全文件解决（T33/T36/T40 结构同型），超出本批边界；其正变换候选与本包换核同款且已证恒等，后续 UI 面批次全行铺面后按候选落地。

## 矩阵与凭证

- `reuse/functions/requirements.json`：48 行（46 business-specific migrated＋1 direct migrated＋1 pending）。TemplateArray.cpp 全行铺面 38 行（1491 行非空行零遗漏，脚本 `gen-matrix.js` 按校验器同口径 Digest 编制）。
- business-specific 行 nonOverlap 绑定：`nonoverlap-requirements.json`（负检索 S1—S4 见 `search-batch.txt`，全部 exit=1 零命中）。
- 逐文件凭证：`reuse/changes/REUSE-T42.json`（TemplateArray.cpp，mode=direct，MATH-ROT）。

## 生产证据（ird-reuse-production/1）

`production/requirements.rotationFromRpy.json`，target `sdurws_ird_requirements`，bindings 13 项（生产源＋CMake＋MATH-ROT 全部上游路径与 targetFile）：

| 面 | 结果 |
| --- | --- |
| normal | 全量 221 项 passed（排除 GUI 进程占位用例 `GuiRegistration.V22RegisteredNotExecuted_ACC1`，同 T30 先例；gui_test exe 单独覆盖）exit=0 |
| oracle | `ReqTemplateArray.MirrorGolden*` 2 项解析黄金 passed |
| disconnected | 字段换序变异（RPY(yaw,pitch,roll)→RPY(roll,pitch,yaw)）后 ReqTemplateArray 22 项中 MirrorGoldenNormalY/Z 2 项真实失败，exit=1；mutatedSource 绑定 `disconnected-source.cpp` |
| upstreamComparison | 探针 `run/probe-upstream-forward.exe`：基线展开（git 基线原文摘录）vs 上游调用位模式逐位相等，1 test passed |

恢复后 recheck：`run/recheck.xml` 22 项全绿（内容摘要与原 sha 一致 `c8aad592…`）。

探针 v1 曾断言上游结果 `isProperRotation()`，general-3（幅值 2.7/1.2/−3.1）在上游自身容差语义下为 false——该断言属上游内部校验语义、非产品契约（产品侧不消费 isProperRotation），v2 收窄为位模式断言；v1 记录保留 `run/upstreamComparison-v1-properness-failed.{xml,log}`。

## 复现

```powershell
cmake --build build --config Release --target sdurws_ird_requirements_test
# normal / oracle / disconnected / recheck 各面命令见 production JSON command 字段
# 探针编译：run/probe-build.cmd（cl + sdurw_math.lib + vcpkg gtest，含 eigen3 include）
```

构建日志：`run/build-t42.log`、`run/build-disconnected.log`、`run/build-recheck.log`。
