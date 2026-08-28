# WP-09 诊断、本地化与日志实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` and complete this plan task-by-task.

**Goal:** 建立唯一诊断 Schema、中文术语、错误分类和日志脱敏策略，使所有插件对同一状态使用相同代码、含义与建议动作。

**Architecture:** 稳定诊断码和结构化字段属于核心契约，中文文案由目录解析；错误只在真实边界映射一次并保留 causeCode。用户日志与开发日志分离，敏感路径按配置脱敏。

**Tech Stack:** C++、Qt Core、JSON 资源、CTest。

---

## 文件与目标

**创建目标：** `sdurws_ird_diagnostics`、`sdurws_ird_diagnostics_test`。

**创建：**

- `industrialrobot/diagnostics/include/.../EngineeringDiagnostic.hpp`
- `industrialrobot/diagnostics/include/.../IDiagnosticCatalog.hpp`
- `industrialrobot/diagnostics/include/.../DiagnosticMapper.hpp`
- `industrialrobot/diagnostics/include/.../ILogSink.hpp`
- `industrialrobot/diagnostics/include/.../LogRedactionPolicy.hpp`
- `industrialrobot/diagnostics/resources/diagnostics.zh-CN.json`
- `industrialrobot/diagnostics/resources/terminology.zh-CN.json`
- `industrialrobot/diagnostics/src/`
- `industrialrobot/diagnostics/test/`

**覆盖需求：** ERR-01，UX-02、03、06，NFR-REL-05，NFR-MNT-03，NFR-SEC-07。

## 诊断契约

```cpp
struct EngineeringDiagnostic {
    DiagnosticCode code;
    DiagnosticCategory category;
    DiagnosticSeverity severity;
    bool retryable;
    ObjectId subjectObjectId;
    std::optional<std::string> localName;
    std::optional<std::string> runtimeScopedName;
    DiagnosticValue actual;
    DiagnosticValue expected;
    ActionCode action;
    std::optional<DiagnosticCode> causeCode;
};
```

类别固定为输入错误、工程不可行和系统错误；任务执行状态不作为第四种错误类别。名称只用于显示，不能替代 subjectObjectId。

## 任务

### Task 1：Schema 与字段完整性

- [ ] 先写缺 objectId、实际值/阈值、建议动作、非法类别和名称代替 ID 的失败测试。
- [ ] 实现不可变诊断和值类型；没有运行时名称的对象允许字段为空。
- [ ] 实现稳定 JSON 往返，禁止非有限数静默转零。

### Task 2：诊断目录和术语

- [ ] 建立唯一简体中文状态词、关节/姿态/碰撞/证据和任务术语表。
- [ ] 诊断码映射用户说明、建议动作和高级技术说明。
- [ ] 启动时校验重复 code、缺语言项、未知 action 和未使用条目。

### Task 3：边界错误映射

- [ ] 为文件导入、RobWork 适配、工作进程和报告渲染建立单层 mapper。
- [ ] 保留底层 causeCode；上层不得重复包装生成同义诊断。
- [ ] 对同一故障从不同插件入口验证 code/category/action 一致。

### Task 4：日志与脱敏

- [ ] 用户日志不输出调用栈、内部哈希和无动作价值的调试信息。
- [ ] 开发日志可关联 run/attempt/snapshot，但不记录凭据、令牌或未脱敏本机路径。
- [ ] 对 Windows 用户目录、网络共享、环境变量和崩溃转储编写脱敏测试。

### Task 5：静态一致性

- [ ] 扫描业务插件中的硬编码状态词、重复诊断码和单位转换文案。
- [ ] 例外只允许测试期望字符串和诊断目录本身。

## 验证命令

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_diagnostics_test$'
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1
```

## 退出条件

- 每个诊断可定位对象、实际值、期望值、原因和动作。
- 相同故障跨入口的稳定 code/category/action 完全一致。
- 用户日志和崩溃转储无凭据类信息及未配置允许的本机绝对路径。
