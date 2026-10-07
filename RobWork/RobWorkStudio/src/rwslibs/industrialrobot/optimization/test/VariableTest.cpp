/**
 * @file   VariableTest.cpp
 * @brief  设计变量模型用例组（OptVariables）——两种初始化（OPT-VER-101/102）、
 *         未授权默认锁定（OPT-VER-103）、连续/量化/枚举值域（OPT-VER-104/105/
 *         106）、StageB 阶段锁（OPT-VER-108）与绑定校验（I-OPT-7～9、快照
 *         闭包核对）——任务契约 WP-20-T03 acceptance 1 的模型测试面。
 *
 * 设计依据：
 *   - units/optimization.md §5.2～§5.4（变量/绑定/锁定语义）、§5.7（阶段锁）、
 *     §8.2（两种初始化）、§13.1（OPT-VER-101～108 用例行——本文件逐用例
 *     对应）、I-OPT-7～10
 *   - 需求 OPT-01/02（REQUIREMENTS §15.0——变量清单/改型默认锁定/V12-02）
 *   - 用例名与断言注释按 AGENTS §2.7 带需求/AT 追溯（IRD_TEST_INFO 登记）
 *
 * 数值口径：量化对齐黄金值全部选 2 的幂友好步长（0.25）——半格判定在
 * IEEE 754 下精确（0.375/0.25＝1.5 无表示误差），黄金期望值可解析给出。
 */

#include <sdurws/ird/optimization/Variable.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/optimization/CandidatePatch.hpp>
#include <sdurws/ird/optimization/DiagCodes.hpp>
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace sdurws::ird;
using optimization::BindingValidationReport;
using optimization::CandidatePatch;
using optimization::OptimizationError;
using optimization::OptimizationStage;
using optimization::PatchItem;
using optimization::StudyInitialization;
using optimization::VariableBinding;
using optimization::VariableDefinition;
using optimization::VariableKind;

namespace {

/// 码常量别名（可读性）。
constexpr auto kInvalid = optimization::kOptInputInvalid;
constexpr auto kLocked = optimization::kOptVarLocked;
constexpr auto kStageLocked = optimization::kOptStageLocked;
constexpr auto kPatchIllegal = optimization::kOptPatchIllegal;

/// 从词表条目实例化一个"结构完整"的绑定（连续/量化）：填类别/单位/值域/
/// 权威定位——测试聚焦被测语义而非样板字段。
VariableBinding makeContinuousBinding(const std::string& bindingId, double lower,
                                      double upper, VariableKind kind
                                      = VariableKind::Continuous,
                                      double step = 0.0)
{
    VariableBinding b;
    b.bindingId = bindingId;
    b.kind = kind;
    const VariableDefinition* d = optimization::matchDefinition(bindingId, OptimizationStage::StageD);
    EXPECT_NE(d, nullptr) << "测试绑定的 token 不在词表（测试自身错误）: " << bindingId;
    if (d == nullptr) {
        return b;
    }
    if (!d->unitSymbol.empty()) {
        // 单位随词表；关节范围类词表单位为空（rad/m 随关节类型）——测试用 rad。
        const auto u = core::UnitToken::find(d->unitSymbol.empty() ? "rad" : d->unitSymbol);
        if (u.has_value()) {
            b.unit = *u;
        }
    }
    b.lowerBound = lower;
    b.upperBound = upper;
    b.step = step;
    b.enumValues = d->enumValues;
    b.authorityFieldPath = d->authorityFieldPath;
    b.authorized = true;   // 测试默认授权（改型锁定语义由各用例显式施加）
    b.locked = false;
    return b;
}

/// 从词表条目实例化一个枚举绑定（值域取词表封闭值域）。
VariableBinding makeEnumBinding(const std::string& bindingId, std::uint32_t defaultIndex = 0)
{
    VariableBinding b;
    b.bindingId = bindingId;
    b.kind = VariableKind::Enumeration;
    const VariableDefinition* d = optimization::matchDefinition(bindingId, OptimizationStage::StageD);
    EXPECT_NE(d, nullptr) << "测试绑定的 token 不在词表: " << bindingId;
    if (d != nullptr) {
        b.enumValues = d->enumValues;
        b.defaultValueIndex = defaultIndex;
        b.authorityFieldPath = d->authorityFieldPath;
    }
    b.authorized = true;
    b.locked = false;
    return b;
}

}  // namespace

