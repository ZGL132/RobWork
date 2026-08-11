# 结构优化设计变量表格可读性 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 改善结构优化设计变量表格的数值格式、对齐、列宽和只读状态，同时保持原始编辑值和优化数据不变。

**Architecture:** 由 `StructureVariableTableModel` 负责基于 Qt role 的显示格式、对齐和可编辑性；由 `StructureOptimizerWidget` 负责 QTableView 的列宽与选择行为。`EditRole` 保持原始值，`DisplayRole` 仅服务显示，避免影响 `collectProblem()`、校验和优化计算。

**Tech Stack:** C++17、Qt Widgets、QAbstractTableModel、QTableView/QHeaderView、现有 `StructureOptimizationTest.cpp` 测试可执行文件。

---

### Task 1: 为变量模型补充显示语义测试

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureVariableTableModel.hpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureVariableTableModel.cpp`

- [ ] **Step 1: 写入失败测试**

在测试文件中加入 `testVariableTableDisplayRoles()`，构造单位为 `m` 和 `deg` 的变量，并断言：

```cpp
REQUIRE(model.data(model.index(0, StructureVariableTableModel::CurrentColumn),
                   Qt::DisplayRole).toString() == "0.400 m");
REQUIRE(model.data(model.index(1, StructureVariableTableModel::CurrentColumn),
                   Qt::DisplayRole).toString() == "12.35 deg");
REQUIRE(model.data(model.index(0, StructureVariableTableModel::CurrentColumn),
                   Qt::EditRole).toDouble() == 0.4);
REQUIRE(model.data(model.index(0, StructureVariableTableModel::CurrentColumn),
                   Qt::TextAlignmentRole).toInt() ==
        static_cast<int>(Qt::AlignRight | Qt::AlignVCenter));
REQUIRE(!(model.flags(model.index(0, StructureVariableTableModel::KindColumn)) &
          Qt::ItemIsEditable));
```

- [ ] **Step 2: 运行测试并确认失败**

运行：

```powershell
.\scripts\build-msvc-debug.cmd sdurws_structureoptimizer_test
& .\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\bin\Debug\sdurws_structureoptimizer_test.exe variable_table_display
```

预期：新增测试因当前 DisplayRole 不带单位、没有 TextAlignmentRole 或 Type 仍可编辑而失败。

- [ ] **Step 3: 实现最小模型改动**

在 `StructureVariableTableModel.cpp`：

```cpp
if (role == Qt::TextAlignmentRole) {
    if (index.column() == EnabledColumn)
        return static_cast<int>(Qt::AlignCenter);
    if (index.column() >= CurrentColumn && index.column() <= StepColumn)
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
}
```

对四个数值列：`DisplayRole` 返回按 `unit` 格式化的文本，`EditRole` 返回原始 `double`。在 `flags()` 中仅为非 `KindColumn` 的可编辑列添加 `Qt::ItemIsEditable`。

- [ ] **Step 4: 运行测试并确认通过**

重复 Step 2 命令。

预期：`variable_table_display` 通过，且无 Qt 警告。

### Task 2: 配置变量表格视图布局

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] **Step 1: 写入失败的 Widget 行为测试**

增加 `testVariableTablePresentation()`，构造 Widget 后查找 `structureVariableTable`，断言：

```cpp
REQUIRE(view->selectionBehavior() == QAbstractItemView::SelectRows);
REQUIRE(view->alternatingRowColors());
REQUIRE(view->horizontalScrollBarPolicy() == Qt::ScrollBarAsNeeded);
REQUIRE(view->horizontalHeader()->sectionResizeMode(
    StructureVariableTableModel::EnabledColumn) == QHeaderView::Fixed);
REQUIRE(view->columnWidth(StructureVariableTableModel::EnabledColumn) == 56);
```

- [ ] **Step 2: 运行测试并确认失败**

运行：

```powershell
.\scripts\build-msvc-debug.cmd sdurws_structureoptimizer_test
& .\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\bin\Debug\sdurws_structureoptimizer_test.exe variable_table_presentation
```

预期：当前变量页未设置这些视图属性，新增测试失败。

- [ ] **Step 3: 实现最小 Widget 改动**

在 `createVariablePage()` 保存变量视图指针，并配置：

```cpp
QTableView* view = makeTableView(_variableModel, "structureVariableTable");
view->setAlternatingRowColors(true);
view->setSelectionBehavior(QAbstractItemView::SelectRows);
view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
QHeaderView* header = view->horizontalHeader();
header->setSectionResizeMode(StructureVariableTableModel::EnabledColumn,
                             QHeaderView::Fixed);
view->setColumnWidth(StructureVariableTableModel::EnabledColumn, 56);
```

为文本列设置 `Interactive`，为四个数值列设置 `Fixed` 和固定宽度。不要添加 Delegate、搜索栏或工具栏。

- [ ] **Step 4: 运行测试并确认通过**

重复 Step 2 命令。

预期：`variable_table_presentation` 通过。

### Task 3: 回归验证和人工验收准备

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] **Step 1: 运行完整结构优化测试**

运行：

```powershell
.\scripts\build-msvc-debug.cmd sdurws_structureoptimizer_test
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "^sdurws_structureoptimizer_test$" --output-on-failure
```

预期：测试通过。

- [ ] **Step 2: 检查变更范围**

运行：

```powershell
git diff --check -- RobWorkStudio/src/rwslibs/structureoptimizer/StructureVariableTableModel.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureVariableTableModel.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp
```

预期：无空白错误；仅包含本阶段模型、Widget 和测试改动。

- [ ] **Step 3: 提供手工验证清单并暂停**

向用户提供以下检查项：

1. 打开包含长度和角度变量的结构优化项目。
2. 验证长度显示为 3 位小数、角度显示为 2 位小数，且单位正确。
3. 验证 Current、Min、Max、Step 右对齐，Enabled 复选框居中。
4. 验证拖动文本列宽度后数值列保持稳定。
5. 将窗口缩窄，确认表格可以横向滚动且文本未重叠。
6. 编辑数值，保存并重开项目，确认值未因显示精度丢失。
