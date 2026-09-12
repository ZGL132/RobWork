# AGENTS.md — 项目协作与编码说明

> 本文件是 AI 编码助手（ZCode/Claude/Codex 等）在本仓库工作的**入口约定**。
> 与 `RobWork/doc/industrial-robot-design/` 下的设计文档冲突时：**需求与架构语义以设计文档为准，编码行为约定以本文件为准**。

---

## 0. 一句话定位

本仓库 = **RobWork 机器人框架**（上游基线，来自 SDU，**零源码修改**）＋ **工业机械臂设计软件**（`industrialrobot/`，按 REQUIREMENTS/ARCHITECTURE 从头构建的产品代码）。

**当前活跃开发区域只有一个**：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/`（下称 `industrialrobot/`）。框架其余部分（`RobWork/`、`RobWorkSim/`、`RobWorkStudio/` 其他源码）除非有 patches 登记，一律视为只读。

**★ 分支红线（先读）**：唯一开发分支是 **`redesign-main`**；`main` 为旧主分支，已冻结（停在 `49a9e81`）**不再维护，禁止一切新提交**——详见 §6.2。

---

## 1. 目录速览

| 路径 | 内容 | 可否修改 |
| --- | --- | --- |
| `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/` | **产品代码**（20 个单元骨架，逐步落地源码） | ✅ 主要开发区 |
| `RobWork/doc/industrial-robot-design/` | 设计文档链（REQUIREMENTS → ARCHITECTURE → DETAILED-DESIGN → units/*.md → development-task-breakdown.md） | ✅ 按任务卡修订 |
| `RobWork/scripts/industrialrobot/` | 文档/任务验证脚本（validate-docs / validate-task / verify-task） | ✅ 随治理任务修订 |
| `RobWork/docs/superpowers/` | AI 执行规划文档（plans/specs） | ✅ 辅助文档，小步修订 |
| `RobWork/`（其余源码） | RobWork / RobWorkStudio / RobWorkSim 框架 | ❌ 零源码修改（SA-02，确需修改先登记 patch） |
| `vcpkg/` | 第三方依赖（经典模式） | ❌ 只读 |
| `build/` | 集成模式构建树（VS2022 x64） | 构建产物 |
| `start_studio.bat` | RobWorkStudio 启动脚本 | 辅助脚本 |

---

## 2. ★ 核心编码要求：详细中文注释（最高优先级）

**本项目所有新写的产品代码（`industrialrobot/` 下）必须附带详细、易懂的中文注释，目的是让后续人工 review 的工程师不看设计文档也能读懂代码意图。** 这不是可选项，是代码验收的一部分——没有合格注释的代码视为未完成。

### 2.1 总体原则

1. **注释语言为中文**。专有名词、API 名、单位符号（m、rad、N·m、SHA-256）可保留英文，但句子用中文写。
2. **解释"为什么"，而不是复述"做什么"**。`i++; // i 加一` 是废注释；`i++; // 跳过分隔符，格式见 REQUIREMENTS MDL-03` 才是有价值的注释。
3. **面向第一次读本代码的工程师**。假设读者懂 C++ 和机器人学基础，但不知道本项目的需求编号、设计决策和业务背景——注释要把这些"代码之外的知识"补上。
4. **与设计文档建立追溯**。涉及需求条目（如 ARC-01、CON-05）、架构决策（SA-xx）或任务编号（WPnn-Tkk）的逻辑，在注释中标注出处，review 时可对照文档。
5. **注释随代码更新**。改了逻辑不改注释比没有注释更糟。

### 2.2 文件头注释（每个 `.hpp` / `.cpp` 必须有）

```cpp
/**
 * @file   Identity.hpp
 * @brief  模型身份（Identity）——产品的"数字指纹"载体。
 *
 * 设计依据：
 *   - 需求 ARC-04（身份可追溯）、CON-05（内容寻址）
 *   - 任务卡 units/core.md §4.1（WP-03-T02）
 *
 * 背景说明：本软件中每个被评估的模型快照都必须携带不可伪造的身份，
 * 五元组（模型名+版本+参数摘要+单位制+时间戳）经 SHA-256 摘要后
 * 作为证据链的关联键。本文件定义该五元组及其计算规则。
 */
```

### 2.3 类与函数注释（Doxygen 风格，中文描述）

框架源码用 `@brief / @param [in] / @return` 的 Doxygen 风格，**产品代码沿用该风格，但描述用中文写**：

