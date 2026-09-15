/**
 * @file   LinkageContractTest.cpp
 * @brief  diagnostics 跨单元契约测试（落位期）——链接边界与 core 契约消费面
 *         的桩形态验证（P-DIAG-8 处置）。
 *
 * 设计依据：
 *   - units/diagnostics.md §3.3（目标 sdurws_ird_diagnostics PUBLIC 链 core；
 *     `_contract_test`＝跨单元契约面：与 core 值类型、与消费方 sink 形态
 *     对齐）、§3.2（消费的 core 契约清单——DiagCode/DiagnosticRecord/
 *     ConfirmableFinding 等）、§10 DT-BUILD 行（依赖仅 core 边）；
 *   - 任务契约 tasks/foundation/DIAG-T02.json acceptance 2（仅 diagnostics→core
 *     一条单元边）与 acceptance 3（P-DIAG-8 处置：契约测试以桩＋契约夹具
 *     验证，不私改 execution 侧链接——P-EX-8 联动，双库集成冒烟另行登记）；
 *   - governance-log.md P-DIAG-8 行（处置约束原文："契约测试以桩验证；
 *     EX-T01 后补双库集成冒烟"）。
 *
 * P-DIAG-8 处置说明（为什么本文件以"桩消费方"形态验证）：diagnostics 的
 * 下游消费者（project/execution/io/ui/reporting）此时尚未落位，execution
 * 侧也没有链接 diagnostics（P-EX-8 待其消费任务启用）。因此跨单元契约的
 * 验证载体是本测试翻译单元——它就是一个"最小桩消费方"：与未来消费者完全
 * 同构的依赖形态（只链 sdurws_ird_diagnostics 一个单元目标，core 类型经
 * PUBLIC 传递到达，零 execution/project 链接改动）。DIAG-T05/T10 落地
 * Confirmable 服务与契约套件后，套件按同一处置消费 testkit 谓词与桩夹具；
 * execution 真实链接启用后由 P-EX-8 联动的双库集成冒烟补验（另行登记，
 * 不在本任务范围内私改）。
 */

#include <sdurws/ird/core/DiagData.hpp>   // core 承载的诊断契约：DiagnosticRecord/
                                          // ComparativeFields/ConfirmableFinding（§4.8/§5.7）
#include <sdurws/ird/core/Identity.hpp>   // ObjectId（诊断关联身份——§3.2 消费清单）
#include <sdurws/ird/core/Provenance.hpp> // SourcedValue<T> 四态承载（比较型值的
                                          // NotApplicable/Invalid 语义——§3.2 消费清单）
#include <sdurws/ird/core/Units.hpp>      // UnitToken（比较型单位字段——§3.2 消费清单）

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <optional>

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird;
namespace core = sdurws::ird::core;

/// diagnostics 单元树根（industrialrobot 目录——IRD_DIAGNOSTICS_UNIT_ROOT 注入）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_DIAGNOSTICS_UNIT_ROOT};
    return dir;
}

/// 桩消费方使用的合法 ObjectId（"obj-"＋32 位十六进制——core Identity 句法）。
core::ObjectId sampleObjectId()
{
    return core::ObjectId::fromCanonical(
        std::string{"obj-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"});
}

/// 合法稳定码样本（句法 ^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64——core C-3）。
const char* sampleCode()
{
    // PRJ- 前缀样本码：句法校验在 core 工厂（core.md §4.8），码值分配权威
    // ＝diagnostics StableCodeRegistry（随 DIAG-T03 落地）。落位期本样本码
    // 只用于验证 core 承载层的构造/校验链路可用，不构成码表收编。
    return "PRJ-LOCK-HELD";
}

/// 已注册单位句柄（UnitToken 只能经 find() 获得——§5.4；"rad" 在 core 冻结
/// 单位表内，Units.cpp 内置表逐条登记）。查表失败＝core 单位表契约破坏，
/// 测试骨架直接失败（FAIL 而非断言穿过）。
core::UnitToken radUnit()
{
    auto token = core::UnitToken::find("rad");
    if (!token.has_value()) {
        ADD_FAILURE() << "core 单位表缺 rad（冻结 token——core.md §4.4）";
        return core::UnitToken{};
    }
    return *token;
}

}  // namespace

