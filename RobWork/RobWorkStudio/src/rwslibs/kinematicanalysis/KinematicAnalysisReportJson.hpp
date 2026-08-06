// =============================================================================
//  KinematicAnalysisReportJson.hpp —— 分析报告 JSON / CSV 序列化(声明)
// =============================================================================
//
// 本组件把一次完整的运动学分析结果(KinematicAnalysisReport)序列化为
// 结构化 JSON 或平面 CSV,并支持反向解析(JSON -> 结构体),用于:
//   - 插件外导出 / 导入分析报告;
//   - 冻结工件中保存评估快照并跨项目 / 跨会话恢复;
//   - 面向表格的 CSV 摘要(任务级与区域级)。
//
// 序列化不变量:
//   - schemaVersion 版本化报告结构,旧版报告在解析时按缺省字段向后兼容
//     (例如早期报告没有 level 字段,按 Must 解释);
//   - 任何非有限数值(double 为 NaN / ±inf)在写出时转为 JSON null,
//     并在报告顶层附加 KIN_REPORT_NONFINITE 告警,绝不写入非法 JSON;
//   - 所有解析函数在字段缺失 / 类型不符 / 枚举未知时返回 false 并给出 error,
//     不会静默地以默认值掩盖数据损坏。
//
// 相关结构:KinematicAnalysisReport(报告本体)、KinematicAnalysisReportFilters
// (派生视图过滤,只读不改写源报告)。
#ifndef RWS_KINEMATICANALYSIS_REPORTJSON_HPP
#define RWS_KINEMATICANALYSIS_REPORTJSON_HPP

#include "KinematicAnalysisTypes.hpp"

#include <rwslibs/robotanalysiscore/RequirementExecutionTypes.hpp>

#include <QJsonObject>

#include <string>

namespace rws {

// =============================================================================
//  KinematicAnalysisReport —— 一次分析报告的完整内容
// =============================================================================
//
// 报告顶层结构:元信息(schemaVersion / pluginName / analysisId)+
// 溯源(provenance)+ 整体可行性/质量/证据等级 + 当前位姿 +
// 任务点结果列表 + 区域结果列表 + 全局告警列表。
struct KinematicAnalysisReport
{
    // 报告结构版本号;结构演进(增删字段)时递增,解析端据此判断兼容性。
    int schemaVersion = 1;
    // 产生该报告的插件名,用于跨插件报告识别来源。
    std::string pluginName = "kinematicanalysis";
    // 分析会话 ID,用于关联同一会话的多份报告。
    std::string analysisId;
    // 溯源信息(需求 / 模型 / 环境指纹与冻结时间),保证结果可复现、可审计。
    RequirementExecutionProvenance provenance;
    // 报告整体可行性:由任务 / 区域结果聚合而来。
    Feasibility feasibility = Feasibility::NotEvaluated;
    // 报告整体质量:Good / Degraded / Critical / Unknown。
    Quality quality = Quality::Unknown;
    // 报告整体证据等级(Estimated / Quick / Verified)。
    AnalysisEvidenceStage evidenceStage = AnalysisEvidenceStage::Estimated;
    // 当前位姿结果(TCP 位姿 + 雅可比 + 奇异/裕度等),见 KinematicAnalysisTypes。
    KinematicCurrentPoseResult currentPose;
    // 全部任务点的评估结果(每个 TaskPoint 一个 TargetEvaluation)。
    std::vector< TargetEvaluation > taskResults;
    // 全部区域覆盖率评估结果。
    std::vector< RegionCoverageResult > regionResults;
    // 全局告警(超出采样上限、非有限数值被序列化为 null 等)。
    std::vector< AnalysisWarning > warnings;
};

// =============================================================================
//  KinematicAnalysisReportFilters —— 报告派生视图的过滤条件
// =============================================================================
//
// 每个 filterXxx 布尔开关与其对应的取值字段成对出现:开关为 false 时该维度
// 不参与过滤(恒真),为 true 时要求结果字段与取值精确相等。regionId 是唯一
// 不需要开关的字段:非空即要求区域 ID 匹配。
// 过滤只作用在 filterReportView 生成的副本视图上,绝不修改源报告。
// Filters apply only to a derived report view. The source report and its
// result vectors are never modified.
struct KinematicAnalysisReportFilters
{
    // 证据等级过滤值与开关。
    AnalysisEvidenceStage stage = AnalysisEvidenceStage::Estimated;
    bool filterStage = false;
    // 可行性过滤值与开关。
    Feasibility feasibility = Feasibility::NotEvaluated;
    bool filterFeasibility = false;
    // 质量过滤值与开关。
    Quality quality = Quality::Unknown;
    bool filterQuality = false;
    // 失败原因过滤值与开关;匹配时在任务 / 区域(或单元)级失败原因集合里查找。
    KinematicFailureReason failureReason = KinematicFailureReason::None;
    bool filterFailureReason = false;
    // 区域 ID 过滤:非空时只保留该区域的区域级结果。
    std::string regionId;
};

// =============================================================================
//  KinematicAnalysisReportJson —— 报告的 JSON / CSV 序列化器
// =============================================================================
//
// 纯静态工具类,不持有状态。toObject / fromObject 处理结构化的 QJsonObject,
// toJson / fromJson 封装为完整 JSON 文本往返,CSV 方法导出表格摘要。
class KinematicAnalysisReportJson
{
  public:
    // 把报告转换为 QJsonObject(非有限数值转 null 并置 nonFinite 标记)。
    static QJsonObject toObject (const KinematicAnalysisReport& report);
    // 从 QJsonObject 解析报告;失败返回 false 并经 error 给出原因(可为 nullptr)。
    static bool fromObject (const QJsonObject& object,
                            KinematicAnalysisReport& report,
                            std::string* error = nullptr);
    // 报告 -> 紧凑 JSON 字符串。
    static std::string toJson (const KinematicAnalysisReport& report);
    // JSON 字符串 -> 报告;含语法解析错误检查,失败经 error 给出原因。
    static bool fromJson (const std::string& json,
                          KinematicAnalysisReport& report,
                          std::string* error = nullptr);
    // 导出"任务级"CSV:每行一个任务点,含残差 / 裕度 / 可操作度等关键指标。
    static std::string taskCsv (const KinematicAnalysisReport& report);
    // 导出"区域级"CSV:每行一个区域,含覆盖单元数 / 位置与姿态覆盖率。
    static std::string regionCsv (const KinematicAnalysisReport& report);
};

// 按过滤条件生成报告的派生视图(深拷贝任务与区域子集)。
// 返回的报告共享顶层元信息,但 taskResults / regionResults 只保留匹配项。
KinematicAnalysisReport filterReportView (
    const KinematicAnalysisReport& report,
    const KinematicAnalysisReportFilters& filters);

} // namespace rws

#endif
