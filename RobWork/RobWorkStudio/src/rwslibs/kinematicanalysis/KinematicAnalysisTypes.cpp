#include "KinematicAnalysisTypes.hpp"

namespace rws {

// 单位换算与字符串解析的匿名命名空间 helper。
// 这些函数仅在本编译单元内可见,避免污染 rws:: 命名空间;所有单位换算都
// 以"1 米 / 1 度"为基准返回一个乘数,调用方通过乘法/除法完成双向换算。
namespace {
constexpr double Pi = 3.141592653589793238462643383279502884;

// 返回"1 米"在指定长度单位下对应的显示数值(换算乘数)。
//   Meters:1.0;Centimeters:100;Millimeters:1000;Inches:39.37007874...(=1/0.0254)。
// 供 displayLengthFromMeters / metersFromDisplayLength 复用,避免两处重复写常量。
double lengthDisplayPerMeter(KinematicLengthUnit unit)
{
    switch (unit) {
        case KinematicLengthUnit::Centimeters: return 100.0;
        case KinematicLengthUnit::Millimeters: return 1000.0;
        case KinematicLengthUnit::Inches:      return 39.37007874015748;
        case KinematicLengthUnit::Meters:
        default:                               return 1.0;
    }
}

// 返回"1 度"在指定角度单位下对应的显示数值(换算乘数)。
//   Degrees:1.0;Radians:PI/180;Grads:10/9(400 grad = 360 deg);Turns:1/360。
// 注意这里以"度"为基准而非弧度,因为内部 RPY 一律以度存取,仅 UI 显示时
// 才转成用户偏好单位。
double angleDisplayPerDegree(KinematicAngleUnit unit)
{
    switch (unit) {
        case KinematicAngleUnit::Radians: return Pi / 180.0;
        case KinematicAngleUnit::Grads:   return 10.0 / 9.0;
        case KinematicAngleUnit::Turns:   return 1.0 / 360.0;
        case KinematicAngleUnit::Degrees:
        default:                          return 1.0;
    }
}

// 字符串 -> 枚举解析失败时的统一处理:把 "Unknown <typeName>: <text>" 写入
// error(可选)并返回 false。所有 fromString 函数失败路径都汇聚到这里,
// 保证错误消息格式一致。
bool conversionFailed(const char* typeName, const std::string& text, std::string* error)
{
    if (error != nullptr)
        *error = std::string ("Unknown ") + typeName + " value: " + text;
    return false;
}

// 解析成功时清空 error,保证输出参数的"成功即空、失败即非空"约定。
void conversionSucceeded(std::string* error)
{
    if (error != nullptr)
        error->clear ();
}
}    // namespace

// 把 KinematicFailureReason 枚举映射成可读字符串。
// 之所以在 .cpp 里实现而不是用 X-macro,是因为枚举数量少、可读性更重要。
const char* toString(KinematicFailureReason reason)
{
    switch (reason) {
        case KinematicFailureReason::None:            return "None";
        case KinematicFailureReason::NoDevice:        return "NoDevice";
        case KinematicFailureReason::NoTcpFrame:      return "NoTcpFrame";
        case KinematicFailureReason::IkNoSolution:    return "IkNoSolution";
        case KinematicFailureReason::Collision:       return "Collision";
        case KinematicFailureReason::TargetResidual:  return "TargetResidual";
        case KinematicFailureReason::JointLimit:      return "JointLimit";
        case KinematicFailureReason::NearJointLimit:  return "NearJointLimit";
        case KinematicFailureReason::Singular:        return "Singular";
        case KinematicFailureReason::NearSingular:    return "NearSingular";
        case KinematicFailureReason::InvalidTarget:   return "InvalidTarget";
        case KinematicFailureReason::SolverError:     return "SolverError";
        case KinematicFailureReason::CollisionDetectorUnavailable:
            return "CollisionDetectorUnavailable";
        case KinematicFailureReason::FrameNotFound: return "FrameNotFound";
        default:                                      return "Unknown";
    }
}

// 把 AnalysisEvidenceStage 枚举映射成可读字符串(与 analysisEvidenceStageFromString
// 互逆)。字符串值用于 Report JSON / UI / CSV,属稳定契约,重命名需同步两边。
const char* toString(AnalysisEvidenceStage stage)
{
    switch (stage) {
        case AnalysisEvidenceStage::Estimated: return "Estimated";
        case AnalysisEvidenceStage::Quick:     return "Quick";
        case AnalysisEvidenceStage::Verified:  return "Verified";
        default:                               return "Unknown";
    }
}

// 把 Feasibility 枚举映射成可读字符串(与 feasibilityFromString 互逆)。
const char* toString(Feasibility feasibility)
{
    switch (feasibility) {
        case Feasibility::Feasible:         return "Feasible";
        case Feasibility::Infeasible:       return "Infeasible";
        case Feasibility::DataInsufficient: return "DataInsufficient";
        case Feasibility::NotEvaluated:     return "NotEvaluated";
        default:                            return "Unknown";
    }
}

// 把 Quality 枚举映射成可读字符串(与 qualityFromString 互逆)。
const char* toString(Quality quality)
{
    switch (quality) {
        case Quality::Good:     return "Good";
        case Quality::Degraded: return "Degraded";
        case Quality::Critical: return "Critical";
        case Quality::Unknown:  return "Unknown";
        default:                return "Unknown";
    }
}

// 字符串 -> 枚举的反向解析。所有 fromString 函数遵循同一约定:
//   - 成功:写回 value、清空 error、返回 true;
//   - 失败:保持 value 不变、写入 "Unknown <type>: <text>"、返回 false。
// 用于从 Report JSON / 外部配置恢复枚举状态。
bool analysisEvidenceStageFromString(const std::string& text,
                                     AnalysisEvidenceStage& value,
                                     std::string* error)
{
    if (text == "Estimated") value = AnalysisEvidenceStage::Estimated;
    else if (text == "Quick") value = AnalysisEvidenceStage::Quick;
    else if (text == "Verified") value = AnalysisEvidenceStage::Verified;
    else return conversionFailed ("AnalysisEvidenceStage", text, error);
    conversionSucceeded (error);
    return true;
}

// Feasibility 的反向解析(约定见 analysisEvidenceStageFromString)。
bool feasibilityFromString(const std::string& text, Feasibility& value, std::string* error)
{
    if (text == "Feasible") value = Feasibility::Feasible;
    else if (text == "Infeasible") value = Feasibility::Infeasible;
    else if (text == "DataInsufficient") value = Feasibility::DataInsufficient;
    else if (text == "NotEvaluated") value = Feasibility::NotEvaluated;
    else return conversionFailed ("Feasibility", text, error);
    conversionSucceeded (error);
    return true;
}

// Quality 的反向解析(约定见 analysisEvidenceStageFromString)。
bool qualityFromString(const std::string& text, Quality& value, std::string* error)
{
    if (text == "Good") value = Quality::Good;
    else if (text == "Degraded") value = Quality::Degraded;
    else if (text == "Critical") value = Quality::Critical;
    else if (text == "Unknown") value = Quality::Unknown;
    else return conversionFailed ("Quality", text, error);
    conversionSucceeded (error);
    return true;
}

// KinematicFailureReason 的反向解析(约定见 analysisEvidenceStageFromString)。
// 分支较多,但都是等长字符串比较;若将来枚举增加新值需同步这里。
bool kinematicFailureReasonFromString(const std::string& text,
                                      KinematicFailureReason& value,
                                      std::string* error)
{
    if (text == "None") value = KinematicFailureReason::None;
    else if (text == "NoDevice") value = KinematicFailureReason::NoDevice;
    else if (text == "NoTcpFrame") value = KinematicFailureReason::NoTcpFrame;
    else if (text == "IkNoSolution") value = KinematicFailureReason::IkNoSolution;
    else if (text == "Collision") value = KinematicFailureReason::Collision;
    else if (text == "TargetResidual") value = KinematicFailureReason::TargetResidual;
    else if (text == "JointLimit") value = KinematicFailureReason::JointLimit;
    else if (text == "NearJointLimit") value = KinematicFailureReason::NearJointLimit;
    else if (text == "Singular") value = KinematicFailureReason::Singular;
    else if (text == "NearSingular") value = KinematicFailureReason::NearSingular;
    else if (text == "InvalidTarget") value = KinematicFailureReason::InvalidTarget;
    else if (text == "SolverError") value = KinematicFailureReason::SolverError;
    else if (text == "CollisionDetectorUnavailable")
        value = KinematicFailureReason::CollisionDetectorUnavailable;
    else if (text == "FrameNotFound") value = KinematicFailureReason::FrameNotFound;
    else return conversionFailed ("KinematicFailureReason", text, error);
    conversionSucceeded (error);
    return true;
}

const char* toString(KinematicLengthUnit unit)
{
    switch (unit) {
        case KinematicLengthUnit::Meters:      return "Meters";
        case KinematicLengthUnit::Centimeters: return "Centimeters";
        case KinematicLengthUnit::Millimeters: return "Millimeters";
        case KinematicLengthUnit::Inches:      return "Inches";
        default:                               return "Meters";
    }
}

const char* toString(KinematicAngleUnit unit)
{
    switch (unit) {
        case KinematicAngleUnit::Degrees: return "Degrees";
        case KinematicAngleUnit::Radians: return "Radians";
        case KinematicAngleUnit::Grads:   return "Grads";
        case KinematicAngleUnit::Turns:   return "Turns";
        default:                          return "Degrees";
    }
}

// 长度单位的显示后缀("m"/"cm"/"mm"/"in"),与 toString 的完整名称对应,
// 供 SpinBox 等短标签使用。
const char* unitSuffix(KinematicLengthUnit unit)
{
    switch (unit) {
        case KinematicLengthUnit::Meters:      return "m";
        case KinematicLengthUnit::Centimeters: return "cm";
        case KinematicLengthUnit::Millimeters: return "mm";
        case KinematicLengthUnit::Inches:      return "in";
        default:                               return "m";
    }
}

// 角度单位的显示后缀("deg"/"rad"/"grad"/"turn")。
const char* unitSuffix(KinematicAngleUnit unit)
{
    switch (unit) {
        case KinematicAngleUnit::Degrees: return "deg";
        case KinematicAngleUnit::Radians: return "rad";
        case KinematicAngleUnit::Grads:   return "grad";
        case KinematicAngleUnit::Turns:   return "turn";
        default:                          return "deg";
    }
}

// 米 -> 显示单位,供 UI SpinBox 显示当前值使用。
double displayLengthFromMeters(double meters, KinematicLengthUnit unit)
{
    return meters * lengthDisplayPerMeter(unit);
}

// 显示单位 -> 米,UI 读取用户输入后换算回内部 SI 单位。
double metersFromDisplayLength(double displayValue, KinematicLengthUnit unit)
{
    return displayValue / lengthDisplayPerMeter(unit);
}

// 度 -> 显示单位(内部 RPY 以度存储,UI 按用户偏好单位显示)。
double displayAngleFromDegrees(double degrees, KinematicAngleUnit unit)
{
    return degrees * angleDisplayPerDegree(unit);
}

// 显示单位 -> 度(UI 输入换算回内部度单位)。
double degreesFromDisplayAngle(double displayValue, KinematicAngleUnit unit)
{
    return displayValue / angleDisplayPerDegree(unit);
}

}    // namespace rws
