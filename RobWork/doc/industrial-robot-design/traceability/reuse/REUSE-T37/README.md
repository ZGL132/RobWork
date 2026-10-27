# WP-15批次A／REUSE-T37：core 基础数值收口——有限检查换标准库 std::isfinite 与 Deg2Rad 候选结构不可落地记录

实施日期：2026-10-25；分支 redesign-main；起始 SHA `e5364bd6ca0000eae7a1d6f394ca63ba2e79ba10`（WP-13-T36 收口提交）。
任务契约：[REUSE-T37](../../../tasks/REUSE-T37.json)。状态＝实施完成待独验（不自签）。

## 改动内容（产品树仅两文件）

1. `core/include/sdurws/ird/core/Compare.hpp`：`Tolerance::make` 有限检测自持表达式 `x==x && x-x==0` 换标准库 `std::isfinite` 直接原语（B31 审计建议，见 [core-units-compare-audit](../../../reuse/core-units-compare-audit.md) 第 9 行）；补 `#include <cmath>`（`std::isfinite` 声明于 `<cmath>`，公共头显式包含不依赖传递包含）。
2. `core/src/Compare.cpp`：`closeWithin` 非有限前置同改 `std::isfinite(value) && std::isfinite(reference)`。

语义等价论据：有限 x 满足 `x==x` 且 `x−x` 精确为 0（等值相减恒为零，无溢出路径）；NaN 破坏自等；±Inf 自差为 NaN≠0——两式对全部 double 逐值等价。C4 公式主体、T21 同时溢出分支（Compare.cpp Boost cpp_bin_float 段）、C7 机械转写零改动。

## 数值等价 oracle（改前/改后同向量行为一致）

- 基线（改动前构建）：九目标全绿 `baseline-*.xml`（合计 tests=1393 failures=0 errors=0 skipped=3）。
- 改后复跑：`postfix-*.xml` 同计数 1393/0/3，九文件 testcase 名单逐一经脚本核对全部一致。
- 关键既有断言保持：`CompareFormula.NonFiniteInputsFalse_UT_UNIT`（±Inf/NaN 拒绝）、`FractionRejectedScalingCases_UT_UNIT`/`FiniteOverflowBoundary_UT_UNIT`（B31 oracle.py Fraction 极值冻结结论——原文见 audit 第 13 行）、`ToleranceMake.Validation_UT_UNIT`。
- skipped=3 为 optimization/runtime/selection 各 1 例既有环境性跳过，改前改后同集。

## 消费者面（公共头编译面）

`Compare.hpp` 全部产品 includer 六单元（core/optimization/runtime/selection/testkit/trajectory）的测试/契约目标增量重建并真实运行（`wp15-t37-consumer-rebuild.log`，exit 0）；optimization 的公共头 `Objective.hpp`、selection `Combination.hpp`、testkit `Check.hpp`/`ToleranceProfile.hpp` 经由其测试目标编译覆盖。

## 独立冒烟（gate-all 第 4 步同形态）

- 标准冒烟配方（GOV-T24 确立：vcpkg toolchain＋Qt 前缀＋`RobWork_DIR`＋`VCPKG_OVERRIDE_FIND_PACKAGE_NAME=vcpkg_find_package`）配置成功：`wp15-t37-smoke-configure.log`；`sdurws_ird_core_test` 冒烟构建运行 71/71 全过：`smoke-sdurws_ird_core_test.{xml,log,ird-test-report.json}`。
- 与集成 74 例差值 3 例＝`IRD_CORE_HAS_RW_Q` 条件编译的 Q 重载用例（Compare.hpp 条件包含既有先例，两模式差异如实分记）。
- **不带 `RobWork_DIR` 的裸冒烟整体配置失败**（`wp15-t37-smoke-bare-fail.log`：modeling/CMakeLists.txt:383 `find_package(RobWork)` 断供）——即当前任何可成功配置的冒烟树都带 RobWork 导出包，不存在"无 rw 头路径的可运行冒烟模式"；本批未引入任何条件编译，两模式编译同一源码，无双分支一致性风险。

## ird_gates

`wp15-t37-ird-gates.log`：引擎直跑 exit 0，R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中。

## Deg2Rad 候选：结构不可落地实测与保留登记（所有者待裁决项）

原计划（侦察建议/B31 第 7 行"可直接用已允许 math 常量"）拟将 Units.cpp:38/58/59 三个角度因子换 `rw::math::Deg2Rad`。本批实测该形态在实现层不成立：

1. `RobWork/RobWork/src/rw/math/Constants.hpp:31` `const double Pi = 3.1415926535897932384626433832795;`、`:40` `const double Deg2Rad = Pi / 180;`——上游为 `const double` 非 `constexpr`（C++17 [expr.const]：命名空间作用域 const double 不可用于常量表达式），`constexpr double kDegToRad = rw::math::Deg2Rad;` 及对它的 `static_assert` 均无法编译（依据为头文件原文与标准规则，未另做编译探针）。
2. 产品侧 `core/src/Units.cpp:31` `kUnitTable` 为 `constexpr` 冻结表（token 索引冻结、core 权威，core.md §4.4）；消费上游常量必须把冻结表降级为动态初始化（引入静态初始化顺序暴露），或维护集成/冒烟双份表体（双模式初始化语义分裂、矩阵与冒烟树编译来源不符）。
3. 数值收益为零：B31 已实测两式双精度舍入逐位一致（audit 第 7 行），本次核对成立。

处置：本批 Units.cpp **零改动**；三行在 core.json 以 business-specific/保留登记（differences 记录不可落地证据链）；"接受冻结表动态初始化以物理换用 Deg2Rad"属结构取舍，列所有者待裁决，不自批。附带评估：Units.cpp `isnan||isinf → !isfinite` 可选简化**不纳入**本批——触碰 Units.cpp 触发 COVERAGE 门禁须全行覆盖并解决 17 个 audit-b31 pending 行（含 F-966 OPEN 语境未授权结论面），超出本包局部授权，留后续 Units 审计批；core.json Evaluation 8 行本批零触碰维持 pending。

## 矩阵与凭证同步

- functions/core.json：closeWithin 区段摘要刷新（19-61）；allCloseWithin.vector/Q、runtimeAbsoluteTolerance 区段随位移刷新；`audit-t21.Compare_cpp.declarations` 区段刷新；三个 `core.Tolerance.*` pending 行按 B31 结论解决为 business-specific/migrated＋nonOverlapEvidence；新增 `core.wp15.Compare_hpp.declarations` 行（触碰文件全行覆盖）；core.json/testkit.json 全部引用 Compare.hpp/Compare.cpp 的 inputs 摘要全链刷新（58＋38 处）。
- 逐文件凭证：[reuse/changes/REUSE-T37.json](../../../reuse/changes/REUSE-T37.json)（Digest 口径，business/无能力核心）。
- 门禁机器校验：reuse-contract.ps1 CheckFunctions 的 FRESHNESS 区段摘要、COVERAGE 全行覆盖、inputs 全链绑定均由 validate-task（含 validate-reuse）实际执行。

## 未决事项

- 独立验收未做（不自签）；Deg2Rad 物理换用裁决项与 Units isfinite 简化、Evaluation 行解决均已登记，随验收材料移交所有者。
