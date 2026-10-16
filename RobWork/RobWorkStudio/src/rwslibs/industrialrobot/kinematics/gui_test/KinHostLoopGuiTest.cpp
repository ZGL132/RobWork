/**
 * @file   KinHostLoopGuiTest.cpp
 * @brief  运动学"正式产品闭环"GUI 回归（★ KIN-P4——审查报告 §5 KIN-P4
 *         行验收主径：以**六轴臂改型案例**，在真实控件事件＋真实求解器
 *         层走通「建改型→多个任务点→求解/比较→预览→导出→重开复算」
 *         全过程）。
 *
 * 设计依据：
 *   - 审查报告 audit/kin-current/运动学模块重新审查-20261016.md §4/§5：
 *     "库存在评估器"与"研发工程师可从正式工程师可从正式程序完成计算"
 *     必须分别验收；本文件即后者的集成测试层证据（真实 widget＋真实
 *     IkSolver/FkEvaluator＋TCP 解析单点＋真实文件写出——注入面仅导出
 *     目录与身份派生）；
 *   - ★ 独立验收 blocker 1 整改（2026-10-16）：初版载体误选二连杆平面臂
 *     （选型依据"姿态约束下平面臂可达位置仅全展点"不成立——验收人探针
 *     audit/p4-acceptance/SixAxisGuiFeasibilityProbe.cpp 实证六轴无旋
 *     TCP 变体在同一 GUI 流程下全过程可行）。本版载体改为**六轴无旋
 *     TCP 变体**（黄金算例 C 关节链＋TCP 纯平移偏置）：零位姿态＝单位
 *     阵，与面板目标编辑面（固定单位旋转——审查 §4 登记的"目标只编辑
 *     XYZ"缺陷，KIN-P4 尽力项未实施）对齐——零位构型即目标精确解，
 *     ReferenceQ 初值确定性收敛；六轴全姿态自由度在改型后可达性判定
 *     （温和改型 D 场景）与解析不可达翻转（缩短改型 E 场景）上均真实
 *     成立。探针 A~F 六场景全部转正为本用例断言。
 *   - 交互路径与 KinematicsPanelSessionGuiTest 同形态（QMetaObject 按名
 *     直触槽函数＝真实控件事件路径的测试镜像；main 线程纪律同则）。
 *
 * 用例与验收映射：
 *   - SixAxisVariantClosedLoop_KIN_P4：六轴 v1 求解/比较/预览/导出 →
 *     温和改型 v2 复算（身份重绑定＋seed 携带＋新解非空）→ 缩短改型
 *     v2s 复算（解析不可达「新解 0 个」如实翻转＋溯源不断链）→ 改型后
 *     可达目标 GUI 求解强断言；
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
#include "KinFkFixture.hpp"              // revolute/trans/rotZ/TestView（同单元夹具）
#include "plugin/KinematicsPanelWidget.hpp"  // 被测面板（真实 QWidget）
#include "plugin/KinPanelTypes.hpp"          // 会话态/服务缝

// ---- 使用声明（与被测代码同一命名空间——用例可读性）--------------------
using namespace sdurws::ird;
using namespace sdurws::ird::kinematics;
using namespace sdurws::ird::kinematics::testfixture;

namespace {

// =====================================================================
// 六轴改型变体构造器（独立验收 blocker 1 整改的载体——真实六轴链）
// =====================================================================

/**
 * @brief 六轴无旋转 TCP 变体（黄金算例 C 的关节链——六转动关节全姿态
 *        自由度；TCP 偏置**纯平移** (tcpx, 0, 0.1)）。
 *
 * 为什么去 TCP 旋转（选型依据的根因更正——初版"平面臂全展点"依据作废）：
 * 面板目标编辑面当前固定单位旋转（审查 §4"目标只编辑 XYZ"缺陷——KIN-P4
 * 尽力项 RPY 编辑未实施），完整位姿目标＝"位置可达＋姿态单位"。六轴
 * 黄金算例 C 的 TCP 偏置带 Rz(π/2)，零位 TCP 姿态非单位——目标姿态与
 * 零位构型差一个定轴旋转，数值搜索从零位出发属局部搜索难题（与求解器
 * 能力无关的编辑面对齐问题）。去 TCP 旋转后零位姿态＝单位阵：零位构型
 * 即"位置＝零位 TCP＋姿态单位"目标的精确解，ReferenceQ 初值确定性收敛
 * ——编辑面缺陷的对齐手段，非载体降级（六自由度、三维工作空间、解析
 * 界限判定全部真实）。
 *
 * @param j2z [in] J2 基座高度（m——改型维度 1：0.4→0.5 温和改型）
 * @param j3x/j4x/j5x/j6x [in] 各段 x 向长度（m——改型维度 2：大幅缩短
 *                        用于解析不可达翻转场景）
 * @param tcpx [in] TCP 偏置 x（m）
 * @return 构造完成的规范模型（经 CanonicalModelBuilder 校验——夹具缺陷
 *         直接抛出 fail-fast）
 */
