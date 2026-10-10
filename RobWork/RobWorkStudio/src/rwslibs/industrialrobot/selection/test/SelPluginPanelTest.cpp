/**
 * @file   SelPluginPanelTest.cpp
 * @brief  WP-19-T10 用例组——界面链路模型层（SelPanelWorkflow/
 *         SelPanelCatalog/SelPanelCandidates/SelPanelAssembly/
 *         SelPanelText/SelPanelGui 六组）：就绪投影合成、目录行集归一、
 *         候选/原因/缺口行集透传与分轨、回填提交三态与复算提示素材、
 *         范围外呈现键双面一致、装配登记面值断言与文案解析流（任务
 *         契约 acceptance 1/2 的执行证明面）。
 *
 * 设计依据：
 *   - units/selection.md §16（WP-19-T10 行——工作流页/目录管理/候选表
 *     的界面交付面）、§3.4（插件零计算红线——模型层零判定零排序）、
 *     §2.3（NFR-PERF-01——UI 线程不执行计算：行集缝现取）、§15.3
 *     （GUI 流程约定——本目标零 GUI 呈现用例，harness 通道承载，
 *     envUnavailable 如实登记）、wp19-t08 登记（格 note 与组合级缺口
 *     双面一致——呈现键唯一承载）；
 *   - 先例：dynamics/test/DynPluginPanelTest.cpp（模型层范式——不启动
 *     GUI；GUI 呈现另行 envUnavailable 登记——WP-17-T09）；
 *   - 需求 UX-02（工程用语/零哈希进用户文本）、UX-10（七态素材呈现）、
 *     SEL-06（逐项淘汰原因呈现）、AT-30（回填复算提示素材）；任务契约
 *     tasks/foundation/WP-19-T10.json acceptance 1/2
 *
 * 测试范围声明：GUI 呈现（widget 渲染/交互点击）不在本目标——无人值
 * 守门禁不做 GUI 运行验证（既有环境事实），呈现归
 * sdurws_ird_selection_app harness 的手动验证留痕（§15.3），本目标内以
 * GuiPresentation 用例 envUnavailable 登记（AGENTS §4.2）。插件面零
 * 计算红线（acceptance 2 的词表扫描半区）归契约测试
 * SelPluginAssemblyContractTest（全文扫描 plugin/＋assembly/）。
 *
 * 数值对照口径（附录 D）：黄金值全部为缝演示行直拷或一次拼接式（投影
 * ＝行原值透传，无浮点递推——期望值手工可读，词面/文本逐字精确比对，
 * 数值经 %g 文本形态逐字断言）；产品代码零自设阈值（P-SEL 纪律不因本
 * 任务引入）。
 */

#include <sdurws/ird/selection/SelectionPluginAssembly.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "SelPanelCommandCatalog.hpp" // 命令目录/域注册键（plugin 私有头）
#include "SelPanelModel.hpp"          // L-S1~L-S5 模型层流（被测主面）
#include "SelPanelModule.hpp"         // SelPanelModule（缝/会话态归属——完整型）
#include "SelPanelTypes.hpp"          // 服务缝/会话态/呈现 DTO

using namespace sdurws::ird::selection;

// 模型层流与词表（被测主面——具名引入，与被测头同一命名空间可读性）。
using sdurws::ird::selection::kSelBackfillUiCommandId;
using sdurws::ird::selection::kSelVerdictKeyDataInsufficient;
using sdurws::ird::selection::kSelVerdictKeyFeasible;
using sdurws::ird::selection::kSelVerdictKeyRejected;
using sdurws::ird::selection::candidateRowsPresented;
using sdurws::ird::selection::catalogDisplayText;
using sdurws::ird::selection::catalogRowsPresented;
using sdurws::ird::selection::formatMetric;
using sdurws::ird::selection::gapRowsPresented;
using sdurws::ird::selection::readinessProjection;
using sdurws::ird::selection::rejectionRowsPresented;
using sdurws::ird::selection::resolvePanelText;
using sdurws::ird::selection::selOutOfScopePresentationKey;
using sdurws::ird::selection::submitBackfill;

