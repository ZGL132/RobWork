#ifndef RWS_WORKFLOWPROJECTSTATE_HPP
#define RWS_WORKFLOWPROJECTSTATE_HPP

#include "WorkflowStageController.hpp"

#include <QJsonObject>

namespace rws {

/** Serializes the managed workflow evidence kept in a project manifest. */
class WorkflowProjectState
{
  public:
    static constexpr int SchemaVersion = 1;

    static WorkflowProjectSnapshot read (const QJsonObject& plugins);
    static void write (QJsonObject& plugins, const WorkflowProjectSnapshot& snapshot);
};

}    // namespace rws

#endif    // RWS_WORKFLOWPROJECTSTATE_HPP
