# 验收去重（2026-10-23）

所有者要求删除或跳过形式化、收益低的验收。执行范围：acceptance-protocol 明确按影响复用真实证据；GOV-T21 专项条件不再被套用于每个能力包；verify-task 复用 validate-task 已执行的门禁，不再对同一契约重复扫描。

保留数值正确性、生产调用及结果消费、模型/状态唯一权威、相关业务回归、必要独立审查和实际失败阻断。没有删除历史证据，没有放宽 H1—H6，没有新增缓存、通用平台或豁免开关。显式 verify 命令仍执行，未尝试按字符串猜测哪些不同命令等价。

验证：`pwsh -NoProfile -File RobWork/doc/industrial-robot-design/traceability/acceptance/lean-verification/TestVerifyPreflight.ps1`。
测试复用真实 validate-task/verify-task，仅对重型 validate-reuse 使用计数替身，验证执行器接线而非替代实际复用门禁。修改前 3 通过、2 失败（重复前置）；修改后 5 通过、0 失败/跳过。结果及每例原始日志在本目录。真实 validate-docs（含当前 validate-reuse）通过，原件见 docs.log；该 PASS 仅为治理检查，不代表算法债务清零。

本次没有 C++/构建/GUI 改动，因此不重跑产品构建、GUI、全152治理夹具、历史索引深查或全量冻结检查。EAA 撤销包的独验与既有数值回归单独保留于 REUSE-T10，未把旧补丁 FAIL 改判为 PASS。
