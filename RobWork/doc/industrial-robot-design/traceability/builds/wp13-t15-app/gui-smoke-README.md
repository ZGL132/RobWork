# sdurws_ird_modeling_app GUI 冒烟留痕（WP-13-T15 增量，2026-09-26）

## 复现步骤
1. 集成模式构建：cmake --build build --config Release --target sdurws_ird_modeling_app
2. 启动：pwsh -File run-modeling-app.ps1（仓库根；或直接运行 build/RobWorkStudio/bin/Release/sdurws_ird_modeling_app.exe）
3. 手动点验清单（对照 units/modeling.md §9.7）：
   - L-1：点左侧结构树节点（模型根/基座安装/关节1~6/法兰），右侧属性区只显示选中对象相关属性（MDL-07）；
   - L-2：在属性区修改关节限位等字段并提交——接受则树/就绪条增量刷新＋脏标记；非法输入就地显示原因并保留原值（UX-03/05）；
   - L-7：取消勾选工具条"可写"——编辑控件禁用（只读门控）；
   - 命令目录：域命令按钮激活后控制台回显 [command] <id>（不进 project 管线——装配前诚实边界）。

## 本次执行记录
- 启动方式：run-modeling-app.ps1；PID=44480；运行 6 秒后进程存活且 Responding=True；截图 modeling-app-smoke-01.png（五区面板/结构树/命令目录十条/状态栏可见）。
- console-stderr.log 仅有既存 T15 呈现面 Qt 警告 QFormLayout::takeRow: Invalid row 0 一次（非 harness 引入，建议级观测归 T15 所有者）。
- 命令交互未自动化点按——交互点验按上表由人工执行（AGENTS GUI 规程）。
