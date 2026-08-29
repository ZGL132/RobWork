# WP-06-T03 WorkCell/DWC 确定性双编译

- Task ID：WP-06-T03
- 需求/阶段：ARC-03、ARC-04、CON-06、NFR-COR-05；阶段 A / R1
- 架构契约：`architecture/public-interfaces.md`、`architecture/execution-model.md`；模块方案：`module-design/runtime-model.md`
- 前置：WP-06-T01、WP-06-T02、RobWork/RobWorkSim 构建环境。
- 允许：修改 `runtime/include/.../CanonicalModelCompiler.hpp`、`CompiledRobotArtifacts.hpp`、`RobWorkModelAdapter.hpp`、`src/*Adapter.cpp`、`test/DualCompileTest.cpp`、`testdata/runtime/failpoints/`。
- 禁止：修改 canonical 字段、名称格式、碰撞策略、项目写入权限和业务插件接口。
- 产出：WorkCell、DWC、名称表的隔离构建、交叉校验和全成全败结果。

## 数据流

`canonical -> isolated WorkCell builder + DWC builder + name map -> bind objectId to device/joint/frame/geometry/collision/mass/limits -> cross-check -> publish immutable artifacts`。builder 任一失败都释放临时对象并返回空结果。

## Given/When/Then

- Given合法 RobotDesign，When compile，Then WorkCell、DWC 和 name map 同时可用，绑定清单可审计。
- Given WorkCell 成功/DWC 失败或相反，When compile，Then `CompiledRobotArtifacts` 为空，调用方旧工件不变。
- Given相同 revision、版本、seed 和线程数，When重复编译，Then artifact 清单、名称顺序和诊断顺序一致。

## 测试、证据与提交

覆盖几何/动力 Link 缺失、第三方异常、重复编译和线程释放。命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_dual_compile_test$'
```
证据：binding manifest、failpoint 日志、artifact hash、资源释放记录。提交：`WP-06-T03: implement atomic dual runtime compilation`。

停止：RobWork API 无法支持全成全败或所有权无法证明时暂停，不降级返回部分指针。