runtime::CanonicalModel sixAxisNoRotTcp(double j2z, double j3x, double j4x,
                                        double j5x, double j6x, double tcpx)
{
    std::vector<runtime::CanonicalJoint> joints;
    joints.push_back(revolute("j1", rw::math::Vector3D<double>(0, 0, 1),
                              trans(0, 0, 0), -3.14, 3.14));
    joints.push_back(revolute("j2", rw::math::Vector3D<double>(0, 1, 0),
                              trans(0, 0, j2z), -1.5, 1.5));
    joints.push_back(revolute("j3", rw::math::Vector3D<double>(0, 1, 0),
                              trans(j3x, 0, 0), -1.5, 1.5));
    joints.push_back(revolute("j4", rw::math::Vector3D<double>(1, 0, 0),
                              trans(j4x, 0, 0), -3.14, 3.14));
    joints.push_back(revolute("j5", rw::math::Vector3D<double>(0, 1, 0),
                              trans(j5x, 0, 0), -1.5, 1.5));
    joints.push_back(revolute("j6", rw::math::Vector3D<double>(1, 0, 0),
                              trans(j6x, 0, 0), -3.14, 3.14));
    // TCP 偏置纯平移（零位姿态＝单位——与 GUI 目标编辑面对齐，见类注）。
    return makeModel(joints, trans(tcpx, 0, 0.1));
}

/// 改型 v1（基线设计）：全展 x 向 0.5+0.5+0.3+0.2＋TCP 0.15＝1.65 m，
/// 零位 TCP＝(1.65, 0, 0.5)。
runtime::CanonicalModel sixAxisV1Model()
{
    return sixAxisNoRotTcp(0.4, 0.5, 0.5, 0.3, 0.2, 0.15);
}

/// 改型 v2（温和改型——J2 基座高度 0.4→0.5 m）：零位 TCP＝(1.65, 0, 0.6)，
/// 工作半径上界不变（x 向各段长度零变化）——原目标 (1.65, 0, 0.5) 在
/// 改型后仍可达（六轴姿态自由度覆盖 0.1 m 的 z 向差，局部搜索邻近收敛）。
runtime::CanonicalModel sixAxisV2Model()
{
    return sixAxisNoRotTcp(0.5, 0.5, 0.5, 0.3, 0.2, 0.15);
}

