# REUSE-T01 B11 独立语义验收

结论：**PASS，仅本批审计记录**。冻结 `a3cb564810b3eae8515ba11bfed9c37756d808f4`，相对 `e0bee2994eee0e8f3299ea5ccf200c354f4454be`。

仅相对base新增33功能、catalog三候选及planning-computation-audit.md的语义审计；B10返工不在本轮范围。

未发现阻断本批审计记录的错误。33条仍为pending、3个catalog仍为candidate；通过不代表生产迁移批准、全T01退出、整文件关闭或T02准入。B10返工由另一验收范围处理。

## 核对结果

- **33条定义/作用域/摘要：** 按相对base新增functionId精确提取33条，非把本提交B10返工混入。源码首行含完整返回类型/属性，闭合括号及真实命名空间/类作用域逐项核对；辅助平衡扫描33/33，源区段及inputs/auditEvidence规范化摘要238/238相符。
- **真实targets与catalog候选：** Planner在if(TARGET sdurw_kinematics)下加入sdurws_ird_trajectory，集成链接sdurw_models/pathplanners；Pareto/Generator、Envelope与PowerEnergy归各自真实库。新增PATH-PLANNING/MATH-METRIC/MATH-TRAPEZOID三候选均为candidate；10个上游/测试引用与CMake目标摘要匹配，未把目标存在当默认供给。
- **L1/Linf/L2与算术顺序：** 源码sampleStraightPath及RRT metric为L2；maxAdjacentStep为逐段Linf；pathJointLength/plan两段累加为L1。PathAnalyzer先每段metric归约再跨段累加，与原逐轴累加舍入可能不同。说明正确保留混合rad/m口径、候选稳定键/身份回归限制，并指出源码同口径注释需后续修正。
- **全局seed与停止条件：** Random.cpp匿名命名空间generator为共享mt19937，QSampler::BoundsSampler::doSample调用Math::ranQ；Math::seed(unsigned)与64位截断/互扰风险有明确pending，未把SingleThread当全局锁。StopCriteria的回调、stopEither与stopAfter真实存在；产品query仍只传remainingS，取消和总预算实效保留待核。
- **权限和实际结果消费：** Planner经WorkCellConstView取const SerialDevice，局部约束/采样器/RRT，不创建权威WorkCell；policy只采Completed+Applicable+finalized，Collision与间距不足分开，缺口保守拒绝。query结果经过端点/PathSequence与候选选择并由buildSegment消费。调用期锚点与全UI装配未证明，明确pending，无新增grant。
- **Pareto与Generator排除：** sandbox Optimizer明确多输入单输出；路径优化器非多目标支配排名。core::closeWithin参考元b、min/max词表、首次去重与排序键被识别。容差支配可能循环未伪称有限偏序。Generator真实委托既有LHS并区分OFAT/预算/锁定与授权；EvaluatorPorts生成批及Quick/Verified前沿结果消费可定位。排除不等于批准新核心。
- **Envelope与PowerEnergy：** Envelope组装PositionOnly并消费IK结果，取消/设施优先，实际调用推进计数，近似/NotRun/未找到非不可达边界被保留；生产宿主接线仍待核。PowerEnergy读mechanicalPower，按W·s=J积分，保留正负分项/缺口/NaN/首峰窗业务规则，RigidBody能量与状态积分不等价。
- **TrapMethod供给与算术差异：** TrapMethod::trapezMethod模板确为固定h、M点，M=2可逐区间；表达式(h/2)*(f0+fL)+h*sum与产品0.5*(p0+p1)*dt不同。softbody/CMakeLists.txt默认FALSE且头安装在if(build)内；公开源码include探测不证明产品CONFIG供给。说明没有声称无可复用积分，也未授权拉起整个模块或复制模板。
- **证据和范围声明：** 7条冻结源码检索独立重放exit0且排序后命中集合相同；外部D盘cache查询未重放，默认OFF由冻结CMake核对。实施者4项探测XML/JSON名称计数与7项源码/上游摘要匹配；未当生产断开实验。33行pending与COV01～05补样未扩为全T01完成。

## 验证

- 独立实跑：validate-task、冻结Head的validate-reuse、validate-docs及本批审计文件git diff --check，均exit 0；复用门禁仅治理通过。全提交diff --check为exit 2，命中B10/监听器原始日志和B11保留的首次构建失败日志空白；原件保留，不作为语义阻断。
- 33条定义及238项规范化绑定通过；3候选10项上游/测试/CMake引用通过；7条源码检索重放命中集合一致。
- 实施者4项API探测：冻结XML/JSON均4 passed、0 failed/skipped/notRun，源码绑定和名称一致。本会话未重新编译/运行它们，未称独立产品验证。
- 未运行全产品/GUI/性能、远程CI及生产断开实验；不要求为本次只读审计重跑全产品。

## 保留边界

全局seed隔离、取消预算接线、路径归约数值与身份、TrapMethod供给/算术语义、Pareto循环风险、包络生产接线及调用期锚点、稳定文案权威仍待后续能力任务实证；这些已被如实记录，不在本轮代实现或关闭。

独立clone最终HEAD正确且工作树干净；相对base产品和上游源码无改动。首次Windows长路径checkout失败日志保留，启用该clone的core.longpaths后重试成功。外部构建cache查询不从冻结树重放，不能作为独立当前环境断言。

详细逐项事实和限制见 acceptance.json；定义/摘要为definition-checks.json、bindings.json、catalog-bindings.json；命令及原输出为search-replay.json和各search日志，治理日志为task/reuse/docs.log。
