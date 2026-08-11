/********************************************************************************
 * Copyright 2026 The Robotics Group, The Maersk Mc-Kinney Moller Institute,
 * Faculty of Engineering, University of Southern Denmark
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ********************************************************************************/

#ifndef RWS_WORKFLOWDOCKLAYOUTCONTROLLER_HPP
#define RWS_WORKFLOWDOCKLAYOUTCONTROLLER_HPP

#include <QString>

#include "WorkflowStageController.hpp"

namespace rws {

class RobWorkStudio;

/** Controls the fixed workflow dock topology and its model-readiness gate. */
class WorkflowDockLayoutController
{
  public:
    explicit WorkflowDockLayoutController (RobWorkStudio* studio);

    void applyLayout ();
    bool hasPendingInitialWidth () const { return _initialWidthPending; }
    void finalizeInitialWidth ();
    void revalidateReadiness ();
    void notifyRobotModelLoaded (const QString& filename);
    QString activeDockName () const;

  private:
    void applyStageSnapshot (const WorkflowStageSnapshot& snapshot);
    void refreshTabEnablement ();

    RobWorkStudio* _studio;
    QString _standaloneModelFilename;
    WorkflowStageSnapshot _stageSnapshot;
    bool _initialWidthPending = false;
    int _initialWidth = 0;
};

}    // namespace rws

#endif    // RWS_WORKFLOWDOCKLAYOUTCONTROLLER_HPP
