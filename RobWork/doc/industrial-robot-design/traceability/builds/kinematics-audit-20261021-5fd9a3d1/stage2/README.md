# 第二批原始证据与重跑

raw/包含修复前9项（1通过8失败）、修复后9项（9通过）、六套完整回归、双模式构建与11条机器verify日志；XML和ird报告独立保留。stage2-validation-summary.json按testcase/skipped节点计跳过，未用testsuites根节点缺省属性冒认0跳过。

截图按Name+Label隔离，config-red/config-green2和集成/独立完整回归均未互相覆盖。gui-build-config-green1为最初编译失败，不是测试失败；保留原日志。完整结论在上一层REPORT-stage2.md。

从仓库根执行：

```powershell
pwsh -NoProfile -File RobWork/doc/industrial-robot-design/traceability/builds/kinematics-audit-20261021-5fd9a3d1/stage2/run-stage2.ps1 -Mode build
pwsh -NoProfile -File RobWork/doc/industrial-robot-design/traceability/builds/kinematics-audit-20261021-5fd9a3d1/stage2/run-stage2.ps1 -Mode test -Name kinematics_gui -Filter 'KinConfigAcceptance.*'
pwsh -NoProfile -File RobWork/scripts/industrialrobot/verify-task.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/foundation/WP-15-T20.json
```

runner带当前机器的VS x64、Qt/vcpkg/Python路径；其他机器按工程环境配置，禁止改成offscreen。GUI启动前及运行中有并发进程检测，发现冲突只停止本树测试，不能关闭另一智能体进程。

原始工具输出不改写；单元卡/计划/报告与源码另做空白检查。未执行正式宿主三维与标准性能，不将截图声明为真实mesh或端到端交付证据。
