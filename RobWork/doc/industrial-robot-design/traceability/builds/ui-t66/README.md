# UI-T66 构建与测试留痕（建模 R1 尾款合批——类型枚举行面板轨＋分组重演＋F-523① 顺带；重堆叠后全量重做）

分支 `ui-t66`（**重堆叠：基点等价后移 merge redesign-main@69be7234——UI-T64/T65
含返工收尾均已合入主线**；任务契约 tasks/foundation/UI-T66.json；
DTB §2.11 WP-10-T66 行；units/modeling.md v0.33；units/ui.md §13 UI-T66 行；
findings F-523① 消账注）。

**本目录为重堆叠后全量重做**：首轮入库的 gate-all.log 为 0 字节空文件（姊妹批
UI-T65 验收同缺陷判阻断 C 的先例），且缺套件日志与 gtest XML。全部数字来自
重堆叠会话的新鲜执行（2026-10-06，返工树 wt@merge 166a44e4 之后），零复用
首轮日志；本批产品代码零改动（modeling/** 源码与测试保持首批实施原样，
本会话仅重堆叠＋验证＋留痕）。

## 交付物（首批实施，重堆叠后回归复核）

- 分组重演（PanelRefresh.hpp）：PendingEdit 表尾追加 groupId（0＝独立
  单条——属性行单字段轨兼容缺省）＋allocateGroupId 分配器＋
  onRevisionEvent 按组保序重演（组＝连续同 groupId 段；组内任一条被域
  拒＝整组 blocked、blockedAt 指组首、队列现场保留；跨组失败即停不变
  ——L-4 的组粒度推广）。
- 编辑页分组提交接入（ModelingPanelWidget.cpp）：applyJointDetailEditSet
  每轮 allocateGroupId，域接受条目逐条入队同组 id（被拒不入队＝队列恒
  真实编辑意图）；诚实缺席边界登记更新为落位注。
- 类型枚举行面板轨：编辑页 QComboBox 四值词表直投（jointTypeToken 同源
  ——域 Type 分支 L-2 分流复用零域改动）；refreshJointEditPane 回填
  （QSignalBlocker 屏蔽——防换目标幽灵提交）＋L-7 门控＋空态隐藏；
  拒绝轨＝TypeBoundsConflict 就地呈现＋下拉回退权威值。
- F-523①（customChainRowSemanticCheck，ModelingCommandFlows 纯函数）：
  I-MDL-4 限位有序＋I-MDL-6 轴非零——声明表单 accepted 前置逐行校验
  定位到行；域拒绝面兜底语义不变。

## 验证记录（重堆叠会话新鲜执行，2026-10-06）

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 集成构建（全量 Release，build-int，F-525 环境前置 flags） | configure EXIT 0＋全量构建 EXIT 0，error C/LNK/fatal 计数＝**0**（60 warning＝框架既有噪声） | build-integration.log |
| 独立冒烟构建（standalone 树 smoke，vcpkg toolchain＋Qt 前缀） | configure EXIT 0＋全量构建 EXIT 0，error 计数＝**0**（33 warning） | gate-all.log（51049 字节——**0 字节缺陷已由本文件替代**） |
| 冒烟 ctest（14 单元按子目录） | 14 单元全部 100% 通过（合计 28 ctest 项） | ctest-smoke.log |
| 冒烟直跑 sdurws_ird_modeling_test | 280/280（基线 276＋本批 4——**本批新用例在冒烟树真实编译运行，非 if(TARGET sdurws) 门控缺席**） | smoke-modeling-test.log |
| 冒烟直跑 sdurws_ird_modeling_gui_test | 36/36（基线 35＋本批 1——同上，冒烟树实跑） | smoke-modeling-gui.log |
| sdurws_ird_modeling_test（集成） | 339/339（新增 4：Refresh_GroupedReplayWholeGroupsInOrder／Refresh_GroupReplayBlockedKeepsWholeGroup／Refresh_TypeEditReplaySingleTrack／CustomChainRowSemanticCheck_TableDriven——均 _UI_T66 后缀） | modeling-test.log／modeling-test.xml |
| sdurws_ird_modeling_gui_test（集成） | 41/41（新增 1：JointDetailEditPane_TypeComboBoxChainAndRejectFallback_UI_T66——合法轨变更记录＋拒绝轨工作集不变＋下拉回退） | modeling-gui.log／modeling-gui.xml |
| sdurws_ird_modeling_contract_test（集成） | 17/17 | modeling-contract.log |
| sdurws_ird_ui_test（集成） | 254/254（重堆叠后合并树基线 254——含 UI-T65 返工新增双口径发散用例；首轮登记 253 为重堆叠前口径） | ui-test.log／ui-test.xml |
| sdurws_ird_ui_contract_test（集成） | 32/32 | ui-contract.log |
| sdurws_ird_ui_gui_test（集成） | 78/78 | ui-gui.log／ui-gui.xml |
| sdurws_ird_requirements_test（集成） | 200 passed＋1 skipped（V22RegisteredNotExecuted_ACC1 登记 gui 呈现项） | requirements-test.log |
| sdurws_ird_requirements_gui_test（集成） | 44/44 | requirements-gui.log／requirements-gui.xml |
| sdurws_ird_kinematics_test（集成） | 182 passed＋1 skipped（ACC1 登记） | kinematics-test.log／kinematics-test.xml |
| sdurws_ird_kinematics_contract_test（集成） | 57/57 | kinematics-contract.log |
| ird_gates 引擎直调（head 侧＝重堆叠树，cmake -P 直调与 --target ird_gates 同一引擎命令） | 引擎自报 **103 命中**；`[ird_gates] IRD-GATE` 提取恰 103 行 | ird-gates-branch.log |
| ird_gates 引擎直调（base 侧＝69be7234 detached worktree＋独立构建目录 base-int 同口径） | 引擎自报 **103 命中**；提取恰 103 行 | ird-gates-base-hits-normalized.txt |
| ird_gates 归一化多重集比对 | 树前缀→占位符后 sort＋diff：**diff＝0 行（103＝103 逐行全等）——零新增零减少**（ird-gates-diff.txt 的 0 字节即"零差异"本体，非缺文件） | ird-gates-branch-hits-normalized.txt／ird-gates-diff.txt |
| validate-docs | PASS（20 units, 12 trace entries, 244 task files） | 会话执行记录（脚本 stdout 见重堆叠提交正文） |
| validate-task（UI-T66.json） | PASS（1 tasks） | 会话执行记录（脚本 stdout 见重堆叠提交正文） |

## 诚实边界

- 域零改动：Type 分支/I-MDL-4 组合约束已落位 Template.cpp（WP-13 时代）
  ——allowedFiles 排除 modeling src/include，本批纯面板轨与重演组语义；
  重堆叠会话同样零产品代码改动（声明计数 339/41 经本次新鲜执行核实为实）。
- 分组重演的组内前缀落位保留（重演落权威工作集——组内被拒条之前的
  条目已生效，与单字段轨前缀语义同源；整组手工处置指队列处置粒度）。
- 本批新用例（modeling_test 4＋modeling_gui 1）**在冒烟树真实编译运行**
  （280/36 直跑全绿——被测 TU 不在 if(TARGET sdurws) 集成门控面），
  与 UI-T65 新用例门控缺席情形不同；数字 280/36 非构成性引用。
- F-522（§13 历史行分列）与 F-523②（子聚合）不在本批——留治理批次/
  批次 B。
- 集成侧 gtest XML 仅入库新用例所在两套件（modeling-test/modeling-gui，
  随批变更面口径——ui-t65 返工同构成纪律）；其余八套件以逐套件 stdout
  log 为留痕本体，验收复现以 verify 命令为准。
- gui 套件运行于原生窗口平台（未设 QT_QPA_PLATFORM）；ird_gates 双侧
  引擎直调输出为 UTF-8 stdout，MSBuild 包装层输出的多行命中折叠不计
  （以引擎直调日志为留痕本体）。
