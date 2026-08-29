# WP-24-T01 构建测试与边界脚本

- **Task ID / 需求 ID / ADR / 阶段：** WP-24-T01；NFR-MNT-06（旧安装目标审计的脚本承载）、NFR-DEP-01/02（离线包构建不依赖开发机路径）、NFR-SEC-04～05（校验脚本承载）、§13.4；ADR-002（`.rwdesign` 用户目录脚本断言不触碰）。阶段 E / R1＋R2。契约：`architecture/testing-contract.md` §4～§5（Windows PowerShell 5.1/7 兼容）、`architecture/persistence-schema.md` §1；模块详设 `module-design/installation-release.md` v0.3 §2（脚本与 `sdurws_ird_installer_test` 口径：只运行脚本化校验，不承担真实安装演练）、§7（CI 对接与旧目标审计）。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（installation-release.md v0.3）
- **前置任务及必需工件：** 任务级无；WP 级：WP-01-T03/T04（`common/configure/build/run-tests/check-boundaries.ps1` 既有脚本与 CI 门禁——本卡扩展不重写）、WP-22-T05 与 WP-23-T01（被打包与被审计资产，Late 绑定不阻塞脚本交付）。
- **允许创建/修改/删除的文件：** 创建 `RobWork/scripts/industrial-robot/package.ps1`（WP-24 调用形态）、`verify-package.ps1`、`smoke-install.ps1`、`RobWork/installer/industrial-robot/test/InstallerScriptTest.cpp`；修改 `RobWork/installer/industrial-robot/CMakeLists.txt`（登记 `sdurws_ird_installer_test`，只运行脚本校验）、`RobWork/.gitlab-ci.yml`（按 WP-01 CI 门禁追加打包 job，不动既有 stage 结构）；写 `RobWork/installer/industrial-robot/evidence/`。不删除文件。
- **禁止修改的文件和公共接口：** WP-01 既有五脚本（`common/configure/build/run-tests/check-boundaries.ps1` 只调用）；业务代码与测试门禁；`requirements.md`、CSV、`architecture/`、`module-design/`；脚本不得自动删除既有构建目录、不引入 offscreen 或 GUI 并行。
- **修改前接口：** `package.ps1`/`verify-package.ps1`/`smoke-install.ps1` 不存在；`sdurws_ird_installer_test` 未登记；CI 无打包 job。
- **修改后接口：** 三脚本兼容 Windows PowerShell 5.1 与 PowerShell 7、从仓库根解析绝对路径（不依赖开发机路径）：`package.ps1 -Configuration Release -Release <R1|R2>`（按 installation-release §3 冻结包结构产出 `ird-setup-<R1|R2>-<version>-win64/`）；`verify-package.ps1 -Package <包路径>`（按 `release-manifest.json` 对包与已安装目录双向校验，输出 `IRD-INST-WHITELIST-MISMATCH`/`IRD-INST-INVENTORY-INCOMPLETE`/`IRD-INST-PATH-LEAK` 诊断）；`smoke-install.ps1 -InstallDir <安装目录>`（断言安装目录无开发机绝对路径、白名单与清单哈希通过）。CI job 产物＝离线包＋verify 报告；`sdurws_ird_installer_test` 只执行清单/哈希/白名单/路径扫描脚本校验，不以 CTest 目标替代真实安装演练。
- **实施步骤：**
  1. 三脚本按 WP-01 资产口径标注（module-design/installation-release.md §8）。
  2. 实现 `package.ps1`：Release 产物收集→五个 payload 分区→三清单位置→包命名。
  3. 实现 `verify-package.ps1`：包内与已安装目录逐文件哈希双向校验、白名单/清单/路径扫描三项检查。
  4. 实现 `smoke-install.ps1`：安装目录字符串扫描（`IRD-INST-PATH-LEAK`）与哈希断言。
  5. 登记 `sdurws_ird_installer_test`（InstallerScriptTest 调用三脚本的校验路径）并接线 CI 打包 job。
  6. 在 PowerShell 5.1 与 7 双环境执行验证命令，写证据。
- **RED 测试：** 实现前三脚本调用均非零（入口缺失）；`sdurws_ird_installer_test` 未登记时构建失败；落地后全部通过。
- **最小实现：** 三脚本＋CI job 接线＋脚本校验目标；白名单/依赖清单内容编制（T04）、真实安装演练（T03）不在本卡。
- **正常/边界/失败测试：**
  - 正常：Given Release 构建产物与 `-Release R1`，When 运行 `package.ps1`，Then 产出 §3 冻结结构包且 `verify-package.ps1` 双向校验退出码 0。
  - 边界：Given `-Release R2`，Then 包名与 ProductCode 独立、与 R1 包可并存路径不冲突；脚本在 PowerShell 5.1 与 7 均通过。
  - 失败：Given 包内注入开发机绝对路径样本，When 运行 `smoke-install.ps1`，Then 非零并报告 `IRD-INST-PATH-LEAK`；Given manifest 缺文件，Then `verify-package.ps1` 非零并列出缺失项。
- **精确验证命令：**（仓库根；脚本真实调用形态，按 D6 计划 installation-release §8）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1`（WP-01 边界扫描复验脚本资产：offscreen/GUI 并行规则）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\package.ps1 -Configuration Release -Release R1`
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\verify-package.ps1 -Package <包路径>`
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\smoke-install.ps1 -InstallDir <安装目录>`
  - `cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_installer_test`；`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_installer_test$"`（只跑脚本校验）
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单；`RobWork/.gitlab-ci.yml` 仅追加打包 job（既有 stage/include 不变）；脚本无 PowerShell 7 专属语法、无 offscreen/GUI 并行、无自动删除构建目录；未改 WP-01 既有脚本与业务代码。
- **证据工件：** `RobWork/installer/industrial-robot/evidence/t01-build-scripts.log`：三脚本 5.1/7 双环境运行日志（含退出码）、`sdurws_ird_installer_test` 输出、CI job 产物清单（离线包＋verify 报告）、资产口径标注记录、命令原文与 commit。
- **提交格式：** `WP-24-T01: 构建测试与边界脚本`
- **停止与升级条件：** WP-01 脚本参数契约或 CI stage 结构无法承载打包 job、或脚本校验必须修改业务代码才能通过时，停止并升级工作包所有者；脚本实现者不得担任本卡独立验证者（发布工程师独立复核）。