namespace {

// =====================================================================
// 黄金缝数据（固定演示行集——零业务语义，全部值手工可读；六缝按需
// 组装，空缝用例不注入对应缝）。
// =====================================================================

/// 黄金目录行（两条——当前锁定＋历史版本）。
std::vector<SelCatalogRow> goldenCatalogRows()
{
    std::vector<SelCatalogRow> rows;
    SelCatalogRow locked;
    locked.catalogId = "cat-a";
    locked.version = "1.0";
    locked.sourceLabelKey = "plugin.selection.catalog.source.demo";
    locked.selected = true;
    rows.push_back(locked);
    SelCatalogRow archived;
    archived.catalogId = "cat-a";
    archived.version = "0.9";
    archived.sourceLabelKey = "plugin.selection.catalog.source.demo";
    archived.selected = false;
    rows.push_back(archived);
    return rows;
}

/// 黄金候选行（三行——可行/数据不足〔范围外标注〕/淘汰各一）。
std::vector<SelCandidateRow> goldenCandidateRows()
{
    std::vector<SelCandidateRow> rows;
    SelCandidateRow feasible;
    feasible.combinationKeyLabel = "plugin.selection.candidate.combo-a";
    feasible.verdictKey = kSelVerdictKeyFeasible;
    feasible.totalMassKg = 9.0;   // kg（组合质量透传——演示值）
    feasible.hasMass = true;
    feasible.minMargin = 0.25;    // 无量纲裕量透传
    feasible.hasMargin = true;
    feasible.reasonCount = 0;
    rows.push_back(feasible);
    SelCandidateRow outOfScope;
    outOfScope.combinationKeyLabel = "plugin.selection.candidate.combo-b";
    outOfScope.verdictKey = kSelVerdictKeyDataInsufficient;
    outOfScope.hasMass = false;   // 无素材——"不适用"占位（不伪造 0）
    outOfScope.hasMargin = false;
    outOfScope.reasonCount = 0;
    outOfScope.noteKey = "axis-out-of-scope";  // 格级标注（wp19-t08）
    rows.push_back(outOfScope);
    SelCandidateRow rejected;
    rejected.combinationKeyLabel = "plugin.selection.candidate.combo-c";
    rejected.verdictKey = kSelVerdictKeyRejected;
    rejected.totalMassKg = 7.5;   // kg
    rejected.hasMass = true;
    rejected.minMargin = -0.4;    // 负裕量＝超限事实如实保留
    rejected.hasMargin = true;
    rejected.reasonCount = 1;
    rows.push_back(rejected);
    return rows;
}

/// 黄金淘汰原因行（一条——比较型字段全列）。
std::vector<SelRejectionRow> goldenRejectionRows()
{
    std::vector<SelRejectionRow> rows;
    SelRejectionRow row;
    row.reasonKey = "gearbox-peak-torque-insufficient";
    row.axisLabel = "plugin.selection.axis.j2";
    row.caseLabel = "plugin.selection.case.emergency";
    row.actual = 220.0;   // 实际值 N·m（演示值）
    row.required = 180.0; // 要求值 N·m
    row.unitToken = "N*m";
    row.thresholdSourceKey = "plugin.selection.threshold.catalog-field";
    row.diagCodeText = "SEL-GEARBOX-PEAK-TORQUE-INSUFFICIENT";
    rows.push_back(row);
    return rows;
}

/// 黄金数据缺口行（一条——范围外轴缺口，与工况无关）。
std::vector<SelGapRow> goldenGapRows()
{
    std::vector<SelGapRow> rows;
    SelGapRow row;
    row.dimensionKey = "axis-out-of-scope";
    row.axisLabel = "plugin.selection.axis.j3";
    // caseLabel 默认空串——缺口与工况无关（T08 语义）。
    row.diagCodeText = "SEL-INPUT-AXIS-OUT-OF-SCOPE";
    rows.push_back(row);
    return rows;
}

}  // namespace

