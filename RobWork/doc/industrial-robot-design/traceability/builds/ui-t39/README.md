# UI-T39 留痕：需求面板审核返工（项目级撤销真实链路＋只读接线＋呈现收口）

分支 `ui-t39`（基点 redesign-main@cb374599）；审核依据＝R2 中间验收审核意见
（2026-10-02——UI-T37/T38 需求面板重构的第一~三阶段修复清单）。

## 审核意见修复对照

| 审核条目 | 处置 |
| --- | --- |
| P1-1 `edit.undo` 错误命令 ID | 修正为宿主注册的 `project.undo`（RequirementsPanelWidget::onProjectUndo） |
| P1-1 撤销按钮按可用性门控 | 新增 refreshUndoButtons 单出口：项目级键随 project.undo 命令可用性快照（无项目/只读/无可撤销修订＝禁用＋原因提示），草稿级随记账器＋可写性 |
| P1-1 撤销真实处理器缺位 | WorkbenchContent deps 增 undo/redo 处理器覆写缝＋undoRedoDisablement 注册期谓词；UiPlugin orchestrateProjectUndo/Redo 接 UndoRedoService（逆命令提交产生新修订） |
| P1-2 只读状态不接线 | RequirementsUiModule/Assembly::setWritable 转发链＋宿主 openViaSessionController 按打开报告驱动；setWritable 全量刷新（生命周期/命令/撤销三出口＋校验页结论保持） |
| P1-3 撤销/重做后校验失真 | onDraftUndo/onDraftRedo 与普通编辑共用 postEditAction 链（不再以空报告直刷校验页） |
| P1-3 结构编辑不入撤销记账 | submitStructuralEdit 补 recordAppliedEdit（新增/复制/删除后撤销键即时可用——字段编辑轨既有先例同语义） |
| 会话关闭按钮残留 | 面板 resetForSessionDetached（锚/记账/投影收拢＋全禁）；Assembly::onSessionDetached 同场解除编辑目标引用 |
| 双树职责不明 | 页头追加『（草稿）』标注（panel.requirements.draft-marker）；共享检查器本域页标题携带『（需求草稿）』；复杂编辑页入口标题注明『在需求面板打开』 |
| 命令依赖全局最后选中 | selectionAnchor(WorkingSetMember) 分页锚；镜像/阵列/重生成按工位页锚取源 |
| 需求 Dock 默认尺寸 | 面板 setMinimumSize(380, 560)（宽度受中央区 320 px 保留红线物理约束，见下）＋宿主首呼初始尺寸 520×680 |
| 底部任务 Dock 占空间 | UI-T38 已默认隐藏（EmbeddedDock 工厂缺省），本轮验证保持 |
| 命令条拥挤 | 分组收敛：高频主排（导入 ▾／导出副本＋两级撤销三键）＋『更多操作 ▾』下拉（捕获两条＋派生四条） |
| 表格列宽失序 | 名称列 Stretch＋摘要列 Interactive 首刷成形＋行 tooltip 承载完整文本（区域/工况两表） |
| 卡片折叠跨刷新 | 验证保持（卡片构造期一次＋render 只填行）＋gui 回归钉 CollapseSurvivesFieldRefresh_UI_T39 |
| 禁用按钮无原因 | 生命周期三态 tooltip（无会话/只读/未选择）＋命令键禁用原因回填＋撤销键动态提示 |
| 三维联动（第四阶段） | 按审核意见保持诚实占位，本批不动 |

## 计划外修复（真实撤销链路打通中暴露的存量缺陷）

| 缺陷 | 修复 |
| --- | --- |
| 生产装配从未注册域命令处理器（draft.apply 恒 unknown-command——"应用修改"从未真实产生修订） | `ProjectStore::handlerRegistry()` 公共装配通道（§5.3.5 从实现私有提升为公共契约）＋宿主打开回调内 registerRequirementCommandHandlers——**F-461**（requirements 半区 fixed；modeling/harness 残余面 open） |
| DomainModuleEntry 闭包捕获 `&bundle`（栈上 unique_ptr 地址）＝use-after-free（首次真实 committed 即崩 0xC0000005） | 改捕获堆对象裸指针 bundleRaw（bindAnchor/onCommitted 四处——UI-T35 引入的存量悬挂） |
| CommandRecord.payloadCanonical 持久化按 UTF-8 字符串直写（域负载是不透明任意字节——二进制载荷被 UTF-8 校验拒绝） | 改 hex 编码持久化（appendHexString/decodeHexString 对称——磁盘兼容负担为零：生产路径此前无域 payload 落盘） |
| 需求域二连 draft.apply 必拒（首应用回执只回填根身份，工作集集合挂载态不随 HEAD 演进） | 本批不修——**F-460**（UI-T40 序列候选：重导线＋重演接线）；冒烟 step9 以诚实双态断言承载 |

## 验证证据（2026-10-02）

1. **双模式构建**：集成树全量零错误（`cmake --build build --config Release` exit 0）；独立冒烟树（vcpkg toolchain＋Qt 6.11.1 前缀）配置＋构建零错误。
2. **测试**（主树 Release）：requirements gui 38/38（含 UI-T39 新增 6 例）、requirements 200+1skip（既有环境跳过）、ui 239/239、ui gui 67/67、project 254+1skip。
3. **ird_gates**：命中集 39 行唯一命中与基线（redesign-main@cb374599 stash 实测）逐条一致＝零新增（GATES_EXIT=1 为登记例外既有口径——F-457）。
4. **冒烟四通道**（`run-smoke-channels.ps1`）：layout=0 layout2=0 auto=0 requirements-tour=0（UI_T39_SMOKE_ALL_PASS）；step9 重写为真实撤销回路验证（首应用不可逆诚实禁用＋原因提示＋apply 后面板刷新链）。

## 已知边界

- 二连应用缺口（F-460）与 modeling/harness 处理器注册残余（F-461 open 半区）随 findings 登记。
- gate-all 第 2 步（ird_gates 退出码判据）与冒烟树配置两项 FAIL 为 F-457 登记的既有口径，与本批无关。
