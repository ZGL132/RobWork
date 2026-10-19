/**
 * @file probe.cpp
 * @brief 本次验收专用反例：真实建模面板的必需编辑入口检查。
 * 仅在 ignored build 目录编译，不修改被验产品。断言预期来自 MDL 需求，
 * 失败代表验收未通过；不把“面板存在”视为“功能可用”。
 */
#include <gtest/gtest.h>
#include <QApplication>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTabWidget>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <memory>
#include <sdurws/ird/modeling/Template.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include "plugin/ModelingPanelWidget.hpp"
#include "plugin/PanelCommandCatalog.hpp"
#include "plugin/ModelingCommandFlows.hpp"
#include "plugin/ModelingXacroBridge.hpp"
using namespace sdurws::ird;
using namespace sdurws::ird::modeling;
namespace {
const QString out = "C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/modeling-audit-20261019/probe-evidence";
/** @brief 仅替换对话框呈现边界，真实命令流、差异算法和数据模型均不替换。 */
struct CaptureDialog final : ModelingDialogHost {
  QString text;
  std::optional<QString> openFilePath(const QString&,const QString&) override {return std::nullopt;}
  std::optional<QString> saveFilePath(const QString&,const QString&,const QString&) override {return std::nullopt;}
  std::optional<int> chooseItem(const QString&,const QString&,const QStringList&) override {return std::nullopt;}
  bool confirmProceed(const QString&,const QString&) override {return true;}
  bool confirmImport(const QString&) override {return true;}
  void showInfo(const QString&,const QString& value) override {text=value;}
  std::optional<std::vector<CustomChainJointSpec>> declareCustomChain(const QString&,const CustomChainJointSpec&) override {return std::nullopt;}
};
/** @brief 面板及工作集仅由测试主线程持有，退出用例时自动销毁。 */
class ModelingAcceptanceProbe : public ::testing::Test {
protected:
  void SetUp() override {
    QDir().mkpath(out);
    std::vector<core::DiagnosticRecord> diagnostics;
    ws = RobotDesignTemplateFactory{}.createDraft(TemplateId{kTemplateIdGeneric6R}, runtime::InstallationPresetToken::Ground, "audit", diagnostics).get();
    panel=std::make_unique<ModelingPanelWidget>(true);
    panel->setEditTargetProvider([this]{return &ws;});
    panel->resize(1300,1000);
    refresh();
    panel->show(); QApplication::processEvents();
  }
  void refresh() { panel->refreshPanel(ws, ModelReadinessReport{}); }
  /** @brief 记录真实控件键与按钮文案，第三方可复核未提供的编辑面。 */
  QString keys() {
    QStringList keys;
    for(auto* table:panel->findChildren<QTableWidget*>()) {
      for(int row=0;row<table->rowCount();++row) {
        auto* item=table->item(row,0);
        if(item) keys << item->data(Qt::UserRole).toString();
      }
    }
    return keys.join('\n');
  }
  void snapshot(const QString& name) {
    QApplication::processEvents();
    panel->grab().save(out+"/"+name+".png");
    QFile file(out+"/"+name+".txt"); file.open(QIODevice::WriteOnly);
    QTextStream stream(&file); stream << "TABLE KEYS\n" << keys() << "\nBUTTONS\n";
    for(auto* button:panel->findChildren<QPushButton*>()) stream<<button->objectName()<<": "<<button->text()<<'\n';
  }
  ModelingWorkingSet ws;
  std::unique_ptr<ModelingPanelWidget> panel;
};
TEST_F(ModelingAcceptanceProbe, JointVelocityAccelerationEditable_MDL01) {
  IRD_TEST_INFO("MDL-01", {"AT-01"}, std::nullopt);
  panel->focusObject(ws.design.joints.front().objectId); snapshot("joint");
  const auto rows=keys();
  EXPECT_TRUE(rows.contains("velocity",Qt::CaseInsensitive)) << "缺少关节最大速度编辑字段；实际键见joint.txt";
  EXPECT_TRUE(rows.contains("acceleration",Qt::CaseInsensitive)) << "缺少关节最大加速度编辑字段";
}
TEST_F(ModelingAcceptanceProbe, ContinuousWorkingRangeEditable_MDL12) {
  IRD_TEST_INFO("MDL-12", {"AT-17"}, std::nullopt);
  ws.design.joints.front().type=JointType::Continuous;
  refresh(); panel->focusObject(ws.design.joints.front().objectId); snapshot("continuous");
  EXPECT_TRUE(keys().contains("working-range",Qt::CaseInsensitive)) << "continuous仅展示既有范围，缺少确认有限工程工作范围的编辑字段";
}
TEST_F(ModelingAcceptanceProbe, LinkPhysicalOverrideEditable_MDL05) {
  IRD_TEST_INFO("MDL-05", {"AT-01"}, std::nullopt);
  panel->focusObject(ws.design.links[1].objectId); snapshot("link");
  bool editable=false;
  for(auto* field:panel->findChildren<QLineEdit*>()) {
    if(field->isVisible() && field->isEnabled() && !field->isReadOnly()) editable=true;
  }
  EXPECT_TRUE(editable) << "选中连杆后物性字段均只读，未提供质量/质心/惯量手工覆盖入口";
}
TEST_F(ModelingAcceptanceProbe, CouplingMatrixEditable_MDL21) {
  IRD_TEST_INFO("MDL-21", {"AT-38"}, std::nullopt);
  snapshot("coupling");
  bool entry=keys().contains("coupling",Qt::CaseInsensitive);
  for(const auto& command:modelingDomainCommands()) if(command.id.find("coupling")!=std::string::npos) entry=true;
  EXPECT_TRUE(entry) << "耦合矩阵已有schema和消费者，但无表单字段或注册编辑命令";
}
TEST_F(ModelingAcceptanceProbe, WorkCellImportEntry_MDL18) {
  IRD_TEST_INFO("MDL-18", {"AT-33"}, std::nullopt);
  bool entry=false;
  for(const auto& command:modelingDomainCommands()) if(command.id.find("import-workcell")!=std::string::npos) entry=true;
  EXPECT_TRUE(entry) << "命令目录仅含URDF/Xacro/规范包导入，缺少外部WorkCell导入通道";
}
TEST_F(ModelingAcceptanceProbe, NamedPoseRestoreEntry_MDL17) {
  IRD_TEST_INFO("MDL-17", {"AT-28"}, std::nullopt);
  auto* area=panel->findChild<QWidget*>("ird_modeling_pose_area"); ASSERT_NE(area,nullptr);
  bool entry=false;
  for(auto* button:area->findChildren<QPushButton*>()) if(button->text().contains("恢复姿态")||button->text().contains("应用姿态")||button->text().contains("恢复位姿")) entry=true;
  snapshot("named-pose");
  EXPECT_TRUE(entry) << "命名位姿只有新增/删除/保存，不能恢复到会话三维姿态";
}
TEST_F(ModelingAcceptanceProbe, LocalUndoRedoEntries_L5) {
  IRD_TEST_INFO("MDL-07", {}, std::nullopt);
  bool undo=false,redo=false;
  for(auto* button:panel->findChildren<QPushButton*>()) {
    undo=undo||button->text().contains("撤销")||button->objectName().contains("undo");
    redo=redo||button->text().contains("重做")||button->objectName().contains("redo");
  }
  EXPECT_TRUE(undo && redo) << "单元卡L-5承诺的建模本地撤销/重做入口均缺席；项目级逆命令不等价于草稿编辑撤销";
}
TEST_F(ModelingAcceptanceProbe, ToolAndSceneCreationEntries_MDL13_MDL15) {
  IRD_TEST_INFO("MDL-15", {"AT-28"}, std::nullopt);
  bool tool=false,scene=false;
  for(auto* button:panel->findChildren<QPushButton*>()) {
    tool=tool||button->text().contains("新增工具")||button->text().contains("添加工具");
    scene=scene||button->text().contains("新增场景")||button->text().contains("添加场景")||button->text().contains("新增障碍");
  }
  for(const auto& command:modelingDomainCommands()) {
    tool=tool||command.id.find("add-tool")!=std::string::npos||command.id.find("create-tool")!=std::string::npos;
    scene=scene||command.id.find("add-scene")!=std::string::npos||command.id.find("create-scene")!=std::string::npos;
  }
  EXPECT_TRUE(tool) << "空白/模板模型无工具时无法从建模面板新建工具";
  EXPECT_TRUE(scene) << "空白/模板模型无场景对象时无法从建模面板新建障碍物";
}
TEST_F(ModelingAcceptanceProbe, BaseEulerEditing_MDL22) {
  IRD_TEST_INFO("MDL-22", {"AT-37"}, std::nullopt);
  auto* button=panel->findChild<QPushButton*>("ird_modeling_edit_base_mode"); ASSERT_NE(button,nullptr); button->click();
  snapshot("base");
  bool euler=false;
  for(auto* label:panel->findChildren<QLabel*>()) if(label->isVisible() && (label->text().contains("欧拉")||label->text().contains("roll")||label->text().contains("pitch")||label->text().contains("yaw"))) euler=true;
  EXPECT_TRUE(euler) << "MDL-22要求任意欧拉角编辑，但安装页仅提供EAA旋转矢量，二者输入语义不同";
}
TEST_F(ModelingAcceptanceProbe, HomeZeroUsesAuthoritativeReservedKeys_MDL04_MDL17) {
  IRD_TEST_INFO("MDL-17", {"AT-28"}, std::nullopt);
  ModuleSessionState session; session.draft=ws; session.draft.poseSetObject=PoseSet{};
  PoseSetEntry entry; entry.key=std::string(kHomeConfigurationPoseKey); entry.jointConfiguration=std::vector<double>(6,0.42);
  session.draft.poseSetObject->entries.push_back(entry);
  CaptureDialog host; std::string summary;
  ASSERT_TRUE(executeModelingCommand("modeling.reset-home-zero",session,ModelingFlowDeps{},host,summary));
  EXPECT_TRUE(host.text.contains("0.42")) << "真实命令连权威homeConfiguration的数据都未读取；实际文本="<<host.text.toStdString();
}
TEST_F(ModelingAcceptanceProbe, ModelDiffShowsObjectAndChangedValues_MDL08_UX13) {
  IRD_TEST_INFO("MDL-08", {"AT-12"}, std::nullopt);
  ModuleSessionState session; session.draft=ws; session.baselineSnapshot=ws;
  session.draft.design.joints.front().zeroOffset=0.25;
  CaptureDialog host; std::string summary;
  ASSERT_TRUE(executeModelingCommand("modeling.diff-baseline",session,ModelingFlowDeps{},host,summary));
  EXPECT_TRUE(host.text.contains("j1") && host.text.contains("0.25")) << "差异只呈现分类条数，未显示变化对象/前后值，无法点击定位；实际文本="<<host.text.toStdString();
}
TEST_F(ModelingAcceptanceProbe, ExistingXacroIncludeConsumedByProductionBridge_MDL19) {
  IRD_TEST_INFO("MDL-19", {"AT-31"}, std::nullopt);
  const QByteArray macros="<robot name=\"macros\"><xacro:macro name=\"joint_def\" params=\"name parent child\"><joint name=\"${name}\" type=\"revolute\"><parent link=\"${parent}\"/><child link=\"${child}\"/><axis xyz=\"0 0 1\"/></joint></xacro:macro></robot>";
  const QByteArray entry="<robot name=\"x\"><xacro:include filename=\"macros.xacro\"/><xacro:joint_def name=\"j1\" parent=\"base\" child=\"l1\"/></robot>";
  QFile included(out+"/macros.xacro"); ASSERT_TRUE(included.open(QIODevice::WriteOnly)); ASSERT_EQ(included.write(macros),macros.size()); included.close();
  QFile main(out+"/robot.xacro"); ASSERT_TRUE(main.open(QIODevice::WriteOnly)); ASSERT_EQ(main.write(entry),entry.size()); main.close();
  const std::vector<std::uint8_t> bytes(entry.begin(),entry.end());
  const auto expanded=expandXacroBytes(bytes,out+"/robot.xacro");
  EXPECT_TRUE(expanded.ok) << "真实磁盘include存在，但生产桥传入空依赖树导致失败："<<expanded.errorDetail;
}
}