// =====================================================================
// SelPanelWorkflow 组——L-S1 就绪投影＋L-S4 回填提交流。
// =====================================================================

/**
 * @brief L-S1：会话事实→投影行逐字段透传（UX-10 七态素材面）；缺省
 *        态不伪造可行性（inputComplete=false/无判定/无在途）。
 */
TEST(SelPanelWorkflow, ReadinessProjectionTransfersSessionFacts_WP19T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-10"},
                  std::vector<std::string>{});

    // 缺省会话（零事实注入）→投影行如实呈现"无判定/不完整"——不伪造。
    SelPanelModule module;
    const SelReadinessRow empty = readinessProjection(module.session);
    EXPECT_EQ(empty.domainKey, "selection") << "域注册键＝ui.md §6.5 词表值";
    EXPECT_EQ(empty.verdict, sdurws::ird::core::EngineeringStatus::NotApplicable)
        << "无判定＝NotApplicable（不伪造可行性）";
    EXPECT_FALSE(empty.inputComplete);
    EXPECT_TRUE(empty.missingItemKeys.empty());
    EXPECT_FALSE(empty.hasActiveTask);

    // 事实注入→逐字段透传（判定权威在域就绪校验——投影零判定）。
    module.session.epoch = 7;
    module.session.inputComplete = true;
    module.session.hasActiveTask = true;
    module.session.verdict =
        sdurws::ird::core::EngineeringStatus::DataInsufficient;
    module.session.missingItemKeys = {"plugin.selection.missing.catalog"};
    const SelReadinessRow row = readinessProjection(module.session);
    EXPECT_EQ(row.verdict, sdurws::ird::core::EngineeringStatus::DataInsufficient);
    EXPECT_TRUE(row.inputComplete);
    EXPECT_TRUE(row.hasActiveTask);
    ASSERT_EQ(row.missingItemKeys.size(), 1u);
    EXPECT_EQ(row.missingItemKeys[0], "plugin.selection.missing.catalog");
}

/**
 * @brief L-S4：回填提交受理＋AT-30 复算提示素材（四域全量＋不沿用
 *        旧结论）＋会话记录缓冲追加。
 */
TEST(SelPanelWorkflow, BackfillSubmitAcceptsAndRecordsNotice_WP19T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"},
                  std::vector<std::string>{"AT-30"});

    SelPanelModule module;
    SelPanelServices services;
    int submitCount = 0;
    services.backfillSubmit = [&submitCount](const std::string&) {
        ++submitCount;
    };
    // 可用性缝未注入＝按可用呈现（模型层不本地拦截——宿主权威缺位
    // 时的保守呈现语义）。

    const SelBackfillRecord record =
        submitBackfill(module.session, services,
                       std::string(kSelBackfillUiCommandId));
    EXPECT_TRUE(record.accepted) << "空可用性缝＋有出口缝＝按可用受理";
    EXPECT_EQ(submitCount, 1) << "提交出口恰被调一次（token→宿主管线）";
    EXPECT_TRUE(record.rejectionKey.empty()) << "受理行零拒绝键";
    // AT-30 复算提示素材（四域全量＋不沿用——呈现半区的机器断言面）。
    ASSERT_EQ(record.notice.domainLabelKeys.size(), 4u);
    EXPECT_EQ(record.notice.domainLabelKeys[0], "kinematics");
    EXPECT_EQ(record.notice.domainLabelKeys[1], "dynamics");
    EXPECT_EQ(record.notice.domainLabelKeys[2], "selection");
    EXPECT_EQ(record.notice.domainLabelKeys[3], "optimization");
    EXPECT_FALSE(record.notice.retainPriorConclusion)
        << "AT-30：复核完成前不沿用原通过结论（呈现素材位恒 false）";
    // 会话记录追加（呈现史——token 逐字）。
    ASSERT_FALSE(module.session.recentBackfills.empty());
    EXPECT_EQ(module.session.recentBackfills.back().commandToken,
              std::string(kSelBackfillUiCommandId));
    EXPECT_EQ(module.session.recentBackfills.size(), 1u);
}

