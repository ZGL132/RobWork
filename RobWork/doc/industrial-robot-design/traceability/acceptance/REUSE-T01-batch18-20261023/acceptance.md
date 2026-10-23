# REUSE-T01 B18 独立验收

结论：**PASS（限定本批审计登记与反例执行证据）**。冻结 SHA `daee4f8905587b05cd1bab88bb570b8dc240097c`，父 SHA `3a8375eac74408d06ba8999013635428aa6c90e7`。F-961 **保持 open**。PASS 不表示测试全绿、产品复用合规、全 T01 退出或准许 T02/T05 实施。

## 语义与权限

- 46 条新增定义逐条审读，定义区段/矩阵源字段一致；506 项外部上下文与审计证据摘要、19 项候选文件/目标摘要均匹配。46 行均 pending，原有矩阵行未改，无 grant。逐条独立意见见 `semantic-review.json`，源码抽取见 `all46-source-review.txt`。
- JointLimits 的已发布策略、形状/非有限/名称守卫、ObjectId排序、首违例、有符号裕量、严格近限位、continuous豁免及来源事实可核。Models::inBounds/QConstraint::makeBounds 为普通闭区间过滤候选，不等价整段业务输出。有限端点缺显式有限校验及差/半宽极值溢出只属未实跑风险；finalized 为本评估 DTO 完成标记。
- PolicyParsing 两入口委托唯一管线；单位真实委托core，域窗委托PolicyThreshold，冻结4π委托JointThresholds，身份及发布委托原codec/工厂。标准排序/去重已用，但角色词表与codec同键排序存在维护重复，尚未消除。Group解析存在性应答不能取代会话成员展开。
- COLLISION 实用ProximitySetup/Rule与ProximityStrategyRW；传入WC仅findDevice/findFrame/getObjects读取，hasModel/addModel写后端会话缓存且持查询锁，不是WC/DWC结构发布。生产评估器共享同后端同锁；直接多会话共享后端未共享锁不能按缺省锁自动判安全。内置RW仅继承CollisionStrategy，动态DistanceStrategy探测才是距离能力依据，描述符字面串不构成能力证明。语义/权限按ARCH §3.6分别核对，无模型构造权授予。
- 诊断仍通过policyDiagCode/registryCode与core记录构造，但调用点中文context/cause/action及domainWindowText/levelText真实存在。ARCH §3.6.3明确diagnostics权威，登记保留pending，须后续追正式模板/转换/显示消费者，未将静态审读冒充全链通过，也未批准第二诊断服务或envelope。

## F-961 与实际执行

原probe CMake通过add_subdirectory编译冻结clone的真实core/testkit/policy目标，别名仅接导出RW目标；探测include原CollisionSessionTest.cpp，buildSession实际调用makeRobWorkCollisionEvaluator()->createSession。未复制实现或自列policy源集。新树位于 `C:/Users/zgl18/AppData/Local/Temp/ird-accept-b18-20261023-020423-1105365130/fresh-build`。

VS2022 x64，原CMake配置exit0，Release目标构建exit0；绝对路径单exe仅过滤 `ReusePolicyBoundary.*`，QT_QPA_PLATFORM=windows。真实结果 **1 passed / 1 failed / 0 skipped / 0 notRun，exit1**；普通点号正例通过，REGEXarm同设备/同帧前缀生产会话规则反例失败于原第42行。独立及冻结原件两套native报告/XML/退出码逐例一致，原件9项摘要按仓库Digest全部匹配。原报告没有末换行，规范口径会补唯一末LF，并无摘要新鲜度缺陷。

上游convertToRegEx把前五字节REGEX去除后解释为regex，runtime合法字符集允许REGEXarm。原生产setup直接传完整名，故match原名为false。evaluate当前按scope逐对调用后端，不使用该setup过滤，因此只实证公开setup规则输出缺陷，**不证明实际碰撞漏检**。F-961范围和open状态准确，后续T05仍须最小适配与原会话回归及权限审查。

## 门禁与现场

`verify-task.ps1 -TaskFile .../REUSE-T01.json -RepoRoot <冻结clone>` exit0；其3条机器命令validate-task、validate-reuse、validate-docs均真实执行通过。reuse仅治理通过（697 functions、closedFiles=0）；docs全库索引DeepEvidence NOT RUN。六组原检索独立重放exit0、行集合全相同，前四组枚举顺序差异不当作内容漂移。

首轮长路径检出失败、首次缺DLL启动（-1073741515，未执行）均保留日志；只在clone启用longpaths，按原导出目标实际DLL路径补当前进程PATH并使用既有SDURWS_IRD_PYTHON_DIR后完成实跑，未重配主构建树、未修改产品。初审摘要算法的假差异及修正日志亦保留。

原policy完整套件虽被注册但 **NOT RUN**，不混入这两项计数。Models::inBounds数值适用性、真实碰撞/距离数值、生命周期/并发/性能、诊断显示全链、GUI与全T01/T02均 **NOT RUN/未授通过**。父提交到冻结SHA的产品/上游/vcpkg源码diff为空；独立clone最终Git干净；实施现场未改，无提交、推送、合并。