// =====================================================================
// OPT-VER-101：新机型初始化（OPT-01）——全部 R1 变量默认可绑定
// =====================================================================

TEST(OptVariables, NewModelInitializationUnlocksAllBindings_WP20T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-01"}, std::vector<std::string>{"AT-09"});

    std::vector<VariableBinding> user = {
        makeContinuousBinding("mdl.joint[2].dh.a", 0.05, 0.40),          // m
        makeEnumBinding("mdl.link[3].material", 0),                      // steel 默认
        makeContinuousBinding("mdl.drivetrain.ratio[1]", 40.0, 160.0),   // 无量纲
        makeEnumBinding("mdl.base.orientation.preset", 0),               // ground 默认
    };

    const auto initialized
        = optimization::initializeBindings(OptimizationStage::StageB,
                                           StudyInitialization::NewModel, user);

    ASSERT_EQ(initialized.size(), user.size());
    for (const auto& b : initialized) {
        // 新机型：变量授权默认全开（§8.2——authorized=true、locked=false）；
        // 无历史锁定集（P-03：不发明默认工程数值，边界保持用户填写值）。
        EXPECT_TRUE(b.authorized) << b.bindingId;
        EXPECT_FALSE(b.locked) << b.bindingId;
    }
    // 词表元数据回填（同一管线实例化）：单位与权威定位来自词表条目。
    EXPECT_EQ(initialized[0].unit.symbol(), "m");
    EXPECT_EQ(initialized[0].authorityFieldPath, "robot-design/joints[i]/dh/a");
    // 无量纲传动比的单位＝core 注册符号 "1"（Dimensionless）。
    EXPECT_EQ(initialized[2].unit.symbol(), "1");
    // OPT-VER-101 观测点"绑定集"稳定性：同一输入两次初始化逐字段一致
    // （确定性，NFR-COR-02——初始化是纯函数面）。
    const auto again
        = optimization::initializeBindings(OptimizationStage::StageB,
                                           StudyInitialization::NewModel, user);
    ASSERT_EQ(again.size(), initialized.size());
    for (std::size_t i = 0; i < again.size(); ++i) {
        EXPECT_EQ(again[i].bindingId, initialized[i].bindingId);
        EXPECT_EQ(again[i].authorized, initialized[i].authorized);
        EXPECT_EQ(again[i].locked, initialized[i].locked);
        EXPECT_EQ(again[i].lowerBound, initialized[i].lowerBound);
        EXPECT_EQ(again[i].authorityFieldPath, initialized[i].authorityFieldPath);
    }
}

// =====================================================================
// OPT-VER-102：改型初始化（OPT-01/02）——未授权参数默认锁定
// =====================================================================

TEST(OptVariables, RefitInitializationLocksUnauthorizedByDefault_WP20T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-01", "OPT-02"}, std::vector<std::string>{"AT-09"});

    std::vector<VariableBinding> user = {
        makeContinuousBinding("mdl.joint[2].dh.a", 0.05, 0.40),
        makeEnumBinding("mdl.link[3].material", 0),
        makeContinuousBinding("mdl.drivetrain.ratio[1]", 40.0, 160.0),
    };
    // 改型语义（§8.2/§5.4）：以当前修订为基线；未授权参数默认锁定——
    // 初始化产出全部 authorized=false、locked=true；"授权"是用户在研究
    // 定义中逐变量显式开启的后续编辑动作（进 config.opt），不属于初始化
    // 的推断职责（入参 authorized 字段默认值不被误读为显式授权）。

    const auto initialized
        = optimization::initializeBindings(OptimizationStage::StageB,
                                           StudyInitialization::Refit, user);

    ASSERT_EQ(initialized.size(), 3U);
    // 全部默认锁定（authorized=false ⇒ locked=true，OPT-02）——含入参
    // 字段默认 authorized=true 的绑定（Refit 不信任字段默认值，统一重置）。
    for (const auto& b : initialized) {
        EXPECT_FALSE(b.authorized) << b.bindingId;
        EXPECT_TRUE(b.locked) << b.bindingId;
    }
    // 词表元数据回填不受授权重置影响（值域/单位/权威定位仍来自词表）。
    EXPECT_EQ(initialized[2].unit.symbol(), "1");
    EXPECT_EQ(initialized[1].enumValues.size(), 5U);
}

