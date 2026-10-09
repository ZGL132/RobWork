/**
 * @file   BackfillTest.cpp
 * @brief  器件回填用例组（SelBackfill*——WP-19-T09）——SEL-10 回填命令
 *         与合成的单元测试面：物性合成黄金数值（MDL-16/MDL-05）、壳体/
 *         转子分轨不重复计入（SEL-10）、MDL-06①～③断言失败拒绝、命令/
 *         记录载荷 canonical 编解码、回填数据组装拒绝族、计划内核三态
 *         与 AT-30 复算提示。
 *
 * 设计依据：
 *   - units/selection.md §12（回填时序/纪律/token/物性合成规则——各用例
 *     语义权威）、§12.4（合成公式与断言容差——附录 D 第 6/7 项）、
 *     §14.8（处理器契约）、§16 WP-19-T09 行（回填产生新修订＋依赖失效
 *     提示；复核前不沿用原通过结论）
 *   - 需求 SEL-10（经领域命令回填各轴 DriveTrainDesign——记录目录版本
 *     与安装关系；按明确参考系合成；壳体/转子区分不重复计入）、
 *     MDL-16（合成物性来源区分）、MDL-05（平行轴规则）、MDL-06（断言
 *     ①～③）、AT-30（新修订提示复算；复核前不沿用通过结论）、CON-05
 *     （内容寻址——canonical 确定性）、NFR-DEP-04（版本不受理拒绝）
 *   - 任务契约 tasks/foundation/WP-19-T09.json acceptance 1/2
 *
 * ★ 接口消费纪律（不留只测自由函数的盲区——WP-20-T03 教训）：处理器面
 *   全部用例经 IDeviceBackfillCommandHandler 接口引用消费
 *   planFromEnvelope/commandType/currentPayloadVersion。prepare 直通壳
 *   本组**不直测**：其入参 HandlerContext 的构造函数实现于 project 库
 *   （CommandServiceImpl.cpp——selection 零 project 链接边，测试目标不
 *   链 project），壳体对内核是零逻辑直通转发（Backfill.cpp prepare 函
 *   数体——三态 switch 逐项对应），端到端（真实命令服务 submit→恰一新
 *   修订）归 L5 装配集成面（单元卡 §21 如实登记）。本组以"内核三态全
 *   覆盖＋壳转发代码走查"承载 prepare 语义，不虚称端到端已执行。
 *
 * 黄金值口径：合成算例为手工解析可验证的两壳体＋连杆配置（黄金期望以
 * 分数表达式写在断言旁）；容差 1×10⁻⁹（量纲量级的浮点运算余量——附录
 * D 精神：断言容差只用于 MDL-06 判定面〔1×10⁻¹² 相对〕，测试期望比对
 * 采用 1×10⁻⁹ 绝对容差）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/selection/Backfill.hpp>
#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/DiagCodes.hpp>

#include <sdurws/ird/project/CommandService.hpp>  // RevisionView/ObjectRef（公共头——测试面）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::selection;
using namespace sdurws::ird;  // 使限定符 core::/project:: 可见

namespace {

// =====================================================================
// 本组黄金数据（自持——目录快照与物理输入，验证互不依赖）
// =====================================================================

/// 黄金电机：目录壳体质量/转子惯量（合成与记录的目录侧取值源）。
MotorCatalogEntry makeGoldenMotor()
{
    MotorCatalogEntry m;
    m.modelId = "M-GOLD";
    m.vendor = "Golden";
    m.displayName = "黄金电机";
    m.catalog = CatalogIdentity{"cat-gold", "v1", core::ContentIdentity{}, "黄金测试目录"};
    m.ratedTorque = 4.0;     // 额定连续转矩，N·m
    m.peakTorque = 10.0;     // 峰值转矩，N·m
    m.ratedSpeed = 150.0;    // 额定转速，rad/s
    m.maxSpeed = 300.0;      // 最高转速，rad/s
    m.ratedPower = 1500.0;   // 额定功率，W
    m.dutyClass = "S1";
    m.rotorInertia = 0.01;   // 转子惯量，kg·m²（独立登记字段——不进合成）
    m.mass = 6.0;            // 壳体质量，kg（合成输入）
    m.mounting = MountSpec{"flangeA", "shaftB"};
    m.status = ValidationStatus::Valid;
    return m;
}

/// 黄金减速器：壳体质量/壳体惯量（§12.4 合成项——目录可缺失字段的齐备面）。
GearboxCatalogEntry makeGoldenGearbox(bool withHousingInertia = true)
{
    GearboxCatalogEntry g;
    g.modelId = "G-GOLD";
    g.vendor = "Golden";
    g.displayName = "黄金减速器";
    g.catalog = CatalogIdentity{"cat-gold", "v1", core::ContentIdentity{}, "黄金测试目录"};
    g.ratedOutputTorque = 50.0;   // 额定输出转矩，N·m
    g.peakOutputTorque = 100.0;   // 峰值输出转矩，N·m
    g.maxInputSpeed = 300.0;      // 允许输入转速，rad/s
    g.ratio = 10.0;               // 速比，无量纲
    g.efficiency = 0.95;          // 效率，无量纲
    g.mountingOrientation = "flange";
    g.mass = 3.0;                 // 壳体质量，kg（合成输入）
    if (withHousingInertia) {
        g.housingInertia = 0.02;  // 壳体惯量，kg·m²（标量目录列→对角承载，
                                  // 黄金面以标量值进对角——合成只读六分量，
                                  // 黄金期望按标量语义核对）
    }
    g.mounting = MountSpec{"flangeA", "shaftB"};
    g.status = ValidationStatus::Valid;
    return g;
}

/// 黄金目录快照（电机/减速器/兼容对齐备；曲线空表合法——导入校验面）。
CatalogPackageSnapshot makeGoldenSnapshot(bool gearboxHousingInertia = true)
{
    CatalogPackageSnapshot s;
    CatalogManifest manifest;
    manifest.identity = CatalogIdentity{"cat-gold", "v1", core::ContentIdentity{}, "黄金测试目录"};
    s.manifest = manifest;
    s.motors = {makeGoldenMotor()};
    s.gearboxes = {makeGoldenGearbox(gearboxHousingInertia)};
    CompatibilityRecord compat;
    compat.motorId = "M-GOLD";
    compat.gearboxId = "G-GOLD";
    compat.mountKind = "flangeA";
    s.compatibility = {compat};
    return s;
}

/// 组装源黄金面（连杆原值/锚点/补充惯量——§12.4 合成输入）。
AxisBackfillSource makeGoldenSource(const core::ObjectId& jointId)
{
    AxisBackfillSource src;
    src.jointId = jointId;
    src.motorModelId = "M-GOLD";
    src.gearboxModelId = "G-GOLD";
    src.mountKind = "flangeA";
    // 连杆原值（权威模型；连杆坐标系；SI）。
    src.linkMassKg = 2.0;                       // kg
    src.linkComM = BackfillVec3{0.1, 0.0, 0.05}; // m
    src.linkInertia = BackfillInertiaTensor{0.02, 0.03, 0.04, 0.0, 0.0, 0.0}; // kg·m²
    // 壳体质心锚点（安装布置；连杆坐标系；m）。
    src.motorComAnchorM = BackfillVec3{0.0, 0.0, 0.1};
    src.gearboxComAnchorM = BackfillVec3{0.2, 0.0, 0.0};
    // 电机壳体惯量补充（目录 v1 缺口——P-SEL-7；黄金面对角标量）。
    src.motorHousingInertiaSupplement =
        BackfillInertiaTensor{0.05, 0.05, 0.05, 0.0, 0.0, 0.0};
    src.appliedRatio = 0.1;  // 回填传动比（c=1/n，n=10——候选传动参数口径）
    return src;
}

/// 直接构造单轴回填条目（合成内核用——不经组装器，隔离被测面）。
AxisBackfillEntry makeGoldenEntry(const core::ObjectId& jointId)
{
    AxisBackfillEntry a;
    a.jointId = jointId;
    a.catalogId = "cat-gold";
    a.catalogVersion = "v1";
    a.motorModelId = "M-GOLD";
    a.gearboxModelId = "G-GOLD";
    a.mountKind = "flangeA";
    a.catalogLockObject = core::ObjectId::generate();
    a.catalogLockVersion = core::ContentVersion{};
    a.appliedRatio = 0.1;
    a.linkMassKg = 2.0;
    a.linkComM = BackfillVec3{0.1, 0.0, 0.05};
    a.linkInertia = BackfillInertiaTensor{0.02, 0.03, 0.04, 0.0, 0.0, 0.0};
    a.motorComAnchorM = BackfillVec3{0.0, 0.0, 0.1};
    a.motorHousingMassKg = 6.0;
    a.motorHousingInertia = BackfillInertiaTensor{0.05, 0.05, 0.05, 0.0, 0.0, 0.0};
    a.gearboxComAnchorM = BackfillVec3{0.2, 0.0, 0.0};
    a.gearboxHousingMassKg = 3.0;
    a.gearboxHousingInertia = BackfillInertiaTensor{0.02, 0.02, 0.02, 0.0, 0.0, 0.0};
    a.rotorInertiaKgM2 = 0.01;
    return a;
}

/// 黄金锁定版本引用（与快照身份一致）。
CatalogVersion makeGoldenLock()
{
    CatalogVersion lock;
    lock.identity = CatalogIdentity{"cat-gold", "v1", core::ContentIdentity{}, "黄金测试目录"};
    lock.lockObjectId = core::ObjectId::generate();
    return lock;
}

/// 构造单轴合法命令载荷（经 encode 保证协议一致）。
DeviceBackfillRequest makeGoldenRequest(std::size_t axisCount = 1)
{
    DeviceBackfillRequest req;
    for (std::size_t i = 0; i < axisCount; ++i) {
        req.axes.push_back(makeGoldenEntry(core::ObjectId::generate()));
    }
    return req;
}

/// 构造闭包引用行。
project::ObjectRef makeRef(core::ObjectId oid, std::string token)
{
    project::ObjectRef ref;
    ref.objectId = oid;
    ref.contentVersion = core::ContentVersion{};
    ref.objectTypeToken = std::move(token);
    ref.digest256 = std::string(64, '0');
    return ref;
}

/// 构造基线修订视图（闭包引用集可注入——计划内核的基线判定面）。
project::RevisionView makeBaseline(std::vector<project::ObjectRef> refs = {})
{
    project::RevisionView view;
    view.id = core::RevisionId::generate();
    view.seq = 1;
    view.branch = core::BranchId::generate();
    view.objectRefs = std::move(refs);
    view.metadataRef.objectTypeToken = "project-metadata";
    return view;
}

/// 按请求注入目录锁定引用的基线（判定 3"目录版本存在"的正向前置——
/// 锁定对象 (oid, cv=零) 以 oid 半区命中闭包存在性核对）。
project::RevisionView makeBaselineWithLockRefs(const DeviceBackfillRequest& req,
                                               std::vector<project::ObjectRef> extra = {})
{
    std::vector<project::ObjectRef> refs = std::move(extra);
    for (const AxisBackfillEntry& axis : req.axes) {
        refs.push_back(makeRef(axis.catalogLockObject, "catalog"));
    }
    return makeBaseline(std::move(refs));
}

}  // namespace

// =====================================================================
// 物性合成内核（§12.4——MDL-16 黄金数值＋断言失败拒绝）
// =====================================================================

/**
 * 黄金数值（MDL-16/MDL-05——手工解析算例）：连杆(2kg)＋电机壳体(6kg)
 * ＋减速器壳体(3kg) 的质量加权质心与平行轴两步迁移。黄金期望以分数
 * 表达式书写（0.8/11 等——浮点运算序差在 1×10⁻⁹ 内）。
 */
