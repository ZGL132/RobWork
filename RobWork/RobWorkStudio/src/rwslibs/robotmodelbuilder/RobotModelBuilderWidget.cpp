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

#include "RobotModelXmlWriter.hpp"

// Qt 控件/工具头文件
#include <QCheckBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <map>
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
    QWidget* saveWidget      = new QWidget ();
    QHBoxLayout* saveLayout  = new QHBoxLayout (saveWidget);
    _saveDirectory           = new QLineEdit ();
    QPushButton* browse      = new QPushButton ("Browse");
    saveLayout->setContentsMargins (0, 0, 0, 0);
    saveLayout->addWidget (_saveDirectory);
    saveLayout->addWidget (browse);

    // 模式选择下拉框
    _mode = new QComboBox ();
    _mode = new QComboBox ();
    _mode->addItem ("Joint + RPY + Pos");
    _mode->addItem ("DH Projection");

    // 4 个选项开关(并排显示)
    QWidget* options        = new QWidget ();
    QHBoxLayout* optionLay  = new QHBoxLayout (options);
    _showFrameAxes          = new QCheckBox ("Show axes");
    _generateDrawables      = new QCheckBox ("Drawables");
    _generateScene          = new QCheckBox ("Scene file");
    _generateDwc            = new QCheckBox ("Dynamic WorkCell");
    _exportDhAdvanced       = new QCheckBox ("Advanced: export DHJoint XML");
    _exportDhAdvanced->setVisible (false);
    optionLay->setContentsMargins (0, 0, 0, 0);
    optionLay->addWidget (_showFrameAxes);
    optionLay->addWidget (_generateDrawables);
    optionLay->addWidget (_generateScene);
    optionLay->addWidget (_generateDwc);
    optionLay->addWidget (_exportDhAdvanced);
    optionLay->addStretch ();

    form->addRow ("Robot name", _robotName);
    form->addRow ("Save directory", saveWidget);
    form->addRow ("Mode", _mode);
    form->addRow ("Options", options);
    root->addLayout (form);

    QTabWidget* tabs = new QTabWidget ();

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
    _drawablesTable = makeTable (
        QStringList () << "Name"
                       << "RefFrame"
                       << "Shape"
                       << "Radius"
                       << "Length"
                       << "RPY deg (Z Y X)"
                       << "Pos m"
                       << "RGB"
                       << "Collision",
        0);
    tabs->addTab (_drawablesTable, "Drawables");

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
    dynLayout->addStretch ();
    tabs->addTab (dynamicsTab, "Dynamics");

    // -------------------------------------------------------------------------
    //  XML Preview 标签页:三段 XML 实时预览(只读)
    // -------------------------------------------------------------------------
    QWidget* previewTab      = new QWidget ();
    QVBoxLayout* previewLay  = new QVBoxLayout (previewTab);
    QTabWidget* previewTabs  = new QTabWidget ();
    _serialPreview           = new QTextEdit ();
    _scenePreview            = new QTextEdit ();
    _dwcPreview              = new QTextEdit ();
    _serialPreview->setReadOnly (true);
    _scenePreview->setReadOnly (true);
    _dwcPreview->setReadOnly (true);
    previewTabs->addTab (_serialPreview, "SerialDevice XML");
    previewTabs->addTab (_scenePreview, "Scene XML");
    previewTabs->addTab (_dwcPreview, "DWC XML");
    previewLay->addWidget (previewTabs);
    tabs->addTab (previewTab, "XML Preview");

    root->addWidget (tabs, 1);

    // -------------------------------------------------------------------------
    //  底部按钮 + 状态栏
    // -------------------------------------------------------------------------
    QWidget* buttons        = new QWidget ();
    QHBoxLayout* buttonLay  = new QHBoxLayout (buttons);
    QPushButton* previewBtn = new QPushButton ("Generate Preview");
    QPushButton* saveBtn    = new QPushButton ("Save XML");
    QPushButton* loadBtn    = new QPushButton ("Save and Load");
    QPushButton* resetBtn   = new QPushButton ("Reset to Default Six Axis");
    buttonLay->setContentsMargins (0, 0, 0, 0);
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
    connect (browse, SIGNAL (clicked ()), this, SLOT (browseSaveDirectory ()));
    connect (_mode, SIGNAL (currentIndexChanged (int)), this, SLOT (modeChanged (int)));
    connect (previewBtn, SIGNAL (clicked ()), this, SLOT (generatePreview ()));
    connect (saveBtn, SIGNAL (clicked ()), this, SLOT (saveXml ()));
    connect (loadBtn, SIGNAL (clicked ()), this, SLOT (saveAndLoad ()));
    connect (resetBtn, SIGNAL (clicked ()), this, SLOT (resetDefaults ()));
    connect (addPoseBtn, SIGNAL (clicked ()), this, SLOT (addPose ()));
    connect (delPoseBtn, SIGNAL (clicked ()), this, SLOT (removeSelectedPose ()));
    connect (addJointBtn, SIGNAL (clicked ()), this, SLOT (addJoint ()));
    connect (delJointBtn, SIGNAL (clicked ()), this, SLOT (removeSelectedJoint ()));
    connect (upJointBtn, SIGNAL (clicked ()), this, SLOT (moveSelectedJointUp ()));
    connect (downJointBtn, SIGNAL (clicked ()), this, SLOT (moveSelectedJointDown ()));

    // Transform 表被编辑后刷新 DH 投影视图;DH 表不反向修改真值。
    // _syncingTables 防止 setItem 触发 _dhTable->itemChanged 引起无谓递归。
    connect (_dhTable, SIGNAL (itemChanged (QTableWidgetItem*)), this,
             SLOT (onDhTableCellChanged (QTableWidgetItem*)));
    connect (_transformTable, SIGNAL (itemChanged (QTableWidgetItem*)), this,
             SLOT (onTransformTableCellChanged (QTableWidgetItem*)));
}

