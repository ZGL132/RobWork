# UI-T65 构建与测试留痕（需求三维采样着色闭环——F-495 消费卡）

分支 `ui-t65`（基点＝堆叠 ui-t64@2972b5d7——UI-T30 堆叠先例；任务契约
tasks/foundation/UI-T65.json；DTB §2.11 WP-10-T65 行；units/ui.md §13
UI-T65 行；findings F-495 两批链进展注）。

## 交付物

- 协议扩展（View3DPreviewContract.hpp）：View3DCellState 表尾追加
  NotSampled（灰——未采样/未运行诚实态）＋新枚举 View3DTint 四档＋
  View3DBoxOutline 表尾追加 optional<View3DTint> tint（UI-T33 冻结面
  追加纪律——缺省 None 语义零破坏）。
- 统一供色（UiTheme.hpp palette）：采样状态色族（四组 GL 三元组＋同源
  换算 hex——三维后端/图例单一供色点）；HostView3DPreviewBackend 硬编码
  消账（applyCellColor 词表消费＋NotSampled 灰分支＋RegionOutlineRender
  tint 分色——缺省蓝保持）。
- 通道值面扩展（KinematicsPanelChannels.hpp）：KinChannelSampleRecord
  表尾追加 position（基座系 m）/regionObjectId（区域锚）＋执行器投影
  补坐标/区域锚（results×samples 按 sampleIndex 双射对齐直投）。
- 映射函数（KinEvaluationChannel.hpp 两个 inline 纯值翻译）：
  mapSampleStateToCell（域五值→四值词表）＋view3DTintFromCoverage
  （计数比×目标下限三档——呈现对照非工程判定）。
- 投影合并（UiPlugin bindRegionPreviewSink）：执行器账面经
  snapshotId+epoch 对账门（跨快照不投影——PA 权威镜像）→regionObjectId
  逐区域过滤→坐标基座→世界系（worldToBase 逆）→View3DSampleGrid.
  samples/cellStates＋框 tint 三档。
- 需求侧：RegionPreviewGeometry/RegionPreviewView 表尾追加
  regionObjectId/minPositionCoverage（面板填充＋门面透传）＋区域页富
  文本图例行（四状态色块＋蓝框选中——palette hex 供色）。

## 验证记录

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 集成构建（全量 Release） | 零错误 | 会话执行记录 |
| 独立冒烟（standalone 树 build-smoke-ui-t65） | 构建零错误（RC=0）＋kinematics_test 176/176（1 跳过＝gui 呈现登记）＋ui_test 239/239 全绿 | gate-all.log＋冒烟树实测 |
| sdurws_ird_ui_test（集成） | 253/253（新增 2——MapSampleStateToCell/View3DTintFromCoverage 表驱动） | 会话执行记录 |
| sdurws_ird_ui_gui_test（集成） | 78/78（PreviewOutlet 用例扩四态/tint 透传断言——计数不变断言扩面） | 会话执行记录 |
| sdurws_ird_requirements_gui_test（集成） | 44/44（RegionPreviewSink 用例扩区域锚/目标透传断言） | 会话执行记录 |
| 其余全套件回归 | modeling_test 335/modeling_gui 40/modeling_contract 17/ui_contract 32/requirements_test 200/kinematics_test 182/kinematics_contract 57 全绿（基线不降） | 会话执行记录 |
| ird_gates | 引擎直跑自报 **103 命中＝ui-t64 侧 103 零新增**（消费面全走既有装配边与 UI-T64 测试面直链——契约预期口径实证） | ird-gates-branch.log |
| validate-docs／validate-task | PASS（20 units/12 trace/243 task files）／PASS | 会话执行记录 |

## 诚实边界

- 着色判定零参与：映射＝词表翻译、框色＝呈现对照（计数比×目标下限）——
  工程判定权威归域/evidence（spec §2.4 口径维持）。
- UiPlugin 合并段为编排薄面：对账门（ui_test 执行器账面）/映射（表驱动）/
  透传（ui_gui 替身＋requirements_gui）三决策已在测试面分别覆盖——上屏
  取证归宿主手动验证通道（UI-T33 先例）。
- 校验项点击定位＝L-R1 jumpTarget 区域级现成机制（校验行点击→区域选中→
  区域预览投影含着色点层）；样本级定位随诊断素材粒度扩展留后续。