TEST(SelBackfillSynthesis, GoldenValuesMassComInertia_MDL16)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-10", "MDL-16", "MDL-05"},
                  std::vector<std::string>{});

    const AxisBackfillEntry axis = makeGoldenEntry(core::ObjectId::generate());
    const SynthesisOutcome o = synthesizeAxisBodyProperties(axis);

    ASSERT_TRUE(o.ok) << o.detail;
    // 合成质量＝三部件之和（kg）。
    EXPECT_NEAR(o.synthesis.massKg, 11.0, 1e-9);
    // 合成质心＝质量加权（m，连杆系）：c=(0.8/11, 0, 0.7/11)。
    EXPECT_NEAR(o.synthesis.comM.x, 0.8 / 11.0, 1e-9);
    EXPECT_NEAR(o.synthesis.comM.y, 0.0, 1e-12);
    EXPECT_NEAR(o.synthesis.comM.z, 0.7 / 11.0, 1e-9);
    // 合成惯量＝Σ(I_i + m_i(|c_i|²E−c_i c_iᵀ)) − M(|c|²E−c cᵀ)（kg·m²）：
    // 原点系 Ixx=0.155/Iyy=0.305/Izz=0.25/Ixz=−0.01（连杆 0.025/0.055/0.06
    // ＋电机 0.11/0.11/0.05＋减速器 0.02/0.14/0.14）；回迁 Bxx=11·0.49/121、
    // Byy=11·1.13/121、Bzz=11·0.64/121、Bxz=−11·0.56/121。
    EXPECT_NEAR(o.synthesis.inertia.ixx, 0.155 - 11.0 * 0.49 / 121.0, 1e-9);
    EXPECT_NEAR(o.synthesis.inertia.iyy, 0.305 - 11.0 * 1.13 / 121.0, 1e-9);
    EXPECT_NEAR(o.synthesis.inertia.izz, 0.250 - 11.0 * 0.64 / 121.0, 1e-9);
    EXPECT_NEAR(o.synthesis.inertia.ixy, 0.0, 1e-9);
    EXPECT_NEAR(o.synthesis.inertia.ixz, -0.01 + 11.0 * 0.56 / 121.0, 1e-9);
    EXPECT_NEAR(o.synthesis.inertia.iyz, 0.0, 1e-9);
    // 转子独立登记照抄（kg·m²）。
    EXPECT_NEAR(o.synthesis.rotorInertiaKgM2, 0.01, 1e-12);
}

