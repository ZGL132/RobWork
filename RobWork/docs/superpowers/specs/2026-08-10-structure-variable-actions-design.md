# 结构优化设计变量操作设计

## 目标

为 StructureOptimizer 的设计变量表格增加受控新增、批量删除和恢复基线操作，同时保持变量与模型快照的物理语义一致，不允许创建无法由 `StructureDesignMutator` 应用的任意变量。

## 范围

- 从项目内嵌 `RobotModelSpec` 的建议变量中受控新增变量；
- 支持表格多选后的批量删除；
- 使用项目模型快照重新生成完整建议变量列表，并覆盖所有当前编辑；
- 优化运行中禁用上述操作；
- 复用现有的运行前校验、项目脏状态和序列化通路。

不包含自由定义未知变量、DH 变量编辑器、搜索筛选、模型陈旧状态 Banner 和撤销/重做。

## 基线与候选池

`StructureOptimizationUiLogic::suggestVariables(_loadedProblem.context)` 是本阶段唯一的变量候选池和基线来源。它从内嵌模型快照生成 Transform 关节位置、TCP 偏移、基座高度和自动连杆几何变量，提供已验证的 ID、targetName、unit、范围、步长和 preferredValue。

新增对话框只显示候选池中尚未被当前变量列表使用的项。不能直接填写枚举值或 targetName，因变异器只支持已定义的模型映射。恢复基线直接将当前变量列表替换为完整候选池，覆盖用户已删项、已加项、范围编辑和启用状态。

## 组件设计

### StructureVariableTableModel

新增以下接口：

- `bool appendVariable(const StructureDesignVariable&)`：ID 未重复时以 `beginInsertRows/endInsertRows` 追加，重复时返回 `false`。
- `int removeRows(const QModelIndexList&)`：提取有效行、去重、降序删除，并为每一段连续行使用 Qt 删除通知；返回实际删除数。

模型不负责确认对话框、候选池生成或恢复基线决策。

### StructureOptimizerWidget

变量页增加三个命令按钮：

- `Add Variable`：弹出对话框，在候选池中选中一个未添加变量后追加。
- `Remove Selected`：读取选择模型的全部行，按变量名称汇总到确认文案；用户确认后批量删除。
- `Restore Model Baseline`：明确提示“将覆盖全部变量编辑”，确认后以 `suggestVariables(_loadedProblem.context)` 替换变量列表。

表格切换为 `ExtendedSelection`。没有选中行时删除按钮禁用；没有未添加候选时新增按钮禁用。运行状态变为 running 时，三个按钮和表格编辑同现有选项卡一起禁用。

每项成功操作后调用 `updateRunState()`。模型发出的 rowsInserted/rowsRemoved/modelReset 信号沿用现有 `projectDocumentChanged` 连接，使项目 Provider 根据 JSON 快照正确标记为脏。

## 数据流程

```text
内嵌 RobotModelSpec
  -> suggestVariables(context)
  -> 新增候选池 / 恢复基线
  -> StructureVariableTableModel
  -> rowsInserted / rowsRemoved / modelReset
  -> projectDocumentChanged
  -> collectProblem()
  -> 现有校验与优化运行
```

## 边界与错误处理

- 删除允许清空全部变量；现有 `hasRunnableInputs()` 负责禁用开始优化并报告原因。
- 新增重复 ID 不修改模型。
- 模型快照不完整时，恢复基线得到空候选池；运行前校验继续报告上下文问题。
- 取消任一确认框或选择框不得修改表格或项目脏状态。
- 运行中按钮不可用，不允许改变后台优化已经捕获的问题快照。

## 测试策略

模型层测试覆盖：

1. 追加唯一变量后行数、顺序和数据正确；重复 ID 被拒绝。
2. 多行删除支持无序、重复索引，且返回实际删除数量。
3. 删除全部变量后模型为空，运行校验失败。
4. 恢复基线使用 `suggestVariables()` 生成的完整变量列表，覆盖手工编辑的变量集合。

Widget 手工验证覆盖：受控新增候选不重复、扩展多选删除、两个确认框、恢复覆盖、运行期间禁用，以及保存/重开后变量列表一致。
