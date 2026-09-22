/**
 * @file   TemplateTest.cpp
 * @brief  模板创建与参数化编辑用例组（MdlTemplate）——契约
 *         tasks/foundation/WP-13-T07.json acceptance 1~5 逐条具名自证：
 *           ACC1 接口落位（§9.4.2 签名 listTemplates/createDraft）＋三模板
 *                清单＋createDraft 产出满足 I-MDL-1~12 的工作集＋不触达
 *                project/不产生修订（纯函数面）＋IllegalName 输入面
 *           ACC2 七轴登记不启用（V-02/O-27/P-03）：enabled=false＋
 *                createDraft→TemplateDisabled＋MDL-TEMPLATE-DISABLED
 *                提示诊断，不静默替换为六轴、无半成品
 *           ACC3 六轴默认参数表 T-MDL-1（D-MDL-7 设计默认值）：逐轴
 *                axis/zeroOffset/bounds/workingRange/maxVel/maxAcc 按卡
 *                §5.1 表值；J6 行程 4π 阈值边界用例（阈值常量归 policy，
 *                本测试只钉"行程恰等于 4π"事实）
 *           ACC4 创建→编辑链路（MDL-01、AT-20/AT-01 建模侧）：字段级/批量
 *                变体（批量产出单条变更摘要）→buildChangeSummary；
 *                4/5 轴与含 prismatic 链创建入口阻止＋提示（§6.4 判定复用）
 *           ACC5 几何生成辅助两条（§5.2 v0.2）：连杆占位圆柱（相邻关节
 *                原点连线、确定性）＋碰撞引用复制（同资源）——产物普通
 *                GeometryRef＋来源 GeometricEstimate（methodTag 区分）
 *
 * 设计依据：units/modeling.md §5.1/§5.2/§6.4/§9.4.2、§4.10（I-MDL 不变量
 * 层）；需求 MDL-01/MDL-04/MDL-22、P-03/O-27。
 *
 * 比对口径：T-MDL-1 期望值在测试内**独立抄写**卡 §5.1 表（实现与测试各
 * 一份表值转写，互为核对——PropertyEstimationTest 同款纪律）；双精度
 * 逐项精确比对（表值为 π 的整倍数字面量，两侧转写位级一致——不用容差）。
 */

#include <sdurws/ird/modeling/DiagCodes.hpp>      // kMdlTemplateDisabled/kMdlImportTemplateRange（码同源对账）
#include <sdurws/ird/modeling/PropertyEstimation.hpp>  // defaultMaterialDensity（材料密度单源对账）
#include <sdurws/ird/modeling/RobotDesign.hpp>    // checkInvariants（I-MDL-1~12 静态断言层）
#include <sdurws/ird/modeling/Template.hpp>       // 被测主面（工厂/编辑流/几何辅助）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using sdurws::ird::core::FieldState;
using sdurws::ird::core::ProvenanceKind;
using sdurws::ird::core::ValueProvenance;
namespace rwmath = rw::math;
using sdurws::ird::modeling::AuthorityMode;
using sdurws::ird::modeling::ChainCapability;
using sdurws::ird::modeling::ChainCapabilityKind;
using sdurws::ird::modeling::GeometryKind;
using sdurws::ird::modeling::GeometryRef;
using sdurws::ird::modeling::GeneratedGeometry;
using sdurws::ird::modeling::JointBatchEditItem;
using sdurws::ird::modeling::JointEditErrorCode;
using sdurws::ird::modeling::JointEditField;
using sdurws::ird::modeling::JointEditValue;
using sdurws::ird::modeling::JointEntry;
using sdurws::ird::modeling::JointLimits;
using sdurws::ird::modeling::JointType;
using sdurws::ird::modeling::ModelingChangeRecord;
using sdurws::ird::modeling::ModelingWorkingSet;
using sdurws::ird::modeling::ModelingErrorCode;
using sdurws::ird::modeling::RobotDesignTemplateFactory;
using sdurws::ird::modeling::SixAxisJointSpec;
using sdurws::ird::modeling::TemplateDescriptor;
using sdurws::ird::modeling::TemplateId;
using sdurws::ird::modeling::TemplateOutcome;
using sdurws::ird::modeling::buildChangeSummary;
using sdurws::ird::modeling::checkInvariants;
using sdurws::ird::modeling::copyVisualToCollision;
using sdurws::ird::modeling::creationEntryGuard;
using sdurws::ird::modeling::defaultMaterialDensity;
using sdurws::ird::modeling::applyJointFieldEdit;
using sdurws::ird::modeling::applyJointFieldEditBatch;
using sdurws::ird::modeling::judgeChainCapability;
using sdurws::ird::modeling::jointEditErrorCodeToken;
using sdurws::ird::modeling::kMdlImportTemplateRange;
using sdurws::ird::modeling::kMdlTemplateDisabled;
using sdurws::ird::modeling::kTemplateIdCustomChain;
using sdurws::ird::modeling::kTemplateIdGeneric6R;
using sdurws::ird::modeling::kTemplateIdGeneric7R;
using sdurws::ird::modeling::makeLinkPlaceholderCylinder;
using sdurws::ird::modeling::sixAxisTemplateDefaults;

