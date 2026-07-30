#include "StationTemplateService.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace rws {
namespace {

constexpr int maximumGeneratedStations = 10000;

bool isFinite(const std::array<double, 3>& values)
{
    return std::all_of(values.begin(), values.end(), [] (double value) { return std::isfinite(value); });
}

std::string number(double value)
{
    std::ostringstream stream;
    stream.precision(12);
    stream << value;
    return stream.str();
}

std::string polylineParameter(const std::vector<std::array<double, 3>>& points)
{
    std::ostringstream stream;
    for (std::size_t index = 0; index < points.size(); ++index) {
        if (index != 0) stream << ';';
        stream << number(points[index][0]) << ',' << number(points[index][1]) << ',' << number(points[index][2]);
    }
    return stream.str();
}

bool samplePolylineByArcLength(const std::vector<std::array<double, 3>>& points, int count,
                               std::vector<std::array<double, 3>>& samples)
{
    std::vector<double> accumulated(points.size(), 0.0);
    for (std::size_t index = 1; index < points.size(); ++index) {
        double squaredLength = 0.0;
        for (int axis = 0; axis < 3; ++axis) {
            const double delta = points[index][axis] - points[index - 1][axis];
            squaredLength += delta * delta;
        }
        accumulated[index] = accumulated[index - 1] + std::sqrt(squaredLength);
    }
    const double totalLength = accumulated.back();
    if (!(totalLength > 0.0) || !std::isfinite(totalLength))
        return false;
    samples.clear();
    samples.reserve(static_cast<std::size_t>(count));
    // 每个采样目标以累计弧长定位，再在线段内做线性插值；这使折线转角不会改变点间距离。
    for (int sampleIndex = 0; sampleIndex < count; ++sampleIndex) {
        const double target = count == 1 ? 0.0 : totalLength * sampleIndex / (count - 1);
        std::size_t segment = 1;
        while (segment + 1 < accumulated.size() && accumulated[segment] < target)
            ++segment;
        const double startLength = accumulated[segment - 1];
        const double endLength = accumulated[segment];
        const double ratio = endLength > startLength ? (target - startLength) / (endLength - startLength) : 0.0;
        std::array<double, 3> sample;
        for (int axis = 0; axis < 3; ++axis)
            sample[axis] = points[segment - 1][axis] + ratio * (points[segment][axis] - points[segment - 1][axis]);
        samples.push_back(sample);
    }
    return true;
}

std::set<std::string> stationIds(const RequirementSet& requirements)
{
    std::set<std::string> ids;
    for (const PoseTask& station : requirements.poseTasks)
        ids.insert(station.id);
    return ids;
}

std::string uniqueId(const std::string& prefix, std::set<std::string>& ids)
{
    const std::string base = prefix.empty() ? "station" : prefix;
    for (int suffix = 1; ; ++suffix) {
        const std::string candidate = base + "_" + std::to_string(suffix);
        if (ids.insert(candidate).second)
            return candidate;
    }
}

std::vector<GenerationParameter> templateParameters(const StationTemplateRequest& request)
{
    return {
        {"kind", StationTemplateService::toString(request.kind)},
        {"idPrefix", request.idPrefix},
        {"namePrefix", request.namePrefix},
        {"referenceFrame", request.referenceFrame},
        {"tcpFrame", request.tcpFrame},
        {"operationOffsetX", number(request.operationOffsetMeters[0])},
        {"operationOffsetY", number(request.operationOffsetMeters[1])},
        {"operationOffsetZ", number(request.operationOffsetMeters[2])},
        {"rows", std::to_string(request.rows)},
        {"columns", std::to_string(request.columns)},
        {"layers", std::to_string(request.layers)},
        {"rowSpacingMeters", number(request.rowSpacingMeters)},
        {"columnSpacingMeters", number(request.columnSpacingMeters)},
        {"layerSpacingMeters", number(request.layerSpacingMeters)},
        {"approachDistanceMeters", number(request.approachDistanceMeters)},
        {"retractDistanceMeters", number(request.retractDistanceMeters)},
        {"clearanceMeters", number(request.clearanceMeters)}
    };
}

std::vector<GenerationParameter> arrayParameters(const StationArrayRequest& request,
                                                  const std::string& sourceStationId)
{
    return {
        {"kind", StationTemplateService::toString(request.kind)},
        {"sourceStationId", sourceStationId},
        {"primaryCount", std::to_string(request.primaryCount)},
        {"secondaryCount", std::to_string(request.secondaryCount)},
        {"radiusMeters", number(request.radiusMeters)},
        {"startAngleDeg", number(request.startAngleDeg)},
        {"endAngleDeg", number(request.endAngleDeg)},
        {"polylinePointsMeters", polylineParameter(request.polylinePointsMeters)}
    };
}

bool validateTemplateRequest(const StationTemplateRequest& request, std::string* error)
{
    if (request.instanceId.empty()) {
        if (error != nullptr) *error = "Template instance ID is required.";
        return false;
    }
    // 只有离散阵列模板消费行、列、层数；其余模板不应因为 UI 隐藏的默认网格参数而被拒绝。
    if (request.kind == StationTemplateKind::BinPicking || request.kind == StationTemplateKind::Palletizing) {
        const long long count = static_cast<long long>(request.rows) * request.columns * request.layers;
        if (request.rows < 1 || request.columns < 1 || request.layers < 1 || count > maximumGeneratedStations) {
            if (error != nullptr) *error = "Template dimensions must create between 1 and 10000 stations.";
            return false;
        }
    }
    if (!isFinite(request.operationOffsetMeters) || !std::isfinite(request.rowSpacingMeters) ||
        !std::isfinite(request.columnSpacingMeters) || !std::isfinite(request.layerSpacingMeters) ||
        !std::isfinite(request.approachDistanceMeters) || !std::isfinite(request.retractDistanceMeters) ||
        !std::isfinite(request.clearanceMeters) || request.rowSpacingMeters < 0.0 ||
        request.columnSpacingMeters < 0.0 || request.layerSpacingMeters < 0.0 ||
        request.approachDistanceMeters < 0.0 || request.retractDistanceMeters < 0.0 ||
        request.clearanceMeters < 0.0) {
        if (error != nullptr) *error = "Template distances must be finite non-negative values.";
        return false;
    }
    return true;
}

bool validateArrayRequest(const StationArrayRequest& request, std::string* error)
{
    const long long count = static_cast<long long>(request.primaryCount) * request.secondaryCount;
    if (request.instanceId.empty()) {
        if (error != nullptr) *error = "Array instance ID is required.";
        return false;
    }
    if (request.primaryCount < 1 || request.secondaryCount < 1 || count > maximumGeneratedStations) {
        if (error != nullptr) *error = "Array dimensions must create between 1 and 10000 stations.";
        return false;
    }
    if (!isFinite(request.primaryStepMeters) || !isFinite(request.secondaryStepMeters) ||
        !std::isfinite(request.radiusMeters) || !std::isfinite(request.startAngleDeg) ||
        !std::isfinite(request.endAngleDeg) || request.radiusMeters < 0.0) {
        if (error != nullptr) *error = "Array parameters must be finite and the radius cannot be negative.";
        return false;
    }
    if (request.kind == StationArrayKind::Polyline) {
        if (request.secondaryCount != 1 || request.polylinePointsMeters.size() < 2 ||
            !std::all_of(request.polylinePointsMeters.begin(), request.polylinePointsMeters.end(), isFinite)) {
            if (error != nullptr) *error = "Polyline arrays require at least two finite points and a secondary count of one.";
            return false;
        }
        std::vector<std::array<double, 3>> samples;
        if (!samplePolylineByArcLength(request.polylinePointsMeters, request.primaryCount, samples)) {
            if (error != nullptr) *error = "Polyline arrays require a non-zero total curve length.";
            return false;
        }
    }
    return true;
}

PoseTask generatedStation(const StationTemplateRequest& request, const std::string& generatorId,
                          const std::string& id, const std::string& name, ProcessType process,
                          const std::array<double, 3>& offset, RequirementLevel level)
{
    PoseTask station;
    station.id = id;
    station.name = name;
    station.source = PoseTaskSource::Template;
    station.processType = process;
    station.level = level;
    station.refFrame = request.referenceFrame.empty() ? "WORLD" : request.referenceFrame;
    station.tcpFrame = request.tcpFrame;
    station.position = offset;
    if (station.refFrame != "WORLD") {
        station.orientation.mode = OrientationMode::AlignFrame;
        station.orientation.targetFrame = station.refFrame;
    }
    station.approach.enabled = request.approachDistanceMeters > 0.0;
    station.approach.axis = OffsetAxis::ToolZ;
    station.approach.distanceMeters = request.approachDistanceMeters;
    station.retract.enabled = request.retractDistanceMeters > 0.0;
    station.retract.axis = OffsetAxis::ReferenceZ;
    station.retract.distanceMeters = request.retractDistanceMeters;
    station.generation.generatorId = generatorId;
    station.generation.instanceId = request.instanceId;
    station.generation.linked = true;
    station.generation.parameters = templateParameters(request);
    return station;
}

std::vector<PoseTask> generateTemplate(const StationTemplateRequest& request, std::set<std::string>& ids)
{
    const std::string namePrefix = request.namePrefix.empty() ? StationTemplateService::toString(request.kind) : request.namePrefix;
    const std::string idPrefix = request.idPrefix.empty() ? "station" : request.idPrefix;
    const std::string generatorId = std::string(StationTemplateService::toString(request.kind)) + ".v1";
    std::vector<PoseTask> stations;
    const auto add = [&] (const std::string& suffix, ProcessType process, const std::array<double, 3>& offset,
                           RequirementLevel level) {
        stations.push_back(generatedStation(request, generatorId, uniqueId(idPrefix, ids),
                                            namePrefix + " " + suffix, process, offset, level));
    };

    switch (request.kind) {
        case StationTemplateKind::BinPicking:
            for (int layer = 0; layer < request.layers; ++layer) {
                for (int row = 0; row < request.rows; ++row) {
                    for (int column = 0; column < request.columns; ++column) {
                        std::array<double, 3> offset = request.operationOffsetMeters;
                        offset[0] += (static_cast<double>(column) - (request.columns - 1) / 2.0) * request.columnSpacingMeters;
                        offset[1] += (static_cast<double>(row) - (request.rows - 1) / 2.0) * request.rowSpacingMeters;
                        offset[2] += static_cast<double>(layer) * request.layerSpacingMeters;
                        add("pick " + std::to_string(layer + 1) + "-" + std::to_string(row + 1) + "-" + std::to_string(column + 1),
                            ProcessType::Pick, offset, request.level);
                    }
                }
            }
            break;
        case StationTemplateKind::MachineTending: {
            const std::array<double, 3> operation = request.operationOffsetMeters;
            std::array<double, 3> standby = operation;
            standby[1] -= request.clearanceMeters;
            std::array<double, 3> approach = operation;
            approach[2] += request.approachDistanceMeters;
            std::array<double, 3> retract = operation;
            retract[2] += request.retractDistanceMeters;
            add("standby", ProcessType::SafeStandby, standby, RequirementLevel::Should);
            add("approach", ProcessType::MachineLoad, approach, request.level);
            add("load", ProcessType::MachineLoad, operation, request.level);
            add("unload", ProcessType::MachineUnload, operation, request.level);
            add("retract", ProcessType::MachineUnload, retract, request.level);
            break;
        }
        case StationTemplateKind::Palletizing:
            for (int layer = 0; layer < request.layers; ++layer) {
                const double z = request.operationOffsetMeters[2] + layer * request.layerSpacingMeters;
                for (int row = 0; row < request.rows; ++row) {
                    for (int column = 0; column < request.columns; ++column) {
                        std::array<double, 3> offset = request.operationOffsetMeters;
                        // 以阵列几何中心为零点，偶数和奇数行列都能保持对称，不再隐式固定 2x2。
                        offset[0] += (static_cast<double>(column) - (request.columns - 1) / 2.0) * request.columnSpacingMeters;
                        offset[1] += (static_cast<double>(row) - (request.rows - 1) / 2.0) * request.rowSpacingMeters;
                        offset[2] = z;
                        add("place " + std::to_string(layer + 1) + "-" + std::to_string(row + 1) + "-" + std::to_string(column + 1),
                            ProcessType::Place, offset, request.level);
                    }
                }
            }
            break;
        case StationTemplateKind::Inspection:
            add("inspection", ProcessType::Inspect, request.operationOffsetMeters, request.level);
            break;
        case StationTemplateKind::ToolChange:
            add("tool change", ProcessType::ToolChange, request.operationOffsetMeters, request.level);
            break;
        case StationTemplateKind::Handover:
            add("handover", ProcessType::Handover, request.operationOffsetMeters, request.level);
            break;
    }
    return stations;
}

} // namespace

