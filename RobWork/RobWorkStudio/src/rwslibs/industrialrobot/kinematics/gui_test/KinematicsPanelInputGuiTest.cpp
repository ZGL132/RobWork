/**
 * @file   KinematicsPanelInputGuiTest.cpp
 * @brief  运动学面板目标输入的 GUI 回归（KIN-P1——审查条目 A07：显示
 *         单位与 SI 真值分离——标签跟随当前单位制、提交按所选单位逆换
 *         算回 SI；会话/求解器侧永远只见 SI 真值）。
 *
 * 设计依据：
 *   - units/kinematics.md §9.8（KIN-12 显示单位纯投影——读侧换算不改
 *     SI 真值；L-K8 单位切换全面板重投影）；§5.3（IkRequest.targetInBase
 *     ——基座系 SI 目标，求解器只见 SI）；
 *   - 审查报告 audit/kin-current/运动学模块重新审查-20261016.md §3 A07
 *     的反例数据（切 mm 后标签仍标 m；输入 0.1 被按 ×1000 提交成
 *     100 m——按任一解释均非法）与探针写法（真实 widget＋注入记录
 *     求解器＋QMetaObject 直触槽函数——本文件即该探针的产品测试化）；
 *   - 夹具：KinFkFixture 六轴模型＋TestView（kinematics/test——同单元
 *     测试夹具消费，header-only）。
 *
 * 用例与验收映射（审查报告 §5 KIN-P1 行）：
 *   - 标签一致：mm 制式下三个目标轴标签携带 "mm" 且携带基座系标识；
 *     缺省（SI 直显）下标注 "m"——修复前标签恒为 "（m）"（缺陷面）；
 *   - SI 一致：m/cm/mm 三种制式输入"同一物理目标 0.1 m"（各制式的
 *     正确显示值 0.1/10/100），提交给求解器的 targetInBase 均为
 *     0.1 m——修复前 mm 制式输入 0.1 被正向投影成 100 m（反例形态）；
 *   - 逆换算方向：mm 制式输入 "0.1"（＝0.1 mm＝1e-4 m）提交 1e-4 m，
 *     绝非 100 m（A07 反例数值钉扎）。
 *
 * 线程约束：全部用例在 main 线程（QApplication 线程）执行——widget 与
 * 槽函数直调同线程（UI 纪律的测试镜像）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯

#include <QComboBox>
#include <QEventLoop>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPushButton>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <rw/math/Constants.hpp>  // rw::math::Pi/Deg2Rad（期望角换算——库权威常量）
#include <sdurws/ird/kinematics/Ik.hpp>  // IIkSolver/IkRequest（记录求解器）
#include "KinFkFixture.hpp"              // TestView/sixAxisModel（同单元夹具）
#include "plugin/KinematicsPanelWidget.hpp"  // 被测面板（真实 QWidget）
#include "plugin/KinPanelRotation.hpp"       // F-919：期望旋转构建（独立冒烟可链接，等价钉见 KinP4FlowsTest）
#include "plugin/KinPanelTypes.hpp"          // KinPanelServices/会话态

// ---- 使用声明（与被测代码同一命名空间——用例可读性）--------------------
using namespace sdurws::ird;
using namespace sdurws::ird::kinematics;
using namespace sdurws::ird::kinematics::testfixture;

namespace {

// =====================================================================
// 记录求解器替身（审查 A07 探针同款——只捕获请求，零求解语义）
// =====================================================================

/**
 * @brief 仅记录真实控件事件提交的请求（注入 m_services.ikSolver——
 *        隔离检查"输入→请求"数据流；不替代数值求解器正确性测试）。
 */
class WidgetRecordingSolver final : public IIkSolver {
public:
    mutable std::optional<IkRequest> received;  ///< 最近一次收到的请求
    IkOutcome solve(const IkRequest& request) const override
    {
        received = request;
        return IkOutcome{};
    }
};

// =====================================================================
// 面板夹具（六轴模型＋记录求解器——事件探针的最小装配）
// =====================================================================

/// 面板夹具：持有视图/求解器/会话态/面板的存活期（探针用例值语义装配
/// ——与审查探针同构；面板构造即全量初刷，标签随注入制式重建）。
struct PanelHarness {
    TestView view{sixAxisModel()};
    WidgetRecordingSolver solver;
    KinPanelServices services;
    KinModuleSessionState state;
    std::unique_ptr<KinematicsPanelWidget> panel;

    PanelHarness()
    {
        services.modelView = &view;
        services.ikSolver = &solver;
        // 求解配置基线（I-KIN-4 seed≥1——与产品装配纪律同则）。
        state.savedConfig.seed = 1;
        state.savedConfig.regionBudget.seed = 1;
        panel = std::make_unique<KinematicsPanelWidget>(services, state);
    }

