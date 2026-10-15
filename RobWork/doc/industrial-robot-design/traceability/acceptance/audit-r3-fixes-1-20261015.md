# audit-r3-fixes-1（R3 审计三轮修复批）——对抗式独立验收归档（attempt 1 fail → 返工 → attempt 2 pass → 合入）

| 字段 | 值 |
| --- | --- |
| 批次 | `origin/audit-r3-fixes-1`（R3 审计三轮修复整批，22 提交＝18 修复＋4 返工，60 文件 +1650/−220，覆盖 12 单元） |
| 被合入 tip | `7106d191a3e1e095a950c227a68ea86993f47ec3`（＝attempt 1 冻结点 604091bf ＋ 4 返工提交 779149e0/093ed0d9/f956c737/7106d191） |
| 验收历程 | attempt 1 **fail**（唯一阻断 B-1：R3-C3 契约签名变更漏同步 DraftContractTest.cpp 装配，冒烟全量构建 EXIT=1）→ 实施方返工（4 提交：B-1 装配修复＋C3 拒收面回归钉＋O-1/O-4/O-9 三条确定性回归用例）→ attempt 2 **pass**（B-1 闭合亲证、四条 O 用例红绿闭环、ctest 两轮 45/45、ird_gates 双口径零命中、SA-02 零触碰） |
| 合入 | **305ffb05** `Merge origin/audit-r3-fixes-1（7106d191）`——2026-10-16 由合入收尾会话执行 `git merge --no-ff`，一次干净合并；合入前按当时主线 tip d69e0904 重算 `git merge-tree --write-tree` **零冲突**（与 attempt 2 §九预报一致：文件级交集恰 2——runtime/src/Codec.cpp 不同函数区、ui/CMakeLists.txt 不同 target 块） |
| 合入后集成实测（收尾批亲跑，主 build/ 树） | ①受影响目标族三批构建（12 单元主库＋23 个 test/contract_test＋gui/perf 测试与 plugin/app/worker/studio 载体）全部 **BUILD_EXIT=0**，零 error 零新增 warning（既有 C4297/C4244 两条与源码同代非本批引入）；②ctest 全部 **45 套件 PASS=45 FAIL=0**（套件名单与 attempt 2 §七逐一套件一致，含 attempt 1 唯一 FAIL 的 sdurws_ird_ui_contract_test）；③定向亲证四条 O 用例（SaturationClampDoesNotWrapCountsToZero_R3_35／PointCloudCollisionDispatchByEvaluationCredential_R3_A7／NonZeroZeroOffsetSnapshotUsesAuthoritativeAngle_R3_A8／PostAccepted+PostRejected R3_C3 族）与 B-1 装配面回归钉全部 [ PASSED ]；45 份逐套件 ctest 日志留痕 `build/acc-r3m-ctest/` |
| 终局 | attempt 2 PASS 判定成立，合入 `redesign-main` 并推送；main 冻结位零触碰；findings.json 零触碰（R3 相关发现翻牌归治理批统一处理）；attempt 1 §三 O-14（库内实施留痕）与 O-15（A1 变异集成面补做）承继为建议级，归治理批清账 |
| 原始记录 | attempt 1 全文见本文第一部分（原件 `build/r3-acc/audit-r3-fixes-1-attempt1.md`）；attempt 2 全文见本文第二部分（原件 `build/r3-acc/audit-r3-fixes-1-attempt2.md`）；本文为两份记录的合并归档，原文零删改 |

---

# 第一部分——attempt 1 验收记录（verdict＝fail，唯一阻断 B-1）

# 整批对抗式独立验收记录——audit-r3-fixes-1（R3 审计三轮修复批）attempt 1

| 字段 | 值 |
| --- | --- |
| 验收对象 | 分支 `origin/audit-r3-fixes-1`，冻结 SHA `604091bf57a147fe2c92a8ecccd1cea190b95377` |
| commit 范围 | `origin/redesign-main..origin/audit-r3-fixes-1`＝**18 提交**（merge-base＝65fe5c92＝redesign-main 尖端，本批直接基于当前主线头） |
| 验收模式 | 整批对抗式验收（acceptance-protocol.md ACC v1.7 §2.2 独立子会话——与实施会话零共享记忆；照 platform-batch4-20261012 先例口径） |
| 复现现场 | 自建 detached 验收工作树 `build/r3-acc-wt` @ 604091bf（`git worktree add --detach`）；冒烟构建树 `build/r3-acc-wt/build-acc`（全新 configure） |
| 验收者证据根 | `build/r3-acc-wt/_r3-acc-evidence/`（116 件：configure/build/gates/ctest/direct-run/变异红绿全日志＋44 份 gtest XML） |
| **裁决** | **fail**——冒烟全量构建 EXIT=1（ui 契约测试 2 处编译错误，R3-C3 公共契约签名变更漏同步 DraftContractTest.cpp）；ACC 4.2 阻断。其余维度（18 提交修复面/红线/门禁/注释/4 处变异红绿闭环）全部通过 |
| 模式声明 | 独立子会话验收（所有者 2026-10-16 指令）；验收者全程未触碰主工作树跟踪面（终测零改动、仍在 redesign-main）、未合并、未推送、findings.json 零触碰 |

---

## 一、冻结核对（双时点）

- 验收始：`git rev-parse origin/audit-r3-fixes-1`＝`604091bf57a147fe2c92a8ecccd1cea190b95377`（fetch 后亲测）✓
- 验收末：同命令复测＝`604091bf…`＝工作树 HEAD＝`604091bf…`——**零漂移** ✓
- `origin/main` 终测仍＝`49a9e819`（冻结位）✓；本批 18 提交无一触碰 main。

## 二、逐提交核验结论表（R3 编号→提交→修复面/回归用例/变异复验）