/**
 * @brief L-S4：空出口缝拒绝（诚实反馈——不静默丢弃）＋宿主可用性
 *        拒绝（权威判定透传）＋记录缓冲容量截断（呈现语义）。
 */
TEST(SelPanelWorkflow, BackfillSubmitRejectsAndTruncatesRecordBuffer_WP19T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-10"},
                  std::vector<std::string>{"AT-30"});

    SelPanelModule module;
    // 空出口缝：不受理＋拒绝键"出口未装配"（零提交调用——结构承载）。
    SelPanelServices bare;
    const SelBackfillRecord missing =
        submitBackfill(module.session, bare,
                       std::string(kSelBackfillUiCommandId));
    EXPECT_FALSE(missing.accepted);
    EXPECT_EQ(missing.rejectionKey, "backfill-outlet-missing");
    EXPECT_FALSE(missing.notice.retainPriorConclusion)
        << "未受理亦不携带沿用位";

    // 宿主可用性拒绝：出口缝在、可用性缝返回 false→不受理（权威
    // 判定透传——插件零本地判定），且出口缝零调用（拦截在提交前）。
    SelPanelServices gated;
    int submitCount = 0;
    gated.backfillSubmit = [&submitCount](const std::string&) { ++submitCount; };
    gated.backfillAvailability = [](const std::string&) { return false; };
    const SelBackfillRecord unavailable =
        submitBackfill(module.session, gated,
                       std::string(kSelBackfillUiCommandId));
    EXPECT_FALSE(unavailable.accepted);
    EXPECT_EQ(unavailable.rejectionKey, "backfill-unavailable");
    EXPECT_EQ(submitCount, 0) << "不可用拦截在出口缝之前（零提交调用）";

    // 缓冲截断：超过 8 条从头丢最旧（呈现缓冲语义——非业务阈值）。
    SelPanelServices ok;
    ok.backfillSubmit = [](const std::string&) {};
    for (int i = 0; i < 12; ++i) {
        submitBackfill(module.session, ok,
                       std::string(kSelBackfillUiCommandId));
    }
    EXPECT_EQ(module.session.recentBackfills.size(),
              sdurws::ird::selection::kSelBackfillRecordCapacity);
    // 最旧的拒绝记录已被挤出（先 2 条拒绝＋12 条受理——尾 8 条全受理）。
    for (const SelBackfillRecord& record : module.session.recentBackfills) {
        EXPECT_TRUE(record.accepted);
    }
}

// =====================================================================
// SelPanelCatalog 组——L-S2 目录行集归一。
// =====================================================================

/**
 * @brief L-S2：黄金目录行集透传（呈现序＝缝给定序——零排序）＋显示
 *         文本拼接（身份词面进用户文本；UX-02 零哈希零内部标识）。
 */
TEST(SelPanelCatalog, CatalogRowsGoldenAndDisplayText_WP19T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"},
                  std::vector<std::string>{});

    SelPanelServices services;
    services.catalogRows = goldenCatalogRows;
    bool notAssembled = true;
    const std::vector<SelCatalogRow> rows =
        catalogRowsPresented(services, &notAssembled);
    EXPECT_FALSE(notAssembled) << "缝已装配＝零未装配标记";
    ASSERT_EQ(rows.size(), 2u);
    // 行序＝缝给定序（透传——稳定序权威在组装侧，本侧零排序）。
    EXPECT_EQ(rows[0].version, "1.0");
    EXPECT_EQ(rows[1].version, "0.9");
    EXPECT_TRUE(rows[0].selected);
    EXPECT_FALSE(rows[1].selected);
    // 显示文本拼接（"ID 版本"——单词面空格分隔；逐字黄金）。
    EXPECT_EQ(catalogDisplayText(rows[0]), "cat-a 1.0");
    EXPECT_EQ(catalogDisplayText(rows[1]), "cat-a 0.9");
    // 空字段占位（"不适用"——不伪造值，ui.md §6.6 纪律）。
    SelCatalogRow blank;
    EXPECT_EQ(catalogDisplayText(blank), "(不适用) (不适用)");
}