    /// 目标输入六编辑（★ KIN-R2 任务4：位置 XYZ＋姿态 RPY——按 objectName
    /// 确定性取值，序＝X/Y/Z/Roll/Pitch/Yaw；findChildren 顺序不依赖）。
    /// 缺件时返回值短于 6——调用方 ASSERT_EQ(edits.size(), 6U) 即红
    /// （gtest 断言不可用于非 void 辅助函数，红证由调用方承载）。
    QList<QLineEdit*> targetEdits() const
    {
        QList<QLineEdit*> edits;
        for (const char* name :
             {"target-x", "target-y", "target-z", "target-roll",
              "target-pitch", "target-yaw"}) {
            if (QLineEdit* edit = panel->findChild<QLineEdit*>(QString(name))) {
                edits << edit;
            }
        }
        return edits;
    }

    /// 目标轴标签文本集合（表单 QLabel——位置三轴＋姿态 RPY 三行的
    /// 随制式断言面）。
    QStringList targetLabelTexts() const
    {
        QStringList texts;
        const QList<QLabel*> labels = panel->findChildren<QLabel*>();
        for (const QLabel* label : labels) {
            const QString text = label->text();
            if (text.startsWith(QStringLiteral("目标 X"))
                || text.startsWith(QStringLiteral("目标 Y"))
                || text.startsWith(QStringLiteral("目标 Z"))
                || text.startsWith(QStringLiteral("目标 RPY-"))) {
                texts << text;  // 只取目标行标签（状态行等无关标签不入集）
            }
        }
        return texts;
    }

    /// 直触求解槽（QMetaObject 按名调用——真实控件事件路径）。
    void clickSolve()
    {
        ASSERT_TRUE(QMetaObject::invokeMethod(panel.get(), "onSolveClicked",
                                              Qt::DirectConnection));
    }
};

/// 断言提交目标 X＝期望 SI 值（记录求解器收到的 targetInBase 平移分量
/// ——求解器侧只见 SI 真值的契约钉扎面）。
void expectSubmittedTargetX(const PanelHarness& h, double expectedSiMeters,
                            double tol, const std::string& context = {})
{
    ASSERT_TRUE(h.solver.received.has_value())
        << "求解器未收到请求" << (context.empty() ? "" : ("：" + context));
    EXPECT_NEAR(h.solver.received->targetInBase.P()[0], expectedSiMeters, tol)
        << (context.empty() ? "" : context);
}

}  // namespace

// =====================================================================
// 标签一致（KIN-P1 验收"标签一致"半区——审查 A07 缺陷面①）
// =====================================================================

/// 切到 mm 后标签必须跟随制式（携带 "mm"＋基座系标识），不得仍标 m；
/// 缺省 SI 直显制式标注 "m"。修复前标签为构造期硬编码 "目标 X（m）"，
/// 切换制式后不更新（A07 缺陷面①——本用例修复前红）。
TEST(KinematicsPanelInput, TargetLabels_FollowDisplayUnitSystem_KIN_P1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12"},
                  std::vector<std::string>{"AT-27"});
    PanelHarness h;

    // 缺省（未注入制式＝SI 直显）：三轴标签标注 m＋基座系参考系标识。
    {
        const QStringList texts = h.targetLabelTexts();
        // ★ KIN-R2 任务4：位置三轴＋姿态 RPY 三行——六标签齐全。
        // ★ 审查 G7 消歧（WP-15-T20 第三批）：RPY 标签带轴向（本面板 roll
        // 驱动 Rz——rw::math::RPY 参数命名；与 requirements 面板"Roll 绕
        // X"同名反轴，标签显式标注消除静默误抄）。
        ASSERT_EQ(texts.size(), 6);
        EXPECT_TRUE(texts.contains(QStringLiteral("目标 X（m，基座系）")));
        EXPECT_TRUE(texts.contains(QStringLiteral("目标 Y（m，基座系）")));
        EXPECT_TRUE(texts.contains(QStringLiteral("目标 Z（m，基座系）")));
        EXPECT_TRUE(texts.contains(QStringLiteral("目标 RPY-Roll＝绕Z（rad，基座系）")));
        EXPECT_TRUE(texts.contains(QStringLiteral("目标 RPY-Pitch＝绕Y（rad，基座系）")));
        EXPECT_TRUE(texts.contains(QStringLiteral("目标 RPY-Yaw＝绕X（rad，基座系）")));
    }

    // L-K8 切换 mm/deg：标签随制式重建（零重算/零修订——呈现半区）。
    const auto mm = DisplayUnitProjection::tryFind("mm", "deg");
    ASSERT_TRUE(mm.has_value());
    h.panel->setDisplayUnits(mm);
    {
        const QStringList texts = h.targetLabelTexts();
        ASSERT_EQ(texts.size(), 6);
        EXPECT_TRUE(texts.contains(QStringLiteral("目标 X（mm，基座系）")));
        EXPECT_TRUE(texts.contains(QStringLiteral("目标 Y（mm，基座系）")));
        EXPECT_TRUE(texts.contains(QStringLiteral("目标 Z（mm，基座系）")));
        // 角度量纲随制式独立标注（deg——长度 mm、角度 deg 分立，KIN-R2）。
        EXPECT_TRUE(texts.contains(QStringLiteral("目标 RPY-Roll＝绕Z（deg，基座系）")));
        EXPECT_TRUE(texts.contains(QStringLiteral("目标 RPY-Pitch＝绕Y（deg，基座系）")));
        EXPECT_TRUE(texts.contains(QStringLiteral("目标 RPY-Yaw＝绕X（deg，基座系）")));
        // 不得残留旧制式硬编码形态 "（m）"（精确全串比对已保证制式正确；
        // 此处对修复前的旧文本再补一钉——防回归回退到"标签恒 m"）。
        for (const QString& text : texts) {
            EXPECT_FALSE(text.contains(QStringLiteral("（m）")))
                << text.toStdString();  // QString 无 ostream 运算符——转 std 串
        }
    }
}

