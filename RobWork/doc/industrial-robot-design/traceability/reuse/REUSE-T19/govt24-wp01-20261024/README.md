# WP-01／GOV-T24：主线准入（IRD-GATE-LIB）与冒烟 RobWork 导出包供给修复记录

实施日期：2026-10-24；分支 redesign-main；起始 SHA `fac3d030a0e301c32972a7bf4564a071fc8e9e0d`（WP-00 收口提交）。
任务契约：[GOV-T24](../../../tasks/GOV-T24.json)。状态＝实施完成待独验（不自签）。

## 问题A：kinematics 三变量被 IRD-GATE-LIB 拒绝

### 修改前真实失败

- `gate-engine-prefail.log`：`cmake -DIRD_ROOT=<industrialrobot> -P cmake/ird_gates.cmake` 引擎直跑，exit 1，`IRD-GATE-LIB` 三命中（`${_ird_kin_rw_math}`/`${_ird_kin_rw_kinematics}`/`${_ird_kin_rw_models}`），17 项自测正负例全过——与 [T19 集成原件](../integration-20261024/a-build.log)同一形态。
- 根因：`ird_gates.cmake` 第 1 步为文本级静态解析（设计前提＝仓库 CMake 风格无变量目标），`target_link_libraries` 中的变量字面 token 不匹配任何已知目标族正则（`^sdurw`/`^GTest::` 等），落入第 2e 步"未知目标族"。实际两模式链接均成功——静态扫描语义与实际解析的差异，非依赖违规。

### 修复（合法接线，门禁代码/词表零改动）

`kinematics/CMakeLists.txt` 上游链接段重写为条件分支显式合法目标：

- 集成分支（`TARGET sdurw_kinematics`）：`sdurw_math sdurw_kinematics sdurw_models` 三真实目标——与原变量解析结果逐字相同，集成链接面零变化。
- 冒烟分支：`sdurw_math`/`sdurw_models` 同名 ALIAS（drivetrain `REUSE-T12`/modeling `REUSE-T07` 既有先例）；`sdurw_kinematics` 不单列——★禁止建立该名冒烟别名（runtime/policy/trajectory/dynamics/optimization/ui/modeling/requirements 与本文件共 8 类以 `TARGET sdurw_kinematics` 作集成模式判定），其符号经 `RW::sdurw_models` 的 `INTERFACE_LINK_LIBRARIES`（targets/sdurw_models.cmake 原生携带 `RW::sdurw_kinematics`/`RW::sdurw_math`）传递到达，链接语义等价。
- 三个 `_ird_kin_rw_*` 选择变量删除；无通用目标解析包装。

`gate-engine-postfix.log`：修复后引擎直跑 exit 0、全检查零命中、17 项自测正负例全过。

## 问题B：独立冒烟 RobWork 导出包供给失败

### 根因（两层，探针全复现）

供给物资本身完好：框架配置期在源码侧生成 `RobWork/RobWork/cmake/RobWorkConfig.cmake`＋`targets/*.cmake`，`build/RobWork/bin|libs/Release` 下 sdurw_* 全量产物在位。失败在消费解析链：

1. **vcpkg 拦截期转义崩溃**：`rwprobe-default-toolchain-fail.log`——vcpkg toolchain 的 `find_package` 拦截宏对 `RobWorkConfig.cmake:108` 的 `find_package(Boost QUIET ... HINTS "C:\local")` 再展开时 `\l` 非法转义直接语法崩溃。冒烟配置必须带 `VCPKG_OVERRIDE_FIND_PACKAGE_NAME=vcpkg_find_package`（T07 既有参数先例）。
2. **框架配置期验证器断链**：`rwprobe-override-verify-fail.log`——`rw_find_components→RW_verify_library`（`find_library(... NO_DEFAULT_PATH)` 只搜框架记录库目录）解析不了 vcpkg Boost 1.92 `boost_atomic-targets.cmake:64` 写入 `Boost::atomic` 接口的裸名 `synchronization`（boost-synchronization 端口未安装；`boost_atomic.dll` 实测零 synchronization.dll 导入——同步原语经 KERNEL32；集成树链接期由 Windows SDK `D:\Windows Kits\10\Lib\10.0.22621.0\um\x64\synchronization.lib` 顺带解析，见集成树 sdurw_models.vcxproj 链接行）与尚未定义的 `Threads::Threads`，递归判定 `sdurw` 元库 "Looking for sdurw - not found" FATAL——与 T19 README 记录的主 checkout 失败同点（当时误记为"构建树路径失效"）。

### 修复（消费边界前置绑定，落 core/CMakeLists.txt 尾段）

落点裁决：最初拟落 modeling/CMakeLists.txt（add_subdirectory 序首个 `find_package(RobWork)` 站点），定向核查发现 modeling/CMakeLists.txt 被 REUSE-T11 两个 **closed** 功能（modeling.rpyToRotation/rpyToEaa）的冻结证据链绑定（矩阵行＋生产报告双冻结，validate-reuse 结构上禁止变更该文件）；industrialrobot 根 CMakeLists.txt 无逐功能矩阵覆盖。**core/CMakeLists.txt** 是首个被处理单元（#1，先于 modeling #11），有全文件逐命令区段覆盖（core.json 233 行、零 closed 行、零 direct/adapter 证据行），尾插零位移既有区段——前置绑定守卫 `if(NOT TARGET sdurw_math AND NOT TARGET RW::sdurw_math)` 集成模式整体跳过、零影响：