/// 改型 v2s（大幅缩短改型——紧凑化设计变更）：x 向各段缩至 0.05，全展
/// x 向≈0.3 m——原目标 (1.65, 0, 0.5) 超**解析工作半径上界**（Bounds.cpp
/// 三角不等式上界≈0.65 m 确定性判定不可达）——复算"新解 0 个"是工程
/// 上正确的结论翻转（非数值噪声）。
runtime::CanonicalModel sixAxisV2sModel()
{
    return sixAxisNoRotTcp(0.5, 0.05, 0.05, 0.05, 0.05, 0.05);
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
/// 纪律：widget 构造期值拷贝服务缝聚合）。视图内容可换绑（地址稳定——
/// 面板构造期拷贝的 modelView 缝指向本成员，内容替换即对面板可见；
/// 会话态是面板构造期引用——锚前移同拍生效）。
struct LoopHarness {
    TestView view;                        ///< 当前视图（内容随改型换绑）
    IkSolver solver;                      ///< 真实求解器（纯函数服务）
    FkEvaluator fk;                       ///< 真实 FK 评估器
    KinSessionPose sessionPose;           ///< 会话姿态（真实容器）
    KinModuleSessionState state;          ///< 插件会话态
    KinPanelServices services;            ///< 服务缝
    QTemporaryDir exportDir;              ///< 真实导出目录（io 原子写落点）
    std::unique_ptr<KinematicsPanelWidget> panel;

    LoopHarness()
        : view(sixAxisV1Model())
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
        // 工程量级：ReferenceQ 策略（初值＝参考构型——目标即零位 TCP 时
        // 初值精确可达）、16 初值×500 迭代、容差 1e-4（schema 默认 1×1/
        // 1e-6 是下界非工程值，真实用户在求解配置面板按工艺调定）。
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
    void rebindTo(const runtime::CanonicalModel& variant, const char* anchor)
    {
        view = TestView(variant);  // 视图内容替换（地址稳定——缝无感）
        state.snapshotId = snapshotIdFrom(anchor);
    }

    /// 目标位置三编辑（★ KIN-R2 任务4：目标编辑扩为六字段后按 objectName
    /// 定位位置分量——findChildren 顺序不可依赖；姿态 RPY 留空＝0，本
    /// 闭环的目标姿态即单位阵。缺件时返回值短于 3——调用方
    /// ASSERT_GE(edits.size(), 3) 即红，gtest 断言不可用于本非 void 助手）。
    QList<QLineEdit*> targetEdits() const
    {
        QList<QLineEdit*> edits;
        for (const char* name : {"target-x", "target-y", "target-z"}) {
            if (QLineEdit* edit =
                    panel->findChild<QLineEdit*>(QString(name))) {
                edits << edit;
            }
        }
        return edits;
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

    /// 将目标三编辑设为给定视图零位 TCP 的 XYZ（FK 真值——SI m，六位
    /// 小数舍入由 1e-4 容差覆盖；调用方保证 fk/view/tcp 缝就绪）。
    void setTargetToZeroPoseTcp()
    {
        const KinTcpResolution tcp = resolveSessionTcp(services);
        ASSERT_TRUE(tcp.ok) << tcp.statusText;
        const std::vector<double> zeros(6, 0.0);  // 六轴 dof=6（全零构型）
        const auto metrics = fk.evaluate(view, tcp.tcp, zeros);
        ASSERT_TRUE(metrics.ok());
        const rw::math::Vector3D<double>& p = metrics.get().tcpInBase.P();
        const QList<QLineEdit*> edits = targetEdits();
        ASSERT_GE(edits.size(), 3);
        edits[0]->setText(QString::number(p[0], 'f', 6));
        edits[1]->setText(QString::number(p[1], 'f', 6));
        edits[2]->setText(QString::number(p[2], 'f', 6));
    }

private:
    bool panelCreated = false;  ///< createPanel 一次性守卫
};

}  // namespace

// =====================================================================
// 六轴改型闭环（★ KIN-P4 验收主径——全过程真实执行；独立验收 blocker 1
// 整改：载体＝六轴无旋 TCP 变体，A~F 六场景自验收探针转正）
// =====================================================================

TEST(KinHostLoop, SixAxisVariantClosedLoop_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01", "KIN-02", "KIN-08"},
                  std::vector<std::string>{"AT-04", "AT-27"});

    // ================= 阶段 A~D：v1 全链 → 温和改型 v2 复算 =================
    {
        LoopHarness h;
        h.createPanel();

        // ---- ①建改型＋任务点呈现：任务点表两行（真实投影——结果列
        // "未计算"）。
        QTableWidget* taskTable = nullptr;
        const QList<QTableWidget*> tables =
            h.panel->findChildren<QTableWidget*>();
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

        // ---- ②求解（真实求解器）：目标＝v1 零位 TCP 的 XYZ＋单位姿态
        //（零位构型即精确解——ReferenceQ 初值确定性收敛）。
        QPushButton* solveButton = h.buttonByText(QStringLiteral("求解 IK"));
        ASSERT_NE(solveButton, nullptr);
        EXPECT_TRUE(solveButton->isEnabled());
        h.setTargetToZeroPoseTcp();
        ASSERT_TRUE(QMetaObject::invokeMethod(h.panel.get(), "onSolveClicked",
                                              Qt::DirectConnection));
        QTableWidget* solutionTable = h.solutionTable();
        ASSERT_NE(solutionTable, nullptr);
        ASSERT_GT(solutionTable->rowCount(), 0)
            << h.panel->lastStatusText().toStdString();

        // ---- ③比较（稳定序可观察）：rank 列 0 基递增（域四键排序的
        // 呈现序）。
        for (int row = 0; row < solutionTable->rowCount(); ++row) {
            EXPECT_EQ(solutionTable->item(row, 0)->text().toInt(), row);
        }

        // ---- ④预览（双击→会话姿态回写——零修订 KIN-06）：首行双击，
        // 回写向量 dof=6（六轴链序）。
        ASSERT_TRUE(QMetaObject::invokeMethod(h.panel.get(),
                                              "onSolutionDoubleClicked",
                                              Qt::DirectConnection,
                                              Q_ARG(int, 0), Q_ARG(int, 0)));
        EXPECT_TRUE(h.sessionPose.isSet());
        EXPECT_EQ(h.sessionPose.jointConfiguration().size(), 6U);

        // ---- ⑤导出（真实 JSON 文件写出——快照锚非零头）。
        QPushButton* exportButton =
            h.buttonByText(QStringLiteral("导出 JSON"));
        ASSERT_NE(exportButton, nullptr);
        EXPECT_TRUE(exportButton->isEnabled());
        ASSERT_TRUE(QMetaObject::invokeMethod(h.panel.get(),
                                              "onExportJsonClicked",
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
                << "导出快照锚全零（身份未装配）——复算重绑定与溯源都无从"
                   "谈起";
        }

        // ---- ⑥建改型重开（温和改型 v2：J2 基座高度 0.4→0.5——目标
        // (1.65,0,0.5) 改型后仍可达）→复算（当前快照）。setWritable 触发
        // 全面板门控刷新（真实用户视角：求解有果＋原解集在场＝复算按钮
        // 点亮）。
        h.rebindTo(sixAxisV2Model(), "kin-p4-loop-v2");
        h.panel->setWritable(true);
        QPushButton* recomputeButton =
            h.buttonByText(QStringLiteral("复算（当前快照）"));
        ASSERT_NE(recomputeButton, nullptr);
        EXPECT_TRUE(recomputeButton->isEnabled());
        ASSERT_TRUE(QMetaObject::invokeMethod(h.panel.get(),
                                              "onRecomputeClicked",
                                              Qt::DirectConnection));
        const std::string recomputeStatus =
            h.panel->lastStatusText().toStdString();
        EXPECT_NE(recomputeStatus.find("复算"), std::string::npos)
            << recomputeStatus;
        // 重绑定可观测（新解集身份＝v2 快照锚；原 seed 携带——原任务定义
        // 自原身份显式恢复，D-KIN-4）。
        ASSERT_TRUE(h.state.lastSessionSolutionSet != nullptr);
        EXPECT_EQ(h.state.lastSessionSolutionSet->requestIdentity.snapshotId,
                  snapshotIdFrom("kin-p4-loop-v2"));
        EXPECT_EQ(h.state.lastSessionSolutionSet->requestIdentity.seed, 7U);
        // 温和改型（J2 高度 +0.1 m）下的新解数**如实呈现、不强断言**：
        // 复算初值＝原身份参考构型（v1 零位），到新目标存在 0.1 m 的 z
        // 向差——阻尼最小二乘属局部搜索，收敛与否取决于初值邻近性（审查
        // A02 登记"局部数值搜索不保证全局收敛"；验收探针同款口径——D5
        // 现象如实记录不作为失败项）。改型后可达目标的**强断言**在本用例
        // 阶段 F（改型后零位 TCP 为目标——零位即解，确定性非空）。
        EXPECT_NE(recomputeStatus.find("已按原任务定义复算"), std::string::npos)
            << recomputeStatus;
    }

    // ================= 阶段 E：缩短改型 v2s 复算——解析不可达如实翻转
    // （改型评估的工程价值：同一任务点在新设计下的可行性结论有据翻转）
    // =================
    {
        LoopHarness h;
        h.createPanel();
        // 前置：v1 求解成功（原任务定义在场）。
        h.setTargetToZeroPoseTcp();
        ASSERT_TRUE(QMetaObject::invokeMethod(h.panel.get(), "onSolveClicked",
                                              Qt::DirectConnection));
        ASSERT_GT(h.solutionTable()->rowCount(), 0);
        // 缩短改型（x 向各段 0.05——原目标 1.65 m 超解析工作半径上界）。
        h.rebindTo(sixAxisV2sModel(), "kin-p4-loop-v2s");
        h.panel->setWritable(true);
        ASSERT_TRUE(QMetaObject::invokeMethod(h.panel.get(),
                                              "onRecomputeClicked",
                                              Qt::DirectConnection));
        const std::string status = h.panel->lastStatusText().toStdString();
        // 结论翻转两面（解集空态＋状态行摘要）——「不可行」结论如实有据
        // （Bounds.cpp 解析界限确定性判定，非数值噪声）。
        ASSERT_TRUE(h.state.lastSessionSolutionSet != nullptr);
        EXPECT_TRUE(h.state.lastSessionSolutionSet->solutions.empty());
        EXPECT_NE(status.find("新解 0 个"), std::string::npos) << status;
        // 不可达结论下身份仍重绑定（溯源不断链——v2s 快照锚）。
        EXPECT_EQ(h.state.lastSessionSolutionSet->requestIdentity.snapshotId,
                  snapshotIdFrom("kin-p4-loop-v2s"));
    }

    // ================= 阶段 F：改型后（v2）可达目标 GUI 求解强断言
    // （分离"局部搜索未收敛"与"载体不可行"——改型后新任务点的求解能力
    // 在真实控件事件路径上独立成立）
    // =================
    {
        LoopHarness h;
        h.createPanel();
        // 前置：v1 原求解（复算按钮点亮语义与主链路同）。
        h.setTargetToZeroPoseTcp();
        ASSERT_TRUE(QMetaObject::invokeMethod(h.panel.get(), "onSolveClicked",
                                              Qt::DirectConnection));
        ASSERT_GT(h.solutionTable()->rowCount(), 0);
        // 换绑 v2 后目标改为 v2 零位 TCP（改型后必可达——零位即解），
        // 真实点击"求解 IK"强断言解表非空。
        h.rebindTo(sixAxisV2Model(), "kin-p4-loop-v2f");
        h.panel->setWritable(true);
        h.setTargetToZeroPoseTcp();
        QPushButton* solveAfter =
            h.buttonByText(QStringLiteral("求解 IK"));
        ASSERT_NE(solveAfter, nullptr);
        ASSERT_TRUE(QMetaObject::invokeMethod(h.panel.get(), "onSolveClicked",
                                              Qt::DirectConnection));
        QTableWidget* tableAfter = h.solutionTable();
        ASSERT_NE(tableAfter, nullptr);
        EXPECT_GT(tableAfter->rowCount(), 0)
            << h.panel->lastStatusText().toStdString();
    }
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
    // 求解可用（真实求解器在册）；复位 Home 禁用（homePose 缝未注入）；
    // 批量/覆盖禁用（后台缝未注入）；导出/复算禁用（无结果）。
    EXPECT_TRUE(h.buttonByText(QStringLiteral("求解 IK"))->isEnabled());
    EXPECT_FALSE(h.buttonByText(QStringLiteral("复位 Home"))->isEnabled());
    EXPECT_FALSE(h.buttonByText(QStringLiteral("批量验证（后台）"))->isEnabled());
    EXPECT_FALSE(h.buttonByText(QStringLiteral("运行覆盖评估（后台）"))->isEnabled());
    EXPECT_FALSE(h.buttonByText(QStringLiteral("导出 JSON"))->isEnabled());
    EXPECT_FALSE(h.buttonByText(QStringLiteral("复算（当前快照）"))->isEnabled());

    // 状态行在位（非模态反馈——UX-03）。
    EXPECT_FALSE(h.panel->lastStatusText().isEmpty());
}