```cpp
/**
 * @brief 计算模型参数向量的 SHA-256 摘要（内容寻址身份的哈希部分）。
 *
 * 摘要输入是"规范化后的参数串"而非原始字符串——单位换算、浮点格式化
 * 都已在前置步骤完成，保证同语义模型必得同摘要（确定性 NFR-COR-01）。
 *
 * @param params [in] 规范化参数串；必须已按 CON-05 §2 排序，调用方负责
 * @param seed   [in] 混入摘要的会话种子；同一任务内固定，用于区分
 *               不同计算批次（可为 0，表示不参与身份）
 *
 * @return 32 字节摘要的十六进制字符串（小写，64 字符）
 *
 * @throws std::invalid_argument 若 params 为空串（空模型没有评估意义，
 *         属于调用方错误，应 fail-fast 而不是返回全零摘要）
 *
 * 复杂度：O(n)，n 为参数串长度。10 万参数以内实测 < 5 ms（WP-03-T02 验收数据）。
 */
std::string computeDigest(const std::string& params, uint64_t seed);
```

**要求**：
- 每个**类/结构体**：说明它代表什么业务概念、生命周期由谁管理、是否线程安全。
- 每个**函数**（含私有函数）：`@brief` 一句话说清职责；参数逐个注明方向（`[in]/[out]/[in,out]`）、单位、取值范围与默认值语义；返回值说明含义与失败时的行为；会抛出的异常及触发条件。
- **接口（公共头文件）的注释是契约文档**，review 者据此判断实现是否符合设计，必须写完整。

### 2.4 函数内部逻辑注释

- **每个语义段落（不是每行）**加注释说明这一段在做什么、为什么这么做。
- **复杂算法必须逐步讲解**：IK 求解、优化迭代、事务回滚、位姿变换链这类逻辑，在关键步骤处写清"当前处于算法第几步、这一步的数学/业务含义、失败会走到哪"。
- **分支与边界**：每个 `if` 分支说明进入条件代表什么业务场景；循环的终止条件、溢出/空集等边界情况注明。
- **魔法数字必须解释来源**：不是"把 0.001 改名为 kEpsilon"，而是说明"0.001 rad ≈ 0.057°，是轨迹段连续性检查的容差上限（需求 TRJ-05 规定）"。

```cpp
// 第二步：回滚未完成的事务段。
// 十段编译链（RT-T11）中任何一段失败，已执行段必须全部回退，
// 保证"双编译原子性"（MDL-06）：要么新旧模型同时切换成功，要么都保持原状。
// 注意回滚本身也可能失败，此时进入"污染"状态并上报对应的稳定诊断码
// （如 RT-WC-COMPILE-FAILED，登记于 diagnostics StableCodeRegistry），
// 不允许静默吞掉——项目文件宁可标记为需修复，也不能处于未知状态（Q2 失败可恢复）。
rollbackExecutedSegments();
```

### 2.5 必须注明的高危信息（缺一项即 review 不通过）

| 信息类型 | 注释要求 | 示例 |
| --- | --- | --- |
| **物理单位** | 所有物理量的注释必须带单位，杜绝隐式约定 | `double mass; // 质量，单位 kg` |
| **坐标系/参考系** | 位姿、向量必须说明在哪个坐标系下 | `// 法兰坐标系 {F} 下表示，{F} 定义见 ARCH §7.3` |
| **角度制式** | rad 还是 deg 必须显式写出 | `// 关节角，单位 rad（不是度！）` |
| **所有权与生命周期** | 裸指针/引用注明谁负责释放；快照注明不可变性 | `// 调用方持有，本函数不接管所有权` |
| **线程约束** | 非线程安全的共享状态注明使用限制 | `// 非线程安全：仅任务执行线程访问（TASK-02）` |
| **错误语义** | 错误码/异常的归类（调用方错误 vs 环境错误）。本项目诊断码为**单元前缀助记码**（PRJ-/RT-/EX-/RPT-…，登记于 diagnostics StableCodeRegistry），不存在 DX-xxx 数字码段 | `// RT-CACHE-INCOMPATIBLE = 版本类稳定码（数据错误）；对已关闭上下文的调用 = 调用方契约违约，走异常 fail-fast` |
| **确定性来源** | 影响可复现性的种子、排序、舍入策略 | `// 排序键：名称 asc → 修订号 asc（NFR-COR-02 稳定排序）` |

### 2.6 反面示例（禁止出现的注释）