namespace {

/// 诊断码存在性检查（ImportTest 同款——DiagCode 与 string_view 可比）。
bool hasDiag(const std::vector<sdurws::ird::core::DiagnosticRecord>& diags,
             std::string_view code)
{
    for (const auto& d : diags) {
        if (d.code == code) { return true; }
    }
    return false;
}

/// 卡 §5.1 表 T-MDL-1 的测试内独立抄写（与实现各自转写、互为核对——
/// 单位 rad／rad·s⁻¹／rad·s⁻²；axis 为连杆系下单位轴）。
struct TableRow {
    JointType type;
    double axis[3];
    double zeroOffset;
    JointLimits bounds;
    double maxVelocity;
    double maxAcceleration;
};

std::vector<TableRow> expectedTable()
{
    // π 常量：与实现同取 double 圆周率（字面量→最近 double，两侧位级一致）。
    const double pi = 3.14159265358979323846;
    const double piHalf = pi / 2.0;
    const double twoPi = 2.0 * pi;
    const double fourPi = 4.0 * pi;
    return {
        {JointType::Revolute, {0, 0, 1}, 0.0, {-pi, pi}, pi, 2 * pi},          // J1
        {JointType::Revolute, {0, 1, 0}, 0.0, {-piHalf, piHalf}, pi, 2 * pi},  // J2
        {JointType::Revolute, {0, 1, 0}, 0.0, {-pi, piHalf}, pi, 2 * pi},      // J3
        {JointType::Revolute, {1, 0, 0}, 0.0, {-pi, pi}, twoPi, fourPi},       // J4
        {JointType::Revolute, {0, 1, 0}, 0.0, {-piHalf, piHalf}, twoPi, fourPi},  // J5
        {JointType::Revolute, {1, 0, 0}, 0.0, {-twoPi, twoPi}, twoPi, fourPi},    // J6
    };
}

/// generic-6r 草稿的便捷创建（成功前置——供编辑链路用例起步）。
ModelingWorkingSet makeSixAxisDraft()
{
    const RobotDesignTemplateFactory factory;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const TemplateOutcome outcome = factory.createDraft(
        TemplateId{kTemplateIdGeneric6R},
        sdurws::ird::runtime::InstallationPresetToken::Ground, "demo", diags);
    return outcome.get();  // 成功前置——失败即测试自身装配错误（logic_error）
}

/// 来源标记断言：UserProvided＋methodTag "template/<id>"（§5.1 来源行）。
void expectTemplateProvenance(
    const ValueProvenance& provenance, const std::string& templateId,
    const std::string& subject)
{
    EXPECT_TRUE(provenance.kind == ProvenanceKind::UserProvided)
        << subject << "：模板提供的值应带 UserProvided 来源（§5.1）";
    ASSERT_TRUE(provenance.methodTag.has_value())
        << subject << "：模板轨迹应以 methodTag 区分";
    EXPECT_EQ(*provenance.methodTag, "template/" + templateId)
        << subject << "：methodTag 应为 template/<templateId>";
}

/// 旋转矩阵与向量乘（逐元素——测试自持，不依赖框架外联符号）。
rwmath::Vector3D<double> applyRotation(const rwmath::Rotation3D<double>& R,
                                       const rwmath::Vector3D<double>& v)
{
    return rwmath::Vector3D<double>(
        R(0, 0) * v[0] + R(0, 1) * v[1] + R(0, 2) * v[2],
        R(1, 0) * v[0] + R(1, 1) * v[1] + R(1, 2) * v[2],
        R(2, 0) * v[0] + R(2, 1) * v[1] + R(2, 2) * v[2]);
}

}  // namespace

// =====================================================================
// ACC1：接口落位（§9.4.2）＋三模板清单＋createDraft 满足 I-MDL-1~12
// =====================================================================

/**
 * 三模板清单（acceptance 1——"模板清单三行"）：行数/行序＝§5.1 表行序；
 * 逐行 id/轴数/权威模式/预设选项/启用状态对齐卡面（七轴 enabled=false
 * ——P-03/O-27 登记面在清单页的呈现）。
 */
TEST(MdlTemplate, ListTemplates_ThreeRowsPerSection51_WP13T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-01"}, std::vector<std::string>{"AT-20"});

    const RobotDesignTemplateFactory factory;
    const std::vector<TemplateDescriptor> catalog = factory.listTemplates();

    ASSERT_EQ(catalog.size(), 3U) << "§5.1 清单恰三行（六轴/七轴/自定义链）";

    // 行 1：generic-6r——6×Revolute、Explicit、ground/inverted/wall、R1 可用。
    EXPECT_EQ(catalog[0].templateId, TemplateId{kTemplateIdGeneric6R});
    EXPECT_EQ(catalog[0].axisCount, std::optional<std::uint32_t>(6));
    EXPECT_EQ(catalog[0].authority, AuthorityMode::Explicit);
    ASSERT_EQ(catalog[0].installationPresets.size(), 3U);
    EXPECT_TRUE(catalog[0].enabled) << "六轴模板 R1 可用";
    EXPECT_FALSE(catalog[0].note.empty());

    // 行 2：generic-7r——7×Revolute、Explicit（MDL-02 七轴锁定显式）、
    // enabled=false（P-03 数值未冻结仅登记不启用——O-27）。
    EXPECT_EQ(catalog[1].templateId, TemplateId{kTemplateIdGeneric7R});
    EXPECT_EQ(catalog[1].axisCount, std::optional<std::uint32_t>(7));
    EXPECT_EQ(catalog[1].authority, AuthorityMode::Explicit);
    EXPECT_FALSE(catalog[1].enabled)
        << "七轴模板 P-03 冻结前仅登记不启用（O-27 处置——V-02）";
    EXPECT_NE(catalog[1].note.find("P-03"), std::string::npos)
        << "冻结状态登记注记可观察（§5.1 表第 5 列）";

    // 行 3：custom-chain——逐轴定义（axisCount 无承诺）、R1 可用。
    EXPECT_EQ(catalog[2].templateId, TemplateId{kTemplateIdCustomChain});
    EXPECT_FALSE(catalog[2].axisCount.has_value())
        << "逐轴定义＝清单面不承诺固定轴数（optional 语义，非 0）";
    EXPECT_TRUE(catalog[2].enabled) << "自定义链 R1 可用";

    // 清单确定性（NFR-COR-02）：同调用同清单（纯函数）。
    EXPECT_EQ(catalog, factory.listTemplates());
}

/**
 * generic-6r 草稿满足 I-MDL-1~12（acceptance 1——"@post 产出满足
 * I-MDL-1~12 的工作集"）：全量不变量核查零违例＋结构（6 关节/7 连杆/
 * 链序）＋BasePlacement 预设与零位地面（MDL-22 V15-04）＋无工具/无引用
 * 表/无资源（模板种子最小面）＋来源标记 Template 轨迹。
 */
