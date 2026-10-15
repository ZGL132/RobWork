# 工程打磨入口核查记录

日期：2026-10-15。性质：只读入口核查与工作包登记证据；不是产品验收，不是阶段收口声明。

## 1. 实际观察

| 项目 | 实际结果 | 取得方式/边界 |
| --- | --- | --- |
| 工作分支 | redesign-main | git branch --show-current；main 未触碰 |
| 入口 SHA | 7ce545e3215cab7c711f5bb50148ff49aa3f8639 | git rev-parse HEAD；本记录之前的产品基线，不包含本次文档提交 |
| 工作树 | 当时无已跟踪修改；存在既有未跟踪研究文档及 .zcode/、audit/、nul、t1/、t2/ 等 | git status --porcelain=v1；保持原样，未纳入本次提交 |
| 集成配置 | RWS_BUILD_INDUSTRIALROBOT:BOOL=ON | 读取 build/CMakeCache.txt；只证明缓存配置，不证明当前产物已构建 |
| 生成器/Qt 前缀 | Visual Studio 17 2022；D:/software/Qt/6.11.1/msvc2022_64 | 同上；运行环境/依赖可加载性尚未验证 |
| 文档检查（拆包前） | PASS：20 units、12 trace entries、350 task files | pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1；exit=0 |
| findings | 总 820，open 502：blocking 1、major 21、minor 8、suggestion 472 | 读取 traceability/findings.json；账面计数，不是已复现缺陷计数 |
| OPT-B | WP-20-T02～T11 均为 done | 逐文件读取 tasks/foundation/WP-20-T*.json；未重跑计算或独立验收 |
| OPT-D | WP-21-T01/T02/T03/T04/T06/T07 为 done，T05/T08 为 planned | 逐文件读取；T05 note 明确 P-04 判定阈值待确认 |
| F-795 | findings 仍 open；HEAD 包含 70f08cfc 的修复合入 | findings 与 git log 对照；未复跑 Debug 必红/修复回归，不代翻状态 |
| 历史 UI C1083 | 主线有 b90a442d/18bdcdb0 的修复及收尾记录 | git log；不能再按旧计划直接视为当前阻断 |

## 2. 可复用资产入口

已列出 `industrialrobot/testdata/golden/` 中的版本化数据，包括建模模板/导入、DH 等价、FK/IK、覆盖/搜索状态、轨迹复检、二连杆动力学、传动映射、器件目录/筛选/回填、优化 LHS/Pareto/采用、证据切片与工作流等。

这些目录是 QP-01.01 的输入，**本次没有核验其参考值独立性、模型真实性、资源闭包或是否可直接组成 `.rwdesign` 工程**。不能因已有黄金数据目录而宣布 S1 已完成。

## 3. 本次状态与下一项

- QP-00 已开始入口核查，QP-00.01 尚未完成全部环境/测试清单盘点；本轮没有勾选任何完整工作项。
- 下项：补齐 QP-00.01 的实际工具链、DLL/Python/Qt、碰撞后端、测试与占用清单；然后按 Windows GUI 规则执行 QP-00.02。
- 同时可做 QP-01.01 资产复用和 QP-07.01 已有优化接线的只读核查，不依赖 P-04 待定阈值。
- 本次没有新建 canonical 修复契约、翻转任何任务/findings 状态或修改产品代码；具体缺口复现后由 QP-00.04 登记。

## 4. 未执行

双模式构建、ird_gates 产品门禁、gtest/ctest、Qt GUI、S1～S8、独立数值对照、独立验收、干净机部署、目标工程师试用和实测对照均未执行。历史 commit 描述只是核查入口，不作为本轮测试通过证据。

## 5. 管理产物

[工程打磨工作包与推进台账](../../../../docs/superpowers/plans/2026-10-15-engineering-polish-work-packages.md)已拆为 QP-00～QP-13，共 56 个工作项定位键；QP 标签不代替主 WP 或 canonical 契约。

本次拆包后已执行以下文档验证：

- `pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1`：exit=0，`validate-docs: PASS (20 units, 12 trace entries, 350 task files)`；既有任务数没有因 QP 管理标签增加。
- PowerShell 结构自查：14 个唯一包标题、56 个唯一未勾选工作项、每包 4 项；结果 PASS。
- 新计划/本记录及 DTB 新增 §0.4 范围内 8 个本地文件链接目标存在；结果 PASS。此检查未验证全库历史链接或 Markdown 渲染器锚点。
- `git diff --check`：exit=0；提交前暂存检查另覆盖本次新增文件。

以上只是文档与管理结构验证，不构成 §4 所列产品测试或独立验收。