const char* StationTemplateService::toString(StationTemplateKind kind)
{
    switch (kind) {
        case StationTemplateKind::BinPicking: return "BinPicking";
        case StationTemplateKind::MachineTending: return "MachineTending";
        case StationTemplateKind::Palletizing: return "Palletizing";
        case StationTemplateKind::Inspection: return "Inspection";
        case StationTemplateKind::ToolChange: return "ToolChange";
        case StationTemplateKind::Handover: return "Handover";
    }
    return "BinPicking";
}

const char* StationTemplateService::toString(StationArrayKind kind)
{
    switch (kind) {
        case StationArrayKind::Linear: return "LinearArray";
        case StationArrayKind::Rectangular: return "RectangularArray";
        case StationArrayKind::Circular: return "CircularArray";
        case StationArrayKind::Polyline: return "PolylineArray";
    }
    return "LinearArray";
}

bool StationTemplateService::appendTemplate(RequirementSet& requirements,
                                            const StationTemplateRequest& request, std::string* error)
{
    if (!validateTemplateRequest(request, error)) return false;
    for (const PoseTask& station : requirements.poseTasks) {
        if (station.generation.instanceId == request.instanceId && station.generation.linked) {
            if (error != nullptr) *error = "Template instance ID already exists. Update it instead of appending another instance.";
            return false;
        }
    }
    std::set<std::string> ids = stationIds(requirements);
    std::vector<PoseTask> generated = generateTemplate(request, ids);
    requirements.poseTasks.insert(requirements.poseTasks.end(), generated.begin(), generated.end());
    if (error != nullptr) error->clear();
    return true;
}