TEST(MdlTemplate, CreateDraft_Generic6R_WorkingSetInvariants_WP13T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-01", "MDL-04", "MDL-22"},
                  std::vector<std::string>{"AT-20"});

    const RobotDesignTemplateFactory factory;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const TemplateOutcome outcome = factory.createDraft(
        TemplateId{kTemplateIdGeneric6R},
        sdurws::ird::runtime::InstallationPresetToken::Inverted, "demo", diags);

    ASSERT_TRUE(outcome.ok()) << "六轴模板创建应成功";
    EXPECT_TRUE(diags.empty()) << "成功路径不追加诊断（§9.4.2 @post 无诊断承诺）";

    const ModelingWorkingSet& ws = outcome.get();
    const auto violations = checkInvariants(ws.design);
    EXPECT_TRUE(violations.empty())
        << "模板草稿必须满足 I-MDL-1~12（§9.4.2 @post；违例数="
        << violations.size() << "）";

    // 结构（I-MDL-1 落地面）：6×Revolute 串联＋links=joints+1。
    ASSERT_EQ(ws.design.joints.size(), 6U);
    ASSERT_EQ(ws.design.links.size(), 7U);
    for (const JointEntry& joint : ws.design.joints) {
        EXPECT_EQ(joint.type, JointType::Revolute) << "T-MDL-1 六行全 Revolute";
        EXPECT_EQ(joint.workingRange.state(), FieldState::NotApplicable)
            << "workingRange 该列全行为「—」（仅 Continuous 适用）";
    }
    EXPECT_EQ(ws.design.links[0].localName, "base") << "连杆链以基座开头";

    // 根面：呈现名＝用户基名；Explicit 权威；无工具/场景/位姿/传动引用、
    // 无资源清单（模板不猜测、不预填——I-MDL-9/10 输入质量由编辑流承接）。
    EXPECT_EQ(ws.design.displayName, "demo");
    EXPECT_EQ(ws.design.authority, AuthorityMode::Explicit);
    EXPECT_FALSE(ws.design.defaultTcp.has_value());
    EXPECT_TRUE(ws.design.toolRefs.empty());
    EXPECT_TRUE(ws.design.sceneRefs.empty());
    EXPECT_TRUE(ws.design.resourceManifest.empty());

    // BasePlacement：预设＝调用参数（倒挂）；基座位置＝零位地面默认值
    // （MDL-22 V15-04：默认值在模板层填入并带来源标记）；customEaa 非
    // Custom 不适用（NotProvided）。
    EXPECT_EQ(ws.design.basePlacement.preset,
              sdurws::ird::runtime::InstallationPresetToken::Inverted);
    ASSERT_EQ(ws.design.basePlacement.basePosition.state(), FieldState::Provided);
    EXPECT_EQ(ws.design.basePlacement.basePosition.value()[0], 0.0);
    EXPECT_EQ(ws.design.basePlacement.basePosition.value()[1], 0.0);
    EXPECT_EQ(ws.design.basePlacement.basePosition.value()[2], 0.0);
    expectTemplateProvenance(ws.design.basePlacement.basePosition.provenance(),
                             "generic-6r", "basePlacement.basePosition");

    // 来源标记 Template 轨迹（§5.1"来源=UserProvided/Template"）：轴线为
    // 模板提供权威值的代表样本。
    expectTemplateProvenance(ws.design.joints[0].axis.provenance(),
                             "generic-6r", "joints[0].axis");
}

/**
 * 非法调用面（acceptance 1——§9.4.2 @pre/@错误 行）：清单外 id＝调用方
 * 契约违约 fail-fast；Custom 预设＝模板路径不接纳（customEaa 属创建后
 * 编辑——I-MDL-7 前置）；localName 空串/字符集违约＝IllegalName 值面
 * （不产诊断——§9.5 尚无该错误独立码行，Errors.hpp 阶段纪律）。
 */
TEST(MdlTemplate, CreateDraft_IllegalInputs_WP13T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-01"}, std::vector<std::string>{});

    const RobotDesignTemplateFactory factory;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;

    // 清单外 id——fail-fast（AGENTS 错误语义：调用方契约违约）。
    EXPECT_THROW(factory.createDraft(TemplateId{"no-such-template"},
                                     sdurws::ird::runtime::InstallationPresetToken::Ground,
                                     "demo", diags),
                 std::invalid_argument)
        << "templateId 存在性是 @pre——违约 fail-fast";

    // Custom 预设——fail-fast（模板预设词表 ground/inverted/wall）。
    EXPECT_THROW(factory.createDraft(TemplateId{kTemplateIdGeneric6R},
                                     sdurws::ird::runtime::InstallationPresetToken::Custom,
                                     "demo", diags),
                 std::invalid_argument)
        << "Custom 需用户 customEaa——模板创建路径不接纳（I-MDL-7 前置）";

    // localName 空串——IllegalName 值面（非异常出口；§9.4 前言）。
    const TemplateOutcome emptyName = factory.createDraft(
        TemplateId{kTemplateIdGeneric6R},
        sdurws::ird::runtime::InstallationPresetToken::Ground, "", diags);
    ASSERT_FALSE(emptyName.ok());
    EXPECT_EQ(emptyName.error().code, ModelingErrorCode::IllegalName);
    EXPECT_TRUE(diags.empty()) << "IllegalName 无已登记映射码行——不得产诊断";

    // localName 字符集违约——IllegalName 值面（[A-Za-z0-9_.-] 之外）。
    const TemplateOutcome badName = factory.createDraft(
        TemplateId{kTemplateIdGeneric6R},
        sdurws::ird::runtime::InstallationPresetToken::Ground, "机臂 A", diags);
    ASSERT_FALSE(badName.ok());
    EXPECT_EQ(badName.error().code, ModelingErrorCode::IllegalName);

    // 拒绝路径无半成品：ok 侧不可得（两态恰持一——runtime::Expected 契约）。
    EXPECT_THROW(badName.get(), std::logic_error) << "错误态无草稿可取";
}

/**
 * 创建确定性（acceptance 1"不触达 project、不产生修订"的纯函数面证据＋
 * NFR-COR-02）：同输入两次创建→工作集逐字段相等（含确定性派生的临时
 * ObjectId——同键同句柄、不经随机源；§5.2 临时句柄纪律）。纯函数无外部
 * 可观察副作用——"不产生修订"由纯函数性保证（修订唯一归 project，PA-1）。
 */
TEST(MdlTemplate, CreateDraft_DeterministicTempHandles_WP13T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-01", "ARC-04"},
                  std::vector<std::string>{});

    const RobotDesignTemplateFactory factory;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const TemplateOutcome first = factory.createDraft(
        TemplateId{kTemplateIdGeneric6R},
        sdurws::ird::runtime::InstallationPresetToken::Ground, "demo", diags);
    const TemplateOutcome second = factory.createDraft(
        TemplateId{kTemplateIdGeneric6R},
        sdurws::ird::runtime::InstallationPresetToken::Ground, "demo", diags);
    ASSERT_TRUE(first.ok());
    ASSERT_TRUE(second.ok());
    EXPECT_EQ(first.get(), second.get())
        << "同输入→同工作集字节（确定性临时句柄——NFR-COR-02；§5.2）";
    // 关节身份非全零（保留值纪律——派生句柄仍是合法形态 ObjectId）。
    EXPECT_TRUE(first.get().design.joints[0].objectId.isValid());
}

