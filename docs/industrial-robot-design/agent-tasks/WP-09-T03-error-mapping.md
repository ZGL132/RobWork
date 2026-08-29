# WP-09-T03 边界错误单层映射

- Task ID：WP-09-T03
- 需求/阶段：ERR-01、UX-03、NFR-REL-05、NFR-MNT-03；阶段 A / R1
- 架构契约：`architecture/public-interfaces.md`、`architecture/execution-model.md`；模块方案：`module-design/diagnostics.md`
- 前置：WP-09-T01/T02、WP-04/T06/T08/T12/T05/T11 对应边界接口。
- 允许：修改 `diagnostics/include/.../DiagnosticMapper.hpp`、`src/DiagnosticMapper.cpp`、`test/ErrorMappingTest.cpp`、`testdata/diagnostics/mapping/`。
- 禁止：上层重复包装、改变 causeCode、创建第四错误类别或修改业务模块状态。
- 产出：文件导入、RobWork 适配、worker/IPC、报告渲染的单层 mapper。

## Given/When/Then

- Given相同底层 cause 从不同入口出现，When map，Then code/category/severity/retryable/action 一致。
- Given已有诊断再次包装，When map，Then保留原 code/causeCode，不生成同义链。
- Given输入错误、工程不可行、系统故障，When map，Then分别归入 Input/Engineering/System。

## 测试、证据与提交

命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_error_mapping_test$'
```
证据：跨入口映射表、causeCode 链、诊断 JSON 和评审签名。提交：`WP-09-T03: implement single-boundary error mapping`。

停止：同一故障需要多个 code 或边界归属无法确定时暂停。