bool StationTemplateService::previewTemplateUpdate(const RequirementSet& requirements, const std::string& instanceId,
                                                   const StationTemplateRequest& request, TemplateUpdatePreview& preview,
                                                   std::string* error)
{
    if (instanceId.empty() || request.instanceId != instanceId) {
        if (error != nullptr) *error = "The template update request must keep the selected instance ID.";
        return false;
    }
    if (!validateTemplateRequest(request, error)) return false;
    preview = TemplateUpdatePreview();
    preview.instanceId = instanceId;
    RequirementSet working = requirements;
    working.poseTasks.erase(std::remove_if(working.poseTasks.begin(), working.poseTasks.end(),
        [&] (const PoseTask& station) {
            if (station.generation.instanceId != instanceId || !station.generation.linked) return false;
            preview.replacedStationIds.push_back(station.id);
            return true;
        }), working.poseTasks.end());
    if (preview.replacedStationIds.empty()) {
        if (error != nullptr) *error = "No linked stations were found for the selected template instance.";
        return false;
    }
    std::set<std::string> ids = stationIds(working);
    preview.generatedStations = generateTemplate(request, ids);
    if (error != nullptr) error->clear();
    return true;
}

bool StationTemplateService::applyTemplateUpdate(RequirementSet& requirements, const TemplateUpdatePreview& preview,
                                                 std::string* error)
{
    if (preview.instanceId.empty() || preview.replacedStationIds.empty() || preview.generatedStations.empty()) {
        if (error != nullptr) *error = "Template update preview is incomplete.";
        return false;
    }
    RequirementSet updated = requirements;
    const std::set<std::string> replaced(preview.replacedStationIds.begin(), preview.replacedStationIds.end());
    updated.poseTasks.erase(std::remove_if(updated.poseTasks.begin(), updated.poseTasks.end(),
        [&] (const PoseTask& station) { return replaced.find(station.id) != replaced.end(); }), updated.poseTasks.end());
    std::set<std::string> ids = stationIds(updated);
    for (const PoseTask& station : preview.generatedStations) {
        if (station.generation.instanceId != preview.instanceId || !station.generation.linked ||
            station.id.empty() || !ids.insert(station.id).second) {
            if (error != nullptr) *error = "Template update contains an invalid or duplicate station.";
            return false;
        }
    }
    updated.poseTasks.insert(updated.poseTasks.end(), preview.generatedStations.begin(), preview.generatedStations.end());
    requirements = updated;
    if (error != nullptr) error->clear();
    return true;
}

