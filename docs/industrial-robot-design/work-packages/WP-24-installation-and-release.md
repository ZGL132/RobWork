# WP-24 安装与发布实施计划

> 阶段/发布：阶段 E / R1+R2（R1、R2 各自生成可回滚包）；人周 5～8（总纲 §5.4）。
> 负责范围：Windows x64 离线安装包、版本并存与升级/回滚、插件白名单、依赖/许可证清单、干净机器冒烟协议、发布检查表与旧安装目标审计。
> 责任分离：打包/演练执行者、发布工程师与安全负责人独立复核（module-design/installation-release.md §8）；验证方式＝真实安装演练记录＋脚本化检查，不以 CTest 目标替代（testing-contract §4）。

**需求与契约：** NFR-MNT-06、NFR-DEP-01～05、NFR-SEC-04～06、§13.4 安装包审计；引用 `architecture/persistence-schema.md`、`architecture/testing-contract.md`；模块方案 `module-design/installation-release.md`（v0.3）。

**拥有目录：** `RobWork/scripts/industrial-robot/`（package/verify/smoke 三脚本）、`RobWork/installer/industrial-robot/`（安装器工程、白名单、依赖清单、licenses）；脚本从仓库根目录解析绝对路径；不修改业务代码或测试门禁。

**输入/输出：** 输入＝Release 构建产物、WP-01 依赖基线、白名单与发布切片；输出＝R1/R2 离线安装包、三清单（白名单/依赖/发布清单）、安装日志、哈希校验与回滚演练记录。

## 1. 目标与非目标

**目标：** 交付可在企业内网离线安装、可并存、可升级、可回滚的 R1/R2 Windows x64 安装包，并以白名单、三清单和干净机演练证明包内容可审计。
- 完成定义：R1/R2 包通过 `verify-package.ps1` 双向校验；干净机 6 步冒烟协议全部通过且留有签署记录；包内与安装目录无开发机绝对路径；旧安装目标审计命中为 0；发布检查表由发布工程师与安全负责人复核签署。

**非目标：** 不建设代码签名能力（NFR-SEC-06 按企业部署策略单独验收，未启用签名时记录例外并保持哈希校验）；不做在线更新与账号（NFR-DEP-03 禁止）；不修改业务代码或测试门禁；安装器不做项目数据迁移（Schema 前向升级由应用按 persistence-schema §5 执行，module-design/installation-release.md §1、§3）。

## 2. 需求、契约与前置

- 需求：NFR-DEP-01～05、NFR-SEC-04～06、NFR-MNT-06；§13.4 安装包审计（旧目标不得进入发布包）。
- 契约：`architecture/persistence-schema.md`（§1 版本口径：R1/R2 同 Schema 主版本）、`architecture/testing-contract.md`（§4～5）。
- 模块方案：`module-design/installation-release.md`（v0.3；本计划对齐其 §2～§9）。
- 代码前置：WP-01（脚本与 CI 门禁）、WP-22（被打包的工作流集成）、WP-23（被打包与被审计的系统测试资产）。

## 3. 拥有目录与脚本

```text
RobWork/scripts/industrial-robot/    package.ps1（本计划入口）  verify-package.ps1  smoke-install.ps1（T01 交付）
RobWork/installer/industrial-robot/  安装器工程（wxs/iss）  plugin-whitelist.json  dependency-inventory.json  licenses/
```

- 脚本兼容 Windows PowerShell 5.1 与 PowerShell 7（testing-contract §5），从仓库根解析绝对路径，不依赖开发机路径。
- CMake 目标：无产品 C++ 目标；`sdurws_ird_installer_test`（本计划登记）只运行脚本化校验（清单/哈希/白名单/路径扫描），不承担真实安装演练，不以 CTest 目标替代真实演练证据（module-design/installation-release.md §2、§8）。

## 4. 离线包结构与版本并存（冻结）

```text
ird-setup-<R1|R2>-<version>-win64/
  setup.msi                       每版本独立 ProductCode
  payload/app/                    主程序、Qt 运行库（windeployqt 产出）、MSVC 运行库
  payload/robwork/                锁定 commit 的 RobWork/RobWorkStudio/RobWorkSim 产物
  payload/plugins/                白名单内业务插件
  payload/catalog-templates/      器件目录模板与示例目录包
  payload/samples/                样例项目（挂接 WP-25 交付物）
  plugin-whitelist.json  dependency-inventory.json  release-manifest.json  LICENSES/
```

