#ifndef RWS_ROBOTMODELBUILDER_ROBOTMODELFINGERPRINT_HPP
#define RWS_ROBOTMODELBUILDER_ROBOTMODELFINGERPRINT_HPP

#include "RobotModelSpec.hpp"

#include <string>

namespace rws {

//! Stable SHA-256 identity for engineering-relevant RobotModelSpec content.
class RobotModelFingerprint
{
  public:
    static std::string canonicalSha256(const RobotModelSpec& spec);
};

} // namespace rws

#endif // RWS_ROBOTMODELBUILDER_ROBOTMODELFINGERPRINT_HPP