// =====================================================================
// 提交逆换算（KIN-P1 验收"m/cm/mm 同一目标的 SI 值一致"——审查 A07
// 缺陷面②：修复前显示值被按 SI→显示的正向投影再放大）
// =====================================================================

/// m/cm/mm 三种制式输入"同一物理目标 X=0.1 m"（各制式正确显示值
/// 0.1/10/100），提交给求解器的 SI 真值必须一致＝0.1 m。修复前 mm
/// 制式输入 100 被 projectLength 正向再放大为 100000 m（方向反转缺陷
/// ——本用例修复前红）。
TEST(KinematicsPanelInput, TargetSubmit_ConsistentSiAcrossUnitSystems_KIN_P1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12"},
                  std::vector<std::string>{"AT-27"});
    // 制式→X 轴输入文本（同一物理目标 0.1 m 的各制式正确显示值）。
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"m", "0.1"},    // m 制式：0.1 m
        {"cm", "10"},    // cm 制式：10 cm＝0.1 m
        {"mm", "100"},   // mm 制式：100 mm＝0.1 m
    };
    for (const auto& [lengthSymbol, inputText] : cases) {
        PanelHarness h;
        const auto units =
            DisplayUnitProjection::tryFind(lengthSymbol, "deg");
        ASSERT_TRUE(units.has_value()) << lengthSymbol;
        h.panel->setDisplayUnits(units);

        const QList<QLineEdit*> edits = h.targetEdits();
        ASSERT_EQ(edits.size(), 6U);  // 六字段（位置 3＋姿态 RPY 3——KIN-R2 任务4）
        edits[0]->setText(QString::fromStdString(inputText));
        edits[1]->setText(QStringLiteral("0"));
        edits[2]->setText(QStringLiteral("0"));
        h.clickSolve();
        expectSubmittedTargetX(h, 0.1, 1e-12,
                               "制式 " + lengthSymbol + " 输入 " + inputText);
        EXPECT_DOUBLE_EQ(h.solver.received->targetInBase.P()[1], 0.0);
        EXPECT_DOUBLE_EQ(h.solver.received->targetInBase.P()[2], 0.0);
    }
}

/// A07 反例数值钉扎：mm 制式输入 "0.1"（标签标 mm——即 0.1 mm＝1e-4 m）
/// 提交 1e-4 m。按 m 标签解释应为 0.1 m、按 mm 解释应为 1e-4 m，均不
/// 是修复前的 100 m（审查 A07 原始反例——方向反转的回归钉）。
TEST(KinematicsPanelInput, TargetSubmit_mmInput01IsTenthMillimetre_KIN_P1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12"},
                  std::vector<std::string>{"AT-27"});
    PanelHarness h;
    const auto mm = DisplayUnitProjection::tryFind("mm", "deg");
    ASSERT_TRUE(mm.has_value());
    h.panel->setDisplayUnits(mm);

    const QList<QLineEdit*> edits = h.targetEdits();
    ASSERT_EQ(edits.size(), 6U);  // 六字段（位置 3＋姿态 RPY 3——KIN-R2 任务 4）
    edits[0]->setText(QStringLiteral("0.1"));
    edits[1]->setText(QStringLiteral("0"));
    edits[2]->setText(QStringLiteral("0"));
    h.clickSolve();
    // 1e-4 m（≠100 m——修复前缺陷值；≠0.1 m——mm 制式按标签解释）。
    expectSubmittedTargetX(h, 1e-4, 1e-15);
}

