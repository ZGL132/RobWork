/**
 * @file   BasePlacementEditTest.cpp
 * @brief  基座安装姿态编辑用例组（MdlBasePlacementEdit）——契约
 *         tasks/foundation/WP-13-T11.json acceptance 逐条具名自证：
 *
 *   ACC1 BasePlacement 编辑（V-13，MDL-22、AT-37 建模侧）：ground/inverted/
 *        wall 三预设＋custom（customEaa 必填，单位 rad）＋basePosition（m）
 *        编辑面；预设 token InstallationPresetToken 词表与 runtime 一致
 *        （直接复用 runtime 枚举——类型级钉住）；编辑经 apply-robot-design
 *        原子持久化——单命令单修订（单命令恰一根对象写入＋双编译声明＋
 *        inverse 快照逆载荷，PA-2 历史不改写）
 *   ACC2 未配置默认地面（V15-04）：模板路径（来源 UserProvided/Template）
 *        与导入路径（来源 ImportMapped）均在映射层填入 ground 并入默认
 *        补全清单——零值语义显式带来源、不静默；I-MDL-7 非法组合拒绝：
 *        custom 缺 customEaa、preset≠ground 而 R=I、正交容差 1×10⁻¹²
 *        （runtime InputInvalid 同口径——编辑边界就地拒绝）
 *   ACC3 P-RT-4 交叉核对（O-16 已裁决 2026-09-22）：预设轴向唯一权威产出
 *        点＝runtime BaseWorldTransform.hpp::installationPresetRotation()
 *        （倒挂=R_x(π)、壁装=R_y(π/2)——runtime.md §6.2 冻结值，测试内
 *        独立抄写）——modeling 只存参数不存矩阵（V-12 建模侧输入面断言：
 *        编辑产物经 Description.base 同款字段映射交 runtime 编译，矩阵仅
 *        在 runtime 侧物质化）；交叉核对结论留痕
 *        traceability/wp13-t11-prt4-crosscheck.md
 *   ACC4 编译单一字段（MDL-22/M-11）：编辑值经 Description.base 由 runtime
 *        唯一编译进 R_world_base（custom EAA→R_z(π/2) 手算对照）；modeling
 *        不计算/缓存/二次叠加基座—世界旋转（不在基座系参数化 g——DTB
 *        禁止项）；仅改 basePlacement 的字节面单字段证据（Codec 往返）
 *
 * 设计依据：units/modeling.md §4.3（basePlacement 行）、§4.10（I-MDL-7）、
 * §5.1/§5.2（模板/编辑流）、§7.6 末段（基座—世界变换隔离）、§9.1（字段
 * 映射表 base 行——不在此计算 R_world_base）、§9.3（apply-robot-design）；
 * runtime.md §6.2（安装预设精确定义）；shared 夹具＝test/CommandFixtures.hpp
 * （T08 同款——测试域替身不入公共面；命令路径用例选移动关节设计，行程
 * 校验不触发④端口——冒烟模式同样可编译执行）。
 *
 * 线程安全：全部用例单线程（编辑态/命令槽语义）。
 */

#include "CommandFixtures.hpp"

#include <sdurws/ird/modeling/Codec.hpp>        // RobotDesignCodec（写入字节/往返解码）
#include <sdurws/ird/modeling/Import.hpp>       // ModelImportMapper（导入路径默认地面）
#include <sdurws/ird/modeling/Template.hpp>     // 被测面：applyBasePlacementEdit＋工厂
#include <sdurws/ird/runtime/BaseWorldTransform.hpp>  // runtime 权威规则面（resolve/check）
#include <sdurws/ird/runtime/Description.hpp>   // runtime::BasePlacementDescription（T12 reader base 字段映射形态）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace sdurws::ird::modeling::testfixture;
namespace modeling = ::sdurws::ird::modeling;  // 被测类型所在命名空间别名
namespace core = ::sdurws::ird::core;          // core 侧类型（身份/来源/字段态）
namespace runtime = ::sdurws::ird::runtime;    // runtime 侧类型（预设词表/规则面）
namespace project = ::sdurws::ird::project;    // project 侧类型（命令契约）
namespace rwmath = rw::math;
using namespace modeling;  // 被测面（Template/RobotDesign/值模型）

