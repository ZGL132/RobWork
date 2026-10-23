# 批次验证与增量治理（2026-10-23）

实施基线 `091e53353a10334a7be4f701324b6a51d5db7dcf`，分支 codex/reuse-foundation。所有者已批准提速建议，并要求每轮仅编译、验证及验收修改与实际受影响部分。无产品/CMake/模型权限/上游变更，不执行 C++、GUI、全库配置或无关历史套件；未自签独验、未远程、未合入。

- validate-docs 的 TaskFile 数组入口委派 validate-task；任务索引共用、逐份契约验证，复用检查每批一次。历史契约批次仍补全量检查，默认入口未减少条件。两个复用任务加文档原需3次全量调用，现在1次；计数由23项定向夹具验证。
- validate-reuse 只扩展选中契约数组，原库存/权限/指纹/生产链/阶段/独立关闭规则不改。真实 CheckTask/Json 的数组、第二份非法契约及越界路径有正负例，不拿替身证明复用规则通过。
- verify-task 仅对开头同一运行时、带 NoProfile、严格相同当前任务的复用命令引用已通过前置；单列 reusedPreflight。其他命令执行后、更强参数、其他任务、跨运行时及命令后缀仍执行。无持久缓存、skip/豁免开关或第二套门禁引擎。
- 根 AGENTS、验收协议、reuse/policy 与既有验证 Skill 明确未变且不受影响不重跑；代码和相关测试稳定后一次性更新受影响凭证。已有证据须核对源码/依赖/工具链/配置/输入/范围，HEAD 相同或不同都不能单独决定重验。共享文件唯一修改者、H1—H6、最终准入及独验要求不变。

复现：`pwsh -NoProfile -File RobWork/scripts/industrialrobot/test-governance-lite.ps1 -ValidationBatchOnly -OutputDir <输出目录>`；本次参数/JSON数组分支涉及PS兼容性，所以另以 powershell 运行同一选中套件。ps7/ps51 各23 passed、0 skipped、0 failed、0 notRun；原件为各目录JSON/JUnit/逐例日志。原21项精简治理套件、152项历史复用夹具不在本次修改影响范围，本轮 NOT RUN，不把定向通过称为全套通过。CI沿既有工作流增加这组选中测试并使用原artifact路径；远程CI NOT RUN。

夹具里的计数替身仅证明编排与失败传播；docs只执行本次修改的原AST委派分支，CheckTask/Json及选中任务循环也取实际AST。测试不复制生产规则。history保留旧代码的批次反例、PS5数组问题、新路径归约问题及夹具Json返回方式误差，不能把中间失败改写为通过。最终参数/数组路径已修复，原始报告仍列notRun。

governance-before.json记录旧版一次全量检查29.381秒，真实失败仍为继承drivetrain覆盖缺口。本包证明重复调用次数3→1和verify前置2→1；未进行完整重构端到端测速，不承诺总耗时按同比例减少。最终文档/两任务真实批次检查为 FAIL（exit 1，28.598秒）：继承的 drivetrain/CMakeLists.txt REUSE-COVERAGE 仍未关闭，由协调主会话安排责任人，不转交基础平台。原始重定向日志见 batch-final.log，终端错误原文另存 batch-terminal.txt；基线同一阻断见 governance-before.log。详见 verification.json，不重复运行三个子入口，不宣称整体准入通过。

独立验收 NOT RUN；门禁组织变更需所有者另开独立会话或人工复核确切提交与本次受影响分支，不要求重验无关产品。回滚整个本次治理提交；本轮不重开FNV/日志/运行时数值证据，不恢复EAA，不消除C4及drivetrain遗留。