| # | 提交 | R3 编号 | 修复面在且与描述一致 | 回归用例 | 变异复验 |
| --- | --- | --- | --- | --- | --- |
| 1 | 69de979b | A1/A2 | ✅ ContinuousProcess `+=` 符号修正＋Playback/Export 双处 Σ段时长容差化（1e-9 相对） | ✅ BoundaryTaylorSignA1_C2ContinuousOk（QuadraticCurve 替身） | ⚠ **未能在冒烟面执行**——该回归所属 ContinuousProcessTest.cpp 为**集成条件源**（trajectory/CMakeLists.txt:472 注明），冒烟 exe 仅 34 用例不含该 TU；变异注入后 `--gtest_filter` 0 命中（mutA1-run.log 留证）。**登记 unverified（限冒烟口径）** |
| 2 | 1d38b3a1 | P0-1（F-356 处置） | ✅ canonicalForm 分支翻转删除＋kPi 移除＋注释/单元卡 v0.51 同步 | ✅ ExplicitToDhNegativeDRecoversFactualValue_P0_1 | ✅ **翻红**（重引翻转→`actual: 0.2 vs <0.0` @DhConvertTest:632，与提交描述逐字一致）→还原→MdlDhConvert 14/14 绿 |
| 3 | f9f74e84 | ㊵ | ✅ 0xFFFFFFFE 钳制上界（钳制+1 回绕修复）；Sampling.cpp:57 经同一函数复用核实在案 | ❌ 无新增（既有套件不覆盖 uint32 回绕路径）——**观察项 O-1** | — |
| 4 | 952d9990 | B1 | ✅ admitResult 拒绝路径按 `record.has_value()` 分派（Queued/NotApplicable 保守投影） | ✅ UnknownRunRejectedWithNoArchiveWrite_EX_REG_1 增补两轴投影钉 | ✅ **翻红**（无条件解引用→705/706 双断言红）→还原→RunRegistryAdmissionTest 25/25 绿 |
| 5 | 1b9979b1 | B5 | ✅ ref=/path=/recorded= 三处 `has_value() && !empty()` 前置 | ❌ 无新增（合法载荷绿网同函数覆盖）——**观察项 O-2** | — |
| 6 | 7cb172c3 | A4/A5 | ✅ EvaluatorPorts reviewedIds 唯一身份游标＋JointRun workerFn try/catch＋join 后重抛 | ⚠ A4 提交信息**如实自登**"未单设端到端用例"；A5 无新用例——**观察项 O-3** | — |
| 7 | bf531476 | A7 | ✅ 碰撞三态按 bestSolution 凭据分派（无凭据→2 保守未评价） | ❌ 既有两处 collisionClass 断言均走 collisionNotEvaluated=true 支，**新 else 支无直接回归钉**——**观察项 O-4** | — |
| 8 | 23320b75 | A10/A11 | ✅ freeRotorComplete 齐备性核对（守卫扩展进矩阵门）＋checkVirtualWork 模型维度防御 | ❌ 无新增（提交信息如实登记）——**观察项 O-5** | — |
| 9 | 9330fdd7 | C1/C7/C5 | ✅ openRecentProject 直开钩子（WorkbenchContent＋UiPlugin:933 同源接线）＋detachRegistries＋快捷键先断连再 deleteLater＋PropertyInspector QPointer/所有权移交 | ❌ 无新增（拆卸窗时序面，ui 既有套件绿网）——**观察项 O-6** | — |
| 10 | 56031c5c | B6/复审P2 | ✅ RevisionIndex::rollbackCommitRegistration＋TxEngine RAII PhantomRegistrationGuard（提交点解除武装）＋C 方阵性守卫 | ❌ 无新增——**观察项 O-7** | — |
| 11 | d20b7be6 | A6/C6/C2/C4 | ✅ quickScreenedInBase 判别式守卫＋同项目重开清退＋切换流先迁移再 requestClose＋m_drainIntent 消费复位＋onRejectedSubmit/setRejectedFeedback 反馈钩子（含 RunRegistry.hpp 契约注回写 P3-3） | ✅ SameProjectReopenPurgesBackgroundCopy_R3_C6（另有 A6/C2 首版被既有用例对抗翻红的迭代诚实记录） | ✅ **翻红**（清退失效→461/463/464 三断言红）→还原→TaskPresentationModelTest 13/13 绿 |
| 12 | 8d71a5f6 | B2/B3 | ✅ beginRead 同步完成 SetEvent 收割＋writeCommand 超时 CancelIoEx 后 GetOverlappedResult(bWait=TRUE) 收口 | ❌ 无新增（真进程管道协议/心跳/超时既有套件覆盖；竞态面难确定性钉）——**观察项 O-8** | — |
| 13 | 41bdbd4a | A8 | ✅ computeGeometry 权威角直用（`qAuth[i]-zeroOffset` 剥离删除）＋文件头/函数注/字段注三处口径改写 | ❌ 无新增（既有夹具 zeroOffset=0 两口径同值——提交信息如实登记）——**观察项 O-9** | — |
| 14 | 70f274e6 | A3 | ✅ setAxesPreNormalized 声明位＋build 路径分派＋parse 声明 true | ✅ NonCoordinateAxisRoundtripsExactly_R3_A3（9³ 轴候选确定性寻漂） | ✅ **翻红**（摘除声明→`parsed.ok()` false @CodecTest:116）→还原→CodecRoundtripTest 6/6 绿 |
| 15 | eb96ff12 | B4 | ✅ setReleaseListener 接缝＋析构注销＋purgeReleasedTask 全簿记摘除 | ❌ 无新增（UAF 场景既有套件无监听者形态）——**观察项 O-10** | — |
| 16 | bdc5d354 | A12/A14/A15 | ✅ RelKeyNormalizer.hpp 单点抽取（XacroExpand 零语义迁移）＋Import mesh 键按所在目录解析＋CanonicalBridge 折叠轴规格化＋resolvePartSlot token/variant 一致性校验 | ❌ 无新增（既有 Import/Xacro 全套件绿网）——**观察项 O-11** | — |
| 17 | b3d586d9 | A16/C3 | ✅ CombinationCheck 电机侧九字段显式 reset＋SerialTaskExecutor::post bool 受理位＋DraftController 拒收值轨收口/在途计数就地回收 | ⚠ 测试侧仅两替身**签名对齐**（恒受理），C3"任务被丢弃"变体无新用例——**观察项 O-12**；**且本提交即阻断项来源（见 §三 B-1）** | — |
| 18 | 604091bf | A13 | ✅ saneCount 预算前置＋remaining()，**恰 11 处**调用点逐处核到（681/819/864/909/993/1098/1112/1174/1186/1202/1211 行） | ❌ 无新增（合法载荷下界零误拒由既有编解码套件覆盖）——**观察项 O-13** | — |