/**
 * @brief L-S2：空缝与空清单两态区分（未装配≠空清单——呈现降级面）。
 */
TEST(SelPanelCatalog, CatalogRowsEmptyAndNotAssembledSemantics_WP19T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-10"},
                  std::vector<std::string>{});

    // 空缝→空行集＋未装配标记（"数据未装配"空态——与空清单不同）。
    SelPanelServices bare;
    bool notAssembled = false;
    EXPECT_TRUE(catalogRowsPresented(bare, &notAssembled).empty());
    EXPECT_TRUE(notAssembled) << "空缝＝未装配态";

    // 已装配空清单→空行集＋零未装配标记（合法空态）。
    SelPanelServices empty;
    empty.catalogRows = []() { return std::vector<SelCatalogRow>{}; };
    notAssembled = false;
    EXPECT_TRUE(catalogRowsPresented(empty, &notAssembled).empty());
    EXPECT_FALSE(notAssembled) << "空清单≠未装配";
}

// =====================================================================
// SelPanelCandidates 组——L-S3 候选/原因/缺口行集与范围外一致性。
// =====================================================================

/**
 * @brief L-S3：候选行集黄金（三呈现态；质量/裕量透传；无素材＝占位
 *         语义由 has* 位承载）＋原因行比较型字段全列（SEL-06/ERR-01
 *         呈现面）＋缺口分轨（与原因不同行集——§10.2 分类的呈现面）。
 */
TEST(SelPanelCandidates, RowsGoldenAndComparativeFields_WP19T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "SEL-06"},
                  std::vector<std::string>{});

    SelPanelServices services;
    services.candidateRows = goldenCandidateRows;
    services.rejectionRows = goldenRejectionRows;
    services.gapRows = goldenGapRows;

    // 候选三态黄金（行序＝缝给定序；三态词表值逐字）。
    bool notAssembled = true;
    const std::vector<SelCandidateRow> candidates =
        candidateRowsPresented(services, &notAssembled);
    EXPECT_FALSE(notAssembled);
    ASSERT_EQ(candidates.size(), 3u);
    EXPECT_EQ(candidates[0].verdictKey, "feasible");
    EXPECT_EQ(candidates[1].verdictKey, "data-insufficient");
    EXPECT_EQ(candidates[2].verdictKey, "rejected");
    // 质量/裕量透传（负裕量＝超限事实如实保留）。
    EXPECT_EQ(candidates[0].totalMassKg, 9.0);
    EXPECT_EQ(candidates[2].totalMassKg, 7.5);
    EXPECT_EQ(candidates[2].minMargin, -0.4);
    EXPECT_EQ(candidates[2].reasonCount, 1);
    EXPECT_FALSE(candidates[1].hasMass) << "无素材＝占位语义位（不伪造 0）";
    EXPECT_EQ(candidates[1].noteKey, "axis-out-of-scope");

    // 原因行比较型字段全列（实际值/要求值/单位/阈值来源/诊断码）。
    const std::vector<SelRejectionRow> rejections =
        rejectionRowsPresented(services, &notAssembled);
    EXPECT_FALSE(notAssembled);
    ASSERT_EQ(rejections.size(), 1u);
    EXPECT_EQ(rejections[0].reasonKey, "gearbox-peak-torque-insufficient");
    EXPECT_EQ(rejections[0].actual, 220.0);
    EXPECT_EQ(rejections[0].required, 180.0);
    EXPECT_EQ(rejections[0].unitToken, "N*m");
    EXPECT_EQ(rejections[0].thresholdSourceKey,
              "plugin.selection.threshold.catalog-field");
    EXPECT_EQ(rejections[0].diagCodeText,
              "SEL-GEARBOX-PEAK-TORQUE-INSUFFICIENT");

    // 缺口行与原因分轨（独立行集——数据不足与能力不足不混同）。
    const std::vector<SelGapRow> gaps = gapRowsPresented(services, &notAssembled);
    EXPECT_FALSE(notAssembled);
    ASSERT_EQ(gaps.size(), 1u);
    EXPECT_EQ(gaps[0].dimensionKey, "axis-out-of-scope");
    EXPECT_TRUE(gaps[0].caseLabel.empty()) << "范围外缺口与工况无关";
    EXPECT_EQ(gaps[0].diagCodeText, "SEL-INPUT-AXIS-OUT-OF-SCOPE");
}

