# WP-15-T20 第一、二修复批的所有者授权合入记录

所有者在已获知“尚未全部修复、尚无完整终验与正式三维通过结论”后，明确指令“提交主分支”。本次据此仅集成当前两个完整修复批，不将WP-15-T20标done，不补写独立验收pass，不把局部修复变成全功能交付结论。

- 合入目标：开发主线redesign-main；合入前e5b342895cdd7c6eaffb5f63e2e0b2e4abddeaf6。
- 合入对象：8bd146f6558579ad2d640124004a1ade6b49b9ca与96fae76f71e7bc926da0ee37e9229d984e6fcf5d，后者包含前者。
- 已实现范围：F01合法区域盒误拒绝、F02已填参数切单位变义、F03四页长布局；F04配置策略/数值整体确认、取消及持久化失败保持的私有面。
- 未完成范围：F04正式宿主恢复及F05～F14对应缺口/实证；70项最终验收、正式三维、归档及性能不改为PASS。详情沿用REPORT-stage1.md、REPORT-stage2.md与MATRIX-RESULTS-stage2.md。

## 合并范围与冲突

保留主线e5b34289中的需求/建模复验材料、findings及已有历史；不合入另一工作树尚在修改的建模、UiPlugin或KinEvaluationChannel。唯一冲突为自动生成evidence-index.json，按双方所有证据重新运行index-evidence.ps1解决，未选择任一侧覆盖另一侧证据。旧stage1/stage2原证据不修改。

合并后industrialrobot整个产品目录与送合对象96fae76f的Git树均为dc00ac574d18d266b902c4d0683591f49240f63a；确认为零额外产品代码变化。冻结main仍为49a9e819c24c7ef15dff24366a4a551068c2d269。前置身份与两侧父提交详见merge-stage12/merge-stage12-identities.json。

## 合并结果验证

| 检查 | 本轮结果 | 证据（merge-stage12/） |
| --- | --- | --- |
| 集成任务四目标（含正式宿主） | PASS | build-kinematics-merge-stage12.log |
| 独立任务三目标 | PASS | standalone-build-kinematics-merge-stage12.log |
| 集成模型、契约、GUI | 287通过＋1跳过；57通过；35通过 | kinematics/kinematics_contract/kinematics_gui-merge-stage12.xml及对应ird报告、日志 |
| 独立模型、契约、GUI | 279通过＋1跳过；57通过；35通过 | standalone_kinematics/standalone_kinematics_contract/standalone_kinematics_gui-merge-stage12.xml及报告、日志 |
| ird_gates | PASS，产品扫描零命中 | gates-kinematics-merge-stage12.log |
| 文档与索引完整性 | 归档后实际validate-docs通过 | 收尾日志保留build/kinematics-audit-20261021/validate-docs-merge-stage12.log |
| 独立上下文验收 | NOT RUN；本会话未冒充验收者 | 本记录仅是所有者授权的阶段集成，不替代acceptance-protocol |
| 正式宿主真实三维、完整70项及P95 | NOT RUN | 不因构建/GUI回归通过改变上述未完成项 |

模型跳过项为既有GUI注册占位PluginPanelLinks.KinGuiPresentationRegisteredNotExecuted_ACC1。GUI在VS2022 x64、QT_QPA_PLATFORM=windows、绝对exe路径、同桌面单进程条件下串行运行，runner在启动前及运行中检查并发；未启动或关闭另一工作树进程。构建仅本任务依赖面，不宣称全仓所有目标构建或完整交互验收。

可重跑：从仓库根使用stage2/run-stage2.ps1，依次运行Mode build、standalone-build；Mode test分别传上述六个Name，最后Mode gates。文档用RobWork/scripts/industrialrobot/validate-docs.ps1；默认时间戳隔离新输出，避免覆盖已归档证据。

## 远程状态与后续

合并前fetch origin redesign-main实际失败（github.com:443无法连接），因此本记录只承诺保留已知本地主线，未声称已核对远端最新状态。最终是否推送成功必须看实际git远端核对与build/kinematics-audit-20261021/同步日志；不强推、不改已发布历史。继续修复仍使用任务分支，保留本工作树和wp15-t20，不删除未完成任务分支。
