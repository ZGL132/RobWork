/**
 * @file   DiagContractCheckTest.cpp
 * @brief  testkit 契约谓词消费用例组（§10 头注 `_contract_test` 面"与
 *         testkit checkDiagnosticRecord/checkComparativeFields"的承接；
 *         testkit.md §10.2 diagnostics 行交接——"从 testkit 接收：
 *         ContractCheck（DiagnosticRecord）"）。
 *
 * 设计依据：
 *   - units/testkit.md §5.5（两谓词签名与校验项）、§10.2 diagnostics 行、
 *     §2.4（允许依赖形态：`sdurws_ird_diagnostics_contract_test → { 被测
 *     产品目标, sdurws_ird_testkit, gtest }`——本目标链 testkit 即该形态；
 *     产品目标 sdurws_ird_diagnostics 不链 testkit＝T-1 红线，由
 *     LinkageContractTest 沿用面钉住）
 *   - units/diagnostics.md §10 DT-DIAG-1 行（"testkit checkDiagnosticRecord
 *     通过"观测点）、§11 DIAG-T10 行（DT-* 用例体收口）
 *   - 任务契约 tasks/foundation/DIAG-T10.json acceptance 3（P-DIAG-8 处置：
 *     契约测试以桩＋契约夹具验证，testkit 两谓词消费于本文件落位）
 *
 * 双面对账说明（本文件的方法论，evidence EnvelopeAccessorContractTest 同源）：
 *   testkit 的 checkDiagnosticRecord 是"诊断记录契约的独立实现"
 *   （testkit 侧冻结，供各单元复用）；diagnostics 的 DiagnosticsFactory
 *   是产品侧权威校验器。两者消费同一事实（core::DiagnosticRecord 字段面）
 *   必须同判：工厂产出的正例记录"双过"；工厂之外的违约输入（默认构造占位
 *   对象等未来消费方可能经手的形态）被 testkit 独立拦下。若两面对同一输入
 *   分叉，即契约漂移——本文件逐面钉住。
 *
 * 替身边界声明（EV-REG-3 同模式，全文见本目录 README.md §1）：本文件无
 * 替身——全部输入为真实工厂产出或显式构造的契约形态数据；断言消费的是
 * 真实 testkit 谓词与真实工厂。
 *
 * 线程约束：全部用例单线程（谓词为纯函数——testkit/ContractCheck.hpp 头注）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>
#include <sdurws/ird/testkit/ContractCheck.hpp>

#include <chrono>
#include <string>

namespace {

using namespace sdurws::ird::diagnostics;
namespace core = sdurws::ird::core;
namespace tk = sdurws::ird::testkit;
using sdurws::ird::core::ObjectId;

// ---------------------------------------------------------------------
// 夹具辅助（与既有套件同风格——自持不共享）
// ---------------------------------------------------------------------

/// 可确认比较型测试码（ConfirmableTest 同款——阶段 A 内置表无可确认码，
/// 按"业务域码随域卡注册"机制注册：MDL 前缀所有权归 modeling）。
inline constexpr const char* kTravelLimitCode = "MDL-06-TRAVEL-LIMIT";

/// 确定性测试时钟（§4.2 IClock 注释）。
class ManualClock final : public IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override { return m_now; }

private:
    std::chrono::system_clock::time_point m_now{std::chrono::seconds{4000000}};
};

/// 组装并 seal 内置注册表＋可确认比较型测试码。
void sealRegistry(StableCodeRegistry& registry)
{
    registerBuiltinCodes(registry);
    CodeDescriptor confirmable;
    confirmable.code = kTravelLimitCode;
    confirmable.ownerUnit = "modeling";
    confirmable.category = DiagnosticCategory::Confirmable;
    confirmable.severity = DiagnosticSeverity::Warning;
    confirmable.titleKey = "diag.mdl-06-travel-limit.title";
    confirmable.detailKey = "diag.mdl-06-travel-limit.detail";
    confirmable.paramSchema = "[]";
    confirmable.confirmable = true;
    confirmable.requiresComparison = true;   // 比较型三要素必填（本套件被测面）
    confirmable.retryable = RetryKind::UserRetry;
    registry.registerCode(confirmable);
    registry.seal();
}

/// 合法用户级记录（非比较型——工厂正例基线）。
core::DiagnosticRecord makeUserRecord(const std::string& code, ObjectId subject)
{
    return core::DiagnosticRecord::make(
        code, subject, std::string("joint_5"), std::string("Robot.joint_5"),
        std::string("评估路径输入非法"), std::string("启用 Must 条目取值越域"),
        std::string("修正输入后重新评估"));
}

/// 合法比较值侧（Provided 数值＋用户来源＋单位 token）。
core::ComparativeValue makeComparativeValue(double number, const char* unitSymbol)
{
    const auto unit = core::UnitToken::find(unitSymbol);
    return core::ComparativeValue{
        core::SourcedValue<double>::provided(
            number, core::ValueProvenance::make(core::ProvenanceKind::UserProvided)),
        *unit};
}

/// failures 的 fieldPath 拼接（失败报告观测辅助——断言违规字段被指明）。
std::string joinFieldPaths(const tk::CheckResult& result)
{
    std::string joined;
    for (const auto& failure : result.failures) {
        joined += failure.fieldPath;
        joined += "; ";
    }
    return joined;
}

// =====================================================================
// checkDiagnosticRecord 消费（§10 DT-DIAG-1 观测点"testkit
// checkDiagnosticRecord 通过"的落位；acceptance 3 主句）
// =====================================================================

class DiagContractCheck : public ::testing::Test {
protected:
    void SetUp() override
    {
        sealRegistry(m_registry);
        m_factory = std::make_unique<DiagnosticsFactory>(m_registry, m_clock);
    }

    StableCodeRegistry m_registry;    ///< 码表（每用例独立）
    ManualClock m_clock;              ///< 确定性时钟
    std::unique_ptr<DiagnosticsFactory> m_factory;
};

/// 工厂正例双过（非比较型码）：工厂 create 接受的记录必过 testkit 契约
/// 校验（双面对账正例——product/testkit 对同一记录同判"合法"）。
TEST_F(DiagContractCheck, FactoryUserRecordPassesCheckDiagnosticRecord)
{
    const core::DiagnosticRecord record =
        makeUserRecord("RT-INPUT-INVALID", ObjectId::generate());
    // 前置自证：该记录确经产品工厂接受（工厂不抛＝产品侧判定合法）。
    const DiagnosticEntry entry = m_factory->create(record, [] {
        DiagContext context;
        context.sourceUnit = "runtime";
        context.sourceInterface = "compile.workcell";
        return context;
    }());
    EXPECT_GE(entry.entryId, 1u);

    const tk::CheckResult result = tk::checkDiagnosticRecord(record, {});
    EXPECT_TRUE(result.passed)
        << "工厂接受的记录被 testkit 契约谓词拒绝（双面分叉）——violations: "
        << joinFieldPaths(result);
}

/// 工厂正例双过（比较型可确认码）：三要素完整记录双过——checkDiagnostic-
/// Record 的比较型校验项（三要素存在＋actual/expected/unit 已注册）与
/// checkComparativeFields 的量纲校验项双双通过（§5.5 两谓词分工面）。
TEST_F(DiagContractCheck, FactoryComparisonRecordPassesBothChecks)
{
    const core::DiagnosticRecord record = core::DiagnosticRecord::make(
        kTravelLimitCode, ObjectId::generate(), std::string("joint_5"),
        std::string("Robot.joint_5"), std::string("行程超限待确认"),
        std::string("实际行程 620mm 超过上限 550mm"), std::string("确认或修正行程参数"),
        core::ComparativeFields{makeComparativeValue(620.0, "mm"),
                                makeComparativeValue(550.0, "mm")});
    // 前置自证：产品工厂接受（confirmable+requiresComparison 码的校验链全过）。
    const DiagnosticEntry entry = m_factory->create(record, [] {
        DiagContext context;
        context.sourceUnit = "modeling";
        context.sourceInterface = "strategy.check";
        return context;
    }());
    EXPECT_GE(entry.entryId, 1u);

    // 面 1：整记录契约（含比较型三要素存在性）。
    const tk::CheckResult recordResult = tk::checkDiagnosticRecord(record, {});
    EXPECT_TRUE(recordResult.passed)
        << "violations: " << joinFieldPaths(recordResult);
    // 面 2：比较型量纲（mm 为长度量纲——kindOf 单点换算表来源）。
    const tk::CheckResult comparisonResult = tk::checkComparativeFields(
        *record.comparison, core::kindOf(*core::UnitToken::find("mm")));
    EXPECT_TRUE(comparisonResult.passed)
        << "violations: " << joinFieldPaths(comparisonResult);
}

/// 瞬时（Dev）记录由选项门控：subject 可空的 Dev 记录在默认选项下被拒
/// （稳定项 subject 必填——testkit §5.5 契约面）、allowTransient=true 放行
/// （瞬时开发诊断例外——同一谓词的选项语义，双面同判 Dev 例外的边界）。
TEST_F(DiagContractCheck, TransientRecordGatedByAllowTransientOption)
{
    // Dev 码记录：subject/localName/runtimeName 全空（§4.2 合法实例 2 的
    // core 形态——工厂 Dev 路径接受）。
    const core::DiagnosticRecord devRecord = core::DiagnosticRecord::make(
        "EX-CHANNEL-PROTOCOL-ERROR", {}, {}, {},
        std::string("worker 通道帧序断裂"), std::string("帧序号回退"),
        std::string("检查回传批次序号"));

    // 默认选项：拒绝（failures 指明 subject 违规字段）。
    const tk::CheckResult strict = tk::checkDiagnosticRecord(devRecord, {});
    EXPECT_FALSE(strict.passed) << "Dev 记录缺 subject 应被默认选项拒绝";
    EXPECT_NE(joinFieldPaths(strict).find("subject"), std::string::npos)
        << "违规报告未指明 subject 字段（fieldPath 观测点）——violations: "
        << joinFieldPaths(strict);

    // allowTransient=true：放行（同一记录同一谓词——选项即边界开关）。
    const tk::CheckResult lenient =
        tk::checkDiagnosticRecord(devRecord, tk::DiagnosticCheckOptions{true});
    EXPECT_TRUE(lenient.passed)
        << "violations: " << joinFieldPaths(lenient);
}

/// 违约输入独立拦下（负例）：未经工厂的占位默认记录（code 空、必填串空、
/// subject 缺失——core 注释明示该形态"勿使用未经验证的对象"，但未来消费方
/// 可能经手）被 testkit 谓词独立拒绝且逐字段指明——产品工厂之外的第二道
/// 契约防线真实在岗（testkit 独立实现的存在意义，非工厂校验的复读机）。
TEST_F(DiagContractCheck, PlaceholderDefaultRecordFailsWithFieldPaths)
{
    const core::DiagnosticRecord placeholder{};   // 占位默认对象（显式负例输入）

    const tk::CheckResult result = tk::checkDiagnosticRecord(placeholder, {});
    EXPECT_FALSE(result.passed) << "占位默认记录必须被契约谓词拒绝";
    const std::string violations = joinFieldPaths(result);
    // code 句法与必填串两类校验项至少各被指明一次（§5.5 校验项面）。
    EXPECT_NE(violations.find("code"), std::string::npos)
        << "violations 未指明 code——" << violations;
    EXPECT_FALSE(result.failures.empty());
}

// =====================================================================
// checkComparativeFields 消费（量纲一致契约——UX-03 三要素面）
// =====================================================================

/// 量纲一致双过：同量纲（mm/mm，Length）通过。
TEST_F(DiagContractCheck, ComparativeFieldsSameQuantityKindPass)
{
    const core::ComparativeFields fields{makeComparativeValue(620.0, "mm"),
                                         makeComparativeValue(550.0, "mm")};
    const tk::CheckResult result =
        tk::checkComparativeFields(fields, core::QuantityKind::Length);
    EXPECT_TRUE(result.passed) << "violations: " << joinFieldPaths(result);
}

/// 量纲不一致拒（负例）：actual=mm（Length）对 expectedKind=Angle——
/// 三要素单位混量纲被独立拒绝（UX-03"单位一致比较"的 testkit 契约面；
/// 与产品工厂的注册单位校验互为两面）。
TEST_F(DiagContractCheck, ComparativeFieldsMismatchedQuantityKindFails)
{
    const core::ComparativeFields fields{makeComparativeValue(620.0, "mm"),
                                         makeComparativeValue(550.0, "mm")};
    const tk::CheckResult result =
        tk::checkComparativeFields(fields, core::QuantityKind::Angle);
    EXPECT_FALSE(result.passed) << "量纲不符必须被拒绝";
    EXPECT_NE(joinFieldPaths(result).find("unit"), std::string::npos)
        << "violations 未指明 unit——" << joinFieldPaths(result);
}

}  // namespace
