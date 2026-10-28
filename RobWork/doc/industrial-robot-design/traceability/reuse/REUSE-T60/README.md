# REUSE-T60——RC-09 权限与真实生产 UI 批（ui 19 行定案＋F-975①解锁＋F-974/F-560 复跑）

起始 `0259db9b2`（RC-10 规则批提交），redesign-main 主工作树串行。
权威计划＝[migration.md §13.5.4 RC-09](../../../reuse/migration.md)；授权口径＝所有者 2026-10-27
委托「后续所有裁决授权按建议进行，不再参与，不因此阻断工作流」＋RC-09 行「只提交实际类别/操作/
生命周期的最小权限方案，不批准目录豁免、不借宿主例外移交模型真值」。契约＝
[tasks/REUSE-T60.json](../../../tasks/REUSE-T60.json)；凭证＝[changes/REUSE-T60.json](../../changes/REUSE-T60.json)。

## 0. 结果总览

| 项 | 结果 |
| --- | --- |
| ui 19 行 | 18 行定案 business-specific/migrated（宿主呈现借持/呈现生命周期语义）；1 行（ensureSessionPreviewAnchor）保持 pending 如实登记 construct-model 结构性死锁（详见 §3） |
| permissions.json | +3 grant：HostView3DPreviewBackend.cpp（HOST-RENDER）、HostView3DGateway.cpp（HOST-GATEWAY）、HostCompilePipelineTest.cpp（test-fixture graphics 面）；permissions.json 相对 HEAD 81 行纯新增零删改 |
| F-975 | 三选一按证据选 **①**（门禁 /test/ 路径 test-fixture graphics 允许面）：候选补丁落地＋门禁增面＋正负例＋ARCH §3.6.2.1/v0.18 登记；NamedTcpClosure 实测 PASS |
| F-974 | 复跑解除：studio 冒烟全链 DONE、exit 0（原崩溃点「共享面装配完成」后无段错误）；归因 WP-15-T21（d1c0320c9）同链启动空快照段错误修复 |
| F-560 | 复跑核归属：钉扎②字面锚漂移（生产空态分支投递已改经 applyCombinedPreview 包装，UiPlugin.cpp:4792 终点即网关 applyPreview）；缺陷未复活，F-560 保持 fixed |
| 门禁自测 | test-reuse-governance.ps1 172→175 全绿（+3 例，旧例零回归） |
| 真实 Widgets/数值链 | ui_test 全量 345＝344P/1F（唯一失败＝F-560 预存钉扎）；ui_gui_test 98/98；ird_gates 语义面由上述承载 |
| 独验 | 全部 NOT RUN 待所有者安排（见 §6） |

## 1. 前哨盘点与门禁提取实测

前哨盘点「ui 19 行＝HOST-SCENE 11＋HOST-LINES 6＋HOST-FRAME 1＋KIN-FK 1」经 ui.json 实测核对成立
（全部落在 HostView3DPreviewBackend.cpp/HOST-RENDER 与 HostView3DGateway.cpp/HOST-GATEWAY 两文件，
hostExceptions 族已在案——宿主例外≠行结项：行脱离 pending 审计豁免即触发 IRD-GATE-AD 严格检查）。

门禁（reuse-contract.ps1 CheckSourcePermission）类别/操作提取逐区段实测
（[t60-probe-extract-output.txt](t60-probe-extract-output.txt)，提取正则与门禁 :131-152 逐字一致，
探针为会话工具不入库、方法与输出如前件存档）：

- 15 行区段零类别/零操作提取（呈现 GL 自绘/作用域/装配校验/幂等摘除面）；
- `ui.HostView3DPreviewBackend.draw`＝categories=[model]（rw::models::WorkCell::Ptr 借持查询）；
- `ui.HostView3DGateway.handleViewDoubleClick`／`currentTcpPose`＝categories=[state]（Frame*/State*
  只读借持）；KIN-FK 行无 write-state 提取（无 setQ/setTransform，纯 worldTframe 读）；
- `ui.HostView3DPreviewBackend.ensureSessionPreviewAnchor`＝categories=[model] ops=[construct-model]
  ——**结构性死锁**：门禁模型所有权规则限定 construct-model 仅 runtime-owner（强制 unit=runtime）或
  test-fixture（强制 /test/ 路径）可持，host-presentation 禁持模型构造操作（reuse-contract.ps1
  「不授模型构造/结构写入」与 ARCH §3.6.2「不借宿主例外移交模型真值」红线）。该行保持 pending 如实
  登记（T57 CollisionQuery 2 行同族先例——「本批不代授，回退 pending 如实登记」）。