/// 非有限输入拒绝（NFR-COR-03 非有限不静默——"inf"/"nan" 文本解析成
/// 功但非有限，提交面就地反馈并保留原值，不向求解器传递非有限目标）。
TEST(KinematicsPanelInput, TargetSubmit_RejectsNonFiniteInput_KIN_P1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12", "NFR-COR-03"},
                  std::vector<std::string>{});
    PanelHarness h;

    const QList<QLineEdit*> edits = h.targetEdits();
    ASSERT_EQ(edits.size(), 6U);  // 六字段（位置 3＋姿态 RPY 3——KIN-R2 任务 4）
    edits[0]->setText(QStringLiteral("inf"));
    edits[1]->setText(QStringLiteral("0"));
    edits[2]->setText(QStringLiteral("0"));
    h.clickSolve();
    // 求解器零触达（受理前拒绝）＋状态行如实反馈。
    EXPECT_FALSE(h.solver.received.has_value());
    EXPECT_NE(h.panel->lastStatusText().toStdString().find("非法"),
              std::string::npos);
}

// =====================================================================
// KIN-P2（审查 A08）——真实按钮请求装配：会话求解请求必须从会话/模型
// 装配完整合法（TCP 身份可解析、初值/容差/迭代来自配置、referenceQ 取
// 会话姿态、intervals 取模型限位），失败显式给稳定诊断码不提交残缺
// 请求；并以真实 IkSolver 闭环验证"请求通过求解器校验面"。
// =====================================================================

namespace {

/// 无工具模型（装配失败轨的载体——tools 空＋defaultTcpIndex 空）。
rt::CanonicalModel makeToollessCoaxModel()
{
    std::vector<rt::CanonicalJoint> joints;
    for (int i = 0; i < 2; ++i) {
        joints.push_back(revolute("t" + std::to_string(i),
                                  rw::math::Vector3D<double>(0, 0, 1),
                                  trans(0, 0, 0), -1.0, 1.0));
    }
    // withTool=false——builder 产出无工具模型（KIN-NO-TCP"未配置"分支）。
    return makeModel(joints, trans(0.3, 0, 0), trans(0, 0, 0), false);
}

/// A08 专用夹具（视图/求解器可换——记录求解器装配断言＋真实求解器
/// 闭环断言两形态；会话基线纪律同 PanelHarness，I-KIN-4 seed≥1）。
struct AssembleHarness {
    TestView view{coaxialChainModel()};   // 六轴同轴链（夹具 A02 载体几何）
    WidgetRecordingSolver recorder;
    IkSolver realSolver;                  // 真实产品求解器（闭环形态注入）
    KinPanelServices services;
    KinModuleSessionState state;
    std::unique_ptr<KinematicsPanelWidget> panel;

    explicit AssembleHarness(bool useRealSolver) : view(coaxialChainModel())
    {
        services.modelView = &view;
        services.ikSolver = useRealSolver
                                ? static_cast<const IIkSolver*>(&realSolver)
                                : static_cast<const IIkSolver*>(&recorder);
        state.savedConfig.seed = 1;
        state.savedConfig.regionBudget.seed = 1;
        // 求解预算面（闭环用例需要真收敛——缺省 iterationLimit=1/
        // initialValuesCount=1 是合法但不足以从零初值收敛的预算；
        // 配置本就是装配语义的驱动面，此处即其会话事实）。
        state.savedConfig.iterationLimit = 256U;
        state.savedConfig.initialValuesCount = 4U;
        panel = std::make_unique<KinematicsPanelWidget>(services, state);
    }

    /// 无工具形态（视图换 bare 模型——装配失败轨载体）。
    static AssembleHarness toolless()
    {
        AssembleHarness h(false);
        h.view = TestView(makeToollessCoaxModel());
        h.panel = std::make_unique<KinematicsPanelWidget>(h.services, h.state);
        return h;
    }

    void setTarget(const QString& x, const QString& y, const QString& z)
    {
        // 位置分量按 objectName 定位（★ KIN-R2 任务4——六字段后
        // findChildren 顺序不可依赖）；姿态 RPY 三字段留空＝0（历史
        // 单位旋转行为——本夹具用例均为位置目标）。
        QLineEdit* ex = panel->findChild<QLineEdit*>(QStringLiteral("target-x"));
        QLineEdit* ey = panel->findChild<QLineEdit*>(QStringLiteral("target-y"));
        QLineEdit* ez = panel->findChild<QLineEdit*>(QStringLiteral("target-z"));
        ASSERT_NE(ex, nullptr);
        ASSERT_NE(ey, nullptr);
        ASSERT_NE(ez, nullptr);
        ex->setText(x);
        ey->setText(y);
        ez->setText(z);
    }

    void clickSolve()
    {
        ASSERT_TRUE(QMetaObject::invokeMethod(panel.get(), "onSolveClicked",
                                              Qt::DirectConnection));
    }
};

}  // namespace