// =====================================================================
// OPT-VER-103：补丁触及未授权/锁定绑定 → 生成阶段拒绝 OPT-VAR-LOCKED
// =====================================================================

TEST(OptVariables, PatchTouchingLockedBindingRejectedWithVarLocked_WP20T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-01", "OPT-02"}, std::vector<std::string>{"AT-09"});

    std::vector<VariableBinding> user = {
        makeContinuousBinding("mdl.joint[2].dh.a", 0.05, 0.40),
        makeContinuousBinding("mdl.drivetrain.ratio[1]", 40.0, 160.0),
    };
    user[0].diagSubject = "obj-00000000000000000000000000000aa0";  // 比较型对象定位
    const auto study = optimization::initializeBindings(OptimizationStage::StageB,
                                                        StudyInitialization::Refit, user);
    // 改型初始化后全部处于锁定态（未授权默认锁定，OPT-02）。
    ASSERT_TRUE(study[0].locked);
    ASSERT_TRUE(study[1].locked);

    const std::vector<PatchItem> items = {
        {"mdl.joint[2].dh.a", 0.30, 0, {}},  // 触及锁定绑定——必须被拒
        {"mdl.drivetrain.ratio[1]", 120.0, 0, {}},
    };
    // 诊断轨（编排面）：比较型拒绝＋定位（绑定 token＋对象）——不静默忽略
    // （§5.4；§2.2 红线 2）。
    const auto report
        = optimization::validatePatchItems(study, OptimizationStage::StageB, items);
    ASSERT_FALSE(report.ok());
    ASSERT_TRUE(report.hasCode(kLocked));
    // 逐条比较型定位（卡 §5.4：绑定 token＋对象定位——逐锁定绑定一条）。
    ASSERT_EQ(report.issues.size(), 2U);
    EXPECT_EQ(report.issues[0].bindingId, "mdl.joint[2].dh.a");
    EXPECT_EQ(report.issues[0].subject, "obj-00000000000000000000000000000aa0");
    EXPECT_EQ(report.issues[1].bindingId, "mdl.drivetrain.ratio[1]");
    // fail-fast 轨（调用方错误）：makeCandidatePatch 抛 OptimizationError，
    // stableCode 携带同一稳定码（错误语义双轨同源——§12.3）。
    try {
        optimization::makeCandidatePatch(study, OptimizationStage::StageB, items);
        FAIL() << "触及锁定变量的补丁必须被拒绝（OPT-VAR-LOCKED）";
    } catch (const OptimizationError& e) {
        EXPECT_EQ(e.stableCode(), kLocked);
    }
}

// =====================================================================
// OPT-VER-104：连续变量越界/非有限 → OPT-PATCH-ILLEGAL，不截断（NFR-COR-03）
// =====================================================================

TEST(OptVariables, ContinuousValueOutOfRangeRejectedNotClamped_WP20T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-02"}, std::vector<std::string>{"AT-09"});

    const std::vector<VariableBinding> study = {
        makeContinuousBinding("mdl.joint[2].dh.a", 0.05, 0.40),  // m
    };
    const auto checkIllegal = [&](double value) {
        const std::vector<PatchItem> items = {{"mdl.joint[2].dh.a", value, 0, {}}};
        const auto report
            = optimization::validatePatchItems(study, OptimizationStage::StageB, items);
        EXPECT_TRUE(report.hasCode(kPatchIllegal))
            << "值 " << value << " 应被拒（越界/非有限）";
        // 不静默截断：非法输入不产出补丁（fail-fast 轨抛出）。
        EXPECT_THROW(optimization::makeCandidatePatch(study, OptimizationStage::StageB, items),
                     OptimizationError);
    };
    checkIllegal(0.40 + 1e-9);        // 越上界（闭区间外）
    checkIllegal(0.05 - 1e-9);        // 越下界
    checkIllegal(std::nan(""));       // NaN 拒绝（I-OPT-9）
    checkIllegal(std::numeric_limits<double>::infinity());  // ±Inf 拒绝
    // 边界值本身合法（含端点闭区间——卡 §5.2"下界（含）/上界（含）"）。
    const std::vector<PatchItem> edge = {{"mdl.joint[2].dh.a", 0.05, 0, {}}};
    EXPECT_TRUE(optimization::validatePatchItems(study, OptimizationStage::StageB, edge).ok());
}