/**
 * 壳体/转子分轨（SEL-10——禁止重复计入的机器证明）：转子惯量变更任意
 * 倍率后，合成质量/质心/合成惯量六分量必须逐位不变——转子若被计入
 * 合成，惯量必然随倍率漂移；同时独立登记字段正确跟随。
 */
TEST(SelBackfillSynthesis, RotorInertiaNeverEntersSynthesis_SEL10)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-10", "MDL-16"},
                  std::vector<std::string>{"AT-30"});

    AxisBackfillEntry base = makeGoldenEntry(core::ObjectId::generate());
    AxisBackfillEntry scaled = base;
    scaled.rotorInertiaKgM2 = base.rotorInertiaKgM2 * 137.0;  // 任意倍率

    const SynthesisOutcome a = synthesizeAxisBodyProperties(base);
    const SynthesisOutcome b = synthesizeAxisBodyProperties(scaled);
    ASSERT_TRUE(a.ok);
    ASSERT_TRUE(b.ok);

    // 合成物性三件套逐位不变（IEEE754 精确相等——同一确定路径）。
    EXPECT_EQ(a.synthesis.massKg, b.synthesis.massKg);
    EXPECT_EQ(a.synthesis.comM, b.synthesis.comM);
    EXPECT_EQ(a.synthesis.inertia, b.synthesis.inertia);
    // 独立登记字段正确跟随（不参与合成≠丢失）。
    EXPECT_NEAR(b.synthesis.rotorInertiaKgM2, base.rotorInertiaKgM2 * 137.0, 1e-12);
}

/// 断言①前半（输入卫生——部件质量非正拒绝；NFR-COR-03 不静默置零）。
TEST(SelBackfillSynthesis, RejectsNonPositiveMass_NFRCOR03)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06", "NFR-COR-03"},
                  std::vector<std::string>{});

    AxisBackfillEntry axis = makeGoldenEntry(core::ObjectId::generate());
    axis.motorHousingMassKg = 0.0;  // 部件质量非正——输入卫生轨
    const SynthesisOutcome o = synthesizeAxisBodyProperties(axis);
    EXPECT_FALSE(o.ok);
    EXPECT_EQ(o.failure, SynthesisFailure::RangeInvalid);
}