// =====================================================================
// ACC2：七轴登记不启用（V-02，O-27/P-03）
// =====================================================================

/**
 * 七轴模板创建入口阻止（acceptance 2）：createDraft(generic-7r)→
 * TemplateDisabled 错误码＋MDL-TEMPLATE-DISABLED 提示诊断（参数携带
 * template-id/freeze-gate——T07 行 paramSchema）；**不静默替换为六轴**
 * （错误态无草稿——AT-20 向导语义建模侧）；P-03 数值未冻结——无任何
 * 七轴建链动作。
 */
TEST(MdlTemplate, CreateDraft_Generic7R_DisabledBlocked_WP13T07_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-01"},
                  std::vector<std::string>{"AT-20"});

    const RobotDesignTemplateFactory factory;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const TemplateOutcome outcome = factory.createDraft(
        TemplateId{kTemplateIdGeneric7R},
        sdurws::ird::runtime::InstallationPresetToken::Ground, "demo7", diags);

    ASSERT_FALSE(outcome.ok()) << "P-03 冻结前七轴模板不启用";
    EXPECT_EQ(outcome.error().code, ModelingErrorCode::TemplateDisabled)
        << "V-02：TemplateDisabled 错误码";

    // 错误参数面：模板 id＋冻结门可定位（启用条件指向 DTB §4.3 O-27 行）。
    bool hasTemplateId = false;
    bool hasFreezeGate = false;
    for (const auto& param : outcome.error().params) {
        if (param.first == "template-id" && param.second == "generic-7r") {
            hasTemplateId = true;
        }
        if (param.first == "freeze-gate" && param.second == "P-03") {
            hasFreezeGate = true;
        }
    }
    EXPECT_TRUE(hasTemplateId) << "定位参数 template-id";
    EXPECT_TRUE(hasFreezeGate) << "定位参数 freeze-gate（P-03）";

    // 提示诊断：恰一条 MDL-TEMPLATE-DISABLED（§9.4.2"附定位诊断"——码值
    // 与 DiagCodes.hpp 常量同源对账，禁字符串拼码）。
    ASSERT_EQ(diags.size(), 1U) << "拒绝路径恰产一条提示诊断";
    EXPECT_TRUE(hasDiag(diags, kMdlTemplateDisabled));
    EXPECT_EQ(diags[0].code, std::string(kMdlTemplateDisabled));
    EXPECT_FALSE(hasDiag(diags, kMdlImportTemplateRange))
        << "七轴拒绝是冻结门（P-03）而非链型范围（§6.4）——两语义不混用";

    // 无半成品：错误态不可取草稿（不得静默替换为六轴——返回侧证据）。
    EXPECT_THROW(outcome.get(), std::logic_error);
}

// =====================================================================
// ACC3：六轴默认参数表 T-MDL-1（D-MDL-7 设计默认值）
// =====================================================================

/**
 * 表值逐格核对（acceptance 3）：sixAxisTemplateDefaults() 与测试内独立
 * 抄写的卡 §5.1 表逐行逐列相等（type/axis/zeroOffset/bounds/maxVel/
 * maxAcc——双精度位级一致，不用容差：两侧同为 π 整倍数字面量转写）。
 */
TEST(MdlTemplate, SixAxisDefaults_TableValues_WP13T07_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-01"}, std::vector<std::string>{});

    const auto actual = sixAxisTemplateDefaults();
    const auto expected = expectedTable();
    ASSERT_EQ(actual.size(), expected.size());

    for (std::size_t i = 0; i < expected.size(); ++i) {
        const SixAxisJointSpec& row = actual[i];
        const TableRow& want = expected[i];
        EXPECT_EQ(row.type, want.type) << "J" << (i + 1) << " type";
        EXPECT_EQ(row.axis[0], want.axis[0]) << "J" << (i + 1) << " axis.x";
        EXPECT_EQ(row.axis[1], want.axis[1]) << "J" << (i + 1) << " axis.y";
        EXPECT_EQ(row.axis[2], want.axis[2]) << "J" << (i + 1) << " axis.z";
        EXPECT_EQ(row.zeroOffset, want.zeroOffset) << "J" << (i + 1) << " zeroOffset";
        EXPECT_EQ(row.bounds.first, want.bounds.first) << "J" << (i + 1) << " qmin";
        EXPECT_EQ(row.bounds.second, want.bounds.second) << "J" << (i + 1) << " qmax";
        EXPECT_EQ(row.maxVelocity, want.maxVelocity) << "J" << (i + 1) << " maxVel";
        EXPECT_EQ(row.maxAcceleration, want.maxAcceleration) << "J" << (i + 1)
                                                             << " maxAcc";
        // 表面自洽（I-MDL-4 前半＋I-MDL-3）：qmin<qmax 且全有限。
        EXPECT_LT(row.bounds.first, row.bounds.second)
            << "J" << (i + 1) << " 限位有序";
        EXPECT_TRUE(std::isfinite(row.maxVelocity) && std::isfinite(row.maxAcceleration))
            << "J" << (i + 1) << " 速度/加速度有限";
    }
}

/**
 * J6 行程边界用例（acceptance 3——"J6 行程 [−2π,+2π] 恰在 4π 阈值边界
 * （附录 D 第 11 项）默认通过行程校验兼作边界用例"）：行程（qmax−qmin）
 * 位级恰等于 4π。★ 阈值常量本身归 policy（ARC-05/NFR-MNT-07：本地不设
 * 第二 4π 常量）——合规判定归就绪校验（T08，阈值经④端口只读），本用例
 * 钉住"表值行程恰在阈值边界"这一事实。
 */
TEST(MdlTemplate, SixAxisDefaults_J6TravelAtThresholdBoundary_WP13T07_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-01"}, std::vector<std::string>{});

    const auto defaults = sixAxisTemplateDefaults();
    const double pi = 3.14159265358979323846;
    const double fourPi = 4.0 * pi;

    const JointLimits& j6 = defaults[5].bounds;
    EXPECT_EQ(j6.first, -2.0 * pi) << "J6 qmin＝−2π（位级）";
    EXPECT_EQ(j6.second, 2.0 * pi) << "J6 qmax＝+2π（位级）";
    // 减法精确性：qmax−qmin＝a−(−a)＝2a（×2 为精确缩放）——位级等于 4π。
    EXPECT_EQ(j6.second - j6.first, fourPi)
        << "J6 行程恰为 4π＝阈值边界（附录 D 第 11 项：行程≤阈值含于合规侧"
           "——默认通过，兼作边界用例）";
}