// =====================================================================
// OPT-VER-105：量化变量——round-half-even 网格对齐进 canonical；步长≤0 拒绝
// =====================================================================

TEST(OptVariables, QuantizedValueAlignedHalfEvenIntoCanonical_WP20T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-02"}, std::vector<std::string>{"AT-09"});

    // 量化使用形态（实现口径——词表 Continuous 条目的 Quantized 绑定声明，
    // 类型系统全阶段支持；卡 §5.3"量化变量说明"）。步长 0.25（2^-2）使
    // 半格判定在 IEEE 754 下精确：0.375/0.25＝1.5 无表示误差。
    const std::vector<VariableBinding> study = {
        makeContinuousBinding("mdl.drivetrain.ratio[1]", 40.0, 160.0,
                              VariableKind::Quantized, 0.25),
    };
    // 黄金值 1：0.375 → k＝1.5 恰半格 → floor＝1（奇）→ 上格 2×0.25＝0.50。
    // 黄金值 2：0.625 → k＝2.5 恰半格 → floor＝2（偶）→ 下格 2×0.25＝0.50。
    // 黄金值 3：0.30 → k＝1.2 → 下格 0.25；0.90 → k＝3.6 → 上格 1.00。
    EXPECT_DOUBLE_EQ(optimization::quantizeToStepHalfEven(0.375, 0.25), 0.50);
    EXPECT_DOUBLE_EQ(optimization::quantizeToStepHalfEven(0.625, 0.25), 0.50);
    EXPECT_DOUBLE_EQ(optimization::quantizeToStepHalfEven(0.30, 0.25), 0.25);
    EXPECT_DOUBLE_EQ(optimization::quantizeToStepHalfEven(0.90, 0.25), 1.00);

    // 非网格值补丁：对齐后入补丁（81.234 → 81.25；OPT-VER-105 观测点
    // "patch canonical"——对齐值是 canonical 的标量载荷）。
    const std::vector<PatchItem> items = {{"mdl.drivetrain.ratio[1]", 81.234, 0, {}}};
    const auto patch
        = optimization::makeCandidatePatch(study, OptimizationStage::StageB, items);
    ASSERT_EQ(patch.items.size(), 1U);
    EXPECT_DOUBLE_EQ(patch.items[0].scalarValue, 81.25);

    // 对齐后越界仍拒绝（不截断）：上界 81.3，值 81.234 → 对齐 81.25 合法；
    // 值 81.29 → 对齐 81.25（≤81.3 合法）；值 81.40 → 对齐 81.50 > 81.3 拒绝。
    auto upperBounded = study;
    upperBounded[0].upperBound = 81.3;
    const std::vector<PatchItem> overflow = {{"mdl.drivetrain.ratio[1]", 81.40, 0, {}}};
    const auto report = optimization::validatePatchItems(upperBounded,
                                                         OptimizationStage::StageB,
                                                         overflow);
    EXPECT_TRUE(report.hasCode(kPatchIllegal)) << "对齐后越界必须拒绝（不截断）";

    // 步长≤0 拒绝（绑定校验面——OPT-VER-105"步长≤0 拒绝"）。
    auto zeroStep = study;
    zeroStep[0].step = 0.0;
    const evidence::AnalysisSnapshot snapshot;  // 闭包核对不适用（无 diagSubject）
    optimization::OptimizationVariableProvider provider(OptimizationStage::StageB);
    const auto stepReport = provider.validateBindings(zeroStep, snapshot);
    EXPECT_TRUE(stepReport.hasCode(kInvalid)) << "量化绑定步长≤0 必须拒绝";
}

// =====================================================================
// OPT-VER-106：材料枚举——外值拒绝；合法切换经 overlay 携带 MaterialRef 键
// =====================================================================

