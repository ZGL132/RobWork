#ifndef RWS_ENGINEERINGREQUIREMENTS_REQUIREMENTSETUNDOSTACK_HPP
#define RWS_ENGINEERINGREQUIREMENTS_REQUIREMENTSETUNDOSTACK_HPP

#include "EngineeringRequirementTypes.hpp"

#include <cstddef>
#include <vector>

namespace rws {

// 需求编辑操作通常会一次创建、替换或删除多个工位。这里保存操作前的完整需求集快照，
// 以保证撤销后模型绑定、冻结状态、工位和工作空间约束能够一起恢复，而不是只删除若干行。
class RequirementSetUndoStack {
  public:
    void pushSnapshot(const RequirementSet& requirements);
    bool canUndo() const;
    bool canRedo() const;
    bool undo(RequirementSet& requirements);
    bool redo(RequirementSet& requirements);
    void clear();

  private:
    static constexpr std::size_t maximumSnapshots = 32;
    std::vector<RequirementSet> _snapshots;
    std::vector<RequirementSet> _redoSnapshots;
};

} // namespace rws

#endif