- payload 三清单：`plugin-whitelist.json`（§5）、`dependency-inventory.json`（§5）、`release-manifest.json`（包内全部文件清单＋哈希，`verify-package.ps1` 对包与已安装目录双向校验）。
- 安装布局：`%ProgramFiles%\SDURWS\IndustrialRobot\<product-version>\` 按版本独立目录实现并存（NFR-DEP-03）；开始菜单快捷方式指向所选版本；按版本独立卸载，不触碰其他版本与用户项目数据（`.rwdesign` 属用户目录，卸载不删除）。
- 升级：新版本安装到新目录并迁移用户设置（记录迁移日志）；回滚：旧版本目录并存可直接启动，回滚演练要求旧版本仍能打开既有 `.rwdesign`（R1/R2 同 Schema 主版本，persistence-schema §1），为发布检查表必测项。

## 5. 插件白名单与依赖/许可证清单（冻结格式）

- `plugin-whitelist.json`（NFR-SEC-04 显式白名单，不自动扫描加载）：每项 `{relativePath, componentName, version, sha256, license, source}`；加载器逐项校验路径与哈希，未登记或哈希不符拒绝加载并记录诊断（`IRD-INST-WHITELIST-MISMATCH`）。
- `dependency-inventory.json`（NFR-SEC-05/NFR-DEP-05）：组件、版本、许可证、SHA-256 与来源；至少覆盖 RobWork/RobWorkStudio/RobWorkSim 锁定 commit、Qt 与编译器版本、碰撞检测后端及版本、运行库——与 WP-01 依赖基线同源，包内清单为准；缺项触发 `IRD-INST-INVENTORY-INCOMPLETE`，补齐后重验。
- windeployqt 仅作为打包脚本中的 Qt 运行库收集步骤，产出进入 `payload/app/` 并入清单；不引入未登记第三方库。

## 6. 干净机器冒烟测试协议（人工演练＋脚本检查）

| 步骤 | 内容 | 记录 |
| --- | --- | --- |
| 1 前置 | 全新 Windows x64 虚机/实机，无开发工具与仓库路径，断网（离线安装，NFR-DEP-03） | 环境快照 |
| 2 安装 | 运行 setup；脚本断言：安装目录与包内无开发机绝对路径（全包字符串扫描，`IRD-INST-PATH-LEAK`）、白名单与清单哈希全部通过 | 安装日志＋脚本输出 |
| 3 冒烟 | 启动 → 新建/打开样例项目 → 运行一个 Verified 计算 → 生成报告 → 保存重开；系统冒烟由 WP-23 统一入口提供辅助证据（不在本 WP 验证入口中重复） | 操作记录 |
| 4 卸载 | 程序目录清除、用户项目数据保留 | 演练记录 |
| 5 并存 | 安装第二版本，两版本均可启动且互不破坏 | 演练记录 |
| 6 升级与回滚 | 按 §4 演练并记录 | 演练记录＋迁移日志 |

每步产出安装日志、检查脚本输出与演练人签署记录；任一步失败为发布阻断项（module-design/installation-release.md §5）。

## 7. CI 对接与旧安装目标审计

- CI：仓库已有 `RobWork/.gitlab-ci.yml`（stages: build、build_second、build_third、build_doc、test、deploy；include `gitlab-ci/gitlab-ci-windows.yml` 等平台文件）——打包作业挂接在其 Windows runner 与既有 stage 结构上，具体 job 定义由 T01 按 WP-01 CI 门禁补齐，本计划不预置内容；CI 产物为离线包＋`verify-package.ps1` 报告。
- 旧安装目标审计（§13.4/NFR-MNT-06）：发布包必须不含 `sdurws_robotmodelbuilder`、`sdurws_engineeringrequirements`、`sdurws_kinematicanalysis`、`sdurws_structureoptimizer*` 四个旧目标（含通配匹配）与旧格式读取；审计＝对包清单做旧目标名单比对，命中即发布阻断；每阶段提交安装包审计结果。

## 任务

### WP-24-T01 构建测试与边界脚本

- **范围：** 交付 `package.ps1`（本计划验证入口的调用形态）、`verify-package.ps1`、`smoke-install.ps1`；在既有 `RobWork/.gitlab-ci.yml` stage 结构上登记打包 job（内容按 WP-01 CI 门禁）；登记 `sdurws_ird_installer_test` 为脚本化校验目标。
- **前置：** 任务级无；WP 级＝WP-01 脚本与 CI 门禁、WP-23 被打包资产。
- **输出工件：** 三个脚本、CI job 接线、脚本校验目标、按 WP-01 资产口径的标注记录。
- **验收断言：** ①脚本兼容 Windows PowerShell 5.1 与 PowerShell 7 并从仓库根解析路径（module-design/installation-release.md §2）；②`sdurws_ird_installer_test` 只执行清单/哈希/白名单/路径扫描校验（§3）；③CI job 产物＝离线包＋verify 报告（§7）；④脚本交付前按 WP-01 资产口径标注（module-design/installation-release.md §8）。

### WP-24-T02 R1/R2 安装包

- **范围：** 安装器工程（wxs/iss）与 §4 冻结包结构落地：`setup.msi`（每版本独立 ProductCode）、五个 payload 分区、三清单与 `LICENSES/`。
- **前置：** T01；WP 级＝WP-01 依赖基线（锁定 commit）、WP-22 构建产物。
- **输出工件：** `ird-setup-<R1|R2>-<version>-win64/` 离线包、`release-manifest.json`（全文件清单＋哈希）。
- **验收断言：** ①包结构与三清单位置逐项符合 §4 冻结树；②`payload/robwork` 为锁定 commit 产物且与 dependency-inventory 一致；③`verify-package.ps1` 对包与已安装目录双向校验通过；④不依赖开发机路径（NFR-DEP-02）。

### WP-24-T03 安装生命周期

- **范围：** §4 安装布局与 §6 六步协议的真实演练：安装、卸载、并存、升级（含设置迁移日志）、回滚（旧版本打开既有 `.rwdesign`）。
- **前置：** T02。
- **输出工件：** 六步演练记录与签署、迁移日志、回滚证据。
- **验收断言：** ①安装于 `%ProgramFiles%\SDURWS\IndustrialRobot\<product-version>\` 且按版本独立卸载不触碰其他版本与用户 `.rwdesign`（§4）；②断网离线安装（NFR-DEP-03）；③全包与安装目录字符串扫描无开发机绝对路径（§6 步骤 2）；④回滚后旧版本可打开既有项目（persistence-schema §1）；⑤任一步失败登记为发布阻断项。

### WP-24-T04 插件白名单与来源

- **范围：** `plugin-whitelist.json` 与 `dependency-inventory.json` 的编制、加载器校验对接与 `licenses/` 归档；windeployqt 收集步骤纳入清单。
- **前置：** T02。
- **输出工件：** 白名单、依赖/许可证清单、licenses/ 目录、加载器校验报告。
- **验收断言：** ①白名单每项含 `{relativePath, componentName, version, sha256, license, source}` 六字段（§5）；②未登记或哈希不符拒绝加载并记录诊断（NFR-SEC-04）；③依赖清单覆盖锁定 commit、Qt/编译器、碰撞后端与运行库，与 WP-01 基线同源（NFR-DEP-05）；④无未登记第三方库进入 `payload/app/`。

### WP-24-T05 发布检查表与签名

- **范围：** 发布检查表（R1/R2 各一份）；旧安装目标审计执行；签名按企业部署策略处理（NFR-SEC-06，P1：可用时验证安装与升级包完整性，未启用时记录例外并保持哈希校验）。
- **前置：** T02、T03、T04。
- **输出工件：** 发布检查表、旧目标审计结果（每阶段提交）、签名例外记录（如适用）。
- **验收断言：** ①包清单与四个旧目标名单比对命中为 0（§7）；②检查表覆盖 §6 六步签署记录与回滚必测项；③签名处理符合 NFR-SEC-06 双分支口径；④由发布工程师与安全负责人独立复核签署（module-design/installation-release.md §8）。

## 验证

前置：WP-01、WP-22、WP-23。验证＝真实安装演练记录（§6）＋脚本化检查；脚本为 T01 交付，交付前按 WP-01 资产口径标注：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\package.ps1 -Configuration Release -Release R1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\verify-package.ps1 -Package <包路径>
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\smoke-install.ps1 -InstallDir <安装目录>
```

