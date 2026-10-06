# UI-T67 构建与测试留痕（宿主验收取证通道——F-496 modeling-tour＋F-529 屏证）

分支 `ui-t67`（基点 redesign-main@6cdaef66——含 T64/65/66 合入）；
任务契约 tasks/foundation/UI-T67.json；DTB §2.11 WP-10-T67 行；
units/ui.md v1.77 §13 UI-T67 行。

## 交付物

- **modeling-tour 新冒烟面**（UiPlugin maybeRunModelingTour，env
  IRD_UI_PLUGIN_SMOKE=modeling-tour）：拍 1 新建项目＋打开＋召唤建模面板；
  拍 2 new-from-template generic-6r 重种子（模态选单自动应答）；拍 3 关节
  Origin 编辑（结构树选 j1→origin-z/r 行设值→apply→inline 确认→状态行
  "已应用：joints[0]"断言）；拍 4 结构新增关节（6→7）；拍 5 draft.apply
  建模域遍历（F-524 已知部署缺口诚实断言）；拍 6 export-package 包导出
  （重复定时器两轮模态应答——非原生文件对话框填路径＋导出确认点"是"→
  irdbundle 存在断言）。20 断言 MTOUR_PASS＋6 帧截图。
- **F-529 屏证**：requirements-tour 7.5 拍扩建——着色数据面断言（网格交
  付＝上屏链通；空交付＝无评估运行诚实缺席）＋校验定位链 exercised
  （rows=10）＋3D 截图落证。
- **F-524 发现**（major，tour 取证捕获）：宿主装配 policyProvider=nullptr
  （C-10 诚实基线）→建模 draft.apply 恒被 ④策略端口未装配 拒绝——应用草
  稿主链在真实宿主不可用（F-496 长期缺证据的根因面）。
- **对话缝**：qt 宿主 open/saveFilePath 冒烟环境（SMOKE env 非空）非原生
  形态（DontUseNativeDialog——自动应答需 findChild 可达行编辑；生产交互
  不受影响）。
- 驱动脚本：modeling-tour.ps1＋requirements-tour.ps1（留痕全集口径）。

## 验证记录

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 真机 modeling-tour | **MTOUR_PASS**（exit=0；20 断言全过——含 F-524 gap 诚实断言） | smoke-tour-modeling/console-mtour.log＋6 PNG＋console-mtour-asserts.log |
| 真机 requirements-tour（7.5 扩建） | 7.5 拍 3 断言 pass（定位链 rows=10；网格诚实缺席语义）；step2 station-added 失败＝先于本批（F-536 登记） | smoke-tour/console-tour.log＋PNG |
| 集成构建（受影响目标含 sdurws_ird_studio 静态链接重建） | 零错误 | 会话执行记录 |
| sdurws_ird_modeling_test／modeling_gui_test（集成） | 339/339＋41/41（含 T64/65/66 合入增量） | modeling-test.log／.xml＋modeling-gui.log／.xml |
| ui_test／ui_contract／ui_gui／modeling_contract／requirements_gui | 254／32／78／17／44 全绿 | 会话执行记录（未触碰套件以验收复现为准） |
| ird_gates | 103 命中＝T64 恰增登记 2 条后存量；归一化多重集与 ui-t63 基线 diff=+2（T64 恰增）；命中集零项涉本批改动文件 | ird-gates-branch.log／ird-gates-branch-hits-normalized.txt＋gate-all.log（留痕全集） |
| validate-docs／validate-task（UI-T67.json） | PASS（20 units/245 task files）／PASS（显式核验输出行） | 会话执行记录 |

## 诚实边界

- **F-524（major）**：宿主装配 policyProvider=nullptr（C-10 诚实基线）→
  建模 draft.apply 恒被 ④策略端口未装配 拒绝——应用草稿主链在真实宿主
  不可用。tour 诚实断言该缺口（拒绝原因精确匹配）；修复（宿主装配接线策
  略装载面）归宿主装配批次。F-496 的"应用→修订"取证链以该缺口的精确断
  言＋出口面呈现为现阶段证据。
- **F-529 残余**：三态全谱（Good/Failed 混合）视觉屏证需评估执行联动
  （UI-T64 通道产出）后呈现——本批交付定位链自动化＋数据面断言＋截图；
  全谱复核归宿主手动/评估联动通道。
- **step2 station-added 失败（F-536）**：requirements-tour 工位新增树行
  计数失败（tree-child=-1）先于本批存在（本批未触需求域代码）——登记留
  查，不阻塞本批（非本批引入、非本批范围）。