/**
 * 草稿关节字段来自表值（acceptance 3——建链落地面）：createDraft 产出的
 * 逐关节 axis/zeroOffset/bounds 与 T-MDL-1 一致；workingRange＝
 * NotApplicable；连杆材料种子＝钢（密度取 §5.3 默认表单源对账——实现
 * 不写第二处 7850 字面量）；物性数值 NotProvided（模板不猜测物性）。
 */
TEST(MdlTemplate, CreateDraft_JointFieldsFromTable_WP13T07_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-01", "MDL-04"},
                  std::vector<std::string>{});

    const ModelingWorkingSet ws = makeSixAxisDraft();
    const auto table = expectedTable();

    for (std::size_t i = 0; i < table.size(); ++i) {
        const JointEntry& joint = ws.design.joints[i];
        const TableRow& want = table[i];
        EXPECT_EQ(joint.type, want.type) << "J" << (i + 1);
        ASSERT_EQ(joint.axis.state(), FieldState::Provided) << "J" << (i + 1);
        EXPECT_EQ(joint.axis.value()[0], want.axis[0]) << "J" << (i + 1) << " axis.x";
        EXPECT_EQ(joint.axis.value()[1], want.axis[1]) << "J" << (i + 1) << " axis.y";
        EXPECT_EQ(joint.axis.value()[2], want.axis[2]) << "J" << (i + 1) << " axis.z";
        EXPECT_EQ(joint.zeroOffset, want.zeroOffset) << "J" << (i + 1);
        ASSERT_EQ(joint.bounds.state(), FieldState::Provided) << "J" << (i + 1);
        EXPECT_EQ(joint.bounds.value().first, want.bounds.first) << "J" << (i + 1);
        EXPECT_EQ(joint.bounds.value().second, want.bounds.second) << "J" << (i + 1);
    }

    // 连杆材料种子（§5.1"连杆几何默认"句材料半句）：钢＋默认表密度
    // （单源对账——默认表查询值即密度事实）。
    const auto steelDensity = defaultMaterialDensity("steel");
    ASSERT_TRUE(steelDensity.has_value()) << "默认表应有登记键 steel";
    for (const auto& link : ws.design.links) {
        ASSERT_TRUE(link.body.material.has_value()) << "连杆材料种子（" << link.localName << "）";
        EXPECT_EQ(link.body.material->materialId, "steel");
        ASSERT_EQ(link.body.material->density.state(), FieldState::Provided);
        EXPECT_EQ(link.body.material->density.value(), *steelDensity)
            << "密度与 §5.3 默认表同源（实现不私写第二处字面量）";
        // 物性数值缺失＝NotProvided（不触发断言——MDL-06/V15-01 降级语义；
        // 模板不猜测质量/质心/惯量——NFR-COR-03）。
        EXPECT_EQ(link.body.mass.state(), FieldState::NotProvided);
        EXPECT_EQ(link.body.centerOfMass.state(), FieldState::NotProvided);
        EXPECT_EQ(link.body.inertia.state(), FieldState::NotProvided);
    }
}

// =====================================================================
// ACC4：创建→编辑链路（MDL-01、AT-20/AT-01 建模侧）＋创建入口阻止
// =====================================================================

/**
 * 字段级逐轴编辑（acceptance 4——类型/轴线/零位/限位编辑）：每次接受＝
 * 设计字段更新＋恰一条变更记录；拒绝（非有限/零轴/坏区间/权威锁）＝
 * 工作集字节不变（§9.4.1 拒绝语义的域内核）。速度/加速度的编辑面无
 * schema 落点（§15 v0.4 ③c——不发明字段），数值面由 SixAxisJointSpec
 * 承载（见其类型注；ACC3 已核对表值）。
 */