- R2 包将 `-Release R1` 替换为 `-Release R2`；`package.ps1` 调用形态以本计划为准（module-design/installation-release.md §8）。
- 必须提交：安装日志、包清单、白名单报告、哈希校验、卸载/并存/升级/回滚演练记录、旧目标审计、发布检查表（T05）。

## 10. 独立评审与证据

- 由发布工程师与安全负责人独立复核离线安装、白名单、哈希、许可证和回滚证据（module-design/installation-release.md §8）。
- 每份证据含执行人、日期、环境与命令/脚本输出；演练记录须有演练人签署。

## 11. 迁移与删除

| 旧资产 | 处置 | 门禁 |
| --- | --- | --- |
| 旧安装目标与旧格式读取入口 | Delete | 仅允许出现在拒绝测试；R1/R2 包验收后从安装清单与 CI 删除 |
| 既有打包脚本（如存在） | Rewrite | 新脚本通过干净机演练后替换 |

## 退出条件

- NFR-DEP-01～05、NFR-SEC-04～06、NFR-MNT-06 通过。
- R1/R2 均可在干净 Windows x64 机器离线安装、卸载、并存、升级并回滚，六步协议全部留痕。
- 包内三清单完整、哈希双向校验通过、无开发机绝对路径、旧目标审计命中为 0。

## 任务卡索引

- [WP-24-T01 构建测试与边界脚本](../agent-tasks/WP-24-T01-build-scripts.md)
- [WP-24-T02 R1/R2 安装包](../agent-tasks/WP-24-T02-packages.md)
- [WP-24-T03 安装生命周期](../agent-tasks/WP-24-T03-install-lifecycle.md)
- [WP-24-T04 插件白名单与来源](../agent-tasks/WP-24-T04-whitelist.md)
- [WP-24-T05 发布检查表与签名](../agent-tasks/WP-24-T05-release-checklist.md)
