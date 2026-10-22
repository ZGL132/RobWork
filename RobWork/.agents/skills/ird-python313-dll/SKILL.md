---
name: ird-python313-dll
description: 工业机械臂项目 python313.dll 运行期缺失的处置规程——构建后启动 exe 弹「找不到 python313.dll」/退出码 0xC0000135/gate-all 报 python 前置不可达时的根因机理、三级解法（环境变量固化＞参数注入＞PATH 前置）与 SKIP+归因口径。本 Skill 是该问题处置细节的唯一 home（DTB §5.1「fresh 树运行时前置」③行的操作面展开）。
---

> **GOV-T21 生效约束（2026-10-22）**：执行根 AGENTS.md §0.1 H1—H6；先读 `reuse/policy.md`、`reuse/migration.md`、`reuse/catalog.json` 与逐文件 `reuse/inventory.json`（路径相对设计根）。直接 API > 薄适配 > 最小上游扩展 > 新实现。后两项先提交缺口证据并取得所有者明确批准。旧“从零/自持内核/冒烟缺库所以重写”不构成重复实现许可；历史 done 不等于复用合规。新任务/PR 必须附逐文件复用证据和真实门禁结果；治理独立验收 pass 前不得进入适配/算法变更。

# ird-python313-dll：python313.dll 运行期缺失处置规程

**时机**：构建后运行 `sdurws_ird_*` 测试/产品 exe 时弹 Windows 对话框「无法启动此程序，因为计算机中丢失 python313.dll」、进程退出码 `0xC0000135`（STATUS_DLL_NOT_FOUND），或 gate-all 输出「[python 前置] python313.dll 全程不可达」并给出 SKIP＋归因行。

## 先读懂：这不是编译错误，是构建环境敏感性缺陷（不要去改代码）

根因链（findings F-428/F-440/F-828 实测定案）：

1. RobWork 框架 CMake 的 Python 检测（`RWS_ENABLE_PYTHON`/`RWS_USE_PYTHON3` → `find_package(PythonLibs 3)`）**随操作员 PATH/注册表当时有无 Python 而成功或失败**。
2. 检出系统 Python 时，框架 `sdurws` 静态库接口面把 `python313.dll` 导入拖给**所有**下游链接 exe——且该 DLL 不是 CMake 感知目标、不随 `TARGET_RUNTIME_DLLS` 部署；python313.dll 自身还同目录相依 zlib.dll（conda 发行形态）——复制单文件无效，只能整目录 PATH 供给。
3. 于是**同源码不同环境构建产物导入面不一致**：配置时环境里有 Python 的树，产物启动即缺 DLL；没有的树一切正常。主树存量交付缓存（`BUILD_sdurws_pythoneditor=OFF` 时代）不受影响——**不要随意重置/重配主构建树缓存**（AGENTS §4.1 只要求 grep 确认 `RWS_BUILD_INDUSTRIALROBOT=ON`，其余缓存保持原样）。

推论：fresh 树/worktree/验收 detached 树重新配置时最易复现（gate-all 冒烟树、独立冒烟配置）。**收到该报错时的正确姿势是按下文供给或按 SKIP 口径处置，绝不能当作产品代码缺陷去修。**

## 硬规则（保证者结果作废）

1. **处置优先级固定**：环境变量固化（一劳永逸）＞ gate-all 参数注入（单次）＞ 临时 PATH 前置（仅当前 shell）。不许跳级用一次性的。
2. **零静默**：受影响目标在 gate-all 中按 F-715 口径 SKIP＋归因（`[python 前置]` 行有重跑指引）——SKIP 是环境前置不可达的诚实登记，**不算失败也不能无视**；验收/提交信息里如实转录 SKIP 行与归因，不得粉饰为 PASS。
3. **禁止下列动作**：向 `build/` 或安装树手抄单个 python313.dll（zlib.dll 相依未解＝换个弹窗）；为"消掉报错"而改产品 CMake/代码（缺陷在环境供给面，治理裁决见 F-440——交付口径未裁）；重置主构建树缓存"试运气"。
4. **复现性**：处置后在报告里记三要素——探测命中目录（或"不可达"）、受影响目标清单、重跑结果。

## 流程

1. **判定受影响面**：确认是运行期缺 DLL 而非构建错误——报错出现于**启动 exe 时**（弹窗/0xC0000135），构建本身零 error。需要精确定位哪个 exe 被拖入导入面时：`dumpbin /dependents <exe> | findstr /i python`（VS 开发环境）或对照 gate-all 的 SKIP 归因行（受影响目标已列名）。
2. **找真实供给目录**（含 python313.dll **且**同目录有 zlib.dll 的安装根）：
   - 本机已有 Miniconda/Anaconda/Python3.13：`<根>` 即 `<根>\python313.dll` 所在目录（conda 根＝condabin 上一级）。
   - 全无 → 安装一个 Python 3.13（或 Miniconda）到标准位；**WindowsApps 的 python.exe 空壳 stub 无 DLL，不算**。
3. **按优先级供给**：
   - ①固化（推荐）：`setx SDURWS_IRD_PYTHON_DIR "<目录>"`（用户级环境变量）——gate-all 探测链第二优先级自动命中，此后一切会话免处置；
   - ②单次：`pwsh -File RobWork/scripts/industrialrobot/gate-all.ps1 -PythonDir "<目录>"`（同批配 `-QtPrefix`/`-Toolchain` 用）；
   - ③临时（直跑 exe 前）：`PATH="<目录>:$PATH"` 前置后启动。
4. **不可达时按 SKIP 口径收口**：全机无 Python 供给且短期不装 → 不硬凑。gate-all 自动按 F-715 SKIP＋归因（探测顺序：`-PythonDir` 参数 > `SDURWS_IRD_PYTHON_DIR` 环境变量 > PATH > conda/python 命令反推 > `%USERPROFILE%\miniconda3|anaconda3`、`%LOCALAPPDATA%\Programs\Python\Python313`、`C:\Python313`）；直跑的 exe 换用不受影响目标或在报告里明记"环境缺供，未执行"。受影响测试**不得标通过**，处置状态如实登记。
5. **留痕**：环境变量固化属于机器级配置变更——在当批 commit 的验证段注记一行（何时设置了 `SDURWS_IRD_PYTHON_DIR=<值>`）；SKIP 归因随 gate-all 日志/验收记录留存。

## 登记索引（深挖时的权威出处）

- 机理与裁决面：findings F-428（导入面机理首登）、F-440（环境敏感交付口径——治理待裁）、F-828（框架判定链）；F-725（历史首跑 42/44 实例——补参即消）。
- 现状口径：DTB §5.1「fresh 树运行时前置」行③；gate-all.ps1 §0.5（探测/注入/SKIP 实现，F-715/TK-T12）。
- 关联但不同族：`0xC0000135` 若指向 Qt/gtest DLL → 走 DTB §5.1 ①②（先建 `sdurws_ird_studio` 或 PATH 前置 `vcpkg/installed/x64-windows/bin`），不是本 Skill 的面。