/// 非有限输入拒绝（NaN 质心——NFR-COR-03；不静默置零/不传播 NaN）。
TEST(SelBackfillSynthesis, RejectsNonFiniteInput_NFRCOR03)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"},
                  std::vector<std::string>{});

    AxisBackfillEntry axis = makeGoldenEntry(core::ObjectId::generate());
    axis.linkComM.x = std::numeric_limits<double>::quiet_NaN();
    const SynthesisOutcome o = synthesizeAxisBodyProperties(axis);
    EXPECT_FALSE(o.ok);
    EXPECT_EQ(o.failure, SynthesisFailure::NonFiniteInput);
}

/**
 * 断言②（SPD——MDL-06②同语义）：合成惯量含负主值时拒绝（硬断言轨
 * ——回填失败零修订）。
 */
TEST(SelBackfillSynthesis, RejectsNotPositiveDefinite_MDLL06A2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06"},
                  std::vector<std::string>{});

    AxisBackfillEntry axis = makeGoldenEntry(core::ObjectId::generate());
    // 全部件锚点与质心置于原点（平行轴迁移零贡献）→ 合成惯量＝惯量直和。
    axis.linkComM = BackfillVec3{};
    axis.motorComAnchorM = BackfillVec3{};
    axis.gearboxComAnchorM = BackfillVec3{};
    axis.linkInertia = BackfillInertiaTensor{-1.0, 0.03, 0.04, 0.0, 0.0, 0.0}; // Ixx<0
    const SynthesisOutcome o = synthesizeAxisBodyProperties(axis);
    EXPECT_FALSE(o.ok);
    EXPECT_EQ(o.failure, SynthesisFailure::NotPositiveDefinite);
}

/**
 * 断言③（三角不等式——MDL-06③同语义）：合成主惯量 λ1+λ2<λ3 时拒绝。
 * 黄金反例：三部件同轴堆叠使合成 (0.2, 0.2, 1.1)——0.2+0.2<1.1。
 */
TEST(SelBackfillSynthesis, RejectsTriangleInequality_MDLL06A3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06"},
                  std::vector<std::string>{});

    AxisBackfillEntry axis = makeGoldenEntry(core::ObjectId::generate());
    axis.linkComM = BackfillVec3{};
    axis.motorComAnchorM = BackfillVec3{};
    axis.gearboxComAnchorM = BackfillVec3{};
    // 三部件惯量直和＝(0.1+0.05+0.05, 同, 1.0+0.05+0.05)＝(0.2, 0.2, 1.1)。
    axis.linkInertia = BackfillInertiaTensor{0.1, 0.1, 1.0, 0.0, 0.0, 0.0};
    const SynthesisOutcome o = synthesizeAxisBodyProperties(axis);
    EXPECT_FALSE(o.ok);
    EXPECT_EQ(o.failure, SynthesisFailure::TriangleInequality);
}

/// 等号边界（三角不等式取等＝平面薄板极限——容差内必须通过；附录 D 第
/// 6 项相对 1×10⁻¹² 的舍入余量语义）。
TEST(SelBackfillSynthesis, AcceptsDegeneratePlanarEquality_MDLL06A3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06"},
                  std::vector<std::string>{});

    AxisBackfillEntry axis = makeGoldenEntry(core::ObjectId::generate());
    axis.linkComM = BackfillVec3{};
    axis.motorComAnchorM = BackfillVec3{};
    axis.gearboxComAnchorM = BackfillVec3{};
    // 直和＝(1, 1, 2)——λ1+λ2=λ3 恰等号（薄板极限，合法）。
    axis.linkInertia = BackfillInertiaTensor{1.0, 1.0, 2.0, 0.0, 0.0, 0.0};
    const SynthesisOutcome o = synthesizeAxisBodyProperties(axis);
    EXPECT_TRUE(o.ok) << o.detail;
}

// =====================================================================
// 命令载荷 canonical 编解码（CON-05 确定性＋严格解码）
// =====================================================================

/// 载荷往返＋编码确定性（CON-05/NFR-COR-02：同输入恒同字节）。
TEST(SelBackfillCodec, PayloadRoundTripDeterministic_CON05)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-05", "NFR-COR-02"},
                  std::vector<std::string>{});

    const DeviceBackfillRequest req = makeGoldenRequest(2);  // 两轴黄金面
    const std::vector<std::uint8_t> bytes = encodeBackfillPayload(req);
    // 编码确定性（两次编码逐字节相等）。
    EXPECT_EQ(bytes, encodeBackfillPayload(req));

    const DecodedBackfillPayload back = decodeBackfillPayload(bytes);
    ASSERT_EQ(back.status, DecodedBackfillPayload::Status::Ok) << back.detail;
    EXPECT_EQ(back.request, req);  // 值往返全等（operator== 全字段）
}

/// 编码入口 fail-fast 族（调用方错误轨——§14.0：空轴/重复轴/词表外）。
TEST(SelBackfillCodec, EncodeRejectsCallerContractViolations)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-10"},
                  std::vector<std::string>{});

    // 空轴表。
    DeviceBackfillRequest empty;
    EXPECT_THROW((void)encodeBackfillPayload(empty), std::invalid_argument);
    // 参考系词表外。
    DeviceBackfillRequest badFrame = makeGoldenRequest(1);
    badFrame.referenceFrameToken = "world-frame";
    EXPECT_THROW((void)encodeBackfillPayload(badFrame), std::invalid_argument);
    // 同轴重复。
    DeviceBackfillRequest dup;
    const core::ObjectId same = core::ObjectId::generate();
    dup.axes.push_back(makeGoldenEntry(same));
    dup.axes.push_back(makeGoldenEntry(same));
    EXPECT_THROW((void)encodeBackfillPayload(dup), std::invalid_argument);
    // 轴身份保留值（全零）。
    DeviceBackfillRequest zeroId;
    zeroId.axes.push_back(makeGoldenEntry(core::ObjectId{}));
    EXPECT_THROW((void)encodeBackfillPayload(zeroId), std::invalid_argument);
    // 部件质量非正（编码面卫生——非法字节不落盘）。
    DeviceBackfillRequest badMass = makeGoldenRequest(1);
    badMass.axes[0].linkMassKg = 0.0;
    EXPECT_THROW((void)encodeBackfillPayload(badMass), std::invalid_argument);
}

