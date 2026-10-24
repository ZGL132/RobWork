# REUSE-T24 / WP-14：F-968 参数解析失败输出

起点 `d1e416b00433bbd3e62903e583b2ee2b103e0396`，主工作树 `redesign-main` 串行。仅改 `diagnostics/src/ParamSchema.cpp` 和原 `test/StableCodeRegistryTest.cpp`。**实施自查；独验 NOT RUN，任务 ready，F-968 仍 open。**

## 复用判断和边界

沿用 [B33 实审](../../../reuse/diagnostics-schema-audit.md)、[原检索](../REUSE-T01/batch33/search.json) 和 [候选差异实验](../REUSE-T01/batch33/DiagnosticSchemaSuitabilityTest.cpp)。原证据仅支持当时版本；本包红绿结果单列，不改历史 FAIL。

- RobWork `rw/core/DOMParser.hpp/.cpp::DOMParser::make` 默认 XML，扩展插件不等于产品冻结的字符串参数数组；`rw/core/StringUtil.hpp/.cpp` 不是该文法的解析入口。上游测试与目标来源见 B33 检索，不新增 `sdurw_core` 依赖。已安装 Boost.PropertyTree `read_json` 丢失字符串/数字区别并接受产品禁止的转义及换行，不能直接替换。保留原窄文法不构成批准新核心，也不宣称生态不存在其他能力；`UTIL-TEXT` 仍 candidate。
- **保留**原参数词形、空模式、空格/tab、重复检查、首处违约错误文本和私有共享入口。原 `isValidParamName`、解析扫描及内部 `skipWs` 逐项核对；无新增解析算法、fallback、包装类、模型或诊断体系。
- **删除/适配**删除扫描途中写入输出清单的路径，改为在原函数中使用一个局部 `vector` 收集，并仅在完整成功后 `swap` 提交。进入时交换原容量并清理旧元素，重复成功调用不因修正新增分配或复制；失败时局部清单自动销毁，输出保持空。容器容量不是接口契约，拒绝后不承诺保留容量。
- **owner/数据流不变**：注册校验或 `DiagnosticsFactory::create` → diagnostics 单一私有解析点 → 成功清单或原错误轨。两调用方原本已检查 false 并停止；本包没有证据表明旧部分清单流入公共诊断对象。core 只承载既有值契约，diagnostics 继续拥有稳定码/文案设施，不扩大公共接口或依赖。
- 原测试文件的描述符夹具、异常断言、字面码表、注册顺序/摘要/词表/键/类型断言均保持。夹具中的小写投影仅服务冻结测试文案键，不复制通用解析/数学核心；异常回调和 `schemaOf` 只组织真实生产输入。测试期望来自私有头契约、单元卡和字面值，不复制生产解析算法；继续用原 gtest/testkit 报告入口与构建目标。

## 增量验证

现有集成配置 `RWS_BUILD_INDUSTRIALROBOT=ON`，VS2022 x64 Release。构建目标不扩展到其他单元：

```powershell
cmake --build build --config Release --target sdurws_ird_diagnostics_test -- /nologo /verbosity:minimal
# run-tests 在 VS x64 环境执行，逐个绝对 exe；固化已有运行时 PATH。
pwsh -NoProfile -File RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T24/run-tests.ps1 -RepoRoot D:/10_Source_Repos/21_robot/RobWork -Label red -Filter DiagCodesRegistry.DtReg5_ParamSchemaFailureClearsNames_F968
# 修正并重建同一目标，最终版本运行：
pwsh -NoProfile -File RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T24/run-tests.ps1 -RepoRoot D:/10_Source_Repos/21_robot/RobWork -Label green -Filter 'DiagCodesRegistry.*:DiagCatalogFactory.*'
cmake -DIRD_ROOT=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot -P RobWork/RobWorkStudio/src/rwslibs/industrialrobot/cmake/ird_gates.cmake
pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/REUSE-T24.json
```

| 验证 | 结果与原件 |
| --- | --- |
| 新断言＋原生产 | **预期 FAIL**：0 passed / 1 failed / 0 skipped；[XML](red.xml)、[原生报告](red-report.json)、[构建](red-build.log)、[启动](red-launch.log) |
| 最终生产＋解析/注册/工厂直接消费者 | **PASS**：24 passed / 0 failed / 0 skipped；[XML](green.xml)、[原生报告](green-report.json)、[构建](integration-build.log)、[启动](green-launch.log) |
| 架构门禁 | **PASS**：11 类零命中；[日志](architecture-gates.log) |
| 文档/任务/复用 | [最终日志](validate-docs.log)；保留无关 DEBT/UNRESOLVED，不声称全项目合规 |
| 同轮微基准 | [原入口探针](perf.cpp)、[原始输出](performance.log)；MSVC /O2，相同旧/新源码入口，100 万次/轮，三轮交替次序 |

微基准新输出槽：旧 0.109600–0.118854µs/次，新 0.109786–0.115088µs/次；重复输出槽：旧 0.057715–0.060850µs，新 0.058222–0.061892µs。重复调用三轮差为 +0.000263～+0.001042µs；只报告观测，不声明性能改善或外推吞吐。正常路径分配/扫描结构不变，增加局部容器的常数次交换；不接受或外推其他包的性能退化。

输入摘要、退出码与统计见 [verification.json](verification.json)。新测试含 12 种拒绝输入，覆盖早期、中途、重复、非法词形及尾部失败；复用同一输出槽交替成功/失败，另核成功声明序与空模式。原失败回归证明测试有真实失败能力；本包未引入新的上游数值调用，不另做库返回值扰动。

**NOT RUN**：不受影响的词表/日志/脱敏/GUI及全库；独立冒烟与对端全套契约（无公共接口、CMake、依赖或核心数值变化，直接消费者已覆盖）；跨 locale 词形（原 B33 未决，不在本包改变 cctype）；端到端及拒绝路径性能；独验（由所有者另开会话或人工执行）。不扩大线程安全或分配异常承诺。

回滚限本包两个产品文件、当前任务/矩阵/逐文件凭证及新证据，不回写历史 B33 或 T16。未关闭 F-968，不影响 T13 模型真值准入；独验若失败，按该私有契约及两个直接消费者返工。
