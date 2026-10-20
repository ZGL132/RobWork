/**
 * @file KinematicsConfigAcceptanceGuiTest.cpp
 * @brief WP-15-T20 F04：配置整体确认、持久化失败和重开行为的窗口反例。
 *
 * 设计依据：KIN-13、UX-05/07、AT-27。仅在既有configPersist写端口注入
 * 故障；合法路径真实使用配置canonical编解码与临时QSettings文件。
 * 本文件证明运动学面板/编排，不将临时存储适配器当正式宿主装配验收。
 * 全部对象由夹具持有，面板先析构；仅UI线程运行，不访问其他工作树。
 */
#include <gtest/gtest.h>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <memory>
#include <stdexcept>
#include "plugin/KinematicsPanelWidget.hpp"

using namespace sdurws::ird;
using namespace sdurws::ird::kinematics;

namespace {
/**
 * @brief 拥有配置窗口和临时用户级存储的夹具；非线程安全，仅UI线程使用。
 * 写端口统计调用用于证明取消/非法/过期确认零写入，不替换配置计算逻辑。
 */
struct ConfigWindow {
    QTemporaryDir store;
    KinModuleSessionState state;
    KinPanelServices services;
    int writes = 0;
    bool reject = false;
    std::unique_ptr<KinematicsConfigPanel> panel;

    /** @brief 建立非默认数值基线与真实临时存储；不产生项目修订。 */
    explicit ConfigWindow(bool withStorage = true)
    {
        state.savedConfig.initialStrategy = InitialValueStrategy::JointGrid;
        state.savedConfig.initialValuesCount = 4;
        state.savedConfig.iterationLimit = 256;
        state.savedConfig.seed = 7;
        state.savedConfig.regionBudget.seed = 7;
        if (withStorage) {
            services.configPersist = [this](const AnalysisConfiguration& config) {
                ++writes;
                if (reject || !store.isValid()) { return false; }
                const auto bytes = AnalysisConfigurationCodec{}.encode(config);
                const QByteArray raw(reinterpret_cast<const char*>(bytes.data()),
                                     static_cast<qsizetype>(bytes.size()));
                QSettings settings(store.path() + "/config.ini", QSettings::IniFormat);
                settings.setValue("config.ik", raw.toBase64());
                settings.sync();
                return settings.status() == QSettings::NoError;
            };
        }
        panel = std::make_unique<KinematicsConfigPanel>(services, state);
        panel->resize(1000, 900);
        panel->show();
        QApplication::processEvents();
    }

    /** @brief 找策略下拉，返回面板持有的借用指针；缺件返回空。 */
    QComboBox* strategy() const
    {
        for (auto* box : panel->findChildren<QComboBox*>()) {
            if (box->findText(QStringLiteral("ReferenceQ（参考构型）")) >= 0) { return box; }
        }
        return nullptr;
    }

    /**
     * @brief 查数值行当前值项；不跨事件循环保存该借用指针。
     * @param key [in] 既有字段键；数值单位由参数表单位列决定。
     * @return 当前表项，缺字段返回空，重绘后指针失效。
     */
    QTableWidgetItem* value(const char* key) const
    {
        auto* table = panel->findChild<QTableWidget*>("ird_param_table");
        if (table == nullptr) { return nullptr; }
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->item(row, 0)->data(Qt::UserRole).toString() == key) {
                return table->item(row, 1);
            }
        }
        return nullptr;
    }

    /**
     * @brief 编辑真实表项并处理公共参数表的延迟解析/重绘。
     * @param key [in] 字段键；@param text [in] 当前显示单位下的用户文本。
     */
    void edit(const char* key, const QString& text)
    {
        ASSERT_NE(value(key), nullptr);
        value(key)->setText(text);
        QApplication::processEvents();
    }

    /**
     * @brief 通过稳定锚向按钮分发真实按下/释放事件。
     * @param name [in] 按钮对象名；按钮由窗口持有，缺件立即测试失败。
     */
    void click(const char* name)
    {
        auto* target = panel->findChild<QPushButton*>(QString::fromUtf8(name));
        ASSERT_NE(target, nullptr);
        ASSERT_TRUE(target->isVisible()) << name;
        const QPointF p(target->rect().center());
        const QPointF global(target->mapToGlobal(p.toPoint()));
        QMouseEvent press(QEvent::MouseButtonPress, p, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, p, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(target, &release);
        QApplication::processEvents();
    }

    /** @brief 保存本面板截图；未配证据目录不写，写失败测试失败。 */
    void capture() const
    {
        const QString out = qEnvironmentVariable("IRD_KIN_GUI_EVIDENCE_OUT");
        if (!out.isEmpty()) {
            ASSERT_TRUE(QDir().mkpath(out));
            ASSERT_TRUE(panel->grab().save(out + "/config-confirmation.png"));
        }
    }

    /** @brief 从真实临时文件重开canonical配置；格式异常原样抛给测试。 */
    AnalysisConfiguration reopen() const
    {
        QSettings settings(store.path() + "/config.ini", QSettings::IniFormat);
        const QByteArray raw = QByteArray::fromBase64(settings.value("config.ik").toByteArray());
        return AnalysisConfigurationCodec{}.decode(
            std::vector<std::uint8_t>(raw.begin(), raw.end()));
    }
};
} // namespace

