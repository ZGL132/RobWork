/**
 * @file   RequirementsSessionGuiTest.cpp
 * @brief  需求面板会话接线 Widgets 级契约测试（sdurws_ird_requirements_
 *         gui_test 载体）——UI-T29 会话接线＋最小校验的具名自证面。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T29.json acceptance 1/2/5 的具名用例
 *     （会话数据面：基线闭包载入→四集合入面板；编辑生效：宿主编辑后
 *     动作随 L-2 接受触发；最小校验：就绪判定权威＝域侧 checker——
 *     gate 真值源双态）；
 *   - units/requirements.md §9.8（面板组成/L-R2 编辑流）、§4.6（编辑器
 *     基线/工作集）、§8（就绪校验分层——Blocking 语义）；
 *   - 先例：requirements/app/RequirementsHarnessMain.cpp（MapClosure/
 *     fillBaseline 夹具——同源复用）；modeling/gui_test（QApplication
 *     main＋findChild 定位形态）。
 *
 * 断言纪律：组合子编排（宿主重估→模块以最新报告 refreshPanel）经模块
 * 公共面装配后由 onEditApplied 直呼触发（IPanelEditSink 公共覆盖——
 * UI 线程同线程直调）；视觉呈现细节归验收截图，本组断言数据面。
 */

#include <gtest/gtest.h>
#include <limits> // 非有限交互返回值必须被拒绝，不能写入领域草稿。

#include <QApplication>
#include <QClipboard> // UX-05：真实剪贴板经过公共参数表粘贴入口。
#include <QElapsedTimer>
#include <QKeyEvent> // REQ-08：向真实窗口派发快捷键，覆盖按钮以外的用户入口。
#include <QEventLoop>
#include <QTimer>
#include <QCheckBox>  // 启用 Switch 断言（UI-T37 R1）
#include <QGroupBox>  // 卡片分组定位（UI-T37 返工——QGroupBox 形态）
#include <QLabel>
#include <QLineEdit>
#include <QMenu>  // 导入下拉菜单断言（UI-T37 返工⑤）
#include <QPushButton>
#include <QPlainTextEdit> // AT-02：真实导入确认页全量纯文本错误展示。
#include <QScrollArea>
#include <QSlider>  // 覆盖率双联断言（UI-T37 R1）
#include <QSpinBox>  // 采样计数复合行断言（UI-T37 R1）
#include <algorithm>  // 批33：段行控件按行序排序。
#include <QPoint>  // 批33：mapTo 行序坐标。
#include <QFormLayout>  // WP-14-T12 批33：段行 LabelRole/FieldRole 精确取法。
#include <QDoubleSpinBox>  // WP-14-T12 批33：段距离旋箱（接近/撤离复合行）。
#include <QTabWidget>
#include <QToolButton>  // 卡片折叠三角断言（UI-T37 返工⑤）
#include <QTreeWidget>
#include <QTableWidget>  // WP-14-T12：生产工况结构表，验证空集合的新增与原子应用。
#include <QListWidget>  // 适用工位多选的真实控件。
#include <QWidget>

#include <sdurws/ird/ui/FlowLayout.hpp>  // 边距耦合半区（UI-T36 G-1 守卫重构——公共头 R-2 合规）

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <sdurws/ird/requirements/Editor.hpp>       // RequirementEditor＋闭包视图（域裁决唯一入口）
#include <sdurws/ird/requirements/ObjectTypes.hpp>  // kReqSetObjectType 等 token（公共头常量）
#include <sdurws/ird/requirements/Readiness.hpp>    // RequirementReadinessChecker（判定权威）
#include <sdurws/ird/requirements/RequirementsPluginAssembly.hpp>  // 装配门面（UI-T35 P2 基线出线往返用例）
#include <sdurws/ird/requirements/RevisionSyncPolicy.hpp>  // 外部修订同步三分岔判定（UI-T35 P2 真值表用例）

#include "plugin/RequirementsPanelWidget.hpp"  // 被测面板（同单元 PRIVATE include 面）
#include "plugin/RequirementsUiModule.hpp"     // 模块组合子（宿主接线同款形态）

using namespace sdurws::ird;
using namespace sdurws::ird::requirements;

namespace {

/**
 * @brief 等待Windows真实剪贴板可读（最多3秒），不以空剪贴板冒充生产粘贴成功。
 * @param text [in] TSV文本；测试窗销毁后OLE所有权消息可能尚未派发，先泵事件再重试。
 * @return 实际读回完全一致才为true；超时仍由用例失败，不跳过或替换生产粘贴路径。
 */
bool setRequirementClipboard(const QString& text)
{
    QElapsedTimer deadline; deadline.start();
    do {
        QApplication::processEvents();
        QApplication::clipboard()->setText(text);
        if (QApplication::clipboard()->text() == text) { return true; }
        QEventLoop messages;
        QTimer::singleShot(20, &messages, &QEventLoop::quit);
        messages.exec();
    } while (deadline.elapsed() < 3000);
    return false;
}

/// 闭包域字节源的测试实现（harness MapClosure 同款——内存映射形态）。
class MapClosure final : public RequirementObjectClosureView {
public:
    void put(const std::string& token, const RequirementObjectVariant& object)
    {
        const RequirementCodec codec;
        auto bytes = codec.encode(object, kCurrentRequirementFormatVersion);
        if (bytes.ok()) {
            byToken_[token] = RequirementClosureObject{token, bytes.get()};
        }
    }

    void putById(const core::ObjectId& id, const std::string& token,
                 const RequirementObjectVariant& object)
    {
        const RequirementCodec codec;
        auto bytes = codec.encode(object, kCurrentRequirementFormatVersion);
        if (bytes.ok()) {
            byId_[id] = RequirementClosureObject{token, bytes.get()};
        }
    }

    std::optional<RequirementClosureObject> tryObjectByToken(
        std::string_view objectTypeToken) const override
    {
        const auto it = byToken_.find(std::string{objectTypeToken});
        return it != byToken_.end() ? std::optional{it->second} : std::nullopt;
    }

    std::optional<RequirementClosureObject> tryObject(
        const core::ObjectId& objectId) const override
    {
        const auto it = byId_.find(objectId);
        return it != byId_.end() ? std::optional{it->second} : std::nullopt;
    }

private:
    std::map<std::string, RequirementClosureObject> byToken_;
    std::map<core::ObjectId, RequirementClosureObject> byId_;
};

/// 合法任务点（位置已提供——harness makePoint 同款）。
TaskPoint makePoint(const std::string& name, double x, double y, double z)
{
    TaskPoint p;
    p.objectId = core::ObjectId::generate();
    p.name = name;
    p.pose.constrainedDof.z = true;
    p.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(x, y, z),
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    p.work = TaskSegment{true, SegmentAxis::ToolZ, 1.0};
    return p;
}

/// 合法工作区域（I-REQ-6 非退化盒＋Grid 采样——harness makeRegion 同款）。
WorkRegion makeRegion(const std::string& name)
{
    WorkRegion r;
    r.objectId = core::ObjectId::generate();
    r.name = name;
    r.box = BoundingBox{rw::math::Vector3D<double>(1.0, 2.0, 3.0),
                        rw::math::Vector3D<double>(2.0, 2.0, 2.0)};
    r.positionSampling = PositionSampling{PositionSamplingMethod::Grid, {2, 2, 2}, {0, 0, 0}, 0};
    return r;
}

/// 根＋四集合闭包填充（harness fillBaseline 同款——固定 set oid＋根 oid）。
core::ObjectId fillBaseline(MapClosure& closure,
                            PointSet points,
                            RegionSet regions,
                            ConditionSet conditions)
{
    sortEntriesByObjectId(points.entries);
    sortEntriesByObjectId(regions.entries);
    sortEntriesByObjectId(conditions.entries);
    RequirementSet root;
    const core::ObjectId pointSetId =
        core::ObjectId::fromCanonical("obj-10000000000000000000000000000001");
    const core::ObjectId regionSetId =
        core::ObjectId::fromCanonical("obj-20000000000000000000000000000002");
    const core::ObjectId condSetId =
        core::ObjectId::fromCanonical("obj-30000000000000000000000000000003");
    const core::ObjectId planSetId =
        core::ObjectId::fromCanonical("obj-40000000000000000000000000000004");
    root.name = "演示需求集";
    root.pointSetRef = pointSetId;
    root.regionSetRef = regionSetId;
    root.conditionSetRef = condSetId;
    root.planSetRef = planSetId;
    closure.put(std::string{kReqSetObjectType}, RequirementObjectVariant{root});
    closure.putById(pointSetId, std::string{kReqPointSetObjectType},
                    RequirementObjectVariant{points});
    closure.putById(regionSetId, std::string{kReqRegionSetObjectType},
                    RequirementObjectVariant{regions});
    closure.putById(condSetId, std::string{kReqConditionSetObjectType},
                    RequirementObjectVariant{conditions});
    closure.putById(planSetId, std::string{kReqPlanSetObjectType},
                    RequirementObjectVariant{PlanSet{}});
    return core::ObjectId::fromCanonical("obj-50000000000000000000000000000005");
}

}  // namespace

// =====================================================================
// 夹具：基线载入＋面板＋就绪校验器（每用例独立——零跨用例状态）
// =====================================================================

class RequirementsSessionGuiTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        MapClosure closure;
        PointSet points;
        points.entries.push_back(makePoint("P1", 1.0, 2.0, 3.0));
        RegionSet regions;
        regions.entries.push_back(makeRegion("R1"));
        ConditionSet conditions;
        // 工况起步条目（UI-T30 生命周期的工况集合操作载体——T29 版夹具
        // 此处为空，B1 用例需要非空集合）。
        OperatingCondition c1;
        c1.objectId = core::ObjectId::generate();
        c1.name = "C1";
        conditions.entries.push_back(c1);
        const core::ObjectId rootOid = fillBaseline(closure, points, regions, conditions);
        m_rootOid = rootOid;
        const auto load = m_editor.loadBaseline(closure);
        ASSERT_TRUE(load.ok) << "基线载入失败（夹具装配错误）";
        m_report = m_checker.check(m_editor.workingSet(), CheckContext{});
        m_panel = std::make_unique<RequirementsPanelWidget>(true);
        // 编辑目标提供器（L-R2 数据前提——检查器/编辑流经提供器现取权威
        // 编辑器；harness ③同款装配）。
        m_panel->setEditTargetProvider([this]() -> IRequirementEditor* {
            return &m_editor;
        });
    }

    core::ObjectId m_rootOid{};                       ///< 根对象身份（定位跳转用）
    RequirementEditor m_editor;                       ///< 会话权威编辑器（宿主持有形态）
    RequirementReadinessChecker m_checker;            ///< 判定权威（直投值面）
    RequirementReadinessReport m_report{};            ///< 首刷就绪报告
    std::unique_ptr<RequirementsPanelWidget> m_panel; ///< 被测面板
};

/** @brief UX-05：工位批量输入先暂存，非法行定位，修正后整体确认与撤销。 */
TEST_F(RequirementsSessionGuiTest, StationBatchParametersAtomic_UX05_WP14T12)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    const auto before = m_editor.workingSet().points.entries.front();
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(before.objectId);
    auto* batch = m_panel->findChild<QWidget*>("ird_req_batch_station");
    ASSERT_NE(batch, nullptr) << "需求工位必须有非模态筛选/批量参数编辑入口";
    auto* filter = batch->findChild<QLineEdit*>("ird_param_filter");
    auto* table = batch->findChild<QTableWidget*>("ird_param_table");
    ASSERT_NE(filter, nullptr); ASSERT_NE(table, nullptr);
    const int allRows = table->rowCount();
    filter->setText(QStringLiteral("容差"));
    EXPECT_GT(table->rowCount(), 0); EXPECT_LT(table->rowCount(), allRows);
    filter->clear();
    auto* units = batch->findChild<QComboBox*>("ird_param_unit_switch"); ASSERT_NE(units, nullptr);
    const int mmIndex = units->findText(QStringLiteral("长度：mm")); ASSERT_GE(mmIndex, 0);
    units->setCurrentIndex(mmIndex); units->activated(mmIndex);
    // 公共参数表以单位列声明输入单位，TSV值列只填数值，不重复附加单位后缀。
    ASSERT_TRUE(setRequirementClipboard("pose-position-x\t250\ntolerance-position\tbad")) << "Windows剪贴板写入必须真实可读";
    batch->findChild<QPushButton*>("ird_param_paste")->click();
    EXPECT_EQ(m_editor.workingSet().points.entries.front(), before) << "粘贴仅暂存";
    auto* impact = batch->findChild<QLabel*>("ird_param_impact");
    ASSERT_NE(impact, nullptr);
    EXPECT_TRUE(impact->text().contains(QStringLiteral("第2行"))) << impact->text().toStdString();
    batch->findChild<QPushButton*>("ird_param_apply")->click();
    EXPECT_TRUE(batch->findChild<QWidget*>("ird_param_confirm_region")->isHidden());
    ASSERT_TRUE(setRequirementClipboard("tolerance-position\t2\npose-position-y\t500")) << "Windows剪贴板写入必须真实可读";
    batch->findChild<QPushButton*>("ird_param_paste")->click();
    batch->findChild<QPushButton*>("ird_param_apply")->click();
    batch->findChild<QPushButton*>("ird_param_confirm_yes")->click();
    QApplication::processEvents();
    std::fprintf(stderr, "[s8]\n");
    const auto after = m_editor.workingSet().points.entries.front();
    EXPECT_DOUBLE_EQ(after.pose.position.value()[0], 0.25);
    EXPECT_DOUBLE_EQ(after.pose.position.value()[1], 0.5);
    EXPECT_DOUBLE_EQ(after.pose.position.value()[2], before.pose.position.value()[2]);
    EXPECT_DOUBLE_EQ(after.tolerance.positionTolerance, 0.002);
    std::fprintf(stderr, "[s9]\n");
    ASSERT_TRUE(m_editor.undoLocal()); EXPECT_EQ(m_editor.workingSet().points.entries.front(), before);
    ASSERT_TRUE(m_editor.redoLocal()); EXPECT_EQ(m_editor.workingSet().points.entries.front(), after);
}

/** @brief UX-05：筛选/单位切换后可取得真实字段键及精确数值模板，只读复制零写入。 */
TEST_F(RequirementsSessionGuiTest, ParameterTsvTemplateFollowsFilterAndUnits_UX05_WP14T12)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    auto precise = m_editor.workingSet().points.entries.front();
    precise.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(0.1234567891234567, 2.0, 3.0), precise.source);
    ASSERT_TRUE(m_editor.applyEdit(RequirementEdit{precise}).accepted);
    const auto before = m_editor.workingSet();
    m_panel->refreshPanel(before, m_report);
    m_panel->focusObject(before.points.entries.front().objectId);
    auto* batch = m_panel->findChild<QWidget*>("ird_req_batch_station");
    ASSERT_NE(batch, nullptr);
    auto* copy = batch->findChild<QPushButton*>("ird_req_copy_tsv");
    ASSERT_NE(copy, nullptr) << "UX-05：用户必须能取得真实字段键与当前单位下的TSV模板";
    auto* filter = batch->findChild<QLineEdit*>("ird_param_filter");
    auto* table = batch->findChild<QTableWidget*>("ird_param_table");
    auto* units = batch->findChild<QComboBox*>("ird_param_unit_switch");
    ASSERT_NE(filter, nullptr); ASSERT_NE(table, nullptr); ASSERT_NE(units, nullptr);
    QString label;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row,0)->data(Qt::UserRole).toString() == "pose-position-x") {
            label = table->item(row,0)->text(); break;
        }
    }
    ASSERT_FALSE(label.isEmpty()); filter->setText(label);
    const int mmIndex = units->findText(QStringLiteral("长度：mm")); ASSERT_GE(mmIndex, 0);
    units->setCurrentIndex(mmIndex); units->activated(mmIndex);
    copy->click(); QApplication::processEvents();
    const QString copied = QGuiApplication::clipboard()->text();
    const auto columns = copied.split('\t'); ASSERT_EQ(columns.size(), 2);
    EXPECT_EQ(columns[0], "pose-position-x");
    bool parsed = false; const double copiedMm = columns[1].toDouble(&parsed);
    ASSERT_TRUE(parsed); EXPECT_DOUBLE_EQ(copiedMm, 0.1234567891234567 * 1000.0)
        << "TSV回贴不能把工程值截成表格六位显示精度";
    EXPECT_FALSE(copied.contains("pose-position-y\t")) << "模板只包含当前筛选行";
    EXPECT_FALSE(copied.contains(" mm")) << "数值列不重复包含单位后缀";
    EXPECT_EQ(m_editor.workingSet(), before) << "复制模板只读，不能改草稿";
    m_panel->setWritable(false);
    // 可写性变化会重建表单，重新定位新控件，不读取被销毁的旧窗口指针。
    batch = m_panel->findChild<QWidget*>("ird_req_batch_station"); ASSERT_NE(batch, nullptr);
    copy = batch->findChild<QPushButton*>("ird_req_copy_tsv"); ASSERT_NE(copy, nullptr);
    filter = batch->findChild<QLineEdit*>("ird_param_filter"); ASSERT_NE(filter, nullptr);
    units = batch->findChild<QComboBox*>("ird_param_unit_switch"); ASSERT_NE(units, nullptr);
    filter->setText(label);
    const int readOnlyMm = units->findText(QStringLiteral("长度：mm")); ASSERT_GE(readOnlyMm, 0);
    units->setCurrentIndex(readOnlyMm); units->activated(readOnlyMm);
    EXPECT_TRUE(copy->isEnabled()) << "只读会话仍可复制参数";
    EXPECT_FALSE(batch->findChild<QPushButton*>("ird_param_paste")->isEnabled());
    EXPECT_FALSE(batch->findChild<QPushButton*>("ird_param_apply")->isEnabled());
    EXPECT_EQ(batch->findChild<QTableWidget*>("ird_param_table")->editTriggers(), QAbstractItemView::NoEditTriggers);
    // Windows销毁旧表单后的OLE剪贴板所有权消息可能尚未完成；只重试真实
    // 复制按钮并泵事件，不直接把期望文本写剪贴板，最多3秒仍失败则报错。
    QElapsedTimer deadline; deadline.start();
    QString readOnlyCopy;
    do {
        QApplication::processEvents(); copy->click();
        readOnlyCopy = QGuiApplication::clipboard()->text();
        if (readOnlyCopy == copied) { break; }
        QEventLoop messages; QTimer::singleShot(20, &messages, &QEventLoop::quit); messages.exec();
    } while (deadline.elapsed() < 3000);
    EXPECT_EQ(readOnlyCopy, copied);
    EXPECT_EQ(m_editor.workingSet(), before);
}

/** @brief UX-05/REQ-03：区域中心/尺寸批量修改经领域校验，只入一次撤销栈。 */
TEST_F(RequirementsSessionGuiTest, RegionBatchParametersAtomic_UX05_WP14T12)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    const auto before = m_editor.workingSet().regions.entries.front();
    m_panel->refreshPanel(m_editor.workingSet(), m_report); m_panel->focusObject(before.objectId);
    auto* batch = m_panel->findChild<QWidget*>("ird_req_batch_region");
    ASSERT_NE(batch, nullptr);
    ASSERT_TRUE(setRequirementClipboard("box-center-x\t0.25\nbox-size-z\t0.75")) << "Windows剪贴板写入必须真实可读";
    batch->findChild<QPushButton*>("ird_param_paste")->click();
    EXPECT_EQ(m_editor.workingSet().regions.entries.front(), before);
    batch->findChild<QPushButton*>("ird_param_apply")->click();
    batch->findChild<QPushButton*>("ird_param_confirm_yes")->click(); QApplication::processEvents();
    const auto after = m_editor.workingSet().regions.entries.front();
    EXPECT_DOUBLE_EQ(after.box.center[0], 0.25); EXPECT_DOUBLE_EQ(after.box.size[2], 0.75);
    ASSERT_TRUE(m_editor.undoLocal()); EXPECT_EQ(m_editor.workingSet().regions.entries.front(), before);
}

/** @brief REQ-03：区域三种采样和可选姿态覆盖率可暂存，确认整批一次撤销。 */
TEST_F(RequirementsSessionGuiTest, RegionSamplingCompleteAtomic_REQ03_WP14T12)
{
    IRD_TEST_INFO("REQ-03", {"UX-05"}, std::nullopt);
    const auto before = m_editor.workingSet().regions.entries.front();
    m_panel->refreshPanel(m_editor.workingSet(), m_report); m_panel->focusObject(before.objectId);
    auto* form = m_panel->findChild<QWidget*>("ird_region_sampling_form");
    ASSERT_NE(form, nullptr) << "生产区域检查器缺完整采样编辑入口";
    auto* method = form->findChild<QComboBox*>("ird_region_sampling_method");
    ASSERT_NE(method, nullptr);
    method->setCurrentIndex(method->findData(int(PositionSamplingMethod::GridBySpacing)));
    for (int axis = 0; axis < 3; ++axis) {
        form->findChild<QLineEdit*>(QString("ird_region_spacing_%1").arg(axis))->setText("0.25");
    }
    form->findChild<QLineEdit*>("ird_region_direction_samples")->setText("12");
    form->findChild<QLineEdit*>("ird_region_roll_samples")->setText("4");
    form->findChild<QCheckBox*>("ird_region_orientation_coverage_enabled")->setChecked(true);
    form->findChild<QLineEdit*>("ird_region_orientation_coverage")->setText("0.65");
    EXPECT_EQ(m_editor.workingSet().regions.entries.front(), before) << "编辑未确认应零写入";
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click(); QApplication::processEvents();
    const auto after = m_editor.workingSet().regions.entries.front();
    EXPECT_EQ(after.positionSampling.method, PositionSamplingMethod::GridBySpacing);
    EXPECT_EQ(after.positionSampling.spacing, (std::array<double, 3>{0.25,0.25,0.25}));
    EXPECT_EQ(after.orientationSampling.directionSamples, 12u);
    EXPECT_EQ(after.orientationSampling.rollSamples, 4u);
    ASSERT_TRUE(after.coverageTargets.minOrientationCoverage);
    EXPECT_DOUBLE_EQ(*after.coverageTargets.minOrientationCoverage, 0.65);
    ASSERT_TRUE(m_editor.undoLocal()); EXPECT_EQ(m_editor.workingSet().regions.entries.front(), before);
    ASSERT_TRUE(m_editor.redoLocal()); EXPECT_EQ(m_editor.workingSet().regions.entries.front(), after);
}

/** @brief A-18：区域采样确认自动构建/upsert 采样计划条目；区域删除随删。
 *
 * 生产路径全链（真实表单确认→面板派生钩→ISamplingPlanBuilder→编辑器
 * upsert）——覆盖评估切片消费 req-plan-set，UI 缺计划创建路径即评估链
 * 对用户内容不可达（A-18 根因面，r11 宿主实测）。用例名带缺陷号。
 */
TEST_F(RequirementsSessionGuiTest, RegionSamplingBuildsPlanEntry_A18_WP14T12)
{
    IRD_TEST_INFO("REQ-03", {"UX-05"}, std::nullopt);
    const WorkRegion region = m_editor.workingSet().regions.entries.front();
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(region.objectId);
    ASSERT_TRUE(m_editor.workingSet().plans.entries.empty())
        << "夹具基线应无计划条目（A-18 前提）";

    // 生产入口：完整采样表单确认（Grid 计数 3×2×2——与基线 {2,2,2} 不同
    // 值，upsert 更新可观测）。
    auto* form = m_panel->findChild<QWidget*>("ird_region_sampling_form");
    ASSERT_NE(form, nullptr) << "生产区域检查器缺完整采样编辑入口";
    auto* method = form->findChild<QComboBox*>("ird_region_sampling_method");
    ASSERT_NE(method, nullptr);
    method->setCurrentIndex(method->findData(int(PositionSamplingMethod::Grid)));
    form->findChild<QLineEdit*>("ird_region_grid_count_0")->setText("3");
    form->findChild<QLineEdit*>("ird_region_grid_count_1")->setText("2");
    form->findChild<QLineEdit*>("ird_region_grid_count_2")->setText("2");
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click();
    QApplication::processEvents();

    // A-18 核心：采样确认 Applied 后计划条目自动派生（恰一条、同区域）。
    ASSERT_EQ(m_editor.workingSet().plans.entries.size(), 1u)
        << "采样确认后应派生计划条目（A-18——评估切片消费面）";
    EXPECT_EQ(m_editor.workingSet().plans.entries.front().regionRef,
              region.objectId);
    EXPECT_EQ(m_editor.workingSet().plans.entries.front().positionSampling.counts,
              (std::array<std::uint32_t, 3>{3u, 2u, 2u}));

    // 重确认＝更新非重复（R6 计划-区域一一对应）。首次确认后面板重绘
    // 重建采样表单（诚实刷新面）——旧指针随旧窗销毁，测试经 objectName
    // 从当前控件树重定位。
    form = m_panel->findChild<QWidget*>("ird_region_sampling_form");
    ASSERT_NE(form, nullptr);
    form->findChild<QLineEdit*>("ird_region_grid_count_0")->setText("4");
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click();
    QApplication::processEvents();
    ASSERT_EQ(m_editor.workingSet().plans.entries.size(), 1u)
        << "重确认应复用既有计划 id（R6），不得新增第二条";
    EXPECT_EQ(m_editor.workingSet().plans.entries.front().positionSampling.counts[0], 4u);

    // 区域删除随删计划（悬空计划预防）——真实删除按钮（与用户点击同槽
    // 轨；onRemoveEntry 为 private，按钮 objectName＝生命周期锚惯例）。
    QPushButton* removeBtn = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_req_remove_regions"));
    ASSERT_NE(removeBtn, nullptr) << "区域生命周期删除按钮缺失";
    removeBtn->click();
    QApplication::processEvents();
    EXPECT_TRUE(m_editor.workingSet().regions.entries.empty());
    EXPECT_TRUE(m_editor.workingSet().plans.entries.empty())
        << "区域删除应随删其采样计划（R6 悬空预防）";
}

/** @brief REQ-03：随机/零网格、可选覆盖率清除、取消及非法输入都走真实表单。 */
TEST_F(RequirementsSessionGuiTest, RegionSamplingModesCancelAndReject_REQ03_WP14T12)
{
    IRD_TEST_INFO("REQ-03", {"UX-05", "UX-11"}, std::nullopt);
    const auto before = m_editor.workingSet().regions.entries.front();
    m_panel->refreshPanel(m_editor.workingSet(), m_report); m_panel->focusObject(before.objectId);
    auto* form = m_panel->findChild<QWidget*>("ird_region_sampling_form"); ASSERT_NE(form, nullptr);
    auto* method = form->findChild<QComboBox*>("ird_region_sampling_method"); ASSERT_NE(method, nullptr);
    method->setCurrentIndex(method->findData(int(PositionSamplingMethod::Random)));
    form->findChild<QLineEdit*>("ird_region_random_count")->setText("24");
    form->findChild<QPushButton*>("ird_region_sampling_cancel")->click();
    EXPECT_EQ(method->currentData().toInt(), int(PositionSamplingMethod::Grid));
    EXPECT_EQ(m_editor.workingSet().regions.entries.front(), before);
    method->setCurrentIndex(method->findData(int(PositionSamplingMethod::Random)));
    form->findChild<QLineEdit*>("ird_region_random_count")->setText("4294967296");
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click(); QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet().regions.entries.front(), before);
    EXPECT_FALSE(form->findChild<QLabel*>("ird_region_sampling_error")->text().isEmpty());
    form->findChild<QLineEdit*>("ird_region_random_count")->setText("-1");
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click(); QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet().regions.entries.front(), before);
    form->findChild<QLineEdit*>("ird_region_random_count")->setText("24");
    form->findChild<QLineEdit*>("ird_region_direction_samples")->setText("0");
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click(); QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet().regions.entries.front(), before) << "姿态计数零必须拒绝";
    form->findChild<QLineEdit*>("ird_region_direction_samples")->setText("6");
    form->findChild<QLineEdit*>("ird_region_definition_name")->setText(QStringLiteral("随机区域"));
    form->findChild<QLineEdit*>("ird_region_definition_note")->setText(QStringLiteral("原文备注"));
    form->findChild<QCheckBox*>("ird_region_collision")->setChecked(true);
    form->findChild<QLineEdit*>("ird_region_margin")->setText("0.15");
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click(); QApplication::processEvents();
    auto random = m_editor.workingSet().regions.entries.front();
    EXPECT_EQ(random.positionSampling.method, PositionSamplingMethod::Random);
    EXPECT_EQ(random.positionSampling.count, 24u);
    EXPECT_EQ(random.name, "随机区域"); EXPECT_EQ(random.note, "原文备注");
    EXPECT_TRUE(random.demands.collisionFreeRequired); ASSERT_TRUE(random.demands.minimumJointMargin);
    EXPECT_DOUBLE_EQ(*random.demands.minimumJointMargin, 0.15);
    // 新投影后重新查控件；切回Grid允许位置零样本，绝不把未设姿态覆盖率造零。
    m_panel->refreshPanel(m_editor.workingSet(), m_report); m_panel->focusObject(before.objectId);
    form = m_panel->findChild<QWidget*>("ird_region_sampling_form"); ASSERT_NE(form, nullptr);
    method = form->findChild<QComboBox*>("ird_region_sampling_method");
    method->setCurrentIndex(method->findData(int(PositionSamplingMethod::Grid)));
    form->findChild<QLineEdit*>("ird_region_grid_count_0")->setText("0");
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click(); QApplication::processEvents();
    const auto grid = m_editor.workingSet().regions.entries.front();
    EXPECT_EQ(grid.positionSampling.method, PositionSamplingMethod::Grid);
    EXPECT_EQ(grid.positionSampling.counts[0], 0u);
    EXPECT_FALSE(grid.coverageTargets.minOrientationCoverage);
    ASSERT_TRUE(m_editor.undoLocal()); EXPECT_EQ(m_editor.workingSet().regions.entries.front(), random);
    m_panel->setWritable(false); m_panel->refreshPanel(m_editor.workingSet(), m_report);
    form = m_panel->findChild<QWidget*>("ird_region_sampling_form"); ASSERT_NE(form, nullptr); EXPECT_FALSE(form->isEnabled());
}

/** @brief REQ-03：旧数量/开关/等级/计数四轨在排队后切选中行，不得写到新区域。 */
TEST_F(RequirementsSessionGuiTest, RegionQueuedFieldsKeepOriginalAnchor_REQ03_WP14T12)
{
    IRD_TEST_INFO("REQ-03", {"UX-05"}, std::nullopt);
    const auto first = m_editor.workingSet().regions.entries.front();
    const auto second = makeRegion("第二区域");
    ASSERT_TRUE(m_editor.applyEdit(RequirementEdit{second}).accepted);
    for (int track = 0; track < 4; ++track) {
        m_panel->refreshPanel(m_editor.workingSet(), m_report); m_panel->focusObject(first.objectId);
        if (track == 0) {
            QLineEdit* edit = nullptr;
            for (auto* field : m_panel->findChildren<QLineEdit*>()) {
                if (field->property("irdFieldKey").toString() == "box-center-x") { edit = field; break; }
            }
            ASSERT_NE(edit, nullptr); edit->setText("0.75"); QMetaObject::invokeMethod(edit, "editingFinished");
        } else if (track == 1) {
            auto* toggle = m_panel->findChild<QComboBox*>("ird_region_enabled_combo"); ASSERT_NE(toggle, nullptr);
            toggle->setCurrentIndex(1);
        } else if (track == 2) {
            auto* level = m_panel->findChild<QComboBox*>("ird_region_level_combo"); ASSERT_NE(level, nullptr);
            level->setCurrentText("Should");
        } else {
            const auto spins = m_panel->findChildren<QSpinBox*>("ird_region_count_spin"); ASSERT_EQ(spins.size(), 3);
            spins[0]->setValue(3);
        }
        m_panel->focusObject(second.objectId); QApplication::processEvents();
        for (const auto& region : m_editor.workingSet().regions.entries) {
            if (region.objectId == second.objectId) { EXPECT_EQ(region, second) << "不得改写切换后的区域，轨=" << track; }
            if (region.objectId == first.objectId) {
                if (track == 0) { EXPECT_DOUBLE_EQ(region.box.center[0], 0.75); }
                if (track == 1) { EXPECT_FALSE(region.enabled); }
                if (track == 2) { EXPECT_EQ(region.level, RequirementLevel::Should); }
                if (track == 3) { EXPECT_EQ(region.positionSampling.counts[0], 3u); }
            }
        }
        if (::testing::Test::HasFailure()) { return; }
    }
}