/**
 * @brief L-S3c：范围外呈现键唯一书写点（wp19-t08 双面一致——格级
 *         note 键与缺口维键在呈现层归一到同一键；三红线之一〔零淘汰
 *         原因〕的呈现面语义：范围外候选 verdict 恒数据不足态）。
 */
TEST(SelPanelCandidates, OutOfScopePresentationKeyUnifiesBothFaces_WP19T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09", "UX-10"},
                  std::vector<std::string>{});

    // 唯一书写点（常量函数——两次调用同值）。
    const std::string key = selOutOfScopePresentationKey();
    EXPECT_EQ(key, "axis-out-of-scope");
    EXPECT_EQ(selOutOfScopePresentationKey(), key);

    // 双面归一：黄金候选行的格级 note 键与黄金缺口行的维键，经唯一
    // 书写点比对同键（呈现侧两承载面〔note/dimension〕零漂移）。
    SelPanelServices services;
    services.candidateRows = goldenCandidateRows;
    services.gapRows = goldenGapRows;
    const std::vector<SelCandidateRow> candidates = candidateRowsPresented(services);
    const std::vector<SelGapRow> gaps = gapRowsPresented(services);
    ASSERT_EQ(candidates.size(), 3u);
    ASSERT_EQ(gaps.size(), 1u);
    EXPECT_EQ(candidates[1].noteKey, gaps[0].dimensionKey)
        << "wp19-t08：格 note 与组合级缺口双面同键";
    // 范围外候选的呈现态＝数据不足（非淘汰——不升级整机不可行的
    // 呈现面语义；三红线之二〔DataInsufficient 语义〕）。
    EXPECT_EQ(candidates[1].verdictKey, kSelVerdictKeyDataInsufficient);
    EXPECT_EQ(candidates[1].reasonCount, 0)
        << "三红线之一：范围外轴零淘汰原因（呈现面 reasonCount=0）";
}

/**
 * @brief 指标呈现文本：数值带单位（UX-02"数值一律带单位"的 R1 面）
 *         ＋非有限数守卫（NaN/Inf 不进用户文本——"不适用"占位）。
 */
TEST(SelPanelCandidates, FormatMetricCarriesUnitAndGuardsNonFinite_WP19T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "SEL-06"},
                  std::vector<std::string>{});

    // 数值＋单位词面拼接（%g 最短形态——逐字黄金）。
    EXPECT_EQ(formatMetric(9.0, "kg"), "9 kg");
    EXPECT_EQ(formatMetric(220.0, "N*m"), "220 N*m");
    EXPECT_EQ(formatMetric(0.25, ""), "0.25") << "无量纲＝纯数值";
    // 非有限守卫（NaN/Inf——呈现占位，不泄漏非有限词形）。
    EXPECT_EQ(formatMetric(std::numeric_limits<double>::quiet_NaN(), "kg"),
              "(不适用)");
    EXPECT_EQ(formatMetric(std::numeric_limits<double>::infinity(), ""),
              "(不适用)");
}

// =====================================================================
// SelPanelAssembly 组——装配登记面与缝转发。
// =====================================================================

