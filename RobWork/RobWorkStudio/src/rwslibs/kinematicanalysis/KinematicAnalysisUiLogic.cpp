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
        ColName,
        ColRefFrame,
        ColTcpFrame,
        ColStatus
    };
}

std::vector< int > rws::taskPointDetailColumns ()
{
    return {
        ColId,
        ColType,
        ColPosTol,
        ColOriTol,
        ColFreeRoll,
        ColWeight,
        ColNote,
        ColRawCandidates,
        ColPositionError,
        ColOrientationError,
        ColMinMargin,
        ColCondition,
        ColCollision
    };
}

std::string rws::defaultTcpFrameName (const rw::models::Device* device)
{
    if (device == nullptr || device->getEnd () == nullptr)
        return std::string ();
    return device->getEnd ()->getName ();
}

bool rws::visualEnvelopeModeAvailable (int sourceKind, int renderMode)
{
    return sourceKind == 1 &&
        renderMode == static_cast< int > (VisualRenderMode::Envelope);
}

bool rws::visualEnvelopeDirectionChangeSupersedesRequest (
    bool envelopeActive, bool requestActive)
{
    return envelopeActive && requestActive;
}

bool rws::visualEnvelopeStateChangeRequiresRefresh (
    bool envelopeActive, bool studioStateChanged)
{
    return envelopeActive && studioStateChanged;
}
