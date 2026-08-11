# 结构优化设计变量表格可读性设计

## 目标

在不改变结构优化数据模型、候选生成和优化算法的前提下，改善 Design Variables 表格的列宽、对齐、数值显示和只读状态，使工程师能够快速比较当前值、搜索范围和步长。

## 范围

本阶段只修改设计变量表格的显示和交互表现：

- 数值按变量单位格式化显示；
- 数字列右对齐，Enabled 复选框居中；
- Type 列明确为只读；
- 为表格设置稳定的列宽策略、交替行颜色和整行选择；
- 保留现有编辑数据通路，EditRole 继续使用原始数值；
- 增加模型层测试，验证显示文本、对齐角色和 Type 只读行为。

本阶段不包含变量增删、搜索筛选、模型状态 Banner、PreferredValue/PreferenceWeight 新列和项目数据结构调整。

## 现有边界

`StructureVariableTableModel` 保存 `std::vector<StructureDesignVariable>`。表格显示通过 `data()` 提供，编辑通过 `setData()` 写回，Widget 的 `collectProblem()` 在运行前读取模型中的完整变量列表。显示格式不能修改底层 `currentValue/minimum/maximum/step`，否则会影响优化计算和项目序列化。

## 设计方案

### 1. 表格数据角色

`StructureVariableTableModel::data()` 按 Qt 角色分工：

- `Qt::DisplayRole`：返回格式化后的字符串。长度单位保留 3 位小数，角度单位保留 2 位小数；无单位或未知单位使用稳定的通用格式。
- `Qt::EditRole`：继续返回原始 `double` 或原始字符串，保证现有编辑和 `setData()` 逻辑不变。
- `Qt::TextAlignmentRole`：Current、Min、Max、Step 右对齐；Enabled 居中；其他文本列左对齐。
- `Qt::CheckStateRole`：Enabled 继续返回 Checked/Unchecked。

单位判断优先使用 `StructureDesignVariable::unit`。当前自动生成的长度变量使用 `m`，角度变量应支持 `deg`；不要在 Delegate 中硬编码某个变量名称。

### 2. 只读字段

`KindColumn` 的显示值来自枚举，变异器只支持既定 `StructureVariableKind`，因此 Type 列必须移除 `Qt::ItemIsEditable`。其他现有可编辑列保持原行为，避免扩大本阶段范围。

### 3. Widget 列宽策略

在 `createVariablePage()` 中取得变量表格并设置：

- Enabled：固定窄列并居中；
- ID、Name、Target、Type：Interactive，允许用户调整；
- Current、Min、Max、Step：稳定的固定宽度，避免数值列随窗口变化跳动；
- 开启水平滚动，窗口变窄时不压缩长文本；
- 开启交替行颜色和整行选择；
- 不使用依赖特定像素宽度的样式表作为主要布局机制。

列宽只影响视图，不改变模型列定义和 JSON 格式。

## 数据流程

```text
StructureDesignVariable.unit/currentValue
    -> StructureVariableTableModel::data(DisplayRole)
    -> 格式化字符串
    -> QTableView 显示

用户编辑
    -> EditRole 原始值
    -> StructureVariableTableModel::setData()
    -> dataChanged
    -> StructureOptimizerWidget::collectProblem()
    -> 现有校验和优化流程
```

## 错误和兼容处理

- 非有限数值不在显示层修正，由现有问题校验报告错误。
- 未知单位不追加猜测单位，使用通用数值格式。
- 旧项目没有新增字段也不受影响，因为本阶段不扩展序列化结构。
- 任何格式化失败都应回退到可读的原始数值文本，不能阻止表格加载。

## 测试策略

在现有 `StructureOptimizationTest.cpp` 中增加模型层测试：

1. 长度变量 DisplayRole 使用 3 位小数并保留单位语义。
2. 角度变量 DisplayRole 使用 2 位小数。
3. 数值列返回右对齐，Enabled 返回居中对齐。
4. Type 列没有 `Qt::ItemIsEditable`。
5. EditRole/setData 后底层数值保持原始精度。

Widget 视觉效果由手工验证确认：列宽、数字右对齐、复选框居中、长文本可读性和窄窗口下的横向滚动。

## 不在本阶段解决的问题

- 设计变量增加、删除、复制和恢复基线；
- 搜索过滤及代理模型索引映射；
- 模型 Stale/Incomplete Banner；
- PreferredValue、PreferenceWeight 的高级展示；
- 缓存候选索引覆盖问题和其他核心算法问题。