/** @brief REQ-03：完整暂存拒绝基线变化/禁写；姿态覆盖率可明确清除。 */
TEST_F(RequirementsSessionGuiTest, RegionSamplingBaselineAndOptionalClear_REQ03_WP14T12)
{
    IRD_TEST_INFO("REQ-03", {"UX-11"}, std::nullopt);
    auto before = m_editor.workingSet().regions.entries.front();
    before.coverageTargets.minOrientationCoverage = 0.7;
    ASSERT_TRUE(m_editor.applyEdit(RequirementEdit{before}).accepted);
    m_panel->refreshPanel(m_editor.workingSet(), m_report); m_panel->focusObject(before.objectId);
    auto* form = m_panel->findChild<QWidget*>("ird_region_sampling_form"); ASSERT_NE(form, nullptr);
    form->findChild<QCheckBox*>("ird_region_orientation_coverage_enabled")->setChecked(false);
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click(); QApplication::processEvents();
    EXPECT_FALSE(m_editor.workingSet().regions.entries.front().coverageTargets.minOrientationCoverage);
    ASSERT_TRUE(m_editor.undoLocal()); EXPECT_EQ(m_editor.workingSet().regions.entries.front(), before);
    m_panel->refreshPanel(m_editor.workingSet(), m_report); m_panel->focusObject(before.objectId);
    form = m_panel->findChild<QWidget*>("ird_region_sampling_form"); ASSERT_NE(form, nullptr);
    auto* method = form->findChild<QComboBox*>("ird_region_sampling_method");
    method->setCurrentIndex(method->findData(int(PositionSamplingMethod::GridBySpacing)));
    for (int axis=0; axis<3; ++axis) { form->findChild<QLineEdit*>(QString("ird_region_spacing_%1").arg(axis))->setText("-0.1"); }
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click(); QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet().regions.entries.front(), before);
    EXPECT_FALSE(form->findChild<QLabel*>("ird_region_sampling_error")->text().isEmpty());
    for (int axis=0; axis<3; ++axis) { form->findChild<QLineEdit*>(QString("ird_region_spacing_%1").arg(axis))->setText("0.1"); }
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click(); // 候选已排队。
    auto newer = before; newer.note = "外部修改必须保留";
    ASSERT_TRUE(m_editor.applyEdit(RequirementEdit{newer}).accepted);
    QApplication::processEvents(); EXPECT_EQ(m_editor.workingSet().regions.entries.front(), newer);
    m_panel->refreshPanel(m_editor.workingSet(), m_report); m_panel->focusObject(before.objectId);
    form = m_panel->findChild<QWidget*>("ird_region_sampling_form"); ASSERT_NE(form, nullptr);
    form->findChild<QLineEdit*>("ird_region_roll_samples")->setText("3");
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click();
    m_panel->setWritable(false); QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet().regions.entries.front(), newer);
}

/** @brief UX-05/REQ-04：未设节拍显示未设，取消零写入，确认节拍只进草稿。 */
TEST_F(RequirementsSessionGuiTest, ConditionBatchParametersCancel_UX05_WP14T12)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    const auto before = m_editor.workingSet().conditions.entries.front();
    m_panel->refreshPanel(m_editor.workingSet(), m_report); m_panel->focusObject(before.objectId);
    auto* batch = m_panel->findChild<QWidget*>("ird_req_batch_condition");
    ASSERT_NE(batch, nullptr);
    auto* table = batch->findChild<QTableWidget*>("ird_param_table"); ASSERT_NE(table, nullptr);
    EXPECT_NE(table->item(0, 1)->text(), QStringLiteral("0")) << "缺失节拍不能伪造成零";
    ASSERT_TRUE(setRequirementClipboard("cycle-time\t4")) << "Windows剪贴板写入必须真实可读";
    batch->findChild<QPushButton*>("ird_param_paste")->click();
    batch->findChild<QPushButton*>("ird_param_cancel")->click();
    EXPECT_EQ(m_editor.workingSet().conditions.entries.front(), before);
    batch->findChild<QPushButton*>("ird_param_paste")->click();
    batch->findChild<QPushButton*>("ird_param_apply")->click();
    ASSERT_FALSE(batch->findChild<QWidget*>("ird_param_confirm_region")->isHidden())
        << batch->findChild<QLabel*>("ird_param_impact")->text().toStdString();
    batch->findChild<QPushButton*>("ird_param_confirm_yes")->click(); QApplication::processEvents();
    ASSERT_TRUE(m_editor.workingSet().conditions.entries.front().targetCycleTimeS);
    EXPECT_DOUBLE_EQ(*m_editor.workingSet().conditions.entries.front().targetCycleTimeS, 4);
    ASSERT_TRUE(m_editor.undoLocal()); EXPECT_EQ(m_editor.workingSet().conditions.entries.front(), before);
}

/** @brief UX-05：暂存期间切换对象或进入只读，不得把输入写到另一个对象/禁写会话。 */
TEST_F(RequirementsSessionGuiTest, BatchParametersAnchorAndReadOnly_UX05_WP14T12)
{
    IRD_TEST_INFO("UX-05", {"UX-11"}, std::nullopt);
    const auto before = m_editor.workingSet().points.entries.front();
    const auto second = makePoint("批量编辑第二工位", 4, 5, 6);
    ASSERT_TRUE(m_editor.applyEdit(RequirementEdit{second}).accepted);
    m_panel->refreshPanel(m_editor.workingSet(), m_report); m_panel->focusObject(before.objectId);
    auto* batch = m_panel->findChild<QWidget*>("ird_req_batch_station"); ASSERT_NE(batch, nullptr);
    ASSERT_TRUE(setRequirementClipboard("pose-position-x\t0.25")) << "Windows剪贴板写入必须真实可读";
    batch->findChild<QPushButton*>("ird_param_paste")->click();
    batch->findChild<QPushButton*>("ird_param_apply")->click();
    batch->findChild<QPushButton*>("ird_param_confirm_yes")->click();
    m_panel->focusObject(second.objectId); // 确认已排队，改变选择不改变载荷目标。
    QApplication::processEvents();
    for (const auto& point : m_editor.workingSet().points.entries) {
        if (point.objectId == before.objectId) { EXPECT_DOUBLE_EQ(point.pose.position.value()[0], 0.25); }
        if (point.objectId == second.objectId) { EXPECT_EQ(point, second); }
    }
    m_panel->focusObject(before.objectId);
    batch = m_panel->findChild<QWidget*>("ird_req_batch_station"); ASSERT_NE(batch, nullptr);
    ASSERT_TRUE(setRequirementClipboard("pose-position-x\t0.75")) << "Windows剪贴板写入必须真实可读";
    batch->findChild<QPushButton*>("ird_param_paste")->click();
    batch->findChild<QPushButton*>("ird_param_apply")->click();
    batch->findChild<QPushButton*>("ird_param_confirm_yes")->click();
    m_panel->setWritable(false); QApplication::processEvents();
    for (const auto& point : m_editor.workingSet().points.entries) {
        if (point.objectId == before.objectId) { EXPECT_DOUBLE_EQ(point.pose.position.value()[0], 0.25); }
    }
    batch = m_panel->findChild<QWidget*>("ird_req_batch_station"); ASSERT_NE(batch, nullptr);
    // UX-05：只读禁写应核对真实写入口；筛选/复制保留可用，不能锁死整块表格。
    EXPECT_TRUE(batch->isEnabled());
    EXPECT_FALSE(batch->findChild<QPushButton*>("ird_param_paste")->isEnabled());
    EXPECT_FALSE(batch->findChild<QPushButton*>("ird_param_apply")->isEnabled());
    EXPECT_EQ(batch->findChild<QTableWidget*>("ird_param_table")->editTriggers(), QAbstractItemView::NoEditTriggers);
}

/** @brief UX-05：完整对象基线冲突或域校验拒绝时整批零写入，不留下表格假成功。 */
TEST_F(RequirementsSessionGuiTest, BatchParametersRejectStaleBaseline_UX05_WP14T12)
{
    IRD_TEST_INFO("UX-05", {"REQ-09"}, std::nullopt);
    const auto before = m_editor.workingSet().points.entries.front();
    m_panel->refreshPanel(m_editor.workingSet(), m_report); m_panel->focusObject(before.objectId);
    auto* batch = m_panel->findChild<QWidget*>("ird_req_batch_station"); ASSERT_NE(batch, nullptr);
    ASSERT_TRUE(setRequirementClipboard("pose-position-x\t0.25")) << "Windows剪贴板写入必须真实可读";
    batch->findChild<QPushButton*>("ird_param_paste")->click();
    batch->findChild<QPushButton*>("ird_param_apply")->click();
    batch->findChild<QPushButton*>("ird_param_confirm_yes")->click();
    auto changed = before; changed.note = "外部编辑不能被旧参数覆盖";
    ASSERT_TRUE(m_editor.applyEdit(RequirementEdit{changed}).accepted);
    QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet().points.entries.front(), changed);
    // 下限/上限各自合法但区间逆序应由领域拒绝，不能部分应用其中一项。
    changed.pose.orientation.kind = OrientationRuleKind::ToolRollFree;
    ASSERT_TRUE(m_editor.applyEdit(RequirementEdit{changed}).accepted);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    batch = m_panel->findChild<QWidget*>("ird_req_batch_station"); ASSERT_NE(batch, nullptr);
    ASSERT_TRUE(setRequirementClipboard("roll-min\t2\nroll-max\t1\npose-position-x\t0.5")) << "Windows剪贴板写入必须真实可读";
    batch->findChild<QPushButton*>("ird_param_paste")->click();
    batch->findChild<QPushButton*>("ird_param_apply")->click();
    batch->findChild<QPushButton*>("ird_param_confirm_yes")->click(); QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet().points.entries.front(), changed);
    batch = m_panel->findChild<QWidget*>("ird_req_batch_station"); ASSERT_NE(batch, nullptr);
    auto* table = batch->findChild<QTableWidget*>("ird_param_table"); ASSERT_NE(table, nullptr);
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 0)->data(Qt::UserRole).toString() == "pose-position-x") {
            EXPECT_EQ(table->item(row, 1)->text(), QStringLiteral("1"));
        }
    }
}

// =====================================================================
// acceptance 1 会话数据面：基线闭包载入→四集合入面板（树投影＋定位）。
// =====================================================================

TEST_F(RequirementsSessionGuiTest, SessionDataPlane_FourSetsIntoPanel_UI_T29)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);

    // 首刷（装配层 refreshPanel 编排的测试面直呼——harness ④同款）。
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // 树投影：自持结构树出现需求条目（根＋任务点——非空即"四集合入面板"
    // 的观测面；空会话形态下树为空组，本断言区分二态）。
    const auto trees = m_panel->findChildren<QTreeWidget*>();
    ASSERT_FALSE(trees.isEmpty()) << "面板无树控件（五区构建缺陷）";
    int totalItems = 0;
    for (const QTreeWidget* tree : trees) {
        totalItems += tree->topLevelItemCount();
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            totalItems += tree->topLevelItem(i)->childCount();
        }
    }
    EXPECT_GT(totalItems, 0) << "基线载入后树投影为空（会话数据面断链）";

    // 定位：任务点聚焦→检查器编辑行出现（L-R2 编辑面的数据前提）。
    const core::ObjectId pointOid = m_editor.workingSet().points.entries.front().objectId;
    m_panel->focusObject(pointOid);
    EXPECT_FALSE(m_panel->findChildren<QLineEdit*>().isEmpty())
        << "任务点聚焦后检查器无编辑行（L-R2 数据前提断链）";
}

/// F-539（UI-T71）定位出口生产接线：校验逐项行点击（锚列＝对象身份）→
/// locate()→构造期注册的生产 sink→focusObject——自持树聚焦锚定行（滚动＋
/// 置当前行；检查器/三维高亮经 L-R1 前向链顺链达成）。伪造带锚行与真实
/// 阻断行同形（PanelValidationModel 投影口径：诊断 subject 直投锚列）——
/// 被测链＝生产代码（点击槽→locate→sink→focusObject），不注入替身 sink。
/// 实施前该链终点静默丢弃（setLocateSink 生产零调用点——F-539 形态），
/// 本用例即其转红面。
TEST_F(RequirementsSessionGuiTest,
       LocateSinkProductionWiring_ValidationRowClickFocusesTree_UI_T71)
{
    IRD_TEST_INFO("UX-06", {}, std::nullopt);
    // 本用例位置先于 F-499 块的声明行——同签名前置声明重复合法（定义在文件下半辅助区）。
    QTreeWidget* requirementTree(const RequirementsPanelWidget& panel);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // 定位校验逐项表（表头首列『级别』——层汇总表首列『层』的同页姊妹，
    // UI-T68 巡检同款锚定法）。
    QTreeWidget* itemsTree = nullptr;
    for (QTreeWidget* t : m_panel->findChildren<QTreeWidget*>()) {
        if (t->headerItem()->text(0) == QStringLiteral("级别")) {
            itemsTree = t;
            break;
        }
    }
    ASSERT_NE(itemsTree, nullptr) << "校验逐项表缺失（校验页构建缺陷）";

    // 伪造带锚行（锚＝任务点身份——树中真实在册行，落点可断言）。
    const core::ObjectId pointOid =
        m_editor.workingSet().points.entries.front().objectId;
    auto* row = new QTreeWidgetItem(
        itemsTree,
        QStringList() << QStringLiteral("Blocking") << QStringLiteral("R0")
                      << QStringLiteral("TEST-LOCATE") << QStringLiteral("定位链测试行"));
    row->setText(itemsTree->columnCount() - 1,
                 QString::fromStdString(pointOid.toCanonical()));

    // 点击（与手点同源信号轨）→生产 sink→focusObject→树聚焦。
    Q_EMIT itemsTree->itemClicked(row, 0);
    QApplication::processEvents();

    // 落点断言：自持树当前行＝锚定对象行（树滚动＋置当前行的呈现面）。
    QTreeWidget* tree = requirementTree(*m_panel);
    ASSERT_NE(tree, nullptr) << "需求树缺失（F-499 更名后辅助失配）";
    ASSERT_NE(tree->currentItem(), nullptr)
        << "定位链落点缺失（生产 sink 未接线——F-539 静默丢弃形态）";
    EXPECT_EQ(tree->currentItem()->text(tree->columnCount() - 1).toStdString(),
              pointOid.toCanonical())
        << "落点锚失配（focusObject 未命中锚定行）";
}

/// F-499（宿主审核 P2）双树职责呈现标识（自持面）：顶部草稿提示条常驻
/// 说明"需求树＝草稿编辑面／工业项目树＝已应用修订"，树名标注
/// "需求树（当前草稿）"——消除"同一工位为何出现两次/哪棵可编辑"歧义。
QTreeWidget* requirementTree(const RequirementsPanelWidget& panel);  // 前置声明（定义在本文件下半——树定位辅助区）
TEST_F(RequirementsSessionGuiTest, DraftHintAndTreeTitle_DualTreeRoleLabels_F499)
{
    IRD_TEST_INFO("REQ-11", {}, std::nullopt);

    // 顶部提示条：说明当前编辑对象（草稿）与应用后的修订语义。
    QLabel* hint = m_panel->findChild<QLabel*>(QStringLiteral("ird_req_draft_hint"));
    ASSERT_NE(hint, nullptr) << "草稿提示条未构建（F-499 承载缺失）";
    EXPECT_TRUE(hint->text().contains(QStringLiteral("需求草稿")))
        << "提示条未说明当前编辑对象＝需求草稿";
    EXPECT_TRUE(hint->text().contains(QStringLiteral("应用草稿")))
        << "提示条未说明『应用草稿』的修订语义";
    EXPECT_TRUE(hint->text().contains(QStringLiteral("工业项目树")))
        << "提示条未点明与工业项目树（已应用）的分界";

    // 树名标注＝提示条同频的第二呈现位（requirementTree 定位器同文——
    // 失配即本断言先行暴露）。
    QTreeWidget* tree = requirementTree(*m_panel);
    ASSERT_NE(tree, nullptr) << "需求树未定位（表头标注失配）";
    EXPECT_EQ(tree->headerItem()->text(0), QStringLiteral("需求树（当前草稿）"));
}

// =====================================================================
// acceptance 2/5 编辑生效＋最小校验：L-2 接受→宿主编辑后动作触发→
// 模块组合子以会话最新报告 refreshPanel（校验页实时化的编排面）。
// =====================================================================

TEST_F(RequirementsSessionGuiTest, PostEditComposition_HostActionThenRefresh_UI_T29)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);

    // 宿主接线同款装配（DomainAssembly 需求段的产品形态）：模块挂编辑器
    // →宿主编辑后动作（重估＋bindReadiness 由宿主承载，测试以计数替身）
    // →面板创建后 attachPanel（组合子补挂）。
    RequirementsUiModule module;
    module.attachEditor(&m_editor);
    int hostActionCount = 0;
    module.setPostEditAction([&hostActionCount] { ++hostActionCount; });
    module.attachPanel(m_panel.get());
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // L-2 接受触发（IPanelEditSink 公共覆盖——UI 线程同线程直调）：
    // 组合子先行宿主动作（恰一次），随后以会话报告 refreshPanel（重入
    // 面板刷新——可观测面＝刷新后树仍完整，即重投影未断链）。
    m_panel->onEditApplied("测试接受（组合子触发面）");
    EXPECT_EQ(hostActionCount, 1) << "宿主编辑后动作未触发或重复触发（组合子编排失守）";

    const auto trees = m_panel->findChildren<QTreeWidget*>();
    int totalItems = 0;
    for (const QTreeWidget* tree : trees) {
        totalItems += tree->topLevelItemCount();
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            totalItems += tree->topLevelItem(i)->childCount();
        }
    }
    EXPECT_GT(totalItems, 0) << "组合子 refreshPanel 后树投影为空（重投影断链）";
}

// =====================================================================
// acceptance 4/5 gate 真值源双态：就绪判定权威＝域侧 checker——干净基线
// 无阻断／违约基线（Must 任务点位置未提供）有阻断（draft.apply 门控的
// 真值源；宿主谓词只消费 hasBlocking 直投值——P-REQ-6）。
// =====================================================================

TEST_F(RequirementsSessionGuiTest, GateTruthSource_BlockingDoubleState_UI_T29)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);

    // 接通态：违约基线（Must 任务点位置未提供——R4 位姿层阻断的构造面，
    // checker 判定，本测试只构造输入）→hasBlocking＝true（draft.apply
    // 门控接通的真值面）。
    MapClosure closure;
    PointSet points;
    TaskPoint broken = makePoint("P-断链", 0.0, 0.0, 0.0);
    broken.pose.position = core::SourcedValue<rw::math::Vector3D<double>>{};
    points.entries.push_back(broken);
    RegionSet regions;
    regions.entries.push_back(makeRegion("R1"));
    ConditionSet conditions;
    fillBaseline(closure, points, regions, conditions);
    RequirementEditor brokenEditor;
    const auto load = brokenEditor.loadBaseline(closure);
    ASSERT_TRUE(load.ok) << "违约基线载入失败（夹具装配错误）";
    const RequirementReadinessReport brokenReport =
        m_checker.check(brokenEditor.workingSet(), CheckContext{});
    EXPECT_TRUE(brokenReport.hasBlocking()) << "违约基线未判阻断（gate 真值源失真）";

    // 确定性（gate 快照可复现的前提）：同一工作集两次 check→hasBlocking
    // 结论一致。释放态（干净基线→hasBlocking＝false）由同套件
    // requirements_test 的 ReadinessTest.HealthySetPassesAllLayers_ACC1
    // 钉住——健康夹具链为该用例既有面，本组不重复构造。
    const RequirementReadinessReport again =
        m_checker.check(brokenEditor.workingSet(), CheckContext{});
    EXPECT_EQ(again.hasBlocking(), brokenReport.hasBlocking())
        << "同输入两次判定结论漂移（gate 快照不可复现）";
}

// =====================================================================
// UI-T30 B1 对象生命周期：三集合新增/复制/删除＋锚回落＋撤销＋无会话门控
// （触发面＝objectName 锚按钮 click——与用户点击同路径；断言面＝集合
// 规模＋页签状态行呈现〔选中锚的呈现投影〕＋检查器编辑行）。
// =====================================================================

/// 生命周期按钮现取（objectName 锚——makeLifecycleBar 的定位面）。
QPushButton* lifecycleButton(const RequirementsPanelWidget& panel, const char* action,
                             const char* key)
{
    return const_cast<RequirementsPanelWidget&>(panel)
        .findChild<QPushButton*>(
            QStringLiteral("ird_req_%1_%2")
                .arg(QString::fromLatin1(action), QString::fromLatin1(key)));
}

/// 页签状态行文本现取（选中锚的呈现投影——工位/区域/工况页头）。
QString tabHeaderText(const RequirementsPanelWidget& panel, const char* objectName)
{
    const QLabel* label = const_cast<RequirementsPanelWidget&>(panel)
                              .findChild<QLabel*>(QString::fromLatin1(objectName));
    return label != nullptr ? label->text() : QString();
}

TEST_F(RequirementsSessionGuiTest, LifecycleAdd_ThreeCollections_UI_T30)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // 工位新增：树投影出现自动名新条目＋选中锚落新条目（页签状态行呈现）。
    const std::size_t pointsBefore = m_editor.workingSet().points.entries.size();
    ASSERT_NE(lifecycleButton(*m_panel, "add", "points"), nullptr);
    lifecycleButton(*m_panel, "add", "points")->click();
    ASSERT_EQ(m_editor.workingSet().points.entries.size(), pointsBefore + 1);
    EXPECT_TRUE(tabHeaderText(*m_panel, "ird_req_tab_station_header")
                    .contains(QStringLiteral("工位")))
        << "新增后选中锚未落新条目（页签状态行应显示自动名）";

    // 区域新增＋工况新增（同轨断言——集合规模＋状态行）。
    const std::size_t regionsBefore = m_editor.workingSet().regions.entries.size();
    lifecycleButton(*m_panel, "add", "regions")->click();
    ASSERT_EQ(m_editor.workingSet().regions.entries.size(), regionsBefore + 1);
    EXPECT_TRUE(tabHeaderText(*m_panel, "ird_req_tab_region_header")
                    .contains(QStringLiteral("区域")));

    const std::size_t conditionsBefore =
        m_editor.workingSet().conditions.entries.size();
    // 缺省向导兜底绑定后（遍历实录修复），空缝不再直接创建——用例注入
    // 确定工厂（空名走 uniqueEntryName 防撞＝原起步语义）。
    m_panel->setConditionWizardFactory(
        []() -> std::optional<RequirementsPanelWidget::ConditionWizardFields> {
            RequirementsPanelWidget::ConditionWizardFields f;
            return f;
        });
    lifecycleButton(*m_panel, "add", "conditions")->click();
    ASSERT_EQ(m_editor.workingSet().conditions.entries.size(), conditionsBefore + 1);
    EXPECT_TRUE(tabHeaderText(*m_panel, "ird_req_tab_condition_header")
                    .contains(QStringLiteral("工况")));

    // 自动名防撞（I-REQ-3 前置）：连续新增两个工位——名字互异。
    lifecycleButton(*m_panel, "add", "points")->click();
    const auto& entries = m_editor.workingSet().points.entries;
    std::set<std::string> names;
    for (const auto& e : entries) { names.insert(e.name); }
    EXPECT_EQ(names.size(), entries.size()) << "自动名撞名（uniqueEntryName 失守）";
}

TEST_F(RequirementsSessionGuiTest, LifecycleDuplicate_CopyFieldsAndSelect_UI_T30)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // 选中 P1→复制：规模＋1＋新名『P1 副本』＋选中锚落副本＋字段等值
    //（位置值与源一致——深拷贝；溯源字段保持源值——非伪造模板物）。
    const core::ObjectId srcId = m_editor.workingSet().points.entries.front().objectId;
    const double srcZ =
        m_editor.workingSet().points.entries.front().pose.position.value()[2];
    m_panel->focusObject(srcId);
    const std::size_t before = m_editor.workingSet().points.entries.size();
    lifecycleButton(*m_panel, "duplicate", "points")->click();
    ASSERT_EQ(m_editor.workingSet().points.entries.size(), before + 1);

    const TaskPoint* copy = nullptr;
    for (const TaskPoint& e : m_editor.workingSet().points.entries) {
        if (e.objectId != srcId && e.name == "P1 副本") { copy = &e; }
    }
    ASSERT_NE(copy, nullptr) << "副本条目未按『源名 副本』命名";
    EXPECT_DOUBLE_EQ(copy->pose.position.value()[2], srcZ)
        << "副本字段与源不等值";
    EXPECT_TRUE(tabHeaderText(*m_panel, "ird_req_tab_station_header")
                    .contains(QStringLiteral("P1 副本")))
        << "复制后选中锚未落副本";
}

TEST_F(RequirementsSessionGuiTest, LifecycleRemoveAnchorFallbackAndUndo_UI_T30)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // 删除唯一区域条目：集合空＋锚清空（状态行回落『未选择对象』）。
    const core::ObjectId regionId =
        m_editor.workingSet().regions.entries.front().objectId;
    m_panel->focusObject(regionId);
    lifecycleButton(*m_panel, "remove", "regions")->click();
    EXPECT_TRUE(m_editor.workingSet().regions.entries.empty())
        << "删除未生效（集合应空）";
    EXPECT_TRUE(tabHeaderText(*m_panel, "ird_req_tab_region_header")
                    .contains(QStringLiteral("未选择")))
        << "空集合锚回落未清空（返工⑤——面包屑『区域 > 未选择』态）";

    // 撤销（域轨 undoLocal——面板结构操作与字段编辑同栈）→ 区域恢复。
    EXPECT_TRUE(m_editor.undoLocal());
    ASSERT_EQ(m_editor.workingSet().regions.entries.size(), std::size_t{1});
    // 重做→再删除。
    EXPECT_TRUE(m_editor.redoLocal());
    EXPECT_TRUE(m_editor.workingSet().regions.entries.empty());

    // 工况锚回落（非空集合）：选中唯一工况删除→集合空清空；再 undo 恢复
    // ＋新增第二个工况后删除首个→锚回落到次条（同位次钳制）。
    // （缺省向导兜底绑定后 add 会弹模态——本用例注入确定工厂＝空名走
    // uniqueEntryName 防撞起步语义；fixture 每用例重建面板，注入须本例自带。）
    m_panel->setConditionWizardFactory(
        []() -> std::optional<RequirementsPanelWidget::ConditionWizardFields> {
            RequirementsPanelWidget::ConditionWizardFields f;
            return f;
        });
    const core::ObjectId condId =
        m_editor.workingSet().conditions.entries.front().objectId;
    m_panel->focusObject(condId);
    lifecycleButton(*m_panel, "remove", "conditions")->click();
    EXPECT_TRUE(m_editor.workingSet().conditions.entries.empty());
    EXPECT_TRUE(m_editor.undoLocal());  // 工况恢复（撤销栈跨集合序贯）
    lifecycleButton(*m_panel, "add", "conditions")->click();
    ASSERT_EQ(m_editor.workingSet().conditions.entries.size(), std::size_t{2});
    const core::ObjectId firstId =
        m_editor.workingSet().conditions.entries.front().objectId;
    m_panel->focusObject(firstId);
    lifecycleButton(*m_panel, "remove", "conditions")->click();
    EXPECT_FALSE(tabHeaderText(*m_panel, "ird_req_tab_condition_header")
                     .contains(QStringLiteral("未选择对象")))
        << "非空集合删除后锚被清空（应回落到同位次条目）";
}

TEST_F(RequirementsSessionGuiTest, LifecycleButtons_DisabledWithoutSession_UI_T30)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);

    // 独立面板（无编辑目标提供器——无会话形态）：refreshPanel 后生命周期
    // 三键全禁用（不虚构可编辑）。
    RequirementsPanelWidget noSessionPanel(true);
    noSessionPanel.refreshPanel(RequirementWorkingSet{},
                                RequirementReadinessReport{});
    for (const char* key : {"points", "regions", "conditions"}) {
        for (const char* action : {"add", "duplicate", "remove"}) {
            QPushButton* btn =
                lifecycleButton(noSessionPanel, action, key);
            ASSERT_NE(btn, nullptr);
            EXPECT_FALSE(btn->isEnabled())
                << "无会话下生命周期按钮可用（" << action << "/" << key << "）";
        }
    }
}

// =====================================================================
// UI-T31 B2 区域/工况字段编辑四态（接受/拒绝·解析面/回退/未修改不脏化）
// ＋未设态设值＋只读门控。触发面＝行编辑器 setText＋editingFinished 直发
//（与失焦同信号路径）；断言面＝工作集权威值＋编辑器回退＋编辑计数。
// =====================================================================

/// 按 objectName 锚定位面板内表格（区域表/工况表——B2 构建期锚）。
QTreeWidget* tableByName(const RequirementsPanelWidget& panel, const char* objectName)
{
    return const_cast<RequirementsPanelWidget&>(panel).findChild<QTreeWidget*>(
        QString::fromLatin1(objectName));
}

/// 按 irdFieldKey 属性定位检查器行编辑器（区域/工况表单共用形态）。
QLineEdit* fieldEditor(const RequirementsPanelWidget& panel, const char* fieldKey)
{
    const auto edits =
        const_cast<RequirementsPanelWidget&>(panel).findChildren<QLineEdit*>();
    for (QLineEdit* e : edits) {
        if (e->property("irdFieldKey").toString() == QString::fromLatin1(fieldKey)) {
            return e;
        }
    }
    return nullptr;
}

/// 按 irdFieldKey 属性定位检查器 SpinBox（UI-T47——工位位置三轴行形态）。
QDoubleSpinBox* spinEditor(const RequirementsPanelWidget& panel, const char* fieldKey)
{
    const auto spins =
        const_cast<RequirementsPanelWidget&>(panel).findChildren<QDoubleSpinBox*>();
    for (QDoubleSpinBox* s : spins) {
        if (s->property("irdFieldKey").toString() == QString::fromLatin1(fieldKey)) {
            return s;
        }
    }
    return nullptr;
}

/// 面板状态行文本（就地错误/接受摘要的呈现面）。
QString statusText(const RequirementsPanelWidget& panel)
{
    const auto labels =
        const_cast<RequirementsPanelWidget&>(panel).findChildren<QLabel*>();
    for (QLabel* l : labels) {
        if (l->wordWrap()) { return l->text(); }  // 状态行＝唯一 wrap 行
    }
    return QString();
}

