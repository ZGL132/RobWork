// =============================================================================
//  文件: RobotModelBuilderWidget.cpp
//  说明: RobotModelBuilder 插件 UI 的实现。整体布局:
//        顶部表单:机器人名、保存目录、模式(Joint+RPY+Pos / DH Projection)、选项开关;
//        中部 QTabWidget 包含 5 个标签页:
//           Kinematics  - SE(3) Joint+RPY+Pos 真值表(可编辑) +
//                        DH 投影视图表(只读,带 Status 列)
//           Drawables   - 可视化几何表
//           Limits      - 关节限位表
//           Poses       - 预设位姿表(可增删)
//           Dynamics    - 动力学参数(链接质量/惯量/材料 + 力限)
//           XML Preview - 三段 XML 实时预览
//        底部按钮:Generate Preview / Save XML / Save and Load / Reset
// =============================================================================
#include "RobotModelBuilderWidget.hpp"

#include "RobotModelPublishService.hpp"
#include "RobotModelProjectPaths.hpp"
#include "RobotModelXmlWriter.hpp"
#include "RobotModelUrdfImporter.hpp"
#include "RobotModelSpecJson.hpp"

// Qt 控件/工具头文件
#include <QCheckBox>
#include <QComboBox>
#include <QByteArrayView>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QIODevice>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QStringDecoder>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <set>

using namespace rws;

namespace {
// -----------------------------------------------------------------------------
//  匿名命名空间:本文件内部的常量与工具函数,不对外暴露
// -----------------------------------------------------------------------------

/// 创建 Joint limit 表的默认单位中立列(由 fillLimitsTable 与 collectSpec 一致使用)
const QStringList kLimitsHeaders = QStringList ()
    << "Joint"
    << "PosMin"
    << "PosMax"
    << "VelMax"
    << "AccMax";

// 快照编解码的安全上限：整个快照/解码变量区最大 64MB、单字段最大 16MB，
// 防止异常或恶意输入耗尽内存。
const qsizetype kMaxSnapshotBytes = 64 * 1024 * 1024;
const qsizetype kMaxSnapshotFieldBytes = 16 * 1024 * 1024;
const qint64 kMaxDecodedVariableBytes = 64 * 1024 * 1024;

// 二进制快照写入器：把 Widget 的编辑控件状态（文本、启用/只读/隐藏、表格内容等）
// 编码为紧凑二进制流。所有多字节整数均大端序；写越界/超限时置 ok=false 并使快照无效。
class SnapshotWriter
{
  public:
    explicit SnapshotWriter (QByteArray& data) : _data (data) { _data.clear (); }

    bool ok () const { return _ok; }
    void writeU8 (quint8 value) { _data.append (char (value)); }
    void writeBool (bool value) { writeU8 (value ? 1 : 0); }
    void writeU32 (quint32 value)
    {
        writeU8 (quint8 ((value >> 24) & 0xff));
        writeU8 (quint8 ((value >> 16) & 0xff));
        writeU8 (quint8 ((value >> 8) & 0xff));
        writeU8 (quint8 (value & 0xff));
    }
    void writeI32 (qint32 value) { writeU32 (quint32 (value)); }
    void writeU64 (quint64 value)
    {
        writeU32 (quint32 (value >> 32));
        writeU32 (quint32 (value & 0xffffffff));
    }
    void writeI64 (qint64 value) { writeU64 (quint64 (value)); }
    void writeDouble (double value)
    {
        quint64 bits = 0;
        static_assert (sizeof (bits) == sizeof (value), "Unexpected double size");
        std::memcpy (&bits, &value, sizeof (bits));
        writeU64 (bits);
    }
    void writeBytes (QByteArrayView value)
    {
        if (!_ok || value.size () > kMaxSnapshotFieldBytes ||
            _variableBytes + value.size () > kMaxDecodedVariableBytes ||
            value.size () > qsizetype (std::numeric_limits< quint32 >::max ())) {
            _ok = false;
            return;
        }
        _variableBytes += value.size ();
        writeU32 (quint32 (value.size ()));
        _data.append (value.data (), value.size ());
    }
    void writeString (const QString& value) { writeBytes (value.toUtf8 ()); }
    void writeVariant (const QVariant& value)
    {
        if (!value.isValid ()) {
            writeU8 (0);
            return;
        }
        switch (value.typeId ()) {
            case QMetaType::QString:
                writeU8 (1);
                writeString (value.toString ());
                break;
            case QMetaType::Int:
                writeU8 (2);
                writeI64 (value.toInt ());
                break;
            case QMetaType::UInt:
                writeU8 (3);
                writeU64 (value.toUInt ());
                break;
            case QMetaType::LongLong:
                writeU8 (4);
                writeI64 (value.toLongLong ());
                break;
            case QMetaType::ULongLong:
                writeU8 (5);
                writeU64 (value.toULongLong ());
                break;
            case QMetaType::Bool:
                writeU8 (6);
                writeBool (value.toBool ());
                break;
            case QMetaType::Nullptr:
                writeU8 (7);
                break;
            case QMetaType::Double:
                writeU8 (8);
                writeDouble (value.toDouble ());
                break;
            default:
                _ok = false;
                break;
        }
    }

  private:
    QByteArray& _data;
    qint64 _variableBytes = 0;
    bool _ok = true;
};

// 二进制快照读取器：带严格边界/长度校验的反序列化器。每次读取都检查剩余字节、
// 字段长度与累计变量字节上限，任何越界或格式非法都置 ok=false 并中止后续读取，
// 从而拒绝损坏或恶意构造的快照。
class SnapshotReader
{
  public:
    SnapshotReader (QByteArrayView data, qint64& variableBytes) :
        _data (data), _variableBytes (variableBytes)
    {}

    bool atEnd () const { return _ok && _offset == _data.size (); }
    bool ok () const { return _ok; }
    bool readU8 (quint8& value)
    {
        if (!_ok || _offset >= _data.size ()) {
            _ok = false;
            return false;
        }
        value = quint8 (uchar (_data[_offset++]));
        return true;
    }
    bool readBool (bool& value)
    {
        quint8 encoded = 0;
        if (!readU8 (encoded) || encoded > 1) {
            _ok = false;
            return false;
        }
        value = encoded != 0;
        return true;
    }
    bool readU32 (quint32& value)
    {
        quint8 bytes[4] = {};
        for (quint8& byte : bytes) {
            if (!readU8 (byte))
                return false;
        }
        value = (quint32 (bytes[0]) << 24) | (quint32 (bytes[1]) << 16) |
                (quint32 (bytes[2]) << 8) | quint32 (bytes[3]);
        return true;
    }
    bool readI32 (qint32& value)
    {
        quint32 encoded = 0;
        if (!readU32 (encoded))
            return false;
        value = qint32 (encoded);
        return true;
    }
    bool readInt (int& value)
    {
        qint32 encoded = 0;
        if (!readI32 (encoded))
            return false;
        value = int (encoded);
        return true;
    }
    bool readU64 (quint64& value)
    {
        quint32 high = 0;
        quint32 low = 0;
        if (!readU32 (high) || !readU32 (low))
            return false;
        value = (quint64 (high) << 32) | low;
        return true;
    }
    bool readI64 (qint64& value)
    {
        quint64 encoded = 0;
        if (!readU64 (encoded))
            return false;
        value = qint64 (encoded);
        return true;
    }
    bool readDouble (double& value)
    {
        quint64 bits = 0;
        if (!readU64 (bits))
            return false;
        std::memcpy (&value, &bits, sizeof (value));
        return true;
    }
    bool readBytes (QByteArrayView& value, qsizetype maxBytes = kMaxSnapshotFieldBytes,
                    bool chargeBudget = true)
    {
        quint32 size = 0;
        if (!readU32 (size) || size > quint32 (maxBytes) ||
            qint64 (size) > qint64 (_data.size () - _offset) ||
            (chargeBudget && _variableBytes + size > kMaxDecodedVariableBytes)) {
            _ok = false;
            return false;
        }
        if (chargeBudget)
            _variableBytes += size;
        value = QByteArrayView (_data.data () + _offset, qsizetype (size));
        _offset += size;
        return true;
    }
    bool readString (QString& value)
    {
        QByteArrayView bytes;
        if (!readBytes (bytes))
            return false;
        QStringDecoder decoder (QStringDecoder::Utf8);
        value = decoder.decode (bytes);
        if (decoder.hasError ()) {
            _ok = false;
            return false;
        }
        return true;
    }
    bool readVariant (QVariant& value)
    {
        quint8 tag = 0;
        if (!readU8 (tag))
            return false;
        qint64 signedValue = 0;
        quint64 unsignedValue = 0;
        switch (tag) {
            case 0:
                value = QVariant ();
                return true;
            case 1: {
                QString stringValue;
                if (!readString (stringValue))
                    return false;
                value = stringValue;
                return true;
            }
            case 2:
                if (!readI64 (signedValue) || signedValue < std::numeric_limits< int >::min () ||
                    signedValue > std::numeric_limits< int >::max ())
                    break;
                value = int (signedValue);
                return true;
            case 3:
                if (!readU64 (unsignedValue) || unsignedValue > std::numeric_limits< uint >::max ())
                    break;
                value = uint (unsignedValue);
                return true;
            case 4:
                if (!readI64 (signedValue))
                    return false;
                value = qlonglong (signedValue);
                return true;
            case 5:
                if (!readU64 (unsignedValue))
                    return false;
                value = qulonglong (unsignedValue);
                return true;
            case 6: {
                bool boolValue = false;
                if (!readBool (boolValue))
                    return false;
                value = boolValue;
                return true;
            }
            case 7:
                value = QVariant::fromValue (nullptr);
                return true;
            case 8: {
                double doubleValue = 0.0;
                if (!readDouble (doubleValue))
                    return false;
                value = doubleValue;
                return true;
            }
            default:
                break;
        }
        _ok = false;
        return false;
    }

  private:
    QByteArrayView _data;
    qint64& _variableBytes;
    qsizetype _offset = 0;
    bool _ok = true;
};

/// 创建一个带表头、隔行着色、列宽自适应的 QTableWidget
QTableWidget* makeTable (const QStringList& headers, int rows)
{
    QTableWidget* table = new QTableWidget (rows, headers.size ());
    table->setHorizontalHeaderLabels (headers);
    table->horizontalHeader ()->setSectionResizeMode (QHeaderView::Stretch);
    table->verticalHeader ()->setVisible (false);
    table->setAlternatingRowColors (true);
    return table;
}

/// 判断一段文本能否解析为 double
bool parseDouble (const QString& text)
{
    bool ok = false;
    text.trimmed ().toDouble (&ok);
    return ok;
}

/// 判断一段以空白分隔的文本能否解析为 expected 个 double
bool parseVector (const QString& text, int expected)
{
    const QStringList parts = text.split (QRegularExpression ("\\s+"), Qt::SkipEmptyParts);
    if (parts.size () != expected)
        return false;
    for (const QString& part : parts) {
        if (!parseDouble (part))
            return false;
    }
    return true;
}

/// 是否 Revolute 关节(大小写不敏感,trim 后比较)
bool isRevoluteType (const std::string& type)
{
    return QString::fromStdString (type).trimmed ().compare ("Revolute", Qt::CaseInsensitive) == 0;
}

bool isMovableType (const std::string& type)
{
    const QString t = QString::fromStdString (type).trimmed ();
    return t.compare ("Revolute", Qt::CaseInsensitive) == 0 ||
           t.compare ("Prismatic", Qt::CaseInsensitive) == 0;
}

std::vector< std::string > movableNames (const std::vector< JointTransformSpec >& joints)
{
    std::vector< std::string > names;
    for (const JointTransformSpec& joint : joints) {
        if (isMovableType (joint.type))
            names.push_back (joint.name);
    }
    return names;
}

void appendUniqueChoice (QStringList& choices, const QString& value)
{
    if (!value.isEmpty () && !choices.contains (value, Qt::CaseSensitive))
        choices << value;
}

QStringList movableJointChoices (const RobotModelSpec& spec)
{
    QStringList choices;
    for (const JointTransformSpec& joint : spec.transformJoints) {
        if (isMovableType (joint.type))
            appendUniqueChoice (choices, QString::fromStdString (joint.name));
    }
    return choices;
}

QStringList deviceFrameChoices (const RobotModelSpec& spec)
{
    QStringList choices;
    appendUniqueChoice (choices, "Base");
    appendUniqueChoice (choices, QString::fromStdString (spec.dynamics.baseFrame));
    for (const JointTransformSpec& joint : spec.transformJoints)
        appendUniqueChoice (choices, QString::fromStdString (joint.name));
    appendUniqueChoice (choices, "TCP");
    return choices;
}

QStringList collisionFrameChoices (const RobotModelSpec& spec)
{
    QStringList choices = deviceFrameChoices (spec);
    appendUniqueChoice (choices, "RobotBase");
    if (spec.generateScene) {
        appendUniqueChoice (choices, "WORLD");
        for (const FrameSpec& frame : spec.sceneFrames)
            appendUniqueChoice (choices, QString::fromStdString (frame.name));
    }
    return choices;
}

QStringList sceneFrameChoices (const RobotModelSpec& spec, int row)
{
    QStringList choices;
    appendUniqueChoice (choices, "WORLD");
    appendUniqueChoice (choices, "RobotBase");
    for (int index = 0; index < row && index < static_cast< int >(spec.sceneFrames.size ());
         ++index)
        appendUniqueChoice (choices, QString::fromStdString (spec.sceneFrames[index].name));
    return choices;
}

QStringList sceneGeometryFrameChoices (const RobotModelSpec& spec)
{
    QStringList choices;
    appendUniqueChoice (choices, "WORLD");
    appendUniqueChoice (choices, "RobotBase");
    for (const FrameSpec& frame : spec.sceneFrames)
        appendUniqueChoice (choices, QString::fromStdString (frame.name));
    return choices;
}

int movableIndexBeforeRow (const std::vector< JointTransformSpec >& joints, int row)
{
    int index = 0;
    for (int i = 0; i < row && i < static_cast< int > (joints.size ()); ++i) {
        if (isMovableType (joints[static_cast< size_t > (i)].type))
            ++index;
    }
    return index;
}

JointLimitSpec defaultLimit (const std::string& jointName, const std::string& type)
{
    JointLimitSpec limit;
    limit.jointName = jointName;
    if (QString::fromStdString (type).trimmed ().compare ("Prismatic", Qt::CaseInsensitive) == 0) {
        limit.posMin = -1.0;
        limit.posMax = 1.0;
        limit.velMax = 0.5;
        limit.accMax = 1.0;
    }
    else {
        limit.posMin = -180.0;
        limit.posMax = 180.0;
        limit.velMax = 180.0;
        limit.accMax = 360.0;
    }
    return limit;
}

JointForceLimitSpec defaultForceLimit (const std::string& jointName)
{
    JointForceLimitSpec force;
    force.jointName = jointName;
    force.maxForce  = 100.0;
    return force;
}

LinkDynamicsSpec defaultLinkDynamics (const std::string& jointName, int index)
{
    LinkDynamicsSpec link;
    link.linkName        = "Link" + std::to_string (index + 1);
    link.objectName      = jointName;
    link.mass            = 1.0;
    link.cog             = {{0, 0, 0}};
    link.inertia         = {{0.01, 0.01, 0.01, 0, 0, 0}};
    link.estimateInertia = false;
    link.material        = "Aluminum";
    return link;
}

std::map< std::string, double > poseMapForMovables (
    const PoseSpec& pose, const std::vector< std::string >& oldMovableNames)
{
    std::map< std::string, double > values;
    const size_t n = std::min (pose.q.size (), oldMovableNames.size ());
    for (size_t i = 0; i < n; ++i)
        values[oldMovableNames[i]] = pose.q[i];
    return values;
}

void synchronizeJointDerivedData (RobotModelSpec& spec,
                                  const RobotModelSpec& oldSpec,
                                  const std::vector< std::string >& oldMovableNames,
                                  const std::set< std::string >& removedNames)
{
    std::map< std::string, JointLimitSpec > oldLimits;
    for (const JointLimitSpec& limit : oldSpec.limits)
        oldLimits[limit.jointName] = limit;

    std::map< std::string, JointForceLimitSpec > oldForces;
    for (const JointForceLimitSpec& force : oldSpec.dynamics.forceLimits)
        oldForces[force.jointName] = force;

    std::map< std::string, LinkDynamicsSpec > oldLinks;
    for (const LinkDynamicsSpec& link : oldSpec.dynamics.links)
        oldLinks[link.objectName] = link;

    spec.limits.clear ();
    spec.dynamics.forceLimits.clear ();
    spec.dynamics.links.clear ();
    int movableIndex = 0;
    for (const JointTransformSpec& joint : spec.transformJoints) {
        if (!isMovableType (joint.type))
            continue;
        std::map< std::string, JointLimitSpec >::const_iterator limitIt =
            oldLimits.find (joint.name);
        spec.limits.push_back (limitIt != oldLimits.end () ?
                               limitIt->second : defaultLimit (joint.name, joint.type));

        std::map< std::string, JointForceLimitSpec >::const_iterator forceIt =
            oldForces.find (joint.name);
        spec.dynamics.forceLimits.push_back (forceIt != oldForces.end () ?
                                             forceIt->second : defaultForceLimit (joint.name));

        std::map< std::string, LinkDynamicsSpec >::const_iterator linkIt =
            oldLinks.find (joint.name);
        spec.dynamics.links.push_back (linkIt != oldLinks.end () ?
                                       linkIt->second : defaultLinkDynamics (joint.name, movableIndex));
        ++movableIndex;
    }

    const std::vector< std::string > newMovableNames = movableNames (spec.transformJoints);
    const size_t poseCount = std::min (spec.poses.size (), oldSpec.poses.size ());
    for (size_t i = 0; i < poseCount; ++i) {
        const std::map< std::string, double > oldQ =
            poseMapForMovables (oldSpec.poses[i], oldMovableNames);
        spec.poses[i].q.clear ();
        for (const std::string& jointName : newMovableNames) {
            std::map< std::string, double >::const_iterator it = oldQ.find (jointName);
            spec.poses[i].q.push_back (it != oldQ.end () ? it->second : 0.0);
        }
    }

    spec.drawables.erase (
        std::remove_if (spec.drawables.begin (), spec.drawables.end (),
                        [&] (const DrawableSpec& d) {
                            return removedNames.find (d.refFrame) != removedNames.end ();
                        }),
        spec.drawables.end ());
    RobotModelXmlWriter::applyDefaultDrawables (spec);

    // Milestone 5:同步删除引用被删 frame 的碰撞模型,避免 dangling refFrame。
    spec.collisionModels.erase (
        std::remove_if (spec.collisionModels.begin (), spec.collisionModels.end (),
                        [&] (const CollisionModelSpec& c) {
                            return removedNames.find (c.refFrame) != removedNames.end ();
                        }),
        spec.collisionModels.end ());
}

/// 在 spec.transformJoints 中找一个还没用过的 "Joint{N}" 名(N 从 1 起递增),
/// 用于 Add Joint 按钮保证名不冲突。
QString pickNextJointName (const RobotModelSpec& spec)
{
    std::set< std::string > used;
    for (const JointTransformSpec& j : spec.transformJoints)
        used.insert (j.name);
    for (int i = 1; i < 10000; ++i) {
        const std::string candidate = "Joint" + std::to_string (i);
        if (used.find (candidate) == used.end ())
            return QString::fromStdString (candidate);
    }
    return QStringLiteral ("Joint_New");
}
}    // namespace

// =============================================================================
//  构造函数
//  说明: 创建 UI + 填入默认值
// =============================================================================
RobotModelBuilderWidget::RobotModelBuilderWidget (QWidget* parent) : QWidget (parent)
{
    buildUi ();
    resetDefaults ();

    // 所有编辑控件均在 buildUi 中一次性创建。统一安装事件过滤器可以覆盖表格、输入框、
    // 下拉框和复选框，而是否真的变脏仍由序列化快照比较决定，避免复制大量槽函数。
    const QList< QObject* > children = findChildren< QObject* > ();
    for (QObject* child : children)
        child->installEventFilter (this);
}

// 项目 Provider 加载回调：读取并解析模型 JSON，全部成功后替换可视模型并建立
// 干净基线快照；失败时保留用户当前编辑的模型不变。
bool RobotModelBuilderWidget::loadProjectDocument (const QString& path, QString* error)
{
    QFile file (path);
    if (!file.open (QIODevice::ReadOnly)) {
        if (error != nullptr)
            *error = QString::fromUtf8 ("无法读取机器人模型：%1。").arg (file.errorString ());
        return false;
    }

    RobotModelSpec parsed;
    std::string parseError;
    if (!RobotModelSpecJson::fromJson (file.readAll ().toStdString (), parsed, &parseError)) {
        if (error != nullptr)
            *error = QString::fromUtf8 ("机器人模型 JSON 无效：%1。")
                         .arg (QString::fromStdString (parseError));
        return false;
    }

    // 解析和 Schema 校验全部成功后才替换可视模型。项目打开失败时，用户当前正在编辑的
    // 临时模型不会被半截 JSON 覆盖。
    // 兼容旧 .rmb.json 中的 saveDirectory 字段，但不信任其值。项目 Provider 已先注入当前
    // 项目目录，因此载入后立即覆盖为受管目录，防止旧工程路径在新项目中复活。
    RobotModelSpec runtime = parsed;
    if (!_projectDirectory.isEmpty () &&
        !RobotModelProjectPaths::resolveManaged (parsed, _projectDirectory, runtime, error))
        return false;

    runtime.saveDirectory = effectiveSaveDirectory ().toStdString ();
    fillFromSpec (runtime);
    _projectCleanSnapshot = projectDocumentSnapshot ();
    _projectSnapshotActive = true;
    return true;
}

