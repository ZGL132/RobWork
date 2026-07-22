#include "KinematicAnalysisUiLogic.hpp"

#include "TaskPointTableModel.hpp"

#include <rw/models/Device.hpp>
#include <rw/kinematics/Frame.hpp>

bool rws::ikCollisionCheckRequested (bool checkboxAvailable, bool checkboxChecked)
{
    return !checkboxAvailable || checkboxChecked;
}

std::vector< int > rws::taskPointCompactTableColumns ()
{
    return {
        ColEnabled,
        ColId,
        ColName,
        ColType,
        ColX,
        ColY,
        ColZ,
        ColStatus,
        ColUsableSolutions,
        ColCollision
    };
}

std::vector< int > rws::taskPointDetailColumns ()
{
    return {
        ColRefFrame,
        ColTcpFrame,
        ColRoll,
        ColPitch,
        ColYaw,
        ColPosTol,
        ColOriTol,
        ColFreeRoll,
        ColWeight,
        ColNote,
        ColReason,
        ColRawCandidates,
        ColBestQ,
        ColPositionError,
        ColOrientationError,
        ColMinMargin,
        ColCondition
    };
}

std::string rws::defaultTcpFrameName (const rw::models::Device* device)
{
    if (device == nullptr || device->getEnd () == nullptr)
        return std::string ();
    return device->getEnd ()->getName ();
}
