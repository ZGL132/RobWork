# UI-T65 构建与测试留痕（需求三维采样着色闭环——F-495 消费卡；返工重做）

分支 `ui-t65`（基点＝堆叠 ui-t64@2972b5d7，返工前已 merge redesign-main@bf54d998
基点等价后移——UI-T64 合入后的新主线尖；任务契约 tasks/foundation/UI-T65.json；
DTB §2.11 WP-10-T65 行；units/ui.md §13 UI-T65 行；findings F-495 两批链进展注）。

**本目录为返工后全量重做**（acc/ui-t65/1 verdict=fail 阻断 C：首轮 gate-all.log
为 0 字节空文件＋套件留痕缺失）——全部数字来自返工会话的新鲜执行（2026-10-06，
返工树 wt@merge 7e4269f6＋修复提交），零复用首轮日志。

## 交付物（含返工增量）

- 协议扩展（View3DPreviewContract.hpp）：View3DCellState 表尾追加
  NotSampled（灰——未采样/未运行诚实态）＋新枚举 View3DTint 四档
  （返工注：Failed 枚举注修正为预留档——映射无生产者，零达标实走 Weak，
  E1 消账）＋View3DBoxOutline 表尾追加 optional<View3DTint> tint（UI-T33
  冻结面追加纪律——缺省 None 语义零破坏）。
- 统一供色（UiTheme.hpp palette）：采样状态色族（四组 GL 三元组＋同源
  换算 hex——三维后端/图例单一供色点）；**返工新增区域框缺省蓝
  kRegionTintDefaultGl/Hex（(0.25,0.45,1.0)＝#4073FF——None/缺省档唯一
  供色点，恢复 UI-T33 以来原值；首轮误为 kPrimary 同值 (0.12,0.35,0.66)
  硬编码，阻断 B/E2 修复）；kSampleSelectedHex 删除（E3 消账——图例选中
  块改与三维框缺省档同源消费 kRegionTintDefaultHex）**。