/// 装配字段完整钉（记录求解器——逐字段断言装配来源）：真实按钮提交的
/// 请求必须携带可解析 TCP（KIN-14 默认工具＋tcpKey＝工具名）、非空初值
/// 集（配置物化）、dof 维 referenceQ 与 intervals（模型限位）与配置
/// 直传的求解参数。修复前：默认构造请求直传——TCP 空/初值空/维度 0，
/// 本用例红（审查 A08 探针 AuditWidget.SolveRequestMustCarryResolved-
/// TcpAndInitialValues 的产品测试化）。
TEST(KinematicsPanelInput, SolveButton_CarriesCompleteAssembledRequest_KIN_P2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "KIN-13", "KIN-14"},
                  std::vector<std::string>{});
    AssembleHarness h(false);

    h.setTarget(QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("0"));
    h.clickSolve();

    ASSERT_TRUE(h.recorder.received.has_value());
    const IkRequest& req = *h.recorder.received;

    // TCP身份：命中快照工具＋空键＝默认偏移（A-15，不受真实同名非默认键影响）。
    const auto loc = h.view.model().findObject(req.tcp.toolObject);
    ASSERT_TRUE(loc.has_value()) << "TCP 身份必须经名称解析为可解析工具";
    EXPECT_EQ(loc->kind, runtime::CanonicalModel::ObjectKind::Tool);
    EXPECT_TRUE(req.tcp.tcpKey.empty());

    // 初值集：非空（来自配置——缺省 JointGrid×1）且逐项 dof 维。
    ASSERT_FALSE(req.initialValues.empty());
    for (const std::vector<double>& q0 : req.initialValues) {
        ASSERT_EQ(q0.size(), 6U);
    }
    // referenceQ/intervals：dof 维（会话未设置→零位回退；区间取模型限位
    // ——与 evaluationIntervals 同源逐轴比较，JointInterval 无聚合等值）。
    EXPECT_EQ(req.referenceQ.size(), 6U);
    ASSERT_EQ(req.intervals.size(), 6U);
    {
        const std::vector<JointInterval> expectItv = evaluationIntervals(h.view);
        ASSERT_EQ(req.intervals.size(), expectItv.size());
        for (std::size_t i = 0; i < expectItv.size(); ++i) {
            EXPECT_EQ(req.intervals[i].bounded, expectItv[i].bounded);
            EXPECT_DOUBLE_EQ(req.intervals[i].lower, expectItv[i].lower);
            EXPECT_DOUBLE_EQ(req.intervals[i].upper, expectItv[i].upper);
        }
    }
    // 求解参数：配置直传（缺省配置 iterationLimit=1/容差 1e-6）。
    EXPECT_EQ(req.iterationLimit, h.state.savedConfig.iterationLimit);
    EXPECT_DOUBLE_EQ(req.positionTolerance,
                     h.state.savedConfig.positionResidualTolerance);
    EXPECT_DOUBLE_EQ(req.orientationTolerance,
                     h.state.savedConfig.orientationResidualTolerance);
    // 目标位姿＝输入的 SI 真值（A07 逆换算链不回归——(0,1,0)）。
    EXPECT_DOUBLE_EQ(req.targetInBase.P()[1], 1.0);
}

/// 真实求解器闭环（验收标准"真实按钮使用真实求解器"）：载体＝
/// twoLinkModel（平面二连杆——一般位形非奇异；审查 A02 的退化链闭环
/// 已由 IkTest/RegionCoverageTest/WorkspaceTest 的 KIN_P2 用例承载，
/// 此处不再用退化链——其任意构型雅可比奇异，条件数 +∞ 会触发
/// 审查 A03 的显示格式化拒绝面〔KIN-P3 范围，不属于本包修复面〕，
/// 与 A08 装配链路无关）。目标＝q*=(0.3,−0.3) 的精确可达位姿：姿态
/// Rz(0)=I（零姿态。KIN-R2 后面板已支持完整 RPY 姿态编辑，本用例仍取
/// 零姿态以聚焦 A08 请求装配链路本身——"姿态恒单位阵"的旧输入边界
/// 已由本文件 RPY 往返/非有限姿态用例闭合），位置 (1.1·cos0.3＋0.7, 1.1·sin0.3, 0) 解析现算。
/// 修复前：残缺请求触发求解器 fail-fast（std::invalid_argument 穿越
/// 槽函数），本用例红；修复后：SolutionsFound＋解集入会话态＋状态行
/// 内联完成。
TEST(KinematicsPanelInput, SolveButton_CompletesRealSolveThroughAssembler_KIN_P2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "KIN-04"},
                  std::vector<std::string>{});
    // 载体换 twoLink（Harness 的 view 成员可赋值——值语义）。
    AssembleHarness h(true);
    h.view = TestView(twoLinkModel());
    h.panel = std::make_unique<KinematicsPanelWidget>(h.services, h.state);

    // q*=(0.3,−0.3) 的 TCP 位置（q1+q2=0 → 姿态恒等；黄金算例 A 几何
    // L1=1.1/L2=0.7——第二段沿 x 轴方向的解析合成）。15 位有效数字
    // 序列化（QString::number——输入文本的舍入误差远小于位置容差）。
    const double qx = 1.1 * std::cos(0.3) + 0.7;
    const double qy = 1.1 * std::sin(0.3);
    h.setTarget(QString::number(qx, 'g', 15),
                QString::number(qy, 'g', 15),
                QStringLiteral("0"));
    // 修复前此处抛 std::invalid_argument（TCP 空→KIN-NO-TCP 轨）穿越
    // Qt 槽——真实求解器 fail-fast 面的回归钉。
    ASSERT_NO_THROW(h.clickSolve());

    // 会话态写点：解集非空、首解位置残差入容差（JointGrid 多初值＝
    // 区间对角点——非奇异链 DLS 预算内收敛）。
    ASSERT_TRUE(h.state.lastSessionSolutionSet != nullptr);
    ASSERT_EQ(h.state.lastSessionSolutionSet->solutions.size(), 1U);
    EXPECT_LE(h.state.lastSessionSolutionSet->solutions.front().positionResidual,
              h.state.savedConfig.positionResidualTolerance);
    // 状态行＝内联完成（非装配失败/非转后台）。
    EXPECT_NE(h.panel->lastStatusText().toStdString().find("内联完成"),
              std::string::npos);
}