// 项目 Provider 保存回调：把当前模型快照写入保存事务分配的暂存路径，
// 不直接覆盖正式项目资源；正式替换由 ProjectSaveTransaction 完成。
bool RobotModelBuilderWidget::saveProjectDocument (const QString& targetPath, QString* error) const
{
    QByteArray json;
    if (!serializeProjectDocument (json, error))
        return false;

    QFile file (targetPath);
    if (!file.open (QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error != nullptr)
            *error = QString::fromUtf8 ("无法写入机器人模型暂存文件：%1。").arg (
                file.errorString ());
        return false;
    }

    if (file.write (json) != json.size ()) {
        if (error != nullptr)
            *error = QString::fromUtf8 ("写入机器人模型暂存文件失败：%1。").arg (
                file.errorString ());
        return false;
    }
    return true;
}

void RobotModelBuilderWidget::beginGeneratedProjectDocument ()
{
    // 新资源没有可加载的历史 JSON。空基线让当前由 WorkCell 同步得到的模型成为未保存变更，
    // 由项目保存事务首次写入 generated/robot-models，而不是在同步时直接落盘。
    _projectCleanSnapshot.clear ();
    _projectSnapshotActive = true;
}

// 脏判定：比较当前模型快照与干净基线；未加载过项目资源时一律视为干净。
bool RobotModelBuilderWidget::isProjectDocumentDirty () const
{
    return _projectSnapshotActive && projectDocumentSnapshot () != _projectCleanSnapshot;
}

void RobotModelBuilderWidget::markProjectDocumentClean ()
{
    // 该方法只由 Provider 的 markClean 回调触发，而 markClean 仅发生在全部项目资源
    // 提交成功之后。若其它资源保存失败，本 Widget 的快照仍保持旧版本并继续显示脏状态。
    _projectCleanSnapshot = projectDocumentSnapshot ();
    _projectSnapshotActive = true;
}

void RobotModelBuilderWidget::clearProjectDocumentContext ()
{
    // 项目资源关闭回调执行完整重置:清空托管输出目录、丢弃已导入的模型文档说明
    // 与上次 URDF 导入的告警,并恢复默认机器人模型草稿,确保新工程不继承旧项目上下文。
    setProjectOutputDirectory (QString ());
    _importedDocument = ImportedDocumentSpec ();
    _lastUrdfImportWarnings.clear ();
    applyDefaultProjectModel ();
    // 作废干净基线并关闭快照追踪:新项目尚未打开时,脏判定一律视为"干净"。
    _projectCleanSnapshot.clear ();
    _projectSnapshotActive = false;
    setStatus ("No project open.");
}

// 生成当前 Widget 完整状态的二进制快照：模型 JSON + 全部编辑控件（行编辑/复选框/
// 表格）的文本、启用态与内容。快照用于"从零构建"流程的撤销/重做与项目文档基线；
// 任何字段超限都会置失败并返回 false，避免保存损坏的快照。
bool RobotModelBuilderWidget::snapshotProjectDocumentState (QByteArray& snapshot,
                                                            QString* error) const
{
    if (error != nullptr)
        error->clear ();

    const QByteArray model = QByteArray::fromStdString (RobotModelSpecJson::toJson (collectSpec ()));
    const QList< QLineEdit* > lineEdits = {_robotName,
                                           _deviceFile,
                                           _sceneFile,
                                           _dynamicWorkCellFile,
                                           _collisionSetupFile,
                                           _baseFrame,
                                           _baseMaterial,
                                           _robotBaseRpy,
                                           _robotBasePos,
                                           _status};
    const QList< QCheckBox* > checkBoxes = {_preserveImportedFileLayout,
                                            _showFrameAxes,
                                            _generateDrawables,
                                            _generateScene,
                                            _generateDwc,
                                            _exportDhAdvanced,
                                            _collisionSetupEnabled,
                                            _excludeBaseFirst,
                                            _excludeAdjacent,
                                            _excludeStatic};
    const QList< QTableWidget* > tables = {_transformTable,
                                           _dhTable,
                                           _drawablesTable,
                                           _collisionModelsTable,
                                           _collisionSetupPairsTable,
                                           _sceneFramesTable,
                                           _sceneGeometryTable,
                                           _limitsTable,
                                           _posesTable,
                                           _dynamicsLinksTable,
                                           _forceLimitsTable};

    QByteArray editableState;
    SnapshotWriter ui (editableState);
    ui.writeU32 (quint32 (lineEdits.size ()));
    for (const QLineEdit* lineEdit : lineEdits) {
        ui.writeString (lineEdit->text ());
        ui.writeBool (lineEdit->isEnabled ());
        ui.writeBool (lineEdit->isReadOnly ());
        ui.writeBool (lineEdit->isHidden ());
    }
    ui.writeU32 (quint32 (_mode->count ()));
    for (int index = 0; index < _mode->count (); ++index)
        ui.writeString (_mode->itemText (index));
    ui.writeU32 (quint32 (_mode->count ()));
    for (int index = 0; index < _mode->count (); ++index)
        ui.writeVariant (_mode->itemData (index));
    ui.writeI32 (_mode->currentIndex ());
    ui.writeString (_mode->currentText ());
    ui.writeBool (_mode->isEditable ());
    ui.writeBool (_mode->isEnabled ());

    ui.writeU32 (quint32 (checkBoxes.size ()));
    for (const QCheckBox* checkBox : checkBoxes) {
        ui.writeI32 (qint32 (checkBox->checkState ()));
        ui.writeBool (checkBox->isTristate ());
        ui.writeBool (checkBox->isEnabled ());
        ui.writeBool (checkBox->isHidden ());
    }

    ui.writeU32 (quint32 (tables.size ()));
    for (const QTableWidget* table : tables) {
        ui.writeI32 (table->rowCount ());
        ui.writeI32 (table->columnCount ());
        ui.writeI32 (table->currentRow ());
        ui.writeI32 (table->currentColumn ());
        ui.writeBool (table->isEnabled ());
        ui.writeBool (table->isHidden ());
        for (int column = 0; column < table->columnCount (); ++column) {
            const QTableWidgetItem* header = table->horizontalHeaderItem (column);
            ui.writeString (header == NULL ? QString () : header->text ());
        }
        for (int row = 0; row < table->rowCount (); ++row) {
            for (int column = 0; column < table->columnCount (); ++column) {
                const QTableWidgetItem* item = table->item (row, column);
                ui.writeBool (item != NULL);
                if (item != NULL) {
                    ui.writeString (item->text ());
                    ui.writeU32 (quint32 (item->flags ()));
                    ui.writeI32 (qint32 (item->checkState ()));
                    ui.writeVariant (item->data (Qt::UserRole));
                }
                if (const QComboBox* combo =
                        qobject_cast< const QComboBox* > (table->cellWidget (row, column))) {
                    ui.writeU8 (1);
                    ui.writeU32 (quint32 (combo->count ()));
                    for (int index = 0; index < combo->count (); ++index)
                        ui.writeString (combo->itemText (index));
                    ui.writeU32 (quint32 (combo->count ()));
                    for (int index = 0; index < combo->count (); ++index)
                        ui.writeVariant (combo->itemData (index));
                    ui.writeI32 (combo->currentIndex ());
                    ui.writeString (combo->currentText ());
                    ui.writeBool (combo->isEditable ());
                    ui.writeBool (combo->isEnabled ());
                }
                else if (const QCheckBox* checkBox =
                             qobject_cast< const QCheckBox* > (table->cellWidget (row, column))) {
                    ui.writeU8 (2);
                    ui.writeI32 (qint32 (checkBox->checkState ()));
                    ui.writeBool (checkBox->isTristate ());
                    ui.writeBool (checkBox->isEnabled ());
                }
                else {
                    ui.writeU8 (0);
                }
            }
        }
    }
    ui.writeBool (_sceneContent->isEnabled ());
    if (!ui.ok ()) {
        if (error != nullptr)
            *error = QStringLiteral ("Could not encode the RobotModelBuilder editable state.");
        return false;
    }

    snapshot.clear ();
    QByteArray mainTabsEnabled;
    for (int index = 0; index < _mainTabs->count (); ++index)
        mainTabsEnabled.append (_mainTabs->isTabEnabled (index) ? '\1' : '\0');
    QByteArray previewTabsEnabled;
    for (int index = 0; index < _previewTabs->count (); ++index)
        previewTabsEnabled.append (_previewTabs->isTabEnabled (index) ? '\1' : '\0');
    SnapshotWriter stream (snapshot);
    stream.writeU32 (quint32 (0x524d4253));
    stream.writeU32 (quint32 (3));
    stream.writeString (_projectDirectory);
    stream.writeString (_projectOutputDirectory);
    stream.writeBytes (_projectCleanSnapshot);
    stream.writeBool (_projectSnapshotActive);
    stream.writeBytes (model);
    stream.writeBytes (editableState);
    stream.writeU32 (quint32 (_lastUrdfImportWarnings.size ()));
    for (const QString& warning : _lastUrdfImportWarnings)
        stream.writeString (warning);
    stream.writeString (_status->text ());
    stream.writeString (_serialPreview->toPlainText ());
    stream.writeString (_scenePreview->toPlainText ());
    stream.writeString (_dwcPreview->toPlainText ());
    stream.writeString (_collisionSetupPreview->toPlainText ());
    stream.writeString (_proximitySetupPreview->toPlainText ());
    stream.writeBytes (mainTabsEnabled);
    stream.writeBytes (previewTabsEnabled);
    stream.writeI32 (_mainTabs->currentIndex ());
    stream.writeI32 (_previewTabs->currentIndex ());
    if (!stream.ok () || snapshot.size () > kMaxSnapshotBytes) {
        if (error != nullptr)
            *error = QStringLiteral ("Could not encode the RobotModelBuilder document snapshot.");
        return false;
    }
    return true;
}

// 从二进制快照恢复 Widget 状态（snapshotProjectDocumentState 的逆操作）。带严格边界
// 校验，损坏/非法快照会整体拒绝并返回 false，不会把半解析的数据写入界面。
bool RobotModelBuilderWidget::restoreProjectDocumentState (const QByteArray& snapshot,
                                                           QString* error)
{
    if (error != nullptr)
        error->clear ();

    QString projectDirectory;
    QString projectOutputDirectory;
    QByteArray cleanSnapshot;
    bool snapshotActive = false;
    QByteArrayView model;
    QByteArrayView editableState;
    quint32 magic = 0;
    quint32 version = 0;
    QStringList importWarnings;
    QString status;
    QString serialPreview;
    QString scenePreview;
    QString dwcPreview;
    QString collisionPreview;
    QString proximityPreview;
    QByteArray mainTabsEnabled;
    QByteArray previewTabsEnabled;
    qint32 mainTabIndex = 0;
    qint32 previewTabIndex = 0;
    const int maxStringListItems = 10000;
    if (snapshot.size () > kMaxSnapshotBytes) {
        if (error != nullptr)
            *error = QStringLiteral ("The RobotModelBuilder document snapshot is too large.");
        return false;
    }

    qint64 decodedVariableBytes = 0;
    SnapshotReader stream (QByteArrayView (snapshot), decodedVariableBytes);
    QByteArrayView cleanSnapshotView;
    if (!stream.readU32 (magic) || !stream.readU32 (version) ||
        magic != quint32 (0x524d4253) || version != 3 ||
        !stream.readString (projectDirectory) ||
        !stream.readString (projectOutputDirectory) ||
        !stream.readBytes (cleanSnapshotView) || !stream.readBool (snapshotActive) ||
        !stream.readBytes (model) || !stream.readBytes (editableState, kMaxSnapshotFieldBytes, false)) {
        if (error != nullptr)
            *error = QStringLiteral ("The RobotModelBuilder document snapshot is invalid.");
        return false;
    }
    cleanSnapshot = QByteArray (cleanSnapshotView.data (), cleanSnapshotView.size ());
    quint32 importWarningCount = 0;
    if (!stream.readU32 (importWarningCount) || importWarningCount > quint32 (maxStringListItems)) {
        if (error != nullptr)
            *error = QStringLiteral ("The RobotModelBuilder document snapshot is invalid.");
        return false;
    }
    importWarnings.reserve (int (importWarningCount));
    for (quint32 index = 0; index < importWarningCount; ++index) {
        QString warning;
        if (!stream.readString (warning)) {
            if (error != nullptr)
                *error = QStringLiteral ("The RobotModelBuilder document snapshot is invalid.");
            return false;
        }
        importWarnings.push_back (warning);
    }
    QByteArrayView mainTabsEnabledView;
    QByteArrayView previewTabsEnabledView;
    if (!stream.readString (status) || !stream.readString (serialPreview) ||
        !stream.readString (scenePreview) || !stream.readString (dwcPreview) ||
        !stream.readString (collisionPreview) || !stream.readString (proximityPreview) ||
        !stream.readBytes (mainTabsEnabledView) || !stream.readBytes (previewTabsEnabledView) ||
        !stream.readI32 (mainTabIndex) || !stream.readI32 (previewTabIndex) || !stream.atEnd ()) {
        if (error != nullptr)
            *error = QStringLiteral ("The RobotModelBuilder document snapshot is invalid.");
        return false;
    }
    mainTabsEnabled = QByteArray (mainTabsEnabledView.data (), mainTabsEnabledView.size ());
    previewTabsEnabled = QByteArray (previewTabsEnabledView.data (), previewTabsEnabledView.size ());

    RobotModelSpec parsed;
    std::string parseError;
    if (!RobotModelSpecJson::fromJson (
            std::string (model.data (), size_t (model.size ())), parsed, &parseError)) {
        if (error != nullptr) {
            *error = QStringLiteral ("The RobotModelBuilder document snapshot is invalid: %1")
                         .arg (QString::fromStdString (parseError));
        }
        return false;
    }

    const QList< QLineEdit* > lineEdits = {_robotName,
                                           _deviceFile,
                                           _sceneFile,
                                           _dynamicWorkCellFile,
                                           _collisionSetupFile,
                                           _baseFrame,
                                           _baseMaterial,
                                           _robotBaseRpy,
                                           _robotBasePos,
                                           _status};
    const QList< QCheckBox* > checkBoxes = {_preserveImportedFileLayout,
                                            _showFrameAxes,
                                            _generateDrawables,
                                            _generateScene,
                                            _generateDwc,
                                            _exportDhAdvanced,
                                            _collisionSetupEnabled,
                                            _excludeBaseFirst,
                                            _excludeAdjacent,
                                            _excludeStatic};
    const QList< QTableWidget* > tables = {_transformTable,
                                           _dhTable,
                                           _drawablesTable,
                                           _collisionModelsTable,
                                           _collisionSetupPairsTable,
                                           _sceneFramesTable,
                                           _sceneGeometryTable,
                                           _limitsTable,
                                           _posesTable,
                                           _dynamicsLinksTable,
                                           _forceLimitsTable};

    struct LineEditState
    {
        QString text;
        bool enabled = false;
        bool readOnly = false;
        bool hidden = false;
    };
    struct CheckBoxState
    {
        qint32 checkState = 0;
        bool tristate = false;
        bool enabled = false;
        bool hidden = false;
    };
    struct CellState
    {
        bool hasItem = false;
        QString itemText;
        quint32 itemFlags = 0;
        qint32 itemCheckState = 0;
        QVariant itemUserRole;
        quint8 widgetKind = 0;
        QStringList comboItems;
        QVariantList comboData;
        int comboIndex = -1;
        QString comboText;
        bool comboEditable = false;
        bool widgetEnabled = false;
        qint32 checkState = 0;
        bool tristate = false;
    };
    struct TableState
    {
        int rows = 0;
        int columns = 0;
        int currentRow = -1;
        int currentColumn = -1;
        bool enabled = false;
        bool hidden = false;
        QStringList headers;
        std::vector< CellState > cells;
    };

    const auto invalidEditableState = [error] (const QString& message) {
        if (error != nullptr)
            *error = message;
        return false;
    };
    const QString invalidEditableMessage =
        QStringLiteral ("The RobotModelBuilder editable state snapshot is invalid.");
    const int maxTableRows = 10000;
    const int maxTableColumns = 256;
    const int maxTableCells = 1000000;
    const int maxTotalTableCells = 1000000;
    const int maxComboItems = 10000;

    SnapshotReader ui (editableState, decodedVariableBytes);
    quint32 lineEditCount = 0;
    if (!ui.readU32 (lineEditCount) || lineEditCount != quint32 (lineEdits.size ()))
        return invalidEditableState (invalidEditableMessage);
    std::vector< LineEditState > lineEditStates (lineEditCount);
    for (LineEditState& state : lineEditStates) {
        if (!ui.readString (state.text) || !ui.readBool (state.enabled) ||
            !ui.readBool (state.readOnly) || !ui.readBool (state.hidden))
            return invalidEditableState (invalidEditableMessage);
    }

    QStringList modeItems;
    QVariantList modeData;
    int modeIndex = -1;
    QString modeText;
    bool modeEditable = false;
    bool modeEnabled = false;
    quint32 modeItemCount = 0;
    if (!ui.readU32 (modeItemCount) || modeItemCount > quint32 (maxComboItems))
        return invalidEditableState (invalidEditableMessage);
    quint32 totalComboItems = modeItemCount;
    modeItems.reserve (int (modeItemCount));
    for (quint32 index = 0; index < modeItemCount; ++index) {
        QString item;
        if (!ui.readString (item))
            return invalidEditableState (invalidEditableMessage);
        modeItems.push_back (item);
    }
    quint32 modeDataCount = 0;
    if (!ui.readU32 (modeDataCount) || modeDataCount != modeItemCount)
        return invalidEditableState (invalidEditableMessage);
    modeData.reserve (int (modeDataCount));
    for (quint32 index = 0; index < modeDataCount; ++index) {
        QVariant data;
        if (!ui.readVariant (data))
            return invalidEditableState (invalidEditableMessage);
        modeData.push_back (data);
    }
    if (!ui.readInt (modeIndex) || !ui.readString (modeText) ||
        !ui.readBool (modeEditable) || !ui.readBool (modeEnabled) || modeIndex < -1 ||
        modeIndex >= modeItems.size ())
        return invalidEditableState (invalidEditableMessage);

    quint32 checkBoxCount = 0;
    if (!ui.readU32 (checkBoxCount) || checkBoxCount != quint32 (checkBoxes.size ()))
        return invalidEditableState (
            QStringLiteral ("The RobotModelBuilder checkbox snapshot is invalid."));
    std::vector< CheckBoxState > checkBoxStates (checkBoxCount);
    for (CheckBoxState& state : checkBoxStates) {
        if (!ui.readI32 (state.checkState) || !ui.readBool (state.tristate) ||
            !ui.readBool (state.enabled) || !ui.readBool (state.hidden) ||
            state.checkState < Qt::Unchecked ||
            state.checkState > Qt::Checked)
            return invalidEditableState (
                QStringLiteral ("The RobotModelBuilder checkbox snapshot is invalid."));
    }

    quint32 tableCount = 0;
    if (!ui.readU32 (tableCount) || tableCount != quint32 (tables.size ()))
        return invalidEditableState (
            QStringLiteral ("The RobotModelBuilder table snapshot is invalid."));
    std::vector< TableState > tableStates (tableCount);
    qint64 totalTableCells = 0;
    for (TableState& tableState : tableStates) {
        if (!ui.readInt (tableState.rows) || !ui.readInt (tableState.columns) ||
            !ui.readInt (tableState.currentRow) || !ui.readInt (tableState.currentColumn) ||
            !ui.readBool (tableState.enabled) || !ui.readBool (tableState.hidden))
            return invalidEditableState (invalidEditableMessage);
        const qint64 cellCount = qint64 (tableState.rows) * qint64 (tableState.columns);
        if (tableState.rows < 0 ||
            tableState.rows > maxTableRows || tableState.columns < 0 ||
            tableState.columns > maxTableColumns || cellCount > maxTableCells ||
            tableState.currentRow < -1 || tableState.currentRow >= tableState.rows ||
            tableState.currentColumn < -1 || tableState.currentColumn >= tableState.columns ||
            ((tableState.currentRow == -1) != (tableState.currentColumn == -1))) {
            return invalidEditableState (
                QStringLiteral ("The RobotModelBuilder table dimensions are invalid."));
        }
        totalTableCells += cellCount;
        if (totalTableCells > maxTotalTableCells)
            return invalidEditableState (
                QStringLiteral ("The RobotModelBuilder table snapshot is too large."));
        tableState.headers.reserve (tableState.columns);
        for (int column = 0; column < tableState.columns; ++column) {
            QString header;
            if (!ui.readString (header))
                return invalidEditableState (invalidEditableMessage);
            tableState.headers.push_back (header);
        }
        tableState.cells.resize (size_t (cellCount));
        for (CellState& cell : tableState.cells) {
            if (!ui.readBool (cell.hasItem))
                return invalidEditableState (invalidEditableMessage);
            if (cell.hasItem) {
                if (!ui.readString (cell.itemText) || !ui.readU32 (cell.itemFlags) ||
                    !ui.readI32 (cell.itemCheckState) || !ui.readVariant (cell.itemUserRole) ||
                    cell.itemCheckState < Qt::Unchecked || cell.itemCheckState > Qt::Checked)
                    return invalidEditableState (invalidEditableMessage);
            }
            if (!ui.readU8 (cell.widgetKind))
                return invalidEditableState (invalidEditableMessage);
            if (cell.widgetKind == 1) {
                quint32 comboItemCount = 0;
                if (!ui.readU32 (comboItemCount) || comboItemCount > quint32 (maxComboItems) ||
                    totalComboItems > quint32 (maxComboItems) - comboItemCount)
                    return invalidEditableState (invalidEditableMessage);
                totalComboItems += comboItemCount;
                cell.comboItems.reserve (int (comboItemCount));
                for (quint32 index = 0; index < comboItemCount; ++index) {
                    QString item;
                    if (!ui.readString (item))
                        return invalidEditableState (invalidEditableMessage);
                    cell.comboItems.push_back (item);
                }
                quint32 comboDataCount = 0;
                if (!ui.readU32 (comboDataCount) || comboDataCount != comboItemCount)
                    return invalidEditableState (invalidEditableMessage);
                cell.comboData.reserve (int (comboDataCount));
                for (quint32 index = 0; index < comboDataCount; ++index) {
                    QVariant data;
                    if (!ui.readVariant (data))
                        return invalidEditableState (invalidEditableMessage);
                    cell.comboData.push_back (data);
                }
                if (!ui.readInt (cell.comboIndex) || !ui.readString (cell.comboText) ||
                    !ui.readBool (cell.comboEditable) || !ui.readBool (cell.widgetEnabled) ||
                    cell.comboIndex < -1 || cell.comboIndex >= cell.comboItems.size ())
                    return invalidEditableState (invalidEditableMessage);
            }
            else if (cell.widgetKind == 2) {
                if (!ui.readI32 (cell.checkState) || !ui.readBool (cell.tristate) ||
                    !ui.readBool (cell.widgetEnabled) || cell.checkState < Qt::Unchecked ||
                    cell.checkState > Qt::Checked)
                    return invalidEditableState (invalidEditableMessage);
            }
            else if (cell.widgetKind != 0) {
                return invalidEditableState (invalidEditableMessage);
            }
            if (!ui.ok ())
                return invalidEditableState (invalidEditableMessage);
        }
    }
    bool sceneContentEnabled = false;
    if (!ui.readBool (sceneContentEnabled) || !ui.atEnd ())
        return invalidEditableState (invalidEditableMessage);

    if (mainTabsEnabled.size () != _mainTabs->count () ||
        previewTabsEnabled.size () != _previewTabs->count () || mainTabIndex < -1 ||
        mainTabIndex >= _mainTabs->count () || previewTabIndex < -1 ||
        previewTabIndex >= _previewTabs->count ())
        return invalidEditableState (
            QStringLiteral ("The RobotModelBuilder tab snapshot is invalid."));

    std::vector< std::unique_ptr< QSignalBlocker > > blockers;
    blockers.reserve (lineEdits.size () + checkBoxes.size () + tables.size () + 3);
    const auto block = [&blockers] (QObject* object) {
        blockers.push_back (std::unique_ptr< QSignalBlocker > (new QSignalBlocker (object)));
    };
    for (QLineEdit* lineEdit : lineEdits)
        block (lineEdit);
    for (QCheckBox* checkBox : checkBoxes)
        block (checkBox);
    for (QTableWidget* table : tables)
        block (table);
    block (_mode);
    block (_mainTabs);
    block (_previewTabs);

    const bool wasSyncingTables = _syncingTables;
    _syncingTables = true;
    _projectDirectory = projectDirectory;
    _projectOutputDirectory = projectOutputDirectory;
    _importedDocument = parsed.imported;

    for (int index = 0; index < lineEdits.size (); ++index) {
        const LineEditState& state = lineEditStates[size_t (index)];
        lineEdits[index]->setText (state.text);
        lineEdits[index]->setEnabled (state.enabled);
        lineEdits[index]->setReadOnly (state.readOnly);
        lineEdits[index]->setHidden (state.hidden);
    }

    _mode->clear ();
    for (int index = 0; index < modeItems.size (); ++index) {
        _mode->addItem (modeItems[index], modeData[index]);
    }
    _mode->setEditable (modeEditable);
    _mode->setCurrentIndex (modeIndex);
    if (modeEditable)
        _mode->setCurrentText (modeText);
    _mode->setEnabled (modeEnabled);

    for (int index = 0; index < checkBoxes.size (); ++index) {
        const CheckBoxState& state = checkBoxStates[size_t (index)];
        checkBoxes[index]->setTristate (state.tristate);
        checkBoxes[index]->setCheckState (Qt::CheckState (state.checkState));
        checkBoxes[index]->setEnabled (state.enabled);
        checkBoxes[index]->setHidden (state.hidden);
    }

    for (int tableIndex = 0; tableIndex < tables.size (); ++tableIndex) {
        QTableWidget* table = tables[tableIndex];
        const TableState& tableState = tableStates[size_t (tableIndex)];
        table->clearContents ();
        table->setColumnCount (tableState.columns);
        table->setRowCount (tableState.rows);
        for (int column = 0; column < tableState.columns; ++column) {
            table->setHorizontalHeaderItem (
                column, new QTableWidgetItem (tableState.headers[column]));
        }
        for (int row = 0; row < tableState.rows; ++row) {
            for (int column = 0; column < tableState.columns; ++column) {
                const CellState& cell =
                    tableState.cells[size_t (row * tableState.columns + column)];
                if (cell.widgetKind == 1) {
                    QComboBox* combo = new QComboBox ();
                    for (int index = 0; index < cell.comboItems.size (); ++index) {
                        combo->addItem (cell.comboItems[index], cell.comboData[index]);
                    }
                    combo->setEditable (cell.comboEditable);
                    combo->setCurrentIndex (cell.comboIndex);
                    if (cell.comboEditable)
                        combo->setCurrentText (cell.comboText);
                    combo->setEnabled (cell.widgetEnabled);
                    table->setCellWidget (row, column, combo);
                    combo->installEventFilter (this);
                    connect (combo, &QComboBox::currentTextChanged, this,
                             [this] (const QString&) {
                                 if (!_syncingTables)
                                     Q_EMIT projectDocumentInteraction ();
                             });
                    if (table == _drawablesTable && column == 2) {
                        connect (combo, &QComboBox::currentTextChanged, this,
                                 [this] (const QString&) {
                                     if (!_syncingTables)
                                         generatePreview ();
                                 });
                    }
                    else if (table == _collisionModelsTable && column == 3) {
                        connect (combo, &QComboBox::currentTextChanged, this,
                                 [this, row] (const QString&) {
                                     if (!_syncingTables) {
                                         synchronizeCollisionFileFromDrawable (row);
                                         generatePreview ();
                                     }
                                 });
                    }
                }
                else if (cell.widgetKind == 2) {
                    QCheckBox* checkBox = new QCheckBox ();
                    checkBox->setTristate (cell.tristate);
                    checkBox->setCheckState (Qt::CheckState (cell.checkState));
                    checkBox->setEnabled (cell.widgetEnabled);
                    table->setCellWidget (row, column, checkBox);
                    checkBox->installEventFilter (this);
                    connect (checkBox, &QCheckBox::checkStateChanged, this,
                             [this] (Qt::CheckState) {
                                 if (!_syncingTables)
                                     Q_EMIT projectDocumentInteraction ();
                             });
                }
                if (cell.hasItem) {
                    QTableWidgetItem* item = new QTableWidgetItem (cell.itemText);
                    item->setFlags (Qt::ItemFlags (cell.itemFlags));
                    item->setCheckState (Qt::CheckState (cell.itemCheckState));
                    item->setData (Qt::UserRole, cell.itemUserRole);
                    table->setItem (row, column, item);
                }
            }
        }
        table->setEnabled (tableState.enabled);
        table->setHidden (tableState.hidden);
        if (tableState.currentRow >= 0)
            table->setCurrentCell (tableState.currentRow, tableState.currentColumn);
    }
    _sceneContent->setEnabled (sceneContentEnabled);
    _syncingTables = wasSyncingTables;

    _projectCleanSnapshot = cleanSnapshot;
    _projectSnapshotActive = snapshotActive;
    _lastUrdfImportWarnings = importWarnings;
    _serialPreview->setPlainText (serialPreview);
    _scenePreview->setPlainText (scenePreview);
    _dwcPreview->setPlainText (dwcPreview);
    _collisionSetupPreview->setPlainText (collisionPreview);
    _proximitySetupPreview->setPlainText (proximityPreview);
    for (int index = 0; index < _mainTabs->count () && index < mainTabsEnabled.size (); ++index)
        _mainTabs->setTabEnabled (index, mainTabsEnabled[index] != '\0');
    for (int index = 0; index < _previewTabs->count () && index < previewTabsEnabled.size ();
         ++index)
        _previewTabs->setTabEnabled (index, previewTabsEnabled[index] != '\0');
    if (mainTabIndex >= 0 && mainTabIndex < _mainTabs->count ())
        _mainTabs->setCurrentIndex (mainTabIndex);
    if (previewTabIndex >= 0 && previewTabIndex < _previewTabs->count ())
        _previewTabs->setCurrentIndex (previewTabIndex);
    setStatus (status);
    return true;
}

