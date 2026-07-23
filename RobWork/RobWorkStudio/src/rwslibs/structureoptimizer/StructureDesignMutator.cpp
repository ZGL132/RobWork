#include "StructureDesignMutator.hpp"
#include <rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace rws {

namespace {

// ---------------------------------------------------------------------------
//  Helper: create an AnalysisWarning
// ---------------------------------------------------------------------------
AnalysisWarning makeWarning(
    const std::string& code,
    const std::string& message,
    AnalysisStatus severity = AnalysisStatus::Fail)
{
    AnalysisWarning w;
    w.code     = code;
    w.message  = message;
    w.source   = "StructureDesignMutator";
    w.severity = severity;
    return w;
}

// ---------------------------------------------------------------------------
//  Helper: clamp double to [lo, hi]
// ---------------------------------------------------------------------------
double clampVal(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// ---------------------------------------------------------------------------
//  Helper: check if a variable kind modifies SE(3) transform data
// ---------------------------------------------------------------------------
bool isTransformVariable(StructureVariableKind kind)
{
    switch (kind) {
        case StructureVariableKind::JointPositionX:
        case StructureVariableKind::JointPositionY:
        case StructureVariableKind::JointPositionZ:
        case StructureVariableKind::JointRotationRoll:
        case StructureVariableKind::JointRotationPitch:
        case StructureVariableKind::JointRotationYaw:
        case StructureVariableKind::BaseHeight:
        case StructureVariableKind::TcpOffsetX:
        case StructureVariableKind::TcpOffsetY:
        case StructureVariableKind::TcpOffsetZ:
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
//  Helper: check if a variable kind modifies DH parameters
// ---------------------------------------------------------------------------
bool isDhVariable(StructureVariableKind kind)
{
    return kind == StructureVariableKind::DhA ||
           kind == StructureVariableKind::DhD;
}

} // anonymous namespace

// ===========================================================================
//  apply()
// ===========================================================================
StructureMutationResult StructureDesignMutator::apply(
    const RobotModelSpec& baseline,
    const std::vector<StructureDesignVariable>& variables,
    const std::vector<double>& values)
{
    StructureMutationResult result;
    result.spec = baseline;  // deep copy (plain data struct)
    auto& spec = result.spec;
    std::vector<AnalysisWarning>& warnings = result.warnings;
    bool validationFailed = false;

    // ── 1. Validate value count ──────────────────────────────────────────
    if (values.size() != variables.size()) {
        warnings.push_back(makeWarning(
            "StructureOptimization.Variable.CountMismatch",
            "Values count (" + std::to_string(values.size()) +
            ") does not match variables count (" +
            std::to_string(variables.size()) + ")."));
        validationFailed = true;
        result.warnings = warnings;
        return result;
    }

    // ── 2. Validate each enabled variable's value ────────────────────────
    for (std::size_t i = 0; i < variables.size(); ++i) {
        if (!variables[i].enabled)
            continue;

        const double v = values[i];

        if (!std::isfinite(v)) {
            warnings.push_back(makeWarning(
                "StructureOptimization.Variable.InvalidValue",
                "Variable '" + variables[i].id +
                "' has non-finite value: " + std::to_string(v) + "."));
            validationFailed = true;
            continue;
        }

        if (v < variables[i].minimum || v > variables[i].maximum) {
            warnings.push_back(makeWarning(
                "StructureOptimization.Variable.InvalidValue",
                "Variable '" + variables[i].id + "' value " +
                std::to_string(v) + " is outside bounds [" +
                std::to_string(variables[i].minimum) + ", " +
                std::to_string(variables[i].maximum) + "]."));
            validationFailed = true;
        }
    }

    // ── 3. Return early if any validation failed ─────────────────────────
    if (validationFailed) {
        result.warnings = warnings;
        return result;
    }

    // ── 4. Check for mixed DH / Transform variables ──────────────────────
    {
        bool hasDh       = false;
        bool hasTransform = false;
        for (const auto& v : variables) {
            if (!v.enabled) continue;
            if (isDhVariable(v.kind))
                hasDh = true;
            else
                hasTransform = true;
        }
        if (hasDh && hasTransform) {
            warnings.push_back(makeWarning(
                "StructureOptimization.Variable.MixedKinematicsSource",
                "Cannot mix DH variables (DhA/DhD) with Transform variables "
                "(JointPositionX/Y/Z, JointRotationRoll/Pitch/Yaw, etc.)."));
            result.warnings = warnings;
            return result;
        }
    }

    // ── 5. Apply each enabled variable ───────────────────────────────────
    bool usedTransformVars = false;
    bool usedDhVars        = false;

    for (std::size_t i = 0; i < variables.size(); ++i) {
        const auto& var   = variables[i];
        const double val  = values[i];

        if (!var.enabled)
            continue;

        // Track categories
        if (isTransformVariable(var.kind))
            usedTransformVars = true;
        if (isDhVariable(var.kind))
            usedDhVars = true;

        switch (var.kind) {

        // ---- JointPosition ----
        case StructureVariableKind::JointPositionX: {
            auto it = std::find_if(spec.transformJoints.begin(),
                spec.transformJoints.end(),
                [&](const JointTransformSpec& j) { return j.name == var.targetName; });
            if (it != spec.transformJoints.end()) {
                it->pos[0] = val;
            } else {
                warnings.push_back(makeWarning(
                    "StructureOptimization.Variable.MissingTarget",
                    "JointPositionX: transform joint '" + var.targetName + "' not found."));
            }
            break;
        }
        case StructureVariableKind::JointPositionY: {
            auto it = std::find_if(spec.transformJoints.begin(),
                spec.transformJoints.end(),
                [&](const JointTransformSpec& j) { return j.name == var.targetName; });
            if (it != spec.transformJoints.end()) {
                it->pos[1] = val;
            } else {
                warnings.push_back(makeWarning(
                    "StructureOptimization.Variable.MissingTarget",
                    "JointPositionY: transform joint '" + var.targetName + "' not found."));
            }
            break;
        }
        case StructureVariableKind::JointPositionZ: {
            auto it = std::find_if(spec.transformJoints.begin(),
                spec.transformJoints.end(),
                [&](const JointTransformSpec& j) { return j.name == var.targetName; });
            if (it != spec.transformJoints.end()) {
                it->pos[2] = val;
            } else {
                warnings.push_back(makeWarning(
                    "StructureOptimization.Variable.MissingTarget",
                    "JointPositionZ: transform joint '" + var.targetName + "' not found."));
            }
            break;
        }

        // ---- JointRotation ----
        case StructureVariableKind::JointRotationRoll: {
            auto it = std::find_if(spec.transformJoints.begin(),
                spec.transformJoints.end(),
                [&](const JointTransformSpec& j) { return j.name == var.targetName; });
            if (it != spec.transformJoints.end()) {
                it->rpyDeg[0] = val;
            } else {
                warnings.push_back(makeWarning(
                    "StructureOptimization.Variable.MissingTarget",
                    "JointRotationRoll: transform joint '" + var.targetName + "' not found."));
            }
            break;
        }
        case StructureVariableKind::JointRotationPitch: {
            auto it = std::find_if(spec.transformJoints.begin(),
                spec.transformJoints.end(),
                [&](const JointTransformSpec& j) { return j.name == var.targetName; });
            if (it != spec.transformJoints.end()) {
                it->rpyDeg[1] = val;
            } else {
                warnings.push_back(makeWarning(
                    "StructureOptimization.Variable.MissingTarget",
                    "JointRotationPitch: transform joint '" + var.targetName + "' not found."));
            }
            break;
        }
        case StructureVariableKind::JointRotationYaw: {
            auto it = std::find_if(spec.transformJoints.begin(),
                spec.transformJoints.end(),
                [&](const JointTransformSpec& j) { return j.name == var.targetName; });
            if (it != spec.transformJoints.end()) {
                it->rpyDeg[2] = val;
            } else {
                warnings.push_back(makeWarning(
                    "StructureOptimization.Variable.MissingTarget",
                    "JointRotationYaw: transform joint '" + var.targetName + "' not found."));
            }
            break;
        }

        // ---- DH parameters ----
        case StructureVariableKind::DhA: {
            auto it = std::find_if(spec.dhJoints.begin(),
                spec.dhJoints.end(),
                [&](const DHJointSpec& j) { return j.name == var.targetName; });
            if (it != spec.dhJoints.end()) {
                it->a = val;
            } else {
                warnings.push_back(makeWarning(
                    "StructureOptimization.Variable.MissingTarget",
                    "DhA: DH joint '" + var.targetName + "' not found."));
            }
            break;
        }
        case StructureVariableKind::DhD: {
            auto it = std::find_if(spec.dhJoints.begin(),
                spec.dhJoints.end(),
                [&](const DHJointSpec& j) { return j.name == var.targetName; });
            if (it != spec.dhJoints.end()) {
                it->d = val;
            } else {
                warnings.push_back(makeWarning(
                    "StructureOptimization.Variable.MissingTarget",
                    "DhD: DH joint '" + var.targetName + "' not found."));
            }
            break;
        }

        // ---- BaseHeight ----
        case StructureVariableKind::BaseHeight: {
            spec.robotBaseFrame.pos[2] = val;
            usedTransformVars = true; // modifies a transform property
            break;
        }

        // ---- TcpOffset ----
        case StructureVariableKind::TcpOffsetX: {
            auto it = std::find_if(spec.transformJoints.begin(),
                spec.transformJoints.end(),
                [&](const JointTransformSpec& j) {
                    return j.name == var.targetName &&
                           j.type == "ToolFrame";
                });
            if (it == spec.transformJoints.end()) {
                // Fallback: search by name only
                it = std::find_if(spec.transformJoints.begin(),
                    spec.transformJoints.end(),
                    [&](const JointTransformSpec& j) { return j.name == var.targetName; });
            }
            if (it != spec.transformJoints.end()) {
                it->pos[0] = val;
            } else {
                warnings.push_back(makeWarning(
                    "StructureOptimization.Variable.MissingTarget",
                    "TcpOffsetX: ToolFrame joint '" + var.targetName + "' not found."));
            }
            break;
        }
        case StructureVariableKind::TcpOffsetY: {
            auto it = std::find_if(spec.transformJoints.begin(),
                spec.transformJoints.end(),
                [&](const JointTransformSpec& j) {
                    return j.name == var.targetName &&
                           j.type == "ToolFrame";
                });
            if (it == spec.transformJoints.end()) {
                it = std::find_if(spec.transformJoints.begin(),
                    spec.transformJoints.end(),
                    [&](const JointTransformSpec& j) { return j.name == var.targetName; });
            }
            if (it != spec.transformJoints.end()) {
                it->pos[1] = val;
            } else {
                warnings.push_back(makeWarning(
                    "StructureOptimization.Variable.MissingTarget",
                    "TcpOffsetY: ToolFrame joint '" + var.targetName + "' not found."));
            }
            break;
        }
        case StructureVariableKind::TcpOffsetZ: {
            auto it = std::find_if(spec.transformJoints.begin(),
                spec.transformJoints.end(),
                [&](const JointTransformSpec& j) {
                    return j.name == var.targetName &&
                           j.type == "ToolFrame";
                });
            if (it == spec.transformJoints.end()) {
                it = std::find_if(spec.transformJoints.begin(),
                    spec.transformJoints.end(),
                    [&](const JointTransformSpec& j) { return j.name == var.targetName; });
            }
            if (it != spec.transformJoints.end()) {
                it->pos[2] = val;
            } else {
                warnings.push_back(makeWarning(
                    "StructureOptimization.Variable.MissingTarget",
                    "TcpOffsetZ: ToolFrame joint '" + var.targetName + "' not found."));
            }
            break;
        }

        // ---- Link geometry ----
        case StructureVariableKind::LinkRadius: {
            bool found = false;
            // Search drawables
            {
                auto it = std::find_if(spec.drawables.begin(),
                    spec.drawables.end(),
                    [&](const DrawableSpec& d) { return d.name == var.targetName; });
                if (it != spec.drawables.end()) {
                    it->radius = val;
                    found = true;
                }
            }
            // Also search collision models
            {
                auto it = std::find_if(spec.collisionModels.begin(),
                    spec.collisionModels.end(),
                    [&](const CollisionModelSpec& c) { return c.name == var.targetName; });
                if (it != spec.collisionModels.end()) {
                    it->radius = val;
                    found = true;
                }
            }
            if (!found) {
                warnings.push_back(makeWarning(
                    "StructureOptimization.Variable.MissingTarget",
                    "LinkRadius: drawable/collision '" + var.targetName + "' not found."));
            }
            break;
        }
        case StructureVariableKind::LinkWidth: {
            bool found = false;
            {
                auto it = std::find_if(spec.drawables.begin(),
                    spec.drawables.end(),
                    [&](const DrawableSpec& d) { return d.name == var.targetName; });
                if (it != spec.drawables.end()) {
                    it->dimensions[0] = val;
                    found = true;
                }
            }
            {
                auto it = std::find_if(spec.collisionModels.begin(),
                    spec.collisionModels.end(),
                    [&](const CollisionModelSpec& c) { return c.name == var.targetName; });
                if (it != spec.collisionModels.end()) {
                    it->dimensions[0] = val;
                    found = true;
                }
            }
            if (!found) {
                warnings.push_back(makeWarning(
                    "StructureOptimization.Variable.MissingTarget",
                    "LinkWidth: drawable/collision '" + var.targetName + "' not found."));
            }
            break;
        }
        case StructureVariableKind::LinkHeight: {
            bool found = false;
            {
                auto it = std::find_if(spec.drawables.begin(),
                    spec.drawables.end(),
                    [&](const DrawableSpec& d) { return d.name == var.targetName; });
                if (it != spec.drawables.end()) {
                    it->dimensions[2] = val;
                    found = true;
                }
            }
            {
                auto it = std::find_if(spec.collisionModels.begin(),
                    spec.collisionModels.end(),
                    [&](const CollisionModelSpec& c) { return c.name == var.targetName; });
                if (it != spec.collisionModels.end()) {
                    it->dimensions[2] = val;
                    found = true;
                }
            }
            if (!found) {
                warnings.push_back(makeWarning(
                    "StructureOptimization.Variable.MissingTarget",
                    "LinkHeight: drawable/collision '" + var.targetName + "' not found."));
            }
            break;
        }

        } // switch
    }

    // ── 6. Synchronize kinematics views ──────────────────────────────────
    try {
        if (usedTransformVars) {
            RobotModelXmlWriter::refreshDhProjectionFromTransform(spec);
        }
        if (usedDhVars) {
            RobotModelXmlWriter::applyDhInputToTransform(spec);
        }
    } catch (const std::exception& e) {
        warnings.push_back(makeWarning(
            "StructureOptimization.Variable.KinematicsSyncError",
            std::string("Kinematics synchronization failed: ") + e.what()));
        result.warnings = warnings;
        return result;
    }

    // ── 7. Recompute auto-link geometry ──────────────────────────────────
    try {
        RobotModelXmlWriter::applyLinkGeometry(spec);
    } catch (const std::exception& e) {
        warnings.push_back(makeWarning(
            "StructureOptimization.Variable.GeometrySyncError",
            std::string("Geometry synchronization failed: ") + e.what()));
        result.warnings = warnings;
        result.warnings = warnings;
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace rws