/// K55：表单首次显示必须已是保存基线，不能等用户筛选后才从“未设”变真值。
TEST(KinConfigAcceptance, InitialFieldsShowSavedBaseline_KIN13)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13"}, std::vector<std::string>{"AT-27"});
    ConfigWindow h;
    ASSERT_NE(h.value("iteration-limit"), nullptr);
    bool parsed = false;
    EXPECT_DOUBLE_EQ(h.value("iteration-limit")->text().toDouble(&parsed), 256.0);
    EXPECT_TRUE(parsed);
}

/// K55/56：只改枚举也必须进入整体确认并真实保存；不能依赖数值字段“搭车”。
TEST(KinConfigAcceptance, StrategyOnlySavesAndReopens_AT27)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13", "UX-07"}, std::vector<std::string>{"AT-27"});
    ConfigWindow h;
    ASSERT_NE(h.strategy(), nullptr);
    h.strategy()->setCurrentIndex(0);
    h.click("ird_param_apply");
    h.capture();
    auto* confirm = h.panel->findChild<QWidget*>("ird_param_confirm_region");
    ASSERT_NE(confirm, nullptr);
    ASSERT_TRUE(confirm->isVisible()) << "策略单独修改也应有确认区";
    auto* text = h.panel->findChild<QLabel*>("ird_param_confirm_text");
    ASSERT_NE(text, nullptr);
    EXPECT_TRUE(text->text().contains("ReferenceQ"));
    EXPECT_TRUE(text->text().contains("JointGrid"));
    EXPECT_EQ(h.writes, 0);
    h.click("ird_param_confirm_yes");
    ASSERT_EQ(h.writes, 1);
    EXPECT_EQ(h.state.savedConfig.initialStrategy, InitialValueStrategy::ReferenceQ);
    EXPECT_EQ(h.reopen(), h.state.savedConfig);
}

/// K55：策略与数值一次确认、一次写入；重开后两项均保持，其他值不漂移。
TEST(KinConfigAcceptance, StrategyAndNumericSaveTogether_AT27)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13"}, std::vector<std::string>{"AT-27"});
    ConfigWindow h;
    h.strategy()->setCurrentIndex(1);
    h.edit("iteration-limit", "512");
    h.click("ird_param_apply");
    h.click("ird_param_confirm_yes");
    ASSERT_EQ(h.writes, 1);
    EXPECT_EQ(h.state.savedConfig.initialStrategy, InitialValueStrategy::SeededRandom);
    EXPECT_EQ(h.state.savedConfig.iterationLimit, 512U);
    EXPECT_EQ(h.reopen(), h.state.savedConfig);
}

