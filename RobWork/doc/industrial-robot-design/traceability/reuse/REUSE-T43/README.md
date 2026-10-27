# REUSE-T43 — WP-19 批次二：selection 单元复用收口

起始 SHA `a445f48765215121e464f9cd9d07ad342f83ae76`（批次一收口提交）。状态：实施完成待独验。

## 判定与实施

1. **对称特征值换核（H1 直接 API）**：`symmetricEigenvalues3x3` 换上游 `rw::math::LinearAlgebra::eigenDecompositionSymmetric<double>`（MATH-EIGEN，Eigen SelfAdjointEigenSolver，升序特征值契约与产品一致），手写解析闭式平行核删除；保留六分量→3×3 展开薄适配。`noexcept` 随 Eigen 堆使用移除（fail-fast）。消费面 `checkSynthesisAssertions`（MDL-06②③）不变。
2. **精度口径（评审缺口 7）**：Eigen QR 与解析闭式为求解器间 ulp 级差异、**非逐位相等**（探针 v1 位模式断言实证——含对角/纯量阵 Eigen 亦不逐位）；产品契约面为容差语义（附录 D 第 6/7 项相对 1×10⁻¹²）——探针 v2 九用例（对角/纯量/平面退化/近退化/大小尺度）容差＋升序＋迹守恒全过。
3. **依赖边（§4 CMake/依赖行）**：`sdurw_math` PRIVATE 库边为 selection 首条 rw 库链接（显式实例化符号在 LinearAlgebra.cpp 编译库内）；条件别名分支与 drivetrain REUSE-T12 同款（`NOT TARGET sdurw_math`→`find_package(RobWork CONFIG REQUIRED)`→`ALIAS RW::sdurw_math`）。独立冒烟构建绿（build/govt24-smoke 经导出包解析）。公共接口零变化（换核函数文件私有），无对端消费者扩大。
4. **字节原语族决策（评审缺口 3/8）**：Backfill 与 CombinationCheck 复制原语统一按字节身份契约保留（`byte-primitives-decision.md`）——BINArchive 主机序/格式化长度不等价（SB/SG 检索）；Boost.Endian 候选列为所有者明确批准项（T20/T21 先例、四单元无 Boost 接线、IRD-GATE-LIB 先例），批准后落地前置＝先捕获字节基线（顺序敏感）。
5. **TRAJ-LINEAR 候选否决（CapabilityCurve）**：LinearInterpolator 为 t∈[0,1] 段内插值器，不覆盖 §6.2 四条插值规则与 §14.3 分轨；数值核仅两 flop、逐段适配零结构可删——保留产品实现，catalog selection usage 同步移除；可复用段插值已在 trajectory CartesianLine（REUSE-T26）真实消费。

## 生产证据（ird-reuse-production/1）

`production/selection.symmetricEigenvalues3x3.json`，target `sdurws_ird_selection`，bindings 5 项：

| 面 | 结果 |
| --- | --- |
| normal | 全量 292 项 passed（GUI harness 环境占位排除）exit=0 |
| oracle | `SelBackfillSynthesis.*` 7 项（MDL-06 分类轨道＋黄金容差对照 sel-golden 附录 D 第 9 项） |
| disconnected | 特征值返回序反转变异 → RejectsNotPositiveDefinite/RejectsTriangleInequality 2 项真实失败 exit=1；mutatedSource `run/backfill-mutated.cpp` |
| upstreamComparison | 探针 v2（`run/probe-eigen-upstream.exe`）1 项 passed（九用例容差网格）；v1 位模式失败记录保留 `run/upstreamComparison-v1-bitwise-failed.*` |

恢复后 recheck：`run/recheck.xml` 7 项绿；CMake 定稿后 `run/recheck2.xml` SelBackfill* 26 项绿。独立冒烟：`run/smoke-selection-build.log`（build/govt24-smoke 的 sdurws_ird_selection 构建成功）。

## 矩阵与凭证

- `functions/selection.json`：52 行（1 direct＋51 business-specific）；Backfill.cpp 1243 行、CMakeLists.txt 660 行零空行遗漏全行铺面；保留行 inputs 绑定随 CMakeLists 变更刷新。
- `changes/REUSE-T43.json`：Backfill.cpp（direct/MATH-EIGEN）＋CMakeLists.txt（governance 接线）。
- `catalog.json`：TRAJ-LINEAR 移除 selection:adapter usage（否决同步，唯一改动处）。
- 负检索：`search-batch.txt`（SF 零命中＝回填业务族产品专属；SB/SC/SE/SG 为候选适用性证据）。

## 复现

```powershell
cmake --build build --config Release --target sdurws_ird_selection_test
cmake --build build/govt24-smoke --config Release --target sdurws_ird_selection
# 四面命令见 production JSON；探针编译 run/probe-build.cmd
```