namespace {

/// π/2 字面量（测试内独立抄写——期望值不引实现常量，TemplateTest 同款纪律）。
constexpr double kPiHalf = 1.57079632679489661923;

/// generic-6r 草稿的便捷创建（TemplateTest 同款——成功前置，失败即测试
/// 自身装配错误；地面默认路径，BasePlacement 已带模板来源标记）。
inline ModelingWorkingSet makeSixAxisDraft()
{
    const RobotDesignTemplateFactory factory;
    std::vector<core::DiagnosticRecord> diags;
    const TemplateOutcome outcome = factory.createDraft(
        TemplateId{kTemplateIdGeneric6R},
        runtime::InstallationPresetToken::Ground, "demo", diags);
    return outcome.get();
}

/**
 * @brief 移动关节（Prismatic＋有限限位 m——I-MDL-4 合法实例）。
 *
 * 为什么命令路径用例选移动关节而非旋转关节：hasTravelRelevantJoints 只把
 * "Revolute 且带有限限位"视为行程相关（CommandHandlers.cpp）——移动关节
 * 设计不消费④策略端口，命令路径用例因此不依赖 policy/JointLimits.cpp
 * 的外联符号（该 TU 仅集成模式编译），本文件得以在独立冒烟模式同样编译
 * 执行（两模式留痕）。
 */
inline JointEntry makePrismaticJoint(const core::ObjectId& oid, const std::string& name)
{
    JointEntry joint;
    joint.objectId = oid;
    joint.localName = name;
    joint.type = JointType::Prismatic;
    joint.axis = core::SourcedValue<rwmath::Vector3D<double>>::provided(
        rwmath::Vector3D<double>(0.0, 0.0, 1.0), userProvenance());
    joint.origin =
        core::SourcedValue<JointPose>::provided(JointPose{}, userProvenance());
    joint.bounds = core::SourcedValue<JointLimits>::provided(
        JointLimits{-0.5, 0.5}, userProvenance());  // 单位 m（移动关节限位）
    return joint;
}

/// 最小合法根对象：1 个移动关节＋2 连杆（I-MDL-1 计数关系）。
inline RobotDesign makePrismaticDesign()
{
    RobotDesign design;
    design.joints.push_back(makePrismaticJoint(makeOid(), "J1"));
    design.links.push_back(makeLink(makeOid(), "base"));
    design.links.push_back(makeLink(makeOid(), "link1"));
    return design;
}

/// 既有对象槽（显式基线身份——字节替换；PartObjectCommandTest 同款局部辅助）。
inline PayloadObjectSlot replaceSlot(const core::ObjectId& oid, std::string token,
                                     const ObjectVariant& object)
{
    PayloadObjectSlot slot;
    slot.allocateNew = false;
    slot.objectId = oid;
    slot.objectTypeToken = std::move(token);
    slot.objectBytes = encodeVariant(object);
    return slot;
}

/// 根写入字节解码（解码失败即测试自身装配错误——logic_error）。
inline RobotDesign decodeRootWrite(const project::ObjectWrite& write)
{
    RobotDesignCodec codec;
    auto decoded = codec.decode(write.payloadCanonical, kCurrentFormatVersion);
    if (!decoded.ok()) {
        throw std::logic_error(std::string("test: 根写入字节解码失败: ")
                               + decoded.error().detail);
    }
    return std::get<RobotDesign>(decoded.get());
}

/**
 * @brief modeling BasePlacement → runtime BasePlacementDescription 的字段
 *        映射（§9.1 字段映射表 base 行的建模侧输入面——T12 reader 将按
 *        本映射逐字段搬运；测试内固化该映射即"输入面断言"的载体）。
 *
 * 映射纪律：只搬运 preset/customEaa/basePosition 三字段——BasePlacement
 * 与 BasePlacementDescription 内均无旋转矩阵字段可搬（modeling 只存参数
 * 不存矩阵，P-RT-4/M-11）；R_world_base 由 runtime 规则面从三参数唯一
 * 解析（§9.1"不在此计算 R_world_base"）。
 */
inline runtime::BasePlacementDescription toRuntimeBase(const BasePlacement& base)
{
    runtime::BasePlacementDescription desc;
    desc.preset = base.preset;  // 词表单一权威——runtime 枚举直通
    if (base.customEaa.state() == core::FieldState::Provided) {
        desc.customEaa = core::SourcedValue<rwmath::Vector3D<double>>::provided(
            base.customEaa.value(), base.customEaa.provenance());
    }
    if (base.basePosition.state() == core::FieldState::Provided) {
        desc.basePosition = base.basePosition.value();  // 单位 m（世界系）
    }
    return desc;
}

/// 断言 R 与手抄期望矩阵逐元素一致（exact——预设/手算整数与半整数元素）。
void expectRotationEquals(const rwmath::Rotation3D<double>& actual,
                          double r00, double r01, double r02,
                          double r10, double r11, double r12,
                          double r20, double r21, double r22,
                          double tol)
{
    const double expected[3][3] = {
        {r00, r01, r02}, {r10, r11, r12}, {r20, r21, r22}};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            EXPECT_NEAR(actual(i, j), expected[i][j], tol)
                << "R(" << i << "," << j << ") 与期望矩阵不符";
        }
    }
}

// ---- 导入路径夹具（ImportTest 同款最小复制——io 产物形态装配）----

/// 构造确定性摘要字节（模式填充——映射器不校验摘要与内容一致性）。
core::Digest256 importDigest(std::uint8_t seed)
{
    core::Digest256 d{};
    for (std::size_t i = 0; i < d.size(); ++i) {
        d[i] = static_cast<std::uint8_t>(seed + i);
    }
    return d;
}

/// 组装 ValidatedSource（入口文档 model.urdf＋根节点——无外部资源）。
sdurws::ird::modeling::ValidatedSource makeImportSource(const std::string& urdf)
{
    sdurws::ird::modeling::ValidatedSource source;
    source.bytes.assign(urdf.begin(), urdf.end());
    source.entrySnapshot.finalPath = "Z:/fake/model.urdf";
    source.entrySnapshot.sizeBytes = source.bytes.size();
    source.entrySnapshot.contentDigest = importDigest(0);
    sdurws::ird::io::ResourceDependencyTree tree;
    tree.rootRel = "model.urdf";
    sdurws::ird::io::ResourceNode node;
    node.relPath = "model.urdf";
    node.exists = true;
    sdurws::ird::io::ResourceSnapshot snapshot;
    snapshot.finalPath = "Z:/fake/model.urdf";
    snapshot.sizeBytes = 128;
    snapshot.mtimeUtc = 0;
    snapshot.contentDigest = importDigest(0);
    node.snapshot = snapshot;  // optional 载荷——整体赋值（ImportTest 同款）
    tree.nodes.push_back(node);
    source.dependencyTree = tree;
    return source;
}

/// 两轴旋转链 URDF（无安装语义——V15-04 默认地面用例的最小源面）。
std::string makeTwoAxisChainUrdf()
{
    return "<?xml version=\"1.0\"?>\n<robot name=\"chain\">\n"
           "  <link name=\"base\"/>\n"
           "  <link name=\"l1\"/>\n"
           "  <link name=\"l2\"/>\n"
           "  <joint name=\"j1\" type=\"revolute\">\n"
           "    <parent link=\"base\"/>\n    <child link=\"l1\"/>\n"
           "    <axis xyz=\"0 0 1\"/>\n    <limit lower=\"-1\" upper=\"1\"/>\n"
           "  </joint>\n"
           "  <joint name=\"j2\" type=\"revolute\">\n"
           "    <parent link=\"l1\"/>\n    <child link=\"l2\"/>\n"
           "    <axis xyz=\"0 1 0\"/>\n    <limit lower=\"-1\" upper=\"1\"/>\n"
           "  </joint>\n"
           "</robot>\n";
}

}  // namespace