TEST(MdlTemplate, CreateEditChain_FieldLevelEdits_WP13T07_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-01", "MDL-09"},
                  std::vector<std::string>{"AT-20", "AT-01"});

    ModelingWorkingSet ws = makeSixAxisDraft();
    ASSERT_TRUE(ws.changes.empty()) << "创建本身不是编辑——摘要自编辑起算";

    // 轴线编辑（Explicit 权威一等字段——MDL-09）：接受。
    auto rejection = applyJointFieldEdit(
        ws, 1, JointEditField::Axis,
        JointEditValue{rwmath::Vector3D<double>(0.0, 0.0, 1.0)});
    EXPECT_FALSE(rejection.has_value()) << "合法轴线编辑应接受";
    ASSERT_EQ(ws.design.joints[1].axis.state(), FieldState::Provided);
    EXPECT_EQ(ws.design.joints[1].axis.value()[2], 1.0);
    // 用户输入覆盖：来源＝UserProvided（§5.3 规则 1 同款语义——编辑值由
    // 用户直接输入，非模板轨迹；与模板种子的 methodTag 标记相区分）。
    EXPECT_EQ(ws.design.joints[1].axis.provenance().kind,
              ProvenanceKind::UserProvided);
    ASSERT_EQ(ws.changes.size(), 1U) << "一次字段级编辑＝一条变更记录";

    // 零位偏置编辑：接受（rad——转动关节）。
    rejection = applyJointFieldEdit(ws, 1, JointEditField::ZeroOffset,
                                    JointEditValue{0.25});
    EXPECT_FALSE(rejection.has_value());
    EXPECT_EQ(ws.design.joints[1].zeroOffset, 0.25);
    ASSERT_EQ(ws.changes.size(), 2U);

    // 限位编辑：接受（qmin<qmax）。
    rejection = applyJointFieldEdit(ws, 1, JointEditField::Bounds,
                                    JointEditValue{JointLimits{-1.0, 1.5}});
    EXPECT_FALSE(rejection.has_value());
    ASSERT_EQ(ws.design.joints[1].bounds.value().second, 1.5);
    ASSERT_EQ(ws.changes.size(), 3U);

    // 类型编辑：Revolute→Prismatic（限位已提供——合法组合）。
    rejection = applyJointFieldEdit(ws, 2, JointEditField::Type,
                                    JointEditValue{JointType::Prismatic});
    EXPECT_FALSE(rejection.has_value());
    EXPECT_EQ(ws.design.joints[2].type, JointType::Prismatic);
    ASSERT_EQ(ws.changes.size(), 4U);

    // ---- 边界/拒绝面（工作集字节不变）----
    // 拒绝不变性比对基线（含此前 4 次接受编辑的结果——拒绝只允许零变化）。
    const ModelingWorkingSet pristine = ws;
    // 非有限轴线分量（I-MDL-3）。
    rejection = applyJointFieldEdit(
        ws, 0, JointEditField::Axis,
        JointEditValue{rwmath::Vector3D<double>(
            std::nan(""), 0.0, 1.0)});
    ASSERT_TRUE(rejection.has_value());
    EXPECT_EQ(rejection->code, JointEditErrorCode::ValueNotFinite);
    // 零轴（I-MDL-6）。
    rejection = applyJointFieldEdit(
        ws, 0, JointEditField::Axis,
        JointEditValue{rwmath::Vector3D<double>(0.0, 0.0, 0.0)});
    ASSERT_TRUE(rejection.has_value());
    EXPECT_EQ(rejection->code, JointEditErrorCode::AxisNotNormalizable);
    // 限位无序 qmin≥qmax（I-MDL-4 前半）。
    rejection = applyJointFieldEdit(ws, 0, JointEditField::Bounds,
                                    JointEditValue{JointLimits{1.0, -1.0}});
    ASSERT_TRUE(rejection.has_value());
    EXPECT_EQ(rejection->code, JointEditErrorCode::LimitIntervalInvalid);
    // Continuous 与已提供限位冲突（I-MDL-4 后半——不静默清除）。
    rejection = applyJointFieldEdit(ws, 0, JointEditField::Type,
                                    JointEditValue{JointType::Continuous});
    ASSERT_TRUE(rejection.has_value());
    EXPECT_EQ(rejection->code, JointEditErrorCode::TypeBoundsConflict);
    // 全部拒绝后：记录数不变＋工作集与基线相等（字节不变）。
    ASSERT_EQ(ws.changes.size(), 4U) << "拒绝不追加变更记录";
    EXPECT_EQ(ws, pristine)
        << "拒绝路径工作集字节不变（§9.4.1 拒绝语义——基线含此前 4 次"
           "接受编辑，拒绝面零变化）";
}

/**
 * 权威互斥下的轴线编辑拒绝（acceptance 4 边界——C-1 复用 §7.3 判定）：
 * StandardDH 权威态 axis 为派生只读——AuthorityLocked 拒绝且工作集不变
 * （判定复用 RobotDesign.hpp authorityEditGuard——单一实现）。
 */
TEST(MdlTemplate, CreateEditChain_AuthorityLockedAxis_WP13T07_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-01", "MDL-09"},
                  std::vector<std::string>{"AT-01"});

    ModelingWorkingSet ws = makeSixAxisDraft();
    // 场景装配：模板草稿为 Explicit——置 StandardDH 构造 C-1 场景（值面
    // 演算，不经 T09 转换——转换判定归 T09，本用例只钉编辑守卫语义）。
    ws.design.authority = AuthorityMode::StandardDH;
    const ModelingWorkingSet before = ws;

    const auto rejection = applyJointFieldEdit(
        ws, 0, JointEditField::Axis,
        JointEditValue{rwmath::Vector3D<double>(0.0, 1.0, 0.0)});
    ASSERT_TRUE(rejection.has_value()) << "DH 权威态编辑 axis＝C-1 拒绝";
    EXPECT_EQ(rejection->code, JointEditErrorCode::AuthorityLocked);
    EXPECT_EQ(ws, before) << "拒绝：工作集字节不变（V-14 同口径）";
    EXPECT_TRUE(ws.changes.empty()) << "拒绝不追加变更记录";
}

/**
 * 批量变体＋变更摘要（acceptance 4——"§5.2 字段级/批量变体、批量产出单条
 * 变更摘要→buildChangeSummary"）：批量＝同一字段多行（UX-05）；应用行与
 * 拒绝行互不连带（BatchPartial 语义）；恰一条批量变更记录；buildChangeSummary
 * 确定性人读中文（行数＝记录数）。
 */
TEST(MdlTemplate, CreateEditChain_BatchAndChangeSummary_WP13T07_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-01"},
                  std::vector<std::string>{"AT-20", "AT-01"});

    ModelingWorkingSet ws = makeSixAxisDraft();

    // 批量零位偏置（rad——转动关节）：4 行合法＋1 行非有限（拒绝）。
    std::vector<JointBatchEditItem> items;
    items.push_back(JointBatchEditItem{0, JointEditValue{0.1}});
    items.push_back(JointBatchEditItem{1, JointEditValue{0.2}});
    items.push_back(JointBatchEditItem{2, JointEditValue{0.3}});
    items.push_back(JointBatchEditItem{3, JointEditValue{0.4}});
    items.push_back(JointBatchEditItem{4, JointEditValue{std::nan("")}});

    const auto outcome = applyJointFieldEditBatch(ws, JointEditField::ZeroOffset,
                                                  items);
    EXPECT_EQ(outcome.appliedCount, 4U) << "合法行应用";
    ASSERT_EQ(outcome.rejectedRows.size(), 1U);
    EXPECT_EQ(outcome.rejectedRows[0], 4U) << "拒绝行下标可观察";
    ASSERT_TRUE(outcome.lastError.has_value());
    EXPECT_EQ(outcome.lastError->code, JointEditErrorCode::ValueNotFinite);

    // 行面结果：应用行已更新、拒绝行保留原值（不连带回滚）。
    EXPECT_EQ(ws.design.joints[0].zeroOffset, 0.1);
    EXPECT_EQ(ws.design.joints[1].zeroOffset, 0.2);
    EXPECT_EQ(ws.design.joints[3].zeroOffset, 0.4);
    EXPECT_EQ(ws.design.joints[4].zeroOffset, 0.0) << "拒绝行保留原值";

    // 批量产出单条变更摘要（UX-05 原文）。
    ASSERT_EQ(ws.changes.size(), 1U) << "5 行批量＝恰一条记录";
    EXPECT_EQ(ws.changes[0].subject, "joints[*].zeroOffset");
    EXPECT_NE(ws.changes[0].summary.find("4"), std::string::npos);
    EXPECT_NE(ws.changes[0].summary.find("1"), std::string::npos);

    // buildChangeSummary（§9.4.1）：人读中文＋确定性格式（[序号] subject：summary）。
    const std::string summary = buildChangeSummary(ws);
    EXPECT_EQ(summary, "[1] joints[*].zeroOffset：" + ws.changes[0].summary);
    // 再次调用同输出（NFR-COR-02）。
    EXPECT_EQ(summary, buildChangeSummary(ws));

    // 空工作集摘要＝空串（无变更不伪造文本）。
    ModelingWorkingSet fresh = makeSixAxisDraft();
    EXPECT_TRUE(buildChangeSummary(fresh).empty());
}

/**
 * 创建入口链型守卫（acceptance 4——"4/5 轴与含 prismatic 链创建入口阻止＋
 * 提示（§6.4 维度二判定复用、§2.1 创建列 ❌）"）：5 轴链/含 prismatic 链
 * →阻止（BeyondTemplateRange）＋TEMPLATE-RANGE 提示诊断（T05 行码复用
 * ——同码同语义）；六/七轴全旋转→放行无诊断。custom-chain 种子（1 轴）
 * 不受守卫约束——守卫只作用于创建确认时的完整声明链。
 */
TEST(MdlTemplate, CreationEntryGuard_BeyondRangeBlocked_WP13T07_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-01", "MDL-12"},
                  std::vector<std::string>{"AT-20", "AT-17"});

    // 5×Revolute——4/5 轴：阻止＋提示（reason 携带"4/5"语义）。
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const std::vector<JointType> fiveAxis(5, JointType::Revolute);
    const auto blocked5 = creationEntryGuard(fiveAxis, diags);
    ASSERT_TRUE(blocked5.has_value()) << "4/5 轴创建入口阻止（§2.1 创建列 ❌）";
    EXPECT_EQ(blocked5->kind, ChainCapabilityKind::BeyondTemplateRange);
    EXPECT_EQ(blocked5->movableAxes, 5U);
    EXPECT_NE(blocked5->reason.find("4/5"), std::string::npos);
    ASSERT_EQ(diags.size(), 1U);
    EXPECT_EQ(diags[0].code, std::string(kMdlImportTemplateRange))
        << "提示诊断复用 §9.5 T05 行码（同码同语义——不私定第二码）";

    // 含 prismatic 链（6×Revolute＋1×Prismatic）：阻止（prismatic 维度）。
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsPrismatic;
    std::vector<JointType> withPrismatic(6, JointType::Revolute);
    withPrismatic.push_back(JointType::Prismatic);
    const auto blockedP = creationEntryGuard(withPrismatic, diagsPrismatic);
    ASSERT_TRUE(blockedP.has_value());
    EXPECT_TRUE(blockedP->containsPrismatic);
    EXPECT_EQ(blockedP->movableAxes, 7U);
    EXPECT_NE(blockedP->reason.find("prismatic"), std::string::npos);
    EXPECT_TRUE(hasDiag(diagsPrismatic, kMdlImportTemplateRange));

    // 6×Revolute——放行（nullopt、无诊断）。
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsOk;
    const std::vector<JointType> sixAxis(6, JointType::Revolute);
    const auto allowed = creationEntryGuard(sixAxis, diagsOk);
    EXPECT_FALSE(allowed.has_value()) << "六轴全旋转放行";
    EXPECT_TRUE(diagsOk.empty()) << "放行路径无阻断诊断";

    // 6×Revolute＋1×Continuous（类型保留计可动轴）——七轴全旋转放行。
    std::vector<JointType> sevenAxis(6, JointType::Revolute);
    sevenAxis.push_back(JointType::Continuous);
    const auto allowed7 = creationEntryGuard(sevenAxis, diagsOk);
    EXPECT_FALSE(allowed7.has_value())
        << "continuous 类型保留（V12-01）计入可动轴——七轴全旋转放行";
    EXPECT_TRUE(diagsOk.empty());

    // 判定复用证据：与 Import 报告同源——judgeChainCapability（§6.4 单一
    // 实现）对同序列给出同结论（5 轴链逐字段相等）。
    const ChainCapability viaJudge =
        sdurws::ird::modeling::judgeChainCapability(fiveAxis);
    EXPECT_EQ(viaJudge, *blocked5) << "守卫结论＝导入判定（§6.4 尾段单一实现）";
}

// =====================================================================
// ACC5：几何生成辅助两条（§5.2 v0.2）
// =====================================================================

/**
 * 连杆占位圆柱（acceptance 5——"相邻关节原点连线、确定性纯函数"）：
 * 轴向/长度/中心按连线；轴线恰为 ±z 的确定性特例；一般方向下 R·z0＝
 * 连线单位方向且正交（1×10⁻¹²——附录 D C7 同量级核对）；同输入两次
 * 调用产物逐字段相等（确定性）；产物＝普通 GeometryRef（Primitive）＋
 * 来源 GeometricEstimate（methodTag 区分辅助类型）。
 */
