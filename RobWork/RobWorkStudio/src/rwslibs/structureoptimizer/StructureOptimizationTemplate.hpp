#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONTEMPLATE_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONTEMPLATE_HPP

#include "StructureOptimizationTypes.hpp"
#include "DesignVariable.hpp"

#include <string>
#include <vector>

namespace rws {

enum class StructureOptimizationTemplateKind
{
    Balanced,
    ReachabilityFirst,
    CompactnessFirst,
    WorkspaceFirst
};

struct StructureOptimizationTemplateInfo
{
    StructureOptimizationTemplateKind kind = StructureOptimizationTemplateKind::Balanced;
    std::string id;
    std::string label;
    std::string description;
};

/**
 * Core-only design-space intent.  This is intentionally separate from the
 * legacy objective-weight presets above, which remain the Widget's API.
 */
enum class DesignIntentTemplateKind
{
    KinematicBasic,
    KinematicWithJointAxis,
    KinematicWithBaseTcp,
    FullKinematicDesign
};

struct DesignIntentTemplateInfo
{
    DesignIntentTemplateKind kind = DesignIntentTemplateKind::KinematicBasic;
    std::string id;
    std::string version;
    std::string label;
    std::vector< SemanticKind > semanticKinds;
};

class StructureOptimizationTemplate
{
public:
    static std::vector<StructureOptimizationTemplateInfo> available();

    static bool apply(StructureOptimizationTemplateKind kind,
                      StructureOptimizationProblem& problem,
                      std::string* error = nullptr);

    static const char* id(StructureOptimizationTemplateKind kind);

    static std::vector< DesignIntentTemplateInfo > availableDesignIntents();
    static const DesignIntentTemplateInfo* designIntent(DesignIntentTemplateKind kind);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONTEMPLATE_HPP
