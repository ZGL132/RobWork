#include "KinematicAnalysisTypes.hpp"

namespace rws {

namespace {
constexpr double Pi = 3.141592653589793238462643383279502884;

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
        default:                                      return "Unknown";
    }
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

double displayLengthFromMeters(double meters, KinematicLengthUnit unit)
{
    return meters * lengthDisplayPerMeter(unit);
}

double metersFromDisplayLength(double displayValue, KinematicLengthUnit unit)
{
    return displayValue / lengthDisplayPerMeter(unit);
}

double displayAngleFromDegrees(double degrees, KinematicAngleUnit unit)
{
    return degrees * angleDisplayPerDegree(unit);
}

double degreesFromDisplayAngle(double displayValue, KinematicAngleUnit unit)
{
    return displayValue / angleDisplayPerDegree(unit);
}

}    // namespace rws