**小结**：18/18 修复面在且与提交描述一致（不修过头不修漏——抽样通读各修复函数全貌均吻合审计条目语义）；5 提交带新回归用例；4 处变异红绿闭环（modeling/runtime/execution/ui 簇各 1）；1 处（A1）受集成条件源限制登记 unverified。

## 三、阻断项与观察项

### 阻断项（fail 依据）

- **B-1（阻断）冒烟全量构建 EXIT=1——R3-C3 公共契约签名变更漏同步既有契约测试装配**。
  - 证据：`_r3-acc-evidence/acc-smoke-build.log:1245/1285`——`ui/contract_test/DraftContractTest.cpp(219,13)` 与 `(222,13)` **error C2679**（void lambda／`std::function<void(std::function<void()>)>` 无法赋给 C3 改签后的 `std::function<bool(std::function<void()>)>`）；唯一失败项目 `sdurws_ird_ui_contract_test.vcxproj`，全日志编译/链接错误**仅此 2 条**。
  - 根因：b3d586d9 将 `IDraftController.hpp` Deps 的 `postToDiskThread/postToUiThread` 由 void 改 bool 受理位，同步了对齐 `DraftControllerModelTest.cpp`/`HostControllerModelTest.cpp` 两处替身（"两处测试替身随签名对齐"），**漏改 WP-10-T12 遗留的 `DraftContractTest.cpp` 装配**（该文件本批零触碰、上一触提交＝3c51a1c5f）。该 TU 在集成模式同样参与编译——**集成全量构建亦不可通过**（本验收未另行重跑集成全量构建：冒烟已 decisively fail；集成树受并行 E2 工作流占用不可用，如实登记）。
  - 复现命令：`cmake -S <wt>/RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B <wt>/build-acc -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=<主仓>/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64 -DCMAKE_CXX_FLAGS="-DNOMINMAX -DWIN32_LEAN_AND_MEAN -DWIN32 -EHsc"` → `cmake --build build-acc --config Release`（EXIT=1）。
  - 连带：b3d586d9 验证段仅声明三具名目标（selection/ui/ui_plugin）构建零错误——表述本身未虚报，但 **DoD 第 1 条（双模式全量零错误）未达成即发起验收**，属流程阻断；修复方向＝DraftContractTest.cpp 两处装配补 bool 受理位形态（与两处替身同款）＋全量构建复跑。
  - 测试面后果：sdurws_ird_ui_contract_test **Not Run**（exe 未产出）——契约面 0/1；其余 **44/45 套件全绿**（acc-ctest-all.log：SUITE PASS=44 FAIL=1，唯一 FAIL 即该 Not Run）。

### 建议级观察项（不阻塞合入决策，建议随返工批或治理批处置）

- **O-1**（R3-㊵）饱和钳制回绕无回归用例；**O-2**（B5）非法 hex 载荷拒绝无专例；**O-3**（A4/A5）重复补丁漏复核/并行异常传播无端到端用例（A4 为提交信息如实自登）；**O-4**（A7）"无凭据→2"新分支无直接断言；**O-5**（A10/A11）部分转子降级/维度防御无用例；**O-6**（C1/C7/C5）UI 拆卸窗时序无用例；**O-7**（B6）幻影注册回滚/方阵守卫无用例；**O-8**（B2/B3）同步完成收割竞态无用例（难确定性构造，理由可接受）；**O-9**（A8）非零 zeroOffset 快照无用例；**O-10**（B4）释放联动无用例；**O-11**（A12/A14/A15）嵌套布局/非单位轴/token 失配无用例；**O-12**（C3）停止后提交"任务被丢弃"变体无用例（替身恒受理——拒绝面在生产执行器，可经 PortAdapters 直测补齐）；**O-13**（A13）伪造计数→MalformedPayload 无专例。
  - 共性建议：本批 18 提交中 13 个无新增回归用例，依赖既有绿网（如实登记）；建议返工批随 B-1 修复顺带为**高危语义面**（至少 O-1/O-4/O-9/O-12——数据损坏/保守呈现/权威口径/UI 假死四类）补确定性用例。
- **O-14** 本批未携带任何库内留痕（tasks//traceability 下无 gtest XML/ird-test-report.json/构建日志）；提交信息内计数（如 trajectory 182/182、modeling 380/380、ui 282/282、runtime 285/285）为**集成树口径**，与冒烟实测计数（34/312/256/188）的差异经查＝集成条件源既有惯例（WP-16-T04 口径，trajectory/CMakeLists.txt:472 等），非虚报；execution 157/project 277/evidence 205/requirements 214 与冒烟逐位一致。返工批建议按 §4.2 留痕规范随批落库。
- **O-15** A1 变异复验受集成条件源限制未能在冒烟面执行（本记录 §二 #1）——返工批全量验收时应在集成树补做该变异红绿闭环。

## 四、ird_gates 对账

| 项 | 结果 |
| --- | --- |
| 验收树（audit-r3-fixes-1 源） | `cmake -DIRD_ROOT=<wt>/…/industrialrobot -P cmake/ird_gates.cmake` → **EXIT=0，R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中**（acc-gates-audit-branch.log，含引擎自测 17 项全按预期） |
| 基线（redesign-main 源，只读调用） | 同引擎同口径 → **EXIT=0 零命中**（acc-gates-baseline-redesign-main.log） |
| 对账 | 两日志除扫描根路径外**逐字节一致**（diff 空）：**新增 0／消除 0** |
| 与"基线 22 命中"的关系 | 指令预期的"基线 22 命中"为 **E2 各批时代（b90a442d 基）集成树口径的历史基线**（_perf_test/_largedata_perf_test/_openbench_worker 目标族形态解析表外边）。该基线已随 **WP-01-T05（55f6b641，ird_gates 词形精确识别修复）在本批与主线的共同祖先中合入而消除**（`git merge-base --is-ancestor 55f6b641 origin/audit-r3-fixes-1` 实测 true）——当前基线即零命中，非本批所致增删。本批对 ird_gates 引擎/白名单**零触碰**（diff 全量文件面核对） |

## 五、ACC 4.1~4.11 逐项结论