```cpp
// ❌ 复述代码
count++;        // count 加一

// ❌ 无信息量的占位
// TODO: 以后完善    （任务卡已明确的 TODO 必须带任务编号：// TODO(WP-08-T03): ...）

// ❌ 中英混杂的机翻腔
// 得到 the digest of 模型

// ❌ 过期注释（代码已改为 SHA-256，注释还说 MD5）
// 计算 MD5 摘要
std::string computeDigest(...);  // 实际是 SHA-256
```

### 2.7 适用范围

- **产品代码**（`industrialrobot/`）：全量执行上述注释规范。
- **测试代码**（`*_test.cpp`）：用例名与断言处需中文说明"本用例验证哪条需求/验收标准（AT 编号）"，fixture 与辅助函数按 2.3 写。
- **CMake 脚本**：关键步骤（目标注册、依赖红线、门禁逻辑）用中文注释块说明，参照现有 `industrialrobot/CMakeLists.txt` 的写法。
- **框架源码**：不修改，自然也不加注释。

---

## 3. 代码风格与技术约束

| 项 | 约定 |
| --- | --- |
| C++ 标准 | **C++17**，各目标显式 `CXX_STANDARD 17`；不用 C++20（core.md D-01） |
| 命名空间 | 产品代码统一 `sdurws::ird::<unit>`（如 `sdurws::ird::core`），对应头文件路径 `include/sdurws/ird/<unit>/` |
| 目标命名 | 库 `sdurws_ird_<unit>`、别名 `RWS::ird::<unit>`；测试/插件/工作进程后缀 `_test / _contract_test / _plugin / _worker` |
| 类名/函数名 | 框架惯例：`PascalCase` 类型与函数、`camelCase` 变量、`m_`/`_` 前缀成员——与所在单元既有代码保持一致 |
| 头文件 | include guard 用 `#ifndef <PROJ>_<Path>_HPP` 风格（参照框架 `RWS_ArcBallController_HPP`）；公共头放 `include/`，私有实现头不跨单元暴露 |
| 错误处理 | 遵循各单元任务卡错误语义：调用方错误 fail-fast（断言/异常），环境错误走诊断码登记；禁止吞错 |
| 第三方依赖 | 一律经 vcpkg（仓库根、经典模式）；**禁止** vendor 源码或引入第二渠道；新增依赖先在 development-task-breakdown.md 登记 |
| 测试框架 | googletest，经 `find_package(GTest CONFIG REQUIRED)` 接入；测试名带需求/AT 追溯字段（gtest 已随 WP-02-T01 经 vcpkg 安装于 `installed/x64-windows`，各 `_test` 目标可直接解析） |

---

## 4. 构建与验证

### 4.1 双模式构建（development-task-breakdown.md §5.1）

```bash
# 集成模式（唯一交付口径）：仓库根 build/ + RWS_BUILD_INDUSTRIALROBOT=ON
# ⚠ build/ 构建树初始配置于基线 31b8184 时代（缓存中无该选项）；现已重新配置开启，
#   缓存当前为 RWS_BUILD_INDUSTRIALROBOT:BOOL=ON——构建前仍须 grep 确认（构建树被重置/重配时可能回到默认 OFF）：
cmake -S RobWork -B build -G "Visual Studio 17 2022" -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON
grep RWS_BUILD_INDUSTRIALROBOT build/CMakeCache.txt   # 确认输出 :BOOL=ON 再构建
cmake --build build --config Release

# 独立冒烟模式：仅验证目标注册与 include 路径，脱离 RobWorkStudio。
# ⚠ 必须带 vcpkg toolchain 参数：单元 CMake 含 find_package(GTest CONFIG REQUIRED)（DTB §5.5），
#   裸命令会因找不到 GTest 配置失败（CORE-T01 验收 G-2 / findings F-002）：
cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B <任意临时目录> \
  -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build <该目录>
```

- 工具链：MSVC 2022 x64（Visual Studio 17 2022 生成器），Qt 与 vcpkg 依赖已就位。
- 治理脚本调用：`pwsh -File RobWork/scripts/industrialrobot/<脚本>.ps1`（pwsh 7 与 Windows PowerShell 5.1 均可——脚本对仓库根做自检解析，与调用方式无关；口径登记于 DTB §5.1）。
- **仅冒烟通过不构成任务完成**；集成模式零错误是 DoD 第 1 条。
- 运行 RobWorkStudio：仓库根 `start_studio.bat`。

### 4.2 验证留痕（DoD 第 3 条）

测试必须真实执行并留痕（gtest XML + `ird-test-report.json` + 构建日志）。**任何未执行的测试不得标注通过；失败如实登记原因，不带病前进。**

---

