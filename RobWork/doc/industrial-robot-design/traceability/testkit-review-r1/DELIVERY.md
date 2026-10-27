# TK-T13 实施送验

实施提交：`e7b9546b8e1f755bb8e2cd0676deeb0594307b59`，分支 `codex/core-review-fixes`，起始版本 `6dc1141ae1e4d0db61baf1e7ab8d2ed414669c9f`。本文件及同次附加检查日志仅记录实施后的提交核对，不改变生产代码、测试、原验证结果或独验状态。

详细范围、格式授权、回滚边界、未决事项及历史证据影响见 [实施记录](README.md)。正式源码的提交对象规范化摘要与 `implementation-inputs.json` 全部一致；core、正式 testdata、框架及 vcpkg 的本包差异为空。提交前既有更改已完整保留，未操作远程或 main。

最终受影响验证为：集成 testkit 106 PASS，独立冒烟 32 PASS，直接消费者 optimization 2／policy contract 2／project contract 1 PASS，以上均0 FAIL/SKIP；真实报告交叉核对集成/冒烟各7组符合预期、lint最终6组、扫描等早期工具14组、自包含头6项通过。受控失败/跳过子进程仍按真实状态保存，不能记成算法验收PASS。首次完整基线132 PASS／1 FAIL与policy首次零命中NOT RUN均保留，后续相关复验通过，不重复全套或掩盖原记录。

`ird-gates.log` 通过引擎及自测；`validate-docs-final.log` 的task/reuse/docs通过，仅是治理检查，仍有全库既有待审与债务输出，不代表全项目算法合规。原始构建日志中编译器输出的尾空格原样保留；代码/文档差异检查不含 `.log` 时通过，不清洗原始日志制造全绿。

提交后实际执行：

```powershell
pwsh -NoProfile -File RobWork/scripts/industrialrobot/verify-task-scope.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/foundation/TK-T13.json -Base 6dc1141ae1e4d0db61baf1e7ab8d2ed414669c9f -Head e7b9546b8e1f755bb8e2cd0676deeb0594307b59
pwsh -NoProfile -File RobWork/scripts/industrialrobot/commit-lint.ps1 -Range 6dc1141ae1e4d0db61baf1e7ab8d2ed414669c9f..e7b9546b8e1f755bb8e2cd0676deeb0594307b59
```

结果分别为206路径全部在允许面内、1提交信息合规；原件见 `scope-implementation.log`、`commit-lint-implementation.log`。本文件后的证据提交只增加本目录记录，最终范围仍须从上述起始SHA显式比对实际送验HEAD。

任务 `TK-T13` 保持 ready，F-015～F-018 保持 open。需要所有者另开独立会话验收冻结源码和本包 acceptance，本实施不自签、不合并、不推送。不重做未受影响的T20性能接受、T49生成器独验、core全套或GUI。

在所列版本、范围和已执行验证内，未发现其他问题。

## 独立验收（2026-10-27）

所有者另开独立会话验收 **PASS**，登记于 [acceptance/TK-T13-20261027-r1.json](../acceptance/TK-T13-20261027-r1.json)（证据同目录），范围显式 `-Base 6dc1141ae -Head 7a941b988`（211 路径允许面）。独立会话受影响目标增量重建后真实重跑：集成 testkit 106／optimization 2／policy contract 2（正确 `_contract_test` 目标）／project contract 1／独立冒烟 32 全绿（ird-test-report 全部 decisive）；lsnchild 真实 listener 子进程七项交叉核对退出码全符合预期，报告实测 `"+Inf"`/`"-Inf"`/`"NaN"` 字符串、缺失侧 null、`seed=18446744073709551615` 完整精度整数，与批准方案逐字一致。ird_gates 零命中、validate-docs 三段 PASS、commit-lint 两提交 PASS。首次基线 132 PASS＋1 FAIL 与 policy 首次零命中原始记录核实未被清洗。F-015～F-018 台账状态保持 open，关口归所有者。