/// 装配失败显式报错（无工具模型）：状态行携带稳定诊断码 KIN-NO-TCP，
/// 求解器零触达（残缺请求绝不提交——A08"解析失败显式报错"义务）。
TEST(KinematicsPanelInput, SolveButton_UnresolvedTcpShowsDiagnostic_KIN_P2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "KIN-14"},
                  std::vector<std::string>{});
    AssembleHarness h = AssembleHarness::toolless();

    h.setTarget(QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"));
    h.clickSolve();

    EXPECT_FALSE(h.recorder.received.has_value());
    EXPECT_NE(h.panel->lastStatusText().toStdString().find("KIN-NO-TCP"),
              std::string::npos);
}

// =====================================================================
// KIN-R2 任务4——目标姿态 RPY 编辑（审查 §4"目标只编辑 XYZ、姿态固定
// 为单位旋转"的修复面）：姿态角按显示制式输入、提交逆换算回 SI rad
// 后经 KinPanelRotation::rotationFromRpySi 构造完整旋转（求解器收到完整
// 位姿目标）。F-919：不再直接引用 rw::math::RPY 的非内联符号，保证独立
// 冒烟树可链接；与 rw::math::RPY 的语义等价由 KinP4FlowsTest 门控用例钉住。
// =====================================================================

/// RPY 往返钉扎：deg 制式输入 roll=30/pitch=0/yaw=90，提交给求解器的
/// targetInBase 旋转必须等于 RPY(π/6, 0, π/2)（Z-Y-X 欧角语义；等价性见
/// KinP4FlowsTest）。修复前姿态恒单位旋转（无编辑入口——审查 §4 已知
/// 边界，本用例修复前红）。
TEST(KinematicsPanelInput, TargetSubmit_RpyAttitudeRoundtrip_KIN_R2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02"},
                  std::vector<std::string>{});
    PanelHarness h;
    // deg 制式（角度标注/输入面；长度保持 m——两量纲分立独立换算）。
    const auto deg = DisplayUnitProjection::tryFind("m", "deg");
    ASSERT_TRUE(deg.has_value());
    h.panel->setDisplayUnits(deg);

    const QList<QLineEdit*> edits = h.targetEdits();
    ASSERT_EQ(edits.size(), 6U);
    edits[0]->setText(QStringLiteral("1"));   // X＝1 m
    edits[1]->setText(QStringLiteral("0"));
    edits[2]->setText(QStringLiteral("0"));
    edits[3]->setText(QStringLiteral("30"));  // roll＝30 deg＝π/6 rad
    edits[4]->setText(QStringLiteral("0"));   // pitch＝0
    edits[5]->setText(QStringLiteral("90"));  // yaw＝90 deg＝π/2 rad
    h.clickSolve();

    ASSERT_TRUE(h.solver.received.has_value());
    // 期望旋转：显示制式 deg → SI rad 逆换算后经 KinPanelRotation 构造
    // （与 widget 同一转换单点；其与 rw::math::RPY 的逐元等价由 KinP4FlowsTest
    // 门控用例钉住——测试面同样不引用框架非内联符号）。
    const double r = 30.0 * (rw::math::Pi / 180.0);   // rad
    const double y = 90.0 * (rw::math::Pi / 180.0);   // rad
    const rw::math::Rotation3D<double> expect = rotationFromRpySi(r, 0.0, y);
    const rw::math::Rotation3D<double>& got =
        h.solver.received->targetInBase.R();
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            EXPECT_NEAR(got(i, j), expect(i, j), 1e-12)
                << "R[" << i << "][" << j << "]";
        }
    }
    // 位置分量同请求不回归（deg 制式只影响角度半区——长度 m 恒等）。
    EXPECT_DOUBLE_EQ(h.solver.received->targetInBase.P()[0], 1.0);
}

