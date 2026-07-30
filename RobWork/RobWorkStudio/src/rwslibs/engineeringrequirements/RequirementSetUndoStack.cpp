#include "RequirementSetUndoStack.hpp"

namespace rws {

void RequirementSetUndoStack::pushSnapshot(const RequirementSet& requirements)
{
    // 批量操作前记录快照；达到上限时淘汰最早记录，避免长时间编辑造成无界内存增长。
    if (_snapshots.size() == maximumSnapshots)
        _snapshots.erase(_snapshots.begin());
    _snapshots.push_back(requirements);
}

bool RequirementSetUndoStack::canUndo() const
{
    return !_snapshots.empty();
}

bool RequirementSetUndoStack::undo(RequirementSet& requirements)
{
    if (_snapshots.empty())
        return false;
    // 最后压入的记录恰好是最近一次操作发生前的完整状态。
    requirements = _snapshots.back();
    _snapshots.pop_back();
    return true;
}

void RequirementSetUndoStack::clear()
{
    _snapshots.clear();
}

} // namespace rws