/// 解码严格面（截断/魔数/版本/残余——Malformed/UnsupportedVersion 分轨）。
TEST(SelBackfillCodec, DecodeRejectsMalformedAndForeignVersions)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-DEP-04"},
                  std::vector<std::string>{});

    const std::vector<std::uint8_t> bytes =
        encodeBackfillPayload(makeGoldenRequest(1));

    // 截断（去尾 8 字节——轴段残缺）。
    const std::vector<std::uint8_t> truncated(bytes.begin(), bytes.end() - 8);
    const DecodedBackfillPayload t = decodeBackfillPayload(truncated);
    EXPECT_EQ(t.status, DecodedBackfillPayload::Status::Malformed);

    // 魔数不符（首字节翻 转——形态轨）。
    std::vector<std::uint8_t> badMagic = bytes;
    badMagic[0] ^= 0xFF;
    EXPECT_EQ(decodeBackfillPayload(badMagic).status,
              DecodedBackfillPayload::Status::Malformed);

    // 版本不符（协议演进面——升级指引轨，非结构损坏）。
    std::vector<std::uint8_t> badVersion = bytes;
    badVersion[11] = 0x09;  // codec 版本字段（偏移 8..11 小端）低字节置 9
    EXPECT_EQ(decodeBackfillPayload(badVersion).status,
              DecodedBackfillPayload::Status::UnsupportedVersion);

    // 尾部残余（追加垃圾字节——形态轨）。
    std::vector<std::uint8_t> trailing = bytes;
    trailing.push_back(0xAB);
    EXPECT_EQ(decodeBackfillPayload(trailing).status,
              DecodedBackfillPayload::Status::Malformed);
}

/// 记录对象往返＋复算提示保真（AT-30 记录面：四域＋不沿用标志）。
TEST(SelBackfillCodec, RecordObjectRoundTripWithRecalcNotice_AT30)
{
    IRD_TEST_INFO(std::vector<std::string>{"AT-30", "CON-05"},
                  std::vector<std::string>{});

    const DeviceBackfillRequest req = makeGoldenRequest(2);
    BackfillRecordObject record;
    record.referenceFrameToken = req.referenceFrameToken;
    record.recalc = BackfillRecalcNotice{};  // 四域全＋retain=false（AT-30 默认）
    for (const AxisBackfillEntry& axis : req.axes) {
        const SynthesisOutcome o = synthesizeAxisBodyProperties(axis);
        ASSERT_TRUE(o.ok);
        record.synthesis.push_back(o.synthesis);
    }
    record.axes = req.axes;

    const std::vector<std::uint8_t> bytes = encodeBackfillRecordObject(record);
    EXPECT_EQ(bytes, encodeBackfillRecordObject(record));  // 确定性

    const DecodedBackfillRecord back = decodeBackfillRecordObject(bytes);
    ASSERT_EQ(back.status, DecodedBackfillRecord::Status::Ok) << back.detail;
    EXPECT_EQ(back.record, record);  // 往返全等（含复算提示）
    // AT-30 语义字段显式核对。
    for (std::size_t i = 0; i < kRecalcDomainCount; ++i) {
        EXPECT_TRUE(back.record.recalc.domains[i]);
    }
    EXPECT_FALSE(back.record.recalc.retainPriorConclusion);
}

// =====================================================================
// 回填数据组装（目录取值/安装核对/数据缺失整体失败）
// =====================================================================

/// 组装黄金路径：目录字段取值（壳体质量/转子惯量）＋安装关系记录＋
/// 逐轴合成预演（SEL-10——记录目录版本与安装关系）。
TEST(SelBackfillAssembly, GoldenAssemblyTakesCatalogValues_SEL10)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-10"},
                  std::vector<std::string>{});

    const CatalogPackageSnapshot snapshot = makeGoldenSnapshot();
    const CatalogVersion lock = makeGoldenLock();
    const std::vector<AxisBackfillSource> sources = {
        makeGoldenSource(core::ObjectId::generate()),
        makeGoldenSource(core::ObjectId::generate()),
    };

    const BackfillAssemblyOutcome o =
        assembleDeviceBackfill(snapshot, lock, sources);
    ASSERT_EQ(o.kind, BackfillAssemblyOutcome::Kind::Assembled) << o.detail;

    // 请求面：目录版本标识与锁定引用来自 lock 参数。
    ASSERT_EQ(o.request.axes.size(), sources.size());
    for (std::size_t i = 0; i < sources.size(); ++i) {
        EXPECT_EQ(o.request.axes[i].catalogId, "cat-gold");
        EXPECT_EQ(o.request.axes[i].catalogVersion, "v1");
        EXPECT_EQ(o.request.axes[i].mountKind, "flangeA");   // 安装关系记录
        EXPECT_EQ(o.request.axes[i].motorModelId, "M-GOLD");
        EXPECT_EQ(o.request.axes[i].gearboxModelId, "G-GOLD");
        EXPECT_EQ(o.request.axes[i].catalogLockObject, lock.lockObjectId);
        // 目录取值：壳体质量（电机 6/减速器 3 kg）与转子惯量（0.01）。
        EXPECT_DOUBLE_EQ(o.request.axes[i].motorHousingMassKg, 6.0);
        EXPECT_DOUBLE_EQ(o.request.axes[i].gearboxHousingMassKg, 3.0);
        EXPECT_DOUBLE_EQ(o.request.axes[i].rotorInertiaKgM2, 0.01);
        EXPECT_DOUBLE_EQ(o.request.axes[i].appliedRatio, 0.1);
    }
    // 合成预演与请求同序同长。
    ASSERT_EQ(o.synthesis.size(), sources.size());
    EXPECT_NEAR(o.synthesis[0].massKg, 11.0, 1e-9);
    // 参考系留痕（§12.4——记录参考系）。
    EXPECT_EQ(o.request.referenceFrameToken, std::string(kBackfillFrameLink));
}

