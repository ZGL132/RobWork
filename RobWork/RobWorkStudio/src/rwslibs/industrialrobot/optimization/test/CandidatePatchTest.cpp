/**
 * @file   CandidatePatchTest.cpp
 * @brief  候选补丁用例组（OptCandidatePatch）——确定性序列化与身份
 *         （acceptance 3：相同补丁必得相同 CandidatePatchId；候选身份不含
 *         显示单位/UI 状态）、OPT-VER-107（传动比 StageB 可编辑并编译进
 *         候选——V12-02/AT-09 回归反例）、diff（I-OPT-10）与 canonical
 *         字节布局黄金核对——任务契约 WP-20-T03 acceptance 2/3 的模型面。
 *
 * 设计依据：
 *   - units/optimization.md §5.5（确定性序列化六条）、§4.2（CandidateId
 *     公式与六层身份）、§5.7（StageB 激活传动比放行——DOPT-15）、§13.1
 *     OPT-VER-107（观测点：候选编译身份；评估请求切片——P-OPT-2 裁决前
 *     以覆盖视图 patchId＋条目值承载）、§8.2 变量编码（c＝Δq_joint/
 *     Δθ_motor 口径注释）
 *   - 需求 OPT-02（V12-02）、AT-09（drivetrain.ratio 回归）
 */

#include <sdurws/ird/optimization/CandidatePatch.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/optimization/DiagCodes.hpp>
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/optimization/Variable.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

using namespace sdurws::ird;
using optimization::CandidateId;
using optimization::CandidatePatch;
using optimization::OptimizationError;
using optimization::OptimizationStage;
using optimization::PatchItem;
using optimization::PatchValueTag;
using optimization::VariableBinding;
using optimization::VariableKind;

namespace {

constexpr auto kIllegal = optimization::kOptPatchIllegal;
constexpr auto kStageLocked = optimization::kOptStageLocked;

/// 实例化一个已授权的传动比绑定（StageB 主角变量——V12-02）。
VariableBinding makeRatioBinding(double lower, double upper)
{
    VariableBinding b;
    b.bindingId = "mdl.drivetrain.ratio[1]";
    b.kind = VariableKind::Continuous;
    const auto u = core::UnitToken::find("1");
    if (u.has_value()) {
        b.unit = *u;
    }
    b.lowerBound = lower;
    b.upperBound = upper;
    b.authorityFieldPath = "robot-drivetrain/ratioPerJoint[j]";
    b.diagSubject = "obj-00000000000000000000000000000d7e";  // robot-drivetrain 对象定位
    b.authorized = true;
    b.locked = false;
    return b;
}

/// 实例化一个已授权的 DH 长度绑定（与 ratio 组成多变量补丁）。
VariableBinding makeDhBinding(double lower, double upper)
{
    VariableBinding b;
    b.bindingId = "mdl.joint[2].dh.a";
    b.kind = VariableKind::Continuous;
    const auto u = core::UnitToken::find("m");
    if (u.has_value()) {
        b.unit = *u;
    }
    b.lowerBound = lower;
    b.upperBound = upper;
    b.authorityFieldPath = "robot-design/joints[i]/dh/a";
    b.authorized = true;
    b.locked = false;
    return b;
}

}  // namespace

// =====================================================================
// acceptance 3：确定性序列化——相同补丁必得相同 CandidatePatchId
// =====================================================================

TEST(OptCandidatePatch, IdenticalPatchesProduceIdenticalIdentity_WP20T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-02", "NFR-COR-02"},
                  std::vector<std::string>{"AT-09"});

    const std::vector<VariableBinding> study = {
        makeRatioBinding(40.0, 160.0),
        makeDhBinding(0.05, 0.40),
    };
    // 两份"语义相同"的补丁：items 输入顺序不同（乱序生成是常态——生成器
    // 不保证序）、label 不同（UI 标签——I-OPT-8 不入身份）。
    const std::vector<PatchItem> itemsA = {
        {"mdl.drivetrain.ratio[1]", 120.0, 0, {}},
        {"mdl.joint[2].dh.a", 0.30, 0, {}},
    };
    const std::vector<PatchItem> itemsB = {  // 相同取值、相反顺序
        {"mdl.joint[2].dh.a", 0.30, 0, {}},
        {"mdl.drivetrain.ratio[1]", 120.0, 0, {}},
    };
    const CandidatePatch a = optimization::makeCandidatePatch(study, OptimizationStage::StageB,
                                                              itemsA, "候选 A");
    const CandidatePatch b = optimization::makeCandidatePatch(study, OptimizationStage::StageB,
                                                              itemsB, "候选 B-不同标签");
    // ① items 规范化为 bindingId 字典序（构造边界强制——卡 §5.5 ①）。
    ASSERT_EQ(a.items.size(), 2U);
    EXPECT_LT(a.items[0].bindingId, a.items[1].bindingId);
    // ② 相同语义补丁 ⇒ 相同字节（乱序输入下仍成立——canonicalize 内部
    //    重排序兜底）。
    EXPECT_EQ(optimization::canonicalize(a), optimization::canonicalize(b));
    // ③ ⇒ 相同 CandidatePatchId（跨运行稳定、天然去重——§4.2）。
    EXPECT_EQ(optimization::patchIdentity(a), optimization::patchIdentity(b));
    // ④ 候选身份公式稳定性：同基线 ⇒ 同 CandidateId。
    const core::ObjectId root = core::ObjectId::fromCanonical(
        "obj-00000000000000000000000000000001");
    const core::ContentVersion cv = core::ContentVersion::fromCanonical(
        "cv-0000000000000000000000000000000000000000000000000000000000000001");
    EXPECT_EQ(optimization::candidateIdOf(root, cv, a),
              optimization::candidateIdOf(root, cv, b));
    // ⑤ 不同补丁 ⇒ 不同身份（内容寻址的区分力）。
    const std::vector<PatchItem> itemsC = {{"mdl.drivetrain.ratio[1]", 121.0, 0, {}},
                                           {"mdl.joint[2].dh.a", 0.30, 0, {}}};
    const CandidatePatch c = optimization::makeCandidatePatch(study, OptimizationStage::StageB,
                                                              itemsC);
    EXPECT_NE(optimization::patchIdentity(a), optimization::patchIdentity(c));
    EXPECT_NE(optimization::candidateIdOf(root, cv, a),
              optimization::candidateIdOf(root, cv, c));
    // ⑥ CandidateId 规范文本＝"cnd-<64 小写 hex>"（本域自有格式，DOPT-3）。
    const CandidateId cid = optimization::candidateIdOf(root, cv, a);
    const std::string text = cid.toCanonical();
    ASSERT_EQ(text.size(), 68U);  // 4 前缀＋64 hex
    EXPECT_EQ(text.substr(0, 4), "cnd-");
    EXPECT_TRUE(cid.isValid());
}

TEST(OptCandidatePatch, CandidateIdDependsOnBaselineAndPatchOnly_WP20T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-02"}, std::vector<std::string>{"AT-09"});

    const std::vector<VariableBinding> study = {makeRatioBinding(40.0, 160.0)};
    const std::vector<PatchItem> items = {{"mdl.drivetrain.ratio[1]", 100.0, 0, {}}};
    const CandidatePatch patch
        = optimization::makeCandidatePatch(study, OptimizationStage::StageB, items);
    const core::ObjectId root = core::ObjectId::fromCanonical(
        "obj-00000000000000000000000000000001");
    const core::ContentVersion cv1 = core::ContentVersion::fromCanonical(
        "cv-0000000000000000000000000000000000000000000000000000000000000001");
    const core::ContentVersion cv2 = core::ContentVersion::fromCanonical(
        "cv-0000000000000000000000000000000000000000000000000000000000000002");
    // 基线内容版本进入候选身份（公式三要素之二）——基线变则候选变。
    EXPECT_NE(optimization::candidateIdOf(root, cv1, patch),
              optimization::candidateIdOf(root, cv2, patch));
    // 保留值全零基线拒绝（调用方契约违约 fail-fast——候选身份不可与
    // "未初始化"歧义）。
    const core::ObjectId zeroRoot{};
    EXPECT_THROW(optimization::candidateIdOf(zeroRoot, cv1, patch), OptimizationError);
}