/**
 * @brief 装配登记面：描述符登记值（T02 两字段逐字保留＋T10 挂位/域键/
 *         命令/面板登记面）——token 词表自持常量、键族派生、恰一条主
 *         面板（advanced=false）。
 */
TEST(SelPanelAssembly, DescriptorRegistrationValues_WP19T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"},
                  std::vector<std::string>{});

    const SelectionPluginAssembly bundle = createSelectionPluginAssembly();
    // T02 登记字段（逐字保留——既有契约测试同值断言）。
    EXPECT_EQ(bundle.descriptor.pluginId, "selection");
    EXPECT_EQ(bundle.descriptor.titleKey, "plugin.selection.title");
    // T10 挂位/域键（ui.md §6.4 七阶段第 5／§6.5 域注册键）。
    EXPECT_EQ(bundle.descriptor.stageToken, "selection");
    EXPECT_EQ(bundle.descriptor.readinessDomainKey, "selection");
    // 命令登记面（恰一条——回填入口；token 自持常量＋键族派生）。
    ASSERT_EQ(bundle.descriptor.commands.size(), 1u);
    EXPECT_EQ(bundle.descriptor.commands[0].token,
              std::string(kSelBackfillUiCommandId));
    EXPECT_EQ(bundle.descriptor.commands[0].titleKey,
              "cmd.selection.apply-device-backfill.title");
    // 面板登记面（恰一条主面板——advanced=false；工厂闭包可调用形态）。
    ASSERT_EQ(bundle.descriptor.panels.size(), 1u);
    EXPECT_EQ(bundle.descriptor.panels[0].stageToken, "selection");
    EXPECT_EQ(bundle.descriptor.panels[0].titleKey,
              "plugin.selection.panel.workflow.title");
    EXPECT_FALSE(bundle.descriptor.panels[0].advanced)
        << "主面板位（UX-04 非 advanced）";
    EXPECT_TRUE(static_cast<bool>(bundle.descriptor.panels[0].factory))
        << "面板工厂闭包非空（宿主装配批次按字段翻译注册）";
}

/**
 * @brief 装配面：bind 系／setServices 转发到模块缝（缝注入的回读验证）
 *        ＋session() 访问与刷新转发（面板未创建＝空操作不崩溃）。
 */
TEST(SelPanelAssembly, AssemblyBindsDriveModuleServices_WP19T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"},
                  std::vector<std::string>{});

    SelectionPluginAssembly bundle = createSelectionPluginAssembly();
    ASSERT_NE(bundle.module(), nullptr);

    // 会话态注入（装配层路径）。
    bundle.session().epoch = 9;
    bundle.session().writable = false;
    EXPECT_EQ(bundle.session().epoch, 9u);

    // 服务缝整体注入＋三 bind（缝回读——转发面）。
    SelPanelServices services;
    int submitCount = 0;
    services.backfillSubmit = [&submitCount](const std::string&) {
        ++submitCount;
    };
    services.textResolver = [](const std::string& key) {
        return "值:" + key;
    };
    bundle.setServices(services);
    bundle.bindBackfillAvailability(
        [](const std::string& token) {
            return token == std::string(kSelBackfillUiCommandId);
        });
    bundle.bindTextResolver(
        [](const std::string& key) { return "覆盖:" + key; });

    ASSERT_TRUE(static_cast<bool>(bundle.module()->services.backfillSubmit));
    ASSERT_TRUE(static_cast<bool>(bundle.module()->services.textResolver));
    bundle.module()->services.backfillSubmit(
        std::string(kSelBackfillUiCommandId));
    EXPECT_EQ(submitCount, 1) << "提交缝经 bind 转发生效";
    EXPECT_EQ(bundle.module()->services.textResolver("k"), "覆盖:k")
        << "后绑定覆盖整体注入（bind 语义）";
    ASSERT_TRUE(
        static_cast<bool>(bundle.module()->services.backfillAvailability));
    EXPECT_TRUE(bundle.module()->services.backfillAvailability(
        std::string(kSelBackfillUiCommandId)));
    EXPECT_FALSE(bundle.module()->services.backfillAvailability("other"));

    // 会话刷新（面板未创建＝空操作——不崩溃）。
    bundle.refreshFromSession();
}

