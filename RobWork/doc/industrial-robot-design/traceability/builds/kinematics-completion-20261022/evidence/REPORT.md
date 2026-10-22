# WP-15-T20 第四批实施验证（2026-10-22）

执行环境：Windows / Visual Studio 2022 x64 / `QT_QPA_PLATFORM=windows`；GUI可执行文件绝对路径逐个运行。XML、ird报告、日志和真实窗口截图均在本目录。

| 状态 | 命令/范围 | 结果 |
|---|---|---|
| PASS | 独立运动学模型 | 281 跑：280 通过＋1 已登记跳过（仅登记宿主GUI呈现、不在模型exe执行），见 `standalone-kinematics-final-model-recheck.*` |
| PASS | 独立运动学契约 | 57/57 通过，见 `standalone-kinematics_contract-final-contract.*` |
| PASS | 独立Windows GUI | 48/48 通过，含状态修复、后台取消/销毁、三维预览投放和画布选点回写，见 `standalone-kinematics_gui-final-standalone-gui.*` |
| PASS | 集成Windows GUI | 45/45 通过，见 `integrated-kinematics_gui-final-integrated-gui.*` |
| PASS | 集成宿主构建 | `sdurws_ird_studio` 成功构建，见 `build-integrated-studio-final-integrated-rebuild.log` |
| PASS | `ird_gates` | 红线门禁退出码0，见 `gates-standalone-kinematics_gui-final-gates.log` |
| PASS | 任务契约 | `validate-task.ps1` 通过 |
| PASS | 代码差异 | `git diff --check` 通过 |
| PASS | 文档链 | `validate-docs.ps1` 在重生成证据索引后通过（20 units、12 trace entries、369 task files、29 registered assets、15823 indexed evidence） |
| NOT RUN | 正式宿主交互tour | 本轮未增加专属宿主tour驱动；组件真实三维像素渲染及宿主可执行构建不替代真实机械臂网格/宿主投放验收。 |

红绿回归：状态修复前两项断言失败；异步采样端口接入前“运行必须异步提交”失败；三维出口接入前“必须有 sampleGrid”失败；修复后相应回归转绿。早期红日志保留在 `build/kinematics-completion-20261022/` 构建树（该树为git忽略构建产物，不作为交付证据）。

范围：正式宿主GUI几何和网格交互仍未验收；尚未覆盖KIN-10/11算法入口、Jacobians完整消费、正式求解结果归档/重开，以及完整70项人工交互矩阵。因此本批组件修复通过不表示任务或整模块验收完成。
