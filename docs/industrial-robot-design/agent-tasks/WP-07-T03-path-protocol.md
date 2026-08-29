# WP-07-T03 路径验证协议

- Task ID：WP-07-T03
- 需求/阶段：ARC-05、KIN-05、TRJ-04、NFR-COR-05；阶段 A / R1
- 架构契约：`architecture/execution-model.md`、`architecture/testing-contract.md`；模块方案：`module-design/policy-collision.md`
- 前置：WP-07-T02、WP-06 任意轴模型。
- 允许：修改 `policy/include/.../PathValidationProfile.hpp`、`src/PathValidator.cpp`、`test/PathProtocolTest.cpp`、`testdata/policy/paths/`。
- 禁止：改变策略安全距离、轨迹规划算法、连续安全声明或 UI。
- 产出：10 段初始采样、自适应二分、步长/余量/深度和结论措辞实现。

## 数据流

`path + joint types -> 10 equal segments -> evaluate endpoints/interior -> bisect collision/uncertain segments -> enforce 0.05 rad / 0.01 m / 0.005 m / depth 8 -> envelope`。

## Given/When/Then

- Given分辨率内无碰路径，When validate，Then返回 Completed + Pass/Complete，并使用固定结论措辞。
- Given碰撞段，When validate，Then优先二分并记录每个采样点、深度和 pair。
- Given深度 8 仍未达到分辨率，When validate，Then返回 Completed + DataInsufficient + Complete，不声称连续安全证明。
- Given距离查询不可用，When validate，Then遵循 fallback 并保留不确定段证据。

## 测试、证据与提交

命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_path_protocol_test$'
```
证据：采样点/深度清单、分辨率参数、结论文本、距离和诊断。提交：`WP-07-T03: implement path validation protocol`。

停止：需求未冻结步长、余量或深度语义时暂停。
