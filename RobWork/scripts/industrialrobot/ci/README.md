# CI 门禁模板（WP-01-T02 落位，2026-09-10）

本目录承载 **CI 常驻门禁的契约模板**（ARCHITECTURE §11.2 第 2 条），两份文件与既有探索分支结论 **逐字节一致**（对齐基准＝commit `eb777e2`〔codex/wp-01-t04，2026-08-31 所有者裁决双文件口径〕，`cmp` 校验通过——契约 acceptance 第 4 条"对齐"成立，无差异改写）：

| 文件 | 角色 | 出处（eb777e2 原路径） |
| --- | --- | --- |
| `industrial-robot-windows.gitlab-ci.yml` | GitLab 门禁契约定义（Runner 接入前豁免实际执行） | `RobWork/gitlab-ci/industrial-robot-windows.yml` |
| `industrial-robot-windows.github-workflow.yml` | GitHub Actions 执行通道（步骤顺序/job 划分/缓存白名单/脚本行与 GitLab 侧逐字符一致） | `.github/workflows/industrial-robot-windows.yml` |

> **2026-10-12 治理批扩面（G-1 消账，pipeline5/redesign-main）**：两文件测试 job（模型测试／GUI 规则通道预检）的 run-tests 正则由 `^sdurws_ird_core_test$`（阶段 A 唯一已建测试目标的历史口径）**同步**扩为 `^sdurws_ird_.*_test$`——覆盖全部单元 `sdurws_ird_<unit>_test` 与 `_contract_test` 注册目标，与本地 gate-all.ps1"全部 _test/_contract_test 一键执行"面拉齐；双文件逐字符一致纪律保持。扩面依据＝units/execution.md §15.4 v0.12（EX-T10 ④）、units/runtime.md §15.4 v0.15（RT-T13 ④）、units/evidence.md EV-T12 三处同缺口登记（消账标注已随批回写三卡）。

## 已固化结论（缓存回退键与模板供给——契约 acceptance 4 的对齐内容）

1. **双 ini 模板供给**：CI 配置期缺失的两个 gitignored 模板（`RobWorkStudio.ini.template.static`／`ini.shared.in`）由基建步内嵌同文生成（与主树模板逐字节一致，eb777e2 验证）——同时消解 findings **F-007** 的"全新树配置卡死"半边（ini 模板项；toolchain/Qt 前缀项由 workflow 的 `CMAKE_PREFIX_PATH` env＋vcpkg 安装步承载）。
2. **缓存回退键**：`restore-keys` 前缀回退（workflow 变更导致 cache key 变化时命中前缀即跳过依赖安装；保存步 `cache-hit != 'true'` 才写）。
3. **缓存白名单**：仅 `.cache/industrial-robot/{dependencies,packages}/`，不缓存测试结果/结果库/项目快照；失败工件 `when: always`。
4. **六条门禁行**：configure → build → 模型测试 → GUI 规则通道预检（`QT_QPA_PLATFORM=windows`）→ check-boundaries → package。

## 与现状的差异说明（激活前置，非本模板缺陷）

| # | 差异 | 激活处置 | 状态 |
| --- | --- | --- | --- |
| 1 | 两文件的**落位目标路径在框架树/仓库根**（`RobWork/gitlab-ci/`、`.github/workflows/`），超出本契约 allowedFiles——SA-02：框架侧文件先登记 `industrialrobot/patches/PATCHES.md` 再改动 | 所有者扩契约＋补丁登记后，将本目录两文件复制至原路径（内容零改动） | **GitHub 通道已于 2026-10-16 闭环**：`.github/workflows/industrial-robot-windows.yml` 落位（内容＝修订后模板逐字节复制，`cmp` 一致）；仓库根元数据不属框架源码，授权与登记见 PATCHES.md「仓库根元数据落位」段。GitLab 侧维持模板态（Runner 未接入，豁免执行） |
| 2 | 模板引用的入口脚本为 `RobWork\scripts\industrial-robot\{configure,build,run-tests,check-boundaries,package}.ps1`（连字符命名，eb777e2 世界），现行治理脚本目录为 `scripts/industrialrobot/`（无连字符）且入口脚本未建 | 激活时由所有者裁决：或按模板口径补建入口脚本（推荐薄封装 `gate-all.ps1`），或统一改路径——二选一，登记于补丁说明 | **2026-10-16 裁决并落地**：按模板口径补建入口脚本——落位 eb777e2 参考实现（探索分支已验证）并做今日树适配，而非薄封装 gate-all（后者为不可拆分单体脚本，与六步分阶段契约语义不匹配）。适配明细见下节 |
| 3 | 集成默认分支保护（main 仅接受六步全绿合并）依赖仓库设置，非文件可承载 | 仓库设置项，激活后操作员配置 | **待操作员在 GitHub 仓库设置中配置**（分支保护规则：redesign-main 要求本 workflow 六 job 全绿） |

## 2026-10-16 激活（gov-ci-activation，所有者裁决"按建议激活"）

CI 常驻门禁正式激活。两份模板与部署文件的同批修订（逐字符一致纪律保持）：

1. **触发器收窄**（仅 GitHub 执行通道）：`on: push` 无过滤＋`pull_request: main` →
   `push: redesign-main`＋`pull_request: redesign-main`＋`workflow_dispatch`。理由＝main 已冻结（AGENTS §6.2）、
   无过滤 push 会对全部任务分支的每次推送触发多小时运行。GitLab 侧 `rules:` 保持"任意分支＋MR"未动（豁免执行态，触发面待 Runner 接入时随接入契约裁决）。