bool RobotModelBuilderWidget::eventFilter (QObject* watched, QEvent* event)
{
    if (watched != this && _projectSnapshotActive &&
        (event->type () == QEvent::KeyRelease || event->type () == QEvent::MouseButtonRelease ||
         event->type () == QEvent::Wheel || event->type () == QEvent::Drop)) {
        // 事件并不直接置脏。页签切换或单纯选择单元格同样会到达这里，插件收到信号后必须
        // 调用 isProjectDocumentDirty 进行内容比较，保证标题星号只反映持久化数据变更。
        Q_EMIT projectDocumentInteraction ();
    }
    return QWidget::eventFilter (watched, event);
}

// 生成规范 JSON 快照：把当前 UI 收集的模型规格序列化，供脏比较与暂存写入共用，
// 保证“比较”与“写盘”使用同一份序列化规则。
QByteArray RobotModelBuilderWidget::projectDocumentSnapshot () const
{
    // 项目文档只保存可迁移的模型定义。XmlWriter 所需的 saveDirectory 在运行时由项目目录
    // 推导，不能写进 .rmb.json，否则项目复制或移动后会重新指向创建机器上的旧绝对路径。
    QByteArray snapshot;
    QString error;
    if (!serializeProjectDocument (snapshot, &error))
        return QByteArrayLiteral ("invalid-managed-model:") + error.toUtf8 ();
    return snapshot;
}

bool RobotModelBuilderWidget::serializeProjectDocument (QByteArray& snapshot,
                                                         QString* error) const
{
    if (error != nullptr)
        error->clear ();
    RobotModelSpec projectSpec = collectSpec ();
    if (!_projectDirectory.isEmpty ()) {
        RobotModelSpec portable;
        if (!RobotModelProjectPaths::makePortable (
                projectSpec, _projectDirectory, portable, error))
            return false;
        projectSpec = portable;
    }
    projectSpec.saveDirectory.clear ();
    snapshot = QByteArray::fromStdString (RobotModelSpecJson::toJson (projectSpec));
    return true;
}

// 设置项目受管输出目录（仅内存，不写入 .rmb.json）。目录为空表示独立 WorkCell 工作流。
void RobotModelBuilderWidget::setProjectOutputDirectory (const QString& projectDirectory)
{
    QString normalizedProjectDirectory;
    QString outputDirectory;
    if (!projectDirectory.trimmed ().isEmpty ()) {
        // 产物统一落在项目目录下的固定子目录，而非项目资源 JSON 所在的 models 目录，避免
        // XML、sidecar 与用户导入的模型快照混在一起，也让项目复制时保留明确的生成物边界。
        normalizedProjectDirectory = QDir::cleanPath (
            QDir::fromNativeSeparators (QFileInfo (projectDirectory).absoluteFilePath ()));
        outputDirectory = QDir::cleanPath (
            QDir (normalizedProjectDirectory).filePath (
                QStringLiteral ("generated/robot-models")));
    }
    // 主窗口标题刷新会频繁发出 projectContextChanged；目录未变化时直接返回，
    // 避免重复重排输出字段并触发不必要的预览重建。
    if (_projectDirectory == normalizedProjectDirectory &&
        _projectOutputDirectory == outputDirectory)
        return;

    _projectDirectory = normalizedProjectDirectory;
    _projectOutputDirectory = outputDirectory;
    const bool projectManaged = !_projectOutputDirectory.isEmpty ();
    // 历史导入文件布局可能包含相对上级目录或绝对路径。在项目模式下禁用该选项并清空
    // 已显示的目录成分，确保每个输出字段仅表示文件名，最终只能写入受管输出目录。
    _preserveImportedFileLayout->setEnabled (!projectManaged);
    if (projectManaged) {
        _preserveImportedFileLayout->setChecked (false);
        for (QLineEdit* outputField :
             {_deviceFile, _sceneFile, _dynamicWorkCellFile, _collisionSetupFile}) {
            outputField->setText (QFileInfo (outputField->text ().trimmed ()).fileName ());
        }
    }
    updateOutputFilePlaceholders ();
    generatePreview ();
}

QString RobotModelBuilderWidget::effectiveSaveDirectory () const
{
    // 独立 WorkCell 工作流没有 .rwproj 上下文时保留旧默认值；一旦主窗口提供项目目录，
    // 所有生成、预览和 URDF 导入都会改用项目内目录，绝不采纳历史 JSON 中的 saveDirectory。
    return _projectOutputDirectory.isEmpty () ? QDir::homePath () : _projectOutputDirectory;
}