// =====================================================================
// ACC1：三预设＋custom＋位置编辑面（V-13）＋词表一致性＋原子持久化
// =====================================================================

/**
 * 三预设编辑（acceptance 1——V-13 编辑面）：ground/inverted/wall 逐预设
 * 整体替换接受；preset 直写、basePosition（m，世界系）以 UserProvided
 * 来源 Provided、customEaa 在非 Custom 预设下保持 NotProvided；每次接受
 * 恰追加一条变更摘要记录（UX-05 粒度）。
 */
TEST(MdlBasePlacementEdit, ApplyThreePresetsAndPosition_WP13T11_ACC1_V13)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-22"}, std::vector<std::string>{"AT-37"});

    ModelingWorkingSet ws = makeSixAxisDraft();
    const std::size_t changesBefore = ws.changes.size();

    // —— inverted：位置 (0,0,2.5) m（倒挂吊装高度语义——runtime §6.5 例）。
    BasePlacementEditValue inverted;
    inverted.preset = runtime::InstallationPresetToken::Inverted;
    inverted.basePosition = rwmath::Vector3D<double>(0.0, 0.0, 2.5);
    const auto r1 = applyBasePlacementEdit(ws, inverted);
    EXPECT_FALSE(r1.has_value()) << "inverted 编辑应接受";
    EXPECT_EQ(ws.design.basePlacement.preset,
              runtime::InstallationPresetToken::Inverted);
    ASSERT_EQ(ws.design.basePlacement.basePosition.state(), core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(ws.design.basePlacement.basePosition.value()[2], 2.5);  // m
    EXPECT_EQ(ws.design.basePlacement.basePosition.provenance().kind,
              core::ProvenanceKind::UserProvided)
        << "用户编辑覆盖来源标记（§5.3 规则 1）";
    EXPECT_EQ(ws.design.basePlacement.customEaa.state(), core::FieldState::NotProvided)
        << "非 Custom 预设 customEaa 不适用（I-MDL-7 不触发）";
    EXPECT_EQ(ws.changes.size(), changesBefore + 1) << "一次编辑恰一条变更记录";

    // —— wall：位置 (0.1,-0.2,1.0) m。
    BasePlacementEditValue wall;
    wall.preset = runtime::InstallationPresetToken::Wall;
    wall.basePosition = rwmath::Vector3D<double>(0.1, -0.2, 1.0);
    const auto r2 = applyBasePlacementEdit(ws, wall);
    EXPECT_FALSE(r2.has_value()) << "wall 编辑应接受";
    EXPECT_EQ(ws.design.basePlacement.preset, runtime::InstallationPresetToken::Wall);
    ASSERT_EQ(ws.design.basePlacement.basePosition.state(), core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(ws.design.basePlacement.basePosition.value()[0], 0.1);  // m
    EXPECT_DOUBLE_EQ(ws.design.basePlacement.basePosition.value()[1], -0.2);  // m
    EXPECT_EQ(ws.changes.size(), changesBefore + 2) << "一次编辑恰一条变更记录";

    // —— ground：回到默认预设（位置归零——显式提交零值，非静默）。
    BasePlacementEditValue ground;
    ground.preset = runtime::InstallationPresetToken::Ground;
    ground.basePosition = rwmath::Vector3D<double>(0.0, 0.0, 0.0);
    const auto r3 = applyBasePlacementEdit(ws, ground);
    EXPECT_FALSE(r3.has_value()) << "ground 编辑应接受";
    EXPECT_EQ(ws.design.basePlacement.preset,
              runtime::InstallationPresetToken::Ground);
    ASSERT_EQ(ws.design.basePlacement.basePosition.state(), core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(ws.design.basePlacement.basePosition.value()[0], 0.0);  // m
    EXPECT_EQ(ws.changes.size(), changesBefore + 3) << "一次编辑恰一条变更记录";
}

/**
 * custom 编辑（acceptance 1——V-13 "custom→customEaa"面）：customEaa
 * （rad）以 UserProvided 来源 Provided；切离 Custom 时 customEaa 复位
 * NotProvided（EAA 随预设失效是编辑的显式语义——变更摘要承载，非静默
 * 清除，NFR-COR-03）。
 */
TEST(MdlBasePlacementEdit, ApplyCustomEaaEdit_WP13T11_ACC1_V13)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-22"}, std::vector<std::string>{"AT-37"});

    ModelingWorkingSet ws = makeSixAxisDraft();

    // —— custom＋EAA（绕 z 90°，单位 rad——不是度！）＋吊装位置。
    BasePlacementEditValue custom;
    custom.preset = runtime::InstallationPresetToken::Custom;
    custom.customEaa = rwmath::Vector3D<double>(0.0, 0.0, kPiHalf);
    custom.basePosition = rwmath::Vector3D<double>(0.0, 0.0, 1.2);
    const auto accepted = applyBasePlacementEdit(ws, custom);
    ASSERT_FALSE(accepted.has_value()) << "custom 编辑应接受";
    EXPECT_EQ(ws.design.basePlacement.preset,
              runtime::InstallationPresetToken::Custom);
    ASSERT_EQ(ws.design.basePlacement.customEaa.state(), core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(ws.design.basePlacement.customEaa.value()[2], kPiHalf);  // rad
    EXPECT_EQ(ws.design.basePlacement.customEaa.provenance().kind,
              core::ProvenanceKind::UserProvided);
    ASSERT_EQ(ws.design.basePlacement.basePosition.state(), core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(ws.design.basePlacement.basePosition.value()[2], 1.2);  // m

    // —— 切离 Custom：customEaa 复位 NotProvided（随预设失效——显式语义）。
    BasePlacementEditValue inverted;
    inverted.preset = runtime::InstallationPresetToken::Inverted;
    inverted.basePosition = rwmath::Vector3D<double>(0.0, 0.0, 2.0);
    const auto switched = applyBasePlacementEdit(ws, inverted);
    ASSERT_FALSE(switched.has_value());
    EXPECT_EQ(ws.design.basePlacement.customEaa.state(), core::FieldState::NotProvided)
        << "EAA 随 Custom 预设失效——复位而非残留（残留即双真值）";
    EXPECT_EQ(ws.design.basePlacement.preset,
              runtime::InstallationPresetToken::Inverted);
}

/**
 * 预设 token 词表一致性（acceptance 1——"预设 token InstallationPresetToken
 * 词表与 runtime 一致"）：modeling 不另设词表——BasePlacement.preset 的
 * 类型即 runtime::InstallationPresetToken（类型级钉住）；四值全部经编辑
 * 面到达（编译/链接级证明：枚举直通无翻译层）。
 */
TEST(MdlBasePlacementEdit, PresetTokenVocabularyIsRuntimeEnum_WP13T11_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-22"}, std::vector<std::string>{});

    // 类型级：modeling 值模型的预设字段＝runtime 枚举本身（单一权威——
    // RobotDesign.hpp include 面；任何"平行词表"都会在此编译失败）。
    static_assert(std::is_same<decltype(BasePlacement::preset),
                               runtime::InstallationPresetToken>::value,
                  "modeling 预设词表必须直接复用 runtime::InstallationPresetToken");

    // 值级：四值经 applyBasePlacementEdit 全部可达（Ground 缺省即达；
    // 其余三值显式编辑）。
    ModelingWorkingSet ws = makeSixAxisDraft();
    EXPECT_EQ(ws.design.basePlacement.preset,
              runtime::InstallationPresetToken::Ground);

    BasePlacementEditValue edit;
    edit.preset = runtime::InstallationPresetToken::Custom;
    edit.customEaa = rwmath::Vector3D<double>(0.0, 0.0, 1.0);  // rad
    edit.basePosition = rwmath::Vector3D<double>(0.0, 0.0, 0.0);
    ASSERT_FALSE(applyBasePlacementEdit(ws, edit).has_value());
    EXPECT_EQ(ws.design.basePlacement.preset,
              runtime::InstallationPresetToken::Custom);
}

/**
 * 原子持久化（acceptance 1——"编辑经 apply-robot-design 原子持久化——
 * 单命令单修订"）：编辑后的根对象经 apply-robot-design 载荷提交——
 * prepare 产出恰一根对象写入（单命令＝单修订的内容变更面）、双编译声明
 * 置位、inverse 快照逆载荷携带前一版本根字节（PA-2：撤销＝新修订，历史
 * 不改写）；写入字节解码后 basePlacement 与编辑值逐字段一致（持久化
 * 内容正确性）；第二次编辑再次恰一根写入（每命令一修订的纪律面）。
 */
TEST(MdlBasePlacementEdit, AtomicPersistenceViaApplyRobotDesign_WP13T11_ACC1_V13)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-22", "MDL-06"},
                  std::vector<std::string>{"AT-37"});

    // 夹具：空端口服务集（移动关节设计不消费④端口——文件头注）＋基线
    // 闭包（首修订已含根对象）。
    TestQueryPort query;
    MockCompilePort compile;
    const HandlerServices services{};  // 全缺省——行程校验不触发（无 Revolute）

    const RobotDesign baselineRoot = makePrismaticDesign();
    const core::ObjectId rootOid = baselineRoot.joints.front().objectId;
    query.view = makeBaselineView(core::RevisionId::generate());
    query.addObject(rootOid, std::string(kRobotDesignObjectType),
                    encodeVariant(baselineRoot));

    // 编辑基座安装姿态（倒挂＋吊装高度）——编辑态工作集演算。
    ModelingWorkingSet ws;
    ws.design = baselineRoot;
    ws.rootObjectId = rootOid;
    BasePlacementEditValue edit;
    edit.preset = runtime::InstallationPresetToken::Inverted;
    edit.basePosition = rwmath::Vector3D<double>(0.0, 0.0, 2.0);  // m
    ASSERT_FALSE(applyBasePlacementEdit(ws, edit).has_value());

    // 单命令提交：恰一根槽（替换）→apply-robot-design。
    CommandPayload payload;
    payload.objects.push_back(
        replaceSlot(rootOid, std::string(kRobotDesignObjectType), ws.design));
    ApplyRobotDesignHandler handler(services);
    project::HandlerContext ctx(query, &compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const auto outcome = handler.prepare(
        ctx, makeEnvelope(query.view, std::string(kCmdApplyRobotDesign), payload),
        query.view, plan, diags);

    ASSERT_EQ(outcome, project::PrepareOutcome::Planned);
    ASSERT_EQ(plan.objectWrites.size(), std::size_t{1})
        << "单命令单修订：基座编辑只产生根对象一次写入";
    EXPECT_EQ(plan.objectWrites[0].objectId, rootOid);
    EXPECT_EQ(plan.objectWrites[0].objectTypeToken, std::string(kRobotDesignObjectType));
    EXPECT_TRUE(plan.requiresDualCompile) << "§9.3 表行 1（根对象入 WC/DWC）";
    EXPECT_TRUE(plan.confirmableFindings.empty())
        << "基座编辑不触发行程确认（无关关节行程事实）";
    EXPECT_FALSE(plan.summary.empty()) << "中文命令摘要随修订留痕";

    // 写入字节＝编辑后的根（basePlacement 逐字段一致——持久化内容正确）。
    const RobotDesign written = decodeRootWrite(plan.objectWrites[0]);
    EXPECT_EQ(written.basePlacement, ws.design.basePlacement);
    EXPECT_EQ(written.basePlacement.preset,
              runtime::InstallationPresetToken::Inverted);
    ASSERT_EQ(written.basePlacement.basePosition.state(), core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(written.basePlacement.basePosition.value()[2], 2.0);  // m

    // inverse 快照逆载荷＝前一版本根字节（D-MDL-9/PA-2：撤销产生新修订、
    // 历史闭包完整保留）。
    ASSERT_TRUE(plan.inversePayloadCanonical.has_value());
    const std::optional<CommandPayload> inverse =
        tryDecodeCommandPayload(*plan.inversePayloadCanonical);
    ASSERT_TRUE(inverse.has_value());
    EXPECT_EQ(inverse->mode, CommandPayload::Mode::Restore);
    ASSERT_EQ(inverse->objects.size(), std::size_t{1});
    EXPECT_EQ(inverse->objects[0].objectId, rootOid);
    EXPECT_EQ(inverse->objects[0].objectBytes, encodeVariant(baselineRoot))
        << "逆载荷携带受影响对象前一版本 canonical 字节";

    // 第二次编辑（wall）→再次恰一根写入（每命令一修订的纪律面；修订
    // 编排与事务原子性归 project——S1~S7 已验契约）。
    BasePlacementEditValue secondEdit;
    secondEdit.preset = runtime::InstallationPresetToken::Wall;
    secondEdit.basePosition = rwmath::Vector3D<double>(0.0, 0.0, 1.0);  // m
    ASSERT_FALSE(applyBasePlacementEdit(ws, secondEdit).has_value());
    CommandPayload payload2;
    payload2.objects.push_back(
        replaceSlot(rootOid, std::string(kRobotDesignObjectType), ws.design));
    project::CommandPlan plan2;
    std::vector<core::DiagnosticRecord> diags2;
    const auto outcome2 = handler.prepare(
        ctx, makeEnvelope(query.view, std::string(kCmdApplyRobotDesign), payload2),
        query.view, plan2, diags2);
    ASSERT_EQ(outcome2, project::PrepareOutcome::Planned);
    ASSERT_EQ(plan2.objectWrites.size(), std::size_t{1})
        << "第二次编辑仍恰一根写入——单命令单修订";
    const RobotDesign written2 = decodeRootWrite(plan2.objectWrites[0]);
    EXPECT_EQ(written2.basePlacement.preset, runtime::InstallationPresetToken::Wall);
}

// =====================================================================
// ACC2：未配置默认地面（V15-04）＋I-MDL-7 非法组合拒绝
// =====================================================================

/**
 * 模板路径默认地面（acceptance 2——V15-04 建模侧）：createDraft 填入
 * ground＋零位 basePosition，来源标记 UserProvided/Template
 * （methodTag=template/<id>）——零值语义显式带来源、不静默；显式选择
 * inverted 时不被强制回 ground（用户选择尊重）。
 */
TEST(MdlBasePlacementEdit, DefaultGroundTemplatePath_WP13T11_ACC2_V15_04)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-22"}, std::vector<std::string>{"V15-04"});

    const RobotDesignTemplateFactory factory;
    std::vector<core::DiagnosticRecord> diags;

    // —— ground 路径：预设＋零位地面默认值带模板来源标记（§5.1）。
    const TemplateOutcome groundDraft =
        factory.createDraft(TemplateId{kTemplateIdGeneric6R},
                            runtime::InstallationPresetToken::Ground, "demo", diags);
    ASSERT_TRUE(groundDraft.ok());
    const ModelingWorkingSet ws = groundDraft.get();
    EXPECT_EQ(ws.design.basePlacement.preset,
              runtime::InstallationPresetToken::Ground)
        << "未显式配置＝地面（MDL-22 V15-04——模板层填入）";
    ASSERT_EQ(ws.design.basePlacement.basePosition.state(), core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(ws.design.basePlacement.basePosition.value()[0], 0.0);  // m
    EXPECT_DOUBLE_EQ(ws.design.basePlacement.basePosition.value()[1], 0.0);  // m
    EXPECT_DOUBLE_EQ(ws.design.basePlacement.basePosition.value()[2], 0.0);  // m
    EXPECT_EQ(ws.design.basePlacement.basePosition.provenance().kind,
              core::ProvenanceKind::UserProvided)
        << "零值语义显式带来源——不静默（V15-04）";
    ASSERT_TRUE(ws.design.basePlacement.basePosition.provenance().methodTag.has_value());
    EXPECT_EQ(*ws.design.basePlacement.basePosition.provenance().methodTag,
              "template/generic-6r");

    // —— inverted 路径：用户显式选择不被覆盖（V15-04 只管辖"未配置"面）。
    const TemplateOutcome invertedDraft =
        factory.createDraft(TemplateId{kTemplateIdGeneric6R},
                            runtime::InstallationPresetToken::Inverted, "demo2", diags);
    ASSERT_TRUE(invertedDraft.ok());
    EXPECT_EQ(invertedDraft.get().design.basePlacement.preset,
              runtime::InstallationPresetToken::Inverted);
}

/**
 * 导入路径默认地面（acceptance 2——V15-04 建模侧）：URDF 无安装语义→
 * 映射层填入 ground 并入默认补全清单（来源 ImportMapped）——零值语义
 * 显式带来源、不静默（NFR-COR-03）。
 */
TEST(MdlBasePlacementEdit, DefaultGroundImportPath_WP13T11_ACC2_V15_04)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-22", "MDL-03"},
                  std::vector<std::string>{"V15-04"});

    ModelImportMapper mapper;
    std::vector<core::DiagnosticRecord> diags;
    const ImportOutcome outcome =
        mapper.mapUrdf(makeImportSource(makeTwoAxisChainUrdf()), ImportOptions{}, diags);
    ASSERT_TRUE(outcome.draft.has_value()) << "合法链应产出草稿";

    // 映射层填入 ground（MDL-22 默认——未显式配置即此值）。
    const RobotDesign& draft = *outcome.draft;
    EXPECT_EQ(draft.basePlacement.preset, runtime::InstallationPresetToken::Ground);

    // 默认补全清单：basePlacement 必入清单（不静默）＋来源 ImportMapped。
    bool basePlacementDefaulted = false;
    for (const auto& item : outcome.report.defaults) {
        if (item.field == "basePlacement") {
            basePlacementDefaulted = true;
            EXPECT_NE(item.appliedValue.find("ground"), std::string::npos)
                << "清单条目应记录默认地面值";
            EXPECT_NE(item.reason.find("import-mapped"), std::string::npos)
                << "清单条目应记录 ImportMapped 来源";
        }
    }
    EXPECT_TRUE(basePlacementDefaulted)
        << "basePlacement 默认地面必须入默认补全清单（V15-04 不静默）";
}

/**
 * I-MDL-7 非法组合拒绝（acceptance 2）：custom 缺 customEaa
 * （CustomEaaMissing）；preset≠ground 而 R=I（PresetIdentityRotation——
 * 映射层拒绝，恒等判定逐元素 1×10⁻¹²）；customEaa/basePosition 非有限
 * （ValueNotFinite）；非 Custom 携带 EAA＝调用方契约违约（fail-fast）；
 * 全部拒绝路径工作集字节不变（强保证）。正交性容差（1×10⁻¹²）的判定
 * 本体在值模型 I-MDL-7 单一实现（RobotDesignTest IMdl7 用例钉住）——
 * 本编辑流经 checkInvariants 复用同一判定（RotationNotOrthogonal 码），
 * Rodrigues 构造下不可达（防御闸——无公开输入面可触达，故无负向用例，
 * 见实现注）。
 */
TEST(MdlBasePlacementEdit, IMdl7IllegalCombinationsRejected_WP13T11_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-22"}, std::vector<std::string>{});

    ModelingWorkingSet ws = makeSixAxisDraft();

    // (a) custom 缺 customEaa → CustomEaaMissing（I-MDL-7 前半）。
    {
        BasePlacementEditValue edit;
        edit.preset = runtime::InstallationPresetToken::Custom;
        edit.basePosition = rwmath::Vector3D<double>(0.0, 0.0, 0.0);
        const ModelingWorkingSet before = ws;
        const auto rejection = applyBasePlacementEdit(ws, edit);
        ASSERT_TRUE(rejection.has_value());
        EXPECT_EQ(rejection->code, BasePlacementEditErrorCode::CustomEaaMissing);
        EXPECT_EQ(ws, before) << "拒绝路径工作集字节不变";
    }

    // (b) custom＋零矢量 EAA → R=I → PresetIdentityRotation（I-MDL-7 后半
    //     "preset≠ground 而 R=I"映射层拒绝——runtime InputInvalid 同口径）。
    {
        BasePlacementEditValue edit;
        edit.preset = runtime::InstallationPresetToken::Custom;
        edit.customEaa = rwmath::Vector3D<double>(0.0, 0.0, 0.0);  // rad——零旋转
        edit.basePosition = rwmath::Vector3D<double>(0.0, 0.0, 0.0);
        const ModelingWorkingSet before = ws;
        const auto rejection = applyBasePlacementEdit(ws, edit);
        ASSERT_TRUE(rejection.has_value());
        EXPECT_EQ(rejection->code, BasePlacementEditErrorCode::PresetIdentityRotation);
        EXPECT_EQ(ws, before) << "拒绝路径工作集字节不变";
    }

    // (c) 恒等判定的容差边界（非恒等侧——1×10⁻¹² 逐元素）：1×10⁻⁶ rad
    //     绕 z 的 R 非对角元 ≈1×10⁻⁶ > 1×10⁻¹² → 非恒等 → 接受；随后
    //     值模型 I-MDL-7 正交性零违例（正交容差内自洽）。
    {
        BasePlacementEditValue edit;
        edit.preset = runtime::InstallationPresetToken::Custom;
        edit.customEaa = rwmath::Vector3D<double>(0.0, 0.0, 1e-6);  // rad
        edit.basePosition = rwmath::Vector3D<double>(0.0, 0.0, 0.0);
        const auto accepted = applyBasePlacementEdit(ws, edit);
        ASSERT_FALSE(accepted.has_value()) << "非恒等微旋转应接受";
        bool imdl7Violation = false;
        for (const InvariantViolation& v : checkInvariants(ws.design)) {
            if (v.id == InvariantId::IMdl7) { imdl7Violation = true; }
        }
        EXPECT_FALSE(imdl7Violation) << "接受的 custom 值应通过 I-MDL-7";
    }

    // (d) 恒等判定的容差边界（恒等侧）：1×10⁻¹³ rad 绕 z 的 R 逐元素与 I
    //     偏差 ≤1×10⁻¹² → 判恒等 → 拒绝（与 runtime"Custom 而 R=I"同拒）。
    {
        BasePlacementEditValue edit;
        edit.preset = runtime::InstallationPresetToken::Custom;
        edit.customEaa = rwmath::Vector3D<double>(0.0, 0.0, 1e-13);  // rad
        edit.basePosition = rwmath::Vector3D<double>(0.0, 0.0, 0.0);
        const ModelingWorkingSet before = ws;
        const auto rejection = applyBasePlacementEdit(ws, edit);
        ASSERT_TRUE(rejection.has_value()) << "恒等旋转（容差内）应拒绝";
        EXPECT_EQ(rejection->code, BasePlacementEditErrorCode::PresetIdentityRotation);
        EXPECT_EQ(ws, before);
    }

    // (e) customEaa 含 NaN → ValueNotFinite（I-MDL-3 不静默置 0）。
    {
        BasePlacementEditValue edit;
        edit.preset = runtime::InstallationPresetToken::Custom;
        edit.customEaa = rwmath::Vector3D<double>(
            0.0, std::numeric_limits<double>::quiet_NaN(), 0.0);
        edit.basePosition = rwmath::Vector3D<double>(0.0, 0.0, 0.0);
        const ModelingWorkingSet before = ws;
        const auto rejection = applyBasePlacementEdit(ws, edit);
        ASSERT_TRUE(rejection.has_value());
        EXPECT_EQ(rejection->code, BasePlacementEditErrorCode::ValueNotFinite);
        EXPECT_EQ(ws, before);
    }

    // (f) basePosition 含 Inf → ValueNotFinite。
    {
        BasePlacementEditValue edit;
        edit.preset = runtime::InstallationPresetToken::Inverted;
        edit.basePosition = rwmath::Vector3D<double>(
            std::numeric_limits<double>::infinity(), 0.0, 0.0);  // m
        const ModelingWorkingSet before = ws;
        const auto rejection = applyBasePlacementEdit(ws, edit);
        ASSERT_TRUE(rejection.has_value());
        EXPECT_EQ(rejection->code, BasePlacementEditErrorCode::ValueNotFinite);
        EXPECT_EQ(ws, before);
    }

    // (g) preset=Ground 而携带 customEaa＝调用方契约违约 → fail-fast。
    {
        BasePlacementEditValue edit;
        edit.preset = runtime::InstallationPresetToken::Ground;
        edit.customEaa = rwmath::Vector3D<double>(0.0, 0.0, 1.0);  // 违约载荷
        edit.basePosition = rwmath::Vector3D<double>(0.0, 0.0, 0.0);
        const ModelingWorkingSet before = ws;
        EXPECT_THROW(applyBasePlacementEdit(ws, edit), std::invalid_argument);
        EXPECT_EQ(ws, before) << "fail-fast 路径同样零写入";
    }

    // 局部错误码 token 表（词表登记完整性——四值全有稳定 token）。
    EXPECT_EQ(std::string(basePlacementEditErrorCodeToken(
                  BasePlacementEditErrorCode::ValueNotFinite)),
              "value-not-finite");
    EXPECT_EQ(std::string(basePlacementEditErrorCodeToken(
                  BasePlacementEditErrorCode::CustomEaaMissing)),
              "custom-eaa-missing");
    EXPECT_EQ(std::string(basePlacementEditErrorCodeToken(
                  BasePlacementEditErrorCode::PresetIdentityRotation)),
              "preset-identity-rotation");
    EXPECT_EQ(std::string(basePlacementEditErrorCodeToken(
                  BasePlacementEditErrorCode::RotationNotOrthogonal)),
              "rotation-not-orthogonal");
}

// =====================================================================
// ACC3：P-RT-4 交叉核对——预设轴向唯一权威产出点（modeling 只存参数）
// =====================================================================

/**
 * P-RT-4 交叉核对（acceptance 3——O-16 已裁决 2026-09-22 的兑现行）：
 * 编辑产物（modeling 只存 preset/参数，无任何矩阵字段）经 Description.base
 * 同款字段映射交 runtime——T_world_base 的旋转部分与 runtime 权威产出点
 * installationPresetRotation() 的冻结值逐元素一致，且与 runtime.md §6.2
 * 表（测试内独立抄写）一致：
 *   - inverted → R_x(π)＝diag(1,−1,−1)（基座 +Z 指世界 −Z）；
 *   - wall     → R_y(π/2)（第 1 行 [0,0,1]、第 3 行 [−1,0,0]）；
 *   - ground   → I。
 * 位置分量原样透传（modeling 不预乘任何旋转）。矩阵仅在 runtime 侧
 * 物质化＝V-12 建模侧输入面断言；结论留痕
 * traceability/wp13-t11-prt4-crosscheck.md。
 */
TEST(MdlBasePlacementEdit, Prt4PresetAxisSingleAuthority_WP13T11_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-22"}, std::vector<std::string>{"AT-37"});

    // —— inverted：编辑→映射→runtime 解析；R 与 §6.2 冻结值逐元素一致
    //     （预设矩阵元素 ∈ {0,±1} 编码无舍入——精确相等）。
    {
        ModelingWorkingSet ws = makeSixAxisDraft();
        BasePlacementEditValue edit;
        edit.preset = runtime::InstallationPresetToken::Inverted;
        edit.basePosition = rwmath::Vector3D<double>(0.0, 0.0, 2.0);  // m
        ASSERT_FALSE(applyBasePlacementEdit(ws, edit).has_value());

        const auto resolved =
            runtime::resolveWorldBaseTransform(toRuntimeBase(ws.design.basePlacement));
        ASSERT_TRUE(resolved.ok()) << "合法编辑表示应可被 runtime 解析";
        // 倒挂＝R_x(π)（P-RT-4 冻结值）。
        expectRotationEquals(resolved.get().R(),
                             1.0, 0.0, 0.0,
                             0.0, -1.0, 0.0,
                             0.0, 0.0, -1.0,
                             0.0);
        EXPECT_DOUBLE_EQ(resolved.get().P()[2], 2.0) << "位置原样透传（m）";

        // 与权威产出点函数逐元素一致（交叉核对的直接对照面）。
        const rwmath::Rotation3D<double> authority =
            runtime::installationPresetRotation(
                runtime::InstallationPresetToken::Inverted);
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                ASSERT_EQ(resolved.get().R()(i, j), authority(i, j));
            }
        }
    }

    // —— wall：R_y(π/2)（第 1 行 [0,0,1]、第 3 行 [−1,0,0]）。
    {
        ModelingWorkingSet ws = makeSixAxisDraft();
        BasePlacementEditValue edit;
        edit.preset = runtime::InstallationPresetToken::Wall;
        edit.basePosition = rwmath::Vector3D<double>(0.1, 0.0, 1.0);  // m
        ASSERT_FALSE(applyBasePlacementEdit(ws, edit).has_value());

        const auto resolved =
            runtime::resolveWorldBaseTransform(toRuntimeBase(ws.design.basePlacement));
        ASSERT_TRUE(resolved.ok());
        // 壁装＝R_y(π/2)＝[[0,0,1],[0,1,0],[−1,0,0]]（第 1 行 [0,0,1]、
        // 第 3 行 [−1,0,0]——runtime.md §6.2/P-RT-4 冻结值）。
        expectRotationEquals(resolved.get().R(),
                             0.0, 0.0, 1.0,
                             0.0, 1.0, 0.0,
                             -1.0, 0.0, 0.0,
                             0.0);
        EXPECT_DOUBLE_EQ(resolved.get().P()[0], 0.1) << "位置原样透传（m）";
    }

    // —— ground：恒等（预设矩阵唯一非平凡面之外的默认值——V15-04）。
    {
        ModelingWorkingSet ws = makeSixAxisDraft();
        BasePlacementEditValue edit;
        edit.preset = runtime::InstallationPresetToken::Ground;
        edit.basePosition = rwmath::Vector3D<double>(1.0, 2.0, 3.0);  // m
        ASSERT_FALSE(applyBasePlacementEdit(ws, edit).has_value());

        const auto resolved =
            runtime::resolveWorldBaseTransform(toRuntimeBase(ws.design.basePlacement));
        ASSERT_TRUE(resolved.ok());
        // 地面＝I（§6.2 默认行）。
        expectRotationEquals(resolved.get().R(),
                             1.0, 0.0, 0.0,
                             0.0, 1.0, 0.0,
                             0.0, 0.0, 1.0,
                             0.0);
        EXPECT_DOUBLE_EQ(resolved.get().P()[0], 1.0);
        EXPECT_DOUBLE_EQ(resolved.get().P()[1], 2.0);
        EXPECT_DOUBLE_EQ(resolved.get().P()[2], 3.0);
    }
}