## 2. F-975 三选一裁决＝选项①（按证据）

| 选项 | 证据评估 | 裁决 |
| --- | --- | --- |
| ① /test/ 路径 test-fixture graphics 允许面 | 沿 3d5753861 test-fixture 先例同型（owner 批准的路径限定豁免＋逐文件 grant＋治理批三件套）；gate-ad-blocked.log 证实唯一缺口＝graphics 类别强制 host-presentation；测试文件全文件提取 [model,state,graphics] ops=[]——零 host/零构造/零 State 写，允准面无红线交集；授权登记仍逐文件 sha256 绑定 | **采纳** |
| ② 宿主例外扩族接纳测试文件 | 破坏 hostExceptions 恰五 ui/plugin/ 生产族不变量（reuse-contract.ps1 $names 硬校验＋target=ui_plugin 校验双破坏）；host-presentation 语义＝宿主呈现装配层，测试脚手架语义错位；改面大于①且 distortion 永久 | 否 |
| ③ UI-T78 渲染面用例重组 | 断言迁往新 TU 后该 TU 仍带 rw/graphics include——同一 IRD-GATE-AD 锁死，③ 单独不可落地（须以①为前置）；改常量钉扎语义＝修订 UI-T78 已验收断言面（渲染分组漂移守卫失效） | 否（且被①支配） |

落地件（REUSE-AD exit 1 → 0 实测翻转）：

1. `reuse-contract.ps1`：test-fixture origin 在 /test/ 路径可持 graphics（仅 graphics、仅 test 路径、
   仅 test-fixture；host 类别仍强制精确宿主例外，非测试路径不变）——ARCH §3.6.2.1 graphics 面扩登
   （[ARCHITECTURE.md](../../../ARCHITECTURE.md) v0.18，所有者 2026-10-27 委托口径）。
2. `test-reuse-governance.ps1`：+3 例（test+graphics 正例／test 路径 host 类别拒／非 test 路径
   test-fixture graphics 拒）；套件 172→175 全绿（[t60-gate-reuse-governance.log](t60-gate-reuse-governance.log)）。
3. `permissions.json`：REUSE-T60-HostCompilePipelineTest.cpp（origin=test-fixture，
   categories=[graphics,model,state]，operations=[borrow]——借用已发布不可变快照本体）。
4. 候选补丁落地：HostCompilePipelineTest.cpp 本地 View 补 runtimeModelView() 锚覆盖
   （UI-T86 README §3 候选补丁逐字应用，:337-343 替换；文件整行摘要
   5b792c915be11ee500e88cc2a083977d6212ecea2a42d5c607448b1efb86fca1）。
5. ui.json 新增整文件行 `ui.HostCompilePipelineTest`（AnchorCaptureExecutorTest 整文件行同型；
   COVERAGE 墙同步解除——该文件此前零行覆盖，任何改动必触 COVERAGE fail）。
6. foundation-governance.json 对 reuse-contract.ps1 的 10 处 input 绑定机械级联重钉（T59 同惯例）。

## 3. ui 19 行逐行定案

18 行 decision=business-specific＋status=migrated（T41 HOST-WORKBENCH 行同型——宿主呈现面业务保留，
in-unit 21 行既resolved全为 business-specific/migrated），nonOverlapEvidence 绑定本 README；
search/differences 按 §1 实测与真实借持/呈现语义刷新。分类登记：

- **HOST-SCENE 11**：呈现生命周期（draw 原子替换先清后挂/clear 幂等/锚三级现取/挂接与失败诚实降级）；
  grant＝REUSE-T60-HostView3DPreviewBackend.cpp（graphics/host/model/state＋query/render）与
  REUSE-T60-HostView3DGateway.cpp（state＋query/borrow）承载提取面。
- **HOST-LINES 6＋HOST-FRAME 1**：graphics 自绘 Render 呈现（构造冻结/作用域恢复/词表供色/诚实空态）
  ——零受限提取，借文件级 grant 的实际类别面登记。
- **KIN-FK 1（currentTcpPose）**：state 类 query/borrow 只读借持（worldTframe 呈现位姿读，KIN-06 零
  修订），与前哨框架一致；T19/T52 的 write-state 形态在本行无对应面（纯读）。

`ensureSessionPreviewAnchor` 保持 pending/pending：search.conclusion 登记 construct-model 墙全链
（提取实测→门禁规则→两墙外无合法形态→语义满足 ARCH 表行但机械规则不认）；differences 登记最小
候选方案（①所有者裁决后门禁增精确锚构造点／②锚构造下沉 runtime 工厂）——均超本批授权，行为不变、
状态如实。