// =============================================================================
//  buildUi()
//  说明: 用代码方式(无 .ui 文件)构建整个 UI 树,顺序为:
//        1) 顶部 QFormLayout(机器人名/保存目录/模式/选项)
//        2) 中部 QTabWidget(Kinematics/Drawables/Limits/Poses/Dynamics/Preview)
//        3) 底部一排按钮 + 状态栏
//        同时把所有需要交互的信号连接到对应的槽。
// =============================================================================
void RobotModelBuilderWidget::buildUi ()
{
    QVBoxLayout* root = new QVBoxLayout (this);

    // ---- 顶部表单 ----
    QFormLayout* form = new QFormLayout ();
    _robotName        = new QLineEdit ();

    // 保存目录行:文本框 + Browse 按钮
    // 模式选择下拉框
    _mode = new QComboBox ();
    _mode->addItem ("Joint + RPY + Pos");
    _mode->addItem ("DH Projection");

    // 全局选项只保留显示和导入布局行为；各输出开关随其功能页显示。
    QWidget* options        = new QWidget ();
    QHBoxLayout* optionLay  = new QHBoxLayout (options);
    _showFrameAxes          = new QCheckBox ("Show axes");
    _preserveImportedFileLayout = new QCheckBox ("Preserve imported file layout");
    _generateDrawables      = new QCheckBox ("Generate Drawables");
    _generateScene          = new QCheckBox ("Generate Scene file");
    _generateDwc            = new QCheckBox ("Generate Dynamic WorkCell");
    _exportDhAdvanced       = new QCheckBox ("Advanced: export DHJoint XML");
    _exportDhAdvanced->setVisible (false);
    optionLay->setContentsMargins (0, 0, 0, 0);
    optionLay->addWidget (_showFrameAxes);
    optionLay->addWidget (_preserveImportedFileLayout);
    optionLay->addWidget (_exportDhAdvanced);
    optionLay->addStretch ();

    form->addRow ("Robot name", _robotName);
    form->addRow ("Mode", _mode);
    form->addRow ("Options", options);
    root->addLayout (form);

    _deviceFile = new QLineEdit ();
    _sceneFile = new QLineEdit ();
    _dynamicWorkCellFile = new QLineEdit ();
    _collisionSetupFile = new QLineEdit ();

    QTabWidget* tabs = new QTabWidget ();
    _mainTabs = tabs;

    // -------------------------------------------------------------------------
    //  Kinematics 标签页:DH / Joint+RPY+Pos 两个表格(同时存在,通过 mode 切换可见性)
    //  Milestone 2:行数 = 当前 spec.transformJoints.size(),不再固定 6。
    // -------------------------------------------------------------------------
    QWidget* kinematicsTab = new QWidget ();
    QVBoxLayout* kinLayout = new QVBoxLayout (kinematicsTab);
    _transformTable        = makeTable (
        QStringList () << "Joint"
                       << "Type"
                       << "RPY deg (Z Y X)"
                       << "Pos m",
        0);
    _dhTable = makeTable (
        QStringList () << "Joint"
                       << "alpha deg"
                       << "a m"
                       << "d m"
                       << "offset deg"
                       << "Status",
        0);
    // DH 表是 SE(3) 真值的投影视图,整张表只读;
    // 任意单元格在 fillFromSpec / onTransformTableCellChanged 写入时
    // 也会显式 setFlags(~ItemIsEditable)以确保不可改。
    _dhTable->setEditTriggers (QAbstractItemView::NoEditTriggers);
    kinLayout->addWidget (_transformTable);
    kinLayout->addWidget (_dhTable);

    // 添加/删除/上下移 Joint 的按钮条(Milestone 2)
    QWidget* jointButtons     = new QWidget ();
    QHBoxLayout* jointBtnLay  = new QHBoxLayout (jointButtons);
    QPushButton* addJointBtn  = new QPushButton ("Add Joint");
    QPushButton* delJointBtn  = new QPushButton ("Remove Joint");
    QPushButton* upJointBtn   = new QPushButton ("Move Up");
    QPushButton* downJointBtn = new QPushButton ("Move Down");
    jointBtnLay->setContentsMargins (0, 0, 0, 0);
    jointBtnLay->addWidget (addJointBtn);
    jointBtnLay->addWidget (delJointBtn);
    jointBtnLay->addWidget (upJointBtn);
    jointBtnLay->addWidget (downJointBtn);
    jointBtnLay->addStretch ();
    kinLayout->addWidget (jointButtons);
    tabs->addTab (kinematicsTab, "Kinematics");

    // -------------------------------------------------------------------------
    //  Drawables 标签页:可视化几何(初始为空,resetDefaults 后会自动填入默认 Drawable)
    // -------------------------------------------------------------------------
    QWidget* drawablesTab = new QWidget ();
    QVBoxLayout* drawablesLay = new QVBoxLayout (drawablesTab);
    drawablesLay->addWidget (_generateDrawables);
    _drawablesTable = makeTable (
        QStringList () << "Name"
                       << "RefFrame"
                       << "Shape"
                       << "Dimensions x y z"
                       << "Radius"
                       << "Length"
                       << "File"
                       << "RPY deg (Z Y X)"
                        << "Pos m"
                        << "RGB",
        0);
    connect (_drawablesTable, &QTableWidget::itemSelectionChanged, this, [this] () {
        const int row = _drawablesTable->currentRow ();
        if (row < 0 || _drawablesTable->selectedItems ().isEmpty ()) {
            Q_EMIT drawableSelectionChanged (QString ());
            return;
        }
        Q_EMIT drawableSelectionChanged (itemText (_drawablesTable, row, 0));
    });
    drawablesLay->addWidget (_drawablesTable);
    QWidget* drawableButtons = new QWidget (drawablesTab);
    QHBoxLayout* drawableBtnLay = new QHBoxLayout (drawableButtons);
    QPushButton* addDrawableBtn = new QPushButton ("Add Geometry", drawableButtons);
    QPushButton* duplicateDrawableBtn = new QPushButton ("Duplicate Geometry", drawableButtons);
    QPushButton* removeDrawableBtn = new QPushButton ("Remove Geometry", drawableButtons);
    QPushButton* regenerateLinksBtn = new QPushButton ("Regenerate Link Helpers", drawableButtons);
    addDrawableBtn->setObjectName ("addDrawableButton");
    duplicateDrawableBtn->setObjectName ("duplicateDrawableButton");
    removeDrawableBtn->setObjectName ("removeDrawableButton");
    regenerateLinksBtn->setObjectName ("regenerateLinkHelpersButton");
    addDrawableBtn->setToolTip ("Add an editable Box drawable.");
    duplicateDrawableBtn->setToolTip ("Duplicate the selected drawable as editable geometry.");
    removeDrawableBtn->setToolTip ("Remove the selected drawable.");
    regenerateLinksBtn->setToolTip ("Rebuild generated inter-joint link helpers without changing shell geometry.");
    drawableBtnLay->setContentsMargins (0, 0, 0, 0);
    drawableBtnLay->addWidget (addDrawableBtn);
    drawableBtnLay->addWidget (duplicateDrawableBtn);
    drawableBtnLay->addWidget (removeDrawableBtn);
    drawableBtnLay->addWidget (regenerateLinksBtn);
    drawableBtnLay->addStretch ();
    drawablesLay->addWidget (drawableButtons);
    QGroupBox* drawablesOutputFiles = new QGroupBox ("Output Files");
    QFormLayout* drawablesOutputForm = new QFormLayout (drawablesOutputFiles);
    drawablesOutputForm->addRow ("Device file", _deviceFile);
    drawablesLay->addWidget (drawablesOutputFiles);
    tabs->addTab (drawablesTab, "Drawables");

    // -------------------------------------------------------------------------
    //  Collision Models 标签页(Milestone 5):
    //    9 列(无 RGB、无 Collision):Name / RefFrame / Shape /
    //    Dimensions x y z / Radius / Length / File / RPY / Pos;
    //    下方 3 个按钮:Add / Remove / Generate From Drawables。
    // -------------------------------------------------------------------------
    QWidget* collisionTab      = new QWidget ();
    QVBoxLayout* collisionLay  = new QVBoxLayout (collisionTab);
    _collisionModelsTable = makeTable (
        QStringList () << "Enabled"
                       << "Name"
                       << "RefFrame"
                       << "Shape"
                       << "Size"
                       << "File"
                       << "Pose",
        0);
    collisionLay->addWidget (_collisionModelsTable);

    QWidget* collisionButtons      = new QWidget ();
    QHBoxLayout* collisionBtnLay   = new QHBoxLayout (collisionButtons);
    QPushButton* addCollisionBtn   = new QPushButton ("Add Collision Model");
    QPushButton* delCollisionBtn   = new QPushButton ("Remove Collision Model");
    QPushButton* genCollisionBtn   = new QPushButton ("Generate From Drawables");
    collisionBtnLay->setContentsMargins (0, 0, 0, 0);
    collisionBtnLay->addWidget (addCollisionBtn);
    collisionBtnLay->addWidget (delCollisionBtn);
    collisionBtnLay->addWidget (genCollisionBtn);
    collisionBtnLay->addStretch ();
    collisionLay->addWidget (collisionButtons);

    QWidget* collisionSetupTab = new QWidget ();
    QVBoxLayout* collisionSetupLay = new QVBoxLayout (collisionSetupTab);
    QWidget* collisionSetupOptions = new QWidget ();
    QHBoxLayout* collisionSetupOptionLay = new QHBoxLayout (collisionSetupOptions);
    _collisionSetupEnabled = new QCheckBox ("Enable CollisionSetup");
    _excludeBaseFirst = new QCheckBox ("Auto exclude base-first");
    _excludeAdjacent = new QCheckBox ("Auto exclude adjacent joints");
    _excludeStatic = new QCheckBox ("Exclude static pairs");
    collisionSetupOptionLay->setContentsMargins (0, 0, 0, 0);
    collisionSetupOptionLay->addWidget (_collisionSetupEnabled);
    collisionSetupOptionLay->addWidget (_excludeBaseFirst);
    collisionSetupOptionLay->addWidget (_excludeAdjacent);
    collisionSetupOptionLay->addWidget (_excludeStatic);
    collisionSetupLay->addWidget (collisionSetupOptions);

    _collisionSetupPairsTable = makeTable (
        QStringList () << "Enabled"
                       << "First Frame"
                       << "Second Frame"
                       << "Source"
                       << "Reason",
        0);
    collisionSetupLay->addWidget (_collisionSetupPairsTable);

    QWidget* collisionSetupButtons = new QWidget ();
    QHBoxLayout* collisionSetupBtnLay = new QHBoxLayout (collisionSetupButtons);
    QPushButton* addExcludePairBtn = new QPushButton ("Add Pair");
    QPushButton* removeExcludePairBtn = new QPushButton ("Remove Pair");
    QPushButton* defaultsCollisionSetupBtn = new QPushButton ("Generate Defaults");
    collisionSetupBtnLay->setContentsMargins (0, 0, 0, 0);
    collisionSetupBtnLay->addWidget (addExcludePairBtn);
    collisionSetupBtnLay->addWidget (removeExcludePairBtn);
    collisionSetupBtnLay->addWidget (defaultsCollisionSetupBtn);
    collisionSetupBtnLay->addStretch ();
    collisionSetupLay->addWidget (collisionSetupButtons);
    QPushButton* advancedCollisionBtn = new QPushButton ("Advanced...");
    advancedCollisionBtn->setCheckable (true);
    _collisionSetupPairsTable->setVisible (false);
    collisionSetupButtons->setVisible (false);
    collisionLay->addWidget (collisionSetupOptions);
    QGroupBox* collisionOutputFiles = new QGroupBox ("Output Files");
    QFormLayout* collisionOutputForm = new QFormLayout (collisionOutputFiles);
    collisionOutputForm->addRow ("Collision setup", _collisionSetupFile);
    collisionLay->addWidget (advancedCollisionBtn);
    collisionLay->addWidget (_collisionSetupPairsTable);
    collisionLay->addWidget (collisionSetupButtons);
    collisionLay->addWidget (collisionOutputFiles);
    connect (advancedCollisionBtn, &QPushButton::toggled, _collisionSetupPairsTable,
             &QWidget::setVisible);
    connect (advancedCollisionBtn, &QPushButton::toggled, collisionSetupButtons,
             &QWidget::setVisible);
    tabs->addTab (collisionTab, "Collision");

    // -------------------------------------------------------------------------
    //  Scene Frames 标签页(Milestone 3):
    //   - 顶部两个 QLineEdit 编辑 RobotBase 位姿(RPY deg / Pos m);
    //   - 中间一张表编辑场景 frame 列表(每行 Name/RefFrame/Type/DAF/PoseMode/
    //     RPY/Pos/Transform 4x4 共 8 列);
    //   - 底部 Add / Remove 按钮。
    // -------------------------------------------------------------------------
    QWidget* sceneTab      = new QWidget ();
    _sceneTab = sceneTab;
    QVBoxLayout* sceneLay  = new QVBoxLayout (sceneTab);
    sceneLay->addWidget (_generateScene);
    _sceneContent = new QWidget ();
    QVBoxLayout* sceneContentLay = new QVBoxLayout (_sceneContent);
    sceneContentLay->setContentsMargins (0, 0, 0, 0);
    sceneLay->addWidget (_sceneContent);
    QFormLayout* robotBaseForm = new QFormLayout ();
    _robotBaseRpy = new QLineEdit ();
    _robotBasePos = new QLineEdit ();
    robotBaseForm->addRow ("RobotBase RPY deg (Z Y X)", _robotBaseRpy);
    robotBaseForm->addRow ("RobotBase Pos m", _robotBasePos);
    sceneContentLay->addLayout (robotBaseForm);

    _sceneFramesTable = makeTable (
        QStringList () << "Name"
                       << "RefFrame"
                       << "Type"
                       << "DAF"
                       << "PoseMode"
                       << "RPY deg (Z Y X)"
                       << "Pos m"
                       << "Transform 4x4",
        0);
    sceneContentLay->addWidget (_sceneFramesTable);

    // ---- Milestone 3.5:场景几何体表 ----
    _sceneGeometryTable = makeTable (
        QStringList () << "Name"
                       << "RefFrame"
                       << "Kind"
                       << "Size x y z"
                       << "Radius"
                       << "Length"
                       << "File"
                       << "RPY deg (Z Y X)"
                       << "Pos m"
                       << "RGB"
                       << "Collision",
        0);
    sceneContentLay->addWidget (_sceneGeometryTable);

    QWidget* sceneButtons = new QWidget ();
    QHBoxLayout* sceneBtnLay = new QHBoxLayout (sceneButtons);
    QPushButton* addSceneBtn = new QPushButton ("Add Scene Frame");
    QPushButton* delSceneBtn = new QPushButton ("Remove Scene Frame");
    QPushButton* addSceneGeometryBtn = new QPushButton ("Add Scene Geometry");
    QPushButton* delSceneGeometryBtn = new QPushButton ("Remove Scene Geometry");
    sceneBtnLay->setContentsMargins (0, 0, 0, 0);
    sceneBtnLay->addWidget (addSceneBtn);
    sceneBtnLay->addWidget (delSceneBtn);
    sceneBtnLay->addWidget (addSceneGeometryBtn);
    sceneBtnLay->addWidget (delSceneGeometryBtn);
    sceneBtnLay->addStretch ();
    sceneContentLay->addWidget (sceneButtons);
    QGroupBox* sceneOutputFiles = new QGroupBox ("Output Files");
    QFormLayout* sceneOutputForm = new QFormLayout (sceneOutputFiles);
    sceneOutputForm->addRow ("Scene file", _sceneFile);
    sceneContentLay->addWidget (sceneOutputFiles);
    tabs->addTab (sceneTab, "Scene Frames");

    // -------------------------------------------------------------------------
    //  Limits 标签页:关节限位(每关节 1 行,行数 = spec.limits.size())
    //  列名改成"单位中立"(PosMin/PosMax/VelMax/AccMax),具体单位在 Joint
    //  type 列指明 —— Revolute 是度,Prismatic 是米。
    // -------------------------------------------------------------------------
    _limitsTable = makeTable (kLimitsHeaders, 0);
    tabs->addTab (_limitsTable, "Limits");

    // -------------------------------------------------------------------------
    //  Poses 标签页:预设位姿 + Add/Remove 按钮
    //  列数随可动关节数变化;fillPosesTable 会按当前 spec 重建列。
    // -------------------------------------------------------------------------
    QWidget* posesTab      = new QWidget ();
    QVBoxLayout* posesLay  = new QVBoxLayout (posesTab);
    _posesTable            = makeTable (QStringList () << "Name", 0);
    QWidget* poseButtons      = new QWidget ();
    QHBoxLayout* poseBtnLay   = new QHBoxLayout (poseButtons);
    QPushButton* addPoseBtn   = new QPushButton ("Add Pose");
    QPushButton* delPoseBtn   = new QPushButton ("Remove Pose");
    poseBtnLay->setContentsMargins (0, 0, 0, 0);
    poseBtnLay->addWidget (addPoseBtn);
    poseBtnLay->addWidget (delPoseBtn);
    poseBtnLay->addStretch ();
    posesLay->addWidget (_posesTable);
    posesLay->addWidget (poseButtons);
    tabs->addTab (posesTab, "Poses");

    // -------------------------------------------------------------------------
    //  Dynamics 标签页:基座信息 + link 表 + 力限表
    // -------------------------------------------------------------------------
    QWidget* dynamicsTab       = new QWidget ();
    QVBoxLayout* dynLayout     = new QVBoxLayout (dynamicsTab);
    dynLayout->addWidget (_generateDwc);
    QFormLayout* dynBaseForm   = new QFormLayout ();
    _baseFrame                 = new QLineEdit ();
    _baseMaterial              = new QLineEdit ();
    dynBaseForm->addRow ("Base frame", _baseFrame);
    dynBaseForm->addRow ("Base material", _baseMaterial);
    dynLayout->addLayout (dynBaseForm);

    QLabel* dynLinksLabel = new QLabel ("Links (object = name of movable joint)");
    dynLayout->addWidget (dynLinksLabel);
    _dynamicsLinksTable = makeTable (
        QStringList () << "Link"
                       << "Object"
                       << "Mass kg"
                       << "COG x y z"
                       << "Ixx Iyy Izz Ixy Ixz Iyz"
                       << "Estimate?"
                       << "Material",
        0);
    dynLayout->addWidget (_dynamicsLinksTable);

    QLabel* forceLabel = new QLabel ("Force limits (Nm for Revolute, N for Prismatic)");
    dynLayout->addWidget (forceLabel);
    _forceLimitsTable = makeTable (
        QStringList () << "Joint"
                       << "Max force",
        0);
    dynLayout->addWidget (_forceLimitsTable);
    QGroupBox* dynamicsOutputFiles = new QGroupBox ("Output Files");
    QFormLayout* dynamicsOutputForm = new QFormLayout (dynamicsOutputFiles);
    dynamicsOutputForm->addRow ("Dynamic WorkCell", _dynamicWorkCellFile);
    dynLayout->addWidget (dynamicsOutputFiles);
    dynLayout->addStretch ();
    tabs->addTab (dynamicsTab, "Dynamics");

    // -------------------------------------------------------------------------
    //  XML Preview 标签页:所有可输出 XML 的实时预览(只读)
    // -------------------------------------------------------------------------
    QWidget* previewTab      = new QWidget ();
    QVBoxLayout* previewLay  = new QVBoxLayout (previewTab);
    QTabWidget* previewTabs  = new QTabWidget ();
    _previewTabs = previewTabs;
    _serialPreview           = new QTextEdit ();
    _scenePreview            = new QTextEdit ();
    _dwcPreview              = new QTextEdit ();
    _collisionSetupPreview   = new QTextEdit ();
    _proximitySetupPreview   = new QTextEdit ();
    _serialPreview->setReadOnly (true);
    _scenePreview->setReadOnly (true);
    _dwcPreview->setReadOnly (true);
    _collisionSetupPreview->setReadOnly (true);
    _proximitySetupPreview->setReadOnly (true);
    previewTabs->addTab (_serialPreview, "SerialDevice XML");
    previewTabs->addTab (_scenePreview, "Scene XML");
    previewTabs->addTab (_dwcPreview, "DWC XML");
    previewTabs->addTab (_collisionSetupPreview, "CollisionSetup XML");
    previewTabs->addTab (_proximitySetupPreview, "ProximitySetup XML");
    previewLay->addWidget (previewTabs);
    tabs->addTab (previewTab, "XML Preview");

    root->addWidget (tabs, 1);
    updateOutputFilePlaceholders ();

    // -------------------------------------------------------------------------
    //  底部按钮 + 状态栏
    // -------------------------------------------------------------------------
    QWidget* buttons        = new QWidget ();
    QHBoxLayout* buttonLay  = new QHBoxLayout (buttons);
    QPushButton* importUrdfBtn = new QPushButton ("Import URDF");
    QPushButton* previewBtn = new QPushButton ("Generate Preview");
    QPushButton* saveBtn    = new QPushButton ("Save XML");
    QPushButton* loadBtn    = new QPushButton ("Save and Load");
    QPushButton* resetBtn   = new QPushButton ("Reset to Default Six Axis");
    buttonLay->setContentsMargins (0, 0, 0, 0);
    buttonLay->addWidget (importUrdfBtn);
    buttonLay->addWidget (previewBtn);
    buttonLay->addWidget (saveBtn);
    buttonLay->addWidget (loadBtn);
    buttonLay->addStretch ();
    buttonLay->addWidget (resetBtn);
    root->addWidget (buttons);

    _status = new QLineEdit ();
    _status->setReadOnly (true);
    root->addWidget (_status);

    // -------------------------------------------------------------------------
    //  信号 -> 槽连接
    // -------------------------------------------------------------------------
    connect (_robotName, &QLineEdit::textChanged, this,
             [this] (const QString&) { updateOutputFilePlaceholders (); });
    connect (_mode, SIGNAL (currentIndexChanged (int)), this, SLOT (modeChanged (int)));
    connect (_generateScene, SIGNAL (toggled (bool)), this, SLOT (sceneGenerationToggled (bool)));
    connect (importUrdfBtn, SIGNAL (clicked ()), this, SLOT (importUrdf ()));
    connect (previewBtn, SIGNAL (clicked ()), this, SLOT (generatePreview ()));
    connect (saveBtn, SIGNAL (clicked ()), this, SLOT (saveXml ()));
    connect (loadBtn, SIGNAL (clicked ()), this, SLOT (saveAndLoad ()));
    connect (resetBtn, SIGNAL (clicked ()), this, SLOT (resetDefaults ()));
    connect (addPoseBtn, SIGNAL (clicked ()), this, SLOT (addPose ()));
    connect (delPoseBtn, SIGNAL (clicked ()), this, SLOT (removeSelectedPose ()));
    connect (addSceneBtn, SIGNAL (clicked ()), this, SLOT (addSceneFrame ()));
    connect (delSceneBtn, SIGNAL (clicked ()), this, SLOT (removeSelectedSceneFrame ()));
    connect (addSceneGeometryBtn, SIGNAL (clicked ()), this, SLOT (addSceneGeometry ()));
    connect (delSceneGeometryBtn, SIGNAL (clicked ()), this, SLOT (removeSelectedSceneGeometry ()));
    connect (addCollisionBtn, SIGNAL (clicked ()), this, SLOT (addCollisionModel ()));
    connect (delCollisionBtn, SIGNAL (clicked ()), this, SLOT (removeSelectedCollisionModel ()));
    connect (genCollisionBtn, SIGNAL (clicked ()), this, SLOT (generateCollisionModelsFromDrawables ()));
    connect (addExcludePairBtn, SIGNAL (clicked ()), this, SLOT (addCollisionExcludePair ()));
    connect (removeExcludePairBtn, SIGNAL (clicked ()), this, SLOT (removeSelectedCollisionExcludePair ()));
    connect (defaultsCollisionSetupBtn, SIGNAL (clicked ()), this, SLOT (generateDefaultCollisionSetup ()));
    connect (addJointBtn, SIGNAL (clicked ()), this, SLOT (addJoint ()));
    connect (delJointBtn, SIGNAL (clicked ()), this, SLOT (removeSelectedJoint ()));
    connect (upJointBtn, SIGNAL (clicked ()), this, SLOT (moveSelectedJointUp ()));
    connect (downJointBtn, SIGNAL (clicked ()), this, SLOT (moveSelectedJointDown ()));
    connect (addDrawableBtn, SIGNAL (clicked ()), this, SLOT (addDrawable ()));
    connect (duplicateDrawableBtn, SIGNAL (clicked ()), this, SLOT (duplicateSelectedDrawable ()));
    connect (removeDrawableBtn, SIGNAL (clicked ()), this, SLOT (removeSelectedDrawable ()));
    connect (regenerateLinksBtn, SIGNAL (clicked ()), this, SLOT (regenerateLinkHelpers ()));

    // Transform 表被编辑后刷新 DH 投影视图;DH 表不反向修改真值。
    // _syncingTables 防止 setItem 触发 _dhTable->itemChanged 引起无谓递归。
    connect (_dhTable, SIGNAL (itemChanged (QTableWidgetItem*)), this,
             SLOT (onDhTableCellChanged (QTableWidgetItem*)));
    connect (_transformTable, SIGNAL (itemChanged (QTableWidgetItem*)), this,
             SLOT (onTransformTableCellChanged (QTableWidgetItem*)));
    connect (_drawablesTable, &QTableWidget::cellDoubleClicked, this,
             [this] (int row, int column) {
                 if (column == 6) chooseGeometryFile (_drawablesTable, row, column);
             });
    connect (_collisionModelsTable, &QTableWidget::cellDoubleClicked, this,
             [this] (int row, int column) {
                 if (column == 5) chooseGeometryFile (_collisionModelsTable, row, column);
             });
    connect (_sceneGeometryTable, &QTableWidget::cellDoubleClicked, this,
             [this] (int row, int column) {
                 if (column == 6) chooseGeometryFile (_sceneGeometryTable, row, column);
             });
}

// =============================================================================
//  resetDefaults()
//  说明: 用 XmlWriter 的 makeDefaultSixAxisModel 生成默认数据并回填 UI,
//        同时立即生成一次预览,让用户看到出厂默认的 XML 长什么样。
// =============================================================================
void RobotModelBuilderWidget::resetDefaults ()
{
    applyDefaultProjectModel ();
}

// 应用默认六轴模型（"从零构建"的初始状态）：清空导入来源，用默认模型填充界面。
void RobotModelBuilderWidget::applyDefaultProjectModel ()
{
    _importedDocument = ImportedDocumentSpec ();
    // 默认模型同样使用当前项目受管目录；没有项目时 effectiveSaveDirectory 才会回退到
    // 独立工作流默认目录，避免“新建项目但仍输出到用户主目录”的窗口期。
    fillFromSpec (RobotModelXmlWriter::makeDefaultSixAxisModel (effectiveSaveDirectory ()));
    generatePreview ();
}

// =============================================================================
//  generatePreview()
//  说明: "Generate Preview" 按钮的回调:
//        1) 先做轻量的文本规则校验(各表格数字格式、向量维度);
//        2) 收集 spec,调用 applyLinkGeometry 自动重算 Link{i}To{i+1} 圆柱;
//        3) 调用 XmlWriter 校验 + 生成全部 XML 文本,刷新到预览框;
//        4) 在底部状态栏报告成功/失败。
// =============================================================================
void RobotModelBuilderWidget::generatePreview ()
{
    QStringList errors;
    if (!validateTableInput (errors)) {
        showErrors (errors);
        return;
    }

    RobotModelSpec spec = collectSpec ();
    RobotModelXmlWriter::applyLinkGeometry (spec);
    if (!RobotModelXmlWriter::validate (spec, errors)) {
        showErrors (errors);
        return;
    }

    fillDrawablesTable (spec);
    fillCollisionModelsTable (spec);
    fillCollisionSetupTab (spec);
    fillSceneTab (spec);
    fillSceneGeometryTable (spec);
    _serialPreview->setPlainText (RobotModelXmlWriter::makeSerialDeviceXml (spec));
    _scenePreview->setPlainText (spec.generateScene
                                     ? RobotModelXmlWriter::makeSceneXml (spec)
                                     : QString ("<!-- Enable \"Scene file\" to generate -->"));
    _dwcPreview->setPlainText (spec.dynamics.generateDynamicWorkCell
                                   ? RobotModelXmlWriter::makeDynamicWorkCellXml (spec)
                                   : QString ("<!-- Enable \"Dynamic WorkCell\" to generate -->"));
    _collisionSetupPreview->setPlainText (spec.generateScene && spec.collisionSetup.enabled
                                              ? RobotModelXmlWriter::makeCollisionSetupXml (spec)
                                              : QString ("<!-- Enable CollisionSetup to generate -->"));
    _proximitySetupPreview->setPlainText (spec.generateScene && spec.proximitySetup.enabled
                                               ? RobotModelXmlWriter::makeProximitySetupXml (spec)
                                               : QString ("<!-- Enable ProximitySetup to generate -->"));
    setStatus ("Preview generated.");
}