TEST(OptVariables, MaterialEnumOutOfBoundsRejectedValidSwitchOverlaid_WP20T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-02"}, std::vector<std::string>{"AT-09"});

    const std::vector<VariableBinding> study = {
        makeEnumBinding("mdl.link[3].material", 0),
    };
    ASSERT_EQ(study[0].enumValues.size(), 5U);  // modeling 默认材料键集（5 键）
    // 枚举外值拒绝（下标越封闭值域）。
    const std::vector<PatchItem> illegal = {{"mdl.link[3].material", 0.0, 9, {}}};
    EXPECT_TRUE(optimization::validatePatchItems(study, OptimizationStage::StageB, illegal)
                    .hasCode(kPatchIllegal));
    // 合法切换（steel→aluminum）：overlay 条目携带枚举键文本——MaterialRef
    // 变更的物化载体（P-OPT-2 通道数据面；物性重估算链随裁决）。
    const std::vector<PatchItem> legal = {{"mdl.link[3].material", 0.0, 1, {}}};
    const auto patch = optimization::makeCandidatePatch(study, OptimizationStage::StageB, legal);
    const auto overlay
        = optimization::buildCandidateDesignOverlay(study, OptimizationStage::StageB, patch);
    ASSERT_EQ(overlay.entries.size(), 1U);
    EXPECT_EQ(overlay.entries[0].enumIndex, 1U);
    EXPECT_EQ(overlay.entries[0].enumValue, "aluminum");
}

// =====================================================================
// OPT-VER-108：StageB 激活电机型号绑定 → 研究定义校验拒绝 OPT-STAGE-LOCKED
// =====================================================================

TEST(OptVariables, StageBActivationOfMotorKeyRejectedWithStageLocked_WP20T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-02"}, std::vector<std::string>{"AT-09"});

    // 离散器件绑定实例（词表全量可匹配——阶段启用性由校验层判定，"未登记"
    // ≠"登记了但阶段不支持"，§5.7）。
    VariableBinding motor;
    motor.bindingId = "mdl.drivetrain.motor-key[1]";
    motor.kind = VariableKind::DiscreteDevice;
    motor.authorized = true;
    motor.locked = false;
    const std::vector<VariableBinding> study = {motor};

    const evidence::AnalysisSnapshot snapshot;
    optimization::OptimizationVariableProvider provider(OptimizationStage::StageB);
    const auto report = provider.validateBindings(study, snapshot);
    // 研究定义校验拒绝＋阶段锁定诊断；不降级、不丢弃（§2.2 红线 2/5——
    // 不得呈现为普通候选淘汰）。
    EXPECT_FALSE(report.ok()) << "StageB 引用离散器件必须阻塞";
    EXPECT_TRUE(report.hasCode(kStageLocked));
    // 补丁面同样阶段锁拒绝（生成阶段双闸）。
    const std::vector<PatchItem> items = {{"mdl.drivetrain.motor-key[1]", 0.0, 0, "motor-xyz"}};
    EXPECT_TRUE(optimization::validatePatchItems(study, OptimizationStage::StageB, items)
                    .hasCode(kStageLocked));
    EXPECT_THROW(optimization::makeCandidatePatch(study, OptimizationStage::StageB, items),
                 OptimizationError);
}

// =====================================================================
// I-OPT-7：权威互斥——同关节 DH/安装位置、基座预设/custom
// =====================================================================

TEST(OptVariables, AuthorityMutexGroupsRejectedOnDuplicateActivation_WP20T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-02"}, std::vector<std::string>{"AT-09"});

    const evidence::AnalysisSnapshot snapshot;
    optimization::OptimizationVariableProvider provider(OptimizationStage::StageB);
    // 同关节（[2]）DH 长度与安装位置 x 分量同时激活——权威互斥（I-OPT-7/
    // I-MDL-8：StandardDH/Explicit 权威互斥）。
    const std::vector<VariableBinding> conflicting = {
        makeContinuousBinding("mdl.joint[2].dh.a", 0.05, 0.40),
        makeContinuousBinding("mdl.joint[2].origin.t.x", -0.10, 0.10),
    };
    const auto conflict = provider.validateBindings(conflicting, snapshot);
    EXPECT_TRUE(conflict.hasCode(kInvalid)) << "同关节 DH/安装位置互斥";
    EXPECT_FALSE(conflict.ok());
    // 不同关节不互斥（组键实例化按关节索引隔离）。
    const std::vector<VariableBinding> distinct = {
        makeContinuousBinding("mdl.joint[2].dh.a", 0.05, 0.40),
        makeContinuousBinding("mdl.joint[3].origin.t.x", -0.10, 0.10),
    };
    EXPECT_TRUE(provider.validateBindings(distinct, snapshot).ok());
    // 基座预设与 custom（EAA 分量）互斥（MDL-22）。
    const std::vector<VariableBinding> baseConflict = {
        makeEnumBinding("mdl.base.orientation.preset", 0),
        makeContinuousBinding("mdl.base.orientation.eaa.x", -3.15, 3.15),
    };
    EXPECT_TRUE(provider.validateBindings(baseConflict, snapshot).hasCode(kInvalid));
}