/// 非有限姿态输入拒绝（NFR-COR-03 交互层拒绝——yaw="inf" 解析成功但
/// 非有限：提交面就地反馈保留原值，求解器零触达——与位置分量的非有限
/// 拒绝同则，KIN-R2 任务4 的姿态半区）。
TEST(KinematicsPanelInput, TargetSubmit_RejectsNonFiniteAttitude_KIN_R2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"},
                  std::vector<std::string>{});
    PanelHarness h;
    const QList<QLineEdit*> edits = h.targetEdits();
    ASSERT_EQ(edits.size(), 6U);
    edits[0]->setText(QStringLiteral("0"));
    edits[1]->setText(QStringLiteral("0"));
    edits[2]->setText(QStringLiteral("0"));
    edits[3]->setText(QStringLiteral("0"));
    edits[4]->setText(QStringLiteral("0"));
    edits[5]->setText(QStringLiteral("inf"));  // 非有限姿态角
    h.clickSolve();
    // 求解器零触达（受理前拒绝）＋状态行如实反馈（姿态分量语义）。
    EXPECT_FALSE(h.solver.received.has_value());
    EXPECT_NE(h.panel->lastStatusText().toStdString().find("姿态"),
              std::string::npos);
}

// =====================================================================
// 审查 20261021 修复批回归（WP-15-T20 第三批）
// =====================================================================

namespace {

/// G3 专用：延迟求解器（耗时形态用例——零求解值语义；IIkSolver 纯函数
/// 线程安全契约下 worker 线程调用安全）。
class SleepingSolver final : public IIkSolver {
public:
    std::chrono::milliseconds delay{1200};
    mutable std::atomic<int> calls{0};
    IkOutcome solve(const IkRequest&) const override
    {
        ++calls;
        std::this_thread::sleep_for(delay);
        return IkOutcome{};
    }
};

/// G3 夹具（两段式装配——缝先注、面板后建；异步执行缝测试侧实现＝
/// 与产品装配层同构的 std::async＋invokeMethod 回投——线程设施在缝的
/// 提供方，不在插件；future 自持保活逃逸作用域）。
struct NonBlockingHarness {
    TestView view{sixAxisModel()};
    SleepingSolver solver;
    KinPanelServices services;
    KinModuleSessionState state;
    std::unique_ptr<KinematicsPanelWidget> panel;

    NonBlockingHarness()
    {
        services.modelView = &view;
        services.ikSolver = &solver;
        services.inlineSolveAsync =
            [](std::function<IkOutcome()> job,
               std::function<void(IkOutcome)> onDone, const QObject* context) {
                auto inflight = std::make_shared<std::future<void>>();
                *inflight = std::async(
                    std::launch::async,
                    [job = std::move(job), onDone = std::move(onDone), context,
                     inflight]() mutable {
                        IkOutcome outcome = job();
                        QMetaObject::invokeMethod(
                            const_cast<QObject*>(context),
                            [onDone = std::move(onDone),
                             outcome = std::move(outcome)]() mutable {
                                onDone(std::move(outcome));
                            },
                            Qt::QueuedConnection);
                    });
            };
        state.savedConfig.seed = 1;
        state.savedConfig.regionBudget.seed = 1;
        panel = std::make_unique<KinematicsPanelWidget>(services, state);
    }
};

}  // namespace

