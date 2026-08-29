# 安装与发布模块详细方案（installation-release）

- 方案版本：v0.3；需求基线：v0.8；架构检查点：`IRD-D2-20260829`
- 负责 WP：WP-24；阶段/发布：阶段 E / R1+R2（R1、R2 各自生成可回滚包）；任务卡：`agent-tasks/WP-24-T01～T05`
- 契约：`architecture/persistence-schema.md`（§1 版本口径）、`architecture/testing-contract.md`（§4～5）；需求：NFR-DEP-01～05、NFR-SEC-04～06、NFR-MNT-06、§13.4 安装包审计
- 代码前置：WP-01（脚本与 CI 门禁）、WP-22、WP-23（被打包与被审计对象）

## 1. 模块职责与非目标

交付 Windows x64 离线安装包、版本并存与升级/回滚、插件白名单、依赖/许可证清单、干净机器冒烟协议、发布检查表与旧安装目标审计。非目标：代码签名能力建设（按企业部署策略单独验收，NFR-SEC-06；未启用签名时记录例外并保持哈希校验）、在线更新与账号（NFR-DEP-03 禁止）、修改业务代码或测试门禁。验证方式＝真实安装演练记录＋脚本化检查，不以 CTest 目标替代（testing-contract §4）。

## 2. 拥有目录与脚本（v0.2 裁决保留）

```text
RobWork/scripts/industrial-robot/    package.ps1（WP 计划入口）  verify-package.ps1  smoke-install.ps1（T01/T05 交付）
RobWork/installer/industrial-robot/  安装器工程（wxs/iss）  plugin-whitelist.json  dependency-inventory.json  licenses/
```

脚本兼容 Windows PowerShell 5.1 与 PowerShell 7（testing-contract §5），从仓库根解析绝对路径，不依赖开发机路径。CMake target：无产品 C++ 目标；`sdurws_ird_installer_test`（WP-24 计划登记）只运行脚本化校验（清单/哈希/白名单/路径扫描），不承担真实安装演练。

## 3. 离线包结构与版本并存（冻结）

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

- 安装布局：`%ProgramFiles%\SDURWS\IndustrialRobot\<product-version>\` 按版本独立目录实现并存（NFR-DEP-03）；开始菜单快捷方式指向所选版本；按版本独立卸载，不触碰其他版本与用户项目数据（`.rwdesign` 属用户目录，卸载不删除）。
- 升级：新版本安装到新目录并迁移用户设置（记录迁移日志）；项目 Schema 前向升级由应用按 persistence-schema §5 执行，安装器不做数据迁移。
- 回滚：旧版本目录并存可直接启动；回滚演练要求旧版本仍能打开既有 `.rwdesign`（R1/R2 同 Schema 主版本，persistence-schema §1），为发布检查表必测项。

## 4. 插件白名单与依赖/许可证清单（冻结格式）

- `plugin-whitelist.json`（NFR-SEC-04 显式白名单，不自动扫描加载）：每项 `{relativePath, componentName, version, sha256, license, source}`；加载器逐项校验路径与哈希，未登记或哈希不符拒绝加载并记录诊断。
- `dependency-inventory.json`（NFR-SEC-05/NFR-DEP-05）：组件、版本、许可证、SHA-256 与来源；至少覆盖 RobWork/RobWorkStudio/RobWorkSim 锁定 commit、Qt 与编译器版本、碰撞检测后端及版本、运行库——与 WP-01 依赖基线同源，包内清单为准。
- `release-manifest.json`：包内全部文件清单＋哈希；`verify-package.ps1` 对包与已安装目录双向校验。
- windeployqt 仅作为打包脚本中的 Qt 运行库收集步骤，产出进入 `payload/app/` 并入清单；不引入未登记第三方库。

## 5. 干净机器冒烟测试协议（人工演练＋脚本检查）

1. 前置：全新 Windows x64 虚机/实机，无开发工具与仓库路径，断网（离线安装，NFR-DEP-03）。
2. 安装：运行 setup；脚本断言：安装目录与包内无开发机绝对路径（全包字符串扫描）、白名单与清单哈希全部通过。
3. 冒烟：启动 → 新建/打开样例项目 → 运行一个 Verified 计算 → 生成报告 → 保存重开；系统冒烟（`run-tests.ps1 -Configuration Release -Regex '^sdurws_ird_system_test$'`）作为辅助证据。
4. 卸载：程序目录清除、用户项目数据保留。
5. 并存：安装第二版本，两版本均可启动且互不破坏。
6. 升级与回滚：按 §3 演练并记录。
每步产出安装日志、检查脚本输出与演练人签署记录；任一步失败为发布阻断项。

## 6. CI 对接与旧安装目标审计

- CI：仓库已有 `RobWork/.gitlab-ci.yml`（stages: build…deploy，include `gitlab-ci/gitlab-ci-windows.yml` 等平台文件）——打包作业挂接在其 Windows runner 与既有 stage 结构上，具体 job 定义由 T01 按 WP-01 CI 门禁补齐，本文不预置内容；CI 产物为离线包＋`verify-package.ps1` 报告。
- 旧安装目标审计（§13.4/NFR-MNT-06）：发布包必须不含 `sdurws_robotmodelbuilder`、`sdurws_engineeringrequirements`、`sdurws_kinematicanalysis`、`sdurws_structureoptimizer*` 等旧目标与旧格式读取；审计＝对包清单做旧目标名单比对，命中即发布阻断。每阶段提交安装包审计结果。

## 7. 错误码（提名，经 WP-09 目录登记后用于脚本与加载器报告）

| 码 | 触发条件 | 类别 | severity | 处置 |
| --- | --- | --- | --- | --- |
| `IRD-INST-PATH-LEAK` | 包或安装目录含开发机绝对路径 | System | Error | 清理打包环境后重打包；发布阻断 |
| `IRD-INST-WHITELIST-MISMATCH` | 插件未登记或哈希不符 | System | Error | 拒绝加载/拒绝发布 |
| `IRD-INST-INVENTORY-INCOMPLETE` | 依赖/许可证清单缺项 | System | Error | 补齐后重验，不得发布 |

## 8. 验证与证据（非 CTest）

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\package.ps1 -Configuration Release -Release R1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\verify-package.ps1 -Package <包路径>
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\smoke-install.ps1 -InstallDir <安装目录>
```

`package.ps1` 调用形态以 WP-24 计划为准；`verify-package.ps1`/`smoke-install.ps1` 为 T01 交付，交付前按 WP-01 资产口径标注。提交：安装日志、包清单、白名单报告、哈希校验、卸载/并存/升级/回滚演练记录、旧目标审计、发布检查表（T05），由发布工程师与安全负责人独立复核（WP 计划）。

## 9. 迁移与删除表

| 旧资产 | 处置 | 门禁 |
| --- | --- | --- |
| 旧安装目标与旧格式读取入口 | Delete | 仅允许出现在拒绝测试；R1/R2 包验收后从安装清单与 CI 删除 |
| 既有打包脚本（如存在） | Rewrite | 新脚本通过干净机演练后替换 |