// =====================================================================
// 绑定校验其余拒绝面（I-OPT-8/9＋Preflight #4 绑定级核对）
// =====================================================================

TEST(OptVariables, BindingValidationRejectsMalformedResearchDefinition_WP20T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-02"}, std::vector<std::string>{"AT-09"});

    const evidence::AnalysisSnapshot snapshot;
    optimization::OptimizationVariableProvider provider(OptimizationStage::StageB);
    // 空 bindingId。
    auto empty = makeContinuousBinding("mdl.joint[2].dh.a", 0.05, 0.40);
    empty.bindingId = {};
    EXPECT_TRUE(provider.validateBindings({empty}, snapshot).hasCode(kInvalid));
    // 未知 token（不在全量词表）。
    auto unknown = makeContinuousBinding("mdl.joint[2].dh.a", 0.05, 0.40);
    unknown.bindingId = "mdl.not-a-variable[1]";
    EXPECT_TRUE(provider.validateBindings({unknown}, snapshot).hasCode(kInvalid));
    // 边界退化（lower==upper / lower>upper——MDL-06④ 同式）。
    auto flat = makeContinuousBinding("mdl.joint[2].dh.a", 0.20, 0.20);
    EXPECT_TRUE(provider.validateBindings({flat}, snapshot).hasCode(kInvalid));
    // 私造枚举值域（≠词表封闭值域）。
    auto forged = makeEnumBinding("mdl.link[3].material", 0);
    forged.enumValues = {"steel", "unobtanium"};
    EXPECT_TRUE(provider.validateBindings({forged}, snapshot).hasCode(kInvalid));
    // 未授权但未锁定（§5.4 联动违约的手工矛盾态）。
    auto inconsistent = makeContinuousBinding("mdl.joint[2].dh.a", 0.05, 0.40);
    inconsistent.authorized = false;
    inconsistent.locked = false;
    EXPECT_TRUE(provider.validateBindings({inconsistent}, snapshot).hasCode(kInvalid));
}

TEST(OptVariables, BindingSubjectMustResideInSnapshotClosure_WP20T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-02"}, std::vector<std::string>{"AT-09"});

    // 快照组装：objectClosure 含一个真实对象（对象身份经 core::generate——
    // 测试夹具允许；生产中由 project 分配）。
    evidence::AnalysisSnapshot snapshot;
    snapshot.project = core::ProjectId::generate();
    snapshot.branch = core::BranchId::generate();
    snapshot.revision = core::RevisionId::generate();
    evidence::ObjectRefEntry entry;
    entry.objectId = core::ObjectId::generate();
    entry.contentVersion = core::ContentVersion::fromCanonical(
        "cv-00000000000000000000000000000000000000000000000000000000000000aa");
    entry.objectTypeToken = "robot-design";
    entry.digest.fill(1);
    snapshot.objectClosure = {entry};

    optimization::OptimizationVariableProvider provider(OptimizationStage::StageB);
    // diagSubject 指向闭包内对象 → 合法。
    auto inside = makeContinuousBinding("mdl.joint[2].dh.a", 0.05, 0.40);
    inside.diagSubject = entry.objectId.toCanonical();
    EXPECT_TRUE(provider.validateBindings({inside}, snapshot).ok());
    // diagSubject 指向闭包外对象 → 悬空拒绝（Preflight #4 的绑定级核对）。
    auto dangling = inside;
    dangling.bindingId = "mdl.joint[3].dh.a";
    dangling.diagSubject = core::ObjectId::generate().toCanonical();
    const auto report = provider.validateBindings({inside, dangling}, snapshot);
    EXPECT_TRUE(report.hasCode(kInvalid)) << "diagSubject 不在 objectClosure 必须拒绝";
    // diagSubject 文本非法（非 obj- 规范形态）→ 拒绝。
    auto malformed = inside;
    malformed.bindingId = "mdl.joint[4].dh.a";
    malformed.diagSubject = "rev-not-an-object-id";
    EXPECT_TRUE(provider.validateBindings({malformed}, snapshot).hasCode(kInvalid));
}