TEST(OptCandidatePatch, EmptyPatchIsLegalBaselineCandidate_WP20T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-02"}, std::vector<std::string>{"AT-09"});

    // 空补丁合法（基线候选——卡 §5.5 ③；OPT-11 Evaluate Baseline 的执行
    // 形态载体）：canonical＝16 字节头，身份有效且确定。
    const CandidatePatch empty{};
    const auto bytes = optimization::canonicalize(empty);
    ASSERT_EQ(bytes.size(), 16U);
    EXPECT_EQ(std::string(bytes.begin(), bytes.begin() + 8), "IRDOPTP1");
    EXPECT_EQ(bytes[8], 0x01);  // codecVersion＝1（小端 u32 低字节）
    EXPECT_EQ(bytes[12], 0x00); // itemCount＝0（低字节）
    const auto id = optimization::patchIdentity(empty);
    EXPECT_TRUE(id.isValid());
    // 空补丁身份确定（两次计算一致）且与非空补丁不同。
    EXPECT_EQ(id, optimization::patchIdentity(CandidatePatch{}));
    const std::vector<VariableBinding> study = {makeRatioBinding(40.0, 160.0)};
    const CandidatePatch one
        = optimization::makeCandidatePatch(study, OptimizationStage::StageB,
                                           {{"mdl.drivetrain.ratio[1]", 90.0, 0, {}}});
    EXPECT_NE(id, optimization::patchIdentity(one));
    // 空补丁的覆盖视图＝空条目＋patchId 仍有效（基线候选的空覆盖）。
    const auto overlay
        = optimization::buildCandidateDesignOverlay(study, OptimizationStage::StageB, empty);
    EXPECT_TRUE(overlay.entries.empty());
    EXPECT_EQ(overlay.patchId, id);
}

// =====================================================================
// acceptance 3：拒绝形态——重复/未知/空 bindingId
// =====================================================================

TEST(OptCandidatePatch, MalformedPatchItemsRejected_WP20T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-02"}, std::vector<std::string>{"AT-09"});

    const std::vector<VariableBinding> study = {
        makeRatioBinding(40.0, 160.0),
        makeDhBinding(0.05, 0.40),
    };
    // 重复 bindingId：同一绑定出现两次——语义冲突，无法定义确定性序。
    const std::vector<PatchItem> duplicated = {
        {"mdl.drivetrain.ratio[1]", 100.0, 0, {}},
        {"mdl.drivetrain.ratio[1]", 110.0, 0, {}},
    };
    EXPECT_TRUE(optimization::validatePatchItems(study, OptimizationStage::StageB, duplicated)
                    .hasCode(kIllegal));
    EXPECT_THROW(optimization::makeCandidatePatch(study, OptimizationStage::StageB, duplicated),
                 OptimizationError);
    // 未知绑定（未在研究定义登记）。
    const std::vector<PatchItem> unknown = {{"mdl.joint[9].dh.d", 0.20, 0, {}}};
    EXPECT_TRUE(optimization::validatePatchItems(study, OptimizationStage::StageB, unknown)
                    .hasCode(kIllegal));
    // 空 bindingId。
    const std::vector<PatchItem> blank = {{}, };
    EXPECT_TRUE(optimization::validatePatchItems(study, OptimizationStage::StageB, blank)
                    .hasCode(kIllegal));
}

// =====================================================================
// I-OPT-10：补丁差异——稳定顺序、三类差异、相同项不出现
// =====================================================================