2. **依赖供给补 gtest＋pugixml＋expat**（双文件镜像，脚本行一致）：今日树各单元 CMake 含
   `find_package(GTest CONFIG REQUIRED)`（DTB §5.5，自 WP-02-T01 起）、
   `find_package(pugixml CONFIG REQUIRED)`（modeling 单元）、`find_package(expat CONFIG REQUIRED)`
   （io 单元）；2026-08-31 验证运行时点 core_test 尚非 gtest 测试、RWS_BUILD_INDUSTRIALROBOT 补丁（0001，2026-09-09）未落地，
   且 modeling/io 两单元尚未进 CI 视野，故当时三项均无需求。
   **首跑实证（run 38936681670，2026-10-16）**：configure 于 modeling 单元 `find_package(pugixml)` 失败
   （job 耗时 1h20m——fresh 依赖树全量源码编译，缓存随后按 `if: always()` 保存）；pugixml/expat 即本条修订依据，
   盘点口径＝全单元 `find_package` REQUIRED 清单（GTest/Qt6/Boost/Eigen3/libzip 原清单已覆盖）。
   **二跑实证（run 38947428868）**：缓存回退命中首跑旧树后，供给步守卫包 `libzip` 在位→短路跳过安装→
   pugixml 仍缺失。**三跑实证（run 38948093498）**：守卫改 pugixml 后 configure 通过（2m15s），但 build job
   于框架库（sdurw_core）编译缺 `boost/spirit` 等仅头文件库——供给清单原只列五个编译型 boost 库，8 月参考
   运行仅构建 core 双目标、框架库不进编译图故未暴露。**四跑实证（run 38949126339）**：补 15 个 boost 端口
   后守卫（boost_tuple 探针）仍被旧树传递依赖预装满足而短路，同点失败。**五跑实证（run 38950026889）**：
   供给步改无条件执行后命令整条未生效——修订时把 `#` 说明行写进了 `run: >-` 折叠标量内，YAML 折叠把全部
   行折叠成一行且以 `#` 开头，PowerShell 整行视为注释，安装命令被吞（步骤假绿秒过）。终态修订：
   - 依赖清单补 15 个 boost 端口（框架源码全量 include 扫描盘点：algorithm/asio/bind/function/graph/
     lambda/math/mpl/multi-index/numeric-conversion/property-tree/random/serialization/spirit/tuple）；
   - **供给步无条件执行 vcpkg install，守卫整个移除**——单文件/单包探针被旧树或其传递依赖满足即短路，
     三轮实证无可靠探针；vcpkg 对已装端口秒级校验跳过、仅补装缺口，无条件执行对一切缓存状态自愈
     （代价＝每 job 数十秒校验）。今后扩依赖清单只改清单本身；
   - **教训（CI yml 改写规范）**：`run: >-`／`- >-` 折叠标量内禁止写 `#` 说明行——会被折叠为脚本注释
     吞掉整条命令；说明一律放标量外的 YAML 注释位。校验口径＝解析 YAML 后打印 run 脚本文本确认命令
     完整且不以 `#` 开头（本节同批已执行并登记）。
3. **入口脚本落位** `RobWork/scripts/industrial-robot/`（6 文件，eb777e2 参考实现＋今日树适配，0003 号补丁登记）：
   - `common.ps1`：与 eb777e2 逐字节一致（VS x64 环境发现/原生日志执行/Qt 冲突检测）。
   - `configure.ps1`：固定选项增 `-DRWS_BUILD_INDUSTRIALROBOT=ON`（0001 号补丁 option，不传则子树不进构建图）、
     去死选项 `IRD_BUILD_BUSINESS_PLUGINS`（全树零消费者）。
   - `build.ps1`：构建面由硬编码 core 双目标扩为全量测试面（叶子 CTestTestfile 发现，与 gate-all 第 3 步同源）。
   - `run-tests.ps1`：执行由构建根单次 ctest 改为叶子发现＋逐目标 ctest（`--timeout 600`，F-728 防线；
     今日树 enable_testing 位于单元作用域，根 ctest 看不到叶子测试）；增依赖 DLL PATH 供给（vcpkg 动态 triplet）；
     移植 gate-all F-715 SKIP 口径（python313.dll 不可达时 ui 系目标记 SKIP 不判 FAIL——本地全量面实测
     `sdurws_ird_ui_test` 即此签名，退出码 8）。GUI 规则面逐字保留。
   - `check-boundaries.ps1`：由 eb777e2 静态正则扫描器整体替换为 **ird_gates 执行器**——静态扫描器 R3 白名单
     停留在 `_test` 单段命名，对今日 `_contract_test/_gui_test/_perf_test` 必然误报"未登记 target"；ird_gates 是
     实际维护的边界权威（gate-all 第 2 步同款调用）。
   - `package.ps1`：收集面由 core 单库扩为全单元 `sdurws_ird_*`（.lib/.dll）＋各单元 include/ 公共头；
     manifest 黑名单与"不执行全树 cmake --install"红线逐字保留。

首跑基线：2026-08-31 成功运行（run 33370296151）六 job 共 5m51s，但当时仅 1 个测试目标且无框架补丁；
今日面（46 测试目标＋20 单元＋框架补丁全量）首跑预计显著更长（fresh 无缓存时依赖供给约 1h 量级），首跑数据登记后作为后续调优基线。

## 本地强制门槛

`pwsh -File RobWork/scripts/industrialrobot/gate-all.ps1`（ird_gates＋双模式构建＋全部 `_test`/`_contract_test` 目标一键执行；零 Python）。CI 已于 2026-10-16 激活，但**本地强制门槛身份不变**（AGENTS §4.2 验证留痕义务不因 CI 存在而免除；CI 与本地门禁互为独立第二双）。