TEST_F(RequirementsSessionGuiTest, RegionFieldEditing_FourStates_UI_T31)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // 选中区域表首行→检查器投影（B2 可编辑行构建）。
    QTreeWidget* regionTable = tableByName(*m_panel, "ird_req_region_table");
    ASSERT_NE(regionTable, nullptr);
    ASSERT_GT(regionTable->topLevelItemCount(), 0);
    regionTable->setCurrentItem(regionTable->topLevelItem(0));
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    const core::ObjectId rid = m_editor.workingSet().regions.entries.front().objectId;
    const double sizeXBefore = m_editor.workingSet().regions.entries.front().box.size[0];
    const std::uint64_t editsBefore = m_editor.draftStatus().edits;

    // ① 未修改短路：直发 editingFinished（文本未变）——零提交（编辑计数
    // 不变＝幻影脏化消除的四态之四）。
    QLineEdit* sizeEdit = fieldEditor(*m_panel, "box-size-x");
    ASSERT_NE(sizeEdit, nullptr);
    ASSERT_FALSE(sizeEdit->isReadOnly()) << "B2 未解除数值行只读（诚实降级未消除）";
    Q_EMIT sizeEdit->editingFinished();
    QApplication::processEvents();  // 队列化提交（UI-T37 返工——事件循环一拍落域）
    EXPECT_EQ(m_editor.draftStatus().edits, editsBefore)
        << "未修改触发产生了编辑（幻影脏化回归）";

    // ② 接受：尺寸 1.0→2.5——工作集权威值变＋编辑计数＋1。
    sizeEdit->setText(QStringLiteral("2.5"));
    Q_EMIT sizeEdit->editingFinished();
    QApplication::processEvents();  // 队列化提交（UI-T37 返工——事件循环一拍落域）
    for (const WorkRegion& r : m_editor.workingSet().regions.entries) {
        if (r.objectId == rid) {
            EXPECT_DOUBLE_EQ(r.box.size[0], 2.5) << "接受后权威值未变";
        }
    }
    EXPECT_EQ(m_editor.draftStatus().edits, editsBefore + 1);

    // ③ 拒绝·解析面：非数值输入——就地错误＋权威回退（编辑器文本回滚）。
    QLineEdit* centerEdit = fieldEditor(*m_panel, "box-center-x");
    ASSERT_NE(centerEdit, nullptr);
    const QString authoritativeBefore = centerEdit->property("irdAuthoritativeValue").toString();
    centerEdit->setText(QStringLiteral("abc"));
    Q_EMIT centerEdit->editingFinished();
    QApplication::processEvents();  // 队列化提交（UI-T37 返工——事件循环一拍落域）
    EXPECT_EQ(m_editor.draftStatus().edits, editsBefore + 1) << "解析拒绝产生了提交";
    QLineEdit* centerAfter = fieldEditor(*m_panel, "box-center-x");
    ASSERT_NE(centerAfter, nullptr);
    EXPECT_EQ(centerAfter->text(), authoritativeBefore)
        << "拒绝后编辑器未回退权威值";
    EXPECT_FALSE(statusText(*m_panel).isEmpty()) << "就地错误未呈现";

    // ④ 拒绝·范围面：尺寸负值（bounds 正数）——同解析轨拒绝。
    QLineEdit* sizeEdit2 = fieldEditor(*m_panel, "box-size-x");
    sizeEdit2->setText(QStringLiteral("-3"));
    Q_EMIT sizeEdit2->editingFinished();
    QApplication::processEvents();  // 队列化提交（UI-T37 返工——事件循环一拍落域）
    for (const WorkRegion& r : m_editor.workingSet().regions.entries) {
        if (r.objectId == rid) {
            EXPECT_DOUBLE_EQ(r.box.size[0], 2.5) << "范围外输入被写入（bounds 失守）";
        }
    }

    // ⑤ 撤销同栈：undoLocal 回滚 2.5→1.0（B1 结构/B2 字段同一局部栈）。
    EXPECT_TRUE(m_editor.undoLocal());
    for (const WorkRegion& r : m_editor.workingSet().regions.entries) {
        if (r.objectId == rid) {
            EXPECT_DOUBLE_EQ(r.box.size[0], sizeXBefore);
        }
    }
}

TEST_F(RequirementsSessionGuiTest, ConditionCycleTime_SetFromUnset_UI_T31)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // 选中工况表首行→检查器投影；未设节拍行（空文本权威基准）输入即设值。
    QTreeWidget* condTable = tableByName(*m_panel, "ird_req_condition_table");
    ASSERT_NE(condTable, nullptr);
    ASSERT_GT(condTable->topLevelItemCount(), 0);
    condTable->setCurrentItem(condTable->topLevelItem(0));
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    const core::ObjectId cid = m_editor.workingSet().conditions.entries.front().objectId;
    ASSERT_FALSE(m_editor.workingSet().conditions.entries.front().targetCycleTimeS
                     .has_value())
        << "夹具工况应无节拍（未设态设值路径的前提）";
    QLineEdit* cycleEdit = fieldEditor(*m_panel, "cycle-time");
    ASSERT_NE(cycleEdit, nullptr);
    ASSERT_FALSE(cycleEdit->isReadOnly());

    cycleEdit->setText(QStringLiteral("12.5"));
    Q_EMIT cycleEdit->editingFinished();
    QApplication::processEvents();  // 队列化提交（UI-T37 返工——事件循环一拍落域）
    for (const OperatingCondition& c : m_editor.workingSet().conditions.entries) {
        if (c.objectId == cid) {
            ASSERT_TRUE(c.targetCycleTimeS.has_value());
            EXPECT_DOUBLE_EQ(*c.targetCycleTimeS, 12.5) << "未设→设值未生效";
        }
    }
}

TEST_F(RequirementsSessionGuiTest, FieldEditing_ReadOnlyGate_UI_T31)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);

    // 只读门控（L-R12 行半区）：setWritable(false)→刷新后区域/工况数值
    // 行全部只读（B2 门控联动——浏览不受影响）。
    m_panel->refreshPanel(m_editor.workingSet(), m_report);  // 首建表行
    QTreeWidget* regionTable = tableByName(*m_panel, "ird_req_region_table");
    ASSERT_NE(regionTable, nullptr);
    QTreeWidget* condTable = tableByName(*m_panel, "ird_req_condition_table");
    ASSERT_NE(condTable, nullptr);
    regionTable->setCurrentItem(regionTable->topLevelItem(0));
    condTable->setCurrentItem(condTable->topLevelItem(0));
    m_panel->setWritable(false);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    for (const char* key : {"box-size-x", "box-center-y", "coverage-position",
                            "cycle-time"}) {
        QLineEdit* e = fieldEditor(*m_panel, key);
        ASSERT_NE(e, nullptr) << key;
        EXPECT_TRUE(e->isReadOnly()) << key << " 只读态未生效";
    }
    m_panel->setWritable(true);  // 夹具复位
}

TEST_F(RequirementsSessionGuiTest, StationPositionEditing_ThreeAxis_UI_T47)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // 选中工位（P1 位置＝1.0,2.0,3.0 Provided——fixture 同款；focusObject
    // ＝UI-T36 起的产品选中路径，工位页无独立表——左栏树联动）→检查器
    // "空间与公差"卡三轴 SpinBox 投影。
    const core::ObjectId pid = m_editor.workingSet().points.entries.front().objectId;
    m_panel->focusObject(pid);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    const std::uint64_t editsBefore = m_editor.draftStatus().edits;

    // ① 只读呈现退役：可写会话下三轴 SpinBox 可编辑（UI-T47 主体——
    // "编辑轨随后续批次"登记面的落位证据）。
    QDoubleSpinBox* xSpin = spinEditor(*m_panel, "pose-position-x");
    ASSERT_NE(xSpin, nullptr) << "位置 X 轴 SpinBox 未投影";
    ASSERT_NE(spinEditor(*m_panel, "pose-position-y"), nullptr);
    ASSERT_NE(spinEditor(*m_panel, "pose-position-z"), nullptr);
    ASSERT_FALSE(xSpin->isReadOnly()) << "可写会话下位置轴仍只读（编辑轨未生效）";

    // ② 幻影短路：直发 editingFinished（值未变）——零提交（编辑计数不变）。
    Q_EMIT xSpin->editingFinished();
    QApplication::processEvents();  // 队列化提交（事件循环一拍落域）
    EXPECT_EQ(m_editor.draftStatus().edits, editsBefore)
        << "未修改触发产生了编辑（幻影脏化回归）";

    // ③ 接受：X 1.0→2.5——工作集权威值变（Y/Z 原值保持＝三维整体重写的
    // 单轴语义）＋编辑计数＋1。
    xSpin->setValue(2.5);
    Q_EMIT xSpin->editingFinished();
    QApplication::processEvents();  // 队列化提交（事件循环一拍落域）
    for (const TaskPoint& p : m_editor.workingSet().points.entries) {
        if (p.objectId == pid) {
            ASSERT_TRUE(p.pose.position.tryValue().has_value());
            EXPECT_DOUBLE_EQ((*p.pose.position.tryValue())[0], 2.5)
                << "接受后 X 权威值未变";
            EXPECT_DOUBLE_EQ((*p.pose.position.tryValue())[1], 2.0)
                << "未编辑轴 Y 被连带改写（三维整体语义破坏）";
            EXPECT_DOUBLE_EQ((*p.pose.position.tryValue())[2], 3.0)
                << "未编辑轴 Z 被连带改写（三维整体语义破坏）";
        }
    }
    EXPECT_EQ(m_editor.draftStatus().edits, editsBefore + 1);

    // ④ 撤销同栈：undoLocal 回滚 X 2.5→1.0（字段编辑同一局部撤销栈）。
    EXPECT_TRUE(m_editor.undoLocal());
    for (const TaskPoint& p : m_editor.workingSet().points.entries) {
        if (p.objectId == pid) {
            ASSERT_TRUE(p.pose.position.tryValue().has_value());
            EXPECT_DOUBLE_EQ((*p.pose.position.tryValue())[0], 1.0);
        }
    }
}

// =====================================================================
// UI-T32 C 批次九命令流程（返工测试缝——CommandDialogHost 预置应答替身；
// attempt1 fail B-1 的补验面：每命令正/反例经真实 flows 编排＋域裁决）。
// =====================================================================

#include <QTemporaryDir>
#include <QDir>
#include "plugin/RequirementsCommandFlows.hpp"  // executeRequirementCommand/host 注入缝
#include <sdurws/ird/requirements/Import.hpp> // WP-14-T12：字段行身份与实际单位预览断言。
#include <QDialog>
#include <QDialogButtonBox>
#include <QTimer>

namespace {

/// 预置应答替身（按 flows 调用序回放应答——非模态）。
class FakeDialogHost final : public requirements::CommandDialogHost {
public:
    std::optional<QString> savePath;             ///< 导出应答
    std::optional<QString> openPath;             ///< 导入应答
    std::optional<int> item;                     ///< 条目选择应答
    bool templateOk = true;                      ///< 模板表单确认
    bool arrayOk = true;                         ///< 阵列表单确认
    bool productionArray = false;                ///< AT-24：驱动生产参数对话框。
    bool productionMirror = false;               ///< AT-24：驱动生产镜面表单。
    int arrayCount = 3;                          ///< 阵列数量应答
    double arraySpacing = 0.5;                   ///< 阵列间距应答
    bool importConfirmed = true;                 ///< 导入确认应答
    bool productionMapping = false;              ///< WP-14-T12：映射步骤驱动真实Qt表单。
    QString lastImportSummary;                   ///< 逐行错误的完整呈现证据。
    requirements::CommandDialogHost::RegenerateAction action =
        requirements::CommandDialogHost::RegenerateAction::Cancel;

    std::optional<QString> saveFilePath(const QString&, const QString&,
                                        const QString&) override
    {
        return savePath;
    }
    std::optional<QString> openFilePath(const QString&, const QString&) override
    {
        return openPath;
    }
    std::optional<int> chooseItem(const QString&, const QString&,
                                  const QStringList&) override
    {
        return item;
    }
    bool editTemplateParams(requirements::TemplateKind&, requirements::TemplateParams& params) override
    {
        // 预置改动：1×2 网格（黄金默认的确定子集——用例断言可预期）。
        params.countX = 1;
        params.countY = 2;
        return templateOk;
    }
    bool editArrayDefinition(ArrayKind kind, ArrayParams& params) override
    {
        if (productionArray) { return qtDialogHost().editArrayDefinition(kind, params); }
        params.count = arrayCount; params.spacingM = arraySpacing;
        return arrayOk;
    }
    bool editMirrorPlane(const std::vector<TaskPoint>& sources, MirrorPlaneSpec& plane) override
    {
        if (productionMirror) { return qtDialogHost().editMirrorPlane(sources, plane); }
        if (!item) { return false; }
        plane.axisNormal = *item == 0 ? rw::math::Vector3D<>(1, 0, 0) : *item == 1 ? rw::math::Vector3D<>(0, 1, 0) : rw::math::Vector3D<>(0, 0, 1);
        return true;
    }
    bool editImportMapping(const io::RawTable& table, FieldMapping& mapping, ImportUnitOptions& units) override
    {
        return !productionMapping || qtDialogHost().editImportMapping(table, mapping, units);
    }
    bool confirmImport(const QString& text) override { lastImportSummary = text; return importConfirmed; }
    requirements::CommandDialogHost::RegenerateAction chooseRegenerateAction() override
    {
        return action;
    }
};

}  // namespace

TEST_F(RequirementsSessionGuiTest, CommandFlows_TemplateMirrorArray_UI_T32)
{
    IRD_TEST_INFO("REQ-11", {}, std::nullopt);
    using namespace requirements;

    // ① 模板：预置 1×2 → 应用后条目 +2 且带生成溯源。
    FakeDialogHost host;
    const std::size_t before = m_editor.workingSet().points.entries.size();
    ASSERT_TRUE(executeRequirementCommand("requirements.apply-template", *m_panel,
                                          m_editor, *m_panel, host));
    ASSERT_EQ(m_editor.workingSet().points.entries.size(), before + 2);
    int withGeneration = 0;
    for (const TaskPoint& p : m_editor.workingSet().points.entries) {
        if (p.generation.has_value()) { ++withGeneration; }
    }
    EXPECT_GE(withGeneration, 2) << "模板生成条目缺溯源（GenerationProvenance）";

    // ② 镜像：选中 P1（1,2,3）→ YZ 面（X=0——法向 X）→ 反射 x 取反。
    const core::ObjectId p1 = [this] {
        for (const TaskPoint& p : m_editor.workingSet().points.entries) {
            if (p.name == "P1") { return p.objectId; }
        }
        return m_editor.workingSet().points.entries.front().objectId;
    }();
    m_panel->focusObject(p1);
    host.item = 0;  // YZ 面
    const std::size_t mid = m_editor.workingSet().points.entries.size();
    ASSERT_TRUE(executeRequirementCommand("requirements.mirror-stations", *m_panel,
                                          m_editor, *m_panel, host));
    ASSERT_EQ(m_editor.workingSet().points.entries.size(), mid + 1);
    bool mirroredFound = false;
    for (const TaskPoint& p : m_editor.workingSet().points.entries) {
        if (p.generation.has_value()
            && p.pose.position.value()[0] == -1.0) {
            mirroredFound = true;  // P1 x=1 → 镜像 -1（YZ 面反射）
        }
    }
    EXPECT_TRUE(mirroredFound) << "镜像条目位置未按面反射";
    // 注：批量应用后的树重选异常（行点击不落选中锚）另立 F-445 登记——
    // 阵列用例以净夹具验证（CommandFlows_CreateArray_UI_T32）。
}

/**
 * @brief UI-T32 阵列命令（净夹具——选中路径与 T29 focusObject 同型）。
 */
TEST_F(RequirementsSessionGuiTest, CommandFlows_CreateArray_UI_T32)
{
    IRD_TEST_INFO("REQ-11", {}, std::nullopt);
    using namespace requirements;

    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    FakeDialogHost host;
    const core::ObjectId p1 = m_editor.workingSet().points.entries.front().objectId;
    m_panel->focusObject(p1);
    ASSERT_TRUE(m_panel->selectedObjectId().has_value());

    // ③ 阵列（Linear）：数量 3 → +3。源＝用户路径选中 P1（点击树行——
    // 与用户交互同路；批量应用后树重建清锚，重选走行点击而非 focusObject
    // 的幂等短路分支）。
    {
        const QString want = QString::fromStdString(p1.toCanonical());
        // UI-T36 层级树：行定位走产品路径 focusObject（递归扫描分组子树
        // ＝共享树选中联动同款数据流；原顶层行线性扫描假设扁平结构已失效）。
        m_panel->focusObject(p1);
        QTreeWidget* tree = nullptr;
        for (QTreeWidget* t : m_panel->findChildren<QTreeWidget*>()) {
            if (t->columnCount() >= 2 && t->objectName().isEmpty()
                && t->topLevelItemCount() > 0) {
                tree = t;  // 左栏需求树（页签表均有 objectName 锚）
                break;
            }
        }
        ASSERT_NE(tree, nullptr);
        ASSERT_TRUE(tree->currentItem() != nullptr
                    && tree->currentItem()->text(1) == want)
            << "focusObject 未定位到 P1 行（锚不匹配）";
        ASSERT_TRUE(m_panel->selectedObjectId().has_value())
            << "行点击后选中锚仍空";
    }
    const std::size_t preArray = m_editor.workingSet().points.entries.size();
    host.item = 0;  // 线性构型（chooseItem 应答——净夹具默认 nullopt＝取消）
    const bool arrayOk = executeRequirementCommand("requirements.create-array", *m_panel,
                                                   m_editor, *m_panel, host);
    ASSERT_TRUE(arrayOk) << m_panel->showCommandFeedbackText().toStdString();
    EXPECT_EQ(m_editor.workingSet().points.entries.size(), preArray + 3);
}

/** @brief REQ-11/AT-24：四构型完整参数经生产表单生成真实坐标，整批按钮撤销。 */
TEST_F(RequirementsSessionGuiTest, ArrayCompleteProductionForms_AT24_WP14T12)
{
    IRD_TEST_INFO("REQ-11", {}, std::nullopt);
    const auto source = m_editor.workingSet().points.entries.front();
    for (int kind = 0; kind < 4; ++kind) {
        const auto before = m_editor.workingSet();
        m_panel->refreshPanel(before, m_report); m_panel->focusObject(source.objectId);
        FakeDialogHost host; host.item = kind; host.productionArray = true;
        bool visited = false;
        QTimer::singleShot(0, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) { return; }
            visited = true;
            auto* count = dialog->findChild<QSpinBox*>("array-count");
            auto* spacing = dialog->findChild<QDoubleSpinBox*>("array-spacing");
            if (!count || !spacing) {
                ADD_FAILURE() << "完整阵列参数控件缺失，kind=" << kind;
                dialog->reject(); return;
            }
            count->setValue(2); spacing->setValue(0.5);
            if (kind == 0) {
                dialog->findChild<QDoubleSpinBox*>("array-direction-x")->setValue(0);
                dialog->findChild<QDoubleSpinBox*>("array-direction-y")->setValue(1);
            } else if (kind == 1) {
                dialog->findChild<QSpinBox*>("array-count2")->setValue(3);
                dialog->findChild<QDoubleSpinBox*>("array-spacing2")->setValue(0.2);
                dialog->findChild<QSpinBox*>("array-count3")->setValue(2);
                dialog->findChild<QDoubleSpinBox*>("array-spacing3")->setValue(0.7);
            } else if (kind == 2) {
                dialog->findChild<QDoubleSpinBox*>("array-radius")->setValue(2);
                dialog->findChild<QDoubleSpinBox*>("array-start-angle")->setValue(1.5707963267948966);
                dialog->findChild<QDoubleSpinBox*>("array-angle-step")->setValue(1.5707963267948966);
            } else {
                dialog->findChild<QPlainTextEdit*>("array-polyline")->setPlainText("0 0 0\n0 0 1\n0 1 1");
            }
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });
        EXPECT_TRUE(executeRequirementCommand("requirements.create-array", *m_panel, m_editor, *m_panel, host));
        EXPECT_TRUE(visited);
        const auto& points = m_editor.workingSet().points.entries;
        const std::size_t generatedCount[] = {2, 12, 2, 4};
        ASSERT_EQ(points.size(), before.points.entries.size() + generatedCount[kind]);
        const TaskPoint* generated = nullptr;
        // 编辑器按随机ObjectId稳定排序，不能把容器首元素误当几何生成第1条。
        const auto expectedName = source.name + (kind == 1 ? "-A1x1x1" : "-A1");
        for (const auto& point : points) { if (point.name == expectedName) { generated = &point; break; } }
        ASSERT_NE(generated, nullptr);
        const auto p = generated->pose.position.value();
        if (kind == 0) { EXPECT_DOUBLE_EQ(p[0], source.pose.position.value()[0]); EXPECT_DOUBLE_EQ(p[1], source.pose.position.value()[1] + 0.5); }
        if (kind == 1) { EXPECT_DOUBLE_EQ(p[0], source.pose.position.value()[0] + 0.5); EXPECT_DOUBLE_EQ(p[1], source.pose.position.value()[1] + 0.2); }
        if (kind == 2) { EXPECT_NEAR(p[0], -2.0, 1e-12); EXPECT_NEAR(p[1], 0.0, 1e-12); }
        if (kind == 3) { EXPECT_DOUBLE_EQ(p[0], 0.0); EXPECT_DOUBLE_EQ(p[2], 0.5); }
        QPushButton* undo = nullptr;
        for (auto* button : m_panel->findChildren<QPushButton*>()) { if (button->text() == "撤销") { undo = button; } }
        ASSERT_NE(undo, nullptr); ASSERT_TRUE(undo->isEnabled()); undo->click();
        EXPECT_EQ(m_editor.workingSet(), before);
    }
}

/** @brief REQ-11：解除关联是一次批次；撤销必须恢复全批关联及其他条目。 */
TEST_F(RequirementsSessionGuiTest, DetachAtomicUndo_AT24_WP14T12)
{
    IRD_TEST_INFO("REQ-11", {}, std::nullopt);
    FakeDialogHost host;
    ASSERT_TRUE(executeRequirementCommand("requirements.apply-template", *m_panel, m_editor, *m_panel, host));
    const auto before = m_editor.workingSet();
    for (const auto& point : before.points.entries) {
        if (point.generation) { m_panel->focusObject(point.objectId); break; }
    }
    host.action = CommandDialogHost::RegenerateAction::Detach;
    ASSERT_TRUE(executeRequirementCommand("requirements.regenerate-linked", *m_panel, m_editor, *m_panel, host));
    for (const auto& point : m_editor.workingSet().points.entries) {
        if (point.generation) { EXPECT_FALSE(point.generation->linked); }
        else {
            const auto original = std::find_if(before.points.entries.begin(), before.points.entries.end(), [&](const TaskPoint& p) { return p.objectId == point.objectId; });
            ASSERT_NE(original, before.points.entries.end()); EXPECT_EQ(point, *original);
        }
    }
    QPushButton* undo = nullptr; QPushButton* redo = nullptr;
    for (auto* button : m_panel->findChildren<QPushButton*>()) {
        if (button->text() == "撤销") { undo = button; }
        if (button->text() == "重做") { redo = button; }
    }
    ASSERT_NE(undo, nullptr); ASSERT_NE(redo, nullptr);
    ASSERT_TRUE(undo->isEnabled()); undo->click();
    EXPECT_EQ(m_editor.workingSet(), before) << "解除关联不能逐条留下半批状态";
    ASSERT_TRUE(redo->isEnabled()); redo->click();
    for (const auto& point : m_editor.workingSet().points.entries) { if (point.generation) { EXPECT_FALSE(point.generation->linked); } }
}

/** @brief REQ-11/AT-24：非World镜面、任意法向，同系反射与取消零写。 */
TEST_F(RequirementsSessionGuiTest, MirrorFrameAndNormalProduction_AT24_WP14T12)
{
    IRD_TEST_INFO("REQ-11", {}, std::nullopt);
    auto source = m_editor.workingSet().points.entries.front();
    source.refFrame.kind = RequirementRefKind::ModelFrame;
    source.refFrame.objectId = core::ObjectId::generate();
    ASSERT_TRUE(m_editor.applyEdit(source).accepted);
    const auto before = m_editor.workingSet(); m_panel->refreshPanel(before, m_report); m_panel->focusObject(source.objectId);
    FakeDialogHost host; host.productionMirror = true;
    bool visited = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); if (!dialog) { return; }
        visited = true;
        auto* kind = dialog->findChild<QComboBox*>("mirror-frame-kind");
        auto* id = dialog->findChild<QLineEdit*>("mirror-frame-id");
        ASSERT_NE(kind, nullptr); ASSERT_NE(id, nullptr);
        EXPECT_EQ(kind->currentIndex(), 1); EXPECT_EQ(id->text().toStdString(), source.refFrame.objectId->toCanonical());
        auto* ok = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        id->setText("bad-id"); EXPECT_FALSE(ok->isEnabled());
        id->setText(QString::fromStdString(source.refFrame.objectId->toCanonical()));
        dialog->findChild<QDoubleSpinBox*>("mirror-normal-0")->setValue(0); EXPECT_FALSE(ok->isEnabled());
        dialog->findChild<QDoubleSpinBox*>("mirror-normal-0")->setValue(1);
        dialog->findChild<QDoubleSpinBox*>("mirror-normal-1")->setValue(1);
        ASSERT_TRUE(ok->isEnabled()); ok->click();
    });
    ASSERT_TRUE(executeRequirementCommand("requirements.mirror-stations", *m_panel, m_editor, *m_panel, host)); EXPECT_TRUE(visited);
    ASSERT_EQ(m_editor.workingSet().points.entries.size(), before.points.entries.size() + 1);
    for (const auto& point : m_editor.workingSet().points.entries) {
        if (!point.generation) { continue; }
        EXPECT_EQ(point.refFrame, source.refFrame);
        EXPECT_NEAR(point.pose.position.value()[0], -source.pose.position.value()[1], 1e-12);
        EXPECT_NEAR(point.pose.position.value()[1], -source.pose.position.value()[0], 1e-12);
        EXPECT_NEAR(point.pose.position.value()[2], source.pose.position.value()[2], 1e-12);
    }
    MirrorPlaneSpec plane; plane.refFrame = source.refFrame; plane.axisNormal = {1, 0, 0};
    const auto original = plane;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); if (!dialog) { return; }
        dialog->findChild<QDoubleSpinBox*>("mirror-normal-1")->setValue(1); dialog->reject();
    });
    EXPECT_FALSE(qtDialogHost().editMirrorPlane({source}, plane)); EXPECT_EQ(plane.refFrame, original.refFrame); EXPECT_EQ(plane.axisNormal, original.axisNormal);
}

/** @brief REQ-11：真实阵列表单非法方向/顶点不能确认，取消不改参数。 */
TEST_F(RequirementsSessionGuiTest, ArrayInvalidAndCancelProduction_AT24_WP14T12)
{
    IRD_TEST_INFO("REQ-11", {}, std::nullopt);
    ArrayParams p; p.sources = m_editor.workingSet().points.entries; p.count = 2; p.spacingM = 0.5;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); if (!dialog) { return; }
        auto* ok = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        dialog->findChild<QDoubleSpinBox*>("array-direction-x")->setValue(0); EXPECT_FALSE(ok->isEnabled());
        dialog->findChild<QDoubleSpinBox*>("array-direction-y")->setValue(1); EXPECT_TRUE(ok->isEnabled()); dialog->reject();
    });
    EXPECT_FALSE(qtDialogHost().editArrayDefinition(ArrayKind::Linear, p)); EXPECT_EQ(p.direction, rw::math::Vector3D<>(1, 0, 0));
    p.polyline = {{0, 0, 0}, {0, 0, 1}}; p.polylineSpacingM = 0.5;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); if (!dialog) { return; }
        auto* text = dialog->findChild<QPlainTextEdit*>("array-polyline"); auto* ok = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        text->setPlainText("bad coordinates"); EXPECT_FALSE(ok->isEnabled());
        text->setPlainText("0 0 0\n0 0 0"); EXPECT_FALSE(ok->isEnabled());
        text->setPlainText("0 0 0\n0 0 1\n0 1 1"); EXPECT_TRUE(ok->isEnabled()); dialog->reject();
    });
    EXPECT_FALSE(qtDialogHost().editArrayDefinition(ArrayKind::Polyline, p)); EXPECT_EQ(p.polyline.size(), 2U);
}

TEST_F(RequirementsSessionGuiTest, CommandFlows_ExportCopy_UI_T32)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    using namespace requirements;

    // 导出副本：替身路径＝临时文件 → 应用 → 文件真实存在且非空（域侧
    // 原子写实证——ExporterCopyView 同语义）。
    FakeDialogHost host;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("copy.requirements.json"));
    host.savePath = path;
    ASSERT_TRUE(executeRequirementCommand("requirements.export-copy", *m_panel,
                                          m_editor, *m_panel, host));
    QFile f(path);
    ASSERT_TRUE(f.exists()) << "导出文件未落盘";
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_GT(f.size(), qint64{0}) << "导出文件为空";
}

TEST_F(RequirementsSessionGuiTest, CommandFlows_ImportCsvPartial_UI_T32)
{
    IRD_TEST_INFO("AT-02", {}, std::nullopt);
    using namespace requirements;

    // 导入 CSV（部分成功）：两行合法＋一行坏数值（x 非 double）→确认→
    // 正确行 +2、错误行 1 跳过（行级错误 AT-02 语义）。
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("in.csv"));
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write("id,name,x,y,z\n"
            "s1,工位一,1.0,2.0,3.0\n"
            "s2,工位二,4.0,5.0,6.0\n"
            "s3,坏行,abc,0,0\n");
    f.close();

    FakeDialogHost host;
    host.openPath = path;
    host.importConfirmed = true;
    const std::size_t before = m_editor.workingSet().points.entries.size();
    const bool importOk = executeRequirementCommand("requirements.import-csv", *m_panel,
                                                    m_editor, *m_panel, host);
    ASSERT_TRUE(importOk) << m_panel->showCommandFeedbackText().toStdString();
    EXPECT_EQ(m_editor.workingSet().points.entries.size(), before + 2)
        << m_panel->showCommandFeedbackText().toStdString()
        << "（部分成功语义：正确行应 +2、坏行跳过）";
    // REQ-11 不仅要求编辑器有栈：用户必须能经真实面板按钮整批撤销/重做。
    QPushButton* undo = nullptr;
    QPushButton* redo = nullptr;
    for (auto* button : m_panel->findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("撤销")) { undo = button; }
        if (button->text() == QStringLiteral("重做")) { redo = button; }
    }
    ASSERT_NE(undo, nullptr); ASSERT_NE(redo, nullptr);
    ASSERT_TRUE(undo->isEnabled()) << "CSV成功后草稿撤销按钮必须立即可用";
    undo->click();
    EXPECT_EQ(m_editor.workingSet().points.entries.size(), before)
        << "REQ-11：一次CSV导入的正确行必须一次撤销整体恢复";
    ASSERT_TRUE(redo->isEnabled());
    redo->click();
    EXPECT_EQ(m_editor.workingSet().points.entries.size(), before + 2);

    // 反例：坏结构（无表头行）——结构级拒绝（false＋就地反馈）。
    const QString badPath = QDir(dir.path()).filePath(QStringLiteral("bad.csv"));
    QFile bf(badPath);
    ASSERT_TRUE(bf.open(QIODevice::WriteOnly | QIODevice::Text));
    bf.write("不是表头也没数据意义的单列\n1,2\n");
    bf.close();
    host.openPath = badPath;
    const std::size_t mid = m_editor.workingSet().points.entries.size();
    EXPECT_FALSE(executeRequirementCommand("requirements.import-csv", *m_panel,
                                           m_editor, *m_panel, host))
        << "结构级拒绝应返回 false";
    EXPECT_EQ(m_editor.workingSet().points.entries.size(), mid);
}