// =============================================================================
//  saveXml()
//  说明: "Save XML" 按钮的回调。流程与 generatePreview 类似,但最后调用
//        saveFiles 真正落盘;完成后再次刷新一次预览(因为保存后再看预览
//        是最自然的"确认"动作)。
// =============================================================================
void RobotModelBuilderWidget::saveXml ()
{
    QStringList errors;
    if (!validateTableInput (errors)) {
        showErrors (errors);
        return;
    }

    RobotModelSpec spec = collectSpec ();
    RobotModelXmlWriter::applyLinkGeometry (spec);
    if (!confirmOutputOverwrite (spec)) {
        setStatus ("Save cancelled.");
        return;
    }
    if (!RobotModelXmlWriter::saveFiles (spec, errors)) {
        showErrors (errors);
        return;
    }
    // 生成目录中的 sidecar 也会被后续作为项目资源导入，项目模式下同样不能保存绝对目录。
    RobotModelSpec sidecarSpec = spec;
    if (!_projectOutputDirectory.isEmpty ())
        sidecarSpec.saveDirectory.clear ();
    if (!RobotModelXmlWriter::saveSpecSidecar (sidecarSpec, errors)) {
        showErrors (errors);
        return;
    }
    generatePreview ();
    setStatus ("XML files saved.");
}

// =============================================================================
//  saveAndLoad()
//  说明: "Save and Load" 按钮的回调。在 saveXml 的基础上额外发出
//        loadSceneRequested 信号,由 RobotModelBuilderPlugin 真正去切换场景。
// =============================================================================
void RobotModelBuilderWidget::saveAndLoad ()
{
    QStringList errors;
    if (!validateTableInput (errors)) {
        showErrors (errors);
        return;
    }

    RobotModelSpec spec = collectSpec ();
    RobotModelXmlWriter::applyLinkGeometry (spec);
    const bool projectManaged = !_projectOutputDirectory.isEmpty ();
    if (!confirmOutputOverwrite (spec, !projectManaged)) {
        setStatus ("Save cancelled.");
        return;
    }

    if (projectManaged) {
        RobotModelPublishRequest request;
        request.spec = spec;
        request.projectRoot = _projectDirectory;
        request.promote = _projectPublishPromoter;
        QString publishError;
        if (!RobotModelPublishService::publishAndLoad (request, &publishError)) {
            errors << publishError;
            showErrors (errors);
            setStatus ("Save and Load failed. Previous project outputs were restored.");
            return;
        }
        generatePreview ();
        setStatus (
            "Generated scene loaded as the managed project WorkCell. Use File > Save Project to commit it.");
        return;
    }

    if (!RobotModelXmlWriter::saveFiles (spec, errors)) {
        showErrors (errors);
        return;
    }
    // Save and Load 生成的 sidecar 与普通保存具有相同的可搬迁性约束。
    if (!RobotModelXmlWriter::saveSpecSidecar (spec, errors)) {
        showErrors (errors);
        return;
    }
    generatePreview ();
    if (spec.generateScene) {
        Q_EMIT loadSceneRequested (RobotModelXmlWriter::sceneFilePath (spec));
        setStatus ("XML files saved. Loading scene...");
    }
    else {
        Q_EMIT loadSceneRequested (RobotModelXmlWriter::serialDeviceFilePath (spec));
        setStatus ("XML files saved. Loading robot model...");
    }
}

void RobotModelBuilderWidget::updateOutputFilePlaceholders ()
{
    const QString robotName = RobotModelXmlWriter::sanitizeFileBaseName (_robotName->text ());
    _deviceFile->setPlaceholderText (robotName + ".wc.xml");
    _sceneFile->setPlaceholderText (robotName + "Scene.wc.xml");
    _dynamicWorkCellFile->setPlaceholderText (robotName + ".dwc.xml");
    _collisionSetupFile->setPlaceholderText ("CollisionSetup.xml");
}