## 4. F-974/F-560 复跑（同批真机证据）

- **F-974**（HEAD 启动段错误，open）：复现命令 `sdurws_ird_studio＋IRD_UI_PLUGIN_SMOKE=auto` 于当前
  HEAD（含 d1c0320c9）重建后复跑：启动越过原崩溃点（「共享面装配完成」后无段错误）、冒烟 8 步全过
  （create-demo/树重建/L1 检查器/draft.apply 6 模块投递/L3 反解/teardown 关停/close-reopen）、
  `[ird-ui-smoke] DONE`、exit 0（[t974-studio-smoke.log](t974-studio-smoke.log)）。归因＝WP-15-T21
  （d1c0320c9）同链修复——其 pre-fix 堆栈（crash-stack-pre-fix.log）即启动装配路径
  KinematicsPanelWidget::refreshVisualization 空指针读，与 F-974 启动段错误同链；精确修复者归属由
  WP-15-T21 独验记录承载，本批按「不再复现＋同链证据」翻牌 fixed。T41 的修订事件双 sink 专项 GUI
  验证仍按其自身 NOT RUN 登记待独验，本批不代签。
- **F-560**（fixed 2026-10-16）：ui_test 全量唯一失败＝`DefectPinning.HostRegionPreviewEmptySet_..._F560`
  断言②（DefectPinningTest.cpp:559 countOccurrences(branch,"applyPreview")=0）。亲读生产分支：空态
  分支语义完好——m_reqBox.reset()＋m_reqGrid.reset()（断言①通过）＋applyCombinedPreview(clearUpdate)
  合并投递 nullopt 双层（UiPlugin.cpp:5346-5367），包装器终点即 `m_view3dGateway->applyPreview(combined)`
  （UiPlugin.cpp:4792）。归属＝**钉扎字面锚漂移**（FIX-T2 空态同拍重构把投递改经 applyCombinedPreview
  包装，钉扎②字面 `applyPreview` 未随更新），非缺陷复活。F-560 保持 fixed；钉扎修订（②字面锚改为
  applyCombinedPreview 或双形态兼容）为产品测试改动，归后续产品测试批（本批不越权）。

## 5. 测试与验证证据（本批实测）

| 证据 | 结果 | 文件 |
| --- | --- | --- |
| ui_test 全量（patched 树，QT_QPA_PLATFORM=windows） | 345＝344P/1F/0S（1F＝F-560 预存钉扎；NamedTcpClosure PASS） | t60-ui-full-suite.log/.xml、t60-ui-ird-test-report.json |
| ui_gui_test 全量（真实 Widgets） | 98/98 PASS exit 0 | t60-ui-gui-suite.log/.xml、t60-ui-gui-ird-test-report.json |
| studio 集成冒烟（IRD_UI_PLUGIN_SMOKE=auto） | 全链 DONE exit 0 | t974-studio-smoke.log |
| 门禁自测套 | 175/175（+3 新例；172 旧例零回归） | t60-gate-reuse-governance.log |
| 类别/操作提取探针 | 19 区段＋3 整文件实测 | t60-probe-extract-output.txt |
| 绑定摘要双运行时复核（node 与 pwsh 逐字复刻门禁 Digest） | 6 绑定逐一一致 | t60-digest-double-runtime.txt |

构建：集成树 `build/`（CMakeCache RWS_BUILD_INDUSTRIALROBOT=ON）`cmake --build build --config
Release --target sdurws_ird_ui_test / sdurws_ird_ui_gui_test / sdurws_ird_studio`，补丁 TU
HostCompilePipelineTest.cpp 实际重编入。

## 6. 独验边界（全部 NOT RUN 待所有者安排）

- 本批实施提交＝实施者自验；独立验收不可自签。分列待独验：①门禁 graphics 面规则（治理批，REUSE-T59
  同批先例「规则未独验前不据其关闭债务」——本批 19 行定案不依赖规则独验，仅 F-975 落地依赖）；②
  F-975 修复面与整文件行登记；③19 行定案语义（business-specific 保留判断＋grant 最小性）；④F-974
  翻牌 fixed 的归因（同链证据在案，精确性由 WP-15-T21 独验承载）；⑤F-560 钉扎漂移归属。
- RC-09 完成判据余量：修订事件双 sink 专项 GUI 验证（T41 NOT RUN 项）待独验会话按其自身范围执行。