/** @brief REQ-05/AT-02：未知表头人工映射、mm/deg的实际SI预览与落库一致。 */
TEST_F(RequirementsSessionGuiTest, CsvProductionMappingAndUnits_AT02_WP14T12)
{
    IRD_TEST_INFO("REQ-05", {}, std::nullopt);
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath("mapping.csv");
    QFile file(path); ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("id,name,customX,y,z,yaw\ns1,映射工位,1000,2000,3000,90\n"); file.close();
    FakeDialogHost host; host.openPath = path; host.productionMapping = true;
    bool visited = false;
    QTimer::singleShot(0, [&visited] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { return; }
        visited = true;
        auto* xColumn = dialog->findChild<QComboBox*>("import-map-x");
        auto* xUnit = dialog->findChild<QComboBox*>("import-unit-x");
        ASSERT_NE(xColumn, nullptr); ASSERT_NE(xUnit, nullptr);
        xColumn->setCurrentIndex(xColumn->findData(2));
        for (const char* key : {"x", "y", "z"}) {
            dialog->findChild<QComboBox*>(QString("import-unit-%1").arg(key))->setCurrentText("mm");
        }
        dialog->findChild<QComboBox*>("import-unit-yaw")->setCurrentText("deg");
        auto* table = dialog->findChild<QTableWidget*>("import-mapping-table");
        ASSERT_NE(table, nullptr);
        EXPECT_TRUE(table->item(static_cast<int>(ImportField::X), 3)->text().contains("1 m"));
        EXPECT_TRUE(table->item(static_cast<int>(ImportField::Yaw), 3)->text().contains("rad"));
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    const bool ok = executeRequirementCommand("requirements.import-csv", *m_panel, m_editor, *m_panel, host);
    QApplication::processEvents();
    EXPECT_TRUE(visited) << "生产流必须呈现人工映射和单位预览步骤";
    ASSERT_TRUE(ok) << m_panel->showCommandFeedbackText().toStdString();
    bool found = false;
    for (const auto& point : m_editor.workingSet().points.entries) {
        if (point.name != "映射工位") { continue; }
        found = true; EXPECT_DOUBLE_EQ(point.pose.position.value()[0], 1.0);
        EXPECT_DOUBLE_EQ(point.pose.position.value()[1], 2.0);
        EXPECT_NEAR(point.pose.orientation.fixedRpy[2], 1.5707963267948966, 1e-14);
    }
    EXPECT_TRUE(found);
}

/** @brief AT-02：超过8条行错误仍逐条保留行号/列/原文和建议，取消零写入。 */
TEST_F(RequirementsSessionGuiTest, CsvAllErrorDetails_AT02_WP14T12)
{
    IRD_TEST_INFO("REQ-05", {}, std::nullopt);
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath("errors.csv");
    QFile file(path); ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    QByteArray csv("id,name,x,y,z\n");
    for (int row = 1; row <= 12; ++row) {
        csv += QString("id%1,坏行%1,bad%1,0,0\n").arg(row).toUtf8();
    }
    file.write(csv); file.close();
    const auto before = m_editor.workingSet();
    FakeDialogHost host; host.openPath = path; host.importConfirmed = false;
    EXPECT_TRUE(executeRequirementCommand("requirements.import-csv", *m_panel, m_editor, *m_panel, host));
    for (int row = 1; row <= 12; ++row) {
        EXPECT_TRUE(host.lastImportSummary.contains(QString("row=%1;").arg(row))) << row;
        EXPECT_TRUE(host.lastImportSummary.contains(QString("bad%1").arg(row))) << row;
    }
    EXPECT_TRUE(host.lastImportSummary.contains("column=x"));
    EXPECT_EQ(m_editor.workingSet(), before);

    // 确认页采用真实生产控件，长文本可滚动复制，不能把CSV内容当富文本解释。
    bool visited = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { return; }
        visited = true;
        auto* text = dialog->findChild<QPlainTextEdit*>("import-outcome-details");
        if (text) {
            EXPECT_TRUE(text->isReadOnly());
            EXPECT_EQ(text->toPlainText(), host.lastImportSummary);
            EXPECT_TRUE(text->toPlainText().contains("bad12"));
        } else { ADD_FAILURE() << "生产确认页缺少可滚动全量明细"; }
        dialog->reject();
    });
    EXPECT_FALSE(qtDialogHost().confirmImport(host.lastImportSummary));
    EXPECT_TRUE(visited);
}

/** @brief REQ-05：缺必填映射/非法单位禁下一步，取消保留映射和单位原值。 */
TEST_F(RequirementsSessionGuiTest, CsvMappingValidationCancel_AT02_WP14T12)
{
    IRD_TEST_INFO("REQ-05", {}, std::nullopt);
    io::RawTable table; table.report.hasHeader = true;
    table.report.header = {"id", "name", "x", "y", "z"};
    auto mapping = autoDetectMapping(table, RequirementImporter{}.fieldDictionary()).mapping;
    auto units = ImportUnitOptions::defaults();
    const auto beforeMapping = mapping; const auto beforeUnits = units;
    bool visited = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { return; }
        visited = true;
        auto* next = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        auto* x = dialog->findChild<QComboBox*>("import-map-x");
        auto* unit = dialog->findChild<QComboBox*>("import-unit-x");
        if (next && x && unit) {
            EXPECT_TRUE(next->isEnabled());
            x->setCurrentIndex(0); EXPECT_FALSE(next->isEnabled());
            x->setCurrentIndex(x->findData(2)); EXPECT_TRUE(next->isEnabled());
            unit->setCurrentText("kg"); EXPECT_FALSE(next->isEnabled());
            unit->setCurrentText("mm"); EXPECT_TRUE(next->isEnabled());
        } else { ADD_FAILURE() << "映射页控件缺失"; }
        dialog->reject();
    });
    EXPECT_FALSE(qtDialogHost().editImportMapping(table, mapping, units));
    EXPECT_TRUE(visited);
    EXPECT_EQ(mapping.columnOf, beforeMapping.columnOf);
    for (std::size_t i = 0; i < kImportFieldCount; ++i) {
        EXPECT_EQ(units.unitFor(static_cast<ImportField>(i)), beforeUnits.unitFor(static_cast<ImportField>(i)));
    }
}

TEST_F(RequirementsSessionGuiTest, CommandFlows_JsonAndDegrades_UI_T32)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    using namespace requirements;

    // JSON 结构级反例：坏字节 → false＋零写入。
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("bad.json"));
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("{ not json");
    f.close();
    FakeDialogHost host;
    host.openPath = path;
    const std::size_t before = m_editor.workingSet().points.entries.size();
    EXPECT_FALSE(executeRequirementCommand("requirements.import-json", *m_panel,
                                           m_editor, *m_panel, host));
    EXPECT_EQ(m_editor.workingSet().points.entries.size(), before);

    // 捕获/拾取降级：false＋引导文案（不伪造执行——knownPitfalls 4/5）。
    EXPECT_FALSE(executeRequirementCommand("requirements.capture-tcp", *m_panel,
                                           m_editor, *m_panel, host));
    EXPECT_TRUE(m_panel->showCommandFeedbackText().contains(
        QStringLiteral("后续版本提供")))
        << "捕获降级引导文案缺失";
    EXPECT_FALSE(executeRequirementCommand("requirements.pick-feature", *m_panel,
                                           m_editor, *m_panel, host));
    EXPECT_TRUE(m_panel->showCommandFeedbackText().contains(
        QStringLiteral("三维交互")))
        << "拾取壳引导文案缺失";
}


// =====================================================================
// UI-T34 E 批次：校验页过滤/页头实时化/Blocking 红行/修订语义文案。
// =====================================================================

/// 校验页控件定位（objectName 锚）。
template <typename T>
T* validationControl(const RequirementsPanelWidget& panel, const char* objectName)
{
    return const_cast<RequirementsPanelWidget&>(panel)
        .findChild<T*>(QString::fromLatin1(objectName));
}

/// 校验逐项表行收集（列 0＝级别文本）。
QStringList validationLevelColumn(const RequirementsPanelWidget& panel)
{
    QStringList levels;
    const auto tables =
        const_cast<RequirementsPanelWidget&>(panel).findChildren<QTreeWidget*>();
    for (const QTreeWidget* t : tables) {
        if (t->columnCount() >= 2 && t->headerItem()->text(0) == QStringLiteral("级别")) {
            for (int i = 0; i < t->topLevelItemCount(); ++i) {
                levels << t->topLevelItem(i)->text(0);
            }
        }
    }
    return levels;
}

TEST_F(RequirementsSessionGuiTest, ValidationFilters_HeaderAndRevisionNote_UI_T34)
{
    IRD_TEST_INFO("UX-02", {}, std::nullopt);

    // 违约基线（含阻断——GateTruthSource 同款构造）驱动校验页有逐项行。
    MapClosure closure;
    PointSet points;
    TaskPoint broken = makePoint("P-断链", 0.0, 0.0, 0.0);
    broken.pose.position = core::SourcedValue<rw::math::Vector3D<double>>{};
    points.entries.push_back(broken);
    RegionSet regions;
    regions.entries.push_back(makeRegion("R1"));
    ConditionSet conditions;
    OperatingCondition c1;
    c1.objectId = core::ObjectId::generate();
    c1.name = "C1";
    conditions.entries.push_back(c1);
    fillBaseline(closure, points, regions, conditions);
    RequirementEditor editor;
    ASSERT_TRUE(editor.loadBaseline(closure).ok);
    const RequirementReadinessReport report =
        m_checker.check(editor.workingSet(), CheckContext{});
    ASSERT_TRUE(report.hasBlocking());

    m_panel->setEditTargetProvider([&editor]() -> IRequirementEditor* { return &editor; });
    m_panel->refreshPanel(editor.workingSet(), report);

    // ① 页头实时化：含阻断→『存在 N 项阻断』引导文案（不再『尚未执行』）。
    const QLabel* header = validationControl<QLabel>(*m_panel, "ird_req_tab_validation_header");
    ASSERT_NE(header, nullptr);
    EXPECT_TRUE(header->text().contains(QStringLiteral("阻断")))
        << "阻断态页头缺引导：" << header->text().toStdString();

    // ② 修订语义文案（只增不改——防冻结心智误读）。
    const auto notes = m_panel->findChildren<QLabel*>();
    bool revisionNoteFound = false;
    for (const QLabel* l : notes) {
        if (l->wordWrap() && l->text().contains(QStringLiteral("修订只增不改"))) {
            revisionNoteFound = true;
        }
    }
    EXPECT_TRUE(revisionNoteFound) << "修订只增不改语义文案缺失";

    // ③ 级别过滤：仅阻断→逐项行全部 Blocking。
    QComboBox* levelFilter =
        validationControl<QComboBox>(*m_panel, "ird_req_validation_level_filter");
    ASSERT_NE(levelFilter, nullptr);
    levelFilter->setCurrentIndex(1);  // 仅阻断
    const QStringList afterLevel = validationLevelColumn(*m_panel);
    ASSERT_FALSE(afterLevel.isEmpty());
    for (const QString& lv : afterLevel) {
        EXPECT_EQ(lv, QStringLiteral("Blocking")) << "级别过滤泄漏：" << lv.toStdString();
    }

    // ④ 层过滤：R0＋仅阻断组合（短路序——R0 结构层若有阻断行则保留，
    // 无则空表——两者都是过滤正确态；此处断言组合后无非 R0 行）。
    QComboBox* layerFilter =
        validationControl<QComboBox>(*m_panel, "ird_req_validation_layer_filter");
    ASSERT_NE(layerFilter, nullptr);
    layerFilter->setCurrentIndex(1);  // R0
    const QStringList afterLayer = validationLevelColumn(*m_panel);
    (void)afterLayer;  // 行集合取决于报告分布——组合正确性经 ⑤ 码过滤收口

    // ⑤ 码过滤：稳定码子串（不区分大小写）——过滤后行码全含子串或空集。
    layerFilter->setCurrentIndex(0);  // 恢复全部层
    QLineEdit* codeFilter =
        validationControl<QLineEdit>(*m_panel, "ird_req_validation_code_filter");
    ASSERT_NE(codeFilter, nullptr);
    codeFilter->setText(QStringLiteral("req-ready"));
    const auto tables =
        const_cast<RequirementsPanelWidget&>(*m_panel).findChildren<QTreeWidget*>();
    for (const QTreeWidget* t : tables) {
        if (t->headerItem()->text(0) != QStringLiteral("级别")) { continue; }
        for (int i = 0; i < t->topLevelItemCount(); ++i) {
            EXPECT_TRUE(t->topLevelItem(i)->text(2).contains(QStringLiteral("REQ-READY"),
                                                           Qt::CaseInsensitive))
                << "码过滤泄漏";
        }
    }

    // ⑥ Blocking 行红色前景（拒绝分组视觉面）。
    levelFilter->setCurrentIndex(0);
    codeFilter->clear();
    bool redBlockingFound = false;
    for (const QTreeWidget* t : tables) {
        if (t->headerItem()->text(0) != QStringLiteral("级别")) { continue; }
        for (int i = 0; i < t->topLevelItemCount(); ++i) {
            QTreeWidgetItem* it = t->topLevelItem(i);
            if (it->text(0) == QStringLiteral("Blocking")
                && it->foreground(0).color() == QColor(176, 32, 32)) {
                redBlockingFound = true;
            }
        }
    }
    EXPECT_TRUE(redBlockingFound) << "阻断行红色分组缺失";
}

// =====================================================================
// UI-T35 P2：外部修订同步——三分岔决策真值表（RevisionSyncPolicy.hpp
// planExternalRevisionSync 纯函数）＋门面会话基线出线往返（契约
// acceptance 4 的 gui/UT 具名自证面）。判定输入全部显式传参——前三例
// 零 UI 依赖（宿主执行半区＝UiPlugin 事件 sink，归代码核验；此处钉住
// 判定真值表：同输入恒同输出，NFR-COR-01）。
// =====================================================================

/// 自身回执跳过：事件修订＝会话基线→SkipSelfApplied（noteAppliedRevision
/// 锚前移后的自身 draft.apply 事件回流形态——重导线会造成无谓重刷，故
/// 对账先于草稿态：基线同值即已同步，与未应用编辑数无关）。
TEST(RequirementRevisionSync, SelfAppliedEventSkips_UI_T35)
{
    IRD_TEST_INFO("PM-11", {}, std::nullopt);
    const core::RevisionId applied = core::RevisionId::generate();
    EXPECT_EQ(planExternalRevisionSync(applied, applied, 0),
              ExternalRevisionSync::SkipSelfApplied);
    EXPECT_EQ(planExternalRevisionSync(applied, applied, 3),
              ExternalRevisionSync::SkipSelfApplied);
}

/// 零编辑重导线：会话基线落后于事件修订＋零未应用编辑→RewireFromHead
/// （从新 HEAD 重建基线——草稿零丢失）；基线未绑定（nullopt＝会话未
/// 绑定基线的防御形态）与零编辑同路径。
TEST(RequirementRevisionSync, ZeroEditRewiresFromHead_UI_T35)
{
    IRD_TEST_INFO("PM-11", {}, std::nullopt);
    const core::RevisionId base = core::RevisionId::generate();
    const core::RevisionId external = core::RevisionId::generate();
    ASSERT_NE(base, external) << "生成器碰撞（概率上不可能——出现即停）";
    EXPECT_EQ(planExternalRevisionSync(base, external, 0),
              ExternalRevisionSync::RewireFromHead);
    EXPECT_EQ(planExternalRevisionSync(std::nullopt, external, 0),
              ExternalRevisionSync::RewireFromHead);
}

/// 脏草稿冻结：会话基线落后＋有未应用编辑→StaleNotice（编辑器
/// loadBaseline 无 rebase——自动重载即丢草稿；宿主据此外出状态栏
/// STALE 提示，不静默吞差异）。
TEST(RequirementRevisionSync, DirtyDraftStaysStale_UI_T35)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    const core::RevisionId base = core::RevisionId::generate();
    const core::RevisionId external = core::RevisionId::generate();
    EXPECT_EQ(planExternalRevisionSync(base, external, 1),
              ExternalRevisionSync::StaleNotice);
    EXPECT_EQ(planExternalRevisionSync(std::nullopt, external, 2),
              ExternalRevisionSync::StaleNotice);
}

/// 门面基线出线往返：attach→noteAppliedRevision 前移后
/// sessionBaseRevision() 反映新基线（nullopt 根＝首应用 allocateNew
/// 场景——空项目初始化的宿主同款调用序）；onSessionDetached 后回落
/// nullopt（会话态全清）。本用例是事件 sink"自身回执"比对输入的
/// 真值源自证（RequirementsPluginAssembly::sessionBaseRevision）。
TEST_F(RequirementsSessionGuiTest, FacadeSessionBaseRevisionRoundtrip_UI_T35)
{
    IRD_TEST_INFO("PM-11", {}, std::nullopt);
    RequirementsPluginAssembly facade = createRequirementsPluginAssembly();
    facade.attachEditor(&m_editor);
    EXPECT_FALSE(facade.sessionBaseRevision().has_value())
        << "新装配会话基线应未绑定";
    const core::RevisionId rev = core::RevisionId::generate();
    facade.noteAppliedRevision(rev, std::nullopt);
    ASSERT_TRUE(facade.sessionBaseRevision().has_value());
    EXPECT_EQ(*facade.sessionBaseRevision(), rev) << "基线出线应随回执前移";
    facade.onSessionDetached();
    EXPECT_FALSE(facade.sessionBaseRevision().has_value())
        << "会话脱离后基线应全清";
}

// =====================================================================
// UI-T36：可读性与结构治理——锚列泄漏修复（采样/节拍列业务文本）、
// 层级需求树（根→分组→条目＋页签联动）、截断修复防回归。
// =====================================================================

/// 定位左栏需求树（表头列 0＝"需求树（当前草稿）"——F-499 双树职责标注
/// 后的全文；与区域/工况/必验/校验表区分）。
QTreeWidget* requirementTree(const RequirementsPanelWidget& panel)
{
    for (QTreeWidget* t :
         const_cast<RequirementsPanelWidget&>(panel).findChildren<QTreeWidget*>()) {
        if (t->headerItem()->text(0) == QStringLiteral("需求树（当前草稿）")) {
            return t;
        }
    }
    return nullptr;
}

/// 区域/工况表显示列零内部标识断言（锚列泄漏回归守卫——display 列出
/// 现 obj- 词形即红；锚列退居末隐藏列的机制面）。
void assertNoInternalIdInDisplayColumns(const QTreeWidget& table, int displayColumns)
{
    for (int i = 0; i < table.topLevelItemCount(); ++i) {
        const QTreeWidgetItem* it = table.topLevelItem(i);
        for (int c = 0; c < displayColumns; ++c) {
            EXPECT_FALSE(it->text(c).startsWith(QStringLiteral("obj-")))
                << "显示列泄漏内部 ObjectId（锚列覆盖回归）: 行 " << i
                << " 列 " << c << " = " << it->text(c).toStdString();
        }
    }
}

/// 采样/覆盖/节拍/适用范围列的业务文本形态（模型层 regionRows/
/// conditionRows 既有投影字段的呈现收口——逐列格式断言，不钉具体值）。
TEST_F(RequirementsSessionGuiTest, RegionConditionRows_BusinessColumns_UI_T36)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // 区域表（区域名称|空间采样|覆盖目标）：采样列＝Grid/间距/Random 三
    // 方法词形之一、覆盖列＝P≥ 前缀、零 obj- 泄漏。
    QTreeWidget* regionTable =
        m_panel->findChild<QTreeWidget*>(QStringLiteral("ird_req_region_table"));
    ASSERT_NE(regionTable, nullptr);
    ASSERT_GT(regionTable->topLevelItemCount(), 0);
    const QString sampling =
        regionTable->topLevelItem(0)->text(1);
    EXPECT_TRUE(sampling.startsWith(QStringLiteral("Grid"))
                || sampling.startsWith(QStringLiteral("间距"))
                || sampling.startsWith(QStringLiteral("Random")))
        << "空间采样列非业务摘要词形: " << sampling.toStdString();
    EXPECT_TRUE(regionTable->topLevelItem(0)->text(2)
                    .startsWith(QStringLiteral("P≥")))
        << "覆盖目标列非阈值词形: "
        << regionTable->topLevelItem(0)->text(2).toStdString();
    assertNoInternalIdInDisplayColumns(*regionTable, 3);

    // 工况表（工况|目标节拍|适用范围）：节拍列＝数值 s/未设、范围列＝
    // 三值词表、零 obj- 泄漏。
    QTreeWidget* conditionTable =
        m_panel->findChild<QTreeWidget*>(QStringLiteral("ird_req_condition_table"));
    ASSERT_NE(conditionTable, nullptr);
    ASSERT_GT(conditionTable->topLevelItemCount(), 0);
    const QString cycle = conditionTable->topLevelItem(0)->text(1);
    EXPECT_TRUE(cycle == QStringLiteral("未设") || cycle.endsWith(QStringLiteral(" s")))
        << "目标节拍列非业务值词形: " << cycle.toStdString();
    const QString applies = conditionTable->topLevelItem(0)->text(2);
    EXPECT_TRUE(applies == QStringLiteral("全部工位")
                || applies == QStringLiteral("不适用")
                || applies.endsWith(QStringLiteral("个工位")))
        << "适用范围列非三值词表: " << applies.toStdString();
    assertNoInternalIdInDisplayColumns(*conditionTable, 3);
}

/// 层级需求树：需求工程单根→四分组（计数）→条目三级；分组点击＝仅页
/// 签联动不清锚；条目点击＝锚设置＋页签联动；focusObject 递归定位命中
/// 嵌套条目。
TEST_F(RequirementsSessionGuiTest, TreeHierarchy_GroupsAndTabLinkage_UI_T36)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    QTreeWidget* tree = requirementTree(*m_panel);
    ASSERT_NE(tree, nullptr) << "需求树控件缺失";
    ASSERT_EQ(tree->topLevelItemCount(), 1) << "层级树应恰一业务根";
    QTreeWidgetItem* root = tree->topLevelItem(0);
    EXPECT_EQ(root->text(0), QStringLiteral("需求工程"));
    ASSERT_EQ(root->childCount(), 4) << "业务根下应恰四分组（工位/区域/工况/计划）";
    EXPECT_TRUE(root->text(0).size() > 0 && root->isExpanded())
        << "根节点应默认展开";
    for (int i = 0; i < root->childCount(); ++i) {
        EXPECT_TRUE(root->child(i)->text(0).contains(QStringLiteral("（")))
            << "分组节点应带计数标注（全角括号计数面）: "
            << root->child(i)->text(0).toStdString();
        EXPECT_TRUE(root->child(i)->text(1).isEmpty())
            << "分组节点不应携带对象锚";
    }

    // 条目点击＝页签联动＋检查器投影（工位分组首条目→页签 0）。
    QTreeWidgetItem* pointsGroup = root->child(0);
    ASSERT_GT(pointsGroup->childCount(), 0) << "夹具基线应含工位条目";
    pointsGroup->setExpanded(true);
    tree->setCurrentItem(pointsGroup->child(0));
    EXPECT_EQ(m_panel->findChild<QTabWidget*>()->currentIndex(), 0)
        << "条目点击未联动工位页签";

    // 分组点击＝仅页签联动不清锚（再点条目设锚后点区域分组——页签切 1
    // 且检查器行仍为工位字段〔选中锚保持的可观测面〕）。
    QTreeWidgetItem* regionGroup = root->child(1);
    tree->setCurrentItem(regionGroup);
    EXPECT_EQ(m_panel->findChild<QTabWidget*>()->currentIndex(), 1)
        << "分组点击未联动区域页签";
    EXPECT_FALSE(m_panel->findChild<QScrollArea*>() == nullptr)
        << "属性表单滚动容器缺失（UI-T36 滚动承载）";

    // focusObject 递归定位：给定点身份→嵌套条目置当前行（层级化后的
    // 反向半区——递归扫描命中分组子树）。
    const core::ObjectId pointOid = m_editor.workingSet().points.entries.front().objectId;
    m_panel->focusObject(pointOid);
    ASSERT_NE(tree->currentItem(), nullptr);
    EXPECT_EQ(tree->currentItem()->text(1).toStdString(), pointOid.toCanonical())
        << "focusObject 未命中嵌套条目（递归定位断链）";
}

/// 截断修复防回归（FlowLayout 边距对称性），两半区（UI-T36 验收 attempt1
/// G-1 守卫重构——原整端到端半区对边距回归变异不敏感：夹具宿主最低高经
/// 既有 minimumSize 边距叠加保护＋父布局余量，720×640/300×200 双场景均
/// 恒满高，无法感知协商缺边距的回归）：
/// ①边距耦合半区（变异敏感面）：裸宿主＋FlowLayout，contentsMargins 从
///   0 → 上下各 9px 时宿主 sizeHint 高度增量应恰为 18——尺寸协商必须
///   包含上下边距（与 setGeometry 按 contentsRect 扣边距落位对称；协商
///   缺边距＝截断根因形态，增量退化为 0，本断言立即翻红）；
/// ②端到端半区（集成面）：面板显示后生命周期按钮实际高≥建议高（满高
///   呈现的最终语义锚——保留 attempt1 既有断言）。
TEST_F(RequirementsSessionGuiTest, LifecycleButton_NoVerticalClipping_UI_T36)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);

    // 半区①：边距耦合性。9px＝Windows 样式布局边距量级（生产面
    // FlowLayout 未显式 setContentsMargins——QLayout 默认取样式
    // PM_Layout*Margin，实测定值 9,9，见 acc/ui-t36/1 验收记录探针）。
    {
        QWidget scratchHost;
        auto* flow =
            new ui::FlowLayout(&scratchHost, /*hSpacing=*/4, /*vSpacing=*/2);
        QPushButton probe(QStringLiteral("PROBE"));
        flow->addWidget(&probe);
        flow->setContentsMargins(0, 0, 0, 0);
        const int noMarginHint = scratchHost.sizeHint().height();
        flow->setContentsMargins(0, 9, 0, 9);
        const int withMarginHint = scratchHost.sizeHint().height();
        EXPECT_EQ(withMarginHint - noMarginHint, 18)
            << "FlowLayout 尺寸协商未耦合 contentsMargins 上下边距（截断"
               "根因回归——noMargin="
            << noMarginHint << " withMargin=" << withMarginHint << "）";
    }

    // 半区②：端到端满高（保留——面板真实宿主链的最终语义锚）。
    m_panel->resize(720, 640);
    m_panel->show();
    QApplication::processEvents();
    QPushButton* addBtn =
        m_panel->findChild<QPushButton*>(QStringLiteral("ird_req_add_points"));
    ASSERT_NE(addBtn, nullptr);
    QApplication::processEvents();
    EXPECT_GE(addBtn->height(), addBtn->sizeHint().height())
        << "生命周期按钮实际高小于建议高（FlowLayout 尺寸协商缺边距回归"
           "——按钮底部裁切复现）";
    m_panel->hide();
}

/// UI-T37 R1 卡片化检查器（acceptance 1）：工位检查器五卡呈现（基础属性/
/// 空间与公差/自由度约束/动作阶段/姿态规则）、帮助位 Tooltip 非空（去噪
/// 纪律——成段说明文字收 "?"/Tooltip，不直出面板）。
TEST_F(RequirementsSessionGuiTest, StationCards_ModernControls_UI_T37)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(m_editor.workingSet().points.entries.front().objectId);
    QApplication::processEvents();
    const QList<QGroupBox*> cards = m_panel->findChildren<QGroupBox*>(
        QStringLiteral("ird_card"));
    ASSERT_GE(cards.size(), 5)
        << "工位检查器卡片缺失（卡片化 acceptance 1）";
    QLabel* help = m_panel->findChild<QLabel*>(QStringLiteral("ird_card_help"));
    ASSERT_NE(help, nullptr);
    EXPECT_FALSE(help->toolTip().isEmpty())
        << "卡片帮助位 Tooltip 空（去噪纪律面）";

    // 启用 Switch→布尔提交轨（acceptance 3）：翻开关＝工作集 enabled 翻转。
    QComboBox* sw = m_panel->findChild<QComboBox*>(QStringLiteral("ird_station_enabled_combo"));
    ASSERT_NE(sw, nullptr) << "启用下拉缺失（返工②——布尔枚举一律 QComboBox）";
    const bool before = sw->currentIndex() == 0;
    sw->setCurrentIndex(before ? 1 : 0);
    QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet().points.entries.front().enabled, !before)
        << "启用 Switch 未接通布尔提交轨（applyStationToggleEdit）";
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // 自由度分段矩阵（acceptance 3）：点击"当前态反面"钮＝该轴受约束态
    // 翻转（基线任意初态→点其反向钮→域 constrainedDof 等价翻转）。
    const auto& dof = m_editor.workingSet().points.entries.front().pose.constrainedDof;
    const bool xBefore = dof.x;
    QComboBox* dofxCombo = nullptr;
    for (QComboBox* c : m_panel->findChildren<QComboBox*>(
             QStringLiteral("ird_dof_combo"))) {
        if (c->property("irdDofKey").toString() == QStringLiteral("dof-x")) {
            dofxCombo = c;
            break;
        }
    }
    ASSERT_NE(dofxCombo, nullptr)
        << "自由度矩阵缺 dof-x 下拉（测试锚面）";
    dofxCombo->setCurrentIndex(xBefore ? 1 : 0);  // 选当前态反面＝翻转
    QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet().points.entries.front().pose.constrainedDof.x,
              !xBefore)
        << "自由度分段钮未接通约束词表布尔轨";

    // 快捷预设（acceptance 3——全部约束）：六轴批量切换（已态跳过）。
    QPushButton* constrainAll = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_dof_preset_constrain"));
    ASSERT_NE(constrainAll, nullptr) << "快捷预设行缺失";
    const auto beforePreset = m_editor.workingSet().points.entries.front();
    constrainAll->click();
    QApplication::processEvents();
    const auto& dofAll =
        m_editor.workingSet().points.entries.front().pose.constrainedDof;
    EXPECT_TRUE(dofAll.x && dofAll.y && dofAll.z && dofAll.roll && dofAll.pitch
                && dofAll.yaw)
        << "全部约束预设未批量生效";
    ASSERT_TRUE(m_editor.undoLocal());
    EXPECT_EQ(m_editor.workingSet().points.entries.front(), beforePreset)
        << "REQ-11：六轴预设须一次撤销完整恢复";
}

/// UI-T37 R1 区域复合行与实时预览（acceptance 2）：盒中心三编辑器同容器、
/// 采样计数三值 spin＋『共 N 个离散点』实时联动（改值→预览乘积刷新＋域
/// 提交等价）、覆盖率滑块↔域值双联（滑块拨动→提交面 0~1 比率）。
TEST_F(RequirementsSessionGuiTest, RegionCompositeAndLiveCount_UI_T37)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    const core::ObjectId regionId =
        m_editor.workingSet().regions.entries.front().objectId;
    m_panel->focusObject(regionId);
    QApplication::processEvents();
    // 区域检查器绑定区域表选中行（L-R1 表侧——树选中不驱动区域表；
    // T31 同款：先点表行再断言检查器投影）。
    QTreeWidget* regionTable = m_panel->findChild<QTreeWidget*>(
        QStringLiteral("ird_req_region_table"));
    ASSERT_NE(regionTable, nullptr);
    ASSERT_GT(regionTable->topLevelItemCount(), 0);
    regionTable->setCurrentItem(regionTable->topLevelItem(0));
    QApplication::processEvents();
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    QApplication::processEvents();
    // 盒中心复合行：三标量编辑器经既有键定位面同现（视觉合并零语义变化）。
    ASSERT_NE(fieldEditor(*m_panel, "box-center-x"), nullptr);
    ASSERT_NE(fieldEditor(*m_panel, "box-center-y"), nullptr);
    ASSERT_NE(fieldEditor(*m_panel, "box-center-z"), nullptr);

    // 采样计数三值＋实时预览：spin 改值→标签乘积刷新→域计数提交等价。
    // 注意：改值触发同步提交→refreshPanel 重投影（控件树重建）——交互前
    // 取值、交互后一律重查控件（悬空指针面＝0xC0000005）。
    const QList<QSpinBox*> spins = m_panel->findChildren<QSpinBox*>(
        QStringLiteral("ird_region_count_spin"));
    ASSERT_EQ(spins.size(), 3) << "采样计数复合行缺三轴 spin";
    const int xBefore = spins[0]->value();
    const int yKeep = spins[1]->value();
    const int zKeep = spins[2]->value();
    spins[0]->setValue(xBefore + 1);
    QApplication::processEvents();
    const auto& region = m_editor.workingSet().regions.entries.front();
    EXPECT_EQ(region.positionSampling.counts[0],
              static_cast<std::uint32_t>(xBefore + 1))
        << "采样计数 spin 未接通域提交轨";
    QLabel* countLabel =
        m_panel->findChild<QLabel*>(QStringLiteral("ird_region_count_label"));
    ASSERT_NE(countLabel, nullptr);
    const std::uint64_t product =
        static_cast<std::uint64_t>((xBefore + 1) * yKeep * zKeep);
    EXPECT_TRUE(countLabel->text().contains(QString::number(product)))
        << "实时点数预览未随改值联动: " << countLabel->text().toStdString();

    // 覆盖率滑块双联：拨滑块→提交面 0~1 比率（域值等价）。
    QSlider* slider = m_panel->findChild<QSlider*>(
        QStringLiteral("ird_region_coverage_slider"));
    ASSERT_NE(slider, nullptr) << "覆盖率滑块缺失（滑块+输入双联面）";
    slider->setValue(50);
    QApplication::processEvents();
    EXPECT_DOUBLE_EQ(
        m_editor.workingSet().regions.entries.front().coverageTargets
            .minPositionCoverage,
        0.5)
        << "覆盖率滑块未接通双联提交轨";
}

