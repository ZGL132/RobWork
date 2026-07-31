#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISPROJECTDOCUMENT_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISPROJECTDOCUMENT_HPP

#include "KinematicAnalysisTypes.hpp"
#include "KinematicAnalysisVisualizationTypes.hpp"

#include <QByteArray>
#include <QString>

#include <array>
#include <vector>

namespace rws {

// 项目文档只描述“如何重新执行分析”，不保存任何一次运行产生的结果。
// 设备和 TCP 使用稳定名称绑定到当前 WorkCell，项目文件中不写入模型、场景或导出目录路径。
struct KinematicAnalysisProjectSettings
{
    // 设备与 TCP 以稳定名称绑定到当前 WorkCell，不保存模型/场景路径，保证项目可搬迁。
    QString deviceName;
    QString tcpFrameName;

    // IK 求解的目标位姿(位置/姿态)与判定参数。
    std::array< double, 3 > ikPositionMeters = {{0.0, 0.0, 0.0}};
    std::array< double, 3 > ikRpyDeg = {{0.0, 0.0, 0.0}};
    double ikDuplicateQThreshold = 1e-4;
    bool ikCollisionCheck = true;
    KinematicLengthUnit lengthUnit = KinematicLengthUnit::Meters;
    KinematicAngleUnit angleUnit = KinematicAngleUnit::Degrees;

    // 工作空间采样配置与着色模式。
    WorkspaceSamplingConfig workspace;
    WorkspaceColorMode workspaceColorMode = WorkspaceColorMode::Reachability;

    // 姿态可达性分析与手动位姿源。
    PoseReachabilityConfig poseReachability;
    bool poseTaskPointsSource = true;
    std::vector< std::array< double, 3 > > manualPosePositions;

    // 可视化偏好(仅影响渲染,不改变分析结果)。
    VisualPointSource visualSource = VisualPointSource::TaskPoint;
    VisualProjection visualProjection = VisualProjection::XY;
    VisualScalarMode visualScalarMode = VisualScalarMode::Status;
    VisualRenderMode visualRenderMode = VisualRenderMode::Scatter;
    int envelopeDirections = 180;
    bool showPass = true;
    bool showWarning = true;
    bool showFail = true;
    bool showUnknown = true;
    bool showLabels = false;
    bool showGrid = true;
    bool showLegend = true;
    double pointSize = 4.5;

    // 任务点列表与各指标阈值。
    KinematicThresholds thresholds;
    std::vector< TaskPoint > taskPoints;
};

// 负责 KinematicAnalysis 项目资源的稳定序列化。该类不依赖 QWidget，既能被 Widget 使用，
// 也能被核心测试直接验证，从而保证“脏比较”和“Provider 暂存保存”使用完全相同的 JSON。
class KinematicAnalysisProjectDocument
{
  public:
    // 序列化可编辑分析配置为规范 JSON（见 .cpp 中格式说明）。
    static QByteArray toJson (const KinematicAnalysisProjectSettings& settings);
    // 反序列化并校验：成功返回 true 并填充 settings；失败返回 false 并经 error 回填原因。
    static bool fromJson (const QByteArray& json,
                          KinematicAnalysisProjectSettings& settings,
                          QString* error = nullptr);
};

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISPROJECTDOCUMENT_HPP
