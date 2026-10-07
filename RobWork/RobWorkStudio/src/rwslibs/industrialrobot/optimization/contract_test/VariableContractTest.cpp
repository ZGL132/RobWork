/**
 * @file   VariableContractTest.cpp
 * @brief  变量与候选补丁契约用例组（OptVariableContract）——跨单元契约面：
 *         OPT-VER-107 的契约承载（StageB 词表传动比"参数可编辑"启用位＋
 *         "性能可评估"禁用位的两阶段口径分离，V12-02/AT-09 回归反例）、
 *         I-MDL-11 值域契约、快照闭包核对契约（evidence AnalysisSnapshot
 *         公共头消费）——任务契约 WP-20-T03 acceptance 2/4。
 *
 * 设计依据：
 *   - units/optimization.md §5.3 #9（传动比词表行——c＝Δq_joint/Δθ_motor、
 *     StageB 放行）、§5.7（阶段锁表——"StageB 激活 drivetrain.ratio：放行"）、
 *     §13.1 OPT-VER-107（类型"模型＋契约"——本文件即契约半区）、§13.3
 *     AT-09 行（"drivetrain.ratio 不被阶段锁拒"观测点）、§16.3 P-OPT-2/
 *     P-OPT-3（裁决前允许范围＝研究定义/导出/应用组装面先行——本契约
 *     钉住数据面就绪而非物化实现）
 *   - 需求 OPT-02（V12-02 口径——REQUIREMENTS §15.0 表后注原文）、
 *     I-MDL-11（ratioPerJoint 逐项有限>0——modeling 卡值域，值域契约跨卡一致）
 *   - 先例：trajectory/dynamics 契约测试的对端面钉住形态（词表/边界以
 *     契约断言承载，随上游增量漂移即失败）
 */

#include <sdurws/ird/optimization/CandidatePatch.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/optimization/DiagCodes.hpp>
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/optimization/Variable.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace sdurws::ird;
using optimization::CandidatePatch;
using optimization::OptimizationStage;
using optimization::PatchItem;
using optimization::VariableBinding;
using optimization::VariableDefinition;
using optimization::VariableKind;

namespace {

constexpr auto kStageLocked = optimization::kOptStageLocked;
constexpr auto kIllegal = optimization::kOptPatchIllegal;

/// 在全量词表中按 tokenPattern 前缀精确查找条目（契约断言的定位器）。
const VariableDefinition* findPattern(const std::vector<VariableDefinition>& table,
                                      const std::string& pattern)
{
    for (const auto& d : table) {
        if (d.tokenPattern == pattern) {
            return &d;
        }
    }
    return nullptr;
}

/// 实例化一个已授权的传动比绑定（与模型测试同构——契约面独立自持）。
VariableBinding makeRatioBinding(double lower, double upper)
{
    VariableBinding b;
    b.bindingId = "mdl.drivetrain.ratio[1]";
    b.kind = VariableKind::Continuous;
    if (const auto u = core::UnitToken::find("1")) {
        b.unit = *u;
    }
    b.lowerBound = lower;
    b.upperBound = upper;
    b.authorityFieldPath = "robot-drivetrain/ratioPerJoint[j]";
    b.authorized = true;
    b.locked = false;
    return b;
}

/// 实例化一个已授权的 DH 长度绑定（与 ratio 组成多变量补丁——供 diff 的
/// "不变项"侧：两侧同值不出现在差异输出中，I-OPT-10）。
VariableBinding makeDhBinding(double lower, double upper)
{
    VariableBinding b;
    b.bindingId = "mdl.joint[2].dh.a";
    b.kind = VariableKind::Continuous;
    if (const auto u = core::UnitToken::find("m")) {
        b.unit = *u;
    }
    b.lowerBound = lower;
    b.upperBound = upper;
    b.authorityFieldPath = "robot-design/joints[i]/dh/a";
    b.authorized = true;
    b.locked = false;
    return b;
}

/// 实例化一个材料枚举绑定（值域从词表回填——防私造枚举项的校验强制；
/// 供 diff 的 "added" 项：b 有 a 无）。
VariableBinding makeMaterialBinding(int linkIndex)
{
    VariableBinding b;
    b.bindingId = "mdl.link[" + std::to_string(linkIndex) + "].material";
    b.kind = VariableKind::Enumeration;
    b.authorityFieldPath = "robot-design/links[i]/body/material";
    const VariableDefinition* def
        = optimization::matchDefinition(b.bindingId, OptimizationStage::StageD);
    if (def != nullptr) {
        b.enumValues = def->enumValues;  // modeling 默认材料键集（5 键——词表
                                         // 唯一定义点，绑定不得私造）
    }
    b.authorized = true;
    b.locked = false;
    return b;
}

}  // namespace

// =====================================================================
// OPT-VER-107 契约半区：两阶段口径分离（参数可编辑 vs 性能可评估）
// =====================================================================

TEST(OptVariableContract, StageBKeepsRatioEditableWhilePerformanceStaysStageD_WP20T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-02"}, std::vector<std::string>{"AT-09"});

    // 契约 1：StageB 词表登记传动比且为连续量（REQUIREMENTS §15.0 OPT-B 行
    // "传动比 drivetrain.ratio（连续量，StageB 绑定）"；V12-02 参数可编辑）。
    const auto& stageB = optimization::builtinVariableDefinitions(OptimizationStage::StageB);
    const VariableDefinition* ratio = findPattern(stageB, "mdl.drivetrain.ratio[j]");
    ASSERT_NE(ratio, nullptr) << "StageB 词表必须登记传动比（V12-02）";
    EXPECT_EQ(ratio->kind, VariableKind::Continuous);
    EXPECT_EQ(ratio->unitSymbol, "1");
    EXPECT_TRUE(ratio->enabledInStageB) << "StageB 传动比启用位必须为真（AT-09 回归反例）";
    // 值域契约：c 口径无量纲、值 >0 且有限（I-MDL-11——值域跨卡一致）。
    EXPECT_TRUE(ratio->valueMustBePositive);
    // 契约 2：完整驱动性能评估（器件联合）归 OPT-D——离散器件条目在
    // 全量词表登记但 StageB 禁用位为假（"参数可编辑"≠"性能可评估"，
    // DOPT-15 两阶段口径分离；StageB 词表不含该条目＝阶段词表视图的正确
    // 形态，阶段锁判定走条目启用位——见 matchDefinition 全量匹配口径）。
    const auto& full = optimization::builtinVariableDefinitions(OptimizationStage::StageD);
    const VariableDefinition* motor = findPattern(full, "mdl.drivetrain.motor-key[j]");
    ASSERT_NE(motor, nullptr) << "全量词表须登记电机型号（StageD 变量）";
    EXPECT_FALSE(motor->enabledInStageB);
    EXPECT_TRUE(motor->enabledInStageD);
    EXPECT_EQ(findPattern(stageB, "mdl.drivetrain.motor-key[j]"), nullptr)
        << "StageB 阶段词表视图不得暴露离散器件条目";
    // 契约 3：生成面行为差异——传动比补丁放行、电机型号补丁阶段锁拒绝
    // （同一补丁批内逐项判定——阶段锁只落在越界变量上，不波及传动比）。
    const VariableBinding ratioBinding = makeRatioBinding(40.0, 160.0);
    VariableBinding motorBinding;
    motorBinding.bindingId = "mdl.drivetrain.motor-key[1]";
    motorBinding.kind = VariableKind::DiscreteDevice;
    motorBinding.authorized = true;
    motorBinding.locked = false;
    const std::vector<VariableBinding> study = {ratioBinding, motorBinding};
    const auto report = optimization::validatePatchItems(
        study, OptimizationStage::StageB,
        {{"mdl.drivetrain.ratio[1]", 120.0, 0, {}},
         {"mdl.drivetrain.motor-key[1]", 0.0, 0, "motor-xyz"}});
    EXPECT_TRUE(report.hasCode(kStageLocked)) << "电机型号在 StageB 必须阶段锁拒绝";
    EXPECT_FALSE(report.hasCode(kIllegal)) << "合法补丁项不得携带补丁非法类问题";
    for (const auto& i : report.issues) {
        // ★ AT-09 回归反例：传动比条目不得出现在任何拒绝中。
        EXPECT_NE(i.bindingId, "mdl.drivetrain.ratio[1]")
            << "传动比不得被阶段锁拒绝（AT-09/V12-02）";
    }
}

TEST(OptVariableContract, RatioValueDomainPositiveFiniteEnforced_WP20T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-02"}, std::vector<std::string>{"AT-09"});

    // I-MDL-11 值域契约在补丁面强制：值 ≤0 拒绝（非发明阈值——需求原文
    // 值域）；0.5～500 的工程内取值放行（边界由用户按机型填写，P-03）。
    const std::vector<VariableBinding> study = {makeRatioBinding(0.5, 500.0)};
    const auto rejectZero
        = optimization::validatePatchItems(study, OptimizationStage::StageB,
                                           {{"mdl.drivetrain.ratio[1]", 0.0, 0, {}}});
    EXPECT_TRUE(rejectZero.hasCode(kIllegal));
    const auto rejectNegative
        = optimization::validatePatchItems(study, OptimizationStage::StageB,
                                           {{"mdl.drivetrain.ratio[1]", -10.0, 0, {}}});
    EXPECT_TRUE(rejectNegative.hasCode(kIllegal));
    const auto acceptSmall
        = optimization::validatePatchItems(study, OptimizationStage::StageB,
                                           {{"mdl.drivetrain.ratio[1]", 0.5, 0, {}}});
    EXPECT_TRUE(acceptSmall.ok());
}

TEST(OptVariableContract, BindingValidationConsumesSnapshotClosureContract_WP20T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-02"}, std::vector<std::string>{"AT-09"});

    // 跨单元契约：validateBindings 只读消费 evidence::AnalysisSnapshot 的
    // objectClosure（diagSubject ⊆ 闭包）——不写快照、不读对象字节
    // （P-OPT-2 裁决前研究定义面先行；基线内容核验归 Preflight #14）。
    evidence::AnalysisSnapshot snapshot;
    snapshot.project = core::ProjectId::generate();
    snapshot.branch = core::BranchId::generate();
    snapshot.revision = core::RevisionId::generate();
    evidence::ObjectRefEntry dt;
    dt.objectId = core::ObjectId::generate();
    dt.contentVersion = core::ContentVersion::fromCanonical(
        "cv-00000000000000000000000000000000000000000000000000000000000000bb");
    dt.objectTypeToken = "robot-drivetrain";
    dt.digest.fill(2);
    snapshot.objectClosure = {dt};

    VariableBinding ratio = makeRatioBinding(40.0, 160.0);
    ratio.diagSubject = dt.objectId.toCanonical();
    optimization::OptimizationVariableProvider provider(OptimizationStage::StageB);
    EXPECT_TRUE(provider.validateBindings({ratio}, snapshot).ok());

    // 闭包外对象 → 悬空拒绝（对比断言钉住"核对真的执行了"）。
    VariableBinding stray = makeRatioBinding(40.0, 160.0);
    stray.bindingId = "mdl.drivetrain.ratio[2]";
    stray.diagSubject = core::ObjectId::generate().toCanonical();
    const auto report = provider.validateBindings({ratio, stray}, snapshot);
    EXPECT_FALSE(report.ok());
    EXPECT_TRUE(report.hasCode(optimization::kOptInputInvalid));
}

// =====================================================================
// I-OPT-10 接口路径契约：diffCandidatePatch 必须经 IOptimizationVariableProvider
// 接口可用（§12.2 契约承诺）——返工钉扎用例（首轮验收 B-1：接口实现为成员
// 无限自递归、27 个用例全走自由函数漏过该路径；本用例以基类引用消费虚函数，
// 接口实现若有回归（自递归/缺失转发）即栈溢出崩溃或输出不一致——红）
// =====================================================================

TEST(OptVariableContract, DiffCandidatePatchViaProviderInterface_WP20T03_B1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-02", "I-OPT-10"},
                  std::vector<std::string>{});

    const std::vector<VariableBinding> study = {
        makeRatioBinding(40.0, 160.0),
        makeDhBinding(0.05, 0.40),
        makeMaterialBinding(3),
    };
    // 补丁 a：ratio=100、dh=0.30；补丁 b：ratio=120（修改 'M'）、dh=0.30
    // （不变——不出现）、material=aluminum（新增 'A'，下标 1）。
    const CandidatePatch a = optimization::makeCandidatePatch(
        study, OptimizationStage::StageB,
        {{"mdl.drivetrain.ratio[1]", 100.0, 0, {}}, {"mdl.joint[2].dh.a", 0.30, 0, {}}});
    const CandidatePatch b = optimization::makeCandidatePatch(
        study, OptimizationStage::StageB,
        {{"mdl.drivetrain.ratio[1]", 120.0, 0, {}},
         {"mdl.joint[2].dh.a", 0.30, 0, {}},
         {"mdl.link[3].material", 0.0, 1, {}}});

    // ★ 关键形态：以**基类接口引用**持有实例并调用虚函数——强制走
    //   IOptimizationVariableProvider::diffCandidatePatch 的实现路径（§12.2
    //   契约承诺；若经具体类型 OptimizationVariableProvider 调用，编译器可
    //   静态绑定成员，无法钉住接口实现的正确性）。
    const optimization::OptimizationVariableProvider provider(OptimizationStage::StageB);
    const optimization::IOptimizationVariableProvider& iface = provider;
    const auto viaIface = iface.diffCandidatePatch(a, b);

    // 契约 1：接口路径输出与自由函数路径输出**逐字段一致**（成员实现＝
    // 自由函数薄转发——两路输出分叉即实现违约；一致断言比单侧断言强：
    // 防未来两路语义漂移）。
    const auto viaFree = optimization::diffCandidatePatch(a, b);
    ASSERT_EQ(viaIface.size(), viaFree.size());
    ASSERT_EQ(viaIface.size(), 2U);  // ratio 修改＋material 新增；dh 不变不出现
    for (std::size_t k = 0; k < viaIface.size(); ++k) {
        EXPECT_EQ(viaIface[k].bindingId, viaFree[k].bindingId) << "差异条目序 " << k;
        EXPECT_EQ(viaIface[k].kind, viaFree[k].kind) << "差异条目序 " << k;
        EXPECT_DOUBLE_EQ(viaIface[k].oldScalar, viaFree[k].oldScalar);
        EXPECT_DOUBLE_EQ(viaIface[k].newScalar, viaFree[k].newScalar);
        EXPECT_EQ(viaIface[k].oldEnumIndex, viaFree[k].oldEnumIndex);
        EXPECT_EQ(viaIface[k].newEnumIndex, viaFree[k].newEnumIndex);
        EXPECT_EQ(viaIface[k].oldDiscreteRef, viaFree[k].oldDiscreteRef);
        EXPECT_EQ(viaIface[k].newDiscreteRef, viaFree[k].newDiscreteRef);
        EXPECT_EQ(viaIface[k].authorityFieldPath, viaFree[k].authorityFieldPath);
    }
    // 契约 2：语义正确性抽查（与自由函数测试互证——M/A 两类＋稳定序）。
    EXPECT_EQ(viaIface[0].bindingId, "mdl.drivetrain.ratio[1]");
    EXPECT_EQ(viaIface[0].kind, 'M');
    EXPECT_DOUBLE_EQ(viaIface[0].oldScalar, 100.0);
    EXPECT_DOUBLE_EQ(viaIface[0].newScalar, 120.0);
    EXPECT_EQ(viaIface[1].bindingId, "mdl.link[3].material");
    EXPECT_EQ(viaIface[1].kind, 'A');
    EXPECT_EQ(viaIface[1].newEnumIndex, 1U);
    // 契约 3：反向 diff 经接口同样可用（removed 'D' 类——§10.4 差异预览的
    // 双向消费面；自递归缺陷对任何调用方向都是致命的，双向各钉一次）。
    const auto reverse = iface.diffCandidatePatch(b, a);
    ASSERT_EQ(reverse.size(), 2U);
    EXPECT_EQ(reverse[1].bindingId, "mdl.link[3].material");
    EXPECT_EQ(reverse[1].kind, 'D');
    // 契约 4：无差异补丁对返回空表（相同输入不出现在输出——I-OPT-10）。
    EXPECT_TRUE(iface.diffCandidatePatch(a, a).empty());
}

