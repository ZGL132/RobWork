# WP-24-T02 R1/R2 安装包

- **Task ID / 需求 ID / ADR / 阶段：** WP-24-T02；NFR-DEP-01/02/03（离线安装、不依赖开发机路径、版本并存）、NFR-SEC-05（依赖清单入包）、§13.4（包内容可审计）；ADR-002（`.rwdesign` 用户数据不进包、卸载保留）；阶段 E / R1＋R2（R1、R2 各自生成可回滚包）。契约：`architecture/persistence-schema.md` §1（R1/R2 同 Schema 主版本）；模块详设 `module-design/installation-release.md` v0.3 §3（冻结包结构）、§4（三清单格式）。验证方式＝真实打包演练记录＋检查表（testing-contract §4），不以 CTest 目标替代。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（installation-release.md v0.3）
- **前置任务及必需工件：** WP-24-T01（`package.ps1`/`verify-package.ps1` 脚本与 CI 接线）；外部：WP-01-T05（依赖基线锁定 commit）、WP-22-T05（被打包的工作流集成 Release 产物）、WP-23-T01（`sdurws_ird_system_test` 冒烟辅助证据由其提供，本卡不重复执行）。
- **允许创建/修改/删除的文件：** 创建 `RobWork/installer/industrial-robot/` 安装器工程（wxs/iss）、`release-manifest.json`（包内全部文件清单＋哈希，随包生成）；写 `RobWork/installer/industrial-robot/evidence/t02-packages/`（打包记录、verify 报告、包结构比对检查表）。不删除文件。
- **禁止修改的文件和公共接口：** WP-01/WP-24 既有脚本（只调用）；业务代码、测试门禁与 WP-23 证据；`requirements.md`、CSV、`architecture/`、`module-design/`；不在包内引入四旧目标与旧格式读取（名单见 WP-24-T05）。
- **修改前接口：** 无安装器工程；无 `ird-setup-<R1|R2>-<version>-win64/` 离线包；`release-manifest.json` 不存在。
- **修改后接口：** installation-release §3 冻结包结构逐项落地：`setup.msi`（每版本独立 ProductCode）；`payload/app/`（主程序＋windeployqt 产出的 Qt 运行库＋MSVC 运行库）；`payload/robwork/`（锁定 commit 的 RobWork/RobWorkStudio/RobWorkSim 产物）；`payload/plugins/`（白名单内业务插件）；`payload/catalog-templates/`（器件目录模板与示例目录包）；`payload/samples/`（样例项目，挂接 WP-25 交付物）；包根 `plugin-whitelist.json`、`dependency-inventory.json`、`release-manifest.json`、`LICENSES/`。升级＝新版本装新目录并迁移用户设置（记录迁移日志）；回滚＝旧版本目录并存可直接启动；安装器不做项目数据迁移（Schema 前向升级由应用按 persistence-schema §5 执行）。
- **实施步骤：**
  1. 编写安装器工程（wxs/iss）：按 §3 冻结树定义组件与安装布局 `%ProgramFiles%\SDURWS\IndustrialRobot\<product-version>\`。
  2. 以 `package.ps1 -Configuration Release -Release R1`（R2 替换 `-Release R2`）产出离线包，核对包结构与三清单位置逐项符合冻结树。
  3. 生成 `release-manifest.json`（全文件清单＋哈希），核对 `payload/robwork` 与 dependency-inventory 锁定 commit 一致。
  4. 运行 `verify-package.ps1 -Package <包路径>` 对包与已安装目录双向校验。
  5. 按检查表逐项复核（见精确验证方式）并写演练记录。
- **RED 测试：** 打包前置环境缺失（无 Release 产物/无锁定 commit 清单）时 `package.ps1` 非零；`verify-package.ps1` 对手工抽换的包文件非零——落地后正式包校验通过。
- **最小实现：** R1/R2 离线包＋三清单＋双向校验记录；白名单内容编制（T04）、安装生命周期演练（T03）、旧目标审计（T05）不在本卡。
- **正常/边界/失败测试：**
  - 正常：Given R1 与 R2 Release 产物，When 分别打包并校验，Then 两包结构逐项符合 §3 冻结树、`setup.msi` ProductCode 独立、双向校验通过。
  - 边界：Given 包内同时存在 R1 版本目录，When 安装 R2，Then 两版本目录并存互不覆盖；Given 样例项目挂接点为空（WP-25 未交付），Then `payload/samples/` 保留占位并在检查表标注，不视为缺项阻断。
  - 失败：Given 包内任一文件哈希与 `release-manifest.json` 不符，When 校验，Then 非零、拒绝出包；Given 包内检出开发机绝对路径，Then 记录 `IRD-INST-PATH-LEAK` 并作为发布阻断项。
- **精确验证方式：**（真实环境打包演练与检查表；脚本调用属打包步骤，非测试入口）
  - 演练记录：两次打包（R1/R2）的 `package.ps1` 运行记录与 `verify-package.ps1` 双向校验输出（含退出码）。
  - 检查表（首项不成立即任务失败）：①包结构八个条目与 §3 冻结树逐项比对一致；②三清单位置正确且 `release-manifest.json` 覆盖包内全部文件；③`payload/robwork` commit 与 WP-01 依赖基线一致；④双向校验退出码 0；⑤全包扫描无开发机绝对路径；⑥不依赖开发机路径复打包（NFR-DEP-02）。
- **diff 和禁止项检查：** `git diff --name-only` 仅含安装器工程、`release-manifest.json` 与 evidence 目录；未修改脚本与业务代码；包内无四旧目标（`sdurws_robotmodelbuilder`/`sdurws_engineeringrequirements`/`sdurws_kinematicanalysis`/`sdurws_structureoptimizer*`）；文件 UTF-8 无 BOM、LF。
- **证据工件：** `RobWork/installer/industrial-robot/evidence/t02-packages/`：R1/R2 打包记录、verify 双向校验报告、包结构比对检查表（签署栏：执行人/日期/环境）、`release-manifest.json`、命令原文与 commit。
- **提交格式：** `WP-24-T02: R1/R2 安装包`

  - 新增 R1/R2 安装包构建脚本与冻结树
  - 新增包结构校验测试
  - 新增运行证据记录
- **停止与升级条件：** Release 产物或 WP-01 依赖基线锁定 commit 缺失、或包结构无法符合 §3 冻结树时，停止并升级工作包所有者；打包执行者不得担任本卡独立验证者（发布工程师独立复核）。