TEST(OptCandidatePatch, DiffReportsStableOrderedEntries_WP20T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-02"}, std::vector<std::string>{"AT-09"});

    const std::vector<VariableBinding> study = {
        makeRatioBinding(40.0, 160.0),
        makeDhBinding(0.05, 0.40),
    };
    // a：ratio=100、dh=0.30；b：ratio=120（修改）、dh=0.30（不变）、
    //    新增 material、移除无效项不设——补丁只含登记绑定的取值。
    const CandidatePatch a = optimization::makeCandidatePatch(
        study, OptimizationStage::StageB,
        {{"mdl.drivetrain.ratio[1]", 100.0, 0, {}}, {"mdl.joint[2].dh.a", 0.30, 0, {}}});
    const std::vector<VariableBinding> studyB = study;
    CandidatePatch b = optimization::makeCandidatePatch(
        studyB, OptimizationStage::StageB,
        {{"mdl.drivetrain.ratio[1]", 120.0, 0, {}}, {"mdl.joint[2].dh.a", 0.30, 0, {}}});
    // 为构造 added 项：b 追加一个 a 没有的材料取值（绑定集补材料绑定——
    // 值域必须从词表回填，空值域会使任何下标越界）。
    auto material = studyB.front();
    material.bindingId = "mdl.link[3].material";
    material.kind = VariableKind::Enumeration;
    material.authorityFieldPath = "robot-design/links[i]/body/material";
    const optimization::VariableDefinition* materialDef
        = optimization::matchDefinition("mdl.link[3].material", OptimizationStage::StageD);
    ASSERT_NE(materialDef, nullptr);
    material.enumValues = materialDef->enumValues;  // modeling 默认材料键集（5 键）
    std::vector<VariableBinding> studyC = study;
    studyC.push_back(material);
    b = optimization::makeCandidatePatch(
        studyC, OptimizationStage::StageB,
        {{"mdl.drivetrain.ratio[1]", 120.0, 0, {}},
         {"mdl.joint[2].dh.a", 0.30, 0, {}},
         {"mdl.link[3].material", 0.0, 1, {}}});

    const auto diffs = optimization::diffCandidatePatch(a, b);
    ASSERT_EQ(diffs.size(), 2U);  // ratio 修改＋material 新增；dh 不变不出现
    // 输出按 bindingId 字典序（稳定顺序——差异预览与导出直接消费）。
    EXPECT_EQ(diffs[0].bindingId, "mdl.drivetrain.ratio[1]");
    EXPECT_EQ(diffs[0].kind, 'M');
    EXPECT_DOUBLE_EQ(diffs[0].oldScalar, 100.0);
    EXPECT_DOUBLE_EQ(diffs[0].newScalar, 120.0);
    // 权威字段定位随差异条目（I-OPT-10——词表形态；对象定位随绑定上下文）。
    EXPECT_EQ(diffs[0].authorityFieldPath, "robot-drivetrain/ratioPerJoint[j]");
    EXPECT_EQ(diffs[1].bindingId, "mdl.link[3].material");
    EXPECT_EQ(diffs[1].kind, 'A');
    EXPECT_EQ(diffs[1].newEnumIndex, 1U);
    // 反向 diff：material 移除（'D'）。
    const auto reverse = optimization::diffCandidatePatch(b, a);
    ASSERT_EQ(reverse.size(), 2U);
    EXPECT_EQ(reverse[1].bindingId, "mdl.link[3].material");
    EXPECT_EQ(reverse[1].kind, 'D');
}

// =====================================================================
// OPT-VER-107：drivetrain.ratio StageB 可编辑并编译进候选（V12-02/AT-09）
// =====================================================================

