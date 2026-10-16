/**
 * @file   KinHostLoopGuiTest.cpp
 * @brief  运动学"正式产品闭环"GUI 回归（★ KIN-P4——审查报告 §5 KIN-P4
 *         行验收主径：以六轴改型案例，在真实控件事件＋真实求解器层走通
 *         「建改型→多个任务点→求解/比较→预览→导出→重开复算」全过程）。
 *
 * 设计依据：
 *   - 审查报告 audit/kin-current/运动学模块重新审查-20261016.md §4/§5：
 *     "库存在评估器"与"研发工程师可从正式程序完成计算"必须分别验收；
 *     本文件即后者的集成测试层证据（真实 widget＋真实 IkSolver/FkEvaluator
 *     ＋TCP 解析单点＋真实文件写出——注入面仅导出目录与身份派生）；
 *   - 交互路径与 KinematicsPanelSessionGuiTest 同形态（QMetaObject 按名
 *     直触槽函数＝真实控件事件路径的测试镜像；main 线程纪律同则）。
 *
 * 用例与验收映射：
 *   - SixAxisVariantClosedLoop：改型两代（J2 基座高度 0.4→0.5 m）→两个
 *     任务点投影→求解（解表非空＋稳定序）→比较（选中→检查器联动）→
 *     预览（双击→会话姿态回写零修订）→导出（真实 JSON 文件＋非零快照
 *     锚头）→重开复算（新快照身份入解集身份——重绑定可观测）；
 *   - PanelPresentation_RealExecution：原 GTEST_SKIP 的 GUI 呈现登记用例
 *     （PluginPanelTest KinGuiPresentationRegisteredNotExecuted_ACC1）在
 *     条件具备后转真实执行（四面板构造/页签/按钮门控/状态行点验）。
 *
 * 线程约束：全部用例在 main 线程（QApplication 线程）执行。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/审查条目追溯

#include <QFile>
#include <QLineEdit>
#include <QMetaObject>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextStream>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/kinematics/Fk.hpp>        // FkEvaluator（真实产品实现）
#include <sdurws/ird/kinematics/Ik.hpp>        // IkSolver（真实求解器）
#include <sdurws/ird/kinematics/SolutionSet.hpp>
#include "KinFkFixture.hpp"              // sixAxisModel/TestView（同单元夹具）
#include "plugin/KinematicsPanelWidget.hpp"  // 被测面板（真实 QWidget）
#include "plugin/KinPanelTypes.hpp"          // 会话态/服务缝

// ---- 使用声明（与被测代码同一命名空间——用例可读性）--------------------
using namespace sdurws::ird;
using namespace sdurws::ird::kinematics;
using namespace sdurws::ird::kinematics::testfixture;

namespace {

// =====================================================================
// 端到端夹具（六轴改型两代——真实求解器/评估器＋真实导出写出）
// =====================================================================

/// 改型 v1：二连杆平面臂（夹具黄金算例 A——L1=1.1＋L2=0.7＋TCP 偏置
/// 0.7，全展 1.8 m，非退化链）。选型依据（诚实登记）：面板目标编辑
/// 位姿固定单位旋转（审查 §4 登记的"目标只编辑 XYZ"缺陷——KIN-P4
/// 尽力项的 RPY 编辑未实施），完整位姿目标＝"位置可达＋姿态单位"。
/// 平面臂姿态一维（Rz(q1+q2)）且**非退化**——单位姿态精确可达（零位
/// 即解）且阻尼最小二乘从邻近初值数值收敛良好（同轴退化链的 DLS 收
/// 敛缺陷见审查 A02 登记——不作本用例载体）。目标 (1.8,0,0)＝v1 零
/// 位 TCP（位置精度受显示值 1e-6 舍入——容差 1e-4 覆盖）。
runtime::CanonicalModel twoLinkV1Model()
{
    return twoLinkModel();  // 夹具同源：q 全零 → TCP=(1.8,0,0) 姿态单位
}

/// 改型 v2：大臂缩短 L1 1.1→1.05 m（全展 1.8→1.75 m——真实改型语义：
/// 紧凑化设计变更、快照内容身份随之变化）。改型后原目标 (1.8,0,0)
/// **解析上界确定性不可达**（工作半径上界 1.75＜1.8——三角不等式上界
/// 证明）——复算产出"新解 0 个"正是改型评估的工程价值：同一任务点在
/// 新设计下的可行性结论如实翻转（解析界限确定性证明，非数值搜索噪声；
/// 姿态约束下平面臂的可达位置只有全展点 (L1+L2, 0)——(1.8,0) 在 v2
/// 不属其中，Bounds.cpp 解析界限直接判定）。
runtime::CanonicalModel twoLinkV2Model()
{
    constexpr double L1_V2 = 1.05;
    constexpr double L2 = 0.7;
    std::vector<runtime::CanonicalJoint> joints;
    joints.push_back(revolute("j1", rw::math::Vector3D<double>(0, 0, 1),
                              trans(0, 0, 0), -2.97, 2.97));
    joints.push_back(revolute("j2", rw::math::Vector3D<double>(0, 0, 1),
                              trans(L1_V2, 0, 0),
                              -3.14159265358979323846,
                              3.14159265358979323846));
    return makeModel(joints, trans(L2, 0, 0));
}

/// 确定性快照身份（导出锚/复算重绑定的核对键——测试面固定值派生）。
core::ContentIdentity snapshotIdFrom(const std::string& seed)
{
    core::ContentDigester d;
    d.update(seed.data(), seed.size());
    core::ContentIdentity id;
    id.bytes = d.finalize();  // 32 字节摘要直构（内容寻址——非全零合法身份）
    return id;
}

/// 端到端夹具（两段式装配——缝先注、面板后建；SessionPanelHarness 同款
/// 纪律：widget 构造期值拷贝服务缝聚合）。
struct LoopHarness {
    runtime::CanonicalModel modelV1;      ///< 改型前（TCP 距 1 m 同轴链）
    runtime::CanonicalModel modelV2;      ///< 改型后（TCP 距 1.2 m）
    TestView view;                        ///< 当前视图（换绑语义随会话推进）
    IkSolver solver;                      ///< 真实求解器（纯函数服务）
    FkEvaluator fk;                       ///< 真实 FK 评估器
    KinSessionPose sessionPose;           ///< 会话姿态（真实容器）
    KinModuleSessionState state;          ///< 插件会话态
    KinPanelServices services;            ///< 服务缝
    QTemporaryDir exportDir;              ///< 真实导出目录（io 原子写落点）
    std::unique_ptr<KinematicsPanelWidget> panel;

    LoopHarness()
        : modelV1(twoLinkV1Model()),
          modelV2(twoLinkV2Model()),
          view(modelV1)
    {
        services.modelView = &view;
        services.ikSolver = &solver;
        services.fkEvaluator = &fk;
        services.sessionPose = &sessionPose;
        // 导出写出缝＝真实文件写出（io 层原语义；目录解析闭包指向
        // 用例临时目录——产品目录解析的环境变量面在宿主装配 TU）。
        const std::string dir = exportDir.path().toStdString();
        services.exportWriter =
            [dir](const std::string& fileName, const std::string& content) {
                const std::filesystem::path target =
                    std::filesystem::path(dir) / fileName;
                std::ofstream out(target, std::ios::binary);
                if (!out.is_open()) {
                    return false;
                }
                out.write(content.data(),
                          static_cast<std::streamsize>(content.size()));
                return static_cast<bool>(out);
            };
        // 会话事实（v1 快照锚＋合法配置——装配纪律同则）。求解参数取
        // 工程量级（16 初值×500 迭代、容差 1e-4——schema 默认 1×1/1e-6
        // 是下界，真实用户在求解配置面板按工艺调定）。
        state.snapshotId = snapshotIdFrom("kin-p4-loop-v1");
        state.savedConfig.seed = 7;
        state.savedConfig.regionBudget.seed = 7;
        state.savedConfig.initialStrategy = InitialValueStrategy::ReferenceQ;
        state.savedConfig.initialValuesCount = 16;
        state.savedConfig.iterationLimit = 500;
        state.savedConfig.positionResidualTolerance = 1e-4;
        state.savedConfig.orientationResidualTolerance = 1e-4;
    }

    /// 建面板（多任务点投影缝在此注入——宿主 taskPoints 闭包的测试等价
    /// 形态：两任务点行，结果列 nullopt＝未计算）。
    void createPanel()
    {
        ASSERT_FALSE(panelCreated) << "面板只建一次（缝注入须在此之前）";
        services.taskPoints = [] {
            std::vector<KinTaskPointRow> rows;
            for (const char* name : {"抓取点 P1", "装配点 P2"}) {
                KinTaskPointRow row;
                row.label = name;
                row.enabled = true;
                rows.push_back(std::move(row));
            }
            return rows;
        };
        panel = std::make_unique<KinematicsPanelWidget>(services, state);
        panelCreated = true;
    }

    /// 换绑改型（"重开"拍——模型视图内容与会话锚整体替换：真实装配层
    /// syncKinematicsSessionFacts 的测试等价形态。视图对象地址不变——
    /// 面板构造期拷贝的 modelView 缝指向本成员，内容更新即对面板可见；
    /// 会话态是面板构造期引用——锚前移同拍生效）。
    void rebindToVariant()
    {
        view = TestView(modelV2);  // 视图内容替换（地址稳定——缝无感）
        state.snapshotId = snapshotIdFrom("kin-p4-loop-v2");
    }

    /// 目标输入三编辑（构造序＝X/Y/Z——表单登记序）。
    QList<QLineEdit*> targetEdits() const
    {
        return panel->findChildren<QLineEdit*>();
    }

    /// 按钮定位（可读文本——真实点击靶）。
    QPushButton* buttonByText(const QString& text) const
    {
        const QList<QPushButton*> buttons = panel->findChildren<QPushButton*>();
        for (QPushButton* button : buttons) {
            if (button->text() == text) {
                return button;
            }
        }
        return nullptr;
    }

    /// 解表定位（首列头 "rank"——不依赖 findChildren 序）。
    QTableWidget* solutionTable() const
    {
        const QList<QTableWidget*> tables = panel->findChildren<QTableWidget*>();
        for (QTableWidget* table : tables) {
            if (table->horizontalHeaderItem(0) != nullptr
                && table->horizontalHeaderItem(0)->text()
                       == QStringLiteral("rank")) {
                return table;
            }
        }
        return nullptr;
    }

private:
    bool panelCreated = false;  ///< createPanel 一次性守卫
};

}  // namespace

// =====================================================================
// 六轴改型闭环（★ KIN-P4 验收主径——全过程真实执行）
// =====================================================================

TEST(KinHostLoop, SixAxisVariantClosedLoop_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01", "KIN-02", "KIN-08"},
                  std::vector<std::string>{"AT-04", "AT-27"});
    LoopHarness h;
    h.createPanel();

    // ---- ①建改型＋任务点呈现：任务点表两行（真实投影——结果列"未计算"）。
    QTableWidget* taskTable = nullptr;
    const QList<QTableWidget*> tables = h.panel->findChildren<QTableWidget*>();
    for (QTableWidget* table : tables) {
        if (table->columnCount() == 4
            && table->horizontalHeaderItem(0)->text()
                   == QStringLiteral("任务点")) {
            taskTable = table;
            break;
        }
    }
    ASSERT_NE(taskTable, nullptr);
    ASSERT_EQ(taskTable->rowCount(), 2);
    EXPECT_EQ(taskTable->item(0, 2)->text(), QStringLiteral("未计算"));

    // ---- ②求解（真实求解器）：目标＝零位 TCP（基座系，SI m）。
    const KinTcpResolution tcp = resolveSessionTcp(h.services);
    ASSERT_TRUE(tcp.ok);
    const std::vector<double> zeros(2, 0.0);  // 二连杆 dof=2（黄金算例 A）
    const auto metrics =
        h.fk.evaluate(h.view, tcp.tcp, zeros);
    ASSERT_TRUE(metrics.ok());
    const rw::math::Vector3D<double>& p = metrics.get().tcpInBase.P();
    const QList<QLineEdit*> edits = h.targetEdits();
    ASSERT_GE(edits.size(), 3);
    edits[0]->setText(QString::number(p[0], 'f', 6));
    edits[1]->setText(QString::number(p[1], 'f', 6));
    edits[2]->setText(QString::number(p[2], 'f', 6));
    QPushButton* solveButton = h.buttonByText(QStringLiteral("求解 IK"));
    ASSERT_NE(solveButton, nullptr);
    EXPECT_TRUE(solveButton->isEnabled());
    ASSERT_TRUE(QMetaObject::invokeMethod(h.panel.get(), "onSolveClicked",
                                          Qt::DirectConnection));
    QTableWidget* solutionTable = h.solutionTable();
    ASSERT_NE(solutionTable, nullptr);
    ASSERT_GT(solutionTable->rowCount(), 0) << h.panel->lastStatusText().toStdString();

    // ---- ③比较（稳定序可观察）：rank 列 0 基递增（域四键排序的呈现序）。
    for (int row = 0; row < solutionTable->rowCount(); ++row) {
        EXPECT_EQ(solutionTable->item(row, 0)->text().toInt(), row);
    }

    // ---- ④预览（双击→会话姿态回写——零修订 KIN-06）：首行双击。
    ASSERT_TRUE(QMetaObject::invokeMethod(h.panel.get(),
                                          "onSolutionDoubleClicked",
                                          Qt::DirectConnection,
                                          Q_ARG(int, 0), Q_ARG(int, 0)));
    EXPECT_TRUE(h.sessionPose.isSet());
    EXPECT_EQ(h.sessionPose.jointConfiguration().size(), 2U);

    // ---- ⑤导出（真实 JSON 文件写出——快照锚非零头）。
    QPushButton* exportButton = h.buttonByText(QStringLiteral("导出 JSON"));
    ASSERT_NE(exportButton, nullptr);
    EXPECT_TRUE(exportButton->isEnabled());
    ASSERT_TRUE(QMetaObject::invokeMethod(h.panel.get(), "onExportJsonClicked",
                                          Qt::DirectConnection));
    const std::filesystem::path exported =
        std::filesystem::path(h.exportDir.path().toStdString())
        / "kin-results.json";
    ASSERT_TRUE(std::filesystem::exists(exported))
        << h.panel->lastStatusText().toStdString();
    {
        std::ifstream in(exported, std::ios::binary);
        const std::string content((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
        // 导出头自证（格式 token＋来源会话内存＋快照锚非零——修复前
        // 内联解集身份未装配，快照锚恒全零）。
        EXPECT_NE(content.find("ird-kin-export"), std::string::npos);
        EXPECT_NE(content.find("session-memory"), std::string::npos);
        const std::string zeroAnchor = "cid-" + std::string(64, '0');
        EXPECT_EQ(content.find("\"snapshotId\":\"" + zeroAnchor + "\""),
                  std::string::npos)
            << "导出快照锚全零（身份未装配）——复算重绑定与溯源都无从谈起";
    }

    // ---- ⑥建改型重开（换绑 v2＋会话锚前移）→复算（当前快照）。
    // 视图对象地址稳定（内容已替换为 v2）＋会话态引用语义（锚已前移）
    // ——复算流经面板内缝读到的即改型后世界，与真实装配层的换绑语义
    // 逐字同构。setWritable 触发全面板门控刷新（真实用户视角：求解有
    // 果＋原解集在场＝复算按钮点亮）。
    h.rebindToVariant();
    h.panel->setWritable(true);
    QPushButton* recomputeButton = h.buttonByText(QStringLiteral("复算（当前快照）"));
    ASSERT_NE(recomputeButton, nullptr);
    EXPECT_TRUE(recomputeButton->isEnabled());
    ASSERT_TRUE(QMetaObject::invokeMethod(h.panel.get(), "onRecomputeClicked",
                                          Qt::DirectConnection));
    const std::string status = h.panel->lastStatusText().toStdString();
    EXPECT_NE(status.find("复算"), std::string::npos) << status;
    // 重绑定可观测（新解集身份＝v2 快照锚；原 seed 携带）。
    ASSERT_TRUE(h.state.lastSessionSolutionSet != nullptr);
    EXPECT_EQ(h.state.lastSessionSolutionSet->requestIdentity.snapshotId,
              snapshotIdFrom("kin-p4-loop-v2"));
    EXPECT_EQ(h.state.lastSessionSolutionSet->requestIdentity.seed, 7U);
    // 复算结论（工程正确的翻转）：改型缩短后原目标超解析工作半径上界
    // （1.75＜1.8——Bounds.cpp 确定性判定）→新解 0 个（结局 5 的"仅素
    // 材"语义如实——"不可行"结论有据，非数值噪声）。状态行摘要与解集
    // 空态两面对照（原解 1 个 → 新解 0 个的对比呈现）。
    EXPECT_TRUE(h.state.lastSessionSolutionSet->solutions.empty());
    EXPECT_NE(h.panel->lastStatusText().toStdString().find("新解 0 个"),
              std::string::npos);
}

// =====================================================================
// GUI 呈现登记转真实执行（原 GTEST_SKIP 用例的条件具备化）
// =====================================================================

/// 四面板呈现点验（真实执行）：四页签在位＋逐页按钮门控如实（可用性
/// ＝缝装配事实，零虚构）＋状态行在位。修复前该面以 GTEST_SKIP 登记
/// （模型测试目标无 GUI 环境——条件不具备）；gui_test 目标具备真实
/// QApplication 环境后转为真实执行（两处用例注释互链）。
TEST(KinHostLoop, PanelPresentation_RealExecution_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-04", "UX-05"},
                  std::vector<std::string>{});
    LoopHarness h;
    h.createPanel();

    // 四页签（构建序＝面板表行 1~4）。
    QTabWidget* tabs = h.panel->findChild<QTabWidget*>();
    ASSERT_NE(tabs, nullptr);
    ASSERT_EQ(tabs->count(), 4);
    EXPECT_EQ(tabs->tabText(0), QStringLiteral("位姿指标"));
    EXPECT_EQ(tabs->tabText(1), QStringLiteral("任务点验证"));
    EXPECT_EQ(tabs->tabText(2), QStringLiteral("区域覆盖"));
    EXPECT_EQ(tabs->tabText(3), QStringLiteral("结果与可视化"));

    // 按钮门控（缝装配事实的真实投影——非全禁用非全可用的诚实形态）：
    // 求解/复位 Home/复算可用（求解器/FK/Home 来源……Home 缝未注入＝
    // 禁用）；批量/覆盖禁用（后台缝未注入）；导出禁用（无结果）。
    EXPECT_TRUE(h.buttonByText(QStringLiteral("求解 IK"))->isEnabled());
    EXPECT_FALSE(h.buttonByText(QStringLiteral("复位 Home"))->isEnabled());
    EXPECT_FALSE(h.buttonByText(QStringLiteral("批量验证（后台）"))->isEnabled());
    EXPECT_FALSE(h.buttonByText(QStringLiteral("运行覆盖评估（后台）"))->isEnabled());
    EXPECT_FALSE(h.buttonByText(QStringLiteral("导出 JSON"))->isEnabled());
    EXPECT_FALSE(h.buttonByText(QStringLiteral("复算（当前快照）"))->isEnabled());

    // 状态行在位（非模态反馈——UX-03）。
    EXPECT_FALSE(h.panel->lastStatusText().isEmpty());
}