- 通道值面扩展（KinematicsPanelChannels.hpp）：KinChannelSampleRecord
  表尾追加 position（基座系 m）/regionObjectId（区域锚）/**kind（位置/
  位姿双口径判定键——返工追加）**＋执行器投影补坐标/区域锚/类别
  （results×samples 按 sampleIndex 双射对齐直投）。
- 映射与计数纯函数（KinEvaluationChannel.hpp inline 纯值面）：
  mapSampleStateToCell（域五值→四值词表）＋view3DTintFromCoverage（计数比
  ×目标下限三档——呈现对照非工程判定）＋**返工新增
  tallyRegionPositionCoverage（逐区域位置口径计数——kind 分轴，与域
  KIN-04 computeCoverage 位置轴同定义）＋view3DRegionTintFromSamples
  （计数＋分档一体——合并段唯一调用点）**。
- 投影合并（UiPlugin bindRegionPreviewSink）：执行器账面经
  snapshotId+epoch 对账门（跨快照不投影——PA 权威镜像）→regionObjectId
  逐区域过滤→坐标基座→世界系（worldToBase 逆）→View3DSampleGrid.
  samples/cellStates＋框 tint **调纯函数（位置口径——返工修复：首轮
  为全样本混计，与面板呈现的 computation.coverage.position 口径分叉，
  契约 acceptance 3"位置口径"重实现，所有者 2026-10-06 裁决）**。
- 需求侧：RegionPreviewGeometry/RegionPreviewView 表尾追加
  regionObjectId/minPositionCoverage（面板填充＋门面透传）＋区域页富
  文本图例行（四状态色块＋蓝框选中——palette hex 供色，蓝框与三维框
  缺省档同源）。

## 验证记录（返工会话新鲜执行）

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 集成构建（全量 Release，build-int） | EXIT 0，error C/LNK/fatal 计数＝0（60 warning＝框架既有噪声） | build-integration.log |
| 独立冒烟构建（standalone 树 smoke） | configure EXIT 0＋全量构建 EXIT 0，error 计数＝0 | gate-all.log（51049 字节） |
| 冒烟 ctest（14 单元按子目录） | 14 单元全部 100% 通过（28 tests；E-R2-1 汇总数笔误随 UI-T65 合入收尾更正） | ctest-smoke.log |
| 冒烟直跑 sdurws_ird_ui_test | 239/239（本批新用例位于 if(TARGET sdurws) 集成门控文件——冒烟树不编译不运行，与 ui-t64 侧同数＝构成性结果） | smoke-ui-test.log |
| 冒烟直跑 sdurws_ird_kinematics_test | 176 passed＋1 skipped（ACC1 登记 gui 呈现项） | smoke-kinematics-test.log |
| sdurws_ird_ui_test（集成） | 254/254（合并树基线 253＋返工新增 1——RegionTint_FollowsPositionAxisNotMixed_UI_T65 双口径发散用例） | ui-test.log／ui-test.xml |
| sdurws_ird_ui_contract_test（集成） | 32/32 | ui-contract.log |
| sdurws_ird_ui_gui_test（集成） | 78/78 | ui-gui.log／ui-gui.xml |
| sdurws_ird_requirements_test（集成） | 200 passed＋1 skipped（ACC1 登记） | requirements-test.log |
| sdurws_ird_requirements_gui_test（集成） | 44/44 | requirements-gui.log／requirements-gui.xml |
| sdurws_ird_kinematics_test（集成） | 182 passed＋1 skipped（ACC1 登记） | kinematics-test.log／kinematics-test.xml |
| sdurws_ird_kinematics_contract_test（集成） | 57/57 | kinematics-contract.log |
| sdurws_ird_modeling_test（集成） | 335/335 | modeling-test.log |
| sdurws_ird_modeling_gui_test（集成） | 40/40 | modeling-gui.log |
| sdurws_ird_modeling_contract_test（集成） | 17/17 | modeling-contract.log |
| ird_gates 引擎直调（branch 侧＝返工树） | 引擎自报 **103 命中**；`[ird_gates] IRD-GATE` 提取恰 103 行 | ird-gates-branch.log |
| ird_gates 引擎直调（base 侧＝bf54d998 detached worktree＋独立构建目录同口径） | 引擎自报 **103 命中**；提取恰 103 行 | ird-gates-base-hits-normalized.txt |
| ird_gates 归一化多重集比对 | 树前缀→占位符后 sort＋diff：**diff＝0 行（103＝103 逐行全等）——零新增零减少**（ird-gates-diff.txt 的 0 字节即"零差异"本体，非缺文件） | ird-gates-branch-hits-normalized.txt／ird-gates-diff.txt |
| validate-docs | PASS（20 units, 12 trace entries, 243 task files） | 会话执行记录（脚本 stdout 见返工提交正文） |
| validate-task（UI-T65.json） | PASS（1 tasks） | 会话执行记录（脚本 stdout 见返工提交正文） |

## 诚实边界

- 着色判定零参与：映射＝词表翻译、框色＝呈现对照（计数比×目标下限）——
  工程判定权威归域/evidence（spec §2.4 口径维持）。
- 框色计数口径＝**位置口径**（契约 acceptance 3 原文；返工重实现）：
  分母＝该区域 kind==Position 样本数、分子＝其中 Reached——与域
  computeCoverage 位置轴同定义；位姿样本不入框色对照（着色点层仍全
  kind 上屏）。守护用例 RegionTint_FollowsPositionAxisNotMixed_UI_T65
  断言双口径发散场景下框色跟随位置口径。
- 本批新用例（映射三用例）位于 if(TARGET sdurws) 集成门控测试文件
  （被测 TU 同源编入面）——冒烟树不编译，冒烟数字（239/176）与
  ui-t64 侧相同为构成性结果；新逻辑回归全部依赖集成 ui_test（254 全绿）。
- UiPlugin 合并段仍不入任何测试目标（编出面＝ui_plugin/studio 产品
  目标）：返工将计数/分档决策抽入 ui_test 可编译纯函数获得守护；对账门
  与坐标逆变换保持编排透传（变异翻红不可施行面如实登记——acc/ui-t65/1
  §4.5③ 同口径）。
- View3DTint::Failed 为预留档（映射无生产者——E1 注修正，值保留供后续
  强信号呈现裁定）；mapSampleStateToCell 尾部防御 return 为编译器穷尽性
  惯例面（E4——保留，删除反引 C4715 警告）。
- 校验项点击定位＝L-R1 jumpTarget 区域级现成机制（校验行点击→区域选中→
  区域预览投影含着色点层）；样本级定位随诊断素材粒度扩展留后续。
