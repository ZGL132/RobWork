#include "LocalSearch.hpp"

#include "EliteSelector.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>

namespace rws {
namespace {

bool finite(double value)
{
    return std::isfinite(value);
}

bool canceled(const LocalSearchCallbacks& callbacks)
{
    return callbacks.isCancellationRequested && callbacks.isCancellationRequested();
}

bool isAcceptable(const CandidateResult& result)
{
    // Quick 或 DataInsufficient 只能作为观测结果保留，绝不能推进局部中心。
    return result.feasibility == Feasibility::Feasible &&
           result.evidenceStage == AnalysisEvidenceStage::Verified;
}

double score(const CandidateResult& result)
{
    return EliteSelector::score(result);
}

double defaultRadius(const LocalSearchVariable& variable)
{
    if (variable.maximum <= variable.minimum)
        return 0.0;
    if (variable.domain != VariableDomain::Discrete && finite(variable.step) &&
        variable.step > 0.0)
        return variable.step / (variable.maximum - variable.minimum);
    // 没有声明工程步长时沿用计划中的 15% 归一化局部邻域，避免连续变量
    // 因 step=0 而完全失去可搜索的邻居。
    return variable.domain == VariableDomain::Continuous ? 0.15 : 0.0;
}

double radiusFor(const LocalSearchVariable& variable,
                 std::size_t index,
                 const LocalSearchConfig& config)
{
    if (index < config.radii.size() && finite(config.radii[index]) &&
        config.radii[index] > 0.0)
        return config.radii[index];
    return defaultRadius(variable);
}

bool normalizeBoundary(double& value, LocalSearchBoundaryHandling boundary)
{
    if (value >= 0.0 && value <= 1.0)
        return true;
    if (boundary == LocalSearchBoundaryHandling::Skip)
        return false;
    if (boundary == LocalSearchBoundaryHandling::Clamp) {
        value = std::max(0.0, std::min(1.0, value));
        return true;
    }

    // 反射使用循环而非一次 if，确保半径大于整个区间时仍得到合法坐标。
    while (value < 0.0 || value > 1.0) {
        if (value < 0.0)
            value = -value;
        if (value > 1.0)
            value = 2.0 - value;
    }
    return true;
}

double quantize(const LocalSearchVariable& variable, double value)
{
    if (variable.domain == VariableDomain::Discrete) {
        if (variable.discreteOptionCount <= 1)
            return 0.0;
        const double last = static_cast<double>(variable.discreteOptionCount - 1);
        return std::round(value * last) / last;
    }
    if (variable.domain != VariableDomain::Integer || !finite(variable.step) ||
        variable.step <= 0.0 || variable.maximum <= variable.minimum)
        return value;

    const double engineering = variable.minimum +
                                value * (variable.maximum - variable.minimum);
    const double steps = std::round((engineering - variable.minimum) / variable.step);
    const double quantized = variable.minimum + steps * variable.step;
    return (quantized - variable.minimum) / (variable.maximum - variable.minimum);
}

bool appendCoordinate(std::vector<double>& design,
                      const std::vector<LocalSearchVariable>& variables,
                      std::size_t index,
                      double delta,
                      LocalSearchBoundaryHandling boundary)
{
    const LocalSearchVariable& variable = variables[index];
    if (!variable.enabled || variable.role == VariableRole::Derived)
        return false;
    double value = design[index] + delta;
    if (!normalizeBoundary(value, boundary))
        return false;
    value = quantize(variable, value);
    if (!normalizeBoundary(value, boundary))
        return false;
    if (value == design[index])
        return false;
    design[index] = value;
    return true;
}

std::string keyFor(const std::vector<double>& design)
{
    std::ostringstream stream;
    stream << std::setprecision(17);
    for (double value : design)
        stream << value << ';';
    return stream.str();
}

} // namespace

LocalSearchResult LocalSearch::run(
    const std::vector<LocalSearchVariable>& variables,
    const LocalSearchCenter& center,
    const LocalSearchConfig& config,
    const LocalSearchEvaluationCallback& evaluate,
    const LocalSearchCallbacks& callbacks)
{
    LocalSearchResult output;
    output.best = center;
    if (!evaluate || config.maxSweeps == 0 || !isAcceptable(center.result) ||
        center.normalizedDesign.size() != variables.size())
        return output;

    for (double value : center.normalizedDesign)
        if (!finite(value) || value < 0.0 || value > 1.0)
            return output;

    std::set<std::string> visited;
    visited.insert(keyFor(center.normalizedDesign));
    bool improvedInPreviousSweep = true;

    for (std::size_t sweep = 0; sweep < config.maxSweeps && improvedInPreviousSweep;
         ++sweep) {
        if (canceled(callbacks)) {
            output.canceled = true;
            break;
        }
        improvedInPreviousSweep = false;
        ++output.sweeps;
        std::vector<std::vector<double>> neighbors;

        auto addNeighbor = [&](const std::vector<double>& design) {
            if (visited.insert(keyFor(design)).second)
                neighbors.push_back(design);
        };

        // 先生成单变量邻居，再生成分组邻居，顺序固定以保证跨运行可复现。
        for (std::size_t index = 0; index < variables.size(); ++index) {
            if (!variables[index].enabled || variables[index].role == VariableRole::Derived)
                continue;
            if (variables[index].domain == VariableDomain::Discrete) {
                if (variables[index].discreteOptionCount <= 1)
                    continue;
                const double step = 1.0 /
                                    static_cast<double>(variables[index].discreteOptionCount - 1);
                for (double direction : {1.0, -1.0}) {
                    std::vector<double> design = output.best.normalizedDesign;
                    if (appendCoordinate(design, variables, index, direction * step,
                                         config.boundary))
                        addNeighbor(design);
                }
            }
            else {
                const double radius = radiusFor(variables[index], index, config);
                for (double direction : {1.0, -1.0}) {
                    std::vector<double> design = output.best.normalizedDesign;
                    if (radius > 0.0 &&
                        appendCoordinate(design, variables, index, direction * radius,
                                         config.boundary))
                        addNeighbor(design);
                }
            }
        }

        for (const std::vector<std::size_t>& group : config.groups) {
            for (double direction : {1.0, -1.0}) {
                std::vector<double> design = output.best.normalizedDesign;
                bool changed = false;
                for (std::size_t index : group) {
                    if (index >= variables.size())
                        continue;
                    const double radius = radiusFor(variables[index], index, config);
                    if (radius > 0.0)
                        changed = appendCoordinate(design, variables, index,
                                                   direction * radius, config.boundary) || changed;
                }
                if (changed)
                    addNeighbor(design);
            }
        }

        for (const std::vector<double>& design : neighbors) {
            if (canceled(callbacks)) {
                output.canceled = true;
                break;
            }
            if (config.maxEvaluations != 0 &&
                output.evaluatedCount >= config.maxEvaluations)
                return output;

            LocalSearchEvaluation evaluation;
            evaluation.normalizedDesign = design;
            evaluation.result = evaluate(design, AnalysisEvidenceStage::Verified);
            evaluation.sweep = sweep;
            output.evaluations.push_back(evaluation);
            ++output.evaluatedCount;

            if (isAcceptable(evaluation.result) &&
                score(evaluation.result) > score(output.best.result) +
                                                 config.improvementTolerance) {
                output.best.normalizedDesign = design;
                output.best.result = evaluation.result;
                improvedInPreviousSweep = true;
            }
        }
        if (output.canceled)
            break;
    }
    return output;
}

} // namespace rws
