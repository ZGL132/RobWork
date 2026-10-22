# WP-17-T11 交付验证记录

- 集成 Release 构建：`build/dynamics-integrated-final.log`，退出码 0，包含正式 `sdurws_ird_studio`、动力学单元、契约和 GUI 目标。
- 独立 Release 构建：`build/dynamics-standalone-delivery-build2.log`，退出码 0。
- 动力学单元：116 项通过，0 跳过，0 失败；报告 `dynamics-unit-final.*`。
- 契约测试：30 项通过，0 跳过，0 失败；报告 `dynamics-contract-final.*`。
- Windows GUI（VS2022 x64、`QT_QPA_PLATFORM=windows`、单独绝对路径进程）：12 项通过，0 跳过，0 失败；报告 `dynamics-gui-final.*`。包含六峰值联动、移动关节单位、越界保留姿态、跨结果拒绝、单点曲线像素和结果刷新清除旧标记。
- 静态门禁 `ird_gates`：退出码 0，R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 全部零命中；日志 `dynamics-gates-final.log`。
- 失败能力证据：`dynamics-gui-behavior-red.*`、`dynamics-model-red.*`、`dynamics-single-red.*`、`dynamics-reload-red.*`。

## 未封闭问题

- DYN-P1：正式宿主没有动力学轨迹生产、归档读取和执行器结果源，分析命令仍诚实置灰。
- DYN-P2：正式三维回放没有经过结果快照、设备和关节对象身份校验的 State 写入桥；为避免错写首设备，宿主姿态回调保持未装配。
- DYN-P3：峰值持续时间窗尚未进入面板峰值 DTO和正式文案。
- DYN-P4：本批是真实 Qt 控件测试，不等同正式宿主完整三维窗口验收；正式宿主分析/取消/导出和真实三维姿态截图仍待结果链落位。
- `validate-docs.ps1` 当前被历史未索引的 `dynamics-audit-20261022-119dce6b` 证据及 `UI-T37` 索引大小漂移阻断，未修改历史证据。