| # | 检查项 | 结论 | 证据/说明 |
| --- | --- | --- | --- |
| 4.1 | 改动范围 | ✅ | 54 文件全在 `industrialrobot/` 产品区＋1 文档（units/modeling.md v0.51）；本批为审计修复批无 canonical 契约 allowedFiles 面；forbiddenFiles（findings.json/ird_gates 脚本与白名单/patches/框架源码）**全零触碰**（`git diff --name-only` 全量核对＋专项 grep 空输出）；无未登记框架改动（SA-02 面零命中互证） |
| 4.2 | 可复现构建 | ❌ **阻断（B-1）** | 冒烟 configure EXIT=0（acc-smoke-configure.log）→全量构建 **EXIT=1**（2 处 C2679，§三）；集成模式未另行重跑（冒烟已 decisively fail＋集成树被并行 E2 工作流占用——如实登记）；ird_gates 目标口径已按脚本模式亲跑（§四） |
| 4.3 | 契约 verify 命令 | ➖（无契约） | 审计修复批无 canonical 契约（r2d 先例口径）；验证面＝构建/门禁/全套件测试，均亲跑（44/45 见 4.10） |
| 4.4 | acceptance 条目 | ➖（无契约） | 各提交"验证（How）"段逐条对照亲测结果核对（§二表）；实施计数为集成口径（O-14 已定性）；**未执行项零粉饰**——实施提交对未建目标未声明通过 |
| 4.5 | 测试真实失败能力 | ✅ | 变异翻红 4 处红绿闭环（modeling/runtime/execution/ui 簇各 1，§二表）＋1 处 unverified 如实登记（A1 集成条件源）；每次还原后 git status 跟踪面干净、全套件无残留 |
| 4.6 | 架构红线 | ✅ | AGENTS §5 八条逐条：①L2 零 Qt——diff 新增 include 仅标准库 8 个＋QPointer（仅 ui/src/PropertyInspector.cpp，L5 合法）＋RelKeyNormalizer.hpp（单元内）；②R-1——本批零 CMakeLists 改动（无新链接边）＋ird_gates R-1/SUB 零命中互证；③R-2——新增私有头 RelKeyNormalizer.hpp 仅被 modeling 两文件 include（全库 grep 实证）；④SA-02——框架零触碰；⑤R-4——ird_gates 零命中；⑥PA-1——修订/权威面改动均在各自权威单元（B6 于 project、A16 于 selection）；⑦PA-2——B6 守卫明文"HEAD 切换后不回滚（D-18）"；⑧old/ 零引用（diff 零命中） |
| 4.7 | 注释规范 | ✅ | 新增私有头 RelKeyNormalizer.hpp 全查合格（文件头设计依据锚点＋Doxygen @param 方向/边界语义＋逐段中文）；批内公共头 7 个全查合格（Controller.setReleaseListener/Scheduler.purgeReleasedTask/CanonicalModel.setAxesPreNormalized/RunRegistry 契约注/IDraftController 受理位契约/IGlobalShortcutRegistry.onRejectedSubmit/WorkbenchContent.openRecentProject——均含 R3 溯源＋线程纪律＋语义边界）；实现文件抽查 ≥5（ContinuousProcess/Playback/MappingCore/Admission/ChannelPair/DhConvert/Codec/TxEngine/DraftController/TaskPresentation）——高危信息（单位/坐标系/错误语义/确定性来源/线程约束）齐备 |
| 4.8 | 文档同步 | ✅（限定面） | P0-1 涉输出面语义作废——units/modeling.md v0.51 随批登记（版本行＋履历行双落位，与代码逐条一致）；其余 17 提交为缺陷修复不涉登记语义变更（B1/C3/A3 的语义回写走产品契约注释——RunRegistry.hpp/IDraftController.hpp/CanonicalModel.hpp 契约注亲读在案）；findings.json 相关性：R3 审计条目登记于 `audit/round3-20261015/`（库外工作面），非 findings.json 条目，无翻牌义务；F-356（唯一涉库 findings）处置与登记注记一致（"按审计结果修复"经单元卡 v0.51 落地） |
| 4.9 | commit 质量 | ✅ | 18 提交标题均 `[audit-r3-fix][R3-xx]` 前缀＋动词开头中文摘要（审计批前缀沿 E2/audit-r2 先例）；正文四段（What/Why/验证/影响面）逐提交核验齐备；**诚实性突出**：d20b7be6 如实登记首版守卫被既有用例对抗翻红的迭代过程、7cb172c3 如实自登 A4 无端到端用例、多处"如实登记"表述与验收者亲测一致；无粉饰（唯 B-1 所涉 b3d586d9 的三目标限定表述未覆盖 DoD 全量口径——已在 B-1 连带段定性） |
| 4.10 | 留痕完整 | ⚠ | 验收者亲跑留痕齐备（gtest XML 44 份＋逐套件 ctest 日志 45 份＋直跑 summary 63 目标＋构建/门禁日志——全在证据根）；**库内实施留痕缺失**（O-14，建议级） |
| 4.11 | 偷懒/缩水扫描 | ✅ | diff 范围 `TODO\|FIXME\|stub\|placeholder\|未实现\|not implemented` 扫描零命中（仅既有中文语境词形）；空实现零（公共接口函数体均有实质逻辑）；acceptance 逐条证据＝§二表；测试强度经 4 处变异证明非恒真 |

**pass/fail 判据对照**：4.2 阻断命中 → **verdict＝fail**（ACC §5：任一阻断级问题即 fail）。修复 B-1 后须**全量重验**（不做增量验收）。

## 六、合并冲突预报（供后续合入批使用）

**基线事实**：merge-base(origin/redesign-main, origin/audit-r3-fixes-1)＝65fe5c92＝redesign-main **当前尖端**——本批基于主线头直接生长，此刻合并为零冲突快进式合并。

**与指令所列主线已合入 E2 修复面的文件级交集**：