// =============================================================================
//  resetDefaults()
//  说明: 用 XmlWriter 的 makeDefaultSixAxisModel 生成默认数据并回填 UI,
//        同时立即生成一次预览,让用户看到出厂默认的 XML 长什么样。
// =============================================================================
void RobotModelBuilderWidget::resetDefaults ()
{
    fillFromSpec (RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::homePath ()));
    generatePreview ();
}

// =============================================================================
//  generatePreview()
//  说明: "Generate Preview" 按钮的回调:
//        1) 先做轻量的文本规则校验(各表格数字格式、向量维度);
//        2) 收集 spec,调用 applyLinkGeometry 自动重算 Link{i}To{i+1} 圆柱;
//        3) 调用 XmlWriter 校验 + 生成三段 XML 文本,刷新到预览框;
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
    _serialPreview->setPlainText (RobotModelXmlWriter::makeSerialDeviceXml (spec));
    _scenePreview->setPlainText (RobotModelXmlWriter::makeSceneXml (spec));
    _dwcPreview->setPlainText (spec.dynamics.generateDynamicWorkCell
                                   ? RobotModelXmlWriter::makeDynamicWorkCellXml (spec)
                                   : QString ("<!-- Enable \"Dynamic WorkCell\" to generate -->"));
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
    if (!RobotModelXmlWriter::saveFiles (spec, errors)) {
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
    if (!RobotModelXmlWriter::saveFiles (spec, errors)) {
        showErrors (errors);
        return;
    }
    generatePreview ();
    Q_EMIT loadSceneRequested (RobotModelXmlWriter::sceneFilePath (spec));
    setStatus ("XML files saved. Loading scene...");
}