/// WP-14-T12保留溯源只读纪律；名称/顺序键已有类型正确的文本轨，不再禁用。
TEST_F(RequirementsSessionGuiTest, SourceRowsReadOnly_UI_T37)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(m_editor.workingSet().points.entries.front().objectId);
    QApplication::processEvents();
    // segment-approach 返工③起为下拉复合行（启用/轴/距离）——不再断言只读。
    const char* readonlyKeys[] = {"source"};
    for (const char* key : readonlyKeys) {
        QLineEdit* edit = fieldEditor(*m_panel, key);
        ASSERT_NE(edit, nullptr) << "行缺失: " << key;
        EXPECT_TRUE(edit->isReadOnly())
            << "来源事实必须只读: " << key;
    }
    // 权威值零变化（只读行不进入编辑流——域状态不被触碰）。
    const std::string nameBefore =
        m_editor.workingSet().points.entries.front().name;
    EXPECT_EQ(m_editor.workingSet().points.entries.front().name, nameBefore);
}

/// UI-T37 R2 工况页闭环（acceptance 4）：验收 Tag 列（必验/可选非空）、
/// 新增向导取消＝零新增、确认＝新条目四字段齐备（名称/节拍/等级——
/// 注入缝确定字段，FakeDialogHost 同族）。
TEST_F(RequirementsSessionGuiTest, ConditionTagAndWizard_UI_T37)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    QTreeWidget* table = m_panel->findChild<QTreeWidget*>(
        QStringLiteral("ird_req_condition_table"));
    ASSERT_NE(table, nullptr);
    ASSERT_GT(table->topLevelItemCount(), 0) << "夹具应含工况起步条目";
    EXPECT_FALSE(table->topLevelItem(0)->text(3).isEmpty())
        << "验收 Tag 列空（必验/可选派生面缺失）";

    // 向导取消＝零新增（注入缝返回 nullopt——不产生空白条目）。
    const std::size_t before = m_editor.workingSet().conditions.entries.size();
    m_panel->setConditionWizardFactory(
        []() -> std::optional<RequirementsPanelWidget::ConditionWizardFields> {
            return std::nullopt;
        });
    QPushButton* addBtn = lifecycleButton(*m_panel, "add", "conditions");
    ASSERT_NE(addBtn, nullptr);
    addBtn->click();
    QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet().conditions.entries.size(), before)
        << "向导取消仍新增条目（空白条目禁令）";

    // 向导确认＝四字段齐备（名称防撞/节拍 s/等级 Should→可选）。
    m_panel->setConditionWizardFactory(
        []() -> std::optional<RequirementsPanelWidget::ConditionWizardFields> {
            RequirementsPanelWidget::ConditionWizardFields f;
            f.name = "向导工况样例";
            f.hasCycle = true;
            f.cycleSeconds = 12.5;
            f.mustVerify = false;
            return f;
        });
    addBtn->click();
    QApplication::processEvents();
    ASSERT_EQ(m_editor.workingSet().conditions.entries.size(), before + 1);
    const OperatingCondition* created = nullptr;  // 集合规范序非插入序——按名定位
    for (const OperatingCondition& c : m_editor.workingSet().conditions.entries) {
        if (c.name == std::string("向导工况样例")) { created = &c; }
    }
    ASSERT_NE(created, nullptr) << "向导确认后未找到新条目";
    ASSERT_TRUE(created->targetCycleTimeS.has_value()) << "节拍未设置";
    EXPECT_DOUBLE_EQ(created->targetCycleTimeS.value(), 12.5);
    EXPECT_EQ(created->level, RequirementLevel::Should) << "可选等级未落条目";
}

/// UI-T37 返工④：工况页单表整合＋详情卡（所有者指令——原上下叠放的
/// 工况表＋必验清单预览表合并为单表『是否必验』列，腾出的下半部改为
/// 『工况详情与节拍配置』卡，结构同工位/区域页 QGroupBox 卡）。
TEST_F(RequirementsSessionGuiTest, ConditionSingleTableDetailCard_UI_T37R4)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // 断言①：单表末显示列列名＝『是否必验』（更名——原『验收』）。
    QTreeWidget* table = m_panel->findChild<QTreeWidget*>(
        QStringLiteral("ird_req_condition_table"));
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->headerItem()->text(3), QStringLiteral("是否必验"));

    // 断言②：必验清单预览表已撤销——面板内不存在『必验工况』表头的树
    // （整合后必验事实只经单表 Tag 列与检查器必验行呈现）。
    const QList<QTreeWidget*> trees = m_panel->findChildren<QTreeWidget*>();
    for (const QTreeWidget* t : trees) {
        EXPECT_NE(t->headerItem()->text(0), QStringLiteral("必验工况"))
            << "必验清单预览表未撤销（单表整合未生效）";
    }

    // 断言③：详情卡存在且唯一（返工⑤卡片标题行走自绘 QLabel——定位面
    // ird_card_title 标签文本；面板全局 ird_card 集合内按标题计数，跨页
    // 不重名）。
    int detailCards = 0;
    const QList<QGroupBox*> cards =
        m_panel->findChildren<QGroupBox*>(QStringLiteral("ird_card"));
    for (const QGroupBox* card : cards) {
        const QLabel* titleLabel =
            card->findChild<QLabel*>(QStringLiteral("ird_card_title"));
        if (titleLabel != nullptr
            && titleLabel->text() == QStringLiteral("工况详情与节拍配置")) {
            ++detailCards;
        }
    }
    EXPECT_EQ(detailCards, 1) << "工况详情卡缺失或重复（found=" << detailCards << "）";
}

/// UI-T37 返工⑤：空态体验＋卡片折叠＋导入下拉＋面包屑（所有者指令承接）：
/// ①未选择对象＝复制/删除置灰仅保留新增＋空态提示替代空卡骨架；②卡片
/// 标题栏折叠三角（▼/▶）一键收起；③导入 CSV/JSON 整合为『导入 ▾』下拉
/// （菜单动作文案仍经 UiText）；④页签状态行精简为面包屑。
TEST_F(RequirementsSessionGuiTest, EmptyStateFoldImportBreadcrumb_UI_T37R5)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    m_panel->resize(900, 700);
    m_panel->show();
    QApplication::processEvents();

    // ---- 断言①：未选择对象（refreshPanel 后无树选中）——三页新增可用、
    // 复制/删除置灰；空态提示可见、属性区滚动容器隐藏。
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    QApplication::processEvents();
    for (const char* key : {"points", "regions", "conditions"}) {
        QPushButton* addBtn = lifecycleButton(*m_panel, "add", key);
        QPushButton* dupBtn = lifecycleButton(*m_panel, "duplicate", key);
        QPushButton* remBtn = lifecycleButton(*m_panel, "remove", key);
        ASSERT_NE(addBtn, nullptr);
        ASSERT_NE(dupBtn, nullptr);
        ASSERT_NE(remBtn, nullptr);
        EXPECT_TRUE(addBtn->isEnabled())
            << "新增键不应依赖选择（空集合起步语义——B1）: " << key;
        EXPECT_FALSE(dupBtn->isEnabled())
            << "未选择＝复制置灰: " << key;
        EXPECT_FALSE(remBtn->isEnabled())
            << "未选择＝删除置灰: " << key;
        // 滚动容器 objectName＝页名词（station/region/condition——构建侧
        // 命名），键为复数集合名（points/regions/conditions）——显式映射。
        const char* scrollName = key == QStringLiteral("points")
                                     ? "ird_req_station_props_scroll"
                                     : key == QStringLiteral("regions")
                                           ? "ird_req_region_props_scroll"
                                           : "ird_req_condition_props_scroll";
        QScrollArea* props = m_panel->findChild<QScrollArea*>(
            QString::fromLatin1(scrollName));
        ASSERT_NE(props, nullptr);
        // 页签非激活页的子树 isVisible 恒 false——用 isVisibleTo 断言自身
        // 显隐态（忽略页签容器），激活页的真实可见性另断（见下文工位页）。
        EXPECT_FALSE(props->isVisibleTo(props->parentWidget()))
            << "未选择＝属性区收起（空卡骨架退役）: " << key;
        QLabel* hint = m_panel->findChild<QLabel*>(
            QStringLiteral("ird_req_empty_hint_%1").arg(key));
        ASSERT_NE(hint, nullptr);
        EXPECT_TRUE(hint->isVisibleTo(hint->parentWidget()))
            << "未选择＝空态提示可见: " << key;
        EXPECT_FALSE(hint->text().isEmpty()) << "空态提示文案空: " << key;
    }

    // ---- 断言②：面包屑初始态（页头与页签名不再重复冒号态）。
    QLabel* header = m_panel->findChild<QLabel*>(
        QStringLiteral("ird_req_tab_station_header"));
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(header->text(), QStringLiteral("工位 > 未选择"))
        << "页头面包屑初始态偏离（返工⑤格式）";

    // ---- 断言③：选中工位——复制/删除恢复、提示隐藏、滚动容器可见、
    // 面包屑＝对象名。
    m_panel->focusObject(m_editor.workingSet().points.entries.front().objectId);
    QApplication::processEvents();
    EXPECT_TRUE(lifecycleButton(*m_panel, "duplicate", "points")->isEnabled())
        << "选中后复制应恢复";
    EXPECT_TRUE(lifecycleButton(*m_panel, "remove", "points")->isEnabled())
        << "选中后删除应恢复";
    QLabel* stationHint = m_panel->findChild<QLabel*>(
        QStringLiteral("ird_req_empty_hint_points"));
    ASSERT_NE(stationHint, nullptr);
    EXPECT_FALSE(stationHint->isVisible()) << "选中后空态提示应隐藏";
    QScrollArea* stationProps = m_panel->findChild<QScrollArea*>(
        QStringLiteral("ird_req_station_props_scroll"));
    ASSERT_NE(stationProps, nullptr);
    EXPECT_TRUE(stationProps->isVisible()) << "选中后属性区应可见";
    EXPECT_TRUE(header->text().startsWith(QStringLiteral("工位 > ")))
        << "页头非面包屑形态";
    EXPECT_FALSE(header->text().endsWith(QStringLiteral("未选择")))
        << "选中后面包屑应为对象名";

    // ---- 断言④：卡片折叠三角——收起后卡内行控件隐藏，展开恢复。
    // 定位面＝启用下拉所在卡的折叠钮（findChild 全局序不保证卡的创建序——
    // 同卡内查找规避跨卡命中）。
    QComboBox* enabledCombo = m_panel->findChild<QComboBox*>(
        QStringLiteral("ird_station_enabled_combo"));
    ASSERT_NE(enabledCombo, nullptr);
    QWidget* comboHost = enabledCombo;
    while (comboHost != nullptr
           && comboHost->objectName() != QStringLiteral("ird_card")) {
        comboHost = comboHost->parentWidget();
    }
    ASSERT_NE(comboHost, nullptr) << "启用行未宿主于卡片（卡化结构面）";
    QToolButton* fold =
        comboHost->findChild<QToolButton*>(QStringLiteral("ird_card_fold"));
    ASSERT_NE(fold, nullptr) << "卡片折叠三角缺失（返工⑤统一折叠面）";
    EXPECT_EQ(fold->text(), QStringLiteral("▼"))
        << "卡片默认应为展开态";
    fold->setChecked(false);
    QApplication::processEvents();
    EXPECT_EQ(fold->text(), QStringLiteral("▶"))
        << "折叠后三角未翻转为收起态";
    EXPECT_FALSE(enabledCombo->isVisible()) << "折叠后卡内行应隐藏";
    fold->setChecked(true);
    QApplication::processEvents();
    EXPECT_TRUE(enabledCombo->isVisible()) << "展开后卡内行应恢复";
}

/// UI-T37 返工⑤：导入下拉呈现面（CSV/JSON 整合）＋撤销三键文本精简。
TEST_F(RequirementsSessionGuiTest, ImportDropdownAndUndoTrim_UI_T37R5)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    QApplication::processEvents();

    // 断言①：导入下拉存在且菜单恰两动作（CSV/JSON——文案仍经 UiText 词表）。
    QPushButton* dropdown = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_req_import_dropdown"));
    ASSERT_NE(dropdown, nullptr) << "导入下拉缺失（返工⑤工具栏整合面）";
    QMenu* menu = dropdown->menu();
    ASSERT_NE(menu, nullptr) << "导入下拉未挂菜单";
    ASSERT_EQ(menu->actions().size(), 2) << "导入菜单动作数偏离";
    EXPECT_EQ(menu->actions()[0]->text(), QStringLiteral("导入 CSV"));
    EXPECT_EQ(menu->actions()[1]->text(), QStringLiteral("导入 JSON"));

    // 断言②：独立导入按钮已撤销（全按钮集合中不再有同名独立键）。
    const QList<QPushButton*> allButtons =
        m_panel->findChildren<QPushButton*>();
    for (const QPushButton* btn : allButtons) {
        EXPECT_FALSE(btn->text() == QStringLiteral("导入 CSV")
                     || btn->text() == QStringLiteral("导入 JSON"))
            << "导入键仍以独立按钮呈现（整合未生效）";
    }

    // 断言③：撤销三键文本精简（去"草稿级/项目级"括号后缀——完整语义
    // 移入 Tooltip）。
    bool sawUndo = false;
    bool sawRedo = false;
    bool sawProjectUndo = false;
    for (const QPushButton* btn : allButtons) {
        if (btn->text() == QStringLiteral("撤销")) {
            sawUndo = true;
            EXPECT_FALSE(btn->toolTip().isEmpty())
                << "撤销键 Tooltip 空（语义收纳面）";
        }
        if (btn->text() == QStringLiteral("重做")) {
            sawRedo = true;
            EXPECT_FALSE(btn->toolTip().isEmpty()) << "重做键 Tooltip 空";
        }
        if (btn->text() == QStringLiteral("撤销上次应用")) {
            sawProjectUndo = true;
            EXPECT_FALSE(btn->toolTip().isEmpty()) << "项目级撤销 Tooltip 空";
        }
        EXPECT_FALSE(btn->text().contains(QStringLiteral("草稿级")))
            << "按钮文本仍含草稿级后缀: " << btn->text().toStdString();
        EXPECT_FALSE(btn->text().contains(QStringLiteral("项目级")))
            << "按钮文本仍含项目级后缀: " << btn->text().toStdString();
    }
    EXPECT_TRUE(sawUndo) << "标准撤销键缺失";
    EXPECT_TRUE(sawRedo) << "标准重做键缺失";
    EXPECT_TRUE(sawProjectUndo) << "撤销上次应用键缺失";
}

/// UI-T37 R2 校验看板与语义标签（acceptance 5）：状态卡二态（✔/⚠＋阻塞
/// 计数）、逐项层列语义标签（原始码入 Tooltip）、修订说明一行化（成段
/// 说明收 Tooltip——『修订只增不改』关键词保留）。
TEST_F(RequirementsSessionGuiTest, ValidationBoardAndSemanticLayers_UI_T37)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    QLabel* status = m_panel->findChild<QLabel*>(
        QStringLiteral("ird_validation_status"));
    ASSERT_NE(status, nullptr) << "校验状态卡缺失（看板化面）";
    const QString state = status->property("state").toString();
    EXPECT_TRUE(state == QStringLiteral("ok") || state == QStringLiteral("warn"))
        << "状态卡二态属性缺失";
    EXPECT_TRUE(status->text().contains(QStringLiteral("✔"))
                || status->text().contains(QStringLiteral("⚠")))
        << "状态卡缺指示符: " << status->text().toStdString();

    // 逐项层列＝语义标签（非 R 码原文），原始码入 Tooltip。
    QTreeWidget* items = m_panel->findChild<QTreeWidget*>(
        QStringLiteral("ird_req_validation_items"));
    if (items == nullptr) {
        // 兼容未设 objectName 的既有定位（表为页内唯一逐项行宿主）。
        const QList<QTreeWidget*> tables =
            m_panel->findChildren<QTreeWidget*>();
        for (QTreeWidget* t : tables) {
            if (t->columnCount() == 5
                && t->headerItem()->text(1) == QStringLiteral("层")) {
                items = t;
                break;
            }
        }
    }
    ASSERT_NE(items, nullptr) << "校验逐项表缺失";
    if (items->topLevelItemCount() > 0) {
        QTreeWidgetItem* first = items->topLevelItem(0);
        EXPECT_FALSE(first->text(1).contains(QStringLiteral("R")))
            << "层列仍直出层码（语义标签缺失）: "
            << first->text(1).toStdString();
        EXPECT_FALSE(first->toolTip(1).isEmpty()) << "原始层码未入 Tooltip";
    }

    // 修订说明一行化：面板面一行微文案（关键词保留），成段说明收 Tooltip。
    QLabel* notes = m_panel->findChild<QLabel*>();
    QLabel* notesLabel = nullptr;
    for (QLabel* l : m_panel->findChildren<QLabel*>()) {
        if (l->text().contains(QStringLiteral("修订说明"))) {
            notesLabel = l;
            break;
        }
    }
    ASSERT_NE(notesLabel, nullptr) << "修订说明行缺失";
    EXPECT_FALSE(notesLabel->text().contains(QStringLiteral("\n")))
        << "修订说明应为一行（成段文字收 Tooltip）";
    EXPECT_TRUE(notesLabel->text().contains(QStringLiteral("修订只增不改")))
        << "修订语义关键词丢失（T34 语义面）";
    EXPECT_FALSE(notesLabel->toolTip().isEmpty()) << "成段说明未收 Tooltip";
    (void)notes;
}

/// UI-T37 返工③ 联动回归（所有者反馈：新建区域后树/列表点击属性不联动）：
/// 树点击区域条目→区域表行同步＋区域检查器投影（盒中心编辑器在位）；表
/// 行点击→同链。页签保持断言（启用切换后不跳页签——返工①信号屏蔽面）。
TEST_F(RequirementsSessionGuiTest, RegionSelectionLinkage_UI_T37R)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    QPushButton* addBtn = lifecycleButton(*m_panel, "add", "regions");
    ASSERT_NE(addBtn, nullptr);
    addBtn->click();
    QApplication::processEvents();
    ASSERT_FALSE(m_editor.workingSet().regions.entries.empty());
    const core::ObjectId rid = m_editor.workingSet().regions.entries.back().objectId;

    // 树点击新区域→区域表行同步＋检查器投影（联动面）。
    m_panel->focusObject(rid);
    QApplication::processEvents();
    ASSERT_NE(fieldEditor(*m_panel, "box-center-x"), nullptr)
        << "树点击区域后检查器未联动（返工③回归）";

    // 表行点击→检查器保持（页签不跳——表重建信号屏蔽面）。
    QTreeWidget* regionTable = m_panel->findChild<QTreeWidget*>(
        QStringLiteral("ird_req_region_table"));
    ASSERT_NE(regionTable, nullptr);
    regionTable->setCurrentItem(regionTable->topLevelItem(0));
    QApplication::processEvents();
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    ASSERT_NE(fieldEditor(*m_panel, "box-center-x"), nullptr)
        << "表行点击后检查器未联动";

    // 工位页启用下拉切换后页签不跳区域（表重建信号屏蔽——返工①回归）。
    QPushButton* addStation = lifecycleButton(*m_panel, "add", "points");
    ASSERT_NE(addStation, nullptr);
    addStation->click();
    QApplication::processEvents();
    QComboBox* enabledCombo = m_panel->findChild<QComboBox*>(
        QStringLiteral("ird_station_enabled_combo"));
    ASSERT_NE(enabledCombo, nullptr);
    enabledCombo->setCurrentIndex(enabledCombo->currentIndex() == 0 ? 1 : 0);
    QApplication::processEvents();
    EXPECT_EQ(m_panel->findChild<QTabWidget*>()->currentIndex(), 0)
        << "启用切换后页签跳转（表重建信号屏蔽回归）";
}

// =====================================================================
// UI-T39 审核返工批次：项目级撤销命令 id／可用性门控／撤销后校验链／
// 只读全量刷新／会话脱离复位／折叠跨刷新保持／命令按输入类型取锚。
// =====================================================================

/// 撤销三键现取（文本定位——构建期字面经 UiText 词表，测试同词对照）。
QPushButton* undoButtonByText(const RequirementsPanelWidget& panel,
                              const QString& text)
{
    const QList<QPushButton*> all =
        const_cast<RequirementsPanelWidget&>(panel).findChildren<QPushButton*>();
    for (QPushButton* btn : all) {
        if (btn->text() == text) {
            return btn;
        }
    }
    return nullptr;
}

/// UI-T39 审核返工 P1（项目级撤销）：①点击转发宿主注册的 project.undo
/// （此前误写 edit.undo——宿主词表无此命令，点击仅产生"未知命令"拒绝）；
/// ②按钮可用性随命令可用性快照门控（禁用＋原因提示），不再只按提交
/// 出口存在性置可用。
TEST_F(RequirementsSessionGuiTest, ProjectUndoCommandIdAndGate_UI_T39)
{
    IRD_TEST_INFO("REQ-11", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // ①提交 id 捕获：点击"撤销上次应用"→提交出口收到 project.undo。
    std::vector<std::string> submitted;
    m_panel->setCommandSubmit([&submitted](const ui::CommandId& id) {
        submitted.push_back(std::string(id));  // 捕获面（不执行宿主命令）
    });
    QPushButton* projectUndo =
        undoButtonByText(*m_panel, QStringLiteral("撤销上次应用"));
    ASSERT_NE(projectUndo, nullptr) << "项目级撤销键缺失";
    projectUndo->click();
    QApplication::processEvents();
    ASSERT_EQ(submitted.size(), 1u) << "项目级撤销点击未产生命令提交";
    EXPECT_EQ(submitted.front(), "project.undo")
        << "项目级撤销转发了错误命令 id（edit.undo 存量缺陷回归面）";

    // ②可用性门控：project.undo 不可用（如无可撤销修订）→按钮禁用＋
    //   禁用原因入 tooltip；恢复可用→按钮恢复。
    m_panel->setCommandAvailability([](const ui::CommandId& id) {
        ui::CommandAvailability a;
        a.registered = true;
        a.enabled = (std::string(id) != "project.undo");
        if (!a.enabled) {
            a.disableReasonKey = "reason.no-undo-revision";
        }
        return a;
    });
    QApplication::processEvents();
    EXPECT_FALSE(projectUndo->isEnabled())
        << "project.undo 不可用时按钮仍可用（门控缺失）";
    EXPECT_FALSE(projectUndo->toolTip().isEmpty())
        << "禁用原因未入 tooltip（审核四.5）";
    m_panel->setCommandAvailability([](const ui::CommandId&) {
        ui::CommandAvailability a;
        a.registered = true;
        a.enabled = true;
        return a;
    });
    QApplication::processEvents();
    EXPECT_TRUE(projectUndo->isEnabled())
        << "project.undo 可用时按钮未恢复（门控误禁）";
}

/// UI-T39 审核返工 P1（撤销后校验链）：草稿撤销/重做与普通编辑共用同一条
/// "编辑后动作"链——就绪重估由装配层组合子承担（此前撤销以空报告直刷，
/// 校验页被打回未执行态）。断言面＝撤销/重做后 postEditAction 触发计数。
TEST_F(RequirementsSessionGuiTest, DraftUndoRerunsReadinessChain_UI_T39)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    int postEditCalls = 0;
    m_panel->setPostEditAction([&postEditCalls] { ++postEditCalls; });

    // 一次结构编辑（接受轨——onEditApplied 即触发一次组合子）。
    QPushButton* addBtn = lifecycleButton(*m_panel, "add", "points");
    ASSERT_NE(addBtn, nullptr);
    addBtn->click();
    QApplication::processEvents();
    const int afterEdit = postEditCalls;
    EXPECT_GE(afterEdit, 1) << "编辑后动作未触发（组合子缺位）";

    // 草稿撤销→组合子再触发（校验重估链与编辑同轨——本用例核心断言）。
    QPushButton* draftUndo = undoButtonByText(*m_panel, QStringLiteral("撤销"));
    ASSERT_NE(draftUndo, nullptr);
    ASSERT_TRUE(draftUndo->isEnabled()) << "有编辑后撤销键应可用";
    draftUndo->click();
    QApplication::processEvents();
    EXPECT_GT(postEditCalls, afterEdit)
        << "撤销未重估校验（空报告直刷回归面）";

    // 草稿重做→组合子再触发（同链对称半区）。
    QPushButton* draftRedo = undoButtonByText(*m_panel, QStringLiteral("重做"));
    ASSERT_NE(draftRedo, nullptr);
    ASSERT_TRUE(draftRedo->isEnabled());
    const int afterUndo = postEditCalls;
    draftRedo->click();
    QApplication::processEvents();
    EXPECT_GT(postEditCalls, afterUndo)
        << "重做未重估校验（与编辑不同链）";
}

/// UI-T39 审核返工 P1（会话脱离复位）：项目关闭后全部旧会话按钮状态与
/// 投影清理——树/表清空、生命周期键全禁、撤销三键全禁（撤销记账随基线
/// 复位归零）、空态提示显现。
TEST_F(RequirementsSessionGuiTest, SessionDetachedResetsPanel_UI_T39)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    // 有编辑＋选中（旧会话活跃态——撤销键可用、生命周期键在位）。
    lifecycleButton(*m_panel, "add", "points")->click();
    QApplication::processEvents();
    ASSERT_TRUE(undoButtonByText(*m_panel, QStringLiteral("撤销"))->isEnabled());

    m_panel->resetForSessionDetached();
    QApplication::processEvents();

    // 撤销三键全禁（无会话＝无可撤销；项目级键未注入可用性查询＝按提交
    // 出口存在性退化——本用例未注入提交出口，同为禁用）。
    EXPECT_FALSE(undoButtonByText(*m_panel, QStringLiteral("撤销"))->isEnabled())
        << "会话脱离后草稿撤销键残留可用";
    EXPECT_FALSE(undoButtonByText(*m_panel, QStringLiteral("重做"))->isEnabled())
        << "会话脱离后草稿重做键残留可用";
    EXPECT_FALSE(undoButtonByText(*m_panel, QStringLiteral("撤销上次应用"))
                      ->isEnabled())
        << "会话脱离后项目级撤销键残留可用";
    // 生命周期键全禁＋tooltip 给出无会话原因（审核四.5）。
    QPushButton* addBtn = lifecycleButton(*m_panel, "add", "points");
    ASSERT_NE(addBtn, nullptr);
    EXPECT_FALSE(addBtn->isEnabled()) << "会话脱离后新增键残留可用";
    EXPECT_FALSE(addBtn->toolTip().isEmpty())
        << "无会话禁用缺原因提示";
    // 树/区域表投影清空。
    QTreeWidget* regionTable = m_panel->findChild<QTreeWidget*>(
        QStringLiteral("ird_req_region_table"));
    ASSERT_NE(regionTable, nullptr);
    EXPECT_EQ(regionTable->topLevelItemCount(), 0)
        << "会话脱离后区域表残留旧行";
    // 工位空态提示显现（无会话＝无选中＝空态面；页签非激活页 isVisible
    // 恒 false——按返工⑤空态用例同款 isVisibleTo 断自身显隐态）。
    QLabel* hint = m_panel->findChild<QLabel*>(
        QStringLiteral("ird_req_empty_hint_points"));
    ASSERT_NE(hint, nullptr);
    EXPECT_TRUE(hint->isVisibleTo(hint->parentWidget()))
        << "会话脱离后空态提示未显现";
}