TEST(MdlTemplate, PlaceholderCylinder_SegmentGeometry_WP13T07_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-01", "MDL-04"},
                  std::vector<std::string>{"AT-01"});

    // 情形 1：连线沿 +z（单位长）——恒等旋转、中心为中点。
    const GeneratedGeometry alongZ = makeLinkPlaceholderCylinder(
        rwmath::Vector3D<double>(0, 0, 0), rwmath::Vector3D<double>(0, 0, 1),
        "ph-1");
    EXPECT_EQ(alongZ.geometry.resourceRefId, "ph-1");
    EXPECT_EQ(alongZ.geometry.kind, GeometryKind::Primitive)
        << "占位原语（视觉用——§5.2）";
    EXPECT_EQ(alongZ.geometry.localTransform.P()[0], 0.0);
    EXPECT_EQ(alongZ.geometry.localTransform.P()[1], 0.0);
    EXPECT_EQ(alongZ.geometry.localTransform.P()[2], 0.5) << "中心＝连线中点";
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            EXPECT_EQ(alongZ.geometry.localTransform.R()(r, c),
                      (r == c) ? 1.0 : 0.0)
                << "沿 +z 连线＝恒等旋转（确定性特例）";
        }
    }

    // 情形 2：连线沿 −z——绕 x 轴 π 的确定性特例（diag(1,−1,−1)）。
    const GeneratedGeometry alongNegZ = makeLinkPlaceholderCylinder(
        rwmath::Vector3D<double>(0, 0, 0), rwmath::Vector3D<double>(0, 0, -2),
        "ph-2");
    EXPECT_EQ(alongNegZ.geometry.localTransform.P()[2], -1.0);
    EXPECT_EQ(alongNegZ.geometry.localTransform.R()(0, 0), 1.0);
    EXPECT_EQ(alongNegZ.geometry.localTransform.R()(1, 1), -1.0);
    EXPECT_EQ(alongNegZ.geometry.localTransform.R()(2, 2), -1.0);

    // 情形 3：一般方向 (3,−4,0)——R·z0＝单位方向、R 正交（1×10⁻¹²）。
    const GeneratedGeometry generic = makeLinkPlaceholderCylinder(
        rwmath::Vector3D<double>(1, 2, 3), rwmath::Vector3D<double>(4, -2, 3),
        "ph-3");
    const rwmath::Vector3D<double> segment(3, -4, 0);
    const double len = std::sqrt(3.0 * 3.0 + 4.0 * 4.0);
    const rwmath::Vector3D<double> unitDir(segment[0] / len, segment[1] / len,
                                           segment[2] / len);
    const rwmath::Vector3D<double> mappedZ =
        applyRotation(generic.geometry.localTransform.R(),
                      rwmath::Vector3D<double>(0, 0, 1));
    EXPECT_NEAR(mappedZ[0], unitDir[0], 1e-12) << "R·z0＝连线单位方向";
    EXPECT_NEAR(mappedZ[1], unitDir[1], 1e-12);
    EXPECT_NEAR(mappedZ[2], unitDir[2], 1e-12);
    // 正交性：R·Rᵀ＝I（占位几何的位姿仍是合法旋转——同权同校验面）。
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            double dot = 0.0;
            for (int k = 0; k < 3; ++k) {
                dot += generic.geometry.localTransform.R()(r, k)
                       * generic.geometry.localTransform.R()(c, k);
            }
            EXPECT_NEAR(dot, (r == c) ? 1.0 : 0.0, 1e-12) << "R 正交（" << r
                                                          << "," << c << "）";
        }
    }

    // 来源标记：GeometricEstimate＋methodTag（§5.2"来源 GeometricEstimate
    // （methodTag 区分辅助类型）"）。
    EXPECT_EQ(alongZ.provenance.kind, ProvenanceKind::GeometricEstimate);
    ASSERT_TRUE(alongZ.provenance.methodTag.has_value());
    EXPECT_EQ(*alongZ.provenance.methodTag, "link-placeholder-cylinder");

    // 确定性：同输入两次调用→产物相等（NFR-COR-02）。
    const GeneratedGeometry again = makeLinkPlaceholderCylinder(
        rwmath::Vector3D<double>(1, 2, 3), rwmath::Vector3D<double>(4, -2, 3),
        "ph-3");
    EXPECT_EQ(generic, again);
}

/**
 * 占位圆柱输入面（acceptance 5 边界）：半径非正/非有限、起终点重合、
 * 空资源键＝调用方契约违约 fail-fast（std::invalid_argument）。
 */
TEST(MdlTemplate, PlaceholderCylinder_RejectsBadInput_WP13T07_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-04"}, std::vector<std::string>{});

    const rwmath::Vector3D<double> a(0, 0, 0);
    const rwmath::Vector3D<double> b(1, 0, 0);
    // 半径非正。
    EXPECT_THROW((makeLinkPlaceholderCylinder(a, b, "ph", 0.0)),
                 std::invalid_argument);
    EXPECT_THROW((makeLinkPlaceholderCylinder(a, b, "ph", -0.1)),
                 std::invalid_argument);
    // 半径非有限。
    EXPECT_THROW((makeLinkPlaceholderCylinder(a, b, "ph", std::nan(""))),
                 std::invalid_argument);
    // 起终点重合（零长度圆柱无几何意义）。
    EXPECT_THROW((makeLinkPlaceholderCylinder(a, a, "ph", 0.05)),
                 std::invalid_argument);
    // 空资源键。
    EXPECT_THROW((makeLinkPlaceholderCylinder(a, b, "", 0.05)),
                 std::invalid_argument);
}

/**
 * 碰撞引用复制辅助（acceptance 5——"视觉引用→同资源碰撞引用"；不引入
 * 网格重画/凸包简化——上游未授权）：产物与视觉引用同资源/同位姿/同类别
 * （普通 GeometryRef 同权同校验）；来源 GeometricEstimate＋methodTag
 * "collision-copy"（与占位圆柱辅助的 methodTag 区分）。
 */
TEST(MdlTemplate, CopyVisualToCollision_SameResourceRef_WP13T07_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-01", "MDL-04"},
                  std::vector<std::string>{"AT-19"});

    // 视觉引用样本（Mesh＋非恒位姿——逐字段复制语义核对）。
    GeometryRef visual;
    visual.resourceRefId = "mesh-link1";
    visual.localTransform = rwmath::Transform3D<double>(
        rwmath::Vector3D<double>(0.1, 0.2, 0.3),
        rwmath::Rotation3D<double>(0.0, -1.0, 0.0,
                                   1.0, 0.0, 0.0,
                                   0.0, 0.0, 1.0));
    visual.kind = GeometryKind::Mesh;

    const GeneratedGeometry collision = copyVisualToCollision(visual);
    EXPECT_EQ(collision.geometry.resourceRefId, "mesh-link1")
        << "同资源碰撞引用（不复制几何本体——引用语义）";
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            EXPECT_EQ(collision.geometry.localTransform.R()(r, c),
                      visual.localTransform.R()(r, c))
                << "位姿逐元素同原引用";
        }
    }
    EXPECT_EQ(collision.geometry.localTransform.P()[0], 0.1);
    EXPECT_EQ(collision.geometry.localTransform.P()[1], 0.2);
    EXPECT_EQ(collision.geometry.localTransform.P()[2], 0.3);
    EXPECT_EQ(collision.geometry.kind, GeometryKind::Mesh);

    // 来源标记：GeometricEstimate＋collision-copy（与占位圆柱区分）。
    EXPECT_EQ(collision.provenance.kind, ProvenanceKind::GeometricEstimate);
    ASSERT_TRUE(collision.provenance.methodTag.has_value());
    EXPECT_EQ(*collision.provenance.methodTag, "collision-copy");
}
