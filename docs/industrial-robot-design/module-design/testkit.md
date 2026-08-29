# testkit 黄金数据与数值验证模块详细方案

- 方案版本：v0.1；对应需求基线：v0.7
- 负责 WP：WP-02；阶段/发布：阶段 A / R1
- 架构契约：architecture/testing-contract.md、architecture/domain-model.md、architecture/persistence-schema.md
- 任务卡：agent-tasks/WP-02-T01～T04

## 1. 目标与非目标

- 目标：为所有模块提供不可变黄金数据、统一容差、稳定集合比较和旧代码迁移判定。
- 非目标：不实现 FK、IK、动力学、规划或优化算法；不把黄金数据当生产配置；不读取 Widget/当前会话生成期望值。

## 2. 代码与构建

- 拥有目录：RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/、testdata/ 和 WP-02 证据目录。
- 公共头：testkit/include/sdurws/ird/testkit/；目标：sdurws_ird_testkit、sdurws_ird_testkit_test。
- 依赖：C++ 标准库、Qt Core JSON（若仓库基线已启用）；禁止依赖业务插件和 GUI。
- 测试数据只读安装；测试输出写入 out/test-evidence，不覆盖源文件。

## 3. 数据和接口

- DataManifestLoader::load(path) 返回不可变 DataManifest；每个 Sample 必含 id/path/kind/version/purpose/units/source/generationMethod/expected/sha256/consumers。
- ToleranceProfile 分为 AlgorithmTolerance 和 ExternalValidationTolerance，禁止用裸 double 跨 profile。
- MigrationVerdict 只允许 Migratable、Rewrite、EvidenceOnly，保存 adapter、输入哈希、差异统计、状态依赖和评审者。
- 所有样本输入使用 SI、有限数和规范化 JSON；未知 schema/version 拒绝读取。

## 4. 数据流和状态

manifest → 路径/JSON/schema/单位/有限数校验 → SHA-256 → expected 完整性 → 不可变样本 → 断言/适配器 → 结构化报告。
- 缺文件、损坏 JSON、哈希不符和非法单位：Input/Integrity 诊断，返回空结果。
- 数据不足：Engineering + DataInsufficient，可用于限制报告，不得伪装为通过。
- 断言失败：保留实际/期望值、profile、容差和输入哈希；不修改黄金数据。

## 5. 数值与确定性规则

- 位置比较使用 m 的绝对/相对误差；旋转使用 SO(3) 测地角；轴使用有向夹角；矩阵检查正交性和行列式。
- Jacobian 归一化为 J_norm=[J_v/L*;J_w]；4/5 轴用任务子空间；L* 非正或非有限返回 DataInsufficient。
- 旋转点积先 clamp 到 [-1,1]；近 1 不使用朴素 acos 作为唯一判定。
- 固定 datasetVersion、算法版本、seed、threadCount 和排序规则时，哈希、stableId、候选集合和 Pareto 关系一致。

## 6. 测试和证据

- 契约测试覆盖 manifest 字段、重复 ID、未知单位、哈希篡改、消费者缺失和版本拒绝。
- 数值测试覆盖二连杆解析 FK/Jacobian、等价模型、零轴/非 Z 轴、插值、功率和恢复样本。
- 外部对照必须单独使用 ExternalValidationTolerance，并报告工具版本、环境和测量不确定性。
- 每份证据包含 Task ID、需求 ID、commit、datasetVersion、manifest hash、seed、threadCount、命令、实际/期望值和评审者。

## 7. 迁移与扩展

- 新增样本先提交来源和生成脚本，再更新 manifest 和哈希；期望值变化必须有推导和评审记录。
- 旧算法只通过只读 adapter 接入；迁移失败不得删除旧实现，按 verdict 选择 Rewrite 或 EvidenceOnly。
- 新数据格式、容差类型或断言需要新增契约测试和 ADR，不得修改已有字段含义。

## 8. 验证命令

powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_(testkit|golden_data|migration_protocol)_test$'
入口由 WP-01-T03 交付；模型测试可用 QCoreApplication，不得以 GUI offscreen 替代。