/// ★ 审查 G3 回归（WP-15-T20 第三批）：异步缝在场＝求解非阻塞——点击
/// 立即返回（＜求解全程的零头；修复前同步求解阻塞 UI 线程整个求解时长），
/// UI 线程期间持续处理事件（计时器运转）；完成回调回 UI 线程后结果经
/// 唯一写点入会话态（恰一次求解——零双份计算）、状态行与按钮门控复位。
/// 缝缺席的降级路径行为由 PluginPanelTest 既有预算用例钉住
/// （runSinglePointSolve 原径不变）。
TEST(KinematicsPanelInput, SolveNonBlockingViaAssemblySeam_WP15T20_G3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "NFR-PERF-01"},
                  std::vector<std::string>{});
    NonBlockingHarness h;

    QList<QLineEdit*> edits;
    for (const char* n : {"target-x", "target-y", "target-z"}) {
        edits << h.panel->findChild<QLineEdit*>(QString(n));
    }
    ASSERT_EQ(edits.size(), 3);
    edits[0]->setText(QStringLiteral("0.1"));
    edits[1]->setText(QStringLiteral("0"));
    edits[2]->setText(QStringLiteral("0"));

    // ---- 点击立即返回（求解在装配层工作线程——UI 线程零全程占用）。
    int timerTicks = 0;
    QTimer ticker;
    ticker.setInterval(30);
    QObject::connect(&ticker, &QTimer::timeout, [&] { ++timerTicks; });
    ticker.start();
    const auto t0 = std::chrono::steady_clock::now();
    ASSERT_TRUE(QMetaObject::invokeMethod(h.panel.get(), "onSolveClicked",
                                          Qt::DirectConnection));
    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0)
            .count();
    EXPECT_LT(elapsedMs, 600);  // 求解全程 1200 ms——点击远早于其结束
    EXPECT_TRUE(h.panel->lastStatusText().contains(QStringLiteral("求解中")))
        << h.panel->lastStatusText().toStdString();
    // 在途旗标禁用求解按钮（防重复提交——refreshAll 门控）。
    QPushButton* solve = nullptr;
    for (QPushButton* b : h.panel->findChildren<QPushButton*>()) {
        if (b->text() == QStringLiteral("求解 IK")) {
            solve = b;
        }
    }
    ASSERT_NE(solve, nullptr);
    EXPECT_FALSE(solve->isEnabled());

    // ---- UI 线程持续处理事件（计时器运转——求解期间界面响应性）＋等待
    // 完成回调（测试侧事件环驱动队列回投；超时 5 s＝延迟求解有界）。
    QEventLoop wait;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, [&] { wait.quit(); });
    // 轮询会话解集（完成回调的唯一可观测落点——现取零缓存）。
    QTimer poll;
    poll.setInterval(20);
    QObject::connect(&poll, &QTimer::timeout, [&] {
        if (h.state.lastSessionSolutionSet != nullptr) {
            wait.quit();
        }
    });
    poll.start();
    timeout.start(5000);
    wait.exec();
    poll.stop();
    timeout.stop();
    ticker.stop();

    EXPECT_GT(timerTicks, 0);             // 求解期间事件被处理（响应性）
    EXPECT_EQ(h.solver.calls.load(), 1);  // 恰一次求解（零双份计算）
    ASSERT_TRUE(h.state.lastSessionSolutionSet != nullptr);  // 结果已入会话态
    EXPECT_TRUE(h.panel->lastStatusText().contains(QStringLiteral("内联完成")))
        << h.panel->lastStatusText().toStdString();
    EXPECT_TRUE(solve->isEnabled());  // 在途旗标已清位（按钮门控复位）
}

/// ★ 审查 G5 回归（WP-15-T20 第三批）：显示单位切换生产入口（长度/角度
/// 两下拉）——切换经 setDisplayUnits 唯一入口全面板重投影（L-K8 对用户
/// 可达；修复前 setDisplayUnits 零生产调用点，交互不可达）。m＋rad 组合
/// ＝SI 直显制式（nullopt）。
TEST(KinematicsPanelInput, DisplayUnitCombosDriveProjection_WP15T20_G5)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12"},
                  std::vector<std::string>{"AT-27"});
    PanelHarness h;

    // 组合框定位（按条目词表——长度框含 "mm"、角度框含 "deg"）。
    QComboBox* lengthBox = nullptr;
    QComboBox* angleBox = nullptr;
    const QList<QComboBox*> boxes = h.panel->findChildren<QComboBox*>();
    for (QComboBox* box : boxes) {
        if (box->findText(QStringLiteral("mm")) >= 0) {
            lengthBox = box;
        }
        if (box->findText(QStringLiteral("deg")) >= 0) {
            angleBox = box;
        }
    }
    ASSERT_NE(lengthBox, nullptr);
    ASSERT_NE(angleBox, nullptr);

    // 切 mm/deg：标签随制式重建（经 setCurrentIndex→onDisplayUnitChanged
    // →setDisplayUnits 生产链——非测试直调 setDisplayUnits）。
    lengthBox->setCurrentIndex(2);  // mm
    angleBox->setCurrentIndex(1);   // deg
    {
        const QStringList texts = h.targetLabelTexts();
        EXPECT_TRUE(texts.contains(QStringLiteral("目标 X（mm，基座系）")));
        EXPECT_TRUE(texts.contains(
            QStringLiteral("目标 RPY-Roll＝绕Z（deg，基座系）")));
    }

    // 切回 m/rad＝SI 直显（nullopt 轨）。
    lengthBox->setCurrentIndex(0);
    angleBox->setCurrentIndex(0);
    {
        const QStringList texts = h.targetLabelTexts();
        EXPECT_TRUE(texts.contains(QStringLiteral("目标 X（m，基座系）")));
        EXPECT_TRUE(texts.contains(
            QStringLiteral("目标 RPY-Roll＝绕Z（rad，基座系）")));
    }
}
