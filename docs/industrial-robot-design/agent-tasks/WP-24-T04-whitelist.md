# WP-24-T04 插件白名单与来源

- **Task ID / 需求 ID / ADR / 阶段：** WP-24-T04；NFR-SEC-04（插件显式白名单，不自动扫描加载）、NFR-SEC-05（依赖与许可证清单）、NFR-DEP-05（与 WP-01 依赖基线同源）、§13.4；无直接关联 ADR。阶段 E / R1＋R2。契约：`architecture/testing-contract.md` §4～§5；模块详设 `module-design/installation-release.md` v0.3 §4（白名单六字段与依赖清单冻结格式）、§7（`IRD-INST-WHITELIST-MISMATCH`/`IRD-INST-INVENTORY-INCOMPLETE` 错误码）。验证方式＝脚本化校验（`sdurws_ird_installer_test` 只跑脚本校验）＋真实包校验演练记录。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（installation-release.md v0.3）
- **前置任务及必需工件：** WP-24-T02（R1/R2 包与 payload 分区就位）、WP-24-T01（`verify-package.ps1` 与 `sdurws_ird_installer_test` 校验入口）；外部：WP-01-T05（依赖基线：组件/版本/许可证/SHA-256/来源同源数据）；工件：T02 包结构与三清单位置验收通过。
- **允许创建/修改/删除的文件：** 创建/修改 `RobWork/installer/industrial-robot/plugin-whitelist.json`、`dependency-inventory.json`、`licenses/`（许可证归档文件）；修改 `RobWork/installer/industrial-robot/test/InstallerScriptTest.cpp`（追加白名单/哈希/清单校验用例）；写 `RobWork/installer/industrial-robot/evidence/t04-whitelist/`。不删除文件。
- **禁止修改的文件和公共接口：** 加载器与业务代码（白名单校验为既有/对接行为，本卡不改码）；WP-01 依赖基线 JSON（只读比对，包内清单为准）；三脚本（只调用）；`requirements.md`、CSV、`architecture/`、`module-design/`。
- **修改前接口：** `plugin-whitelist.json`/`dependency-inventory.json` 内容为空或非冻结格式；`licenses/` 未归档；`sdurws_ird_installer_test` 无白名单校验用例。
- **修改后接口：** `plugin-whitelist.json` 每项六字段 `{relativePath, componentName, version, sha256, license, source}`（显式白名单，不自动扫描加载；加载器逐项校验路径与哈希，未登记或哈希不符拒绝加载并记录 `IRD-INST-WHITELIST-MISMATCH`）；`dependency-inventory.json` 至少覆盖 RobWork/RobWorkStudio/RobWorkSim 锁定 commit、Qt 与编译器版本、碰撞检测后端及版本、运行库（与 WP-01 依赖基线同源，包内清单为准；缺项触发 `IRD-INST-INVENTORY-INCOMPLETE`，补齐后重验）；`licenses/` 归档与清单 license 字段一一对应；windeployqt 产出并入 `payload/app/` 清单，无未登记第三方库。
- **实施步骤：**
  1. 从 WP-01 依赖基线与 Release 构建产物提取组件/版本/哈希，编制两清单与 `licenses/` 归档。
  2. 校验白名单六字段逐项完整（sha256 与包内实际文件一致）。
  3. 追加 InstallerScriptTest 用例：未登记插件、哈希不符、清单缺项三分支。
  4. 重打包并入清单（`package.ps1`），`verify-package.ps1` 复验白名单与哈希。
  5. 写校验报告证据。
- **RED 测试：** 清单为空或六字段缺失时 `sdurws_ird_installer_test` 白名单用例非零；落地后三分支用例（未登记拒绝/哈希不符拒绝/缺项报 `IRD-INST-INVENTORY-INCOMPLETE`）全部按预期。
- **最小实现：** 两清单＋licenses 归档＋校验用例；不改加载器实现、不做安装演练（T03）、不出发布检查表（T05）。
- **正常/边界/失败测试：**
  - 正常：Given 白名单内全部插件已登记且哈希一致，When 加载器逐项校验，Then 全部加载、`verify-package.ps1` 退出码 0。
  - 边界：Given windeployqt 收集的 Qt 运行库新 DLL，When 复核清单，Then 已并入 `payload/app/` 清单且来源字段登记；Given licenses 目录新增许可证文本，Then 与清单 license 字段一一对应。
  - 失败：Given 未登记插件或哈希不符样本，When 校验，Then 拒绝加载并记录 `IRD-INST-WHITELIST-MISMATCH`（System/Error，拒绝发布）；Given 依赖清单缺碰撞后端版本项，Then `IRD-INST-INVENTORY-INCOMPLETE`（Input/Error），补齐后重验。
- **精确验证命令：**（仓库根；脚本真实调用＋脚本校验目标）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\verify-package.ps1 -Package <包路径>`（白名单与清单哈希逐项通过）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\package.ps1 -Configuration Release -Release R1`（清单更新后重打包；R2 同理）
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_installer_test`；`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_installer_test$"`（只跑清单/哈希/白名单/路径扫描脚本校验）
- **diff 和禁止项检查：** `git diff --name-only` 仅含两清单、`licenses/`、InstallerScriptTest 与 evidence 目录；未改加载器/业务代码/三脚本；白名单逐项六字段齐全且与包内文件哈希一致；无未登记第三方库进入 `payload/app/`；文件 UTF-8 无 BOM、LF。
- **证据工件：** `RobWork/installer/industrial-robot/evidence/t04-whitelist/`：白名单/依赖清单六字段核对表、三分支校验用例输出（含两个错误码触发记录）、licenses 归档清单、与 WP-01 依赖基线同源比对记录、verify 报告、命令原文与 commit。
- **提交格式：** `WP-24-T04: 插件白名单与来源`
- **停止与升级条件：** 加载器白名单校验能力缺失或行为与 §4 冻结语义不符（未登记/哈希不符必须拒绝加载）时，停止并升级工作包所有者——本卡不得为通过校验而修改业务代码；WP-01 依赖基线与实际产物哈希不一致时停止上报，不自行改写基线。
