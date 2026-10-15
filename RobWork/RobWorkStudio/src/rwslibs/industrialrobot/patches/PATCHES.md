# 框架源码补丁登记（SA-02）

按架构文档 §5〔SA-02〕：力争对 RobWork/RobWorkSim/RobWorkStudio 源码零修改；确需修改时以显式 patch 文件集中登记（本目录），由门禁校验清单与应用状态一致，禁止未登记的框架工作区改动。

| 补丁 | 修改文件 | 内容 | 原因 | 可逆性 | 登记日期 |
| --- | --- | --- | --- | --- | --- |
| 0001-rwslibs-entry-option.patch | `RobWorkStudio/src/rwslibs/CMakeLists.txt` | 追加 `option(RWS_BUILD_INDUSTRIALROBOT ... OFF)`＋守卫的 `add_subdirectory(industrialrobot)` | 产品代码树构建入口（架构 §5 落位；模块方案既定目录约定） | option 默认 OFF，关闭时与基线字节等价；revert 即移除 | 2026-09-09 |
| 0002-rwsimulator-plugin-nominmax.patch | `RobWorkSim/src/rwsimlibs/plugins/RWSimulatorPlugin.cpp` | TU 序言区（首个 `#include` 之前）以 `#ifndef` 守卫定义 `NOMINMAX` | findings.json F-714：fresh 树全量构建下该插件 TU 的 include 链经 OpenGL/宿主头先于 rw/math 与 Eigen 引入 `<windows.h>`，MSVC min/max 函数式宏污染 `rw/math/Math.hpp`（warning C4003 → error C2589/C2059，71 处错误）致编译中断；NOMINMAX 一处防护同时免疫 rw/math 与 Eigen（vcpkg 第三方）两族污染点，是覆盖该 TU 全部错误的最小修法 | 移除 TU 内三行宏守卫即回到基线形态（该插件在 fresh 树编译失败——即 F-714 缺陷态）；对其他 TU 零影响 | 2026-10-16 |

**校验方式**：`git diff -- RobWork/RobWorkStudio/src/rwslibs/CMakeLists.txt` 的输出应与 0001 补丁内容一致（已应用状态）；`git diff -- RobWork/RobWorkSim/src/rwsimlibs/plugins/RWSimulatorPlugin.cpp` 的输出应与 0002 补丁内容一致（已应用状态）；工作区出现上表之外的框架改动即门禁失败（WP-01-T01 承载）。0002 的构建级验证：集成模式 `cmake --build --target RWSimulatorPlugin` 缺陷版失败 71 处 error C2589/C2059（E2-FW 批 `_e2-evidence/integ/f714-rwsimulator-defect-build.log`）、补丁版零错误通过（`f714-rwsimulator-fixed-build.log`）。