TEST(OptCandidatePatch, StageBRatioPassesStageLockAndCompilesIntoOverlay_WP20T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-02"}, std::vector<std::string>{"AT-09"});

    const std::vector<VariableBinding> study = {makeRatioBinding(40.0, 160.0)};
    const std::vector<PatchItem> items = {{"mdl.drivetrain.ratio[1]", 120.0, 0, {}}};
    // ★ AT-09 回归反例的核心断言：StageB 激活传动比**不被阶段锁错误拒绝**
    //   （V12-02——传动比是 StageB 连续变量，"参数可编辑"与"性能可评估"
    //   为两个阶段口径，DOPT-15）。补丁合法（无任何 STAGE-LOCKED/拒绝）。
    const auto report
        = optimization::validatePatchItems(study, OptimizationStage::StageB, items);
    EXPECT_TRUE(report.ok()) << "传动比不得被阶段锁拒绝（AT-09 回归反例）";
    EXPECT_FALSE(report.hasCode(kStageLocked));
    const CandidatePatch patch
        = optimization::makeCandidatePatch(study, OptimizationStage::StageB, items);
    ASSERT_EQ(patch.items.size(), 1U);
    EXPECT_DOUBLE_EQ(patch.items[0].scalarValue, 120.0);
    // 候选身份可计算（编译身份要素——补丁侧；runtime 侧物化随 P-OPT-2 裁决）。
    const core::ObjectId root = core::ObjectId::fromCanonical(
        "obj-00000000000000000000000000000001");
    const core::ContentVersion cv = core::ContentVersion::fromCanonical(
        "cv-0000000000000000000000000000000000000000000000000000000000000001");
    const CandidateId cid = optimization::candidateIdOf(root, cv, patch);
    EXPECT_TRUE(cid.isValid());
    // 覆盖视图（P-OPT-2 数据面）：ratio 条目携带 c 口径值
    // （c＝Δq_joint/Δθ_motor，无量纲 "1"——与 drivetrain/runtime 一致）；
    // 评估请求切片消费该视图随裁决（T06 管线面）。
    const auto overlay
        = optimization::buildCandidateDesignOverlay(study, OptimizationStage::StageB, patch);
    ASSERT_EQ(overlay.entries.size(), 1U);
    EXPECT_EQ(overlay.entries[0].bindingId, "mdl.drivetrain.ratio[1]");
    EXPECT_EQ(overlay.entries[0].valueTag, PatchValueTag::Scalar);
    EXPECT_DOUBLE_EQ(overlay.entries[0].scalarValue, 120.0);
    EXPECT_EQ(overlay.entries[0].unitSymbol, "1");
    EXPECT_EQ(overlay.entries[0].authorityFieldPath, "robot-drivetrain/ratioPerJoint[j]");
    EXPECT_EQ(overlay.entries[0].diagSubject, "obj-00000000000000000000000000000d7e");
    EXPECT_TRUE(overlay.patchId.isValid());
}

// =====================================================================
// canonical 字节布局黄金核对（codecVersion 1 的编码契约钉住）
// =====================================================================

TEST(OptCandidatePatch, CanonicalByteLayoutGoldenCheck_WP20T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-02"}, std::vector<std::string>{"AT-09"});

    // 单标量项 "mdl.drivetrain.ratio[2]" 值 81.25 的期望字节（独立拼装——
    // 布局契约：magic[8]‖u32LE version‖u32LE count‖u16LE len‖token‖'S'‖f64LE）。
    const CandidatePatch patch{
        {PatchItem{"mdl.drivetrain.ratio[2]", 81.25, 0, {}}},
        "布局黄金",
    };
    std::vector<std::uint8_t> expected;
    const char magic[8] = {'I', 'R', 'D', 'O', 'P', 'T', 'P', '1'};
    expected.insert(expected.end(), std::begin(magic), std::end(magic));
    expected.push_back(0x01); expected.push_back(0x00);
    expected.push_back(0x00); expected.push_back(0x00);  // codecVersion＝1 LE
    expected.push_back(0x01); expected.push_back(0x00);
    expected.push_back(0x00); expected.push_back(0x00);  // itemCount＝1 LE
    const std::string token = "mdl.drivetrain.ratio[2]";
    expected.push_back(static_cast<std::uint8_t>(token.size() & 0xFF));  // u16 LE 长度
    expected.push_back(0x00);
    expected.insert(expected.end(), token.begin(), token.end());
    expected.push_back(static_cast<std::uint8_t>(PatchValueTag::Scalar));  // 'S'
    double value = 81.25;                                                   // f64 LE
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int i = 0; i < 8; ++i) {
        expected.push_back(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFFu));
    }
    EXPECT_EQ(optimization::canonicalize(patch), expected);
}