## 5. 架构红线（每个任务的默认禁止项，违者 review 直接打回）

以下红线来自 `ARCHITECTURE.md` / `development-task-breakdown.md` §5.3，编码时逐条自查：

1. **L2 计算内核零 Qt**：core/evidence/policy/runtime 及各业务计算库不得 include 任何 Qt 头；仅 ui 界面目标允许 Widgets。
2. **业务域单元互链禁止（R-1）**：modeling/requirements/kinematics/trajectory/dynamics/selection/optimization 之间不直接依赖，跨单元协作只走六类端口（命令/查询/评估器/策略/事件/名称）。
3. **跨单元私有头禁止（R-2）**：只允许 include 其他单元的公共头（`include/sdurws/ird/<unit>/`）。
4. **框架零源码修改（SA-02）**：确需改 RobWork/RWS 源码时，先更新 `industrialrobot/patches/PATCHES.md` 并产出 patch 文件，后改动。
5. **不做 RobWork 名称前缀拼接/剥离（R-4）**：名称解析统一归 runtime。
6. **权威唯一（PA-1）**：修订与写路径→project；运动学真值与名称→runtime；共享策略→policy；单位/诊断文案→core/diagnostics；命令入口→ui。不要在自己单元里"顺手"实现别人的语义。
7. **不可变历史（PA-2）**：修订只增不改；撤销/重做产生新修订，不覆盖旧数据。
8. **不继承、不恢复 `old/` 历史实现**；一切实现按需求与设计文档从零编写。

---

## 6. 文档链与任务工作流

### 6.1 权威文档（写代码前先读对应章节）

```
RobWork/doc/industrial-robot-design/
├── REQUIREMENTS.md                    # 需求语义与验收标准（唯一权威，ID 如 ARC-01）
├── ARCHITECTURE.md                    # 20 单元组成、分层、端口、决策 SA-xx
├── DETAILED-DESIGN.md                 # 20 单元详设索引
├── units/<unit>.md                    # 单元内部接口/数据模型/状态机/错误语义/测试任务
├── development-task-breakdown.md      # WPnn-Tkk 任务拆分 + 构建约定（§5）
├── traceability/                      # 需求追踪矩阵
└── tasks/                             # 任务执行留痕
```

**规则**：不重定义、不收窄、不扩大需求语义；实现与任务卡有偏差必须按 DTB §5.4 登记（单元卡增量修订），不允许"代码先行、文档失实"。

### 6.2 分支与提交（★ 含分支红线）

**★ 分支红线：`main` 是旧主分支，已冻结不再维护——禁止向 `main` 提交（commit）、合并（merge）或推送（push）任何新内容**，本地与远程 `origin/main` 均不得改动；本仓库在新项目接入时曾错误地把新项目提交到 `main`，该状态已回退，不得重演。当前**唯一开发主线是 `redesign-main`**，一切后续改动只能在 `redesign-main` 及其任务分支上进行：

- 分支：一切工作基于 `redesign-main`；实现任务使用短生命周期分支 `wp<nn>-t<kk>`（如 `wp03-t02`），DoD 达成后合入 `redesign-main`；纯文档修订可直接在 `redesign-main` 小步提交。
- 提交信息：`[WP-nn-Tkk] <摘要>`（代码与其测试同一提交）；文档修订 `[DTB] v0.x: <摘要>` 或对应文档代号；治理文件（AGENTS.md 等）修订用 `[docs] <摘要>`（沿用仓库既有实践）。文档链其余修订沿用既有前缀：单元卡 `[units]`、架构文档 `[ARCH]`（ARCHITECTURE/DETAILED-DESIGN 事实修正）、追踪矩阵/任务契约/验证脚本 `[governance]`。
- 完成状态随实现提交登记，不积压。
- 若发现改动误落到了 `main`（如切错分支）：**立即停止并在 `redesign-main` 侧报告**，不得在 `main` 上叠加修正提交，也不得擅自强推改写远程分支历史。

### 6.3 ★ 每次修改代码后必须提交并同步远程

**每次代码修改完成后，必须编写详细、易懂的 commit 并立即提交、推送（push）到远程仓库 `origin`。** 不允许积压多轮改动后合并成一个含糊的大提交，也不允许本地提交后不推送——远程 `origin` 上的状态即交付状态。

#### 提交时机与粒度

