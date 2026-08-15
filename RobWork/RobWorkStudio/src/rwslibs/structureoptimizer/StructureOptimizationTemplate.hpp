#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONTEMPLATE_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONTEMPLATE_HPP

#include "StructureOptimizationTypes.hpp"

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

class StructureOptimizationTemplate
{
public:
    static std::vector<StructureOptimizationTemplateInfo> available();

    static bool apply(StructureOptimizationTemplateKind kind,
                      StructureOptimizationProblem& problem,
                      std::string* error = nullptr);

    static const char* id(StructureOptimizationTemplateKind kind);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONTEMPLATE_HPP
