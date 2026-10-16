# 框架源码补丁登记（SA-02）

按架构文档 §5〔SA-02〕：力争对 RobWork/RobWorkSim/RobWorkStudio 源码零修改；确需修改时以显式 patch 文件集中登记（本目录），由门禁校验清单与应用状态一致，禁止未登记的框架工作区改动。

| 补丁 | 修改文件 | 内容 | 原因 | 可逆性 | 登记日期 |
| --- | --- | --- | --- | --- | --- |
| 0001-rwslibs-entry-option.patch | `RobWorkStudio/src/rwslibs/CMakeLists.txt` | 追加 `option(RWS_BUILD_INDUSTRIALROBOT ... OFF)`＋守卫的 `add_subdirectory(industrialrobot)` | 产品代码树构建入口（架构 §5 落位；模块方案既定目录约定） | option 默认 OFF，关闭时与基线字节等价；revert 即移除 | 2026-09-09 |
| 0002-rwsimulator-plugin-nominmax.patch | `RobWorkSim/src/rwsimlibs/plugins/RWSimulatorPlugin.cpp` | TU 序言区（首个 `#include` 之前）以 `#ifndef` 守卫定义 `NOMINMAX` | findings.json F-714：fresh 树全量构建下该插件 TU 的 include 链经 OpenGL/宿主头先于 rw/math 与 Eigen 引入 `<windows.h>`，MSVC min/max 函数式宏污染 `rw/math/Math.hpp`（warning C4003 → error C2589/C2059，71 处错误）致编译中断；NOMINMAX 一处防护同时免疫 rw/math 与 Eigen（vcpkg 第三方）两族污染点，是覆盖该 TU 全部错误的最小修法 | 移除 TU 内三行宏守卫即回到基线形态（该插件在 fresh 树编译失败——即 F-714 缺陷态）；对其他 TU 零影响 | 2026-10-16 |
| 0003-industrial-robot-ci-entry-scripts.patch | `RobWork/scripts/industrial-robot/`（新增目录，6 个 .ps1：common/configure/build/run-tests/check-boundaries/package） | CI 六步门禁（configure→build→模型测试→GUI 预检→check-boundaries→package）的入口脚本落位——eb777e2（codex/wp-01-t04-gitlab-gate-r2）参考实现＋今日树适配：configure 增 `-DRWS_BUILD_INDUSTRIALROBOT=ON`／去死选项 IRD_BUILD_BUSINESS_PLUGINS；build/run-tests 改叶子 CTestTestfile 全量测试面发现（46 目标）；run-tests 增逐目标 `--timeout 600`（F-728 防线）与依赖 DLL PATH 供给；check-boundaries 由陈旧静态扫描器替换为 ird_gates 执行器（静态扫描器 R3 白名单对 `_contract_test` 等今日命名必然误报）；package 收集面扩为全单元 | 所有者裁决激活 CI 常驻门禁（gov-ci-activation，2026-10-16）：模板引用的入口脚本按 ci/README 差异表第 2 项正式补建；纯新增目录，零修改既有框架文件 | 删除新增目录即回到基线（对既有框架文件零影响） | 2026-10-16 |

**校验方式**：`git diff -- RobWork/RobWorkStudio/src/rwslibs/CMakeLists.txt` 的输出应与 0001 补丁内容一致（已应用状态）；`git diff -- RobWork/RobWorkSim/src/rwsimlibs/plugins/RWSimulatorPlugin.cpp` 的输出应与 0002 补丁内容一致（已应用状态）；0003 为纯新增目录（`git status -- RobWork/scripts/industrial-robot/` 应无未登记改动，文件清单与补丁一致）；工作区出现上表之外的框架改动即门禁失败（WP-01-T01 承载）。0002 的构建级验证：集成模式 `cmake --build --target RWSimulatorPlugin` 缺陷版失败 71 处 error C2589/C2059（E2-FW 批 `_e2-evidence/integ/f714-rwsimulator-defect-build.log`）、补丁版零错误通过（`f714-rwsimulator-fixed-build.log`）。0003 的运行级验证＝CI 六 job 首次全绿运行（GitHub Actions，2026-10-16 激活后）。

**仓库根元数据落位（非框架源码，随 0003 同批登记）**：`.github/workflows/industrial-robot-windows.yml`（内容＝ci/ 目录 GitHub 模板逐字节复制，`cmp` 校验一致）。仓库根 `.github/` 属仓库元数据、不属 RobWork/RWS/Sim 框架源码，不在 SA-02"零源码修改"约束面内；其落位授权＝所有者 2026-10-16"按建议激活"裁决（ci/README 差异表第 1 项的处置闭环），登记于此备查。