/**
 * 行为级自证：产品目标 PUBLIC 链 sdurws_ird_core（acceptance 1）。
 *
 * 本测试翻译单元在 CMake 侧只链接 sdurws_ird_diagnostics 与 gtest，不显式
 * 链接 core——它能编译并消费 core 诊断契约类型（DiagnosticRecord 工厂＋
 * 值语义），本身就证明 core 头与 core 库符号经 diagnostics 的 PUBLIC 链接
 * 接口传染到达。这正是未来消费方（project/execution/io/ui/reporting）的
 * 依赖形态：链 diagnostics 即获得 core 承载的诊断数据契约，无须（也不允许）
 * 各自直链 core 之外的第二条单元边。
 */
TEST(DiagnosticsLinkage, PublicCoreExposure_DT_BUILD)
{
    // core C-3 工厂构造：码句法＋必填串校验在 core 侧执行——构造成功即
    // "core 头可编译＋core 库符号可链接"双验证通过（make 的实现符号在
    // sdurws_ird_core 内，不在本单元）。
    const auto record = core::DiagnosticRecord::make(
        sampleCode(), sampleObjectId(), std::string{"Gripper-A"},
        std::string{"/Base/Gripper-A"}, std::string{"夹爪锁定占用"},
        std::string{"上一命令尚未完成"}, std::string{"等待或中止当前命令"});
    // 字段逐一承载断言（ERR-01 承载链路的落位期最小核对）。
    EXPECT_EQ(record.code, sampleCode());
    ASSERT_TRUE(record.subject.has_value());
    EXPECT_EQ(*record.subject, sampleObjectId());
    EXPECT_EQ(record.localName, std::optional<std::string>{"Gripper-A"});
    EXPECT_FALSE(record.comparison.has_value()) << "非比较型记录不得携带比较字段";
}

/**
 * 链接图契约：diagnostics 的 CMake 目标引用集合仅含 core 一条产品单元边
 * （acceptance 2——ARCH §3.5 既有边，零新增同层边，SA-10）。
 *
 * 扫描单元 CMakeLists.txt 文本中出现的全部 sdurws_ird_* 目标引用，与白名单
 * 比对：产品目标链接 core（唯一单元边）＋本单元自身/测试目标的引用。出现
 * 任何其他产品单元目标（如 sdurws_ird_execution）即构建图越界。
 *
 * 白名单含 sdurws_ird_testkit 的依据（DIAG-T10 登记）：testkit.md §2.4 的
 * T-1 允许形态 `sdurws_ird_<unit>_contract_test → { 被测产品目标,
 * sdurws_ird_testkit, gtest }`——契约目标消费 testkit 契约谓词
 * （checkDiagnosticRecord/checkComparativeFields，§10 头注与任务契约
 * acceptance 3 的消费面）。testkit 是测试侧单元（不随产品分发，§3.6），
 * 该链接只存在于 _contract_test 目标、不改变产品库的单元边集合——
 * R-1 判定范围（产品单元互链）不受影响；产品目标 sdurws_ird_diagnostics
 * 本体零 testkit 边由配置期守卫＋安装扫描（WP-24）继续把守。
 */
TEST(DiagnosticsLinkage, UnitEdgeOnlyCore_DT_BUILD_R1_R2)
{
    const auto cmakeFile = unitRoot() / "diagnostics" / "CMakeLists.txt";
    ASSERT_TRUE(fs::exists(cmakeFile)) << "diagnostics/CMakeLists.txt 不存在";
    std::ifstream in(cmakeFile, std::ios::binary);
    ASSERT_TRUE(static_cast<bool>(in)) << "无法读取 diagnostics/CMakeLists.txt";
    const std::string text{std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>()};

    // 收集文本中全部 sdurws_ird_<unit>[<_role>] 目标名。先剥离注释：CMake
    // 注释自 "#" 起到行尾——注释文字（如"不链 sdurws_ird_testkit"的处置
    // 说明、守卫正则示例）不是构建图引用，参与扫描会误报。目标名由单词
    // 边界分隔，逐字符扫描稳定实现。
    std::set<std::string> refs;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        const auto hashPos = line.find('#');
        if (hashPos != std::string::npos) { line.erase(hashPos); }
        for (std::size_t pos = line.find("sdurws_ird_");
             pos != std::string::npos; pos = line.find("sdurws_ird_", pos + 1)) {
            std::size_t end = pos + std::string("sdurws_ird_").size();
            while (end < line.size()
                   && (std::isalnum(static_cast<unsigned char>(line[end]))
                       || line[end] == '_')) {
                ++end;
            }
            // 名字至少含一个单元名字符才计入：守卫正则模式（"^sdurws_ird_"）
            // 中的裸前缀后紧跟引号，不是目标引用（若不排除会把模式误报为边）。
            if (end > pos + std::string("sdurws_ird_").size()) {
                refs.insert(line.substr(pos, end - pos));
            }
        }
    }
    ASSERT_FALSE(refs.empty()) << "CMakeLists 未引用任何 ird 目标（扫描失效）";

    // 白名单：core（唯一允许的产品单元边）＋ diagnostics 本单元三目标（产品/
    // 测试/契约测试——同一单元内部引用不构成跨单元边，R-1 判定范围）＋
    // sdurws_ird_testkit（测试侧单元——T-1 允许形态的 _contract_test 消费面，
    // 依据见本用例 DOC 注释；DIAG-T10 登记）。
    const std::set<std::string> allowed = {
        "sdurws_ird_core",
        "sdurws_ird_diagnostics",
        "sdurws_ird_diagnostics_test",
        "sdurws_ird_diagnostics_contract_test",
        "sdurws_ird_testkit"};
    for (const auto& ref : refs) {
        EXPECT_NE(allowed.find(ref), allowed.end())
            << "diagnostics 构建图出现白名单外目标引用（产品单元边仅 "
               "diagnostics→core 一条，ARCH §3.5）: " << ref;
    }
}