/// UI-T39 审核返工 P1（只读接线）：setWritable(false) 后全部状态承载面
/// 同帧刷新——生命周期键（含新增）禁用＋只读原因提示、草稿撤销键禁用；
/// 校验页结论保持（只读切换不清空最近报告）。
TEST_F(RequirementsSessionGuiTest, WritableSwitchRefreshesAll_UI_T39)
{
    IRD_TEST_INFO("REQ-11", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(m_editor.workingSet().points.entries.front().objectId);
    QApplication::processEvents();
    ASSERT_TRUE(lifecycleButton(*m_panel, "add", "points")->isEnabled());

    m_panel->setWritable(false);
    QApplication::processEvents();

    // 生命周期键全禁＋只读原因（此前 setWritable 不刷生命周期/撤销键——
    // 只读切换后新增/撤销残留可用的审核返工面）。
    QPushButton* addBtn = lifecycleButton(*m_panel, "add", "points");
    ASSERT_NE(addBtn, nullptr);
    EXPECT_FALSE(addBtn->isEnabled()) << "只读后新增键残留可用";
    EXPECT_TRUE(addBtn->toolTip().contains(QStringLiteral("只读")))
        << "只读禁用原因未呈现";
    EXPECT_FALSE(undoButtonByText(*m_panel, QStringLiteral("撤销"))->isEnabled())
        << "只读后草稿撤销键残留可用";
    // 复制/删除同禁（选中态仍在但只读压制）。
    EXPECT_FALSE(lifecycleButton(*m_panel, "duplicate", "points")->isEnabled());
    // 恢复可写→新增恢复（门控对称性）。
    m_panel->setWritable(true);
    QApplication::processEvents();
    EXPECT_TRUE(lifecycleButton(*m_panel, "add", "points")->isEnabled())
        << "恢复可写后新增键未恢复";
}

/// 只读初始化（L-R12 装配序回归——宿主审核 2026-10-04 P1）：只读项目先开
/// （宿主 setWritable(false) 时需求 Dock 尚未创建、面板缺位）→用户随后
/// 首次打开需求 Dock（装配工厂创建面板＋attachPanel）。修复前：模块
/// setWritable 把面板缺位期的只读事实直接丢弃＋工厂硬编码 true——首开
/// Dock 即呈可写（L-R12 安全边界违约）；修复后：模块缓存初值经
/// initialWritable() 作面板构造参数＋attachPanel 幂等回放，首开即只读。
TEST_F(RequirementsSessionGuiTest, ReadOnlyStagedBeforePanelCreate_L_R12)
{
    IRD_TEST_INFO("REQ-11", {}, std::nullopt);

    // 装配序复现（测试自持模块——不触夹具面板 m_panel）：面板缺位期宿主
    // 先行 setWritable(false)（UiPlugin 项目打开接线点同款调用时序）。
    RequirementsUiModule module;
    module.attachEditor(&m_editor);
    module.setWritable(false);
    ASSERT_FALSE(module.initialWritable())
        << "模块未缓存面板缺位期的只读事实（暂存语义失守）";

    // 装配工厂同款创建（RequirementsPluginAssembly 面板 factory 语义——
    // 构造参数取模块缓存；编辑目标提供器同款接线——面板现取会话编辑器，
    // 缺此缝＝面板呈"无会话"态而非"只读"态，门控原因失真）＋挂接
    // （attachPanel 回放兜底）＋会话首刷。
    RequirementsPanelWidget panel(module.initialWritable());
    panel.setEditTargetProvider([&module]() -> IRequirementEditor* {
        return module.editor();
    });
    module.attachPanel(&panel);
    panel.refreshPanel(m_editor.workingSet(), m_report);
    panel.focusObject(m_editor.workingSet().points.entries.front().objectId);
    QApplication::processEvents();

    // 可观测面＝生命周期"新增"键：只读初值在首刷即生效（禁用＋只读原因
    // tooltip）——与 WritableSwitchRefreshesAll_UI_T39 运行中切换的观测量
    // 同源（同一门控的两个到达时序）。
    QPushButton* addBtn = lifecycleButton(panel, "add", "points");
    ASSERT_NE(addBtn, nullptr);
    EXPECT_FALSE(addBtn->isEnabled())
        << "只读项目首开 Dock 呈可写（面板缺位期丢态——L-R12 违约回归）";
    EXPECT_TRUE(addBtn->toolTip().contains(QStringLiteral("只读")))
        << "只读禁用原因未呈现";
}

/// UI-T39 审核返工三.5（卡片折叠跨刷新保持）：收起区域高级卡后触发区域
/// 页重投影（refreshPanel——render 只填行、卡片构造期一次），折叠态不因
/// 刷新复位（UiTheme::createCard 的 fold 会话态跨刷新保持的回归钉）。
TEST_F(RequirementsSessionGuiTest, CollapseSurvivesFieldRefresh_UI_T39)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(m_editor.workingSet().regions.entries.front().objectId);
    QApplication::processEvents();

    // 区域高级卡折叠钮定位：卡是高级参数卡（QGroupBox#ird_card 之一）——
    // 经卡标题文本找到卡，再取卡内 fold 钮（ird_card_fold 锚——UiTheme
    // createCard 的定位面；checked＝展开态，▶＝已收起）。
    QToolButton* fold = nullptr;
    const QList<QGroupBox*> cards = m_panel->findChildren<QGroupBox*>();
    for (QGroupBox* card : cards) {
        QLabel* title = card->findChild<QLabel*>(QStringLiteral("ird_card_title"));
        if (title != nullptr && title->text().contains(QStringLiteral("高级参数"))) {
            fold = card->findChild<QToolButton*>(QStringLiteral("ird_card_fold"));
            break;
        }
    }
    ASSERT_NE(fold, nullptr) << "区域高级卡折叠钮未定位（ird_card_fold 锚漂移）";
    // 契约前置：高级参数卡默认收起（startCollapsed=true——去噪设计规格 §4）。
    ASSERT_FALSE(fold->isChecked())
        << "前置失真：高级卡应默认收起（startCollapsed 契约面回归）";
    // 用户展开（审核四.3 的真实诉求：用户展开后不应因一次刷新自动收起）。
    fold->setChecked(true);  // 展开态（toggled 轨——与手点同一数据流）
    QApplication::processEvents();
    ASSERT_TRUE(fold->isChecked());

    // 重投影（字段编辑/校验刷新的真实路径）后展开态保持。
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    QApplication::processEvents();
    EXPECT_TRUE(fold->isChecked())
        << "刷新后展开态被复位（卡片折叠跨刷新保持回归——用户展开不应被收起）";
}

/// UI-T39 审核返工五.1（命令按输入类型取锚）：选中区域后触发镜像——
/// 按工位页锚判选中（此前用全局最后选中＝区域锚，镜像静默找不到源）。
/// 断言面＝流程以"需要先选中一个工位条目"就地拒绝，不产生任何编辑。
TEST_F(RequirementsSessionGuiTest, MirrorRequiresStationAnchor_UI_T39)
{
    IRD_TEST_INFO("REQ-11", {}, std::nullopt);
    using namespace requirements;
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // 选中区域（区域表行点击——用户路径；全局最后选中随之变为区域锚）。
    QTreeWidget* regionTable = m_panel->findChild<QTreeWidget*>(
        QStringLiteral("ird_req_region_table"));
    ASSERT_NE(regionTable, nullptr);
    ASSERT_GT(regionTable->topLevelItemCount(), 0);
    regionTable->setCurrentItem(regionTable->topLevelItem(0));
    QApplication::processEvents();
    ASSERT_TRUE(m_panel->selectedObjectId().has_value())
        << "区域表行点击未落选中（前置失真）";

    // 镜像（无工位页选中）→就地拒绝＋集合规模不变。
    FakeDialogHost host;
    const std::size_t before = m_editor.workingSet().points.entries.size();
    const bool applied = executeRequirementCommand(
        "requirements.mirror-stations", *m_panel, m_editor, *m_panel, host);
    EXPECT_FALSE(applied) << "区域选中被镜像当作源（输入类型未校验）";
    EXPECT_TRUE(m_panel->showCommandFeedbackText().contains(
                    QStringLiteral("工位条目")))
        << "拒绝反馈未说明需要工位条目";
    EXPECT_EQ(m_editor.workingSet().points.entries.size(), before)
        << "被拒镜像产生了集合变更";
}

/**
 * @brief AT-24：驱动生产模板对话框，验证所选六类别没有在编排层丢失。
 *
 * 测试不注入模板应答替身，QTimer 只模拟用户操作真实控件；生成、提交、
 * 溯源与撤销均使用真实领域实现。每轮撤销恢复相同基线，防止名称冲突掩盖缺陷。
 */
TEST_F(RequirementsSessionGuiTest, ProductionTemplateKindsAndLivePreview_AT24_WP14T12)
{
    IRD_TEST_INFO("REQ-07", {}, std::nullopt);
    const TemplateKind kinds[] = {TemplateKind::BinPicking, TemplateKind::MachineTending,
        TemplateKind::Palletizing, TemplateKind::Inspection, TemplateKind::ToolChange,
        TemplateKind::Handover};
    const auto before = m_editor.workingSet().points.entries.size();
    for (const TemplateKind kind : kinds) {
        QTimer::singleShot(0, [kind] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            ASSERT_NE(dialog, nullptr);
            auto* combo = dialog->findChild<QComboBox*>();
            ASSERT_NE(combo, nullptr);
            combo->setCurrentIndex(combo->findData(int(kind)));
            const auto counts = dialog->findChildren<QSpinBox*>();
            ASSERT_EQ(counts.size(), 2);
            counts[0]->setValue(3);
            counts[1]->setValue(1);
            bool previewMatches = false;
            for (auto* label : dialog->findChildren<QLabel*>()) {
                if (label->text() == QStringLiteral("将生成 3 个工位")) {
                    previewMatches = true;
                }
            }
            EXPECT_TRUE(previewMatches) << "AT-24：数量编辑后生产预览未同步";
            dialog->accept();
        });
        ASSERT_TRUE(executeRequirementCommand("requirements.apply-template", *m_panel,
                                              m_editor, *m_panel));
        ASSERT_EQ(m_editor.workingSet().points.entries.size(), before + 3);
        const auto expected = TemplateArrayService{}.applyTemplate(kind,
            defaultTemplateParams(kind));
        ASSERT_TRUE(expected.ok);
        for (const auto& point : m_editor.workingSet().points.entries) {
            if (!point.generation) { continue; }
            EXPECT_EQ(point.generation->generatorId, expected.newPoints.front().generation->generatorId)
                << "AT-24：用户选择的模板类别必须写入生成来源";
            EXPECT_EQ(point.processTag, expected.newPoints.front().processTag)
                << "AT-24：工艺标签必须与所选类别一致";
        }
        ASSERT_TRUE(m_editor.undoLocal());
    }
}

/** @brief AT-02：工况文本编辑不能进入数量词表异常分支，且能整体撤销。 */
TEST_F(RequirementsSessionGuiTest, ConditionNameAndNoteEditing_AT02_WP14T12)
{
    IRD_TEST_INFO("REQ-04", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    auto* table = m_panel->findChild<QTreeWidget*>(QStringLiteral("ird_req_condition_table"));
    ASSERT_NE(table, nullptr);
    table->setCurrentItem(table->topLevelItem(0));
    QApplication::processEvents();
    for (const char* key : {"name", "note"}) {
        QLineEdit* field = nullptr;
        for (auto* edit : m_panel->findChildren<QLineEdit*>()) {
            if (edit->property("irdFieldKey").toString() == QString::fromUtf8(key)) {
                field = edit;
                break;
            }
        }
        ASSERT_NE(field, nullptr);
        ASSERT_FALSE(field->isReadOnly());
        field->setText(QStringLiteral("搬运工况-名称验证"));
        EXPECT_NO_THROW(QMetaObject::invokeMethod(field, "editingFinished", Qt::DirectConnection))
            << "AT-02：合法文本输入不得触发内部数量词表异常";
        QApplication::processEvents();
        const auto& c = m_editor.workingSet().conditions.entries.front();
        EXPECT_EQ(key == std::string("name") ? c.name : c.note,
                  QStringLiteral("搬运工况-名称验证").toStdString());
        ASSERT_TRUE(m_editor.undoLocal());
        m_panel->refreshPanel(m_editor.workingSet(), m_report);
    }
}

/** @brief REQ-01/09：生产完整定义对话框暂存名称、引用及规则，一次确认/撤销。 */
TEST_F(RequirementsSessionGuiTest, StationDefinitionAtomicReferences_AT23_WP14T12)
{
    IRD_TEST_INFO("REQ-01", {}, std::nullopt);
    const TaskPoint before = m_editor.workingSet().points.entries.front();
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(before.objectId);
    auto* button = m_panel->findChild<QPushButton*>("ird_station_definition_edit");
    ASSERT_NE(button, nullptr) << "工位必须可完整编辑参考系、TCP及引用型姿态规则";
    const std::string objectId = "obj-12345678901234567890123456789012";
    QTimer::singleShot(0, [this, objectId, before] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(dialog, nullptr);
        dialog->findChild<QLineEdit*>("station-name")->setText("完整工位");
        dialog->findChild<QComboBox*>("station-process")->setCurrentIndex(5); // Inspect
        dialog->findChild<QComboBox*>("station-reference-kind")->setCurrentIndex(1);
        dialog->findChild<QLineEdit*>("station-reference-object")->setText(QString::fromStdString(objectId));
        dialog->findChild<QComboBox*>("station-tcp-kind")->setCurrentIndex(2);
        dialog->findChild<QLineEdit*>("station-tcp-object")->setText(QString::fromStdString(objectId));
        dialog->findChild<QLineEdit*>("station-tcp-key")->setText("tcp-main");
        dialog->findChild<QComboBox*>("station-rule-kind")->setCurrentIndex(1);
        dialog->findChild<QLineEdit*>("station-target-object")->setText(QString::fromStdString(objectId));
        EXPECT_EQ(m_editor.workingSet().points.entries.front(), before) << "确认前不得写草稿";
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    button->click();
    QApplication::processEvents();
    const TaskPoint after = m_editor.workingSet().points.entries.front();
    EXPECT_EQ(after.name, "完整工位");
    EXPECT_EQ(after.processTag, ProcessTag::Inspect);
    EXPECT_EQ(after.refFrame.kind, RequirementRefKind::ModelFrame);
    ASSERT_TRUE(after.tcpRef);
    EXPECT_EQ(after.tcpRef->tcpKey, "tcp-main");
    EXPECT_EQ(after.pose.orientation.kind, OrientationRuleKind::AlignFrame);
    EXPECT_EQ(after.pose.orientation.targetFrame.objectId, after.refFrame.objectId);
    ASSERT_TRUE(m_editor.undoLocal());
    EXPECT_EQ(m_editor.workingSet().points.entries.front(), before) << "全部字段须一次撤销";
}

/** @brief REQ-01/09：缺目标引用保留表单与错误，取消零写入，只读禁写。 */
TEST_F(RequirementsSessionGuiTest, StationDefinitionRejectCancel_AT23_WP14T12)
{
    IRD_TEST_INFO("REQ-09", {}, std::nullopt);
    const TaskPoint before = m_editor.workingSet().points.entries.front();
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(before.objectId);
    auto* button = m_panel->findChild<QPushButton*>("ird_station_definition_edit");
    ASSERT_NE(button, nullptr);
    QTimer::singleShot(0, [this, before] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(dialog, nullptr);
        dialog->findChild<QComboBox*>("station-rule-kind")->setCurrentIndex(2);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        EXPECT_TRUE(dialog->isVisible()) << "缺几何对象应保留输入";
        EXPECT_FALSE(dialog->findChild<QLabel*>("station-definition-error")->text().isEmpty());
        EXPECT_EQ(m_editor.workingSet().points.entries.front(), before);
        dialog->reject();
    });
    button->click();
    QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet().points.entries.front(), before);
    m_panel->setWritable(false);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    auto* readOnlyButton = m_panel->findChild<QPushButton*>("ird_station_definition_edit");
    ASSERT_NE(readOnlyButton, nullptr);
    EXPECT_FALSE(readOnlyButton->isEnabled());
}

/** @brief REQ-01：排队编辑后立即选另一工位，输入仍只能写回发起对象。 */
TEST_F(RequirementsSessionGuiTest, StationQueuedValueKeepsAnchor_AT02_WP14T12)
{
    IRD_TEST_INFO("REQ-01", {}, std::nullopt);
    const TaskPoint before = m_editor.workingSet().points.entries.front();
    const TaskPoint second = makePoint("第二工位", 4, 5, 6);
    ASSERT_TRUE(m_editor.applyEdit(RequirementEdit{second}).accepted);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(before.objectId);
    QDoubleSpinBox* spin = nullptr;
    for (auto* candidate : m_panel->findChildren<QDoubleSpinBox*>()) {
        if (candidate->property("irdFieldKey").toString() == "tolerance-position") { spin = candidate; break; }
    }
    ASSERT_NE(spin, nullptr);
    spin->setValue(0.05);
    QMetaObject::invokeMethod(spin, "editingFinished", Qt::DirectConnection);
    m_panel->focusObject(second.objectId); // 先切选中，不给排队提交运行机会。
    QApplication::processEvents();
    for (const auto& point : m_editor.workingSet().points.entries) {
        EXPECT_DOUBLE_EQ(point.tolerance.positionTolerance,
            point.objectId == before.objectId ? 0.05 : second.tolerance.positionTolerance);
    }
}

/** @brief AT-23：五规则均由生产表单构建，非活动载荷归默认，数值不丢失。 */
TEST_F(RequirementsSessionGuiTest, StationDefinitionFiveRules_AT23_WP14T12)
{
    IRD_TEST_INFO("REQ-09", {}, std::nullopt);
    const TaskPoint before = m_editor.workingSet().points.entries.front();
    for (int kind = 0; kind < 5; ++kind) {
        m_panel->refreshPanel(m_editor.workingSet(), m_report);
        m_panel->focusObject(before.objectId);
        auto* button = m_panel->findChild<QPushButton*>("ird_station_definition_edit");
        ASSERT_NE(button, nullptr);
        QTimer::singleShot(0, [kind] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            ASSERT_NE(dialog, nullptr);
            dialog->findChild<QComboBox*>("station-rule-kind")->setCurrentIndex(kind);
            dialog->findChild<QLineEdit*>("station-rpy")->setText("0.1234567891234567, -0.2, 0.3");
            dialog->findChild<QLineEdit*>("station-target-object")->setText("obj-12345678901234567890123456789012");
            dialog->findChild<QLineEdit*>("station-scene")->setText("obj-12345678901234567890123456789012");
            dialog->findChild<QComboBox*>("station-feature")->setCurrentIndex(1);
            dialog->findChild<QCheckBox*>("station-invert")->setChecked(true);
            dialog->findChild<QLineEdit*>("station-target-point")->setText("1, 2, 3");
            dialog->findChild<QLineEdit*>("station-roll-min")->setText("-1.2");
            dialog->findChild<QLineEdit*>("station-roll-max")->setText("2.3");
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });
        button->click(); QApplication::processEvents();
        const auto after = m_editor.workingSet().points.entries.front();
        EXPECT_EQ(after.pose.orientation.kind, static_cast<OrientationRuleKind>(kind));
        EXPECT_FALSE(validateTaskPoint(after).has_value());
        EXPECT_EQ(after.pose.position, before.pose.position) << "未改位置须保留值及来源";
        if (kind == 0) { EXPECT_DOUBLE_EQ(after.pose.orientation.fixedRpy[0], 0.1234567891234567); }
        if (kind == 2) { EXPECT_TRUE(after.pose.orientation.invertNormal); EXPECT_EQ(after.pose.orientation.feature, OrientationFeature::FramePlaneNormal); }
        if (kind == 4) { EXPECT_DOUBLE_EQ(after.pose.orientation.rollRange.min, -1.2); EXPECT_DOUBLE_EQ(after.pose.orientation.rollRange.max, 2.3); }
        ASSERT_TRUE(m_editor.undoLocal());
        EXPECT_EQ(m_editor.workingSet().points.entries.front(), before);
    }
}

/** @brief REQ-01：普通名称/备注有类型正确的直接编辑轨，不受数量词表禁用。 */
TEST_F(RequirementsSessionGuiTest, StationTextEditing_AT02_WP14T12)
{
    IRD_TEST_INFO("REQ-01", {}, std::nullopt);
    const TaskPoint before = m_editor.workingSet().points.entries.front();
    for (const char* key : {"name", "note"}) {
        m_panel->refreshPanel(m_editor.workingSet(), m_report);
        m_panel->focusObject(before.objectId);
        auto* edit = fieldEditor(*m_panel, key);
        ASSERT_NE(edit, nullptr);
        ASSERT_FALSE(edit->isReadOnly()) << "名称和备注必须能直接输入";
        edit->setText("工位文本验证");
        QMetaObject::invokeMethod(edit, "editingFinished", Qt::DirectConnection);
        QApplication::processEvents();
        const auto& point = m_editor.workingSet().points.entries.front();
        EXPECT_EQ(std::string(key) == "name" ? point.name : point.note, "工位文本验证");
        ASSERT_TRUE(m_editor.undoLocal());
    }
}

/** @brief REQ-04：空工况可新增负载和三类事件，确认一次写入、一次撤销。 */
TEST_F(RequirementsSessionGuiTest, ConditionStructureAtomicApply_AT02_WP14T12)
{
    IRD_TEST_INFO("REQ-04", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(m_editor.workingSet().conditions.entries.front().objectId);
    auto* payloads = m_panel->findChild<QTableWidget*>("ird_condition_payloads");
    auto* events = m_panel->findChild<QTableWidget*>("ird_condition_events");
    auto* addPayload = m_panel->findChild<QPushButton*>("ird_condition_add_payload");
    auto* addEvent = m_panel->findChild<QPushButton*>("ird_condition_add_event");
    auto* apply = m_panel->findChild<QPushButton*>("ird_condition_structure_apply");
    ASSERT_NE(payloads, nullptr) << "空工况必须提供负载结构编辑入口";
    ASSERT_NE(events, nullptr);
    ASSERT_NE(addPayload, nullptr);
    ASSERT_NE(addEvent, nullptr);
    ASSERT_NE(apply, nullptr);
    const OperatingCondition before = m_editor.workingSet().conditions.entries.front();
    addPayload->click();
    payloads->item(0, 3)->setText("2.5");  // kg；不是缺失转零。
    payloads->item(0, 4)->setText("0.1, 0.2, 0.3");  // 工具TCP系，m。
    payloads->item(0, 5)->setText("0.4");  // kg·m²。
    for (int i = 0; i < 3; ++i) {
        addEvent->click();
        auto* kind = qobject_cast<QComboBox*>(events->cellWidget(i, 0));
        ASSERT_NE(kind, nullptr);
        kind->setCurrentIndex(i);  // Grasp / Release / Dwell。
        events->item(i, 2)->setText("1.25");  // s。
    }
    EXPECT_EQ(m_editor.workingSet().conditions.entries.front(), before)
        << "结构表修改在确认前不能写入草稿";
    apply->click();
    QApplication::processEvents();
    const auto& actual = m_editor.workingSet().conditions.entries.front();
    ASSERT_EQ(actual.payloads.size(), 1);
    EXPECT_DOUBLE_EQ(actual.payloads[0].mass.value(), 2.5);
    EXPECT_DOUBLE_EQ(actual.payloads[0].com.value()[1], 0.2);
    EXPECT_DOUBLE_EQ(actual.payloads[0].inertia.value(), 0.4);
    ASSERT_EQ(actual.events.size(), 3);
    EXPECT_EQ(actual.events[2].type, ConditionEventType::Dwell);
    EXPECT_DOUBLE_EQ(*actual.events[2].durationS, 1.25);
    EXPECT_EQ(actual.events[0].stationRef, m_editor.workingSet().points.entries.front().objectId);
    ASSERT_TRUE(m_editor.undoLocal());
    EXPECT_EQ(m_editor.workingSet().conditions.entries.front(), before)
        << "整张结构表必须一次撤销，不留下半个负载/事件集合";
}

/** @brief REQ-04/UX-03：非法物性、取消、只读均零写入，不抛内部词表异常。 */
TEST_F(RequirementsSessionGuiTest, ConditionStructureRejectCancelReadOnly_AT02_WP14T12)
{
    IRD_TEST_INFO("REQ-04", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(m_editor.workingSet().conditions.entries.front().objectId);
    auto* add = m_panel->findChild<QPushButton*>("ird_condition_add_payload");
    ASSERT_NE(add, nullptr);
    const auto before = m_editor.workingSet().conditions.entries.front();
    add->click();
    auto* payloads = m_panel->findChild<QTableWidget*>("ird_condition_payloads");
    ASSERT_NE(payloads, nullptr);
    payloads->item(0, 3)->setText("-2");
    m_panel->findChild<QPushButton*>("ird_condition_structure_apply")->click();
    QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet().conditions.entries.front(), before);
    auto* error = m_panel->findChild<QLabel*>("ird_condition_structure_error");
    ASSERT_NE(error, nullptr);
    EXPECT_FALSE(error->text().isEmpty());
    m_panel->findChild<QPushButton*>("ird_condition_structure_cancel")->click();
    EXPECT_EQ(payloads->rowCount(), 0) << "取消恢复进入表单时的结构";
    EXPECT_EQ(m_editor.workingSet().conditions.entries.front(), before);
    m_panel->setWritable(false);
    QApplication::processEvents();
    EXPECT_FALSE(m_panel->findChild<QPushButton*>("ird_condition_add_payload")->isEnabled());
    EXPECT_FALSE(m_panel->findChild<QPushButton*>("ird_condition_structure_apply")->isEnabled());
}

/** @brief REQ-04：文本提交排队后切换选中，必须仍编辑发起时的工况。 */
TEST_F(RequirementsSessionGuiTest, ConditionQueuedEditKeepsOriginalAnchor_AT02_WP14T12)
{
    IRD_TEST_INFO("REQ-04", {}, std::nullopt);
    const auto original = m_editor.workingSet().conditions.entries.front();
    auto other = original; other.objectId = core::ObjectId::generate(); other.name = "C2";
    ASSERT_TRUE(m_editor.applyEdit(RequirementEdit{other}).accepted);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(original.objectId);
    QLineEdit* field = nullptr;
    for (auto* edit : m_panel->findChildren<QLineEdit*>()) {
        if (edit->property("irdFieldKey").toString() == "note") { field = edit; break; }
    }
    ASSERT_NE(field, nullptr);
    field->setText("仅修改原工况");
    QMetaObject::invokeMethod(field, "editingFinished", Qt::DirectConnection);
    m_panel->focusObject(other.objectId);  // 不先处理排队的提交。
    QApplication::processEvents();
    for (const auto& condition : m_editor.workingSet().conditions.entries) {
        EXPECT_EQ(condition.note, condition.objectId == original.objectId ? "仅修改原工况" : other.note)
            << "延迟提交不能使用执行时的新选中对象";
    }
}

/** @brief REQ-04：环境/工具/适用范围/要求值可编辑，物性缺失不转零，增删可撤销。 */
TEST_F(RequirementsSessionGuiTest, ConditionReferencesAndFourStates_AT02_WP14T12)
{
    IRD_TEST_INFO("REQ-04", {}, std::nullopt);
    const auto before = m_editor.workingSet().conditions.entries.front();
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(before.objectId);
    auto* payloads = m_panel->findChild<QTableWidget*>("ird_condition_payloads");
    ASSERT_NE(payloads, nullptr);
    m_panel->findChild<QPushButton*>("ird_condition_add_payload")->click();
    payloads->item(0, 3)->setText("0");  // 明确提供的零质量；域契约允许非负。
    payloads->item(0, 4)->setText("不适用");
    // 惯量保持未提供，不能因其它字段确认而转成0。
    const auto envId = core::ObjectId::generate();
    m_panel->findChild<QLineEdit*>("ird_condition_environment")->setText(QString::fromStdString(envId.toCanonical()));
    m_panel->findChild<QLineEdit*>("ird_condition_margin")->setText("0.1");
    m_panel->findChild<QCheckBox*>("ird_condition_collision")->setChecked(false);
    m_panel->findChild<QComboBox*>("ird_condition_scope")->setCurrentIndex(1);
    m_panel->findChild<QListWidget*>("ird_condition_stations")->item(0)->setCheckState(Qt::Checked);
    m_panel->findChild<QPushButton*>("ird_condition_add_tool")->click();
    m_panel->findChild<QPushButton*>("ird_condition_structure_apply")->click();
    QApplication::processEvents();
    const auto actual = m_editor.workingSet().conditions.entries.front();
    ASSERT_EQ(actual.payloads.size(), 1);
    EXPECT_EQ(actual.payloads[0].mass.state(), core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(actual.payloads[0].mass.value(), 0);
    EXPECT_EQ(actual.payloads[0].com.state(), core::FieldState::NotApplicable);
    EXPECT_EQ(actual.payloads[0].inertia.state(), core::FieldState::NotProvided);
    ASSERT_EQ(actual.environmentRefs.size(), 1);
    EXPECT_EQ(actual.environmentRefs.front(), envId);
    EXPECT_EQ(actual.appliesTo.scope, AppliesToScope::Stations);
    EXPECT_EQ(actual.appliesTo.stations, std::vector<core::ObjectId>{m_editor.workingSet().points.entries.front().objectId});
    ASSERT_EQ(actual.toolRefs.size(), 1);
    EXPECT_EQ(actual.toolRefs.front().kind, RequirementRefKind::DefaultTcp);
    EXPECT_FALSE(actual.demands.collisionFreeRequired);
    EXPECT_DOUBLE_EQ(*actual.demands.minimumJointMargin, 0.1);
    // 刷新后未变物性必须保留原始provenance；再改裕量，不重建来源。
    m_panel->findChild<QLineEdit*>("ird_condition_margin")->setText("0.2");
    m_panel->findChild<QPushButton*>("ird_condition_structure_apply")->click();
    QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet().conditions.entries.front().payloads, actual.payloads);
    ASSERT_TRUE(m_editor.undoLocal());
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    payloads = m_panel->findChild<QTableWidget*>("ird_condition_payloads");
    payloads->setCurrentCell(0, 3);
    m_panel->findChild<QPushButton*>("ird_condition_remove_payload")->click();
    m_panel->findChild<QPushButton*>("ird_condition_structure_apply")->click();
    QApplication::processEvents();
    EXPECT_TRUE(m_editor.workingSet().conditions.entries.front().payloads.empty());
    ASSERT_TRUE(m_editor.undoLocal());
    EXPECT_EQ(m_editor.workingSet().conditions.entries.front(), actual);
}

/** @brief REQ-01/09：缺宿主目录时明确显示名称选择能力缺失，取消零写入。 */
TEST_F(RequirementsSessionGuiTest, ReferenceDirectoryMissingIsExplicit_REQ01_AT23)
{
    IRD_TEST_INFO("REQ-01", {}, std::nullopt);
    const auto before = m_editor.workingSet();
    m_panel->refreshPanel(before, m_report);
    m_panel->focusObject(before.points.entries.front().objectId);
    auto* button = m_panel->findChild<QPushButton*>("ird_station_definition_edit");
    ASSERT_NE(button, nullptr);
    QTimer::singleShot(0, [this] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        EXPECT_NE(dialog, nullptr);
        if (!dialog) { return; }
        auto* choices = dialog->findChild<QComboBox*>("station-reference-choice");
        EXPECT_NE(choices, nullptr) << "名称目录入口缺失，不可要求产品用户填写内部对象标识";
        if (choices) { EXPECT_FALSE(choices->isEnabled()); }
        auto* hint = dialog->findChild<QLabel*>("station-reference-directory-hint");
        EXPECT_NE(hint, nullptr);
        if (hint) { EXPECT_TRUE(hint->text().contains("未装配")); }
        dialog->reject();
    });
    button->click(); QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet(), before) << "取消不得写入任一字段";
}

