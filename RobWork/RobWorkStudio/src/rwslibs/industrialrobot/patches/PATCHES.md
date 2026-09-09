# 框架源码补丁登记（SA-02）

按架构文档 §5〔SA-02〕：力争对 RobWork/RobWorkSim/RobWorkStudio 源码零修改；确需修改时以显式 patch 文件集中登记（本目录），由门禁校验清单与应用状态一致，禁止未登记的框架工作区改动。

| 补丁 | 修改文件 | 内容 | 原因 | 可逆性 | 登记日期 |
| --- | --- | --- | --- | --- | --- |
| 0001-rwslibs-entry-option.patch | `RobWorkStudio/src/rwslibs/CMakeLists.txt` | 追加 `option(RWS_BUILD_INDUSTRIALROBOT ... OFF)`＋守卫的 `add_subdirectory(industrialrobot)` | 产品代码树构建入口（架构 §5 落位；模块方案既定目录约定） | option 默认 OFF，关闭时与基线字节等价；revert 即移除 | 2026-09-09 |

**校验方式**：`git diff -- RobWork/RobWorkStudio/src/rwslibs/CMakeLists.txt` 的输出应与 0001 补丁内容一致（已应用状态）；工作区出现上表之外的框架改动即门禁失败（WP-01-T01 承载）。