/**
 * P-DIAG-8 处置自证：桩消费方经 diagnostics 链接面消费 core 承载的
 * ConfirmableFinding 契约（SA-15 数据形状）——确认放行流的"数据"半区
 * 在消费侧可用，而"编排"半区归 project、"交互"半区归 ui（§5.1 分工）；
 * 本单元与其消费者之间零 execution 侧链接改动（P-EX-8 联动的双库集成
 * 冒烟另行登记）。
 *
 * 演练 core 契约的两条不变量（core.md §5.7——本单元 §3.2 消费清单的
 * ConfirmableFinding 行的落位期可用性核对；状态机实现承载随 DIAG-T05）：
 *   - C-1：可确认诊断必为比较型（make 携带 comparison 字段的记录才可造）；
 *   - C-2：Confirmed ⇔ 携带凭据（Pending/Rejected 不得携带凭据）。
 */
TEST(DiagnosticsLinkage, ConsumerStubCoreFindingContracts_P_DIAG_8)
{
    // 比较型记录：行程上限比较（实际 6.4 rad，期望 2π rad——UX-03 三要素；
    // SourcedValue<double> 的 Provided 四态经 core 词表承载，来源标记显式
    // 给出——来源不缺席是 ValueProvenance 的构造前置）。
    core::ComparativeFields fields{};
    const auto provenance = core::ValueProvenance::make(core::ProvenanceKind::UserProvided);
    fields.actual.quantity = core::SourcedValue<double>::provided(6.4, provenance);
    fields.actual.unit = radUnit();
    fields.expected.quantity =
        core::SourcedValue<double>::provided(6.283185307179586476925, provenance);
    fields.expected.unit = radUnit();

    auto record = core::DiagnosticRecord::make(
        sampleCode(), sampleObjectId(), std::string{"Joint-2"},
        std::string{"/Base/Joint-2"}, std::string{"行程上限超过工程范围"},
        std::string{"目标关节角超出限位"}, std::string{"调整目标或限位"},
        fields);
    ASSERT_TRUE(record.comparison.has_value());

    // C-1 正例：比较型记录可确认化，初始态 Pending 且无凭据（C-2）。
    auto finding = core::ConfirmableFinding::make(std::move(record));
    EXPECT_EQ(finding.state, core::ConfirmationState::Pending);
    EXPECT_FALSE(finding.credential.has_value());

    // C-1 反例：非比较型记录 make 即抛（core/diag/c1:）——不可比较的发现
    // 不可确认；异常语义为调用方错误 fail-fast（不吞错，AGENTS.md §3）。
    auto plain = core::DiagnosticRecord::make(
        sampleCode(), sampleObjectId(), std::optional<std::string>{},
        std::optional<std::string>{}, std::string{"上下文"}, std::string{"原因"},
        std::string{"动作"});
    EXPECT_THROW(core::ConfirmableFinding::make(std::move(plain)), core::CoreError);

    // C-2 正例：Pending→Confirmed 必须携带凭据；确认后状态与凭据一致。
    finding.confirm(core::ConfirmationCredential{
        std::string{"sample-principal"}, std::chrono::system_clock::now()});
    EXPECT_EQ(finding.state, core::ConfirmationState::Confirmed);
    ASSERT_TRUE(finding.credential.has_value());
    EXPECT_EQ(finding.credential->principal, "sample-principal");
}
