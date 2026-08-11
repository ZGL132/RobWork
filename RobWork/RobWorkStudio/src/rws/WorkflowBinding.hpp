#ifndef RWS_WORKFLOWBINDING_HPP
#define RWS_WORKFLOWBINDING_HPP

#include <QString>

namespace rws {

struct WorkflowBinding
{
    static constexpr int SchemaVersion = 1;

    QString projectId;
    QString targetDevice;
    QString tcpFrame;
    QString sceneResourceId;
    QString modelResourceId;
    QString sourceKind;
    QString sourceFingerprint;
    int schemaVersion = SchemaVersion;

    bool isValid (QString* error = nullptr) const;
    bool write (const QString& projectRoot, QString* error = nullptr) const;
    static bool read (const QString& projectRoot, WorkflowBinding& binding,
                      QString* error = nullptr);
};

}    // namespace rws

#endif    // RWS_WORKFLOWBINDING_HPP