/// K56：取消恢复整个配置草稿（含策略），不是只恢复公共表的数值半区。
TEST(KinConfigAcceptance, CancelRestoresStrategyAndNumbers_AT27)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13", "UX-05"}, std::vector<std::string>{"AT-27"});
    ConfigWindow h;
    const auto before = h.state.savedConfig;
    h.strategy()->setCurrentIndex(0);
    h.edit("iteration-limit", "512");
    h.click("ird_param_cancel");
    EXPECT_EQ(h.strategy()->currentIndex(), 2);
    EXPECT_DOUBLE_EQ(h.value("iteration-limit")->text().toDouble(), 256.0);
    EXPECT_EQ(h.state.savedConfig, before);
    EXPECT_EQ(h.writes, 0);
}

/// F04失败原子性：失败不冒充会话生效、不丢草稿；恢复存储后无需重输即可重试。
TEST(KinConfigAcceptance, PersistenceFailurePreservesDraftForRetry_AT27)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13", "NFR-COR-03"}, std::vector<std::string>{"AT-27"});
    ConfigWindow h;
    const auto before = h.state.savedConfig;
    h.reject = true;
    h.strategy()->setCurrentIndex(0);
    h.edit("iteration-limit", "512");
    h.click("ird_param_apply");
    h.click("ird_param_confirm_yes");
    EXPECT_EQ(h.writes, 1);
    EXPECT_EQ(h.state.savedConfig, before);
    EXPECT_FALSE(h.panel->lastStatusText().contains("仅在会话生效"));
    h.reject = false;
    h.click("ird_param_apply");
    h.click("ird_param_confirm_yes");
    ASSERT_EQ(h.writes, 2);
    EXPECT_EQ(h.state.savedConfig.initialStrategy, InitialValueStrategy::ReferenceQ);
    EXPECT_EQ(h.reopen().iterationLimit, 512U);
}

/// UX-07：确认区显示后再修改输入，旧确认不能授权用户没看到的另一批值。
TEST(KinConfigAcceptance, ChangedDraftRequiresNewConfirmation_UX07)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13", "UX-07"}, std::vector<std::string>{"AT-27"});
    ConfigWindow h;
    h.edit("iteration-limit", "512");
    h.click("ird_param_apply");
    h.edit("iteration-limit", "1024");
    h.click("ird_param_confirm_yes");
    EXPECT_EQ(h.writes, 0);
    EXPECT_EQ(h.state.savedConfig.iterationLimit, 256U);
}

/// K56/空态：缺少用户级持久化通道，保存明确禁用而编辑/取消仍可用。
TEST(KinConfigAcceptance, MissingStorageDisablesSave_KIN13)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13"}, std::vector<std::string>{"AT-27"});
    ConfigWindow h(false);
    auto* save = h.panel->findChild<QPushButton*>("ird_param_apply");
    ASSERT_NE(save, nullptr);
    EXPECT_FALSE(save->isEnabled());
}

/// K55：任何数值错误都阻断整个配置，不能只保存合法策略或其他数值。
TEST(KinConfigAcceptance, InvalidNumberBlocksWholeConfiguration_AT27)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13", "UX-05"}, std::vector<std::string>{"AT-27"});
    ConfigWindow h;
    const auto before = h.state.savedConfig;
    h.strategy()->setCurrentIndex(0);
    h.edit("seed", "0");
    h.click("ird_param_apply");
    auto* confirm = h.panel->findChild<QWidget*>("ird_param_confirm_region");
    ASSERT_NE(confirm, nullptr);
    EXPECT_FALSE(confirm->isVisible());
    EXPECT_EQ(h.writes, 0);
    EXPECT_EQ(h.state.savedConfig, before);
}

/// F04：故障在既有写端口注入，真实编排必须返回拒绝，不将异常带到Qt事件边界。
TEST(KinConfigAcceptance, StorageExceptionBecomesRejectedResult_AT27)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13", "NFR-COR-03"}, std::vector<std::string>{"AT-27"});
    ConfigWindow h;
    const auto before = h.state.savedConfig;
    KinConfigEditResult result;
    EXPECT_NO_THROW(result = applyConfigurationEdit(h.state, before,
        {{"iteration-limit", 512}}, [](const AnalysisConfiguration&) -> bool {
            throw std::runtime_error("模拟用户级存储写入异常");
        }));
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.reason.empty());
    EXPECT_EQ(h.state.savedConfig, before);
}