bool RobotModelBuilderWidget::confirmOutputOverwrite (const RobotModelSpec& spec,
                                                       bool includeSidecar)
{
    QStringList outputFiles;
    outputFiles << RobotModelXmlWriter::serialDeviceFilePath (spec);
    if (includeSidecar)
        outputFiles << RobotModelXmlWriter::specSidecarFilePath (spec);
    if (spec.generateScene) {
        if (spec.collisionSetup.enabled)
            outputFiles << RobotModelXmlWriter::collisionSetupFilePath (spec);
        if (spec.proximitySetup.enabled)
            outputFiles << RobotModelXmlWriter::proximitySetupFilePath (spec);
        outputFiles << RobotModelXmlWriter::sceneFilePath (spec);
    }
    if (spec.dynamics.generateDynamicWorkCell)
        outputFiles << RobotModelXmlWriter::dynamicWorkCellFilePath (spec);

    QSet< QString > seen;
    QStringList existingFiles;
    for (const QString& file : outputFiles) {
        const QString normalized = QDir::cleanPath (file);
        if (seen.contains (normalized) || !QFileInfo::exists (normalized))
            continue;
        seen.insert (normalized);
        existingFiles << normalized;
    }
    if (existingFiles.isEmpty ())
        return true;

    return QMessageBox::question (
               this, "Confirm overwrite",
               "The following files already exist and will be replaced:\n" +
                   existingFiles.join ("\n") + "\n\nContinue?",
               QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

// =============================================================================
//  importUrdf()
//  说明: Import URDF 按钮回调;弹出文件选择对话框,调用
//        RobotModelUrdfImporter::importFile,然后把 spec 灌回 UI 并刷新预览;
//        警告信息以信息框告诉用户。
// =============================================================================
void RobotModelBuilderWidget::importUrdf ()
{
    const QString path = QFileDialog::getOpenFileName (
        this,
        "Import URDF",
        effectiveSaveDirectory (),
        "URDF files (*.urdf *.xml);;All files (*)");
    if (path.isEmpty ())
        return;

    QString error;
    if (!importUrdfFile (path, &error)) {
        showErrors (error.split ('\n', Qt::SkipEmptyParts));
        return;
    }

    if (!_lastUrdfImportWarnings.isEmpty ())
        QMessageBox::information (this, "URDF Import Warnings",
                                  _lastUrdfImportWarnings.join ("\n"));
}

// 无对话框的 URDF 导入实现：供"从机器人文件创建项目"流程复用，把源文件导入结果填入
// UI 并生成预览；失败经 error 回填，警告存入 _lastUrdfImportWarnings 由调用方展示。
bool RobotModelBuilderWidget::preflightUrdfFile (const QString& path,
                                                 const QString& projectRoot,
                                                 RobotModelSpec& parsed,
                                                 QStringList& warnings,
                                                 QString* error) const
{
    return preflightUrdfFile (path, projectRoot, RobotProjectImportOptions {}, parsed, warnings, error);
}

bool RobotModelBuilderWidget::preflightUrdfFile (const QString& path,
                                                 const QString& projectRoot,
                                                 const RobotProjectImportOptions& importOptions,
                                                 RobotModelSpec& parsed,
                                                 QStringList& warnings,
                                                 QString* error) const
{
    if (error != NULL)
        error->clear ();
    if (path.isEmpty ()) {
        if (error != NULL)
            *error = "No URDF file was selected.";
        return false;
    }

    UrdfImportOptions options;
    QString intendedSaveDirectory;
    QTemporaryDir validationDirectory;
    // URDF 的读取位置可以在项目外，但生成后的模型/XML 必须返回当前项目的受管输出目录。
    // 因此不再以 URDF 所在目录或用户输入目录作为 saveDirectory 的回退值。
    if (!projectRoot.trimmed ().isEmpty ()) {
        if (!QDir::isAbsolutePath (projectRoot)) {
            if (error != NULL)
                *error = "The robot project root must be an absolute path.";
            return false;
        }
        const QString normalizedRoot = QDir::cleanPath (QDir::fromNativeSeparators (projectRoot));
        intendedSaveDirectory =
            QDir (normalizedRoot).filePath (QStringLiteral ("generated/robot-models"));
        options.saveDirectory = intendedSaveDirectory;
    }
    else {
        options.saveDirectory = effectiveSaveDirectory ();
        intendedSaveDirectory = options.saveDirectory;
    }
    if (!QDir (options.saveDirectory).exists ()) {
        if (!validationDirectory.isValid ()) {
            if (error != NULL)
                *error = "Could not create a temporary directory for robot model preflight.";
            return false;
        }
        options.saveDirectory = validationDirectory.path ();
    }
    const QDir urdfDir (QFileInfo (path).absolutePath ());
    options.packageRoots = importOptions.packageRoots;
    options.packageRoots << urdfDir.absolutePath ();
    QDir parentDir = urdfDir;
    if (parentDir.cdUp ())
        options.packageRoots << parentDir.absolutePath ();
    QDir packageParentDir = parentDir;
    if (packageParentDir.cdUp ())
        options.packageRoots << packageParentDir.absolutePath ();
    options.packageRoots.removeDuplicates ();
    options.meshImportMode = importOptions.meshImportMode;
    options.missingMeshPolicy = importOptions.missingMeshPolicy;
    options.generateScene          = _generateScene->isChecked ();
    options.generateDrawables      = _generateDrawables->isChecked ();
    options.generateDynamicWorkCell = _generateDwc->isChecked ();

    UrdfImportResult result;
    QStringList errors;
    if (!RobotModelUrdfImporter::importFile (path, options, result, errors)) {
        if (error != NULL)
            *error = errors.join ("\n");
        return false;
    }

    parsed = result.spec;
    parsed.saveDirectory = intendedSaveDirectory.toStdString ();
    warnings = result.warnings;
    return true;
}

void RobotModelBuilderWidget::applyImportedProjectModel (const RobotModelSpec& parsed,
                                                         const QStringList& warnings)
{
    _importedDocument = ImportedDocumentSpec ();
    fillFromSpec (parsed);
    generatePreview ();
    _lastUrdfImportWarnings = warnings;

    setStatus ("URDF imported. Review the preview, then use Save XML or Save and Load.");
}

bool RobotModelBuilderWidget::importUrdfFile (const QString& path, QString* error)
{
    RobotModelSpec parsed;
    QStringList warnings;
    if (!preflightUrdfFile (path, _projectDirectory, parsed, warnings, error))
        return false;
    applyImportedProjectModel (parsed, warnings);
    return true;
}

// =============================================================================
//  modeChanged()
//  说明: UI 视图模式切换:Joint+RPY+Pos / DH Projection。
//        只决定哪张表可见;真值永远是 SE(3),DH 永远是派生视图。
// =============================================================================
void RobotModelBuilderWidget::modeChanged (int index)
{
    const bool dhMode = index == 1;
    _dhTable->setVisible (dhMode);
    _transformTable->setVisible (!dhMode);
}

// =============================================================================
//  addPose() / removeSelectedPose()
//  说明: Poses 表的"新增/删除选中行"。新增时初始化当前可动关节数个关节角为 0;
//        删除时至少保留 1 行,避免空表带来的边界问题。
//        Milestone 2:_posesTable 列数随可动关节数变化;这里读 fillFromSpec
//        留下的现有列数,作为新增行的列数参考。
// =============================================================================
void RobotModelBuilderWidget::addPose ()
{
    const int row    = _posesTable->rowCount ();
    const int qCount = std::max< int > (1, _posesTable->columnCount () - 1);
    _posesTable->insertRow (row);
    setItem (_posesTable, row, 0, "Pose" + QString::number (row + 1));
    for (int i = 1; i <= qCount; ++i)
        setItem (_posesTable, row, i, "0");
}

void RobotModelBuilderWidget::removeSelectedPose ()
{
    const int row = _posesTable->currentRow ();
    if (row >= 0 && _posesTable->rowCount () > 1)
        _posesTable->removeRow (row);
}

// =============================================================================
//  addJoint() / removeSelectedJoint() / moveSelectedJoint*()
//  说明: Milestone 2 的"关节数量可变"UI。
//        * addJoint():在 _transformTable 当前选中行下方追加一行 Revolute,
//                     并同步追加 DH/limit/forceLimit/dynamics link/pose.q/
//                     drawables(housing + auto link);
//        * removeSelectedJoint():从所有 spec 中删除该行;至少保留 1 个关节;
//        * moveSelectedJoint*():swap 两行;同样同步所有 spec 字段;
//        同步完成后 fillFromSpec() 重新把所有表显示回 UI。
// =============================================================================
void RobotModelBuilderWidget::addJoint ()
{
    RobotModelSpec spec = collectSpec ();
    const RobotModelSpec oldSpec = spec;
    const std::vector< std::string > oldMovables = movableNames (oldSpec.transformJoints);
    const int insertRow = std::max< int > (0, _transformTable->currentRow () + 1);
    const QString newName = pickNextJointName (spec);

    JointTransformSpec j;
    j.name   = newName.toStdString ();
    j.type   = "Revolute";
    j.rpyDeg = {{0, 0, 0}};
    j.pos    = {{0, 0, 0.1}};
    spec.transformJoints.insert (spec.transformJoints.begin () + insertRow, j);

    DHJointSpec dh;
    dh.name      = j.name;
    dh.alphaDeg  = 0.0;
    dh.a         = 0.0;
    dh.d         = 0.1;
    dh.offsetDeg = 0.0;
    spec.dhJoints.insert (spec.dhJoints.begin () + insertRow, dh);

    synchronizeJointDerivedData (spec, oldSpec, oldMovables, std::set< std::string > ());
    fillFromSpec (spec);
    setStatus (QString ("Added joint %1 (now %2 joints).").arg (newName).arg (
                  static_cast< int >(spec.transformJoints.size ())));
    return;
}

void RobotModelBuilderWidget::removeSelectedJoint ()
{
    RobotModelSpec spec = collectSpec ();
    const RobotModelSpec oldSpec = spec;
    const std::vector< std::string > oldMovables = movableNames (oldSpec.transformJoints);
    const int row = _transformTable->currentRow ();
    if (row < 0 || row >= static_cast< int >(spec.transformJoints.size ()))
        return;
    if (spec.transformJoints.size () <= 1) {
        setStatus ("At least one joint must remain.");
        return;
    }
    const QString removedName = QString::fromStdString (spec.transformJoints[row].name);

    spec.transformJoints.erase (spec.transformJoints.begin () + row);
    if (static_cast< size_t >(row) < spec.dhJoints.size ())
        spec.dhJoints.erase (spec.dhJoints.begin () + row);
    std::set< std::string > removedNames;
    removedNames.insert (removedName.toStdString ());
    synchronizeJointDerivedData (spec, oldSpec, oldMovables, removedNames);
    fillFromSpec (spec);
    setStatus (QString ("Removed joint %1 (now %2 joints).").arg (removedName).arg (
                  static_cast< int >(spec.transformJoints.size ())));
    return;
}

void RobotModelBuilderWidget::moveSelectedJointUp ()
{
    RobotModelSpec spec = collectSpec ();
    const RobotModelSpec oldSpec = spec;
    const std::vector< std::string > oldMovables = movableNames (oldSpec.transformJoints);
    const int row = _transformTable->currentRow ();
    if (row <= 0 || row >= static_cast< int >(spec.transformJoints.size ()))
        return;
    std::swap (spec.transformJoints[static_cast< size_t > (row - 1)],
               spec.transformJoints[static_cast< size_t > (row)]);
    if (static_cast< size_t > (row) < spec.dhJoints.size ())
        std::swap (spec.dhJoints[static_cast< size_t > (row - 1)],
                   spec.dhJoints[static_cast< size_t > (row)]);
    synchronizeJointDerivedData (spec, oldSpec, oldMovables, std::set< std::string > ());
    fillFromSpec (spec);
    if (static_cast< size_t >(row - 1) < spec.transformJoints.size ())
        _transformTable->setCurrentCell (row - 1, 0);
}

void RobotModelBuilderWidget::moveSelectedJointDown ()
{
    RobotModelSpec spec = collectSpec ();
    const RobotModelSpec oldSpec = spec;
    const std::vector< std::string > oldMovables = movableNames (oldSpec.transformJoints);
    const int row = _transformTable->currentRow ();
    if (row < 0)
        return;
    const size_t n = spec.transformJoints.size ();
    if (static_cast< size_t >(row + 1) >= n)
        return;
    std::swap (spec.transformJoints[static_cast< size_t > (row)],
               spec.transformJoints[static_cast< size_t > (row + 1)]);
    if (static_cast< size_t > (row + 1) < spec.dhJoints.size ())
        std::swap (spec.dhJoints[static_cast< size_t > (row)],
                   spec.dhJoints[static_cast< size_t > (row + 1)]);
    synchronizeJointDerivedData (spec, oldSpec, oldMovables, std::set< std::string > ());
    fillFromSpec (spec);
    if (static_cast< size_t >(row + 1) < spec.transformJoints.size ())
        _transformTable->setCurrentCell (row + 1, 0);
}

void RobotModelBuilderWidget::addDrawable ()
{
    RobotModelSpec spec = collectSpec ();
    std::set< std::string > names;
    for (const DrawableSpec& drawable : spec.drawables)
        names.insert (drawable.name);
    int index = 1;
    std::string name;
    do {
        name = "Geometry" + std::to_string (index++);
    } while (names.find (name) != names.end ());

    DrawableSpec drawable;
    drawable.name       = name;
    drawable.refFrame   = spec.transformJoints.empty () ? "Base" : spec.transformJoints.front ().name;
    drawable.shape      = "Box";
    drawable.dimensions = {{0.1, 0.1, 0.1}};
    drawable.radius     = 0.05;
    drawable.length     = 0.1;
    drawable.rgb        = {{0.65, 0.65, 0.68}};
    spec.drawables.push_back (drawable);
    fillFromSpec (spec);
    _drawablesTable->setCurrentCell (_drawablesTable->rowCount () - 1, 0);
    generatePreview ();
    setStatus (QString ("Added drawable %1.").arg (QString::fromStdString (name)));
}

void RobotModelBuilderWidget::duplicateSelectedDrawable ()
{
    RobotModelSpec spec = collectSpec ();
    const int row = _drawablesTable->currentRow ();
    if (row < 0 || row >= static_cast< int > (spec.drawables.size ())) {
        setStatus ("Select a drawable to duplicate.");
        return;
    }

    DrawableSpec duplicate = spec.drawables[static_cast< size_t > (row)];
    std::set< std::string > names;
    for (const DrawableSpec& drawable : spec.drawables)
        names.insert (drawable.name);
    const std::string base = duplicate.name.empty () ? "Geometry" : duplicate.name + " Copy";
    duplicate.name = base;
    int suffix = 2;
    while (names.find (duplicate.name) != names.end ())
        duplicate.name = base + " " + std::to_string (suffix++);
    // A copied helper becomes independent, editable geometry instead of being
    // overwritten by the next automatic-link update.
    duplicate.autoGenerated = false;
    duplicate.autoLinkGeometry = false;
    spec.drawables.push_back (duplicate);
    fillFromSpec (spec);
    _drawablesTable->setCurrentCell (_drawablesTable->rowCount () - 1, 0);
    generatePreview ();
    setStatus (QString ("Duplicated drawable as %1.")
                   .arg (QString::fromStdString (duplicate.name)));
}

void RobotModelBuilderWidget::removeSelectedDrawable ()
{
    RobotModelSpec spec = collectSpec ();
    const int row = _drawablesTable->currentRow ();
    if (row < 0 || row >= static_cast< int > (spec.drawables.size ())) {
        setStatus ("Select a drawable to remove.");
        return;
    }
    const QString name = QString::fromStdString (spec.drawables[static_cast< size_t > (row)].name);
    spec.drawables.erase (spec.drawables.begin () + row);
    fillFromSpec (spec);
    if (_drawablesTable->rowCount () > 0)
        _drawablesTable->setCurrentCell (std::min (row, _drawablesTable->rowCount () - 1), 0);
    generatePreview ();
    setStatus (QString ("Removed drawable %1.").arg (name));
}

void RobotModelBuilderWidget::regenerateLinkHelpers ()
{
    RobotModelSpec spec = collectSpec ();
    RobotModelXmlWriter::regenerateAutoLinkDrawables (spec);
    const int helperCount = static_cast< int > (std::count_if (
        spec.drawables.begin (), spec.drawables.end (), [] (const DrawableSpec& drawable) {
            return drawable.autoLinkGeometry;
        }));
    fillFromSpec (spec);
    generatePreview ();
    setStatus (QString ("Regenerated %1 link helpers (%2 drawables total).").arg (helperCount).arg (
                   static_cast< int > (spec.drawables.size ())));
}

// =============================================================================
//  addSceneFrame() / removeSelectedSceneFrame()
//  说明: Milestone 3 场景 frame 增删按钮。
//        * addSceneFrame:在 spec.sceneFrames 末尾追加一个默认
//          (Name="SceneFrame{N}", RefFrame=WORLD, Fixed, PoseMode=RPYPos,
//           RPY/Pos 全 0,Transform 4x4 默认单位阵)。Daf=false。
//        * removeSelectedSceneFrame:删除当前选中行;若被删 frame 被其他
//          sceneFrame 引用,则把它们的 refFrame 重置为 "WORLD",
//          避免留下 dangling reference。
//        两种操作后都 fillFromSpec(spec) + generatePreview(),
//        让用户立刻看到 XML 预览的变化。
// =============================================================================
void RobotModelBuilderWidget::addSceneFrame ()
{
    RobotModelSpec spec = collectSpec ();
    FrameSpec frame;
    frame.name      = "SceneFrame" + std::to_string (spec.sceneFrames.size () + 1);
    frame.refFrame  = "WORLD";
    frame.frameType = SceneFrameType::Fixed;
    frame.daf       = false;
    frame.poseMode  = PoseMode::RPYPos;
    frame.rpyDeg    = {{0, 0, 0}};
    frame.pos       = {{0, 0, 0}};
    frame.transform = {{1, 0, 0, 0,
                        0, 1, 0, 0,
                        0, 0, 1, 0,
                        0, 0, 0, 1}};
    spec.sceneFrames.push_back (frame);
    fillFromSpec (spec);
    generatePreview ();
}

void RobotModelBuilderWidget::removeSelectedSceneFrame ()
{
    RobotModelSpec spec = collectSpec ();
    const int row = _sceneFramesTable->currentRow ();
    if (row < 0 || row >= static_cast< int >(spec.sceneFrames.size ()))
        return;
    const std::string removed = spec.sceneFrames[static_cast< size_t > (row)].name;
    spec.sceneFrames.erase (spec.sceneFrames.begin () + row);
    // 同步修正引用被删 frame 的其他 sceneFrame + 场景几何体,避免 dangling refFrame
    for (FrameSpec& frame : spec.sceneFrames) {
        if (frame.refFrame == removed)
            frame.refFrame = "WORLD";
    }
    spec.sceneGeometries.erase (
        std::remove_if (spec.sceneGeometries.begin (), spec.sceneGeometries.end (),
                        [&] (const SceneGeometrySpec& geometry) {
                            return geometry.refFrame == removed;
                        }),
        spec.sceneGeometries.end ());
    fillFromSpec (spec);
    generatePreview ();
}

// =============================================================================
//  addSceneGeometry() / removeSelectedSceneGeometry()
//  说明: Milestone 3.5 场景几何体增删按钮。
//        * addSceneGeometry:追加默认 Box,RefFrame 取第一个场景 frame,
//          没有则用 RobotBase。RGB 中性灰,collisionModel=true。
//        * removeSelectedSceneGeometry:删除当前选中行,不级联。
// =============================================================================
void RobotModelBuilderWidget::addSceneGeometry ()
{
    RobotModelSpec spec = collectSpec ();
    SceneGeometrySpec geometry;
    geometry.name = "SceneGeometry" + std::to_string (spec.sceneGeometries.size () + 1);
    geometry.refFrame =
        spec.sceneFrames.empty () ? "RobotBase" : spec.sceneFrames.front ().name;
    geometry.kind          = GeometryKind::Box;
    geometry.size          = {{0.1, 0.1, 0.1}};
    geometry.radius        = 0.05;
    geometry.length        = 0.1;
    geometry.rgb           = {{0.6, 0.6, 0.6}};
    geometry.collisionModel = true;
    spec.sceneGeometries.push_back (geometry);
    fillFromSpec (spec);
    generatePreview ();
}

void RobotModelBuilderWidget::removeSelectedSceneGeometry ()
{
    RobotModelSpec spec = collectSpec ();
    const int row = _sceneGeometryTable->currentRow ();
    if (row < 0 || row >= static_cast< int >(spec.sceneGeometries.size ()))
        return;
    spec.sceneGeometries.erase (spec.sceneGeometries.begin () + row);
    fillFromSpec (spec);
    generatePreview ();
}

// =============================================================================
//  addCollisionModel() / removeSelectedCollisionModel()
//  说明: Milestone 5 碰撞模型按钮。
//        * addCollisionModel:追加默认 Box 碰撞模型,RefFrame 取第一个关节
//          (没有则 Base);尺寸 0.1 各向,radius/length 给个合法的 0.05/0.1
//          默认值,避免落进 validate 拒绝区;
//        * removeSelectedCollisionModel:删除当前选中行;不级联。
// =============================================================================
void RobotModelBuilderWidget::addCollisionModel ()
{
    RobotModelSpec spec = collectSpec ();
    CollisionModelSpec collision;
    collision.name     = "CollisionModel" +
                          std::to_string (spec.collisionModels.size () + 1);
    collision.refFrame = spec.transformJoints.empty ()
                              ? "Base"
                              : spec.transformJoints.front ().name;
    collision.shape        = "Box";
    collision.dimensions   = {{0.1, 0.1, 0.1}};
    collision.radius       = 0.05;
    collision.length       = 0.1;
    collision.rpyDeg       = {{0, 0, 0}};
    collision.pos          = {{0, 0, 0}};
    spec.collisionModels.push_back (collision);
    fillFromSpec (spec);
    generatePreview ();
}

void RobotModelBuilderWidget::removeSelectedCollisionModel ()
{
    RobotModelSpec spec = collectSpec ();
    const int row = _collisionModelsTable->currentRow ();
    if (row < 0 || row >= static_cast< int >(spec.collisionModels.size ()))
        return;
    spec.collisionModels.erase (spec.collisionModels.begin () + row);
    fillFromSpec (spec);
    generatePreview ();
}

void RobotModelBuilderWidget::addCollisionExcludePair ()
{
    RobotModelSpec spec = collectSpec ();
    FramePairSpec pair;
    pair.enabled = true;
    pair.first = spec.dynamics.baseFrame.empty () ? "Base" : spec.dynamics.baseFrame;
    pair.second = spec.transformJoints.empty () ? "Joint1" : spec.transformJoints.front ().name;
    pair.source = "Manual";
    pair.reason = "User-defined collision exclusion";
    spec.collisionSetup.excludePairs.push_back (pair);
    fillFromSpec (spec);
    generatePreview ();
}

void RobotModelBuilderWidget::removeSelectedCollisionExcludePair ()
{
    if (_collisionSetupPairsTable == NULL)
        return;
    RobotModelSpec spec = collectSpec ();
    const int row = _collisionSetupPairsTable->currentRow ();
    if (row < 0 || row >= static_cast< int >(spec.collisionSetup.excludePairs.size ()))
        return;
    spec.collisionSetup.excludePairs.erase (spec.collisionSetup.excludePairs.begin () + row);
    fillFromSpec (spec);
    generatePreview ();
}

void RobotModelBuilderWidget::generateDefaultCollisionSetup ()
{
    RobotModelSpec spec = collectSpec ();
    spec.collisionSetup.enabled = true;
    spec.collisionSetup.excludeBaseToFirstJoint = true;
    spec.collisionSetup.excludeAdjacentLinkPairs = true;
    spec.collisionSetup.excludeStaticPairs = false;
    fillFromSpec (spec);
    generatePreview ();
    setStatus ("CollisionSetup defaults enabled.");
}

// =============================================================================
//  collisionFromDrawable
//  说明: 从 Drawable 生成简化 CollisionModel。
//        * 视觉是 Cylinder/Sphere/Cone → 沿用形状(Cylinder/Cone 需要 length);
//        * 视觉是 STL / Mesh / Polytope / Plane / Unknown → 退化为 Box。
//        这样不强制用户在"试图把 STL 当碰撞"时再多填一行 Mesh 路径。
// =============================================================================
namespace {

CollisionModelSpec collisionFromDrawable (const DrawableSpec& drawable, int index)
{
    CollisionModelSpec collision;
    collision.name = drawable.name.empty ()
                         ? "CollisionModel" + std::to_string (index + 1)
                         : drawable.name + "Collision";
    collision.refFrame = drawable.refFrame;
    const GeometryKind kind = geometryKindFromString (drawable.shape);
    if (kind == GeometryKind::Cylinder || kind == GeometryKind::Sphere ||
        kind == GeometryKind::Cone) {
        collision.shape      = drawable.shape;
        collision.radius     = drawable.radius;
        collision.length     = drawable.length > 0 ? drawable.length : 0.1;
        collision.dimensions = drawable.dimensions;
    }
    else {
        collision.shape      = "Box";
        collision.dimensions = drawable.dimensions;
        collision.radius     = drawable.radius > 0 ? drawable.radius : 0.05;
        collision.length     = drawable.length > 0 ? drawable.length : 0.1;
    }
    collision.filePath.clear ();
    collision.rpyDeg = drawable.rpyDeg;
    collision.pos    = drawable.pos;
    collision.enabled = true;
    return collision;
}

}    // namespace

// =============================================================================
//  generateCollisionModelsFromDrawables()
//  说明: Milestone 5 "Generate From Drawables" 按钮:
//        清空当前 collisionModels,按 spec.drawables 顺序用 collisionFromDrawable
//        生成;Link{i}To{i+1} 这些自动 link 也参与(它们的尺寸已经按关节几何
//        填了,当 Box 碰撞用没问题)。
// =============================================================================
void RobotModelBuilderWidget::generateCollisionModelsFromDrawables ()
{
    RobotModelSpec spec = collectSpec ();
    spec.collisionModels.clear ();
    int index = 0;
    for (const DrawableSpec& drawable : spec.drawables) {
        if (!drawable.name.empty ()) {
            spec.collisionModels.push_back (collisionFromDrawable (drawable, index));
            ++index;
        }
    }
    fillFromSpec (spec);
    generatePreview ();
    setStatus (QString ("Generated %1 collision models from drawables.")
                   .arg (static_cast< int >(spec.collisionModels.size ())));
}

void RobotModelBuilderWidget::sceneGenerationToggled (bool)
{
    updateSceneUiEnabled ();
    if (!_syncingTables)
        generatePreview ();
}

// =============================================================================
//  onDhTableCellChanged()
//  说明: DH 表是 SE(3) 真值的投影视图,整表 NoEditTriggers + 单元格
//        ~ItemIsEditable 双重保护,理论上不会被用户编辑。
//        留这个槽只是为了在极端情况下(setItem 误用)给出明确提示,
//        避免用户疑惑"为什么我改了 DH 表没反应"。
// =============================================================================
void RobotModelBuilderWidget::onDhTableCellChanged (QTableWidgetItem* item)
{
    if (_syncingTables || _importingFromWorkCell || item == NULL)
        return;
    setStatus ("DH parameters are a projection view. Edit Joint + RPY + Pos to change the model.");
}

// =============================================================================
//  onTransformTableCellChanged()
//  说明: SE(3) Joint+RPY+Pos 真值表被编辑后,刷新 DH 投影视图(只读):
//          a         = sqrt(px^2 + py^2)
//          offsetDeg = atan2(py, px)
//          d         = pz
//          alphaDeg  = rpyDeg[2] (yaw)
// 投影在 pitch!=0 或 roll 与 pos.xy 方向不一致时会有损,此时仍把投影值
// 写回 DH 表(Status=Projected),并通过状态栏告知用户。
//
// 注意:这是"真值 -> 投影"的单向刷新;DH 表被整体设为只读,所以不再
// 存在反向回写 SE(3) 的路径。_syncingTables 防止 setItem 触发
// _dhTable->itemChanged 引起无谓递归。
// =============================================================================
void RobotModelBuilderWidget::onTransformTableCellChanged (QTableWidgetItem* item)
{
    if (_syncingTables || _importingFromWorkCell || item == NULL)
        return;
    const int row = item->row ();
    if (row < 0 || row >= _transformTable->rowCount ())
        return;
    if (row >= _dhTable->rowCount ())
        return;

    JointTransformSpec j;
    j.name = itemText (_transformTable, row, 0).toStdString ();
    j.type = itemText (_transformTable, row, 1).toStdString ();

    // 1) 输入校验:任一向量解析失败都中止,避免把 0 写进 DH 表导致数据损坏
    if (!parseVector3 (itemText (_transformTable, row, 2), j.rpyDeg)) {
        setStatus (QString ("Row %1: invalid RPY vector; DH row not updated.")
                       .arg (row + 1));
        return;
    }
    if (!parseVector3 (itemText (_transformTable, row, 3), j.pos)) {
        setStatus (QString ("Row %1: invalid Pos vector; DH row not updated.")
                       .arg (row + 1));
        return;
    }

    // 2) 检测有损转换;有损时仍把反推出的 DH 值写回,但明确告诉用户
    bool lossy = false;
    const DHJointSpec dh = RobotModelXmlWriter::transformJointToDh (j, &lossy);

    QString status;
    if (!isRevoluteType (j.type))
        status = "Unsupported";
    else if (lossy)
        status = "Projected";
    else
        status = "Lossless";

    if (lossy) {
        const double projectedOffset = dh.offsetDeg;
        setStatus (QString ("Row %1: RPY/Pos was projected to DH; "
                            "pitch=%2 deg, roll=%3 deg, projected offset=%4 deg, "
                            "alpha=%5 deg, a=%6 m, d=%7 m. (Status: %8)")
                       .arg (row + 1)
                       .arg (j.rpyDeg[1], 0, 'g', 4)
                       .arg (j.rpyDeg[0], 0, 'g', 4)
                       .arg (projectedOffset, 0, 'g', 4)
                       .arg (dh.alphaDeg, 0, 'g', 4)
                       .arg (dh.a, 0, 'g', 4)
                       .arg (dh.d, 0, 'g', 4)
                       .arg (status));
    }
    else {
        setStatus (QString ("Row %1 synced to DH: offset=%2°, alpha=%3°, "
                            "a=%4 m, d=%5 m. (Status: %6)")
                       .arg (row + 1)
                       .arg (dh.offsetDeg, 0, 'g', 4)
                       .arg (dh.alphaDeg, 0, 'g', 4)
                       .arg (dh.a, 0, 'g', 4)
                       .arg (dh.d, 0, 'g', 4)
                       .arg (status));
    }

    // DH 表是投影视图,所有写入都强制只读(false),避免某次 setItem
    // 把 DH 单元格重新变成可编辑(进而触发 onDhTableCellChanged)
    _syncingTables = true;
    setItem (_dhTable, row, 0, QString::fromStdString (dh.name), false);
    setItem (_dhTable, row, 1, QString::number (dh.alphaDeg), false);
    setItem (_dhTable, row, 2, QString::number (dh.a), false);
    setItem (_dhTable, row, 3, QString::number (dh.d), false);
    setItem (_dhTable, row, 4, QString::number (dh.offsetDeg), false);
    setItem (_dhTable, row, 5, status, false);
    _syncingTables = false;
}

// =============================================================================
//  fillFromSpec()
//  说明: 用 RobotModelSpec 数据完整回填整个 UI。注意 setCurrentIndex 会
//        触发 modeChanged,所以在最后再调一次以确保可见性正确。
//        持有 _syncingTables,避免 setItem 触发的 itemChanged 引发跨表回写。
// =============================================================================
void RobotModelBuilderWidget::fillFromSpec (const RobotModelSpec& spec)
{
    _syncingTables = true;
    _robotName->setText (QString::fromStdString (spec.robotName));
    // 项目模式的输出字段只允许文件名。旧 JSON/WorkCell 导入可能携带完整路径，显示和收集时
    // 都将其压平为文件名，防止路径片段经由可编辑控件重新逃逸出项目输出目录。
    const auto outputFileName = [this] (const std::string& value) {
        const QString path = QString::fromStdString (value).trimmed ();
        return _projectOutputDirectory.isEmpty () ? path : QFileInfo (path).fileName ();
    };
    _deviceFile->setText (outputFileName (spec.exportLayout.deviceFile));
    _sceneFile->setText (outputFileName (spec.exportLayout.sceneFile));
    _dynamicWorkCellFile->setText (
        outputFileName (spec.exportLayout.dynamicWorkCellFile));
    _collisionSetupFile->setText (
        outputFileName (spec.exportLayout.collisionSetupFile));
    _preserveImportedFileLayout->setChecked (
        _projectOutputDirectory.isEmpty () && spec.exportLayout.preserveImportedFileLayout);
    _preserveImportedFileLayout->setEnabled (_projectOutputDirectory.isEmpty ());
    updateOutputFilePlaceholders ();
    _mode->setCurrentIndex (spec.mode == KinematicsViewMode::DHProjection ? 1 : 0);
    _showFrameAxes->setChecked (spec.showFrameAxes);
    _generateDrawables->setChecked (spec.generateDrawables);
    _generateScene->setChecked (spec.generateScene);
    _generateDwc->setChecked (spec.dynamics.generateDynamicWorkCell);
    _exportDhAdvanced->setChecked (spec.exportDhJointsAdvanced);
    _baseFrame->setText (QString::fromStdString (spec.dynamics.baseFrame));
    _baseMaterial->setText (QString::fromStdString (spec.dynamics.baseMaterial));
    fillKinematicsTables (spec);
    fillDrawablesTable (spec);
    fillCollisionModelsTable (spec);
    fillCollisionSetupTab (spec);
    fillSceneTab (spec);
    fillSceneGeometryTable (spec);
    fillLimitsTable (spec);
    fillPosesTable (spec);
    fillDynamicsTab (spec);
    modeChanged (_mode->currentIndex ());
    updateSceneUiEnabled ();
    _syncingTables = false;
}

void RobotModelBuilderWidget::syncFromWorkCellSpec (const RobotModelSpec& spec,
                                                    const QStringList& warnings)
{
    _importingFromWorkCell = true;
    _importedDocument = spec.imported;
    fillFromSpec (spec);
    generatePreview ();
    _importingFromWorkCell = false;

    if (warnings.isEmpty ()) {
        setStatus ("Loaded WorkCell synchronized to RobotModelBuilder.");
    }
    else {
        setStatus (QString ("Loaded WorkCell synchronized with %1 warning(s).")
                       .arg (warnings.size ()));
        QMessageBox::information (this, "WorkCell Import Warnings", warnings.join ("\n"));
    }
}

// =============================================================================
//  collectSpec()
//  说明: 与 fillFromSpec 相反,把 UI 上的全部控件值收集为 RobotModelSpec。
//        这里会按列含义把表格文本解析回 double / vector。
// =============================================================================
RobotModelSpec RobotModelBuilderWidget::collectSpec () const
{
    RobotModelSpec spec;
    // 项目模式把输出布局限制为纯文件名；XmlWriter 会统一以前缀受管目录拼接这些名称，
    // 因此即使旧工程或粘贴操作提供了绝对路径、盘符或 ../，也无法让输出越出项目目录。
    const auto outputFileName = [this] (const QLineEdit* field) {
        const QString value = field->text ().trimmed ();
        return (_projectOutputDirectory.isEmpty () ? value : QFileInfo (value).fileName ())
            .toStdString ();
    };
    spec.imported = _importedDocument;
    spec.exportLayout.deviceFile = outputFileName (_deviceFile);
    spec.exportLayout.sceneFile = outputFileName (_sceneFile);
    spec.exportLayout.dynamicWorkCellFile = outputFileName (_dynamicWorkCellFile);
    spec.exportLayout.collisionSetupFile = outputFileName (_collisionSetupFile);
    spec.exportLayout.preserveImportedFileLayout =
        _projectOutputDirectory.isEmpty () && _preserveImportedFileLayout->isChecked ();
    spec.robotName         = _robotName->text ().toStdString ();
    spec.saveDirectory     = effectiveSaveDirectory ().toStdString ();
    spec.mode              = _mode->currentIndex () == 1 ? KinematicsViewMode::DHProjection
                                                          : KinematicsViewMode::JointRPYPos;
    spec.exportDhJointsAdvanced = _exportDhAdvanced->isChecked ();
    spec.showFrameAxes     = _showFrameAxes->isChecked ();
    spec.generateDrawables = _generateDrawables->isChecked ();
    spec.generateScene     = _generateScene->isChecked ();
    spec.dynamics.generateDynamicWorkCell = _generateDwc->isChecked ();
    spec.dynamics.baseFrame    = _baseFrame->text ().toStdString ();
    spec.dynamics.baseMaterial = _baseMaterial->text ().toStdString ();
    if (_collisionSetupEnabled != NULL)
        spec.collisionSetup.enabled = _collisionSetupEnabled->isChecked ();
    if (_excludeBaseFirst != NULL)
        spec.collisionSetup.excludeBaseToFirstJoint = _excludeBaseFirst->isChecked ();
    if (_excludeAdjacent != NULL)
        spec.collisionSetup.excludeAdjacentLinkPairs = _excludeAdjacent->isChecked ();
    if (_excludeStatic != NULL)
        spec.collisionSetup.excludeStaticPairs = _excludeStatic->isChecked ();

    // ---- DH 关节表 ----
    for (int row = 0; row < _dhTable->rowCount (); ++row) {
        DHJointSpec joint;
        joint.name      = itemText (_dhTable, row, 0).toStdString ();
        joint.alphaDeg  = itemDouble (_dhTable, row, 1);
        joint.a         = itemDouble (_dhTable, row, 2);
        joint.d         = itemDouble (_dhTable, row, 3);
        joint.offsetDeg = itemDouble (_dhTable, row, 4);
        spec.dhJoints.push_back (joint);
    }

    // ---- Joint+RPY+Pos 表 ----
    for (int row = 0; row < _transformTable->rowCount (); ++row) {
        JointTransformSpec joint;
        joint.name = itemText (_transformTable, row, 0).toStdString ();
        joint.type = itemText (_transformTable, row, 1).toStdString ();
        parseVector3 (itemText (_transformTable, row, 2), joint.rpyDeg);
        parseVector3 (itemText (_transformTable, row, 3), joint.pos);
        spec.transformJoints.push_back (joint);
    }

    // ---- Drawables 表(Milestone 4:新增 Dimensions/File 两列,共 11 列)----
    for (int row = 0; row < _drawablesTable->rowCount (); ++row) {
        DrawableSpec drawable;
        drawable.name       = itemText (_drawablesTable, row, 0).toStdString ();
        drawable.refFrame  = itemText (_drawablesTable, row, 1).toStdString ();
        drawable.shape     = itemText (_drawablesTable, row, 2).toStdString ();
        parseVector3 (itemText (_drawablesTable, row, 3), drawable.dimensions);
        drawable.radius    = itemDouble (_drawablesTable, row, 4);
        drawable.length    = itemDouble (_drawablesTable, row, 5);
        drawable.filePath  = itemText (_drawablesTable, row, 6).toStdString ();
        parseVector3 (itemText (_drawablesTable, row, 7), drawable.rpyDeg);
        parseVector3 (itemText (_drawablesTable, row, 8), drawable.pos);
        parseVector3 (itemText (_drawablesTable, row, 9), drawable.rgb);
        const QTableWidgetItem* nameItem = _drawablesTable->item (row, 0);
        drawable.autoGenerated = nameItem != NULL &&
                                 nameItem->data (Qt::UserRole).toBool ();
        // 自动生成的 Link{i}To{i+1} Drawable 在保存前会被 applyLinkGeometry 覆写
        drawable.autoLinkGeometry =
            isAutoLinkDrawable (QString::fromStdString (drawable.name));
        spec.drawables.push_back (drawable);
    }

    // ---- Collision Models 表(Milestone 5:独立碰撞几何)----
    for (int row = 0; row < _collisionModelsTable->rowCount (); ++row) {
        CollisionModelSpec collision;
        collision.enabled = itemText (_collisionModelsTable, row, 0) == "Enabled";
        collision.name = itemText (_collisionModelsTable, row, 1).toStdString ();
        collision.refFrame = itemText (_collisionModelsTable, row, 2).toStdString ();
        collision.shape = itemText (_collisionModelsTable, row, 3).toStdString ();
        parseCollisionSize (itemText (_collisionModelsTable, row, 4), collision);
        collision.filePath = itemText (_collisionModelsTable, row, 5).toStdString ();
        parseCollisionPose (itemText (_collisionModelsTable, row, 6), collision);
        spec.collisionModels.push_back (collision);
    }

    if (_collisionSetupPairsTable != NULL) {
        spec.collisionSetup.excludePairs.clear ();
        for (int row = 0; row < _collisionSetupPairsTable->rowCount (); ++row) {
            FramePairSpec pair;
            pair.enabled = itemText (_collisionSetupPairsTable, row, 0) == "Enabled";
            pair.first = itemText (_collisionSetupPairsTable, row, 1).toStdString ();
            pair.second = itemText (_collisionSetupPairsTable, row, 2).toStdString ();
            pair.source = itemText (_collisionSetupPairsTable, row, 3).toStdString ();
            pair.reason = itemText (_collisionSetupPairsTable, row, 4).toStdString ();
            spec.collisionSetup.excludePairs.push_back (pair);
        }
    }

    // ---- Limits 表 ----
    for (int row = 0; row < _limitsTable->rowCount (); ++row) {
        JointLimitSpec limit;
        limit.jointName = itemText (_limitsTable, row, 0).toStdString ();
        limit.posMin    = itemDouble (_limitsTable, row, 1);
        limit.posMax    = itemDouble (_limitsTable, row, 2);
        limit.velMax    = itemDouble (_limitsTable, row, 3);
        limit.accMax    = itemDouble (_limitsTable, row, 4);
        spec.limits.push_back (limit);
    }

    // ---- Milestone 3:Scene Frames(RobotBase + 场景 frame 列表)----
    spec.robotBaseFrame.name      = "RobotBase";
    spec.robotBaseFrame.refFrame  = "WORLD";
    spec.robotBaseFrame.frameType = SceneFrameType::Fixed;
    spec.robotBaseFrame.daf       = false;
    spec.robotBaseFrame.poseMode  = PoseMode::RPYPos;
    parseVector3 (_robotBaseRpy->text (), spec.robotBaseFrame.rpyDeg);
    parseVector3 (_robotBasePos->text (), spec.robotBaseFrame.pos);

    for (int row = 0; row < _sceneFramesTable->rowCount (); ++row) {
        FrameSpec frame;
        frame.name      = itemText (_sceneFramesTable, row, 0).toStdString ();
        frame.refFrame  = itemText (_sceneFramesTable, row, 1).toStdString ();
        frame.frameType = sceneFrameTypeFromString (
            itemText (_sceneFramesTable, row, 2).toStdString ());
        frame.daf = itemChecked (_sceneFramesTable, row, 3);
        frame.poseMode = poseModeFromString (
            itemText (_sceneFramesTable, row, 4).toStdString ());
        parseVector3 (itemText (_sceneFramesTable, row, 5), frame.rpyDeg);
        parseVector3 (itemText (_sceneFramesTable, row, 6), frame.pos);
        parseVector16 (itemText (_sceneFramesTable, row, 7), frame.transform);
        spec.sceneFrames.push_back (frame);
    }

    // ---- Milestone 3.5:场景几何体收集 ----
    for (int row = 0; row < _sceneGeometryTable->rowCount (); ++row) {
        SceneGeometrySpec geometry;
        geometry.name     = itemText (_sceneGeometryTable, row, 0).toStdString ();
        geometry.refFrame = itemText (_sceneGeometryTable, row, 1).toStdString ();
        geometry.kind     = geometryKindFromString (
            itemText (_sceneGeometryTable, row, 2).toStdString ());
        parseVector3 (itemText (_sceneGeometryTable, row, 3), geometry.size);
        geometry.radius   = itemDouble (_sceneGeometryTable, row, 4);
        geometry.length   = itemDouble (_sceneGeometryTable, row, 5);
        geometry.file     = itemText (_sceneGeometryTable, row, 6).toStdString ();
        parseVector3 (itemText (_sceneGeometryTable, row, 7), geometry.rpyDeg);
        parseVector3 (itemText (_sceneGeometryTable, row, 8), geometry.pos);
        parseVector3 (itemText (_sceneGeometryTable, row, 9), geometry.rgb);
        geometry.collisionModel = itemChecked (_sceneGeometryTable, row, 10);
        spec.sceneGeometries.push_back (geometry);
    }

    // ---- Poses 表(Milestone 2:q 长度 = 当前表列数 - 1)----
    for (int row = 0; row < _posesTable->rowCount (); ++row) {
        PoseSpec pose;
        pose.name = itemText (_posesTable, row, 0).toStdString ();
        for (int col = 1; col < _posesTable->columnCount (); ++col)
            pose.q.push_back (itemDouble (_posesTable, row, col));
        spec.poses.push_back (pose);
    }

    // ---- Dynamics 链接表 ----
    for (int row = 0; row < _dynamicsLinksTable->rowCount (); ++row) {
        LinkDynamicsSpec link;
        link.linkName      = itemText (_dynamicsLinksTable, row, 0).toStdString ();
        link.objectName    = itemText (_dynamicsLinksTable, row, 1).toStdString ();
        link.mass          = itemDouble (_dynamicsLinksTable, row, 2);
        parseVector3 (itemText (_dynamicsLinksTable, row, 3), link.cog);
        parseVector6 (itemText (_dynamicsLinksTable, row, 4), link.inertia);
        link.estimateInertia = itemChecked (_dynamicsLinksTable, row, 5);
        link.material = itemText (_dynamicsLinksTable, row, 6).toStdString ();
        spec.dynamics.links.push_back (link);
    }

    // ---- 力限表 ----
    for (int row = 0; row < _forceLimitsTable->rowCount (); ++row) {
        JointForceLimitSpec fl;
        fl.jointName = itemText (_forceLimitsTable, row, 0).toStdString ();
        fl.maxForce  = itemDouble (_forceLimitsTable, row, 1);
        spec.dynamics.forceLimits.push_back (fl);
    }

    return spec;
}

// =============================================================================
//  validateTableInput()
//  说明: 在调用 XmlWriter.validate 之前,先做一轮纯 UI 输入格式校验,
//        用来尽早把"输入框里写了非法字符"等问题拦截下来,避免
//        错误信息到达底层后变得难以理解。规则:
//          - DH/Limits/Poses/ForceLimits 从第 1 列起必须是 double
//          - Transform/Drawables/Dynamics 中标了"x y z"或"x1..x6"的列
//            必须能拆成对应数量的 double
//        若对应功能未启用(如未勾选 DWC),相关表会被跳过。
// =============================================================================
bool RobotModelBuilderWidget::validateTableInput (QStringList& errors) const
{
    errors.clear ();
    QTableWidget* numericTables[] = {NULL, _limitsTable, _posesTable,
                                     _generateDwc->isChecked () ? _forceLimitsTable : NULL};
    const int numericStartCols[]  = {1, 1, 1, 1};
    for (int t = 0; t < 4; ++t) {
        QTableWidget* table = numericTables[t];
        if (table == NULL)
            continue;
        for (int row = 0; row < table->rowCount (); ++row) {
            for (int col = numericStartCols[t]; col < table->columnCount (); ++col) {
                if (!parseDouble (itemText (table, row, col)))
                    errors << QString ("Invalid number at row %1 column %2.")
                                  .arg (row + 1)
                                  .arg (table->horizontalHeaderItem (col)->text ());
            }
        }
    }

    for (int row = 0; row < _transformTable->rowCount (); ++row) {
        if (!parseVector (itemText (_transformTable, row, 2), 3))
            errors << QString ("Invalid RPY vector at joint row %1.").arg (row + 1);
        if (!parseVector (itemText (_transformTable, row, 3), 3))
            errors << QString ("Invalid Pos vector at joint row %1.").arg (row + 1);
    }

    if (_generateDrawables->isChecked ()) {
        for (int row = 0; row < _drawablesTable->rowCount (); ++row) {
            if (!parseVector (itemText (_drawablesTable, row, 3), 3))
                errors << QString ("Invalid drawable dimensions vector at row %1.").arg (row + 1);
            if (!parseDouble (itemText (_drawablesTable, row, 4)))
                errors << QString ("Invalid drawable radius at row %1.").arg (row + 1);
            if (!parseDouble (itemText (_drawablesTable, row, 5)))
                errors << QString ("Invalid drawable length at row %1.").arg (row + 1);
            if (!parseVector (itemText (_drawablesTable, row, 7), 3))
                errors << QString ("Invalid drawable RPY vector at row %1.").arg (row + 1);
            if (!parseVector (itemText (_drawablesTable, row, 8), 3))
                errors << QString ("Invalid drawable Pos vector at row %1.").arg (row + 1);
            if (!parseVector (itemText (_drawablesTable, row, 9), 3))
                errors << QString ("Invalid drawable RGB vector at row %1.").arg (row + 1);
        }
    }

    // ---- Milestone 5:Collision Models 表输入校验(独立于 generateDrawables)----
    for (int row = 0; row < _collisionModelsTable->rowCount (); ++row) {
        CollisionModelSpec collision;
        collision.shape = itemText (_collisionModelsTable, row, 3).toStdString ();
        if (!parseCollisionSize (itemText (_collisionModelsTable, row, 4), collision))
            errors << QString ("Invalid collision model size at row %1.").arg (row + 1);
        if (!parseCollisionPose (itemText (_collisionModelsTable, row, 6), collision))
            errors << QString ("Invalid collision model pose at row %1.").arg (row + 1);
    }

    // ---- Milestone 3:Scene Frames 输入校验 ----
    if (_robotBaseRpy != NULL &&
        !parseVector (_robotBaseRpy->text (), 3))
        errors << "Invalid RobotBase RPY vector.";
    if (_robotBasePos != NULL &&
        !parseVector (_robotBasePos->text (), 3))
        errors << "Invalid RobotBase Pos vector.";
    if (_sceneFramesTable != NULL) {
        for (int row = 0; row < _sceneFramesTable->rowCount (); ++row) {
            if (!parseVector (itemText (_sceneFramesTable, row, 5), 3))
                errors << QString ("Invalid scene frame RPY vector at row %1.").arg (row + 1);
            if (!parseVector (itemText (_sceneFramesTable, row, 6), 3))
                errors << QString ("Invalid scene frame Pos vector at row %1.").arg (row + 1);
            // Milestone 3.5 follow-up:Transform 4x4 仅在 PoseMode=Transform4x4
            // (或别名 Transform)时才校验 16 个数;RPYPos 模式下允许占位值,
            // 避免用户在用 RPY/Pos 时被强制输入 16 个数。
            const QString poseMode = itemText (_sceneFramesTable, row, 4);
            if (poseMode.compare ("Transform4x4", Qt::CaseInsensitive) == 0 ||
                poseMode.compare ("Transform", Qt::CaseInsensitive) == 0) {
                if (!parseVector (itemText (_sceneFramesTable, row, 7), 16))
                    errors << QString ("Invalid scene frame Transform vector at row %1.")
                                  .arg (row + 1);
            }
        }
    }

    if (_generateDwc->isChecked ()) {
        for (int row = 0; row < _dynamicsLinksTable->rowCount (); ++row) {
            if (!parseDouble (itemText (_dynamicsLinksTable, row, 2)))
                errors << QString ("Invalid mass at dynamics link row %1.").arg (row + 1);
            if (!parseVector (itemText (_dynamicsLinksTable, row, 3), 3))
                errors << QString ("Invalid COG vector at dynamics link row %1.").arg (row + 1);
            if (!parseVector (itemText (_dynamicsLinksTable, row, 4), 6))
                errors << QString ("Invalid inertia vector at dynamics link row %1.").arg (row + 1);
        }
    }

    return errors.isEmpty ();
}

// =============================================================================
//  fillKinematicsTables() / fillDrawablesTable() / fillLimitsTable() /
//  fillPosesTable() / fillDynamicsTab()
//  说明: 这 5 个函数都是"用 spec 数据回填 UI 子表",逻辑基本一致:
//          - 先按 spec 调整表格行数;
//          - 再按列把数据写入对应单元格;
//        Drawables 表对 "Link{i}To{i+1}" 这种自动生成的圆柱,把除
//        Radius/RGB/Collision 之外的列锁为只读,避免用户误改后又被
//        applyLinkGeometry 覆盖。
// =============================================================================
void RobotModelBuilderWidget::fillKinematicsTables (const RobotModelSpec& spec)
{
    const int n = static_cast< int >(spec.transformJoints.size ());
    _dhTable->setRowCount (n);
    _transformTable->setRowCount (n);
    for (int row = 0; row < n; ++row) {
        const JointTransformSpec& joint = spec.transformJoints[row];
        // 把 SE(3) 真值投影成 DH;有损时仍写出投影值,但在 Status 列标记
        bool lossy = false;
        const DHJointSpec dh = RobotModelXmlWriter::transformJointToDh (joint, &lossy);
        const QString jt = QString::fromStdString (joint.type).trimmed ();
        // Status 列语义:
        //   * Revolute + 无损 -> "Lossless"   (可被高级 <DHJoint> 导出)
        //   * Revolute + 有损 -> "Projected"  (高级 <DHJoint> 导出将被拒绝)
        //   * Prismatic      -> "Projected"  (DH 仍能投影 d,但不能表达 theta)
        //   * FixedFrame     -> "Unsupported"(DH 表对它无意义)
        //   * ToolFrame      -> "Unsupported"
        QString status;
        if (jt.compare ("Revolute", Qt::CaseInsensitive) == 0)
            status = lossy ? "Projected" : "Lossless";
        else if (jt.compare ("Prismatic", Qt::CaseInsensitive) == 0)
            status = "Projected";
        else
            status = "Unsupported";

        // DH 表全部只读
        setItem (_dhTable, row, 0, QString::fromStdString (dh.name), false);
        setItem (_dhTable, row, 1, QString::number (dh.alphaDeg), false);
        setItem (_dhTable, row, 2, QString::number (dh.a), false);
        setItem (_dhTable, row, 3, QString::number (dh.d), false);
        setItem (_dhTable, row, 4, QString::number (dh.offsetDeg), false);
        setItem (_dhTable, row, 5, status, false);

        setItem (_transformTable, row, 0, QString::fromStdString (joint.name));
        setCombo (_transformTable, row, 1,
                  QStringList () << "Revolute" << "Prismatic" << "FixedFrame" << "ToolFrame",
                  QString::fromStdString (joint.type));
        setItem (_transformTable, row, 2, vectorText (joint.rpyDeg));
        setItem (_transformTable, row, 3, vectorText (joint.pos));
    }
}

void RobotModelBuilderWidget::fillDrawablesTable (const RobotModelSpec& spec)
{
    _drawablesTable->setRowCount (static_cast< int > (spec.drawables.size ()));
    for (int row = 0; row < _drawablesTable->rowCount (); ++row) {
        const DrawableSpec& drawable = spec.drawables[row];
        const bool autoLink = isAutoLinkDrawable (QString::fromStdString (drawable.name));
        // 自动生成的连杆圆柱:名称/参考系/形状/姿态/位置/长度 都由
        // applyLinkGeometry 维护,所以在 UI 上锁为只读;radius/RGB/Collision
        // 仍可手动覆盖。Milestone 4 起共 11 列,新增 Dimensions/File。
        setItem (_drawablesTable, row, 0, QString::fromStdString (drawable.name), !autoLink);
        _drawablesTable->item (row, 0)->setData (Qt::UserRole, drawable.autoGenerated);
        setCombo (_drawablesTable, row, 1, deviceFrameChoices (spec),
                  QString::fromStdString (drawable.refFrame), !autoLink);
        setShapeCombo (_drawablesTable, row, 2,
                       QString::fromStdString (drawable.shape), !autoLink);
        const QString shape = QString::fromStdString (drawable.shape);
        setItem (_drawablesTable, row, 3, vectorText (drawable.dimensions),
                 drawableColumnEditableForShape (shape, 3, autoLink));
        setItem (_drawablesTable, row, 4, QString::number (drawable.radius),
                 drawableColumnEditableForShape (shape, 4, autoLink));
        setItem (_drawablesTable, row, 5, QString::number (drawable.length),
                 drawableColumnEditableForShape (shape, 5, autoLink));
        setItem (_drawablesTable, row, 6, QString::fromStdString (drawable.filePath),
                 drawableColumnEditableForShape (shape, 6, autoLink));
        setItem (_drawablesTable, row, 7, vectorText (drawable.rpyDeg), !autoLink);
        setItem (_drawablesTable, row, 8, vectorText (drawable.pos), !autoLink);
        setItem (_drawablesTable, row, 9, vectorText (drawable.rgb));
    }
}

// =============================================================================
//  fillCollisionModelsTable()
//  说明: Milestone 5 — 把 spec.collisionModels 回填到 Collision Models 表。
//        第 2 列(Shape)用 setCombo 给 6 种合法形状,不再用 setShapeCombo
//        (后者多带 Plane/STL/8 项);其它列按 collisionColumnEditableForShape 解锁。
// =============================================================================
void RobotModelBuilderWidget::fillCollisionModelsTable (const RobotModelSpec& spec)
{
    if (_collisionModelsTable == NULL)
        return;
    _collisionModelsTable->setRowCount (static_cast< int > (spec.collisionModels.size ()));
    for (int row = 0; row < _collisionModelsTable->rowCount (); ++row) {
        const CollisionModelSpec& collision = spec.collisionModels[row];
        const QString shape = QString::fromStdString (collision.shape);
        setCombo (_collisionModelsTable, row, 0, QStringList () << "Enabled" << "Disabled",
                  collision.enabled ? "Enabled" : "Disabled");
        setItem (_collisionModelsTable, row, 1, QString::fromStdString (collision.name));
        setCombo (_collisionModelsTable, row, 2, deviceFrameChoices (spec),
                  QString::fromStdString (collision.refFrame));
        setCollisionShapeCombo (_collisionModelsTable, row, 3, shape);
        setItem (_collisionModelsTable, row, 4, collisionSizeText (collision),
                 collisionColumnEditableForShape (shape, 4));
        setItem (_collisionModelsTable, row, 5, QString::fromStdString (collision.filePath),
                 collisionColumnEditableForShape (shape, 5));
        setItem (_collisionModelsTable, row, 6, collisionPoseText (collision));
    }
}

void RobotModelBuilderWidget::fillCollisionSetupTab (const RobotModelSpec& spec)
{
    if (_collisionSetupEnabled != NULL)
        _collisionSetupEnabled->setChecked (spec.collisionSetup.enabled);
    if (_excludeBaseFirst != NULL)
        _excludeBaseFirst->setChecked (spec.collisionSetup.excludeBaseToFirstJoint);
    if (_excludeAdjacent != NULL)
        _excludeAdjacent->setChecked (spec.collisionSetup.excludeAdjacentLinkPairs);
    if (_excludeStatic != NULL)
        _excludeStatic->setChecked (spec.collisionSetup.excludeStaticPairs);

    if (_collisionSetupPairsTable == NULL)
        return;
    _collisionSetupPairsTable->setRowCount (
        static_cast< int > (spec.collisionSetup.excludePairs.size ()));
    for (int row = 0; row < _collisionSetupPairsTable->rowCount (); ++row) {
        const FramePairSpec& pair = spec.collisionSetup.excludePairs[row];
        setCombo (_collisionSetupPairsTable, row, 0, QStringList () << "Enabled" << "Disabled",
                  pair.enabled ? "Enabled" : "Disabled");
        setCombo (_collisionSetupPairsTable, row, 1, collisionFrameChoices (spec),
                  QString::fromStdString (pair.first));
        setCombo (_collisionSetupPairsTable, row, 2, collisionFrameChoices (spec),
                  QString::fromStdString (pair.second));
        setCombo (_collisionSetupPairsTable, row, 3,
                  QStringList () << "Manual" << "Auto" << "Imported",
                  QString::fromStdString (pair.source));
        setItem (_collisionSetupPairsTable, row, 4, QString::fromStdString (pair.reason));
    }
}

void RobotModelBuilderWidget::chooseGeometryFile (QTableWidget* table, int row, int column)
{
    const QString path = QFileDialog::getOpenFileName (
        this, "Choose geometry file", itemText (table, row, column),
        "Geometry files (*.stl *.obj *.dae *.wrl *.iv);;All files (*)");
    if (path.isEmpty ())
        return;
    setItem (table, row, column, QDir::fromNativeSeparators (path));
    generatePreview ();
}

void RobotModelBuilderWidget::synchronizeCollisionFileFromDrawable (int row)
{
    if (row < 0 || row >= _collisionModelsTable->rowCount ())
        return;
    const GeometryKind kind = geometryKindFromString (
        itemText (_collisionModelsTable, row, 3).toStdString ());
    if (kind != GeometryKind::STL && kind != GeometryKind::Mesh &&
        kind != GeometryKind::Polytope)
        return;
    const QString name = itemText (_collisionModelsTable, row, 1);
    const QString refFrame = itemText (_collisionModelsTable, row, 2);
    QString file;
    for (int drawableRow = 0; drawableRow < _drawablesTable->rowCount (); ++drawableRow) {
        if (itemText (_drawablesTable, drawableRow, 0) == name) {
            file = itemText (_drawablesTable, drawableRow, 6);
            break;
        }
    }
    if (file.isEmpty ()) {
        for (int drawableRow = 0; drawableRow < _drawablesTable->rowCount (); ++drawableRow) {
            if (itemText (_drawablesTable, drawableRow, 1) == refFrame) {
                file = itemText (_drawablesTable, drawableRow, 6);
                break;
            }
        }
    }
    if (!file.isEmpty ())
        setItem (_collisionModelsTable, row, 5, file);
}

void RobotModelBuilderWidget::fillLimitsTable (const RobotModelSpec& spec)
{
    const int n = static_cast< int >(spec.limits.size ());
    _limitsTable->setRowCount (n);
    for (int row = 0; row < n; ++row) {
        const JointLimitSpec& limit = spec.limits[row];
        setCombo (_limitsTable, row, 0, movableJointChoices (spec),
                  QString::fromStdString (limit.jointName));
        setItem (_limitsTable, row, 1, QString::number (limit.posMin));
        setItem (_limitsTable, row, 2, QString::number (limit.posMax));
        setItem (_limitsTable, row, 3, QString::number (limit.velMax));
        setItem (_limitsTable, row, 4, QString::number (limit.accMax));
    }
}

void RobotModelBuilderWidget::fillPosesTable (const RobotModelSpec& spec)
{
    // Milestone 2:列数 = 1 (name) + 可动关节数 (q 长度)
    int movable = 0;
    for (const JointTransformSpec& j : spec.transformJoints) {
        const QString t = QString::fromStdString (j.type).trimmed ();
        if (t.compare ("Revolute", Qt::CaseInsensitive) == 0 ||
            t.compare ("Prismatic", Qt::CaseInsensitive) == 0)
            ++movable;
    }
    if (movable == 0)
        movable = 1;    // 至少 1 个 q 列,避免空表
    QStringList headers;
    headers << "Name";
    for (int i = 1; i <= movable; ++i)
        headers << "q" + QString::number (i);
    _posesTable->setColumnCount (headers.size ());
    _posesTable->setHorizontalHeaderLabels (headers);

    _posesTable->setRowCount (static_cast< int > (spec.poses.size ()));
    for (int row = 0; row < _posesTable->rowCount (); ++row) {
        const PoseSpec& pose = spec.poses[row];
        setItem (_posesTable, row, 0, QString::fromStdString (pose.name));
        for (int i = 0; i < movable; ++i) {
            const double v = i < static_cast< int >(pose.q.size ()) ? pose.q[i] : 0.0;
            setItem (_posesTable, row, i + 1, QString::number (v));
        }
    }
}

void RobotModelBuilderWidget::fillDynamicsTab (const RobotModelSpec& spec)
{
    const int nLinks = static_cast< int >(spec.dynamics.links.size ());
    const int nForce = static_cast< int >(spec.dynamics.forceLimits.size ());
    _dynamicsLinksTable->setRowCount (nLinks);
    _forceLimitsTable->setRowCount (nForce);
    for (int row = 0; row < nLinks; ++row) {
        const LinkDynamicsSpec& link = spec.dynamics.links[row];
        setItem (_dynamicsLinksTable, row, 0, QString::fromStdString (link.linkName));
        setCombo (_dynamicsLinksTable, row, 1, movableJointChoices (spec),
                  QString::fromStdString (link.objectName));
        setItem (_dynamicsLinksTable, row, 2, QString::number (link.mass));
        setItem (_dynamicsLinksTable, row, 3, vectorText (link.cog));
        setItem (_dynamicsLinksTable, row, 4, vectorText6 (link.inertia));
        setCheckBox (_dynamicsLinksTable, row, 5, link.estimateInertia);
        setItem (_dynamicsLinksTable, row, 6, QString::fromStdString (link.material));
    }
    for (int row = 0; row < nForce; ++row) {
        const JointForceLimitSpec& fl = spec.dynamics.forceLimits[row];
        setCombo (_forceLimitsTable, row, 0, movableJointChoices (spec),
                  QString::fromStdString (fl.jointName));
        setItem (_forceLimitsTable, row, 1, QString::number (fl.maxForce));
    }
}

// =============================================================================
//  fillSceneTab()
//  说明: Milestone 3 — 用 spec 回填 Scene Frames 标签页:
//         - 顶部两个 QLineEdit 上写 RobotBase RPY / Pos;
//         - 表格逐行写 name / refFrame / type / daf / poseMode /
//           rpyDeg / pos / transform 4x4。
//        表格里 PoseMode=RPYPos 的行只填 RPY/Pos 列,UI 上同时显示
//        Transform 4x4(默认 4x4 单位阵,不影响 spec 数据)。
// =============================================================================
void RobotModelBuilderWidget::fillSceneTab (const RobotModelSpec& spec)
{
    if (_robotBaseRpy != NULL)
        _robotBaseRpy->setText (vectorText (spec.robotBaseFrame.rpyDeg));
    if (_robotBasePos != NULL)
        _robotBasePos->setText (vectorText (spec.robotBaseFrame.pos));

    if (_sceneFramesTable == NULL)
        return;
    _sceneFramesTable->setRowCount (static_cast< int >(spec.sceneFrames.size ()));
    for (int row = 0; row < _sceneFramesTable->rowCount (); ++row) {
        const FrameSpec& frame = spec.sceneFrames[row];
        setItem (_sceneFramesTable, row, 0, QString::fromStdString (frame.name));
        setCombo (_sceneFramesTable, row, 1, sceneFrameChoices (spec, row),
                  QString::fromStdString (frame.refFrame));
        setCombo (_sceneFramesTable, row, 2, QStringList () << "Fixed" << "Movable" << "Normal",
                  sceneFrameTypeToString (frame.frameType));
        setCheckBox (_sceneFramesTable, row, 3, frame.daf);
        setCombo (_sceneFramesTable, row, 4, QStringList () << "RPYPos" << "Transform4x4",
                  poseModeToString (frame.poseMode));
        setItem (_sceneFramesTable, row, 5, vectorText (frame.rpyDeg));
        setItem (_sceneFramesTable, row, 6, vectorText (frame.pos));
        setItem (_sceneFramesTable, row, 7, vectorText16 (frame.transform));
    }
}

// =============================================================================
//  fillSceneGeometryTable()
//  说明: Milestone 3.5 — 把 spec.sceneGeometries 回填到 Scene Geometry 表。
//        每行 11 列对应 SceneGeometrySpec 的字段;数字列按几何形状
//        可同时填(Box 表 size,Cylinder/Cone 表 radius/length,...),
//        多余字段在 UI 保持也无所谓,Writer 只读需要的。
// =============================================================================
void RobotModelBuilderWidget::fillSceneGeometryTable (const RobotModelSpec& spec)
{
    if (_sceneGeometryTable == NULL)
        return;
    _sceneGeometryTable->setRowCount (static_cast< int >(spec.sceneGeometries.size ()));
    for (int row = 0; row < _sceneGeometryTable->rowCount (); ++row) {
        const SceneGeometrySpec& geometry = spec.sceneGeometries[row];
        setItem (_sceneGeometryTable, row, 0, QString::fromStdString (geometry.name));
        setCombo (_sceneGeometryTable, row, 1, sceneGeometryFrameChoices (spec),
                  QString::fromStdString (geometry.refFrame));
        setCombo (_sceneGeometryTable, row, 2,
                  QStringList () << "Box" << "Cylinder" << "Sphere" << "Cone"
                                 << "Plane" << "STL" << "Mesh" << "Polytope",
                  geometryKindToString (geometry.kind));
        setItem (_sceneGeometryTable, row, 3, vectorText (geometry.size));
        setItem (_sceneGeometryTable, row, 4, QString::number (geometry.radius));
        setItem (_sceneGeometryTable, row, 5, QString::number (geometry.length));
        setItem (_sceneGeometryTable, row, 6, QString::fromStdString (geometry.file));
        setItem (_sceneGeometryTable, row, 7, vectorText (geometry.rpyDeg));
        setItem (_sceneGeometryTable, row, 8, vectorText (geometry.pos));
        setItem (_sceneGeometryTable, row, 9, vectorText (geometry.rgb));
        setCheckBox (_sceneGeometryTable, row, 10, geometry.collisionModel);
    }
}

void RobotModelBuilderWidget::updateSceneUiEnabled ()
{
    const bool enabled = _generateScene != NULL && _generateScene->isChecked ();
    if (_sceneContent != NULL)
        _sceneContent->setEnabled (enabled);
    if (_previewTabs != NULL) {
        const int index = _previewTabs->indexOf (_scenePreview);
        if (index >= 0) {
            _previewTabs->setTabEnabled (index, enabled);
            if (!enabled && _previewTabs->currentIndex () == index)
                _previewTabs->setCurrentIndex (0);
        }
    }
}

// =============================================================================
//  showErrors()
//  说明: 把错误列表弹窗 + 在状态栏显示第一条
// =============================================================================
void RobotModelBuilderWidget::showErrors (const QStringList& errors)
{
    const QString message = errors.join ("\n");
    setStatus (errors.isEmpty () ? QString () : errors.first ());
    QMessageBox::warning (this, "RobotModelBuilder", message);
}

// =============================================================================
//  setStatus()
//  说明: 设置底部状态栏文本
// =============================================================================
void RobotModelBuilderWidget::setStatus (const QString& message)
{
    _status->setText (message);
}

// =============================================================================
//  静态小工具
// =============================================================================

/// 安全读取单元格文本(自动 trim,空指针返回空串)
/// Milestone 4 起:若单元格挂了 QComboBox(Milestone 4 Drawables Shape 列),
/// 优先返回 combo 的当前文本(因为 setShapeCombo 把显示文本走 widget,
/// QTableWidgetItem 的文本是空)。
QString RobotModelBuilderWidget::itemText (const QTableWidget* table, int row, int column)
{
    if (table->cellWidget (row, column) != NULL) {
        if (QComboBox* combo = qobject_cast< QComboBox* > (table->cellWidget (row, column)))
            return combo->currentText ().trimmed ();
    }
    const QTableWidgetItem* item = table->item (row, column);
    return item == NULL ? QString () : item->text ().trimmed ();
}

/// 读取单元格并转 double(失败返回 0.0)
double RobotModelBuilderWidget::itemDouble (const QTableWidget* table, int row, int column)
{
    return itemText (table, row, column).toDouble ();
}

/// 解析 "x y z" -> std::array<double, 3>;解析失败返回 false
bool RobotModelBuilderWidget::parseVector3 (const QString& text, std::array< double, 3 >& values)
{
    const QStringList parts = text.split (QRegularExpression ("\\s+"), Qt::SkipEmptyParts);
    if (parts.size () != 3)
        return false;
    for (int i = 0; i < 3; ++i) {
        bool ok = false;
        values[i] = parts[i].toDouble (&ok);
        if (!ok)
            return false;
    }
    return true;
}

/// 解析 "x1 x2 ... x6" -> std::array<double, 6>;解析失败返回 false
bool RobotModelBuilderWidget::parseVector6 (const QString& text, std::array< double, 6 >& values)
{
    const QStringList parts = text.split (QRegularExpression ("\\s+"), Qt::SkipEmptyParts);
    if (parts.size () != 6)
        return false;
    for (int i = 0; i < 6; ++i) {
        bool ok = false;
        values[i] = parts[i].toDouble (&ok);
        if (!ok)
            return false;
    }
    return true;
}

/// 设置单元格文本,可选是否可编辑
void RobotModelBuilderWidget::setItem (QTableWidget* table, int row, int column,
                                       const QString& value, bool editable)
{
    QTableWidgetItem* item = new QTableWidgetItem (value);
    if (!editable)
        item->setFlags (item->flags () & ~Qt::ItemIsEditable);
    table->setItem (row, column, item);
}

/// 判断 Drawable 名是否形如 "Link1To2"(自动连杆几何),用于决定是否锁列
bool RobotModelBuilderWidget::isAutoLinkDrawable (const QString& name)
{
    return QRegularExpression ("^Link\\d+To\\d+$").match (name).hasMatch ();
}

/// std::array<double,3> -> "x y z"
QString RobotModelBuilderWidget::vectorText (const std::array< double, 3 >& values)
{
    return QString::number (values[0]) + " " + QString::number (values[1]) + " " +
           QString::number (values[2]);
}

/// std::array<double,6> -> "x1 x2 x3 x4 x5 x6"
QString RobotModelBuilderWidget::vectorText6 (const std::array< double, 6 >& values)
{
    QString s;
    for (int i = 0; i < 6; ++i) {
        if (i > 0)
            s += " ";
        s += QString::number (values[i]);
    }
    return s;
}

/// std::array<double,16> -> "m00 m01 ... m33"(行优先 4x4),
/// Milestone 3 用于 Scene Frames 表格 Transform 4x4 列。
QString RobotModelBuilderWidget::vectorText16 (const std::array< double, 16 >& values)
{
    QStringList parts;
    parts.reserve (16);
    for (double value : values)
        parts << QString::number (value);
    return parts.join (" ");
}

QString RobotModelBuilderWidget::collisionSizeText (const CollisionModelSpec& collision)
{
    const GeometryKind kind = geometryKindFromString (collision.shape);
    if (kind == GeometryKind::Cylinder || kind == GeometryKind::Cone)
        return QString::number (collision.radius) + " " + QString::number (collision.length);
    if (kind == GeometryKind::Sphere)
        return QString::number (collision.radius);
    return vectorText (collision.dimensions);
}

bool RobotModelBuilderWidget::parseCollisionSize (const QString& text,
                                                   CollisionModelSpec& collision)
{
    const QStringList values = text.split (QRegularExpression ("\\s+"), Qt::SkipEmptyParts);
    const GeometryKind kind = geometryKindFromString (collision.shape);
    const int expected = kind == GeometryKind::Sphere ? 1 :
                         (kind == GeometryKind::Cylinder || kind == GeometryKind::Cone ? 2 : 3);
    if (values.size () != expected)
        return false;
    bool ok = false;
    if (expected == 1) {
        collision.radius = values[0].toDouble (&ok);
        return ok;
    }
    if (expected == 2) {
        collision.radius = values[0].toDouble (&ok);
        if (!ok) return false;
        collision.length = values[1].toDouble (&ok);
        return ok;
    }
    for (int i = 0; i < 3; ++i) {
        collision.dimensions[i] = values[i].toDouble (&ok);
        if (!ok) return false;
    }
    return true;
}

QString RobotModelBuilderWidget::collisionPoseText (const CollisionModelSpec& collision)
{
    return vectorText (collision.rpyDeg) + "; " + vectorText (collision.pos);
}

bool RobotModelBuilderWidget::parseCollisionPose (const QString& text,
                                                   CollisionModelSpec& collision)
{
    const QStringList values = text.split (';', Qt::SkipEmptyParts);
    return values.size () == 2 && parseVector3 (values[0].trimmed (), collision.rpyDeg) &&
           parseVector3 (values[1].trimmed (), collision.pos);
}

/// 解析 "m00 m01 ... m33" -> std::array<double,16>(行优先 4x4)
bool RobotModelBuilderWidget::parseVector16 (const QString& text,
                                             std::array< double, 16 >& values)
{
    const QStringList parts = text.split (QRegularExpression ("\\s+"), Qt::SkipEmptyParts);
    if (parts.size () != 16)
        return false;
    for (int i = 0; i < 16; ++i) {
        bool ok = false;
        values[i] = parts[i].toDouble (&ok);
        if (!ok)
            return false;
    }
    return true;
}

QComboBox* RobotModelBuilderWidget::makeCombo (const QStringList& values,
                                               const QString& currentValue,
                                               bool editable)
{
    QComboBox* combo = new QComboBox ();
    QStringList choices = values;
    if (!currentValue.isEmpty () && !choices.contains (currentValue, Qt::CaseSensitive))
        choices << currentValue;
    combo->addItems (choices);
    const int index = combo->findText (currentValue, Qt::MatchFixedString);
    combo->setCurrentIndex (index >= 0 ? index : 0);
    combo->setEnabled (editable);
    return combo;
}

void RobotModelBuilderWidget::setCombo (QTableWidget* table, int row, int column,
                                        const QStringList& values,
                                        const QString& value, bool editable)
{
    table->setCellWidget (row, column, makeCombo (values, value, editable));
    QTableWidgetItem* item = new QTableWidgetItem ();
    item->setFlags (item->flags () & ~Qt::ItemIsEditable);
    table->setItem (row, column, item);
}

QCheckBox* RobotModelBuilderWidget::setCheckBox (QTableWidget* table, int row, int column,
                                                  bool checked, bool editable)
{
    QCheckBox* checkBox = new QCheckBox ();
    checkBox->setChecked (checked);
    checkBox->setEnabled (editable);
    table->setCellWidget (row, column, checkBox);
    QTableWidgetItem* item = new QTableWidgetItem ();
    item->setFlags (item->flags () & ~Qt::ItemIsEditable);
    table->setItem (row, column, item);
    return checkBox;
}

bool RobotModelBuilderWidget::itemChecked (const QTableWidget* table, int row, int column)
{
    if (QCheckBox* checkBox =
            qobject_cast< QCheckBox* > (table->cellWidget (row, column)))
        return checkBox->isChecked ();
    const QString value = itemText (table, row, column);
    return value.compare ("true", Qt::CaseInsensitive) == 0 ||
           value.compare ("yes", Qt::CaseInsensitive) == 0 ||
           value.compare ("enabled", Qt::CaseInsensitive) == 0 || value == "1";
}

/// Milestone 4:为 Drawables 表的 Shape 列做一个 ComboBox,
/// 列出 8 种支持的几何类型。组合框不受 itemText 默认走 QTableWidgetItem
/// 的限制,所以 itemText() 也做了一次 cellWidget 优先的特殊处理。
void RobotModelBuilderWidget::setShapeCombo (QTableWidget* table, int row, int column,
                                             const QString& value, bool editable)
{
    QComboBox* combo = makeCombo (QStringList () << "Box" << "Cylinder" << "Sphere" << "Cone"
                                                 << "Plane" << "STL" << "Mesh" << "Polytope",
                                  value, editable);
    table->setCellWidget (row, column, combo);
    QTableWidgetItem* item = new QTableWidgetItem ();
    item->setFlags (item->flags () & ~Qt::ItemIsEditable);
    table->setItem (row, column, item);
    connect (combo, &QComboBox::currentTextChanged, this, [this] (const QString&) {
        if (!_syncingTables)
            generatePreview ();
    });
}

void RobotModelBuilderWidget::setCollisionShapeCombo (QTableWidget* table, int row,
                                                      int column, const QString& value,
                                                      bool editable)
{
    QComboBox* combo = makeCombo (QStringList () << "Box" << "Cylinder" << "Sphere"
                                                 << "Cone" << "Plane" << "STL" << "Mesh"
                                                 << "Polytope",
                                  value, editable);
    table->setCellWidget (row, column, combo);
    QTableWidgetItem* item = new QTableWidgetItem ();
    item->setFlags (item->flags () & ~Qt::ItemIsEditable);
    table->setItem (row, column, item);
    connect (combo, &QComboBox::currentTextChanged, this, [this, table, row] (const QString&) {
        if (!_syncingTables) {
            if (table == _collisionModelsTable)
                synchronizeCollisionFileFromDrawable (row);
            generatePreview ();
        }
    });
}

/// Milestone 4:按 shape 决定哪几列可编辑,实现"切 Box 后只剩 Dimensions
/// 改得了,切 Cylinder 后只剩 Radius/Length"的语义。
///   * autoLink row:仅 radius/RGB/Collision 可编辑(Link{i}To{i+1});
///   * 非 autoLink:Dimensions 对 Box/Plane 可编辑;Radius 对 Cyl/Sphere/Cone;
///                 Length 对 Cyl/Cone;File 对 STL/Mesh/Polytope。
bool RobotModelBuilderWidget::drawableColumnEditableForShape (const QString& shape,
                                                              int column, bool autoLink)
{
    if (autoLink)
        return column == 4 || column == 9 || column == 10;
    const GeometryKind kind = geometryKindFromString (shape.toStdString ());
    if (column == 3)
        return kind == GeometryKind::Box || kind == GeometryKind::Plane;
    if (column == 4)
        return kind == GeometryKind::Cylinder || kind == GeometryKind::Sphere ||
               kind == GeometryKind::Cone;
    if (column == 5)
        return kind == GeometryKind::Cylinder || kind == GeometryKind::Cone;
    if (column == 6)
        return kind == GeometryKind::STL || kind == GeometryKind::Mesh ||
               kind == GeometryKind::Polytope;
    return true;
}

// Milestone 5:Collision Models 表的列解锁规则;
// 与 drawable 版相比没有 STL(Collision 不支持 STL,这一列位置给了 Mesh)。
bool RobotModelBuilderWidget::collisionColumnEditableForShape (const QString& shape, int column)
{
    const GeometryKind kind = geometryKindFromString (shape.toStdString ());
    if (column == 4)
        return kind != GeometryKind::STL && kind != GeometryKind::Mesh &&
               kind != GeometryKind::Polytope;
    if (column == 5)
        return kind == GeometryKind::STL || kind == GeometryKind::Mesh ||
               kind == GeometryKind::Polytope;
    return true;
}