// =============================================================================
//  browseSaveDirectory()
//  说明: 弹出原生目录选择对话框,把结果回填到 _saveDirectory。
// =============================================================================
void RobotModelBuilderWidget::browseSaveDirectory ()
{
    const QString dir =
        QFileDialog::getExistingDirectory (this, "Choose save directory", _saveDirectory->text ());
    if (!dir.isEmpty ())
        _saveDirectory->setText (dir);
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

    JointLimitSpec lim;
    lim.jointName = j.name;
    lim.posMin    = -180;
    lim.posMax    = 180;
    lim.velMax    = 180;
    lim.accMax    = 360;
    spec.limits.insert (spec.limits.begin () + insertRow, lim);

    JointForceLimitSpec fl;
    fl.jointName = j.name;
    fl.maxForce  = 1000;
    spec.dynamics.forceLimits.push_back (fl);

    LinkDynamicsSpec link;
    link.linkName        = "Link" + std::to_string (spec.dynamics.links.size () + 1);
    link.objectName      = j.name;
    link.mass            = 1.0;
    link.cog             = {{0, 0, 0}};
    link.inertia         = {{0.01, 0.01, 0.01, 0, 0, 0}};
    link.estimateInertia = false;
    link.material        = "Aluminum";
    spec.dynamics.links.push_back (link);

    for (PoseSpec& pose : spec.poses)
        pose.q.push_back (0.0);

    // 同步 drawables:新外壳 + 新 auto link(若 size >= 2)
    DrawableSpec housing;
    housing.name     = newName.toStdString () + "Housing";
    housing.refFrame = j.name;
    housing.shape    = "Cylinder";
    housing.radius   = 0.06;
    housing.length   = 0.08;
    housing.rgb      = {{0.45, 0.45, 0.48}};
    spec.drawables.push_back (housing);
    if (spec.transformJoints.size () >= 2) {
        DrawableSpec link_d;
        link_d.name             = "Link" + std::to_string (insertRow + 1) + "To" +
                                  std::to_string (insertRow + 2);
        link_d.refFrame         = spec.transformJoints[insertRow].name;    // 新关节本身
        link_d.shape            = "Cylinder";
        link_d.shape            = "Cylinder";
        link_d.radius           = 0.04;
        link_d.length           = 0;
        link_d.rgb              = {{0.35, 0.45, 0.65}};
        link_d.autoLinkGeometry = true;
        // 把它放在所有 housings + links 之后;不清掉用户自定义的 drawable。
        spec.drawables.push_back (link_d);
    }

    RobotModelXmlWriter::applyLinkGeometry (spec);
    fillFromSpec (spec);
    setStatus (QString ("Added joint %1 (now %2 joints).").arg (newName).arg (
                  static_cast< int >(spec.transformJoints.size ())));
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
    if (static_cast< size_t >(row) < spec.limits.size ())
        spec.limits.erase (spec.limits.begin () + row);

    // force limits / dynamics links / pose.q 都是按可动关节顺序索引;
    // 移除时把它们同步缩短。简单做法:直接按 name 匹配删除。
    const std::string removedKey = removedName.toStdString ();
    spec.dynamics.forceLimits.erase (
        std::remove_if (spec.dynamics.forceLimits.begin (),
                        spec.dynamics.forceLimits.end (),
                        [&] (const JointForceLimitSpec& f) { return f.jointName == removedKey; }),
        spec.dynamics.forceLimits.end ());
    spec.dynamics.links.erase (
        std::remove_if (spec.dynamics.links.begin (), spec.dynamics.links.end (),
                        [&] (const LinkDynamicsSpec& l) { return l.objectName == removedKey; }),
        spec.dynamics.links.end ());

    for (PoseSpec& pose : spec.poses) {
        if (!pose.q.empty ())
            pose.q.erase (pose.q.begin () + row);
    }

    // 同步 drawables:auto link drawables 与 joints 一一对应,删除对应的那条
    // housing + 那条 link。一次性用一个谓词过滤,避免多个 remove_if 嵌套。
    const QString houseName    = removedName + "Housing";
    const QString linkABefore  = "Link" + QString::number (row + 1) + "To" +
                                 QString::number (row + 2);
    const QString linkAAfter   = "Link" + QString::number (row + 2) + "To" +
                                 QString::number (row + 3);
    spec.drawables.erase (
        std::remove_if (spec.drawables.begin (), spec.drawables.end (),
                        [&] (const DrawableSpec& d) {
                            const QString n = QString::fromStdString (d.name);
                            return n == houseName || n == linkABefore || n == linkAAfter;
                        }),
        spec.drawables.end ());

    RobotModelXmlWriter::applyLinkGeometry (spec);
    fillFromSpec (spec);
    setStatus (QString ("Removed joint %1 (now %2 joints).").arg (removedName).arg (
                  static_cast< int >(spec.transformJoints.size ())));
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

// =============================================================================
//  onDhTableCellChanged()
//  说明: DH 表是 SE(3) 真值的投影视图,整表 NoEditTriggers + 单元格
//        ~ItemIsEditable 双重保护,理论上不会被用户编辑。
//        留这个槽只是为了在极端情况下(setItem 误用)给出明确提示,
//        避免用户疑惑"为什么我改了 DH 表没反应"。
// =============================================================================
void RobotModelBuilderWidget::onDhTableCellChanged (QTableWidgetItem* item)
{
    if (_syncingTables || item == NULL)
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
    if (_syncingTables || item == NULL)
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
    _saveDirectory->setText (QString::fromStdString (spec.saveDirectory));
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
    fillLimitsTable (spec);
    fillPosesTable (spec);
    fillDynamicsTab (spec);
    modeChanged (_mode->currentIndex ());
    _syncingTables = false;
}

// =============================================================================
//  collectSpec()
//  说明: 与 fillFromSpec 相反,把 UI 上的全部控件值收集为 RobotModelSpec。
//        这里会按列含义把表格文本解析回 double / vector。
// =============================================================================
RobotModelSpec RobotModelBuilderWidget::collectSpec () const
{
    RobotModelSpec spec;
    spec.robotName         = _robotName->text ().toStdString ();
    spec.saveDirectory     = _saveDirectory->text ().toStdString ();
    spec.mode              = _mode->currentIndex () == 1 ? KinematicsViewMode::DHProjection
                                                          : KinematicsViewMode::JointRPYPos;
    spec.exportDhJointsAdvanced = _exportDhAdvanced->isChecked ();
    spec.showFrameAxes     = _showFrameAxes->isChecked ();
    spec.generateDrawables = _generateDrawables->isChecked ();
    spec.generateScene     = _generateScene->isChecked ();
    spec.dynamics.generateDynamicWorkCell = _generateDwc->isChecked ();
    spec.dynamics.baseFrame    = _baseFrame->text ().toStdString ();
    spec.dynamics.baseMaterial = _baseMaterial->text ().toStdString ();

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

    // ---- Drawables 表 ----
    for (int row = 0; row < _drawablesTable->rowCount (); ++row) {
        DrawableSpec drawable;
        drawable.name           = itemText (_drawablesTable, row, 0).toStdString ();
        drawable.refFrame       = itemText (_drawablesTable, row, 1).toStdString ();
        drawable.shape          = itemText (_drawablesTable, row, 2).toStdString ();
        drawable.radius         = itemDouble (_drawablesTable, row, 3);
        drawable.length         = itemDouble (_drawablesTable, row, 4);
        parseVector3 (itemText (_drawablesTable, row, 5), drawable.rpyDeg);
        parseVector3 (itemText (_drawablesTable, row, 6), drawable.pos);
        parseVector3 (itemText (_drawablesTable, row, 7), drawable.rgb);
        drawable.collisionModel =
            itemText (_drawablesTable, row, 8).compare ("Enabled", Qt::CaseInsensitive) == 0;
        // 自动生成的 Link{i}To{i+1} Drawable 在保存前会被 applyLinkGeometry 覆写
        drawable.autoLinkGeometry =
            isAutoLinkDrawable (QString::fromStdString (drawable.name));
        spec.drawables.push_back (drawable);
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
        link.estimateInertia =
            itemText (_dynamicsLinksTable, row, 5).compare ("Enabled", Qt::CaseInsensitive) == 0;
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
            if (!parseDouble (itemText (_drawablesTable, row, 3)))
                errors << QString ("Invalid drawable radius at row %1.").arg (row + 1);
            if (!parseDouble (itemText (_drawablesTable, row, 4)))
                errors << QString ("Invalid drawable length at row %1.").arg (row + 1);
            if (!parseVector (itemText (_drawablesTable, row, 5), 3))
                errors << QString ("Invalid drawable RPY vector at row %1.").arg (row + 1);
            if (!parseVector (itemText (_drawablesTable, row, 6), 3))
                errors << QString ("Invalid drawable Pos vector at row %1.").arg (row + 1);
            if (!parseVector (itemText (_drawablesTable, row, 7), 3))
                errors << QString ("Invalid drawable RGB vector at row %1.").arg (row + 1);
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
        setItem (_transformTable, row, 1, QString::fromStdString (joint.type));
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
        // applyLinkGeometry 维护,所以在 UI 上锁为只读
        setItem (_drawablesTable, row, 0, QString::fromStdString (drawable.name), !autoLink);
        setItem (_drawablesTable, row, 1, QString::fromStdString (drawable.refFrame), !autoLink);
        setItem (_drawablesTable, row, 2, QString::fromStdString (drawable.shape), !autoLink);
        setItem (_drawablesTable, row, 3, QString::number (drawable.radius));
        setItem (_drawablesTable, row, 4, QString::number (drawable.length), !autoLink);
        setItem (_drawablesTable, row, 5, vectorText (drawable.rpyDeg), !autoLink);
        setItem (_drawablesTable, row, 6, vectorText (drawable.pos), !autoLink);
        setItem (_drawablesTable, row, 7, vectorText (drawable.rgb));
        setItem (_drawablesTable, row, 8, drawable.collisionModel ? "Enabled" : "Disabled");
    }
}

void RobotModelBuilderWidget::fillLimitsTable (const RobotModelSpec& spec)
{
    const int n = static_cast< int >(spec.limits.size ());
    _limitsTable->setRowCount (n);
    for (int row = 0; row < n; ++row) {
        const JointLimitSpec& limit = spec.limits[row];
        setItem (_limitsTable, row, 0, QString::fromStdString (limit.jointName));
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
        setItem (_dynamicsLinksTable, row, 1, QString::fromStdString (link.objectName));
        setItem (_dynamicsLinksTable, row, 2, QString::number (link.mass));
        setItem (_dynamicsLinksTable, row, 3, vectorText (link.cog));
        setItem (_dynamicsLinksTable, row, 4, vectorText6 (link.inertia));
        setItem (_dynamicsLinksTable, row, 5, link.estimateInertia ? "Enabled" : "Disabled");
        setItem (_dynamicsLinksTable, row, 6, QString::fromStdString (link.material));
    }
    for (int row = 0; row < nForce; ++row) {
        const JointForceLimitSpec& fl = spec.dynamics.forceLimits[row];
        setItem (_forceLimitsTable, row, 0, QString::fromStdString (fl.jointName));
        setItem (_forceLimitsTable, row, 1, QString::number (fl.maxForce));
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
QString RobotModelBuilderWidget::itemText (const QTableWidget* table, int row, int column)
{
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
