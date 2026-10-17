# 源码区 Agent 规则（industrialrobot/AGENTS.md）

| 字段 | 值 |
| --- | --- |
| 文档版本 | v1.0（2026-10-17，随 Agent-native 治理 Task 2 建立） |
| 用途 | 产品代码区（20 单元）的**本地硬规则速查**：只收"动代码时每次都要自查的红线与测试规则"。全量规则以仓库根 `AGENTS.md` 为准——本文件引用不复制，防止两份文本漂移 |
| 上游 | 仓库根 `AGENTS.md`（协作/编码/分支红线总入口，冲突时以根规则为准） |
| 姊妹分区 | 设计区 `RobWork/doc/industrial-robot-design/AGENTS.md`（文档链权威关系与阅读路径）——写代码前需要的语义（需求条目/单元卡章节/任务契约）按该区阅读路径取用 |

---

## 1. 语言与命名（根 §3 全量）

- **C++17**（core.md D-01，不用 C++20）；各目标显式 `CXX_STANDARD 17`。
- 命名空间 `sdurws::ird::<unit>`；库目标 `sdurws_ird_<unit>`、别名 `RWS::ird::<unit>`；类型/函数 `PascalCase`、变量 `camelCase`、成员 `m_`/`_` 前缀。
- 公共头放 `include/sdurws/ird/<unit>/`，include guard `#ifndef <PROJ>_<Path>_HPP`；**跨单元只允许 include 公共头（R-2）**。

## 2. 架构红线速查（根 §5 全量，违者 review 直接打回）

1. L2 计算内核**零 Qt**（core/evidence/policy/runtime 及业务计算库禁 include Qt 头）。
2. 业务域单元**互链禁止**（R-1）：跨单元协作只走六类端口（命令/查询/评估器/策略/事件/名称）。
3. **框架零源码修改**（SA-02）：确需改 RobWork/RWS 源码，先更新 `patches/PATCHES.md` 登记 patch，后改动。
4. 不做 RobWork 名称前缀拼接/剥离（R-4）；权威唯一（PA-1）；修订不可变（PA-2）；不继承 `old/` 历史实现。

## 3. 注释契约（根 §2 全量）

详细中文注释是**代码验收的一部分**：文件头（设计依据＋背景）、类/函数（Doxygen 风格中文描述，参数方向/单位/取值/异常）、内部语义段落、高危信息六表（物理单位/坐标系/角度制式/所有权/线程约束/错误语义/确定性来源）。没有合格注释的代码视为未完成。

## 4. 测试规则（四类目录 × 所有权 × 证据）

测试按单元分散组织（无顶层 tests/ 目录）：

| 目录 | 测试目标 | 内容 | 命名与追溯 |
| --- | --- | --- | --- |
| `<unit>/test/` | `sdurws_ird_<unit>_test` | 单元测试（googletest，经 vcpkg） | 用例名带需求/AT 追溯字段（根 §2.7） |
| `<unit>/contract_test/` | `sdurws_ird_<unit>_contract_test` | 跨单元端口契约测试 | 同上 |
| `<unit>/gui_test/` | `sdurws_ird_<unit>_gui_test` | GUI 测试（Qt Widgets） | 同上＋Windows GUI 规则（§5） |
| `<unit>/perf_test/` | `sdurws_ird_<unit>_perf_test` | 性能测试 | 协议与基准清单按设计区 `performance-baseline.md` |

**"集成"口径**＝双模式构建的**集成模式门禁**（仓库根 build/ + `RWS_BUILD_INDUSTRIALROBOT=ON`，唯一交付口径，根 §4.1）——集成模式零错误是 DoD 第 1 条，仅独立冒烟通过不构成任务完成。

**所有权**（根 §6.4 三段式）：实施者写测试并真实执行留痕 → 验收者在全新上下文重配重跑验证 → 合入由所有者决策。**任何未执行的测试不得标注通过；失败如实登记原因。**

**证据输出**（根 §4.2）：gtest XML ＋ `ird-test-report.json` ＋ 构建日志，落 `doc/industrial-robot-design/traceability/` 留痕；证据文件**只增不改**。

**测试替身（mock）政策**（治理原则"真实验证优先"，review 层执法——验收时核查，不做 lint）：

- 测试替身**只允许打在六类端口边界**（命令/查询/评估器/策略/事件/名称），即昂贵或副作用型依赖处（如跨单元编译调用、写路径）；现范例＝modeling 单元的 `MockCompilePort`（观测"零编译调用"契约）。
- 计算逻辑、数学算法、数据变换**一律真实验证**，不得打替身绕过。
- 手写 fake 优先（计数观测/桩实现），**不引入 gmock 依赖**——当前依赖面零 gmock，保持经 vcpkg 的最小依赖纪律。
- 单元内替身＋契约层真接线两层合起来才构成完整验证：跨单元真实交互由 `<unit>/contract_test/` 覆盖，替身不得渗透到契约测试。

## 5. Windows GUI 测试规则

- 工具链 Visual Studio 2022 x64；`QT_QPA_PLATFORM=windows`，**禁止 offscreen**；路径用绝对路径；一次只跑一个 GUI 可执行文件（避免窗口焦点/输入串扰）。
- 依据：治理计划 Task 5（该规则将随任务落成 `ird-qt-gui-windows` skill，本节届时改为引用）。

## 6. 构建自查（每次提交前）

- 双模式构建：集成模式（构建前 `grep RWS_BUILD_INDUSTRIALROBOT build/CMakeCache.txt` 确认 `:BOOL=ON`）＋ 独立冒烟（必带 vcpkg toolchain 与 Qt 前缀两参数，根 §4.1）。
- `ird_gates` 零命中；测试已执行留痕。
- 第三方依赖一律经 vcpkg，禁止 vendor 或第二渠道。