/// 型号不在快照主表（组装拒绝族——UnknownDevice）。
TEST(SelBackfillAssembly, RejectsUnknownDevice)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-10"},
                  std::vector<std::string>{});

    CatalogPackageSnapshot snapshot = makeGoldenSnapshot();
    snapshot.motors.clear();  // 电机主表置空——查找必落空
    const BackfillAssemblyOutcome o = assembleDeviceBackfill(
        snapshot, makeGoldenLock(), {makeGoldenSource(core::ObjectId::generate())});
    EXPECT_EQ(o.kind, BackfillAssemblyOutcome::Kind::UnknownDevice);
}

/// 安装关系与兼容表不一致（§12.2 纪律 4——记录一致性前提）。
TEST(SelBackfillAssembly, RejectsMountMismatch_SEL10)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-10"},
                  std::vector<std::string>{});

    AxisBackfillSource src = makeGoldenSource(core::ObjectId::generate());
    src.mountKind = "shaftC";  // 兼容表只登记 flangeA
    const BackfillAssemblyOutcome o = assembleDeviceBackfill(
        makeGoldenSnapshot(), makeGoldenLock(), {src});
    EXPECT_EQ(o.kind, BackfillAssemblyOutcome::Kind::MountIncompatible);
}

/// 电机壳体惯量无补充（P-SEL-7 目录缺口＋§12.4 整体失败口径）。
TEST(SelBackfillAssembly, RejectsMissingMotorHousingInertia_PSEL6)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-16"},
                  std::vector<std::string>{});

    AxisBackfillSource src = makeGoldenSource(core::ObjectId::generate());
    src.motorHousingInertiaSupplement = std::nullopt;  // 未补充
    const BackfillAssemblyOutcome o = assembleDeviceBackfill(
        makeGoldenSnapshot(), makeGoldenLock(), {src});
    EXPECT_EQ(o.kind, BackfillAssemblyOutcome::Kind::DataInsufficient);
}

/// 减速器壳体惯量目录缺失（optional 落空——数据不足不伪造）。
TEST(SelBackfillAssembly, RejectsMissingGearboxHousingInertia_PSEL6)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-16"},
                  std::vector<std::string>{});

    const BackfillAssemblyOutcome o = assembleDeviceBackfill(
        makeGoldenSnapshot(/*gearboxHousingInertia=*/false), makeGoldenLock(),
        {makeGoldenSource(core::ObjectId::generate())});
    EXPECT_EQ(o.kind, BackfillAssemblyOutcome::Kind::DataInsufficient);
}

/// 锁定引用与快照身份失配（引用完整性——调用方错误轨）。
TEST(SelBackfillAssembly, RejectsLockIdentityMismatch)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-10"},
                  std::vector<std::string>{});

    CatalogVersion lock = makeGoldenLock();
    lock.identity.catalogId = "cat-other";  // 与快照 manifest 不一致
    const BackfillAssemblyOutcome o = assembleDeviceBackfill(
        makeGoldenSnapshot(), lock, {makeGoldenSource(core::ObjectId::generate())});
    EXPECT_EQ(o.kind, BackfillAssemblyOutcome::Kind::InvalidInput);
}

/// 回填传动比非法（I-MDL-11 同口径——有限>0；组装期即拒绝）。
TEST(SelBackfillAssembly, RejectsInvalidRatio_IMDL11)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-10", "MDL-16"},
                  std::vector<std::string>{});

    AxisBackfillSource src = makeGoldenSource(core::ObjectId::generate());
    src.appliedRatio = -0.5;  // 非法（≤0）
    const BackfillAssemblyOutcome o = assembleDeviceBackfill(
        makeGoldenSnapshot(), makeGoldenLock(), {src});
    EXPECT_EQ(o.kind, BackfillAssemblyOutcome::Kind::InvalidInput);
}

// =====================================================================
// 计划内核（IDeviceBackfillCommandHandler 接口消费——三态＋AT-30）
// =====================================================================