// =====================================================================
// ACC4：编译单一字段（MDL-22/M-11）——编辑值经 Description.base 由
// runtime 唯一编译进 R_world_base
// =====================================================================

/**
 * 单一字段编译用例（acceptance 4）：custom EAA 编辑值（唯一可变旋转的
 * 预设）经 Description.base 映射由 runtime 解析——R 与手算 R_z(π/2) 一致
 * （1×10⁻¹² 容差）、平移＝basePosition（m）；runtime S5 构造校验面
 * （checkWorldBaseTransform/checkPresetConsistency）对编译产物全部通过；
 * 仅改 basePlacement 的字节面证据：编码字节变化、解码往返逐字段一致、
 * 其余字段不变（单一字段变更面——M-11 单一不变量的输入侧）。
 *
 * modeling 侧"不计算/不缓存/不二次叠加"的可执行形态：BasePlacement 及
 * 映射产物无任何矩阵字段可承载 R（结构面）；R 全部由 runtime 规则面
 * 从三参数解析（本用例的直接断言面）。四消费方读同一编译产物的联合
 * 观测归 runtime/policy 侧（RT-BW 契约测试——本卡观测点＝输入面）。
 */
TEST(MdlBasePlacementEdit, SingleFieldCompileIntoRWorldBase_WP13T11_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-22"}, std::vector<std::string>{"AT-37"});

    RobotDesign before = makePrismaticDesign();
    const std::vector<std::uint8_t> bytesBefore = encodeVariant(before);

    ModelingWorkingSet ws;
    ws.design = before;
    BasePlacementEditValue edit;
    edit.preset = runtime::InstallationPresetToken::Custom;
    edit.customEaa = rwmath::Vector3D<double>(0.0, 0.0, kPiHalf);  // rad——绕 z 90°
    edit.basePosition = rwmath::Vector3D<double>(1.0, 2.0, 3.0);   // m
    ASSERT_FALSE(applyBasePlacementEdit(ws, edit).has_value());

    // —— 字节面：仅 basePlacement 变更（单一字段变更面）。
    const std::vector<std::uint8_t> bytesAfter = encodeVariant(ws.design);
    EXPECT_NE(bytesBefore, bytesAfter) << "编辑改变 canonical 字节（新内容版本前提）";
    {
        RobotDesignCodec codec;
        const auto decoded = codec.decode(bytesAfter, kCurrentFormatVersion);
        ASSERT_TRUE(decoded.ok());
        const RobotDesign& roundtrip = std::get<RobotDesign>(decoded.get());
        EXPECT_EQ(roundtrip.basePlacement, ws.design.basePlacement)
            << "解码往返 basePlacement 逐字段一致";
        // 单一字段：把往返值的 basePlacement 摘除后应与编辑前全量一致
        // （除 basePlacement 外其余字段零变化——字节面单字段证据；
        // decode(encode(x))==x 的全量往返由 CodecTest 钉住，此处不重复）。
        RobotDesign roundtripOthers = roundtrip;
        roundtripOthers.basePlacement = before.basePlacement;
        EXPECT_EQ(roundtripOthers, before) << "除 basePlacement 外其余字段不变";
    }

    // —— 编译面：Description.base 映射→runtime 唯一解析 R_world_base。
    const auto resolved =
        runtime::resolveWorldBaseTransform(toRuntimeBase(ws.design.basePlacement));
    ASSERT_TRUE(resolved.ok());
    // 手算 R_z(π/2)＝[[0,−1,0],[1,0,0],[0,0,1]]（绕 z 90°——独立抄写，
    // 不引实现常量）；Rodrigues 数值通道 1×10⁻¹² 容差。
    expectRotationEquals(resolved.get().R(),
                         0.0, -1.0, 0.0,
                         1.0, 0.0, 0.0,
                         0.0, 0.0, 1.0,
                         1e-12);
    EXPECT_DOUBLE_EQ(resolved.get().P()[0], 1.0);  // m
    EXPECT_DOUBLE_EQ(resolved.get().P()[1], 2.0);  // m
    EXPECT_DOUBLE_EQ(resolved.get().P()[2], 3.0);  // m

    // —— runtime S5 构造校验面对编译产物全部通过（输入面合法性的下游
    //     确认——非法组合在编辑边界已被 I-MDL-7 拒绝，见 ACC2 用例）。
    EXPECT_FALSE(runtime::checkWorldBaseTransform(resolved.get()).has_value())
        << "矩阵层合法（正交 1×10⁻¹²＋det=+1＋有限）";
    EXPECT_FALSE(runtime::checkPresetConsistency(
                     runtime::InstallationPresetToken::Custom,
                     resolved.get())
                     .has_value())
        << "预设一致性合法（Custom 且 R≠I）";
}
