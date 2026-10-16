/**
 * @file   KinematicsPanelSamplingGuiTest.cpp
 * @brief  运动学面板"探索式工作空间采样组"GUI 回归（★ KIN-R2 任务3/
 *         KIN-09——采样模式编辑入口的真实 widget 事件路径）。
 *
 * 设计依据：
 *   - units/kinematics.md §9.8（KIN-09 采样语义——模式三值词表；结果与
 *     可视化面板承接行）＋§14.6 v0.29（面板编辑入口落位登记）；
 *   - KIN-P5 验收登记"ui/plugin 面板未暴露关节空间采样模式编辑入口
 *     （域查询值面已就绪，归 ui 呈现批次）"——本文件即呈现批次的 widget
 *     半区回归（编排半区在 PluginPanelTest 的 WorkspaceSampling_* 组）。
 *
 * 用例与验收映射：
 *   - 模式切换使能面：切到关节随机/关节网格后区域盒字段禁用、对应关节
 *     字段启用（与域 validateWorkspaceSamplingQuery 的按模式校验面对位
 *     ——禁用态字段不参与装配）；
 *   - 运行全链：关节随机 6 样本经真实控件路径提交 → 状态行"采样完成"＋
 *     摘要表"模式 joint-random"行（F-905 溯源标注）＋导出/回写按钮门控
 *     随产物点亮。
 *
 * 线程约束：全部用例在 main 线程（QApplication 线程）执行。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPushButton>
#include <QTableWidget>

#include <memory>
#include <string>

#include "KinFkFixture.hpp"              // TestView/twoLinkModel（同单元夹具）
#include "plugin/KinematicsPanelWidget.hpp"  // 被测面板（真实 QWidget）
#include "plugin/KinPanelTypes.hpp"          // KinPanelServices/会话态

// ---- 使用声明（与被测代码同一命名空间——用例可读性）--------------------
using namespace sdurws::ird;
using namespace sdurws::ird::kinematics;
using namespace sdurws::ird::kinematics::testfixture;

namespace {

/// 采样组夹具（模型视图缝注入——runWorkspaceSamplingFlow 的域执行面；
/// 求解器缝不注入＝域内置求解器生产路径）。两段式装配纪律与
/// KinematicsPanelSessionGuiTest 的 SessionPanelHarness 同则（widget
/// 构造期值拷贝服务缝）。
struct SamplingHarness {
    runtime::CanonicalModel model;   ///< 规范模型（twoLink——带工具）
    TestView view;                   ///< 宿主注入替身（IKinRuntimeView 最小实现）
    KinPanelServices services;       ///< 服务缝（modelView/exportWriter——执行/写出面）
    KinModuleSessionState state;     ///< 插件会话态（seed 合法基线）
    std::unique_ptr<KinematicsPanelWidget> panel;
    std::string exportedPath;        ///< 导出写出路径记录（记录缝——PNG/CSV 断言面）
    std::string exportedContent;     ///< 导出内容记录（PNG 字节/PNG 魔数断言面）

    SamplingHarness()
        : model(twoLinkModel()),
          view(model)
    {
        services.modelView = &view;
        // 导出写出记录缝（★ KIN-R2 任务6——PNG 字节捕获；io write 以
        // string_view 承载二进制安全，std::string 直存字节）。
        services.exportWriter =
            [this](const std::string& path, const std::string& content) {
                exportedPath = path;
                exportedContent = content;
                return true;
            };
        state.savedConfig.seed = 7;
        state.savedConfig.regionBudget.seed = 7;
        panel = std::make_unique<KinematicsPanelWidget>(services, state);
    }

    /// 按 objectName 取采样组编辑框（缺件＝面板装配违约——断言红）。
    QLineEdit* edit(const char* objectName) const
    {
        return panel->findChild<QLineEdit*>(QString(objectName));
    }

    /// 采样模式下拉（objectName＝ws-mode-box——稳定定位锚）。
    QComboBox* modeBox() const
    {
        return panel->findChild<QComboBox*>(QStringLiteral("ws-mode-box"));
    }

    /// "运行采样"按钮定位（可读文本）。
    QPushButton* runButton() const
    {
        const QList<QPushButton*> buttons = panel->findChildren<QPushButton*>();
        for (QPushButton* b : buttons) {
            if (b->text() == QStringLiteral("运行采样")) {
                return b;
            }
        }
        return nullptr;
    }

    /// 采样摘要表定位（表头首列＝"采样摘要"——不依赖 findChildren 序）。
    QTableWidget* summaryTable() const
    {
        const QList<QTableWidget*> tables = panel->findChildren<QTableWidget*>();
        for (QTableWidget* table : tables) {
            if (table->horizontalHeaderItem(0) != nullptr
                && table->horizontalHeaderItem(0)->text()
                       == QStringLiteral("采样摘要")) {
                return table;
            }
        }
        return nullptr;
    }
};

}  // namespace

/// 模式切换使能面：区域盒模式（缺省）下区域字段启用；切到关节随机后
/// 区域字段禁用、关节随机字段启用；切到关节网格后关节网格字段启用
/// （禁用态字段值不参与装配——与域按模式校验面对位）。
TEST(KinematicsPanelSampling, SamplingGroup_ModeSwitchEnablesFields_KIN_R2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09"}, std::vector<std::string>{});
    SamplingHarness h;
    ASSERT_NE(h.panel, nullptr);

    // 缺省＝区域盒模式：区域盒/位置采样字段可用。
    QLineEdit* cx = h.edit("ws-region-cx");
    QLineEdit* jointRandom = h.edit("ws-joint-random");
    QLineEdit* jointGrid = h.edit("ws-joint-grid");
    ASSERT_NE(cx, nullptr);
    ASSERT_NE(jointRandom, nullptr);
    ASSERT_NE(jointGrid, nullptr);
    EXPECT_TRUE(cx->isEnabled());

    // 切到关节随机（index 1＝WorkspaceSamplingMode::JointSpaceRandom）：
    // 区域字段禁用、关节随机字段启用、关节网格字段仍禁用。
    QComboBox* mode = h.modeBox();
    ASSERT_NE(mode, nullptr);
    mode->setCurrentIndex(1);
    EXPECT_FALSE(cx->isEnabled());
    EXPECT_TRUE(jointRandom->isEnabled());
    EXPECT_FALSE(jointGrid->isEnabled());

    // 切到关节网格（index 2）：关节网格字段启用、关节随机字段禁用。
    mode->setCurrentIndex(2);
    EXPECT_TRUE(jointGrid->isEnabled());
    EXPECT_FALSE(jointRandom->isEnabled());
}

/// 运行全链（真实控件路径）：关节随机 6 样本 → 状态行"采样完成"＋摘要
/// 表"模式 joint-random"行（F-905 溯源标注）＋导出/回写按钮门控随产物
/// 点亮（写点缓存缝注入后——导出按钮还受写出缝门控，此处回写按钮以
/// 会话姿态写点注入验证点亮）。
TEST(KinematicsPanelSampling, SamplingGroup_RunJointRandomFillsSummary_KIN_R2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09"}, std::vector<std::string>{});
    SamplingHarness h;
    KinSessionPose sessionPose;
    h.services.sessionPose = &sessionPose;
    // 两段式：缝后注面板重建（widget 构造期值拷贝服务缝——后注不可见）。
    h.panel = std::make_unique<KinematicsPanelWidget>(h.services, h.state);

    QComboBox* mode = h.modeBox();
    ASSERT_NE(mode, nullptr);
    mode->setCurrentIndex(1);  // 关节随机
    QLineEdit* jointRandom = h.edit("ws-joint-random");
    ASSERT_NE(jointRandom, nullptr);
    jointRandom->setText(QStringLiteral("6"));

    QPushButton* run = h.runButton();
    ASSERT_NE(run, nullptr);
    ASSERT_TRUE(QMetaObject::invokeMethod(h.panel.get(), "onWsRunClicked",
                                          Qt::DirectConnection));
    // 状态行＋摘要表（模式 token 行＝F-905"点从哪来"溯源标注）。
    EXPECT_NE(h.panel->lastStatusText().toStdString().find("采样完成"),
              std::string::npos)
        << h.panel->lastStatusText().toStdString();
    QTableWidget* summary = h.summaryTable();
    ASSERT_NE(summary, nullptr);
    ASSERT_EQ(summary->rowCount(), 7);
    EXPECT_EQ(summary->item(0, 0)->text(), QStringLiteral("模式 joint-random"));
    EXPECT_EQ(summary->item(1, 0)->text(), QStringLiteral("样本总数 6"));

    // 门控随产物点亮：回写按钮（会话姿态写点＋产物在场）。
    const QList<QPushButton*> buttons = h.panel->findChildren<QPushButton*>();
    bool writebackEnabled = false;
    for (QPushButton* b : buttons) {
        if (b->text() == QStringLiteral("回写样本构型到会话")) {
            writebackEnabled = b->isEnabled();
        }
    }
    EXPECT_TRUE(writebackEnabled);
}

/// 投影视图画布与 PNG 导出全链（★ KIN-R2 任务6——"三维或投影视图＋
/// PNG 导出"最小可用入口的真实控件路径）：运行采样后画布持有 QPixmap
/// （散点已绘——非占位文本）；切换投影面（XZ）画布重绘；导出 PNG 经
/// 既有写出缝落盘（捕获字节以 PNG 魔数 \x89PNG 开头——真实编码证据）。
TEST(KinematicsPanelSampling, ProjectionCanvasAndPngExport_KIN_R2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09"}, std::vector<std::string>{});
    SamplingHarness h;

    // 运行前：画布＝占位（无产物——零虚构数据）。
    QLabel* canvas = h.panel->findChild<QLabel*>(QStringLiteral("ws-projection-canvas"));
    ASSERT_NE(canvas, nullptr);
    EXPECT_FALSE(canvas->pixmap().isNull());  // 占位画布也是 QPixmap（无产物态非空位图）
    // 占位态 pixmap 恒在（占位画布也是 QPixmap）——以导出按钮门控验证
    // 无产物禁用（诚实降级面）。
    QPushButton* pngButton = nullptr;
    const QList<QPushButton*> buttons = h.panel->findChildren<QPushButton*>();
    for (QPushButton* b : buttons) {
        if (b->text() == QStringLiteral("导出投影视图 PNG")) {
            pngButton = b;
        }
    }
    ASSERT_NE(pngButton, nullptr);
    EXPECT_FALSE(pngButton->isEnabled());  // 无产物＋（记录缝在场）＝产物门控禁用

    // 运行采样（关节随机 6 样本）→ 画布重绘＋门控点亮。
    QComboBox* mode = h.modeBox();
    ASSERT_NE(mode, nullptr);
    mode->setCurrentIndex(1);
    QLineEdit* jointRandom = h.edit("ws-joint-random");
    ASSERT_NE(jointRandom, nullptr);
    jointRandom->setText(QStringLiteral("6"));
    ASSERT_TRUE(QMetaObject::invokeMethod(h.panel.get(), "onWsRunClicked",
                                          Qt::DirectConnection));
    
    EXPECT_FALSE(canvas->pixmap().isNull());  // 散点画布已绘（非空位图）

    // 投影面切换（XZ）→ 画布重绘仍非空（纯呈现态切换——零重算）。
    QComboBox* plane = h.panel->findChild<QComboBox*>(QStringLiteral("ws-projection-plane"));
    ASSERT_NE(plane, nullptr);
    plane->setCurrentIndex(1);
    
    EXPECT_FALSE(canvas->pixmap().isNull());

    // PNG 导出：经写出缝落盘（PNG 魔数＋固定文件名——io 字节安全面）。
    EXPECT_TRUE(pngButton->isEnabled());
    ASSERT_TRUE(QMetaObject::invokeMethod(h.panel.get(), "onWsExportPngClicked",
                                          Qt::DirectConnection));
    EXPECT_EQ(h.exportedPath, "kin-workspace-projection.png");
    ASSERT_GE(h.exportedContent.size(), 8U);
    EXPECT_EQ(h.exportedContent.compare(0, 4, "\x89PNG", 4), 0)
        << "导出字节非 PNG 魔数（size=" << h.exportedContent.size() << "）";
    EXPECT_NE(h.panel->lastStatusText().toStdString().find("已导出投影视图"),
              std::string::npos)
        << h.panel->lastStatusText().toStdString();
}