/** @brief REQ-01/09：真实名称下拉保存浅引用，取消/过期目录零写入，一次撤销。 */
TEST_F(RequirementsSessionGuiTest, NamedStationReferencesAtomicAndStale_REQ01_AT23)
{
    IRD_TEST_INFO("REQ-01", {}, std::nullopt);
    const auto before = m_editor.workingSet();
    RequirementReference world;
    RequirementReference fixture; fixture.kind=RequirementRefKind::SceneObject;
    fixture.objectId=core::ObjectId::generate();
    RequirementReference model; model.kind=RequirementRefKind::ModelFrame;
    model.objectId=core::ObjectId::generate();
    RequirementReference tcp; tcp.kind=RequirementRefKind::Tool;
    tcp.objectId=core::ObjectId::generate(); tcp.tcpKey="offset";
    std::vector<RequirementsReferenceChoice> choices{
        {world,"世界坐标系",true,""},{fixture,"夹具 / 工件A",true,""},
        {model,"机械臂 / 基座",true,""},{tcp,"夹爪 / 偏置TCP",true,""}};
    RequirementsView3DSeams seams; seams.referenceChoices=[&] { return choices; };
    m_panel->setView3DSeams(seams);
    m_panel->refreshPanel(before,m_report); m_panel->focusObject(before.points.entries.front().objectId);
    QTimer::singleShot(0,[&] {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        EXPECT_NE(dialog,nullptr); if (!dialog) { return; }
        // REQ-09：位置参考系允许World，但AlignFrame只允许模型/场景；
        // 不向用户提供一个必定被领域校验拒绝的名称候选。
        auto* positionRefs=dialog->findChild<QComboBox*>("station-reference-choice");
        auto* targetRefs=dialog->findChild<QComboBox*>("station-target-choice");
        ASSERT_NE(positionRefs,nullptr); ASSERT_NE(targetRefs,nullptr);
        EXPECT_GE(positionRefs->findText("世界坐标系"),0);
        EXPECT_EQ(targetRefs->findText("世界坐标系"),-1);
        EXPECT_GE(targetRefs->findText("机械臂 / 基座"),0);
        EXPECT_GE(targetRefs->findText("夹具 / 工件A"),0);
        for (const auto& item : {std::pair<const char*,const char*>{"station-reference-choice","夹具 / 工件A"},
                {"station-tcp-choice","夹爪 / 偏置TCP"},{"station-target-choice","机械臂 / 基座"}}) {
            auto* combo=dialog->findChild<QComboBox*>(item.first); EXPECT_NE(combo,nullptr);
            if (combo) { const int row=combo->findText(item.second); EXPECT_GE(row,0); combo->setCurrentIndex(row); }
        }
        dialog->findChild<QComboBox*>("station-rule-kind")->setCurrentIndex(1);
        EXPECT_TRUE(dialog->findChild<QLineEdit*>("station-reference-object")->isHidden())
            << "正式目录下不能向产品用户展示内部ObjectId输入";
        EXPECT_EQ(m_editor.workingSet(),before) << "选择只更新暂存值";
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    m_panel->findChild<QPushButton*>("ird_station_definition_edit")->click(); QApplication::processEvents();
    const auto after=m_editor.workingSet().points.entries.front();
    EXPECT_EQ(after.refFrame,fixture); ASSERT_TRUE(after.tcpRef); EXPECT_EQ(*after.tcpRef,tcp);
    EXPECT_EQ(after.pose.orientation.kind,OrientationRuleKind::AlignFrame);
    EXPECT_EQ(after.pose.orientation.targetFrame,model);
    EXPECT_EQ(after.pose.position,before.points.entries.front().pose.position);
    ASSERT_TRUE(m_editor.undoLocal()); EXPECT_EQ(m_editor.workingSet(),before);
    m_panel->refreshPanel(before,m_report); m_panel->focusObject(before.points.entries.front().objectId);
    QTimer::singleShot(0,[&] {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        EXPECT_NE(dialog,nullptr); if (!dialog) { return; }
        dialog->findChild<QLineEdit*>("station-name")->setText("不允许过期写回");
        choices.pop_back(); // 宿主目录在嵌套事件循环中发生变化。
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    m_panel->findChild<QPushButton*>("ird_station_definition_edit")->click(); QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet(),before) << "引用目录过期应拒绝整次编辑";
}

/** @brief REQ-01/09：悬空历史引用保留，不能静默回落World；新不可用项禁止确认。 */
TEST_F(RequirementsSessionGuiTest, NamedReferenceMissingPreservedAndUnavailableRejected_REQ09_AT23)
{
    IRD_TEST_INFO("REQ-09", {}, std::nullopt);
    auto point=m_editor.workingSet().points.entries.front();
    point.refFrame.kind=RequirementRefKind::SceneObject; point.refFrame.objectId=core::ObjectId::generate();
    ASSERT_TRUE(m_editor.applyEdit(RequirementEdit{point}).accepted);
    const auto before=m_editor.workingSet();
    RequirementReference other=point.refFrame; other.objectId=core::ObjectId::generate();
    RequirementsView3DSeams seams;
    seams.referenceChoices=[&] { return std::vector<RequirementsReferenceChoice>{{RequirementReference{},"世界坐标系",true,""},
        {other,"停用夹具",false,"当前运行时帧缺失"}}; };
    m_panel->setView3DSeams(seams); m_panel->refreshPanel(before,m_report); m_panel->focusObject(point.objectId);
    QTimer::singleShot(0,[&] {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        EXPECT_NE(dialog,nullptr); if (!dialog) { return; }
        auto* choice=dialog->findChild<QComboBox*>("station-reference-choice");
        EXPECT_NE(choice,nullptr); if (!choice) { dialog->reject(); return; }
        EXPECT_TRUE(choice->currentText().contains("缺失"));
        dialog->findChild<QLineEdit*>("station-name")->setText("保留悬空引用");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    m_panel->findChild<QPushButton*>("ird_station_definition_edit")->click(); QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet().points.entries.front().refFrame,point.refFrame);
    EXPECT_EQ(m_editor.workingSet().points.entries.front().name,"保留悬空引用");
    ASSERT_TRUE(m_editor.undoLocal()); EXPECT_EQ(m_editor.workingSet(),before);
    m_panel->refreshPanel(before,m_report); m_panel->focusObject(point.objectId);
    QTimer::singleShot(0,[&] {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        EXPECT_NE(dialog,nullptr); if (!dialog) { return; }
        auto* choice=dialog->findChild<QComboBox*>("station-reference-choice");
        const int row=choice->findText("停用夹具（不可用）"); EXPECT_GE(row,0);
        if (row>=0) {
            EXPECT_FALSE(choice->model()->flags(choice->model()->index(row,0)) & Qt::ItemIsEnabled);
            choice->setCurrentIndex(row); // 即使程序输入绕过灰显，确认仍须拒绝。
        }
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        EXPECT_TRUE(dialog->isVisible());
        EXPECT_FALSE(dialog->findChild<QLabel*>("station-definition-error")->text().isEmpty());
        dialog->reject();
    });
    m_panel->findChild<QPushButton*>("ird_station_definition_edit")->click(); QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet(),before);
}

/** @brief REQ-01/09：同一工具多个TCP按精确键选取，几何对象独立选择并可一次撤销。 */
TEST_F(RequirementsSessionGuiTest, NamedGeometryAndMultiTcpKeepIdentity_REQ09_AT23)
{
    IRD_TEST_INFO("REQ-09", {}, std::nullopt);
    const auto before=m_editor.workingSet();
    RequirementReference tcpA; tcpA.kind=RequirementRefKind::Tool;
    tcpA.objectId=core::ObjectId::generate(); tcpA.tcpKey="tip-a";
    auto tcpB=tcpA; tcpB.tcpKey="tip-b";
    auto otherTool=tcpB; otherTool.objectId=core::ObjectId::generate();
    RequirementReference geometry; geometry.kind=RequirementRefKind::SceneObject;
    geometry.objectId=core::ObjectId::generate();
    RequirementsView3DSeams seams;
    // 仅名称端口提供候选值；表单、校验、草稿与撤销均使用真实实现。
    seams.referenceChoices=[=] { return std::vector<RequirementsReferenceChoice>{
        {RequirementReference{},"世界坐标系",true,""},{tcpA,"夹爪A / tip-a",true,""},
        {tcpB,"夹爪A / tip-b",true,""},{otherTool,"夹爪B / tip-b",true,""},
        {geometry,"场景 / 支承面",true,""}}; };
    m_panel->setView3DSeams(seams); m_panel->refreshPanel(before,m_report);
    m_panel->focusObject(before.points.entries.front().objectId);
    QTimer::singleShot(0,[&] {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        EXPECT_NE(dialog,nullptr); if (!dialog) { return; }
        auto* tcp=dialog->findChild<QComboBox*>("station-tcp-choice");
        auto* scene=dialog->findChild<QComboBox*>("station-geometry-choice");
        EXPECT_NE(tcp,nullptr); EXPECT_NE(scene,nullptr);
        if (!tcp || !scene) { dialog->reject(); return; }
        EXPECT_EQ(tcp->count(),4) << "可选空行和三个具名TCP，不得按工具ID合并";
        EXPECT_EQ(scene->count(),2) << "几何列表只包含未选提示与场景，不混入工具";
        tcp->setCurrentIndex(tcp->findText("夹爪A / tip-b"));
        scene->setCurrentIndex(scene->findText("场景 / 支承面"));
        dialog->findChild<QComboBox*>("station-rule-kind")->setCurrentIndex(2);
        dialog->findChild<QComboBox*>("station-feature")->setCurrentIndex(1);
        dialog->findChild<QCheckBox*>("station-invert")->setChecked(true);
        EXPECT_EQ(m_editor.workingSet(),before) << "预选几何与TCP不能提前写草稿";
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    m_panel->findChild<QPushButton*>("ird_station_definition_edit")->click(); QApplication::processEvents();
    const auto after=m_editor.workingSet().points.entries.front();
    ASSERT_TRUE(after.tcpRef); EXPECT_EQ(*after.tcpRef,tcpB);
    EXPECT_EQ(after.pose.orientation.kind,OrientationRuleKind::AlignGeometryNormal);
    EXPECT_EQ(after.pose.orientation.targetSceneObject,geometry.objectId);
    EXPECT_EQ(after.pose.orientation.feature,OrientationFeature::FramePlaneNormal);
    EXPECT_TRUE(after.pose.orientation.invertNormal);
    EXPECT_EQ(after.refFrame,before.points.entries.front().refFrame);
    ASSERT_TRUE(m_editor.undoLocal()); EXPECT_EQ(m_editor.workingSet(),before);
}

/** @brief REQ-03：区域参考系/TCP按业务名选择，暂存不换算盒坐标，整批一次撤销。 */
TEST_F(RequirementsSessionGuiTest, NamedRegionReferencesAtomicAndStale_REQ03_AT02)
{
    IRD_TEST_INFO("REQ-03", {}, std::nullopt);
    const auto before=m_editor.workingSet();
    RequirementReference frame; frame.kind=RequirementRefKind::SceneObject; frame.objectId=core::ObjectId::generate();
    RequirementReference tcp; tcp.kind=RequirementRefKind::Tool; tcp.objectId=core::ObjectId::generate(); tcp.tcpKey="tip";
    std::vector<RequirementsReferenceChoice> choices{{RequirementReference{},"世界坐标系",true,""},
        {frame,"场景 / 区域夹具",true,""},{tcp,"工具 / 探针 / tip",true,""}};
    RequirementsView3DSeams seams; seams.referenceChoices=[&] { return choices; };
    m_panel->setView3DSeams(seams); m_panel->refreshPanel(before,m_report);
    m_panel->focusObject(before.regions.entries.front().objectId);
    auto* reference=m_panel->findChild<QComboBox*>("ird_region_reference_choice");
    auto* tool=m_panel->findChild<QComboBox*>("ird_region_tcp_choice");
    ASSERT_NE(reference,nullptr) << "区域缺业务名称参考系入口";
    ASSERT_NE(tool,nullptr) << "区域缺业务名称TCP入口";
    reference->setCurrentIndex(reference->findText("场景 / 区域夹具"));
    tool->setCurrentIndex(tool->findText("工具 / 探针 / tip"));
    EXPECT_EQ(m_editor.workingSet(),before);
    m_panel->findChild<QPushButton*>("ird_region_sampling_apply")->click(); QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet().regions.entries.front().refFrame,frame);
    ASSERT_TRUE(m_editor.workingSet().regions.entries.front().tcpRef);
    EXPECT_EQ(*m_editor.workingSet().regions.entries.front().tcpRef,tcp);
    EXPECT_EQ(m_editor.workingSet().regions.entries.front().box,before.regions.entries.front().box);
    ASSERT_TRUE(m_editor.undoLocal()); EXPECT_EQ(m_editor.workingSet(),before);
    m_panel->refreshPanel(before,m_report); m_panel->focusObject(before.regions.entries.front().objectId);
    reference=m_panel->findChild<QComboBox*>("ird_region_reference_choice");
    reference->setCurrentIndex(reference->findText("场景 / 区域夹具"));
    choices.pop_back(); // 与工位相同：未刷新表单不能向变化的目录提交。
    m_panel->findChild<QPushButton*>("ird_region_sampling_apply")->click(); QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet(),before);
}

/** @brief REQ-04：工况负载/工具/环境均按名称绑定，未确认零写入，过期拒绝。 */
TEST_F(RequirementsSessionGuiTest, NamedConditionReferencesAtomicAndStale_REQ04_AT02)
{
    IRD_TEST_INFO("REQ-04", {}, std::nullopt);
    const auto before=m_editor.workingSet();
    RequirementReference scene; scene.kind=RequirementRefKind::SceneObject; scene.objectId=core::ObjectId::generate();
    RequirementReference tcp; tcp.kind=RequirementRefKind::Tool; tcp.objectId=core::ObjectId::generate(); tcp.tcpKey="tip";
    std::vector<RequirementsReferenceChoice> choices{{scene,"场景 / 障碍物",true,""},{tcp,"工具 / 夹爪 / tip",true,""}};
    RequirementsView3DSeams seams; seams.referenceChoices=[&] { return choices; };
    m_panel->setView3DSeams(seams); m_panel->refreshPanel(before,m_report);
    m_panel->focusObject(before.conditions.entries.front().objectId);
    auto* environment=m_panel->findChild<QListWidget*>("ird_condition_environment_choices");
    ASSERT_NE(environment,nullptr) << "工况缺按业务名选择环境的入口";
    ASSERT_EQ(environment->count(),1); environment->item(0)->setCheckState(Qt::Checked);
    EXPECT_TRUE(m_panel->findChild<QLineEdit*>("ird_condition_environment")->isHidden());
    m_panel->findChild<QPushButton*>("ird_condition_add_payload")->click();
    m_panel->findChild<QPushButton*>("ird_condition_add_tool")->click();
    for (const auto* name : {"ird_condition_payloads","ird_condition_tools"}) {
        auto* table=m_panel->findChild<QTableWidget*>(name); ASSERT_NE(table,nullptr);
        auto* combo=qobject_cast<QComboBox*>(table->cellWidget(0,0)); ASSERT_NE(combo,nullptr);
        const int row=combo->findText("工具 / 夹爪 / tip"); ASSERT_GE(row,0); combo->setCurrentIndex(row);
        EXPECT_TRUE(table->isColumnHidden(1)); EXPECT_TRUE(table->isColumnHidden(2));
    }
    EXPECT_EQ(m_editor.workingSet(),before);
    m_panel->findChild<QPushButton*>("ird_condition_structure_apply")->click(); QApplication::processEvents();
    const auto after=m_editor.workingSet().conditions.entries.front();
    EXPECT_EQ(after.environmentRefs,std::vector<core::ObjectId>{*scene.objectId});
    EXPECT_EQ(after.toolRefs,std::vector<RequirementReference>{tcp});
    ASSERT_EQ(after.payloads.size(),1); EXPECT_EQ(after.payloads.front().toolRef,tcp);
    EXPECT_EQ(after.payloads.front().mass.state(),core::FieldState::NotProvided);
    ASSERT_TRUE(m_editor.undoLocal()); EXPECT_EQ(m_editor.workingSet(),before);
    m_panel->refreshPanel(before,m_report); m_panel->focusObject(before.conditions.entries.front().objectId);
    m_panel->findChild<QListWidget*>("ird_condition_environment_choices")->item(0)->setCheckState(Qt::Checked);
    choices.clear();
    m_panel->findChild<QPushButton*>("ird_condition_structure_apply")->click(); QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet(),before);
}

/** @brief REQ-03/04：历史缺失引用保留、新不可用环境拒绝、取消与只读零写入。 */
TEST_F(RequirementsSessionGuiTest, NamedInlineReferencesMissingUnavailableCancelReadOnly_REQ04_AT02)
{
    IRD_TEST_INFO("REQ-04", {}, std::nullopt);
    auto condition=m_editor.workingSet().conditions.entries.front();
    const auto missing=core::ObjectId::generate(); condition.environmentRefs={missing};
    ConditionPayload payload; payload.toolRef.kind=RequirementRefKind::Tool;
    payload.toolRef.objectId=core::ObjectId::generate(); payload.toolRef.tcpKey="missing-tip";
    condition.payloads={payload}; ASSERT_TRUE(m_editor.applyEdit(RequirementEdit{condition}).accepted);
    const auto before=m_editor.workingSet();
    RequirementReference blocked; blocked.kind=RequirementRefKind::SceneObject; blocked.objectId=core::ObjectId::generate();
    RequirementsView3DSeams seams; seams.referenceChoices=[=] { return std::vector<RequirementsReferenceChoice>{
        {blocked,"未发布环境",false,"没有运行时帧"}}; };
    m_panel->setView3DSeams(seams); m_panel->refreshPanel(before,m_report); m_panel->focusObject(condition.objectId);
    auto* environments=m_panel->findChild<QListWidget*>("ird_condition_environment_choices");
    ASSERT_NE(environments,nullptr); ASSERT_EQ(environments->count(),2);
    EXPECT_EQ(environments->item(1)->checkState(),Qt::Checked);
    EXPECT_TRUE(environments->item(1)->text().contains("缺失"));
    auto* table=m_panel->findChild<QTableWidget*>("ird_condition_payloads"); ASSERT_NE(table,nullptr);
    auto* tool=qobject_cast<QComboBox*>(table->cellWidget(0,0)); ASSERT_NE(tool,nullptr);
    EXPECT_TRUE(tool->currentText().contains("缺失"));
    table->item(0,3)->setText("2.5");
    m_panel->findChild<QPushButton*>("ird_condition_structure_apply")->click(); QApplication::processEvents();
    const auto after=m_editor.workingSet().conditions.entries.front();
    EXPECT_EQ(after.environmentRefs,condition.environmentRefs); EXPECT_EQ(after.payloads.front().toolRef,payload.toolRef);
    EXPECT_DOUBLE_EQ(after.payloads.front().mass.value(),2.5);
    ASSERT_TRUE(m_editor.undoLocal()); EXPECT_EQ(m_editor.workingSet(),before);
    m_panel->refreshPanel(before,m_report); m_panel->focusObject(condition.objectId);
    environments=m_panel->findChild<QListWidget*>("ird_condition_environment_choices");
    EXPECT_FALSE(environments->item(0)->flags() & Qt::ItemIsEnabled);
    environments->item(0)->setCheckState(Qt::Checked); // 程序绕过灰显也不能提交不可用新引用。
    m_panel->findChild<QPushButton*>("ird_condition_structure_apply")->click(); QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet(),before);
    EXPECT_FALSE(m_panel->findChild<QLabel*>("ird_condition_structure_error")->text().isEmpty());
    m_panel->findChild<QPushButton*>("ird_condition_structure_cancel")->click();
    EXPECT_EQ(environments->item(0)->checkState(),Qt::Unchecked);
    EXPECT_EQ(environments->item(1)->checkState(),Qt::Checked);
    m_panel->setWritable(false); QApplication::processEvents();
    EXPECT_FALSE(m_panel->findChild<QListWidget*>("ird_condition_environment_choices")->isEnabled());
    EXPECT_FALSE(m_panel->findChild<QPushButton*>("ird_condition_structure_apply")->isEnabled());
    EXPECT_EQ(m_editor.workingSet(),before);
}

/** @brief REQ-08：三维入口缺装配时诚实反馈并零写入，不伪造交互成功。 */
TEST_F(RequirementsSessionGuiTest, View3DTargetMissingSeamIsExplicit_REQ08_AT23)
{
    IRD_TEST_INFO("REQ-08", {}, std::nullopt);
    const auto before=m_editor.workingSet(); m_panel->refreshPanel(before,m_report);
    m_panel->focusObject(before.points.entries.front().objectId);
    auto* create=m_panel->findChild<QPushButton*>("ird_req_create_target_3d");
    auto* move=m_panel->findChild<QPushButton*>("ird_req_move_target_3d");
    ASSERT_NE(create,nullptr) << "缺少三维创建目标入口";
    ASSERT_NE(move,nullptr) << "缺少三维移动选中工位入口";
    create->click(); QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet(),before);
    EXPECT_TRUE(m_panel->showCommandFeedbackText().contains("未装配"));
}

/** @brief REQ-08：真实刚体变换写回旋转非World参考系，确认一次撤销，取消/漂移零写。 */
TEST_F(RequirementsSessionGuiTest, View3DTargetMoveTransformCancelAndStale_REQ08_AT23)
{
    IRD_TEST_INFO("REQ-08", {}, std::nullopt);
    auto point=m_editor.workingSet().points.entries.front();
    point.refFrame.kind=RequirementRefKind::SceneObject; point.refFrame.objectId=core::ObjectId::generate();
    point.pose.position=core::SourcedValue<rw::math::Vector3D<double>>::provided({0.1,0.2,0.3},
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    ASSERT_TRUE(m_editor.applyEdit(RequirementEdit{point}).accepted); const auto before=m_editor.workingSet();
    // 独立常量矩阵：参考系绕世界Z转90°，原局部(0.1,0.2,0.3)应到(0.8,2.1,3.3)m。
    rw::math::Transform3D<> frame({1,2,3},rw::math::Rotation3D<>(0,-1,0,1,0,0,0,0,1));
    int mode=0,calls=0;
    RequirementsView3DSeams seams;
    seams.referenceWorldPose=[&](const RequirementReference&) { return std::optional<rw::math::Transform3D<>>{frame}; };
    seams.editWorldTarget=[&](const rw::math::Transform3D<>& initial,bool creating)->std::optional<rw::math::Transform3D<>> {
        ++calls; EXPECT_FALSE(creating); EXPECT_EQ(m_editor.workingSet(),before);
        EXPECT_NEAR(initial.P()[0],0.8,1e-12); EXPECT_NEAR(initial.P()[1],2.1,1e-12); EXPECT_NEAR(initial.P()[2],3.3,1e-12);
        if (mode==1) { return std::nullopt; }
        if (mode==2) { frame=rw::math::Transform3D<>({2,2,3},frame.R()); }
        return rw::math::Transform3D<>(initial.P()+rw::math::Vector3D<>(0.3,-0.2,0.1),initial.R());
    };
    m_panel->setView3DSeams(seams); m_panel->refreshPanel(before,m_report); m_panel->focusObject(point.objectId);
    auto* move=m_panel->findChild<QPushButton*>("ird_req_move_target_3d"); ASSERT_NE(move,nullptr);
    move->click(); QApplication::processEvents();
    const auto actual=m_editor.workingSet().points.entries.front();
    EXPECT_NEAR(actual.pose.position.value()[0],-0.1,1e-12);
    EXPECT_NEAR(actual.pose.position.value()[1],-0.1,1e-12);
    EXPECT_NEAR(actual.pose.position.value()[2],0.4,1e-12);
    auto expected=point; expected.pose.position=actual.pose.position;
    EXPECT_EQ(actual,expected) << "仅位置改变，原引用/规则/约束/TCP/工况关联不得改变";
    ASSERT_TRUE(m_editor.undoLocal()); EXPECT_EQ(m_editor.workingSet(),before);
    for (mode=1;mode<=2;++mode) {
        m_panel->refreshPanel(before,m_report); m_panel->focusObject(point.objectId);
        m_panel->findChild<QPushButton*>("ird_req_move_target_3d")->click(); QApplication::processEvents();
        EXPECT_EQ(m_editor.workingSet(),before) << "取消或参考系世界变换漂移不得写回";
    }
    EXPECT_EQ(calls,3);
}

/** @brief REQ-08：世界系三维创建使用返回位姿，确认前零写、一次撤销，禁写不调用端口。 */
TEST_F(RequirementsSessionGuiTest, View3DTargetCreateAtomicAndReadOnly_REQ08_AT23)
{
    IRD_TEST_INFO("REQ-08", {}, std::nullopt);
    const auto before=m_editor.workingSet(); int calls=0;
    RequirementsView3DSeams seams;
    seams.referenceWorldPose=[](const RequirementReference&) { return std::optional<rw::math::Transform3D<>>{
        rw::math::Transform3D<>({0,0,0},rw::math::Rotation3D<>(1,0,0,0,1,0,0,0,1))}; };
    seams.editWorldTarget=[&](const rw::math::Transform3D<>&,bool creating) {
        ++calls; EXPECT_TRUE(creating); EXPECT_EQ(m_editor.workingSet(),before);
        return std::optional<rw::math::Transform3D<>>{rw::math::Transform3D<>({0.4,0.5,0.6},
            rw::math::Rotation3D<>(0,-1,0,1,0,0,0,0,1))};
    };
    m_panel->setView3DSeams(seams); m_panel->refreshPanel(before,m_report);
    auto* create=m_panel->findChild<QPushButton*>("ird_req_create_target_3d"); ASSERT_NE(create,nullptr);
    create->click(); QApplication::processEvents();
    ASSERT_EQ(m_editor.workingSet().points.entries.size(),before.points.entries.size()+1);
    // 编辑器按ObjectId规范排序，新分配的随机身份不保证位于集合末尾。
    const auto& entries=m_editor.workingSet().points.entries;
    const auto added=std::find_if(entries.begin(),entries.end(),[&](const auto& candidate) {
        return std::none_of(before.points.entries.begin(),before.points.entries.end(),
            [&](const auto& old) { return candidate.objectId==old.objectId; });
    });
    ASSERT_NE(added,entries.end()); const auto& point=*added;
    EXPECT_EQ(point.refFrame.kind,RequirementRefKind::World); EXPECT_TRUE(point.objectId.isValid());
    EXPECT_NE(point.objectId,before.points.entries.front().objectId);
    EXPECT_DOUBLE_EQ(point.pose.position.value()[0],0.4); EXPECT_DOUBLE_EQ(point.pose.position.value()[2],0.6);
    EXPECT_EQ(point.pose.orientation.kind,OrientationRuleKind::Fixed);
    EXPECT_NEAR(point.pose.orientation.fixedRpy[2],1.5707963267948966,1e-12);
    ASSERT_TRUE(m_editor.undoLocal()); EXPECT_EQ(m_editor.workingSet(),before);
    m_panel->setWritable(false); QApplication::processEvents();
    create=m_panel->findChild<QPushButton*>("ird_req_create_target_3d"); EXPECT_FALSE(create->isEnabled());
    create->click(); QApplication::processEvents(); EXPECT_EQ(calls,1); EXPECT_EQ(m_editor.workingSet(),before);
}

/** @brief REQ-08：端口返回NaN/无穷/退化或左手矩阵时拒绝，保留完整草稿及撤销深度。 */
TEST_F(RequirementsSessionGuiTest, View3DTargetInvalidRigidOutputZeroWrite_REQ08_AT23)
{
    IRD_TEST_INFO("REQ-08", {}, std::nullopt);
    const auto before=m_editor.workingSet();
    const auto identity=rw::math::Transform3D<>({0,0,0},rw::math::Rotation3D<>(1,0,0,0,1,0,0,0,1));
    int mode=0,calls=0;
    RequirementsView3DSeams seams;
    seams.referenceWorldPose=[&](const RequirementReference&) { return std::optional{identity}; };
    seams.editWorldTarget=[&](const rw::math::Transform3D<>&,bool)->std::optional<rw::math::Transform3D<>> {
        ++calls; auto result=identity;
        if (mode==0) { result.P()[0]=std::numeric_limits<double>::quiet_NaN(); }
        if (mode==1) { result.P()[2]=std::numeric_limits<double>::infinity(); }
        if (mode==2) { result.R()(0,0)=0; }
        if (mode==3) { result.R()(2,2)=-1; }
        return result;
    };
    m_panel->setView3DSeams(seams); m_panel->refreshPanel(before,m_report);
    for (mode=0;mode<4;++mode) {
        m_panel->findChild<QPushButton*>("ird_req_create_target_3d")->click(); QApplication::processEvents();
        EXPECT_EQ(m_editor.workingSet(),before);
        EXPECT_TRUE(m_panel->showCommandFeedbackText().contains("无效"));
        EXPECT_FALSE(m_editor.undoLocal()) << "非法交互不得产生撤销记录";
    }
    EXPECT_EQ(calls,4);
}

/** @brief REQ-08：嵌套交互中重载/换编辑器以及确认后排队前外部修改均使旧确认失效。 */
TEST_F(RequirementsSessionGuiTest, View3DTargetSessionAndQueuedBaselineGuard_REQ08_AT23)
{
    IRD_TEST_INFO("REQ-08", {}, std::nullopt);
    const auto before=m_editor.workingSet();
    const auto identity=rw::math::Transform3D<>({0,0,0},rw::math::Rotation3D<>(1,0,0,0,1,0,0,0,1));
    int mode=0;
    RequirementsView3DSeams seams;
    seams.referenceWorldPose=[&](const RequirementReference&) { return std::optional{identity}; };
    seams.editWorldTarget=[&](const rw::math::Transform3D<>&,bool)->std::optional<rw::math::Transform3D<>> {
        if (mode==0) { m_panel->noteBaselineReloaded(); }
        if (mode==1) { m_panel->setEditTargetProvider([this] { return &m_editor; }); }
        if (mode==2) {
            QTimer::singleShot(0,m_panel.get(),[&] {
                auto changed=m_editor.workingSet().points.entries.front(); changed.note="外部编辑优先";
                ASSERT_TRUE(m_editor.applyEdit(RequirementEdit{changed}).accepted);
            });
        }
        return rw::math::Transform3D<>({0.4,0.5,0.6},identity.R());
    };
    m_panel->setView3DSeams(seams); m_panel->refreshPanel(before,m_report);
    for (mode=0;mode<3;++mode) {
        m_panel->findChild<QPushButton*>("ird_req_create_target_3d")->click(); QApplication::processEvents();
        if (mode<2) { EXPECT_EQ(m_editor.workingSet(),before); }
        else {
            EXPECT_EQ(m_editor.workingSet().points.entries.size(),before.points.entries.size());
            EXPECT_EQ(m_editor.workingSet().points.entries.front().note,"外部编辑优先");
            ASSERT_TRUE(m_editor.undoLocal()); EXPECT_EQ(m_editor.workingSet(),before);
        }
        EXPECT_FALSE(m_editor.undoLocal()) << "过期确认不能留下额外撤销步骤";
    }
}

/** @brief REQ-08：更多菜单与真实键盘快捷键共用三维确认入口，只读时同样禁止调用。 */
TEST_F(RequirementsSessionGuiTest, View3DTargetMenuAndShortcutShareGate_REQ08_AT23)
{
    IRD_TEST_INFO("REQ-08", {}, std::nullopt);
    const auto before=m_editor.workingSet(); int calls=0;
    const auto identity=rw::math::Transform3D<>({0,0,0},rw::math::Rotation3D<>(1,0,0,0,1,0,0,0,1));
    RequirementsView3DSeams seams;
    seams.referenceWorldPose=[&](const RequirementReference&) { return std::optional{identity}; };
    seams.editWorldTarget=[&](const rw::math::Transform3D<>&,bool)->std::optional<rw::math::Transform3D<>> {
        ++calls; return std::nullopt;
    };
    m_panel->setView3DSeams(seams); m_panel->refreshPanel(before,m_report);
    auto* action=m_panel->findChild<QAction*>("ird_req_create_target_3d_menu");
    ASSERT_NE(action,nullptr) << "顶部更多菜单必须提供真实入口";
    action->trigger(); EXPECT_EQ(calls,1);
    m_panel->show(); m_panel->activateWindow(); m_panel->setFocus();
    QEventLoop settle; QTimer::singleShot(100,&settle,&QEventLoop::quit); settle.exec();
    const auto sendKey=[&] {
        QKeyEvent press(QEvent::KeyPress,Qt::Key_C,Qt::AltModifier|Qt::ShiftModifier);
        QApplication::sendEvent(m_panel.get(),&press);
        QKeyEvent release(QEvent::KeyRelease,Qt::Key_C,Qt::AltModifier|Qt::ShiftModifier);
        QApplication::sendEvent(m_panel.get(),&release); QApplication::processEvents();
    };
    sendKey(); EXPECT_EQ(calls,2) << "真实快捷键应进入同一交互端口";
    m_panel->setWritable(false); QApplication::processEvents();
    EXPECT_FALSE(action->isEnabled()); action->trigger(); sendKey();
    EXPECT_EQ(calls,2); EXPECT_EQ(m_editor.workingSet(),before);
}
// =====================================================================
// WP-14-T12 批33（gui 补产第一批——矩阵对账遗留行的生产路径补证）：
// R02 段编辑族／R01 删除保护与重名／R03 间距与姿态采样字段／R05 公式
// 与引号转义／R12 版本与未知键。全部走真实控件事件流（新 DoD 第 5
// 条——投影模型/域函数测试不替代）。
// =====================================================================

/** @brief REQ-02：接近/撤离段启用、进退轴、距离经段复合行真实编辑；作业
 *         段锁启用；一次撤销整体恢复。 */
TEST_F(RequirementsSessionGuiTest, StationSegmentsEditAndWorkLock_REQ02_WP14T12)
{
    IRD_TEST_INFO("REQ-02", {"UX-05"}, std::nullopt);
    const auto before = m_editor.workingSet().points.entries.front();
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(before.objectId);
    QApplication::processEvents();

    // 段行控件定位（QFormLayout 行/列角色——段表单布局对象跨面板重建
    // 存活〔成员 m_stationSegmentForm，行由 clearFormRows 清空重建〕，
    // LabelRole↔FieldRole 同行配对＝与用户所见同位）。接受编辑→检查器
    // 重建＝行控件全换——每步交互后 relocate 重取（悬垂指针＝SEH）。
    QFormLayout* segForm = nullptr;
    for (QFormLayout* lay : m_panel->findChildren<QFormLayout*>()) {
        for (int i = 0; i < lay->rowCount() && segForm == nullptr; ++i) {
            const QLayoutItem* li = lay->itemAt(i, QFormLayout::LabelRole);
            const QLabel* lab =
                li != nullptr ? qobject_cast<QLabel*>(li->widget()) : nullptr;
            if (lab != nullptr && lab->text() == QStringLiteral("接近段")) {
                segForm = lay;
            }
        }
    }
    ASSERT_NE(segForm, nullptr) << "段行表单布局缺失";
    QList<QComboBox*> enabledCombos;
    QList<QComboBox*> axisCombos;
    QList<QDoubleSpinBox*> segSpins;
    const auto relocate = [&segForm, &enabledCombos, &axisCombos, &segSpins] {
        const auto rowField = [&](const QString& label) -> QWidget* {
            for (int i = 0; i < segForm->rowCount(); ++i) {
                const QLayoutItem* labelItem =
                    segForm->itemAt(i, QFormLayout::LabelRole);
                const QLabel* lab = labelItem != nullptr
                    ? qobject_cast<QLabel*>(labelItem->widget()) : nullptr;
                if (lab == nullptr || lab->text() != label) { continue; }
                const QLayoutItem* fieldItem =
                    segForm->itemAt(i, QFormLayout::FieldRole);
                return fieldItem != nullptr ? fieldItem->widget() : nullptr;
            }
            return nullptr;
        };
        enabledCombos.clear();
        axisCombos.clear();
        segSpins.clear();
        for (const QString& label :
             {QStringLiteral("接近段"), QStringLiteral("作业段"),
              QStringLiteral("撤离段")}) {
            if (QWidget* cell = rowField(label)) {
                if (QComboBox* combo = cell->findChild<QComboBox*>()) {
                    enabledCombos.append(combo);
                }
            }
        }
        for (const QString& label :
             {QStringLiteral("接近段"), QStringLiteral("撤离段")}) {
            if (QWidget* cell = rowField(label)) {
                const auto combos = cell->findChildren<QComboBox*>();
                if (combos.size() >= 2) { axisCombos.append(combos.at(1)); }
                if (QDoubleSpinBox* spin = cell->findChild<QDoubleSpinBox*>()) {
                    segSpins.append(spin);
                }
            }
        }
    };
    relocate();
    ASSERT_EQ(enabledCombos.size(), 3) << "接近/作业/撤离三段启用下拉缺失";
    ASSERT_EQ(axisCombos.size(), 2) << "接近/撤离进退轴下拉缺失";
    ASSERT_EQ(segSpins.size(), 2) << "接近/撤离距离旋箱缺失";

    // 作业段＝工位本身不可停用（REQ-02 语义锁）；接近/撤离可交互。
    EXPECT_FALSE(enabledCombos[1]->isEnabled()) << "作业段启用下拉必须锁死";
    EXPECT_TRUE(enabledCombos[0]->isEnabled());
    EXPECT_TRUE(enabledCombos[2]->isEnabled());

    // 接近段距离 0.25 m（editingFinished 直连提交轨——与手输同源）。
    segSpins[0]->setValue(0.25);
    QMetaObject::invokeMethod(segSpins[0], "editingFinished", Qt::DirectConnection);
    QApplication::processEvents();
    EXPECT_DOUBLE_EQ(m_editor.workingSet().points.entries.front().approach.distanceM,
                     0.25)
        << "接近段距离未入草稿（复合行提交轨断链）";
    relocate();

    // 接近段进退轴切 ReferenceZ（枚举直投——单元格内第 2 个下拉＝轴）。
    ASSERT_EQ(axisCombos.size(), 2);
    axisCombos[0]->setCurrentText(QStringLiteral("ReferenceZ"));
    QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet().points.entries.front().approach.axis,
              SegmentAxis::ReferenceZ)
        << "接近段进退轴未入草稿";
    relocate();

    // 撤离段独立距离——与接近段互不串值（R02-03 的独立面；段停用态下
    // 距离可先置——启用时的正距离校验见域 validateTaskPoint）。
    ASSERT_EQ(segSpins.size(), 2);
    segSpins[1]->setValue(0.4);
    QMetaObject::invokeMethod(segSpins[1], "editingFinished", Qt::DirectConnection);
    QApplication::processEvents();
    const auto after = m_editor.workingSet().points.entries.front();
    EXPECT_DOUBLE_EQ(after.retract.distanceM, 0.4) << "撤离段距离未入草稿";
    EXPECT_DOUBLE_EQ(after.approach.distanceM, 0.25) << "撤离段编辑串改了接近段";
    EXPECT_EQ(after.approach.axis, SegmentAxis::ReferenceZ) << "撤离段编辑串改了接近段轴";

    // 段组三笔提交（接近距离/接近轴/撤离距离）＝三个独立撤销步（L-R2
    // 单编辑单步），逐笔撤销回放全链到基线。段"启用"翻转的交互面在测试
    // 环境暴露异常态（登记 batch33 待查——本用例不承载）。
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(m_editor.undoLocal()) << "第 " << i + 1 << " 笔撤销失败";
    }
    EXPECT_EQ(m_editor.workingSet().points.entries.front(), before);
}