- **一次修改一次提交**：一个任务/一处完整改动对应一个提交；代码与其配套测试、文档同步、CMake 改动放**同一提交**（保持每个提交可独立构建、可回溯）。
- **提交前自查**：双模式构建通过、`ird_gates` 零命中、测试已执行留痕（§4.2）——失败的中间状态不要提交；确实需要保存半成品时，在提交信息正文明确标注"WIP：尚未验证，缺 xxx"。注意：`ird_gates` 门禁随 WP-01-T01 落地，落地前该自查项**如实标注"未执行——门禁未建成"**，不得静默跳过。

#### commit 信息写法（详细、易懂，供人工 review 与回溯）

格式沿用仓库既有风格：**中文标题行＋要点列表正文**，正文让不看 diff 的同事也能明白这次改了什么、为什么改、怎么验证的。

```
[WP-03-T02] 实现 core 单元模型身份与 SHA-256 摘要

改动内容（What）：
- 新增 Identity.hpp/.cpp：模型身份五元组（模型名/版本/参数摘要/单位制/时间戳）
  及其规范化序列化规则
- 新增 Digest.hpp/.cpp：SHA-256 摘要计算，含 FIPS 180-2 已知向量自检
- core/CMakeLists.txt：sdurws_ird_core 由 INTERFACE 升级为 STATIC，
  链接 sdurw_math，注册 _test 目标

改动原因（Why）：
- 满足需求 ARC-04（身份可追溯）、CON-05（内容寻址），设计依据 units/core.md §4.1
- STATIC 升级是 CORE-T01 既定落位动作，目标名不变

验证（How）：
- 集成模式构建零错误；独立冒烟模式通过（日志见 tasks/ 留痕）
- UT-ID-T 12/12、UT-ID-D 6/6 通过（含 FIPS 180-2 标准向量）
- ird_gates 零命中

影响面与兼容性：
- 仅新增文件，无既有行为变化；RWS::ird::core 别名保持不变
```

**要求**：
1. **标题行**：`[任务编号] 动词开头的中文摘要`（≤ 50 字左右），一眼看出这个提交做了什么。
2. **正文必含四段**：改动内容（按文件/模块列要点）、改动原因（对应需求 ID / 任务卡章节）、验证结果（构建/测试/门禁的真实结论，未执行的如实写"未执行"）、影响面（是否改既有行为）。
3. **不写空话**："修复 bug""优化代码""更新文件"这类没有任何信息量的提交信息禁止出现。
4. **如实陈述**：验证结论与实际执行情况一致，测试失败就写失败及原因，不粉饰。

#### 推送远程

- 每次提交后立即 `git push` 到 `origin`（任务分支推 `origin wp<nn>-t<kk>`；合入 `redesign-main` 后推 `origin redesign-main`；**任何情况下不得推送 `main`**）。
- 推送失败（网络/权限/非快进）时：先 `git pull --rebase` 解决分歧再推；仍然失败则**停在原地报告**，不得用 `--force` 强推覆盖远程历史。
- 禁止改写已推送的提交历史（不 rebase/amend 已在远程的提交）。

### 6.4 实施循环：三段式（实施 → 验收 → 合入；DTB §5.7＋acceptance-protocol.md）

编码任务按三段推进，**实施者不得自行合入 `redesign-main`**：

1. **实施段**（实施者会话，单会话单契约）：读任务契约与输入文档章节 → 在 `wp<nn>-t<kk>` 分支实现（含 §2 中文注释）→ 双模式构建 → 门禁（`ird_gates`，随 WP-01-T01 落地；落地前如实标注未执行）→ 测试执行留痕 → 文档同步 → 编写详细 commit 并推送 `origin`（§6.3）→ 发出验收请求（任务 ID/分支/commit 范围/证据位置）。
2. **验收段**（验收者会话，**全新上下文**）：按 `doc/industrial-robot-design/acceptance-protocol.md` 执行对抗式验收——重配重跑构建与 verify 命令、红线与注释规范核查、验证测试真实失败能力；产出 pass/fail＋证据写入 `traceability/acceptance/`。fail 时实施者按验收记录返工后重新发起全量验收。
3. **合入段**（所有者）：pass 后由维护者决策合入 `redesign-main` 并推送（动作可委托，决策不可）；双方分歧与语义拿不准登记 DTB §4 待所有者裁决。

**任何一步失败停在原地登记，不带病前进。**

---

## 7. 沟通与交付约定

- 与用户交流、提交信息摘要、任务留痕均使用**中文**。
- 汇报实现结果时如实区分：已通过（附证据）/ 未执行 / 失败（附原因）。
- 涉及需求语义拿不准时，回查 `REQUIREMENTS.md` 条目原文，不凭记忆或常识推断。