/// token 无点词形（P-SEL-3——project §4.4.4 冻结 ^[a-z0-9-]{3,64}；与
/// modeling D-MDL-6/O-35 同案）＋payload 版本冻结值。
TEST(SelBackfillPlan, HandlerTokenDotlessAndPayloadVersion_PSEL3)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-10"},
                  std::vector<std::string>{"P-SEL-3"});

    const IDeviceBackfillCommandHandler& handler = DeviceBackfillCommandHandler{};
    EXPECT_EQ(handler.commandType(), "apply-device-backfill");
    // 冻结语法逐字符自证（小写字母/数字/连字符；长度 3..64；不含点）。
    const std::string token = handler.commandType();
    ASSERT_GE(token.size(), 3u);
    ASSERT_LE(token.size(), 64u);
    for (const char c : token) {
        EXPECT_TRUE((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')
            << "token 出现冻结语法外字符: " << c;
        EXPECT_NE(c, '.') << "token 含点（P-PR-9 裁决前禁止——P-SEL-3）";
    }
    EXPECT_EQ(handler.currentPayloadVersion(), kBackfillPayloadFormatVersion);
    EXPECT_EQ(handler.currentPayloadVersion(), 1u);
}

/**
 * AT-30 主用例：合法载荷＋空基线 → Planned＋恰一 sel-device-backfill
 * 对象写＋记录载荷可解码回读（合成/条目/复算提示齐备）＋命令摘要留痕
 * （四域复算＋不沿用）。"恰一对象写"＝多轴整体原子的记录面载体（§12.2
 * 纪律 3）；"新修订"由 project S6 对该计划的事务提交产生（恰一修订）。
 */
TEST(SelBackfillPlan, PlanHappyPathExactlyOneObjectWrite_AT30)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-10", "MDL-16"},
                  std::vector<std::string>{"AT-30"});

    const DeviceBackfillRequest goldenReq = makeGoldenRequest(3);
    const project::CommandEnvelope env =
        makeBackfillEnvelope(core::BranchId::generate(), std::nullopt, goldenReq);

    DeviceBackfillCommandHandler handler;
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const BackfillPlanOutcome o =
        handler.planFromEnvelope(env, makeBaselineWithLockRefs(goldenReq), plan, diags);

    ASSERT_EQ(o.kind, BackfillPlanOutcome::Kind::Planned) << o.detail;
    EXPECT_TRUE(diags.empty());  // 成功路径零诊断
    // 恰一对象写（多轴整体原子——一个命令一个记录对象）。
    ASSERT_EQ(plan.objectWrites.size(), 1u);
    EXPECT_EQ(plan.objectWrites.front().objectTypeToken, "sel-device-backfill");
    EXPECT_FALSE(plan.objectWrites.front().objectId.has_value());  // 新建＝project 取号
    EXPECT_FALSE(plan.requiresDualCompile);
    EXPECT_FALSE(plan.inverseCommandType.has_value());

    // 记录载荷可解码回读（三轴合成/条目/复算提示）。
    const DecodedBackfillRecord back =
        decodeBackfillRecordObject(plan.objectWrites.front().payloadCanonical);
    ASSERT_EQ(back.status, DecodedBackfillRecord::Status::Ok) << back.detail;
    ASSERT_EQ(back.record.axes.size(), 3u);
    ASSERT_EQ(back.record.synthesis.size(), 3u);
    for (std::size_t i = 0; i < kRecalcDomainCount; ++i) {
        EXPECT_TRUE(back.record.recalc.domains[i])
            << "复算域缺失: " << recalcDomainToken(static_cast<RecalcDomain>(i));
    }
    EXPECT_FALSE(back.record.recalc.retainPriorConclusion);  // 不沿用原结论
    // 命令摘要留痕（AT-30 提示面——随修订持久化）。
    EXPECT_NE(plan.summary.find("复算"), std::string::npos);
    EXPECT_NE(plan.summary.find("不沿用"), std::string::npos);
    EXPECT_NE(plan.summary.find("3 轴"), std::string::npos);
}

/// 基线闭包已有记录对象 → 继承其 oid 改版（PA-2：旧版本字节随历史
/// 修订闭包保留——不可变历史）。
TEST(SelBackfillPlan, PlanReusesBaselineRecordObjectId_PA2)
{
    IRD_TEST_INFO(std::vector<std::string>{"PA-2"},
                  std::vector<std::string>{});

    const core::ObjectId existing = core::ObjectId::generate();
    const DeviceBackfillRequest goldenReq = makeGoldenRequest(1);
    // 基线闭包＝既有记录对象＋目录锁定引用（判定 3 正向前置）。
    project::RevisionView baseline = makeBaselineWithLockRefs(
        goldenReq, {makeRef(existing, std::string(kBackfillRecordObjectToken))});

    const project::CommandEnvelope env =
        makeBackfillEnvelope(core::BranchId::generate(), std::nullopt, goldenReq);
    DeviceBackfillCommandHandler handler;
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const BackfillPlanOutcome o = handler.planFromEnvelope(env, baseline, plan, diags);

    ASSERT_EQ(o.kind, BackfillPlanOutcome::Kind::Planned) << o.detail;
    ASSERT_EQ(plan.objectWrites.size(), 1u);
    ASSERT_TRUE(plan.objectWrites.front().objectId.has_value());
    EXPECT_EQ(*plan.objectWrites.front().objectId, existing);  // 改版继承
}

/// 载荷版本不受理（NFR-DEP-04——拒绝＋SEL-BACKFILL-PAYLOAD-VERSION-
/// UNSUPPORTED 定位）。
TEST(SelBackfillPlan, PlanRejectsForeignPayloadVersion_NFRDEP04)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-DEP-04"},
                  std::vector<std::string>{});

    project::CommandEnvelope env =
        makeBackfillEnvelope(core::BranchId::generate(), std::nullopt, makeGoldenRequest(1));
    env.payloadFormatVersion = 99;  // 未来版本

    DeviceBackfillCommandHandler handler;
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const BackfillPlanOutcome o = handler.planFromEnvelope(env, makeBaseline(), plan, diags);

    EXPECT_EQ(o.kind, BackfillPlanOutcome::Kind::RejectedInvalidInput);
    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags.front().code, std::string(kSelBackfillPayloadVersionUnsupported));
    EXPECT_TRUE(plan.objectWrites.empty());  // 拒绝零计划
}