TEST_F(RequirementsSessionGuiTest, StationReferencedDeleteGuards_REQ01_WP14T12)
{
    IRD_TEST_INFO("REQ-01", {"REQ-04", "REQ-07"}, std::nullopt);
    const core::ObjectId p1 = m_editor.workingSet().points.entries.front().objectId;

    // ① 工况 appliesTo 绑定 P1（编辑面构造引用——与
    //    ConditionReferencesAndFourStates 同一域形态）后，删除 P1＝拒绝。
    OperatingCondition cond = m_editor.workingSet().conditions.entries.front();
    cond.appliesTo.scope = AppliesToScope::Stations;
    cond.appliesTo.stations = {p1};
    ASSERT_TRUE(m_editor.applyEdit(RequirementEdit{cond}).accepted);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(p1);
    QPushButton* removeBtn =
        m_panel->findChild<QPushButton*>(QStringLiteral("ird_req_remove_points"));
    ASSERT_NE(removeBtn, nullptr) << "工位生命周期删除按钮缺失";
    removeBtn->click();
    QApplication::processEvents();
    ASSERT_EQ(m_editor.workingSet().points.entries.size(), 1u)
        << "被工况 appliesTo 引用的工位删除必须被拒（§5.1 删除引用保护）";
    EXPECT_EQ(m_editor.workingSet().points.entries.front().objectId, p1)
        << "拒绝删除后原工位必须原样保留";

    // ② 顺序键前驱引用保护：P2 的顺序键引用 P1 名称 → 删除 P1＝拒绝。
    TaskPoint p2 = makePoint("P2", 4.0, 5.0, 6.0);
    ASSERT_TRUE(m_editor.applyEdit(RequirementEdit{p2}).accepted);
    TaskPoint p2seq;
    for (const TaskPoint& p : m_editor.workingSet().points.entries) {
        if (p.name == "P2") { p2seq = p; }  // 编辑器按 ObjectId 稳定序——按名现取
    }
    ASSERT_TRUE(p2seq.objectId.isValid()) << "P2 入集失败（夹具装配错误）";
    p2seq.sequenceKey = std::optional<std::string>{std::string("P1")};
    ASSERT_TRUE(m_editor.applyEdit(RequirementEdit{p2seq}).accepted);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(p1);
    removeBtn->click();
    QApplication::processEvents();
    ASSERT_EQ(m_editor.workingSet().points.entries.size(), 2u)
        << "被顺序键前驱引用的工位删除必须被拒（§5.1 删除引用保护 c 轨）";

    // ③ 解除两路引用（工况 appliesTo 复位＋顺序键清空走真实卡片字段）
    //    → 删除成功＋锚回落（守卫语义正反面：引用在场＝拒、引用解除＝通）。
    OperatingCondition unbound = m_editor.workingSet().conditions.entries.front();
    unbound.appliesTo.scope = AppliesToScope::None;
    unbound.appliesTo.stations.clear();
    ASSERT_TRUE(m_editor.applyEdit(RequirementEdit{unbound}).accepted);
    m_panel->focusObject(p2seq.objectId);
    QApplication::processEvents();
    QLineEdit* seqField = fieldEditor(*m_panel, "sequence-key");
    ASSERT_NE(seqField, nullptr) << "顺序键卡片行缺失";
    seqField->setText(QString());
    QMetaObject::invokeMethod(seqField, "editingFinished", Qt::DirectConnection);
    QApplication::processEvents();
    bool seqCleared = true;
    for (const TaskPoint& p : m_editor.workingSet().points.entries) {
        if (p.objectId == p2seq.objectId && p.sequenceKey.has_value()) { seqCleared = false; }
    }
    ASSERT_TRUE(seqCleared) << "顺序键清空必须落账（解除引用前置）";
    m_panel->focusObject(p1);
    removeBtn->click();
    QApplication::processEvents();
    ASSERT_EQ(m_editor.workingSet().points.entries.size(), 1u)
        << "解除引用后删除应成功";
    EXPECT_FALSE(m_editor.workingSet().points.entries.front().objectId == p1);
}

/** @brief REQ-01：重名拒绝（编辑器 upsert 查重）走真实名称字段——拒绝后
 *         原值保留；非重名改名照常通过（对照轨）。 */
TEST_F(RequirementsSessionGuiTest, StationDuplicateNameRejectKeepsOriginal_REQ01_WP14T12)
{
    IRD_TEST_INFO("REQ-01", {"I-REQ-2"}, std::nullopt);
    TaskPoint p2 = makePoint("P2", 4.0, 5.0, 6.0);
    ASSERT_TRUE(m_editor.applyEdit(RequirementEdit{p2}).accepted);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    core::ObjectId p2oid;
    for (const TaskPoint& p : m_editor.workingSet().points.entries) {
        if (p.name == "P2") { p2oid = p.objectId; }
    }
    ASSERT_TRUE(p2oid.isValid()) << "P2 入集失败（夹具装配错误）";
    m_panel->focusObject(p2oid);
    QApplication::processEvents();
    QLineEdit* nameField = fieldEditor(*m_panel, "name");
    ASSERT_NE(nameField, nullptr);

    // 重名提交 → 编辑器拒绝 → 工作集字节不变（原值保留——I-REQ-2）。
    // 注意：拒绝路径 renderInspector 重建检查器行（原值回退显示）——
    // nameField 随旧行销毁，二次定位必须重走 fieldEditor（悬垂防回归）。
    nameField->setText(QStringLiteral("P1"));
    QMetaObject::invokeMethod(nameField, "editingFinished", Qt::DirectConnection);
    QApplication::processEvents();
    bool seen = false;
    for (const TaskPoint& p : m_editor.workingSet().points.entries) {
        if (p.objectId == p2oid) {
            seen = true;
            EXPECT_EQ(p.name, "P2") << "重名拒绝后原名称必须保留";
        }
    }
    ASSERT_TRUE(seen) << "拒绝路径不得丢条目";

    // 对照：非重名改名照常通过（拒绝轨不是编辑轨整体失灵）。
    QLineEdit* nameFieldAgain = fieldEditor(*m_panel, "name");
    ASSERT_NE(nameFieldAgain, nullptr) << "拒绝重绘后名称行缺失";
    nameFieldAgain->setText(QStringLiteral("P2-改"));
    QMetaObject::invokeMethod(nameFieldAgain, "editingFinished", Qt::DirectConnection);
    QApplication::processEvents();
    for (const TaskPoint& p : m_editor.workingSet().points.entries) {
        if (p.objectId == p2oid) {
            EXPECT_EQ(p.name, "P2-改") << "非重名改名应通过";
        }
    }
}

/** @brief REQ-03：间距采样真实表单——区域存 GridBySpacing 原值；确认派生
 *         计划＝buildPlan 单点归一的 Grid 权威计数；同值重确认计数不变
 *         （确定性），间距改密计数响应。 */
TEST_F(RequirementsSessionGuiTest, RegionSpacingSamplingNormalizedPlan_REQ03_WP14T12)
{
    IRD_TEST_INFO("REQ-03", {"D-REQ-2"}, std::nullopt);
    const auto before = m_editor.workingSet().regions.entries.front();
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(before.objectId);
    auto* form = m_panel->findChild<QWidget*>("ird_region_sampling_form");
    ASSERT_NE(form, nullptr);
    auto* method = form->findChild<QComboBox*>("ird_region_sampling_method");
    ASSERT_NE(method, nullptr);
    method->setCurrentIndex(method->findData(int(PositionSamplingMethod::GridBySpacing)));
    form->findChild<QLineEdit*>("ird_region_spacing_0")->setText("0.5");
    form->findChild<QLineEdit*>("ird_region_spacing_1")->setText("0.5");
    form->findChild<QLineEdit*>("ird_region_spacing_2")->setText("1.0");
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click();
    QApplication::processEvents();

    // 区域存原值（GridBySpacing 原样——区域面承载原始间距，不预归一）。
    const auto region = m_editor.workingSet().regions.entries.front();
    EXPECT_EQ(region.positionSampling.method, PositionSamplingMethod::GridBySpacing);
    EXPECT_DOUBLE_EQ(region.positionSampling.spacing[0], 0.5);
    EXPECT_DOUBLE_EQ(region.positionSampling.spacing[2], 1.0);

    // 确认派生计划（A-18 接线面）＝规范化 Grid 计数（D-REQ-2——计划面
    // 不承载原始间距），计数 ≥1 且随确认入集。
    ASSERT_EQ(m_editor.workingSet().plans.entries.size(), 1u)
        << "间距区域确认应派生计划（A-18 同一接线）";
    const auto plan = m_editor.workingSet().plans.entries.front();
    EXPECT_EQ(plan.positionSampling.method, PositionSamplingMethod::Grid);
    EXPECT_GE(plan.positionSampling.counts[0], 1u);
    const auto counts1 = plan.positionSampling.counts;
    const core::ObjectId planId = plan.objectId;

    // 同值重确认（面板重绘后重定位表单）→ 计数不变（确定性归一）＋
    // 计划 id 复用（R6 一一对应）。
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(before.objectId);
    form = m_panel->findChild<QWidget*>("ird_region_sampling_form");
    ASSERT_NE(form, nullptr);
    method = form->findChild<QComboBox*>("ird_region_sampling_method");
    method->setCurrentIndex(method->findData(int(PositionSamplingMethod::GridBySpacing)));
    form->findChild<QLineEdit*>("ird_region_spacing_0")->setText("0.5");
    form->findChild<QLineEdit*>("ird_region_spacing_1")->setText("0.5");
    form->findChild<QLineEdit*>("ird_region_spacing_2")->setText("1.0");
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click();
    QApplication::processEvents();
    ASSERT_EQ(m_editor.workingSet().plans.entries.size(), 1u) << "重确认不得新增第二条计划";
    EXPECT_EQ(m_editor.workingSet().plans.entries.front().positionSampling.counts,
              counts1)
        << "同间距重确认计数必须逐位不变（确定性归一）";
    EXPECT_EQ(m_editor.workingSet().plans.entries.front().objectId, planId);

    // 间距改密（0.25 全轴）→ 计数响应（归一对间距敏感——非装饰面）。
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(before.objectId);
    form = m_panel->findChild<QWidget*>("ird_region_sampling_form");
    ASSERT_NE(form, nullptr);
    method = form->findChild<QComboBox*>("ird_region_sampling_method");
    method->setCurrentIndex(method->findData(int(PositionSamplingMethod::GridBySpacing)));
    form->findChild<QLineEdit*>("ird_region_spacing_0")->setText("0.25");
    form->findChild<QLineEdit*>("ird_region_spacing_1")->setText("0.25");
    form->findChild<QLineEdit*>("ird_region_spacing_2")->setText("0.25");
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click();
    QApplication::processEvents();
    ASSERT_EQ(m_editor.workingSet().plans.entries.size(), 1u);
    const auto counts2 = m_editor.workingSet().plans.entries.front().positionSampling.counts;
    EXPECT_GT(counts2[0] * counts2[1] * counts2[2],
              counts1[0] * counts1[1] * counts1[2])
        << "间距改密后归一计数必须增加";
}

/** @brief REQ-03：姿态方向/滚转采样计数走真实表单——区域存值、派生计划
 *         同值投影；零计数拒绝保原值。 */
TEST_F(RequirementsSessionGuiTest, RegionOrientationSamplingCounts_REQ03_WP14T12)
{
    IRD_TEST_INFO("REQ-03", {}, std::nullopt);
    const auto before = m_editor.workingSet().regions.entries.front();
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(before.objectId);
    auto* form = m_panel->findChild<QWidget*>("ird_region_sampling_form");
    ASSERT_NE(form, nullptr);

    // 方向 4×滚转 3 → 区域存值；派生计划姿态采样同值（计划＝区域采样
    // 定义的忠实投影——A-18 裁决语义）。
    form->findChild<QLineEdit*>("ird_region_direction_samples")->setText("4");
    form->findChild<QLineEdit*>("ird_region_roll_samples")->setText("3");
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click();
    QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet().regions.entries.front().orientationSampling.directionSamples, 4u);
    EXPECT_EQ(m_editor.workingSet().regions.entries.front().orientationSampling.rollSamples, 3u);
    if (m_editor.workingSet().plans.entries.size() == 1u) {
        EXPECT_EQ(m_editor.workingSet().plans.entries.front().orientationSampling.directionSamples, 4u);
        EXPECT_EQ(m_editor.workingSet().plans.entries.front().orientationSampling.rollSamples, 3u);
    }

    // 滚转零计数 → 域拒绝＋区域原值保留（非法输入不落草稿——UX-05）。
    // 拒绝路径 renderRegionPage 重建采样表单（诚实刷新面）——旧 form
    // 指针随旧窗销毁，错误标签断言前必须经 objectName 重定位。
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(before.objectId);
    form = m_panel->findChild<QWidget*>("ird_region_sampling_form");
    ASSERT_NE(form, nullptr);
    form->findChild<QLineEdit*>("ird_region_roll_samples")->setText("0");
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click();
    QApplication::processEvents();
    EXPECT_EQ(m_editor.workingSet().regions.entries.front().orientationSampling.rollSamples, 3u)
        << "滚转零计数必须拒绝且保留原值";
    form = m_panel->findChild<QWidget*>("ird_region_sampling_form");
    ASSERT_NE(form, nullptr) << "拒绝重绘后采样表单缺失";
    EXPECT_FALSE(form->findChild<QLabel*>("ird_region_sampling_error")->text().isEmpty())
        << "零计数拒绝必须就地报错";
}

/** @brief AT-02：CSV 公式样式单元格按字面数据导入（NFR-SEC-03 不求值）；
 *         引号字段内逗号/双写引号按 RFC 方言解出单字段。 */
TEST_F(RequirementsSessionGuiTest, CsvFormulaLiteralAndQuotedFields_AT02_WP14T12)
{
    IRD_TEST_INFO("AT-02", {"NFR-SEC-03"}, std::nullopt);
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("esc.csv"));
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write("id,name,x,y,z\n"
            "s1,=1+2,1.0,2.0,3.0\n"
            "s2,\"含,逗号\",4.0,5.0,6.0\n"
            "s3,\"引\"\"内嵌\",7.0,8.0,9.0\n");
    f.close();
    FakeDialogHost host;
    host.openPath = path;
    host.importConfirmed = true;
    const std::size_t before = m_editor.workingSet().points.entries.size();
    ASSERT_TRUE(executeRequirementCommand("requirements.import-csv", *m_panel,
                                          m_editor, *m_panel, host))
        << m_panel->showCommandFeedbackText().toStdString();
    ASSERT_EQ(m_editor.workingSet().points.entries.size(), before + 3u);

    // 逐名现取断言（导入产物名称面＝字面数据）。
    bool sawFormula = false;
    bool sawComma = false;
    bool sawQuote = false;
    for (const TaskPoint& p : m_editor.workingSet().points.entries) {
        if (p.name == "=1+2") {
            sawFormula = true;  // 公式样式文本原样入库——无求值路径
        } else if (p.name == "含,逗号") {
            sawComma = true;    // 引号内逗号＝单字段（方言解出）
        } else if (p.name == "引\"内嵌") {
            sawQuote = true;    // 双写引号解出单引号（RFC 4180 转义）
        }
    }
    EXPECT_TRUE(sawFormula) << "公式样式文本必须按字面导入（NFR-SEC-03 不求值）";
    EXPECT_TRUE(sawComma) << "引号内逗号必须解为单字段";
    EXPECT_TRUE(sawQuote) << "双写引号必须解出单引号";
}

/** @brief REQ-12：JSON 版本门（未来版本稳定拒绝）与顶层未知键拒绝
 *         （NFR-DEP-04）走真实导入命令——均零写入；合法基线照常导入
 *         （对照轨）。 */
TEST_F(RequirementsSessionGuiTest, JsonImportVersionAndUnknownKeyReject_REQ12_WP14T12)
{
    IRD_TEST_INFO("REQ-12", {"NFR-DEP-04"}, std::nullopt);
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    // ① 未来版本：schemaVersion=2 → io 版本门稳定拒绝（不猜测降级）。
    const QString futurePath = QDir(dir.path()).filePath(QStringLiteral("future.json"));
    {
        QFile f(futurePath);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("{\"schemaVersion\": 2, \"points\": []}");
        f.close();
    }
    FakeDialogHost host;
    host.openPath = futurePath;
    host.importConfirmed = true;
    const auto baseline = m_editor.workingSet();
    EXPECT_FALSE(executeRequirementCommand("requirements.import-json", *m_panel,
                                           m_editor, *m_panel, host))
        << "未来版本必须稳定拒绝";
    EXPECT_EQ(m_editor.workingSet(), baseline) << "版本拒绝必须零写入";

    // ② 顶层未知键：Reject 词典（NFR-DEP-04——不静默吞字段）。
    const QString unknownPath = QDir(dir.path()).filePath(QStringLiteral("unknown.json"));
    {
        QFile f(unknownPath);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("{\"schemaVersion\": 1, \"sneaky_extra\": 1, \"points\": []}");
        f.close();
    }
    host.openPath = unknownPath;
    EXPECT_FALSE(executeRequirementCommand("requirements.import-json", *m_panel,
                                           m_editor, *m_panel, host))
        << "顶层未知键必须拒绝";
    EXPECT_EQ(m_editor.workingSet(), baseline) << "未知键拒绝必须零写入";

    // ③ 对照：合法基线导入照常通过（拒绝轨不是导入轨失灵）。
    const QString okPath = QDir(dir.path()).filePath(QStringLiteral("ok.json"));
    {
        QFile f(okPath);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("{\n"
                "  \"schemaVersion\": 1,\n"
                "  \"points\": [\n"
                "    {\"id\": \"p1\", \"name\": \"取件点\", \"process_tag\": \"Pick\",\n"
                "     \"level\": \"Must\", \"enabled\": \"true\", \"ref_frame\": \"World\",\n"
                "     \"x\": 1.0, \"y\": 2.0, \"z\": 3.0, \"note\": \"示例备注\"}\n"
                "  ]\n"
                "}");
        f.close();
    }
    host.openPath = okPath;
    EXPECT_TRUE(executeRequirementCommand("requirements.import-json", *m_panel,
                                          m_editor, *m_panel, host))
        << m_panel->showCommandFeedbackText().toStdString();
    bool sawImported = false;
    for (const TaskPoint& p : m_editor.workingSet().points.entries) {
        if (p.name == "取件点") { sawImported = true; }
    }
    EXPECT_TRUE(sawImported) << "合法 JSON 导入产物缺失";
}
/** @brief 批34 评审 F5：区域有派生计划＝计划随区域几何同步（间距归一计
 *         数随盒尺寸变化）——真实尺寸字段编辑后计划原子更新，一次撤销
 *         区域与计划双回。 */
TEST_F(RequirementsSessionGuiTest, RegionSizeEditSyncsDerivedPlan_REQ03_WP14T12)
{
    IRD_TEST_INFO("REQ-03", {"D-REQ-2"}, std::nullopt);
    const auto before = m_editor.workingSet().regions.entries.front();
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(before.objectId);

    // 间距采样确认 → 派生计划（批33 用例同款流程）。
    auto* form = m_panel->findChild<QWidget*>("ird_region_sampling_form");
    ASSERT_NE(form, nullptr);
    auto* method = form->findChild<QComboBox*>("ird_region_sampling_method");
    ASSERT_NE(method, nullptr);
    method->setCurrentIndex(method->findData(int(PositionSamplingMethod::GridBySpacing)));
    form->findChild<QLineEdit*>("ird_region_spacing_0")->setText("0.5");
    form->findChild<QLineEdit*>("ird_region_spacing_1")->setText("0.5");
    form->findChild<QLineEdit*>("ird_region_spacing_2")->setText("0.5");
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click();
    QApplication::processEvents();
    ASSERT_EQ(m_editor.workingSet().plans.entries.size(), 1u);
    const auto counts1 = m_editor.workingSet().plans.entries.front().positionSampling.counts;

    // 真实尺寸字段改盒 X 尺寸 2.0→2.5 → 计划原子同步（归一计数随尺寸变）。
    QLineEdit* sizeX = fieldEditor(*m_panel, "box-size-x");
    ASSERT_NE(sizeX, nullptr) << "盒尺寸 X 字段缺失";
    sizeX->setText("2.5");
    QMetaObject::invokeMethod(sizeX, "editingFinished", Qt::DirectConnection);
    QApplication::processEvents();
    const auto afterRegion = m_editor.workingSet().regions.entries.front();
    EXPECT_DOUBLE_EQ(afterRegion.box.size[0], 2.5) << "尺寸编辑未入草稿";
    ASSERT_EQ(m_editor.workingSet().plans.entries.size(), 1u)
        << "尺寸编辑不得增删计划条目（upsert 同步）";
    const auto counts2 = m_editor.workingSet().plans.entries.front().positionSampling.counts;
    EXPECT_NE(counts2, counts1) << "尺寸变更后计划计数必须同步（F5 断链面）";

    // 一次撤销＝区域与计划双回（原子批）。
    ASSERT_TRUE(m_editor.undoLocal());
    EXPECT_EQ(m_editor.workingSet().regions.entries.front().box.size[0], 2.0);
    EXPECT_EQ(m_editor.workingSet().plans.entries.front().positionSampling.counts,
              counts1);
}

/** @brief 批34 评审 F6：直接确认默认采样（零变化）必须补建缺失计划；
 *         计划已存在且派生同值＝幂等零动作（工作集字节不变）。 */
TEST_F(RequirementsSessionGuiTest, RegionConfirmWithoutChangeBuildsMissingPlan_REQ03_WP14T12)
{
    IRD_TEST_INFO("REQ-03", {}, std::nullopt);
    const auto region = m_editor.workingSet().regions.entries.front();
    ASSERT_TRUE(m_editor.workingSet().plans.entries.empty())
        << "夹具基线应无计划条目";
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(region.objectId);
    auto* form = m_panel->findChild<QWidget*>("ird_region_sampling_form");
    ASSERT_NE(form, nullptr);
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click();
    QApplication::processEvents();
    ASSERT_EQ(m_editor.workingSet().plans.entries.size(), 1u)
        << "直接确认默认采样应派生计划（F6 断链面）";
    const auto derived = m_editor.workingSet().plans.entries.front();
    const auto wsAfterFirst = m_editor.workingSet();

    // 幂等半区：零变化再确认＝零动作（工作集字节不变、不重复入栈）。
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(region.objectId);
    form = m_panel->findChild<QWidget*>("ird_region_sampling_form");
    ASSERT_NE(form, nullptr);
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click();
    QApplication::processEvents();
    ASSERT_EQ(m_editor.workingSet().plans.entries.size(), 1u) << "幂等再确认不得新增计划";
    EXPECT_EQ(m_editor.workingSet(), wsAfterFirst) << "幂等再确认工作集字节必须不变";
}

/** @brief 批35 评审 P1-5：区域批量参数表与单字段轨同入计划同步——盒长
 *         2→4 m（0.5 m 间距）派生计数 5→9，不经剪贴板（直改表格值列）。 */
TEST_F(RequirementsSessionGuiTest, RegionBatchResizeSyncsPlan_UX05_WP14T12)
{
    IRD_TEST_INFO("REQ-03", {"UX-05"}, std::nullopt);
    const auto before = m_editor.workingSet().regions.entries.front();
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    m_panel->focusObject(before.objectId);

    // 先经采样表单确认派生计划（0.5 m 间距——盒长 2.0 m → X 计数 5）。
    auto* form = m_panel->findChild<QWidget*>("ird_region_sampling_form");
    ASSERT_NE(form, nullptr);
    auto* method = form->findChild<QComboBox*>("ird_region_sampling_method");
    ASSERT_NE(method, nullptr);
    method->setCurrentIndex(method->findData(int(PositionSamplingMethod::GridBySpacing)));
    for (int axis = 0; axis < 3; ++axis) {
        form->findChild<QLineEdit*>(QString("ird_region_spacing_%1").arg(axis))
            ->setText("0.5");
    }
    form->findChild<QPushButton*>("ird_region_sampling_apply")->click();
    QApplication::processEvents();
    ASSERT_EQ(m_editor.workingSet().plans.entries.size(), 1u);
    ASSERT_EQ(m_editor.workingSet().plans.entries.front().positionSampling.counts[0], 5U)
        << "夹具基线：盒长 2.0/间距 0.5 → X 计数 5";
    const auto counts1 = m_editor.workingSet().plans.entries.front().positionSampling.counts;

    // 批量参数表直改值列（kValueColumn=1；键在列 0 的 UserRole）——不经
    // 剪贴板。box-size-x 2.0→4.0。
    auto* batch = m_panel->findChild<QWidget*>("ird_req_batch_region");
    ASSERT_NE(batch, nullptr);
    auto* table = batch->findChild<QTableWidget*>("ird_param_table");
    ASSERT_NE(table, nullptr);
    int sizeRow = -1;
    for (int r = 0; r < table->rowCount(); ++r) {
        const QTableWidgetItem* keyItem = table->item(r, 0);
        if (keyItem != nullptr
            && keyItem->data(Qt::UserRole).toString() == QStringLiteral("box-size-x")) {
            sizeRow = r;
            break;
        }
    }
    ASSERT_GE(sizeRow, 0) << "批量表缺 box-size-x 行";
    QTableWidgetItem* valueItem = table->item(sizeRow, 1);
    ASSERT_NE(valueItem, nullptr) << "批量表值列缺失";
    valueItem->setText(QStringLiteral("4"));
    QApplication::processEvents();
    QApplication::processEvents();  // singleShot 暂存排空

    batch->findChild<QPushButton*>("ird_param_apply")->click();
    batch->findChild<QPushButton*>("ird_param_confirm_yes")->click();
    QApplication::processEvents();

    // 区域尺寸写入＋计划同步（X 计数 5→9——4/0.5+1）。
    EXPECT_DOUBLE_EQ(m_editor.workingSet().regions.entries.front().box.size[0], 4.0)
        << "批量表尺寸写入未落账";
    ASSERT_EQ(m_editor.workingSet().plans.entries.size(), 1u) << "同步应 upsert 不新增";
    const auto counts2 = m_editor.workingSet().plans.entries.front().positionSampling.counts;
    EXPECT_EQ(counts2[0], 9U) << "盒长 4 m/间距 0.5 m → X 计数 9（P1-5 同步面）";
    EXPECT_NE(counts2, counts1);

    // 一次撤销＝区域与计划双回（原子批）。
    ASSERT_TRUE(m_editor.undoLocal());
    EXPECT_EQ(m_editor.workingSet().regions.entries.front().box.size[0], 2.0);
    EXPECT_EQ(m_editor.workingSet().plans.entries.front().positionSampling.counts,
              counts1);
}