// =====================================================================
// P-OPT-4 词表契约钉扎（返工 G-1 消账）：1b 截面类型枚举保留登记但两阶段
// 均禁用——"登记不删除、引用即阶段锁"的数据位＋行为双断言，防后续任务
// 误开（schema 增量裁决前锁定，units/optimization.md §5.3 #1b/§16.3）
// =====================================================================

TEST(OptVariableContract, SectionTypeEntryKeptRegisteredButDisabledInBothStages_WP20T03_G1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-02"}, std::vector<std::string>{});

    // 契约 1（数据位）：条目在 StageB/StageD 两份词表视图中**均在册**（保留
    // 登记不删除——删除会使引用报"未知绑定"而非阶段锁，语义混同 AT-09 同型
    // 反例）；P-OPT-4 裁决前启用位两阶段均为 false。
    const auto& stageB = optimization::builtinVariableDefinitions(OptimizationStage::StageB);
    const auto& stageD = optimization::builtinVariableDefinitions(OptimizationStage::StageD);
    const VariableDefinition* secB = findPattern(stageB, "mdl.link[i].section.type");
    const VariableDefinition* secD = findPattern(stageD, "mdl.link[i].section.type");
    ASSERT_NE(secB, nullptr) << "1b 截面类型必须保留登记（P-OPT-4——登记不删除）";
    ASSERT_NE(secD, nullptr) << "全量词表同样在册（matchDefinition 匹配域口径）";
    EXPECT_EQ(secB->kind, VariableKind::Enumeration);
    EXPECT_EQ(secB->unitSymbol, "") << "枚举值非物理量——单位列为空";
    EXPECT_EQ(secB->authorityFieldPath, "robot-design/links[i]/shape/section/type");
    // 封闭值域＝modeling §5.3 SegmentSpec primitive 词表三值（卡面 §5.3 行 1b）。
    const std::vector<std::string> expected
        = {"SolidCylinder", "HollowCylinder", "Box"};
    EXPECT_EQ(secB->enumValues, expected);
    EXPECT_FALSE(secB->enabledInStageB) << "P-OPT-4：StageB 启用位必须为 false";
    EXPECT_FALSE(secB->enabledInStageD) << "P-OPT-4：StageD 启用位同样为 false";

    // 契约 2（行为位）：StageB 引用该变量即 OPT-STAGE-LOCKED 拒绝（§5.7
    // 阶段锁——不降级、不丢弃；两阶段禁用位若被误开，本断言随数据位一起红）。
    VariableBinding secBinding;
    secBinding.bindingId = "mdl.link[1].section.type";
    secBinding.kind = VariableKind::Enumeration;
    secBinding.enumValues = expected;  // 值域对齐词表（非拒绝主因——阶段锁优先）
    secBinding.authorized = true;
    secBinding.locked = false;
    const auto report = optimization::validatePatchItems(
        {secBinding}, OptimizationStage::StageB,
        {{"mdl.link[1].section.type", 0.0, 0, {}}});
    EXPECT_FALSE(report.ok()) << "截面类型枚举 P-OPT-4 裁决前不得放行";
    EXPECT_TRUE(report.hasCode(kStageLocked));
}
