#include "KinematicAnalysisTypes.hpp"

namespace rws {

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
        case KinematicFailureReason::JointLimit:      return "JointLimit";
        case KinematicFailureReason::NearJointLimit:  return "NearJointLimit";
        case KinematicFailureReason::Singular:        return "Singular";
        case KinematicFailureReason::NearSingular:    return "NearSingular";
        case KinematicFailureReason::InvalidTarget:   return "InvalidTarget";
        case KinematicFailureReason::SolverError:     return "SolverError";
        default:                                      return "Unknown";
    }
}

}    // namespace rws