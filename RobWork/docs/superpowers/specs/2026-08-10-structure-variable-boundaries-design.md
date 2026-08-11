# 结构优化设计变量数据边界设计

## 目标

完成设计变量后续优化的第一阶段：锁定变量的物理结构语义，补齐行级操作 API，并在每次编辑后立即沿用现有运行门禁。新增变量继续严格选自嵌入模型快照生成的完整建议项，不提供字段自由组装。

## 范围与阶段边界

本阶段包括：

- 表格中的结构字段只读；
- 当前值、范围、步长、偏好值、偏好权重和启用状态可编辑；
- 模型层支持单行删除、复制、完整基线重置；
- 复制自动生成唯一 ID；
- 编辑和行操作触发现有项目脏状态与 `hasRunnableInputs()` 刷新；
- 补齐模型和 Widget 回归测试。

本阶段不包括搜索/筛选代理、高级列展示和模型状态 Banner；它们分别进入后续的表格效率阶段和状态体验阶段。

## 数据边界

`id`、`label`、`targetName`、`kind` 和 `unit` 是由 `StructureOptimizationUiLogic::suggestVariables(context)` 生成的物理映射定义。表格将这些字段设为只读，防止用户把一个已知变量修改成 `StructureDesignMutator` 不能应用的未知组合。

用户可编辑的数值字段是 `currentValue`、`minimum`、`maximum`、`step`、`preferredValue`、`preferenceWeight` 和 `enabled`。编辑后必须满足：所有数值有限、`minimum <= currentValue <= maximum`、`step > 0`、`0 <= preferenceWeight <= 1`。不满足时，模型拒绝写入，保留旧值；Widget 通过状态栏给出简短提示。

`preferredValue` 和 `preferenceWeight` 在本阶段不新增表格列，而是确保数据层可以受校验地编辑。它们的可视化入口放入下一阶段的高级列或详情面板，避免再次扩宽现有主表。

## 模型 API

`StructureVariableTableModel` 提供以下操作：

- `appendVariable(variable)`：拒绝空 ID 和重复 ID，以单行插入通知追加；
- `removeVariable(row)`：删除一个有效行，使用 `beginRemoveRows/endRemoveRows`；
- `removeRows(indexes)`：保留已有的批量删除能力，处理无序、重复索引；
- `duplicateVariable(row)`：复制现有变量并以 `<source-id>_copy_<n>` 生成当前列表中唯一的 ID，同时在标签后添加 ` (Copy)`；
- `resetVariables(variables)`：专用于完整替换，使用 model reset；`setVariables()` 保持为加载项目的兼容入口并转发给它。

新增、复制和删除均保持行级 Qt 模型通知。恢复模型基线仍是完整替换，因为其确认语义是覆盖新增、删除和全部用户编辑。

## Widget 行为

变量页增加复制命令。添加继续只显示尚未存在的完整建议变量；复制、删除和恢复基线都受运行状态禁用。删除允许清空变量列表，随后立即重新运行 `hasRunnableInputs()` 并显示“至少需要一个启用的设计变量”。

所有模型的 `dataChanged`、`rowsInserted`、`rowsRemoved` 和 `modelReset` 已接入 `projectDocumentChanged`。本阶段复用该路径，Widget 不自行维护第二份脏状态。

## 测试

模型测试覆盖：只读结构字段、每个可编辑字段的合法写入、非法范围/步长/偏好权重拒绝、单行删除、复制 ID 唯一性、基线重置、批量删除和删空。

Widget 测试覆盖：复制按钮存在和初始禁用状态、选中后启用、行操作导致项目变脏、删空后开始按钮禁用。需要模态确认和真实优化运行状态的流程继续由 Windows Qt 手工验证。

## 后续阶段

第二阶段加入 `QSortFilterProxyModel`、搜索/类型筛选、高级字段呈现和代理到源模型的操作映射。第三阶段加入明确的 `Current`、`Stale`、`Untracked`、`SourceMissing`、`SourceInvalid` Banner 与上下文引导动作，且不提供 Stale 快照覆盖同步。