/**
 * @brief 命令目录：目录现产函数与描述符清单一致（token 词表唯一书写
 *         点的转发面——表外命令零登记）。
 */
TEST(SelPanelAssembly, CommandCatalogMatchesDescriptor_WP19T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"},
                  std::vector<std::string>{});

    const std::vector<SelCommandDescriptor> commands = selDomainCommands();
    ASSERT_EQ(commands.size(), 1u) << "本域零会话命令——回填入口恰一条";
    EXPECT_EQ(commands[0].token, std::string(kSelBackfillUiCommandId));
    EXPECT_EQ(commands[0].titleKey, "cmd.selection.apply-device-backfill.title");
    // 域注册键便利形态（与投影行 domainKey 对账——宿主汇聚锚）。
    EXPECT_EQ(selReadinessDomainKey(), "selection");
}

// =====================================================================
// SelPanelText 组——L-S5 文案解析流（UX-02 零哈希守卫）。
// =====================================================================

/**
 * @brief L-S5：缝解析＋空缝键名兜底＋哈希形态回退（UX-02"零哈希进
 *        用户文本"——64 位十六进制串按泄漏处置回退键名）。
 */
TEST(SelPanelText, ResolveTextFallsBackAndGuardsDigestLeak_WP19T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"},
                  std::vector<std::string>{});

    // 缝解析原值。
    SelPanelServices services;
    services.textResolver = [](const std::string& key) {
        return key == "plugin.selection.panel.workflow.title"
                   ? std::string("工作流")
                   : key;
    };
    bool fellBack = false;
    EXPECT_EQ(resolvePanelText(services, "plugin.selection.panel.workflow.title",
                               &fellBack),
              "工作流");
    EXPECT_FALSE(fellBack);

    // 空缝→键名兜底（开发态可见缺口——dynamics 同纪律）。
    SelPanelServices bare;
    fellBack = false;
    EXPECT_EQ(resolvePanelText(bare, "cmd.selection.apply-device-backfill.title",
                               &fellBack),
              "cmd.selection.apply-device-backfill.title");
    EXPECT_TRUE(fellBack);

    // 解析结果哈希形态→回退键名（泄漏守卫）。
    SelPanelServices leaky;
    leaky.textResolver = [](const std::string&) {
        return std::string(
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    };
    fellBack = false;
    EXPECT_EQ(resolvePanelText(leaky, "plugin.selection.panel.candidates.title",
                               &fellBack),
              "plugin.selection.panel.candidates.title");
    EXPECT_TRUE(fellBack) << "哈希形态解析值不进用户文本";
}

// =====================================================================
// SelPanelGui 组——GUI 呈现边界（诚实登记：envUnavailable，非通过）。
// =====================================================================

/**
 * @brief GUI 呈现不在本目标（无人值守门禁不做 GUI 运行验证——既有环
 *        境事实）：widget 渲染/交互点击归 sdurws_ird_selection_app
 *        harness 手动验证通道（单元卡 §15.3 流程——一次只启动一个
 *        GUI 可执行文件、不用 offscreen），本次未启动、未留截图——
 *        如实登记为环境不可用，不标注通过（AGENTS §4.2）。
 */
TEST(SelPanelGui, WidgetPresentationDeferredToHarnessEnvUnavailable)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "UX-10"},
                  std::vector<std::string>{});

    GTEST_SKIP()
        << "envUnavailable：无人值守门禁不做 GUI 运行验证——工作流页/"
           "目录管理页/候选表页的呈现与交互由 sdurws_ird_selection_app "
           "harness 手动点验承载（units/selection.md §15.3 流程），"
           "本次未启动，留痕见 traceability/builds/wp19-t10/ 登记";
}
