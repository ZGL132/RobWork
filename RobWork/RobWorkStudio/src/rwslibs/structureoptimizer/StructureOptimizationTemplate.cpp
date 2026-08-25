#include "StructureOptimizationTemplate.hpp"

#include "StructureOptimizationValidation.hpp"

#include <algorithm>

namespace rws {

namespace {

ObjectiveTerm objective(const char* metricId, double weight,
                        OptimizationDirection direction = OptimizationDirection::Maximize,
                        double good = 1.0, double bad = 0.0)
{
    ObjectiveTerm term;
    term.metricId = metricId;
    term.weight = weight;
    term.direction = direction;
    term.normalization.good = good;
    term.normalization.bad = bad;
    term.normalization.clamp = true;
    term.enabled = weight > 0.0;
    return term;
}

std::vector<ObjectiveTerm> objectivesFor(
    StructureOptimizationTemplateKind kind)
{
    switch (kind) {
    case StructureOptimizationTemplateKind::ReachabilityFirst:
        return {
            objective("kinematics.reachability.weighted", 0.60),
            objective("kinematics.manipulability.p10", 0.15, OptimizationDirection::Maximize, 0.05),
            objective("kinematics.joint_margin.p10", 0.10, OptimizationDirection::Maximize, 0.20),
            objective("collision.free_rate", 0.10),
            objective("geometry.compactness", 0.03),
            objective("structure.preference", 0.02)};
    case StructureOptimizationTemplateKind::CompactnessFirst:
        return {
            objective("kinematics.reachability.weighted", 0.20),
            objective("kinematics.manipulability.p10", 0.15, OptimizationDirection::Maximize, 0.05),
            objective("kinematics.joint_margin.p10", 0.10, OptimizationDirection::Maximize, 0.20),
            objective("collision.free_rate", 0.10),
            objective("geometry.compactness", 0.40),
            objective("structure.preference", 0.05)};
    case StructureOptimizationTemplateKind::WorkspaceFirst:
        return {
            objective("kinematics.workspace.coverage", 0.45),
            objective("kinematics.reachability.weighted", 0.25),
            objective("kinematics.manipulability.p10", 0.10, OptimizationDirection::Maximize, 0.05),
            objective("kinematics.joint_margin.p10", 0.08, OptimizationDirection::Maximize, 0.20),
            objective("collision.free_rate", 0.07),
            objective("geometry.compactness", 0.05)};
    case StructureOptimizationTemplateKind::Balanced:
    default:
        return {
            objective("kinematics.reachability.weighted", 0.35),
            objective("kinematics.manipulability.p10", 0.20, OptimizationDirection::Maximize, 0.05),
            objective("kinematics.joint_margin.p10", 0.15, OptimizationDirection::Maximize, 0.20),
            objective("collision.free_rate", 0.15),
            objective("geometry.compactness", 0.10),
            objective("structure.preference", 0.05)};
    }
}

const std::vector< DesignIntentTemplateInfo >& designIntentDefinitions()
{
    static const std::vector< DesignIntentTemplateInfo > definitions = {
        {DesignIntentTemplateKind::KinematicBasic, "kinematic-basic", "1", "Kinematic Basic",
         {SemanticKind::LinkLength, SemanticKind::JointOriginOffsetX,
          SemanticKind::JointOriginOffsetY, SemanticKind::JointOriginOffsetZ,
          SemanticKind::JointOffsetAlongAxis}},
        {DesignIntentTemplateKind::KinematicWithJointAxis, "kinematic-with-joint-axis", "1",
         "Kinematic With Joint Axis",
         {SemanticKind::JointAxisTiltU, SemanticKind::JointAxisTiltV}},
        {DesignIntentTemplateKind::KinematicWithBaseTcp, "kinematic-with-base-tcp", "1",
         "Kinematic With Base/TCP",
         {SemanticKind::BaseTx, SemanticKind::BaseTy, SemanticKind::BaseTz,
          SemanticKind::BaseRotationVectorX, SemanticKind::BaseRotationVectorY,
          SemanticKind::BaseRotationVectorZ, SemanticKind::TcpTx, SemanticKind::TcpTy,
          SemanticKind::TcpTz, SemanticKind::TcpRotationVectorX,
          SemanticKind::TcpRotationVectorY, SemanticKind::TcpRotationVectorZ}},
        {DesignIntentTemplateKind::FullKinematicDesign, "full-kinematic-design", "1",
         "Full Kinematic Design",
         {SemanticKind::LinkLength, SemanticKind::JointOriginOffsetX,
          SemanticKind::JointOriginOffsetY, SemanticKind::JointOriginOffsetZ,
          SemanticKind::JointOffsetAlongAxis, SemanticKind::JointAxisTiltU,
          SemanticKind::JointAxisTiltV,
          SemanticKind::BaseTx, SemanticKind::BaseTy, SemanticKind::BaseTz,
          SemanticKind::BaseRotationVectorX, SemanticKind::BaseRotationVectorY,
          SemanticKind::BaseRotationVectorZ, SemanticKind::TcpTx, SemanticKind::TcpTy,
          SemanticKind::TcpTz, SemanticKind::TcpRotationVectorX,
          SemanticKind::TcpRotationVectorY, SemanticKind::TcpRotationVectorZ,
          SemanticKind::FlangeTx, SemanticKind::FlangeTy, SemanticKind::FlangeTz,
          SemanticKind::FlangeRotationVectorX, SemanticKind::FlangeRotationVectorY,
          SemanticKind::FlangeRotationVectorZ, SemanticKind::LinkRadius,
          SemanticKind::LinkWidth, SemanticKind::LinkHeight,
          SemanticKind::LinkCrossSectionX, SemanticKind::LinkCrossSectionY,
          SemanticKind::LinkWallThickness, SemanticKind::LinkScale,
          SemanticKind::ParameterizedMaterial}}
    };
    return definitions;
}

} // namespace

std::vector<StructureOptimizationTemplateInfo>
StructureOptimizationTemplate::available()
{
    return {
        {StructureOptimizationTemplateKind::Balanced,
         "balanced", "Balanced", "Balances reachability, safety margin and compactness."},
        {StructureOptimizationTemplateKind::ReachabilityFirst,
         "reachability-first", "Reachability First", "Prioritizes task reachability and usable IK solutions."},
        {StructureOptimizationTemplateKind::CompactnessFirst,
         "compactness-first", "Compactness First", "Prioritizes a shorter, more compact kinematic chain."},
        {StructureOptimizationTemplateKind::WorkspaceFirst,
         "workspace-first", "Workspace First", "Prioritizes configured workspace coverage."}
    };
}

const char* StructureOptimizationTemplate::id(StructureOptimizationTemplateKind kind)
{
    switch (kind) {
    case StructureOptimizationTemplateKind::ReachabilityFirst:
        return "reachability-first";
    case StructureOptimizationTemplateKind::CompactnessFirst:
        return "compactness-first";
    case StructureOptimizationTemplateKind::WorkspaceFirst:
        return "workspace-first";
    case StructureOptimizationTemplateKind::Balanced:
    default:
        return "balanced";
    }
}

std::vector< DesignIntentTemplateInfo > StructureOptimizationTemplate::availableDesignIntents()
{
    return designIntentDefinitions();
}

const DesignIntentTemplateInfo* StructureOptimizationTemplate::designIntent(
    DesignIntentTemplateKind kind)
{
    const std::vector< DesignIntentTemplateInfo >& definitions = designIntentDefinitions();
    const auto found = std::find_if(
        definitions.begin(), definitions.end(), [kind](const DesignIntentTemplateInfo& definition) {
            return definition.kind == kind;
        });
    return found == definitions.end() ? nullptr : &*found;
}

bool StructureOptimizationTemplate::apply(StructureOptimizationTemplateKind kind,
                                          StructureOptimizationProblem& problem,
                                          std::string* error)
{
    std::string modelReason;
    if (!StructureOptimizationValidation::hasCompleteModel(problem.context.modelSpec,
                                                           &modelReason)) {
        if (error != nullptr)
            *error = "A complete robot model is required before applying a template.";
        return false;
    }

    if (kind == StructureOptimizationTemplateKind::WorkspaceFirst &&
        problem.evaluation.coverageBoxes.empty() &&
        !problem.evaluation.coverageBox.enabled) {
        if (error != nullptr)
            *error = "Workspace First requires at least one enabled workspace coverage region.";
        return false;
    }

    problem.objectives = objectivesFor(kind);
    problem.run.strategy = StructureStrategyKind::Hybrid;
    problem.run.candidateCount = kind == StructureOptimizationTemplateKind::Balanced ? 300 : 500;
    problem.run.eliteCount = kind == StructureOptimizationTemplateKind::Balanced ? 20 : 30;
    problem.run.localEliteCount = kind == StructureOptimizationTemplateKind::Balanced ? 5 : 8;
    problem.run.finalVerificationCount = kind == StructureOptimizationTemplateKind::Balanced ? 3 : 5;
    problem.run.maxLocalSweeps = kind == StructureOptimizationTemplateKind::Balanced ? 20 : 30;
    problem.run.gridSteps = 3;
    if (error != nullptr)
        error->clear();
    return true;
}

} // namespace rws
