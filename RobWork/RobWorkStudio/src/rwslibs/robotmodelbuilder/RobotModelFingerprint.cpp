#include "RobotModelFingerprint.hpp"

#include "RobotModelSpecJson.hpp"

#include <QCryptographicHash>

namespace rws {

std::string RobotModelFingerprint::canonicalSha256(const RobotModelSpec& spec)
{
    RobotModelSpec canonical = spec;
    canonical.saveDirectory.clear();
    const QByteArray data = QByteArray::fromStdString(RobotModelSpecJson::toJson(canonical));
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().toStdString();
}

} // namespace rws
