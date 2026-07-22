#include "KinematicAnalysisUiLogic.hpp"

#include "TaskPointTableModel.hpp"

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
