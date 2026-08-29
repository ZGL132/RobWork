# WP-06-T03 WorkCell/DWC 确定性双编译

- **Task ID / 需求 ID / ADR / 阶段：**WP-06-T03；ARC-03、ARC-04、CON-06、NFR-COR-05；ADR-004（规范模型为共享语义唯一权威）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；契约 `architecture/public-interfaces.md` §7、`architecture/symbol-registry.md`（SYM-KIN-005）；方案 `module-design/runtime-model.md` v0.3 §2/§4
- **前置任务及必需工件：**WP-06-T01（CanonicalModelCompiler 前置的 canonical 产物）、WP-06-T02（RuntimeNameMap）、WP-01-T05（RobWork/RobWorkSim 依赖与 API 基线）
- **允许创建/修改/删除的文件：**根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/` 下 `include/sdurws/ird/runtime/{CanonicalModelCompiler.hpp,CompiledRobotArtifacts.hpp,RobWorkModelAdapter.hpp}`、`src/{CanonicalModelCompiler.cpp,RobWorkModelAdapter.cpp,DynamicWorkCellAdapter.cpp}`、`test/DualCompileTest.cpp`、`testdata/runtime/failpoints/`
- **禁止修改的文件和公共接口：**canonical 字段与名称格式、WP-07 碰撞策略、项目写入权限（编译结果不自动写项目）、业务插件接口；`CanonicalModelCompiler/RobWorkModelAdapter/DynamicWorkCellAdapter` 为模块私有类型不得进入公共头
- **修改前接口：**基线 `WorkCellConverter.cpp` 单链路直接构造 WorkCell，无 DWC 交叉校验；部分失败可能残留半成品对象并继续传播
- **修改后接口：**`CompiledRobotArtifacts`（SYM-KIN-005）同载 canonical、names、WorkCell::Ptr、DynamicWorkCell::Ptr、compileDiagnostics 与 source identity；任一工件失败整体为空（无部分指针），调用方旧工件不变
- **实施步骤：**隔离 builder 分别构建 WorkCell 与 DWC（RobWork 指针仅由创建线程在 builder 内释放）→ 按 objectId 交叉校验：device/joint/frame 集合一一对应，几何、collision/proximity 绑定、limits、mass、COM、inertia 双侧一致 → 原子发布或全败释放并返回诊断
- **RED 测试：**`test/DualCompileTest.cpp`（注册于 `sdurws_ird_runtime_test`）：failpoint 注入 WorkCell 成功/DWC 失败及反向，必须返回空 artifacts＋`IRD-RUNTIME-COMPILE-FAILED`（System）；交叉校验任一项不一致报 `IRD-RUNTIME-ARTIFACTS-MISMATCH`（Engineering）——先确认测试在无实现时失败
- **最小实现：**两个隔离 builder＋失败清理路径＋交叉校验清单逐项实现；不做任意轴补偿（T04）与重命名（T05）
- **正常/边界/失败测试：**正常：合法 RobotDesign 双工件同时可用且绑定清单可审计。边界：几何/动力 Link 缺失（Engineering/DataInsufficient，无部分编译结果）、第三方构造抛异常（System，释放已创建对象）、相同 revision/版本/seed/线程数重复编译时 artifact 清单、名称顺序与诊断顺序逐字节一致。失败：builder 返回空或构造失败 → 空工件＋诊断
- **精确验证命令：**

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_runtime(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_runtime_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_runtime(_contract)?_test$"
```

- **diff 和禁止项检查：**`git diff --name-only` 仅命中允许清单；三个新类型未出现在公共 include 导出；无直接写项目 revision 的调用；无 Qt Widgets include
- **证据工件：**`runtime/evidence/WP-06/T03/`：binding manifest、failpoint 日志、artifact hash、RobWork 版本、资源释放记录、命令日志与评审签名
- **提交格式：**`WP-06-T03: implement atomic dual runtime compilation`
- **停止与升级条件：**RobWork API 无法支持全成全败或指针所有权无法证明时暂停并报告，不降级返回部分指针；需新增 RobWork 版本适配时升级 WP-01-T05 基线评审