```cmake
find_package(Threads REQUIRED)
if(NOT TARGET synchronization)
    add_library(synchronization INTERFACE IMPORTED)
endif()
```

空接口目标承载 `synchronization` 名字＝消费侧零链接语义变化（该名字在 DLL 消费链上无符号需求）。

### 探针验证（修复有效性，全部真实执行）

- `rwprobe-prebound-configure-ok.log`：带前置绑定＋RobWork_DIR＋VCPKG_OVERRIDE 配置成功（`Found RobWork`，RW::sdurw/RW::sdurw_math/RW::sdurw_kinematics/RW::sdurw_models 全可用）。
- `rwprobe-prebound-build-ok.log`：探针 exe（include rw/math+rw/kinematics+rw/models 头、链接三 RW:: 目标）Release 构建成功。
- `rwprobe-prebound-run-ok.log`：exe 运行成功（exit 1＝探针自身 `return (int)q.size()` 的 Q(1) 语义，非崩溃）。

## 产品文件与复用凭证

- `kinematics/CMakeLists.txt`＋`core/CMakeLists.txt`：GOV-T24 凭证（[reuse/changes/GOV-T24.json](../../../reuse/changes/GOV-T24.json)，business/无能力核心）。
- 逐功能矩阵同步：functions/kinematics.json（UpstreamLinks 整文件区段行号/摘要、ird_add_gtest 宏区段行号〔内容 sha 不变〕、45 处 inputs/nonOverlap 摘要、FkEvaluator 证据绑定与自绑定）；functions/core.json（新增 GOV_T24_smoke_supply 区段行）；permissions.json（3 处 REUSE-T19 build 绑定摘要）。
- modeling 侧矩阵/证据/权限**零触碰**（落点裁决回退——closed 冻结链保持原样）；历史凭证（changes/REUSE-T19.json、REUSE-T07.json）按设计保留旧值，现行哈希由 GOV-T24 凭证承载。

## 验证与移交

见本目录各日志原件；集成增量构建/测试与冒烟树结果随实施补录于下方"实施验证结果"。实施完成待独验；WP-02/算法替换未开始；不自签、不置 done。

## 实施验证结果

全部真实执行，命令/环境/退出码以本目录原件为准（VS2022 x64、vcpkg toolchain、Qt 前缀 `D:/software/Qt/6.11.1/msvc2022_64`）：

| 项 | 结果 | 证据 |
| --- | --- | --- |
| 门禁引擎直跑（修复前） | **FAIL**：3×IRD-GATE-LIB（三变量字面 token），17 项自测全过，exit 1 | gate-engine-prefail.log |
| 门禁引擎直跑（修复后） | **PASS**：全检查零命中，17 项自测正负例全过，exit 0 | gate-engine-postfix.log |
| validate-reuse | **PASS**（governance only：inventory=1401 changed=83） | validate-reuse-final.log |
| 集成增量构建＋ird_gates 目标 | **PASS**：kinematics test/contract 目标＋门禁（引擎＋validate-reuse）exit 0 | int-build-gates-final.log |
| 集成 kinematics 测试 | **292 tests，failed=0，skipped=1**（T19 已登记预存 envUnavailable，与本包无关），exit 0 | int-kinematics-test.xml/.log/-report.json |
| 集成 kinematics 契约测试 | **57/57 passed**，failed=0，exit 0 | int-kinematics-contract.xml/.log/-report.json |
| 探针（问题B三阶段） | 默认 toolchain FAIL（转义崩溃）/带 override FAIL（Looking for sdurw）/带前置绑定 **PASS**（配置＋构建＋运行） | rwprobe-*.log |
| 冒烟全树配置（20 单元） | **PASS**：T07 引入 find_package(RobWork) 以来首次，`Found RobWork`，exit 0 | smoke-configure.log（参数含 RobWork_DIR＋VCPKG_OVERRIDE_FIND_PACKAGE_NAME） |
| 冒烟构建 | **PASS**：sdurws_ird_kinematics_plugin（RW include 供给验证）＋sdurws_ird_modeling_test＋sdurws_ird_drivetrain_test（导出包链接消费），exit 0 | smoke-build.log |
| 冒烟 modeling 测试 | **372/372 passed**（含 T07 CouplingReuse 上游 SVD 消费用例——真实链接 RW::sdurw_math），exit 0 | smoke-modeling-test.xml/.log/-report.json |
| 冒烟 drivetrain 测试 | **42/42 passed**（RW::sdurw_math 链接消费），exit 0 | smoke-drivetrain-test.xml/.log/-report.json |
| 冒烟 kinematics test exe | **NOT RUN（阻断）**：T19 遗留缺口——Fk.cpp 两模式编译但 runtime WorkCellConstView 实现集成专属（架构设计：模型真值归 runtime 编译链），冒烟 exe 链接缺 findFrame/findDevice 符号；修复该缺口属 T19 语义（冒烟桩或源集调整），不在本包授权内。受影响消费者：sdurws_ird_kinematics_test/_contract_test 冒烟构建 | smoke-kinematics-test-blocked.log（LNK2019×2：runtime WorkCellConstView::findFrame/findDevice 无冒烟实现） |

集成与冒烟共用的门禁扫描均零命中；未运行 UI 全套（与本包无关）；不宣称算法/独验通过。