bool StationTemplateService::detachStation(RequirementSet& requirements, const std::string& stationId,
                                           std::string* error)
{
    const auto it = std::find_if(requirements.poseTasks.begin(), requirements.poseTasks.end(),
        [&] (const PoseTask& station) { return station.id == stationId; });
    if (it == requirements.poseTasks.end() || it->generation.instanceId.empty()) {
        if (error != nullptr) *error = "The selected station is not generated by a template or array.";
        return false;
    }
    it->generation.linked = false;
    if (error != nullptr) error->clear();
    return true;
}

bool StationTemplateService::appendArray(RequirementSet& requirements, const std::string& sourceStationId,
                                         const StationArrayRequest& request, std::string* error)
{
    if (!validateArrayRequest(request, error)) return false;
    const auto sourceIt = std::find_if(requirements.poseTasks.begin(), requirements.poseTasks.end(),
        [&] (const PoseTask& station) { return station.id == sourceStationId; });
    if (sourceIt == requirements.poseTasks.end()) {
        if (error != nullptr) *error = "The source station for the array no longer exists.";
        return false;
    }
    std::set<std::string> ids = stationIds(requirements);
    const PoseTask source = *sourceIt;
    const std::string idPrefix = request.idPrefix.empty() ? source.id : request.idPrefix;
    const std::string namePrefix = request.namePrefix.empty() ? source.name : request.namePrefix;
    const std::string generatorId = std::string(toString(request.kind)) + ".v1";
    const std::vector<GenerationParameter> parameters = arrayParameters(request, sourceStationId);
    const double pi = std::acos(-1.0);
    std::vector<std::array<double, 3>> polylineSamples;
    if (request.kind == StationArrayKind::Polyline &&
        !samplePolylineByArcLength(request.polylinePointsMeters, request.primaryCount, polylineSamples)) {
        if (error != nullptr) *error = "Polyline array sampling failed.";
        return false;
    }
    std::vector<PoseTask> generated;
    for (int primary = 0; primary < request.primaryCount; ++primary) {
        for (int secondary = 0; secondary < request.secondaryCount; ++secondary) {
            PoseTask station = source;
            station.id = uniqueId(idPrefix, ids);
            station.name = namePrefix + " " + std::to_string(primary + 1) + "-" + std::to_string(secondary + 1);
            station.source = PoseTaskSource::Template;
            if (request.kind == StationArrayKind::Linear) {
                for (int axis = 0; axis < 3; ++axis)
                    station.position[axis] += (primary + 1) * request.primaryStepMeters[axis];
            } else if (request.kind == StationArrayKind::Rectangular) {
                for (int axis = 0; axis < 3; ++axis)
                    station.position[axis] += (primary + 1) * request.primaryStepMeters[axis] +
                                              (secondary + 1) * request.secondaryStepMeters[axis];
            } else if (request.kind == StationArrayKind::Circular) {
                const int count = request.primaryCount * request.secondaryCount;
                const int index = primary * request.secondaryCount + secondary;
                const double angle = (request.startAngleDeg +
                    (count == 1 ? 0.0 : (request.endAngleDeg - request.startAngleDeg) * index / (count - 1))) * pi / 180.0;
                station.position[0] += request.radiusMeters * std::cos(angle);
                station.position[1] += request.radiusMeters * std::sin(angle);
            } else {
                // 折线采样点已经是参考系内的绝对位置，保留源工位的 TCP、姿态、工艺类型和校验策略。
                station.position = polylineSamples[static_cast<std::size_t>(primary)];
            }
            station.generation.generatorId = generatorId;
            station.generation.instanceId = request.instanceId;
            station.generation.linked = false;
            station.generation.parameters = parameters;
            generated.push_back(station);
        }
    }
    requirements.poseTasks.insert(requirements.poseTasks.end(), generated.begin(), generated.end());
    if (error != nullptr) error->clear();
    return true;
}

} // namespace rws