/// 载荷结构非法（Malformed——SEL-BACKFILL-PAYLOAD-MALFORMED 定位）。
TEST(SelBackfillPlan, PlanRejectsMalformedPayload)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-10"},
                  std::vector<std::string>{});

    project::CommandEnvelope env;
    env.commandType = std::string(kBackfillCommandToken);
    env.payloadFormatVersion = kBackfillPayloadFormatVersion;
    env.payloadCanonical = {0x00, 0x01, 0x02};  // 三字节垃圾（magic 必不符）

    DeviceBackfillCommandHandler handler;
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const BackfillPlanOutcome o = handler.planFromEnvelope(env, makeBaseline(), plan, diags);

    EXPECT_EQ(o.kind, BackfillPlanOutcome::Kind::RejectedInvalidInput);
    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags.front().code, std::string(kSelBackfillPayloadMalformed));
    EXPECT_TRUE(plan.objectWrites.empty());
}

/// 目录锁定引用不在基线闭包（§12.1 S3"目录版本存在"的基线侧判定——
/// SEL-BACKFILL-LOCK-REF-MISMATCH 定位）。
TEST(SelBackfillPlan, PlanRejectsLockRefOutsideBaseline)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-10"},
                  std::vector<std::string>{});

    // 载荷携带的锁定 oid 不注入基线闭包（空基线）→ 必失配。
    const project::CommandEnvelope env =
        makeBackfillEnvelope(core::BranchId::generate(), std::nullopt, makeGoldenRequest(1));
    DeviceBackfillCommandHandler handler;
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const BackfillPlanOutcome o = handler.planFromEnvelope(env, makeBaseline(), plan, diags);

    EXPECT_EQ(o.kind, BackfillPlanOutcome::Kind::RejectedInvalidInput);
    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags.front().code, std::string(kSelBackfillLockRefMismatch));
}

/// 锁定引用在基线闭包（正向半区——存在性核对通过）。
TEST(SelBackfillPlan, PlanAcceptsLockRefInsideBaseline)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-10"},
                  std::vector<std::string>{});

    // 用黄金请求的锁定 oid 构造闭包（先编码取出 oid）。
    const DeviceBackfillRequest req = makeGoldenRequest(1);
    const DecodedBackfillPayload decoded =
        decodeBackfillPayload(encodeBackfillPayload(req));
    ASSERT_EQ(decoded.status, DecodedBackfillPayload::Status::Ok);
    std::vector<project::ObjectRef> refs;
    refs.push_back(makeRef(decoded.request.axes.front().catalogLockObject, "catalog"));
    refs.push_back(makeRef(core::ObjectId::generate(), "robot-design"));

    const project::CommandEnvelope env =
        makeBackfillEnvelope(core::BranchId::generate(), std::nullopt, req);
    DeviceBackfillCommandHandler handler;
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const BackfillPlanOutcome o = handler.planFromEnvelope(env, makeBaseline(refs), plan, diags);

    ASSERT_EQ(o.kind, BackfillPlanOutcome::Kind::Planned) << o.detail;
    EXPECT_TRUE(diags.empty());
}

/**
 * 合成断言失败（MDL-06③三角违约载荷——经编码面〔编码允许断言失败
 * 数据落盘〕进入 prepare 域 → 硬断言轨 RejectedHardAssert＋
 * SEL-BACKFILL-SYNTHESIS-ASSERT-FAILED 定位；多轴整体原子——恰一轴
 * 失败即整体拒绝零计划）。
 */
TEST(SelBackfillPlan, PlanRejectsSynthesisTriangleViolation_MDL06)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06", "SEL-10"},
                  std::vector<std::string>{});

    // 构造三角违约轴（同合成内核用例的黄金反例）＋一轴合法——多轴整体。
    AxisBackfillEntry bad = makeGoldenEntry(core::ObjectId::generate());
    bad.linkComM = BackfillVec3{};
    bad.motorComAnchorM = BackfillVec3{};
    bad.gearboxComAnchorM = BackfillVec3{};
    bad.linkInertia = BackfillInertiaTensor{0.1, 0.1, 1.0, 0.0, 0.0, 0.0};

    DeviceBackfillRequest req;
    req.axes.push_back(makeGoldenEntry(core::ObjectId::generate()));  // 合法轴
    req.axes.push_back(bad);                                          // 违约轴
    const std::vector<std::uint8_t> bytes = encodeBackfillPayload(req);  // 编码允许

    project::CommandEnvelope env;
    env.commandType = std::string(kBackfillCommandToken);
    env.payloadFormatVersion = kBackfillPayloadFormatVersion;
    env.payloadCanonical = bytes;

    // 基线注入锁定引用（判定 3 通过——本用例钉判定 4 合成断言轨）。
    DeviceBackfillCommandHandler handler;
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const BackfillPlanOutcome o =
        handler.planFromEnvelope(env, makeBaselineWithLockRefs(req), plan, diags);

    EXPECT_EQ(o.kind, BackfillPlanOutcome::Kind::RejectedHardAssert);  // 硬断言轨
    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags.front().code, std::string(kSelBackfillSynthesisAssertFailed));
    EXPECT_TRUE(plan.objectWrites.empty());  // 整体失败零计划（零修订）
}

// gtest 主入口由 test/TestMainReport.cpp 提供（ird-test-report.json 用例级
// 明细与 gtest XML 并存——ird_add_gtest 宏的报告通道）。
