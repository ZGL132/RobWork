#include "RequirementSetUndoStack.hpp"

namespace rws {

void RequirementSetUndoStack::pushSnapshot(const RequirementSet& requirements)
{
    // 批量操作前记录快照；达到上限时淘汰最早记录，避免长时间编辑造成无界内存增长。
    if (_snapshots.size() == maximumSnapshots)
        _snapshots.erase(_snapshots.begin());
    _snapshots.push_back(requirements);
    // 新编辑分支一旦发生，旧的重做历史不再代表当前需求演化路径，必须立即丢弃。
    _redoSnapshots.clear();
}

bool RequirementSetUndoStack::canUndo() const
{
    return !_snapshots.empty();
}
bool RequirementSetUndoStack::canRedo() const { return !_redoSnapshots.empty(); }

bool RequirementSetUndoStack::undo(RequirementSet& requirements)
{
    if (_snapshots.empty())
        return false;
    // 最后压入的记录恰好是最近一次操作发生前的完整状态。
    // 撤销前的当前态进入重做栈，确保“撤销 -> 重做”不会丢失任何需求、模型绑定或冻结状态字段。
    _redoSnapshots.push_back(requirements);
    requirements = _snapshots.back();
    _snapshots.pop_back();
    return true;
}

bool RequirementSetUndoStack::redo(RequirementSet& requirements)
{
    if (_redoSnapshots.empty()) return false;
    _snapshots.push_back(requirements);
    requirements = _redoSnapshots.back();
    _redoSnapshots.pop_back();
    return true;
}

void RequirementSetUndoStack::clear()
{
    _snapshots.clear();
    _redoSnapshots.clear();
}

} // namespace rws
