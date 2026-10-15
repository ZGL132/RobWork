# RT-T16 最终自测摘要（2026-10-15）

实现范围：CanonicalModel.cpp、DescriptionValidator.cpp 及对应 runtime 回归测试；同步 runtime 单元卡。未修改公共头、模型布局、输入物理值、Codec 身份规则或禁区目录。

数值口径：S3/S5 共用 `max|Iij-Iji| / ||(I+I^T)/2||F <= 1e-12`；零范数沿用绝对差退化。SPD 使用实际对称化临时矩阵，按合同平衡后的固定顺序 LDLᵀ 严格正主元判定，不设置 epsilon，不回写输入。

验证证据：
- `smoke-red/`、`integration-red/`：旧实现真实翻红，复现小尺度误放行与大尺度误拒。
- `smoke-final/`：Release 构建成功，187/187 通过，原生 JSON 187 passed / 0 failed / 0 skipped。
- `integration-final/`：Release 构建成功，284 passed / 1 既有测试 skipped / 0 failed；跳过项为 `ContractSuite.ChildProcessWorker_RT_ID_1`。
- 两模式 `ird_gates`、RT-T16 `validate-task`、全量 `validate-docs` 均通过。

交付状态：实施自测完成，分支已推送；请独立验收会话按 acceptance-protocol 重配、重跑并出具 PASS/FAIL，不由实施者代行合入。