| E2 面 | E2 涉文件 | 本批涉文件 | git 冲突风险 | 语义冲突风险与建议 |
| --- | --- | --- | --- | --- |
| E2-DT | drivetrain/src/DriveTrainCodec.cpp、test/MappingGoldenTest.cpp、test/TestMainReport.cpp | drivetrain/src/**MappingCore.cpp**（A10/A11/B6 同单元**不同文件**） | **零** | 低——codec 解码侧 vs mapping 计算侧无共享状态；无并集动作需要 |
| E2-TRJ | trajectory/src/Sequence.cpp、test/PtpSequenceTest.cpp | trajectory/src/{ContinuousProcess,Export,Playback}.cpp、test/ContinuousProcessTest.cpp | **零** | 低——序列展开 vs 回放表/连续工艺边界/导出，无交集调用面；双方语义并集无需调和 |
| E2-UI-A / E2-HOTFIX | ui/CMakeLists.txt、ui/test/DefectPinningTest.cpp、ui/gui_test/WorkbenchContentGuiTest.cpp | ui/src/{WorkbenchContent,CommandPalette,GlobalShortcutRegistry,PropertyInspector,UiSessionController,TaskPresentation,DraftController}.cpp 等 18 文件 | **零**（CMakeLists 本批零触碰） | **需关注（已核安全）**：DefectPinningTest 为源码形态扫描器——F-557 钉扎要求 WorkbenchContent.cpp 注释剥离后 `a.disableReasonKey.empty()` ≥2 处＋"命令未执行"在位；F-554 要求 UiPlugin.cpp `currentRequirementsCheckContext()` 消费 ≥7 且 `CheckContext{}` 零命中。**本批树实测：守卫恰 2 处（:1578/:1627）、文案在位、消费者 8、CheckContext{} 为 0**——本批 C4 重构（拒绝半区抽出 reportCommandRejection）与 C1/C3 接线**未破坏任何钉扎形态**，合并后 ui_test 预期绿；建议合入批照常跑 ui 套件复测即可 |
| 在飞 E2 修复工作流 | 未知（同机并行） | — | 取决于其落点 | **前瞻建议**：若在飞批次后续向 redesign-main 落execution/ui 域提交（ Scheduler.cpp/Controller.cpp/UiSessionController.cpp 等是 E2 类 findings 的高概率落点），与本批 54 文件即存在真实冲突可能；**建议本批 B-1 返工后尽快合入，或合入前以当时 redesign-main 重算三方交集**；任何 ui 重叠以双方语义并集为准（本批改动多为守卫增补，并集兼容性良好） |

## 七、验收者环境说明

- OS Windows 11 x64／MSVC 2022＋VS 生成器 17／Qt 6.11.1 msvc2022_64／vcpkg 经典模式（主仓绝对路径 toolchain——F-628 口径）；configure 增补 `-DNOMINMAX -DWIN32_LEAN_AND_MEAN -DWIN32 -EHsc`（照 platform-batch4 先例）。
- 本机有并行 E2 修复工作流构建负载：全量构建约 45 分钟属预期；测试直跑以 `build/acc-staging-mut2/bin`（python313.dll）＋主仓框架/Qt/vcpkg bin 前置 PATH（F-007/F-475/F-715 口径），**零 SKIP**（63 目标直跑全 EXIT=0）。
- 集成模式构建未另行复跑（冒烟已阻断性失败＋主仓 build/ 树被并行工作流占用不可竞争写入）——如实登记，不影响 fail 裁决的成立。
- 写入面自查：主工作树跟踪面终测零改动（仍在 redesign-main）；全部写入限于 `build/r3-acc-wt/`（工作树＋构建树＋证据根）与 `build/r3-acc/`（本记录）；findings.json 零触碰；无任何 commit/push/merge 动作。

## 八、证据根清单（build/r3-acc-wt/_r3-acc-evidence/）

- 配置/构建：`acc-smoke-configure.log`（EXIT=0）、`acc-smoke-build.log`（EXIT=1；error 行 1245/1285）
- 门禁：`acc-gates-audit-branch.log`／`acc-gates-baseline-redesign-main.log`（均 EXIT=0 零命中，逐字节一致）
- 测试：`acc-ctest-all.log`（SUITE PASS=44 FAIL=1）＋`ctest-sdurws_ird_*.log` 45 份；`acc-direct-summary.txt`（63 目标直跑全 EXIT=0 含 gtest 计数）＋`direct-*.log` 63 份＋`xml/` 44 份 gtest XML
- 变异：`mutA1-{build,run}.log`（0 命中——集成条件源登记）、`mutP01-{build,run,restore-*}.log`（红→绿）、`mutA3-…`（红→绿）、`mutB1-…`（红→绿）、`mutC6-…`（红→绿）
- 脚本：`run-all-tests.sh`／`run-direct-xml.sh`（逐单元 ctest 与直跑口径——可复跑）

—— 验收员：独立子会话（整批对抗式），2026-10-16

---

# 第二部分——attempt 2 复验记录（verdict＝pass）

# 整批对抗式独立验收记录——audit-r3-fixes-1（R3 审计三轮修复批）attempt 2

| 字段 | 值 |
| --- | --- |
| 验收对象 | 分支 `origin/audit-r3-fixes-1`，冻结 SHA `7106d191a3e1e095a950c227a68ea86993f47ec3`（＝attempt 1 冻结点 604091bf ＋ 4 返工提交 779149e0/093ed0d9/f956c737/7106d191） |
| commit 范围 | `origin/redesign-main..origin/audit-r3-fixes-1`＝**22 提交**（merge-base 仍＝65fe5c92＝attempt 1 时的主线头；本批 18 提交面在 attempt 1 已逐条全验，本 attempt 复验返工面＋抽验已过面） |
| 验收模式 | attempt 2 复验（acceptance-protocol.md ACC §2.2 独立子会话——与实施/attempt 1 验收零共享记忆；唯一阻断 B-1 返工后按指令做全量重验面） |
| 复现现场 | 自建 detached 复验工作树 `build/r3-acc-wt2` @ 7106d191（`git worktree add --detach`，先 fetch 亲核 tip）；冒烟构建树 `build/r3-acc-wt2/build-acc`（全新 configure，照 attempt 1 §三复现命令） |
| 验收者证据根 | `build/r3-acc-wt2/_r3-acc-evidence2/`（110 件：configure/build/gates 两口径/ctest 两轮各 45 套件日志/3 条变异红绿 12 件/直跑日志） |
| **裁决** | **pass**——B-1 闭合亲证（全量构建 EXIT=0 零 error、attempt 1 唯一 FAIL 套件本轮两轮均绿、全树旧签名零残留）；四条 O 用例全部具备真实失败能力（3 条验收者亲做变异红绿闭环＋O-12 双面直跑绿＋返工批红日志核真）；attempt 1 已过面抽查 3 条在位；ird_gates 零命中对零命中；ctest 两轮 45/45。**无阻断、无新增 fail 项** |
| 模式声明 | 独立子会话复验（所有者 2026-10-16 指令）；写权限仅限 `build/r3-acc-wt2/`＋本记录；主工作树（redesign-main）跟踪面终测零改动、未合并、未推送、findings.json 零触碰 |

---

## 一、冻结核对（双时点）

- 复验始（fetch 后亲测）：`git rev-parse origin/audit-r3-fixes-1`＝`7106d191…`＝申报 tip ✓
- 复验末复测：origin 与复验工作树 HEAD 均＝`7106d191…`——**零漂移** ✓
- `origin/main` 终测仍＝`49a9e819`（冻结位）✓；返工 4 提交无一触碰 main（diff 面核对）。

## 二、返工面逐提交核验（四提交 diff 全量亲读）

| # | 提交 | 申报 | diff 亲读结论 |
| --- | --- | --- | --- |
| 1 | 779149e0 | B-1 装配面修＋O-12 | ✅ 与申报逐条一致：DraftContractTest.cpp 构造形参改 `std::function<bool(std::function<void()>)>`（:220）、postToUiThread 内联 lambda 补 `-> bool` 返回（:229-232——attempt 1 故障行 :219/:222 即此两处）、三调用点 lambda 补受理位返 true（:287/:333/:381 一带）；新增 ui/contract_test/PortAdaptersExecutorContractTest.cpp（106 行，含 PostAccepted/PostRejected 两用例）；DraftControllerModelTest.cpp 替身增拒收开关（默认恒受理——既有用例零感知）＋消费侧 SaveAndDiscardReturnExecutorStoppedWhenPostRejected_R3_C3；ui/CMakeLists.txt 为契约测试目标增列新 TU＋同源编入 app/PortAdapters.cpp（CrashCapture/DomainAssembly 先例形态，测试目标内私有头授权——无产品链接边）。**返工总面 7 文件 +391/−8，产品代码零触碰——18 提交修复面相对 604091bf 零漂移** |
| 2 | 093ed0d9 | O-1 | ✅ 纯测试新增（SamplingTest.cpp +44）：SaturationClampDoesNotWrapCountsToZero_R3_35——①病态面商 1e10 钳制饱和 0xFFFFFFFF＋②边界紧邻值 0xFFFFFFFE＋1 不回绕，双锚定 |
| 3 | f956c737 | O-4 | ✅ 纯测试新增（WorkspaceTest.cpp +83）：PointCloudCollisionDispatchByEvaluationCredential_R3_A7——会话在场替身＋按 z 分流双轨样本自证（防空断言恒绿）＋逐样本三态分派钉 |
| 4 | 7106d191 | O-9 | ✅ 纯测试新增（RneaTest.cpp +55）：NonZeroZeroOffsetSnapshotUsesAuthoritativeAngle_R3_A8——mutableChain 注入 0.7 rad 非零偏置，权威角直用＝与零偏置解析解逐位对照（判别裕度 ≈30%） |

## 三、B-1 闭合亲证（attempt 1 唯一阻断）

| 项 | 结论 | 证据 |
| --- | --- | --- |
| 全新冒烟 configure | ✅ EXIT=0 | `acc2-smoke-configure.log`（vcpkg toolchain＋Qt 前缀＋`-DNOMINMAX -DWIN32_LEAN_AND_MEAN -DWIN32 -EHsc`，照 attempt 1 §三口径） |
| 全量构建 | ✅ **EXIT=0，error C/LNK 计数 0** | `acc2-smoke-build.log`（末行 `BUILD EXIT=0`；attempt 1 唯一失败项目 `sdurws_ird_ui_contract_test.vcxproj` 本轮 :1254 产出 exe；PortAdapters.cpp:1236/1252 编入在列） |
| 唯一 FAIL 套件复绿 | ✅ ctest 两轮均 Passed（1.08 s） | `round1/ctest-sdurws_ird_ui_contract_test.log`、`round2/` 同 |
| 契约面直跑 | ✅ 5 用例 [OK]（含 B-1 装配面 3 用例＋O-12 执行器面 2 用例） | `direct-ui-contract-new.log`（EXIT=0） |
| 旧签名残留 | ✅ 全树 `std::function<void(std::function<void()>)>` **零命中** | grep --include=*.cpp/hpp 全工作树；全部接线点（IDraftController.hpp:461/465、UiPlugin.cpp:2558/2565、DraftControllerModelTest.cpp:320/323、HostControllerModelTest.cpp:262/266、DraftContractTest.cpp:220/229）均为 bool 受理位形态 |

## 四、四条 O 用例真实失败能力复验

**验收者亲做变异闭环 3 条**（单点还原→红→还原→绿；每次还原后 `git diff`/`git status` 核对跟踪面零残留）：

| 项 | 注入 | 红（亲测） | 还原后绿（亲测） |
| --- | --- | --- | --- |
| O-1 | Services.cpp:312 钳制上界 0xFFFFFFFEu→0xFFFFFFFFu | `mutO1-run-red.log` EXIT=1——三轴 counts 实得 0 vs 期望 4294967295（回绕复现） | `mutO1-run-green.log` [PASSED] 1 test EXIT=0 |
| O-4 | Workspace.cpp:731 兜底 else `2U→0U`（仅无凭据支） | `mutO4-run-red.log` EXIT=1——无凭据样本 2/5/8… 逐样本误标 0，10 处相等断言翻红 | `mutO4-run-green.log` [PASSED] 1 test EXIT=0 |
| O-9 | InverseDynamics.cpp:777 权威角直用→还原 `qAuth[i] - j.zeroOffset` | `mutO9-run-red.log` EXIT=1——重力矩实得 −7.0029/−0.2501 vs 期望 −10.3005/−1.4715（**与返工批自证红日志逐位一致——交叉印证**） | `mutO9-run-green.log` [PASSED] 1 test EXIT=0 |

**未亲做变异 1 条（O-12）**：核返工批自证证据 `build/r3-rework-wt/_r3-rework-evidence/`——`mutO12a-run-red.log`（PortAdapters post 还原无条件入队＋恒受理→停止后 accepted 实得 true 翻红）、`mutO12b-run-red.log`（消费侧收口摘除→**RUN 后永久阻塞被 90 s 超时击杀＝原缺陷"假死"形态复现**，README 登记 EXIT=124）→各自绿日志在案；验收者另在本树直跑双面绿：执行器面 `PortAdaptersExecutor.Post{Accepted,Rejected}…R3_C3` [OK]（`direct-ui-contract-new.log`）＋消费侧 `SaveAndDiscardReturnExecutorStoppedWhenPostRejected_R3_C3` [OK]（`direct-ui-model-consumer.log`）。**四条 O 用例全部具备红绿闭环证据，判定真实失败能力成立。**

## 五、attempt 1 已过面抽查（不重复全量）

| 抽查 | 结论 |
| --- | --- |
| R3-㊵（f9f74e84） | ✅ Services.cpp:312 钳制上界 0xFFFFFFFEu＋修复注释块在位（复验工作树亲读；过程中曾误 grep 主工作树旧代码报"疑似回归"，经 git show 7106d191 blob 比对证为主树本就无此修复——非分支问题，登记以示留痕） |
| R3-A7（bf531476） | ✅ Workspace.cpp:722-731 四支分派在位（collisionNotEvaluated→2／inCollision→1／有解→0／无凭据→2 保守未评价） |
| R3-A8（41bdbd4a） | ✅ InverseDynamics.cpp 剥离已删（`qAuth−zeroOffset` 计算式零命中）＋:34/:223/:769-775 文件头/字段注/函数注三处口径改写在位 |
| （加验）R3-A13（604091bf） | ✅ saneCount 预算前置在位（modeling/src/Codec.cpp 12 处引用：定义:661＋:681/:819/:864/:909… 调用点） |

**红线复核**：findings.json／ird_gates 脚本与白名单／patches／框架源码——`git diff --name-only 65fe5c92 7106d191` 全量核对**零触碰**；产品区外仅 `RobWork/doc/industrial-robot-design/units/modeling.md`（attempt 1 已登记的 v0.51 文档同步）；`origin/main` 冻结在位；L2 零 Qt——返工 7 文件 Qt include 计数全 0（新 TU 仅 std 头＋ird 公共头）；R-1/R-2——返工 CMakeLists 改动仅测试目标内源增列（无产品链接边），ird_gates R-1/SUB 零命中互证；PA-2/old/——本批零触碰（attempt 1 结论承继）。

## 六、ird_gates 对账

| 项 | 结果 |
| --- | --- |
| 复验树（audit-r3-fixes-1 源） | `cmake -DIRD_ROOT=<wt2>/…/industrialrobot -P cmake/ird_gates.cmake` → **EXIT=0，R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中**（acc2-gates-audit-branch.log，含引擎自测按预期） |
| 基线（redesign-main 源，主工作树只读调用） | 同引擎同口径 → **EXIT=0 零命中**（acc2-gates-baseline-redesign-main.log） |
| 对账 | 两日志除扫描根路径外**逐字节一致**（diff 空）：**新增 0／消除 0**；与指令预期"当前基线为零命中"一致（attempt 1 已定性：历史 22 命中基线随 WP-01-T05 于共同祖先消除，非本批增删） |

## 七、ctest 全套件两轮

| 轮次 | 时机 | 结果 |
| --- | --- | --- |
| 第一轮 | 全量构建后、变异前 | **SUITE PASS=45 FAIL=0**（round1/acc2-ctest-all-round1-console.log；45 份 ctest-sdurws_ird_*.log 全绿——attempt 1 唯一 FAIL 的 sdurws_ird_ui_contract_test 在列且 Passed） |
| 第二轮 | 三条变异全部还原重建后（终态确认） | **SUITE PASS=45 FAIL=0**（round2/acc2-ctest-all-round2-console.log） |

## 八、ACC 4.1~4.11 逐项结论

| # | 检查项 | 结论 | 证据/说明 |
| --- | --- | --- | --- |
| 4.1 | 改动范围 | ✅ | 60 文件（attempt 1 面 54＋返工 6：RneaTest/WorkspaceTest/SamplingTest/ui CMakeLists/PortAdaptersExecutorContractTest/DraftControllerModelTest）全在 industrialrobot 产品区＋1 文档（units/modeling.md）；forbiddenFiles 全零触碰（§五） |
| 4.2 | 可复现构建 | ✅ **（B-1 已闭合）** | 全新 configure EXIT=0＋全量构建 EXIT=0 零 error/LNK（§三）；ird_gates 两口径亲跑零命中（§六）；集成模式构建未复跑（attempt 1 因冒烟阻断未跑；本批集成树被并行 E2 工作流占用——如实登记；冒烟树已含全部 45 套件编译与运行，ui_contract_test TU 集成面同源参与冒烟编译，阻断面已消除） |
| 4.3 | 契约 verify 命令 | ➖（无契约） | 审计修复批无 canonical 契约（attempt 1 同结论）；验证面＝构建/门禁/全套件测试，均亲跑 |
| 4.4 | acceptance 条目 | ✅ | 四返工提交"验证（How）"段逐条对照亲测结果核对（§二/§三/§四）；779149e0 申报的"三调用点受理位返回＋全树无同型残留"亲证成立；**未执行项零粉饰** |
| 4.5 | 测试真实失败能力 | ✅ | 本 attempt 亲做 3 处变异红绿闭环（O-1/O-4/O-9）＋O-12 采纳实施方红证据（含 90 s 假死击杀实录）并双面直跑绿（§四）；attempt 1 的 4 处闭环＋A1 unverified（集成条件源，冒烟口径限制）承继不变；每次还原后跟踪面零残留 |
| 4.6 | 架构红线 | ✅ | AGENTS §5 八条：L2 零 Qt（返工文件 Qt include 全 0）；R-1（返工仅测试目标源增列＋gates R-1/SUB 零命中）；R-2（新 TU include 的 app/PortAdapters.hpp 为同单元私有头、经测试目标内授权——CrashCapture 先例形态）；SA-02（框架零触碰＋gates SA02 零命中）；R-4（gates 零命中）；PA-1（O-12 用例落 ui 契约/模型测试面、O-1 落 requirements 测试面、O-9 落 dynamics 测试面——各归其位）；PA-2（本批零触碰修订面）；old/ 零引用 |
| 4.7 | 注释规范 | ✅ | 新 TU PortAdaptersExecutorContractTest.cpp 全查合格（文件头设计依据锚点——R3-C3/F-819/O-12 溯源＋线程纪律"零 sleep 判据"声明＋每用例语义注）；装配面注释解释受理位语义与"恒受理/拒收面分工"；三条 O 用例注释均含需求/AT 追溯（IRD_TEST_INFO）＋原缺陷形态说明＋翻红点预告（亲测与注释预告一致）；中文详注规范全过 |
| 4.8 | 文档同步 | ✅（限定面） | 返工为纯测试新增＋测试装配修复，无登记语义变更——无文档义务；attempt 1 的 modeling.md v0.51 面未再变动；产品契约注释（IDraftController.hpp 受理位注）attempt 1 已核，本轮复核在位 |
| 4.9 | commit 质量 | ✅ | 四返工提交均 `[audit-r3-fix][…]` 前缀＋动词开头中文摘要＋四段正文；**诚实性延续**：779149e0 正文如实登记首轮新 TU 命名空间笔误（`ui::app` 少写一段，13 条 error，修正后终版零错误——`_r3-rework-evidence/rw-smoke-build-attempt1-namespace-typo.log` 留证）；O 提交均如实申报"纯测试新增、产品代码零触碰" |
| 4.10 | 留痕完整 | ✅（口径同 attempt 1） | 验收者亲跑留痕齐备（110 件全在 _r3-acc-evidence2：configure/build/gates/两轮 ctest 90 份/变异 12 件/直跑）；实施方返工批自证留痕在 `build/r3-rework-wt/_r3-rework-evidence/`（39 件＋README 索引——构建/门禁/两轮全套件/四变异红绿/直跑绿）；库内实施留痕（tasks/traceability 下随批 XML）仍未落库——**O-14 承继为建议级不阻塞**（与 attempt 1 同处置，建议治理批统一清账） |
| 4.11 | 偷懒/缩水扫描 | ✅ | 返工 diff（604091bf..7106d191）`TODO/FIXME/stub/placeholder/未实现/not implemented` 扫描**零命中**；四条 O 用例经变异证明非恒真；新 TU 无空实现；无越界"顺手修"（产品代码零触碰——diff 面核对） |

**pass/fail 判据对照**：attempt 1 唯一阻断 4.2 已闭合且复验无新增阻断 → **verdict＝pass**。

## 九、合并冲突预报（按复验末现场——2026-10-16 复验期间主线两次前移，以下为末次实测）

**基线事实**：merge-base(origin/redesign-main, origin/audit-r3-fixes-1) 仍＝65fe5c92；复验期间主线由 59427caf → 4c705656（本地，E2-RT F-806）→ **d69e0904**（origin 末测，含 E2-IO F-801/802/803、E2-RT F-806、E2-FW F-714 及其收尾批）。合并已**非快进**，为真实三方合并。

| 项 | 现场实测 |
| --- | --- |
| 文件级交集 | 分支 60 文件 × 上游 15 文件 → **恰 2 个**：`runtime/src/Codec.cpp`（分支 A3 worker 物化通道 × 上游 F-806 parseNameMap 唯一性复核）、`ui/CMakeLists.txt`（分支契约测试目标源增列 × 上游 F-714 DefectPinning 注册行注释微调） |
| 试合并（`git merge-tree --write-tree d69e0904 7106d191`，无工作树副作用） | **EXIT=0，零冲突标记，干净合并树 cd60a95d**——两处交集双方改动在不同区域/不同目标块，无文本冲突 |
| 语义冲突风险 | 低：Codec.cpp 双方改不同函数（物化通道轴规格化 vs parseNameMap 去重），无共享状态；ui/CMakeLists.txt 双方改不同 target 块；F-714 回归钉（DefectPinningTest 的 F_714 钉）指向 RWSimulatorPlugin.cpp——本分支零触碰；attempt 1 核过的 F-557/F-554 钉（WorkbenchContent/UiPlugin 形态扫描）在返工后仍成立（返工未触碰 ui/src 任何文件）；**合并后建议照常跑 ui 套件（ui_test＋ui_contract_test）与 runtime 套件复测** |
| 在飞 E2 前瞻 | worktree 清单仍见 e2-rpt-f815-6-wt／e2-ui-b-wt 等在飞面（基于 d69e0904）——本预报为**时点快照**；合入前应以当时 origin/redesign-main 重算三方交集（重算口径：`git merge-tree --write-tree <当时主线tip> 7106d191` 零冲突即可照常合入） |

## 十、复验者环境说明与写入面自查

- OS Windows x64／MSVC 2022＋VS 生成器 17／Qt 6.11.1 msvc2022_64／vcpkg 主仓绝对路径 toolchain（F-628 口径）；同机有并行 E2 工作流负载——全量构建约 60 分钟属预期，未跳步。
- 测试直跑 PATH＝主仓框架/Qt/vcpkg bin＋`build/acc-staging-mut2/bin`（attempt 1 run-direct-xml.sh 同口径）；零 SKIP 异常。
- 集成模式构建未复跑（§4.2 如实登记）；变异复验受限于冒烟树（与 attempt 1 同口径——A1 的集成条件源 unverified 承继）。
- **写入面自查**：主工作树跟踪面终测零改动（git status 无跟踪面条目、仍在 redesign-main）；全部写入限于 `build/r3-acc-wt2/`（工作树＋构建树＋证据根）与 `build/r3-acc/`（本记录）；findings.json 零触碰；无 commit/push/merge/push 动作；试合并经 `git merge-tree` 完成（仅写对象库，不动任何工作树与引用）。

## 十一、证据根清单（build/r3-acc-wt2/_r3-acc-evidence2/，110 件）

- 配置/构建：`acc2-smoke-configure.log`（EXIT=0）、`acc2-smoke-build.log`（EXIT=0 零 error/LNK；ui_contract_test exe :1254 产出）
- 门禁：`acc2-gates-audit-branch.log`／`acc2-gates-baseline-redesign-main.log`（均 EXIT=0 零命中，除扫描根逐字节一致）
- 测试：`round1/`（45 份 ctest 日志＋console 总账 SUITE PASS=45 FAIL=0）、`round2/`（同，终态复跑）、`direct-ui-contract-new.log`（5 用例 [OK]）、`direct-ui-model-consumer.log`（O-12 消费侧 [OK]）
- 变异（亲测三闭环×4 阶段）：`mutO1-{build,run-red,build-restore,run-green}.log`、`mutO4-…`、`mutO9-…`（O-9 红态数值与实施方自证逐位一致）
- 实施方返工自证：`build/r3-rework-wt/_r3-rework-evidence/`（README 索引＋构建/门禁/两轮 45 套件/四变异红绿/直跑绿）

—— 复验员：独立子会话（attempt 2 对抗式复验），2026-10-16
