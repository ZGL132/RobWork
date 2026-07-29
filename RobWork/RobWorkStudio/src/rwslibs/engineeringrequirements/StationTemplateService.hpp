#ifndef RWS_ENGINEERINGREQUIREMENTS_STATIONTEMPLATESERVICE_HPP
#define RWS_ENGINEERINGREQUIREMENTS_STATIONTEMPLATESERVICE_HPP

#include "EngineeringRequirementTypes.hpp"

#include <array>
#include <string>
#include <vector>

namespace rws {

enum class StationTemplateKind { BinPicking, MachineTending, Palletizing, Inspection, ToolChange, Handover };
enum class StationArrayKind { Linear, Rectangular, Circular };

struct StationTemplateRequest {
    StationTemplateKind kind = StationTemplateKind::BinPicking;
    std::string instanceId;
    std::string idPrefix;
    std::string namePrefix;
    std::string referenceFrame = "WORLD";
    std::string tcpFrame;
    RequirementLevel level = RequirementLevel::Must;
    std::array<double, 3> operationOffsetMeters = {{0.0, 0.0, 0.0}};
    int rows = 1;
    int columns = 1;
    int layers = 1;
    double rowSpacingMeters = 0.05;
    double columnSpacingMeters = 0.05;
    double layerSpacingMeters = 0.05;
    double approachDistanceMeters = 0.10;
    double retractDistanceMeters = 0.10;
    double clearanceMeters = 0.15;
};

struct StationArrayRequest {
    StationArrayKind kind = StationArrayKind::Linear;
    std::string instanceId;
    std::string idPrefix;
    std::string namePrefix;
    int primaryCount = 2;
    int secondaryCount = 1;
    std::array<double, 3> primaryStepMeters = {{0.05, 0.0, 0.0}};
    std::array<double, 3> secondaryStepMeters = {{0.0, 0.05, 0.0}};
    double radiusMeters = 0.10;
    double startAngleDeg = 0.0;
    double endAngleDeg = 360.0;
};

struct TemplateUpdatePreview {
    std::string instanceId;
    std::vector<std::string> replacedStationIds;
    std::vector<PoseTask> generatedStations;
};

class StationTemplateService {
  public:
    static bool appendTemplate(RequirementSet& requirements, const StationTemplateRequest& request,
                               std::string* error = nullptr);
    static bool previewTemplateUpdate(const RequirementSet& requirements, const std::string& instanceId,
                                      const StationTemplateRequest& request, TemplateUpdatePreview& preview,
                                      std::string* error = nullptr);
    static bool applyTemplateUpdate(RequirementSet& requirements, const TemplateUpdatePreview& preview,
                                    std::string* error = nullptr);
    static bool detachStation(RequirementSet& requirements, const std::string& stationId,
                              std::string* error = nullptr);
    static bool appendArray(RequirementSet& requirements, const std::string& sourceStationId,
                            const StationArrayRequest& request, std::string* error = nullptr);

    static const char* toString(StationTemplateKind kind);
    static const char* toString(StationArrayKind kind);
};

} // namespace rws

#endif
