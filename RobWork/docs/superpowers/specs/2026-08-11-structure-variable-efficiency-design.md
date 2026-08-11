# 结构优化设计变量表效率设计

## 目标

为设计变量表加入可组合的关键词与类型筛选、默认折叠的偏好高级列，以及在代理模型下仍准确作用于源模型的批量操作。补充“添加缺失建议变量”，使其与完整覆盖语义的“恢复模型基线”明确区分。

## 模型与筛选

新增 `StructureVariableFilterProxyModel`，继承 `QSortFilterProxyModel`，源模型固定为 `StructureVariableTableModel`。关键词匹配 ID、名称、目标和类型显示文本，忽略大小写；类型筛选为“全部”或某一个 `StructureVariableKind`。只有同时满足关键词与类型条件的源行显示。

变量表始终绑定代理模型。复制和删除先将选中的代理索引映射回源索引，去重后调用源模型 API；复制完成后用 `mapFromSource()` 选择新行。任何因筛选导致的空选中都会禁用复制和删除。

## 高级字段

源模型新增 `PreferredColumn` 和 `PreferenceWeightColumn`，分别映射 `preferredValue` 与 `preferenceWeight`。这两列使用现有单位格式化和右对齐；编辑时通过现有偏好值合法性校验。变量页默认隐藏它们，用可勾选的 `Show Advanced` 控件显示或隐藏，不改变项目持久化数据。

## 工具栏语义

变量页工具栏包含关键词搜索框、类型下拉框、Show Advanced、Add Missing Suggestions、Add Variable、Duplicate Selected、Remove Selected、Restore Model Baseline。

`Add Missing Suggestions` 从 `suggestVariables(context)` 取出当前列表没有的 ID 并逐行追加，保留已有变量和所有用户编辑；无缺失候选时禁用。`Restore Model Baseline` 保持完整替换，覆盖新增、删除、数值与启用状态，仍要求确认。两者在运行期间均禁用。

## 测试

模型测试验证高级列的显示、对齐、合法编辑与非法权重拒绝。代理测试验证关键词与类型组合、代理到源索引映射后的复制和删除，以及筛选空结果。Widget 测试验证控件存在、高级列默认隐藏并可显示、筛选后复制/删除作用于正确源行、添加缺失建议变量保留已有编辑并标记项目为脏。

## 非范围

不加入模型状态 Banner、来源查看或新建项目引导；这些属于下一阶段的模型状态体验。不会修改 `hasRunnableInputs()` 或放宽现有运行门禁。
