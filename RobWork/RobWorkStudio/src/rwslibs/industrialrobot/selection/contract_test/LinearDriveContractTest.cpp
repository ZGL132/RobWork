/**
 * @file   LinearDriveContractTest.cpp
 * @brief  直线传动选型层契约用例组（SelLinearDriveContract）——WP-19-T12
 *         acceptance 2/3 的合同面：零 drivetrain 编译边与零直线映射公式
 *         词表（机器可断言红线——selection 只消费不自实现直线映射）、
 *         T12 批词表/稳定码/器件类别词表封闭性、v2 schema 注册面钉扎、
 *         经 IHardConstraintSelector 接口分派消费的可达性。
 *
 * 设计依据：
 *   - units/selection.md §9.1（"selection 不得重新计算"红线——直线侧同款
 *     机器可断言形态）、§17.2（SEL-09-S1：drivetrain 扩展端口——selection
 *     只消费；四类器件词表；曲线量纲词表扩展）、§10.3/§10.4（词表封闭）、
 *     §3.2（零 drivetrain 编译边——P-SEL-2）、§14.4（接口契约——直线通道
 *     经既有筛选器接口面）、§5.2（v2 注册面）
 *   - 需求 SEL-09-S1、DYN-04（唯一映射实现——直线侧延伸）、NFR-MNT-03
 *     （单一权威——词表唯一书写点）
 *   - 任务契约 tasks/foundation/WP-19-T12.json acceptance 2（零 drivetrain
 *     include、零映射公式词表，契约测试扫描锁定）＋acceptance 3（ird_gates
 *     零命中的同源机器面）
 *
 * 与单元测试的分工：单元测试管黄金值与判定正确性；本契约文件管"合同
 * 面"——红线扫描、词表稳定性、注册面落值、接口分派可达性。
 *
 * 线程约束：gtest 用例天然串行。
 */

#include <sdurws/ird/selection/CatalogProvider.hpp>
#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/DiagCodes.hpp>
#include <sdurws/ird/selection/LinearDrive.hpp>
#include <sdurws/ird/selection/Screening.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace sdurws::ird::selection;
namespace core = sdurws::ird::core;

#ifndef IRD_SELECTION_UNIT_ROOT
#error "契约测试需要 IRD_SELECTION_UNIT_ROOT 注入（CMake 编译定义）"
#endif

namespace {

/// 产品面源码收集（include/**＋src/**——与 CombinationCheckContractTest
/// 同口径：selection 产品面两层结构固定）。
std::vector<std::string> collectProductSources()
{
    std::vector<std::string> files;
    const std::string root = IRD_SELECTION_UNIT_ROOT;
    const std::vector<std::string> dirs = {root + "/selection/include",
                                            root + "/selection/src"};
    for (const std::string& dir : dirs) {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                files.push_back(entry.path().string());
            }
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

/// 读文件全文（文本模式——UTF-8 原样）。
std::string readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

/// 契约自持最小目录（单直线器件——接口分派可达性行使的最小面）。
CatalogIdentity contractCatalog()
{
    CatalogIdentity id;
    id.catalogId = "cat-linear-contract";
    id.version = "1.0.0";
    id.source = "直线传动契约自持夹具（WP-19-T12）";
    return id;
}

CatalogPackageSnapshot makeContractSnapshot()
{
    CatalogPackageSnapshot snap;
    snap.manifest.formatVersion = kCatalogFormatVersionV2;
    snap.manifest.identity = contractCatalog();
    // 恰一个可行直线器件（滚珠丝杠——公共能力面）。
    LinearDriveCatalogEntry d;
    d.modelId = "LD-CT-001";
    d.catalog = snap.manifest.identity;
    d.kind = LinearDriveKind::BallScrew;
    d.ratedForce = 9000.0;    ///< N
    d.peakForce = 15000.0;    ///< N
    d.maxLinearSpeed = 1.0;   ///< m/s
    d.ratedPower = 5000.0;    ///< W
    d.mass = 12.0;            ///< kg
    d.status = ValidationStatus::Valid;
    snap.linearDrives.push_back(std::move(d));
    snap.contentIdentity = computePackageContentIdentity(snap);
    snap.manifest.identity.contentIdentity = snap.contentIdentity;
    return snap;
}

/// 直线轴工作点事实（可行侧——全部维度在能力内）。
LinearAxisWorkpointFacts makeFeasibleFacts(const core::ObjectId& axis)
{
    LinearAxisWorkpointFacts f;
    f.jointId = axis;
    f.caseId = "case-contract";
    f.forceRms = 6000.0;         ///< N
    f.forcePeak = 10500.0;       ///< N
    f.linearSpeedPeak = 0.8;     ///< m/s（恰边界——附录 D C7 容差内不淘汰）
    f.powerPeak = 4000.0;        ///< W
    f.powerRms = 3000.0;         ///< W
    f.displacementPeak = 1.0;    ///< m（承载不判定字段）
    f.accelerationPeak = 2.0;    ///< m/s²（承载不判定字段）
    return f;
}

}  // namespace

// =====================================================================
// 红线一：零 drivetrain 编译边（P-SEL-2——③端口不落编译边；直线通道
// 的新增源文件同样纳入扫描域）
// =====================================================================

/**
 * 全产品面零他单元 include（直线通道源文件入域复扫——acceptance 2
 * "零 drivetrain include"的机器面；与 CombinationCheckContractTest 的
 * R-2 扫描互为冗余防线，本用例锚定 SEL-09-S1 增量面）。
 */
TEST(SelLinearDriveContract, ProductFaceHasNoDrivetrainIncludes_WP19T12_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09-S1", "DYN-04"},
                  std::vector<std::string>{"AT-36"});  // R2 选型层——零映射编译边
    for (const std::string& path : collectProductSources()) {
        const std::string text = readFile(path);
        EXPECT_EQ(text.find("#include <sdurws/ird/drivetrain/"), std::string::npos)
            << "产品面包含 drivetrain 头（零编译边红线——" << path << "）";
        EXPECT_EQ(text.find("#include <sdurws/ird/dynamics/"), std::string::npos) << path;
        EXPECT_EQ(text.find("#include <sdurws/ird/modeling/"), std::string::npos) << path;
    }
}

// =====================================================================
// 红线二：零直线映射公式词表（drivetrain 唯一映射实现的直线侧延伸——
// DYN-04/§9.1；selection 只消费类型化广义量事实，不自实现换算）
// =====================================================================

/**
 * 全产品面零直线映射公式词表（acceptance 2"零映射公式词表，契约测试
 * 扫描锁定"）：词表为"旋转量→直线量换算"的计算表达式特征（丝杠/齿条/
 * 同步带的传动常数换算与推力-速度乘积）——映射输出的唯一产生者是
 * drivetrain 扩展端口（§16.2），selection 面只承载事实字段消费。词表
 * 扫描全文（含注释）——注释书写公式词形同样命中（防"文档性第二实现"
 * 漂移为代码）。
 */
TEST(SelLinearDriveContract, ProductFaceHasNoLinearMappingFormulaTokens_WP19T12_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09-S1", "DYN-04", "NFR-MNT-07"},
                  std::vector<std::string>{"AT-36"});  // R2 选型层——零自实现映射
    // 词表（计算表达式特征——直线映射公式族的表达式面）：
    //   丝杠传动常数换算（圆周率倍数乘除）/ 齿条小齿轮节圆半径乘算 /
    //   同步带轮速比换算 / 推力×速度的功率乘算 / 直线映射核符号。
    // 注意：工作点事实×安全系数（如 *f.forcePeak * sf）是筛选消费不是
    // 映射公式——词表不收录（映射公式特征是"传动常数/几何参数参与旋转
    // →直线换算"与"F×v 造功率"，不是阈值比较）。
    const std::vector<std::string> formulaTokens = {
        "2.0 * 3.14159265", "3.14159265 * 2.0", "6.283185307",
        "screwLead", "lead_mm", "leadMm", "pitchRadius", "pinionRadius",
        "pulleyRadius", "beltRatio", "gearRadius", "thetaToLinear",
        "linearFromTheta", "forcePeak * speedPeak", "forceRms * speedPeak",
        "LinearMappingCore", "LinearDriveMapping",
    };
    for (const std::string& path : collectProductSources()) {
        const std::string text = readFile(path);
        for (const std::string& token : formulaTokens) {
            EXPECT_EQ(text.find(token), std::string::npos)
                << "产品面命中直线映射公式词表 «" << token << "»（" << path
                << "——不自实现直线映射，SEL-09-S1/§9.1 直线侧同款红线）";
        }
    }
}

// =====================================================================
// 词表封闭性（§10.3 T12 批——ReasonToken/稳定码/器件类别/曲线量纲）
// =====================================================================

/**
 * T12 批直线 token 词表文本钉扎（acceptance 2 词表面）：4 个新 token 的
 * 词表文本逐字钉住＋既有 34 token 规模语义（kReasonTokenCount 38＝34＋4
 * ——表尾追加纪律的规模锚）；直线 token 的稳定码映射非空且等于 T12 批
 * 新码常量（唯一映射点）。
 */
TEST(SelLinearDriveContract, T12TokenVocabularyAndDiagCodes_WP19T12_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09-S1", "SEL-06", "ERR-01", "NFR-MNT-03"},
                  std::vector<std::string>{"AT-36"});  // R2 选型层——词表封闭
    // 词表规模锚：38＝既有 34＋T12 批 4（追加只允许表尾——既有枚举值零变化）。
    EXPECT_EQ(kReasonTokenCount, 38);
    // T12 批 4 token 文本逐字钉扎（词表序＝枚举追加序——稳定排序键）。
    EXPECT_EQ(reasonTokenText(ReasonToken::LinearForceContinuousInsufficient),
              "linear-force-continuous-insufficient");
    EXPECT_EQ(reasonTokenText(ReasonToken::LinearForcePeakInsufficient),
              "linear-force-peak-insufficient");
    EXPECT_EQ(reasonTokenText(ReasonToken::LinearSpeedInsufficient),
              "linear-speed-insufficient");
    EXPECT_EQ(reasonTokenText(ReasonToken::LinearPowerInsufficient),
              "linear-power-insufficient");
    // T12 批 4 token → 稳定码映射（唯一映射点——SEL-LINEAR- 新码族）。
    EXPECT_EQ(reasonTokenDiagCode(ReasonToken::LinearForceContinuousInsufficient),
              kSelLinearForceContinuousInsufficient);
    EXPECT_EQ(reasonTokenDiagCode(ReasonToken::LinearForcePeakInsufficient),
              kSelLinearForcePeakInsufficient);
    EXPECT_EQ(reasonTokenDiagCode(ReasonToken::LinearSpeedInsufficient),
              kSelLinearSpeedInsufficient);
    EXPECT_EQ(reasonTokenDiagCode(ReasonToken::LinearPowerInsufficient),
              kSelLinearPowerInsufficient);
    // 登记表同源：4 码全部在 selectionCodeEntries 表尾（单一登记面——
    // 装配期 diagnostics 注册的数据源不缺行）。
    const std::vector<DiagnosticEntry> entries = selectionCodeEntries();
    ASSERT_GE(entries.size(), std::size_t{4});
    EXPECT_EQ(entries.back().code, kSelLinearPowerInsufficient);
    EXPECT_EQ(entries[entries.size() - 2].code, kSelLinearSpeedInsufficient);
    EXPECT_EQ(entries[entries.size() - 3].code, kSelLinearForcePeakInsufficient);
    EXPECT_EQ(entries[entries.size() - 4].code, kSelLinearForceContinuousInsufficient);
}

/**
 * 四类直线传动器件词表封闭（§17.2——滚珠丝杠/齿条/同步带/直线电机）：
 * 词表文本逐字钉扎＋两两不同＋kebab 词形＋越界防御（词表规模 4）。
 */
TEST(SelLinearDriveContract, LinearDriveKindVocabularyClosed_WP19T12_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09-S1"},
                  std::vector<std::string>{"AT-36"});  // R2 选型层——器件词表
    EXPECT_EQ(kLinearDriveKindCount, 4);
    std::set<std::string> seen;
    for (int i = 0; i < kLinearDriveKindCount; ++i) {
        const std::string_view text =
            linearDriveKindText(static_cast<LinearDriveKind>(i));
        ASSERT_FALSE(text.empty()) << "类别 " << i << " 文本为空";
        ASSERT_TRUE(seen.insert(std::string(text)).second)
            << "类别文本重复（词表封闭性破坏）：" << text;
        // kebab 词形（小写字母开头；小写/数字/'-'；不以 '-' 结尾）。
        ASSERT_TRUE(std::islower(static_cast<unsigned char>(text.front()))) << text;
        ASSERT_NE(text.back(), '-') << text;
    }
    // 词面钉扎（REQUIREMENTS SEL-09-S1 行四类器件的物化值——目录 CSV
    // drive_kind 列合法值）。
    EXPECT_EQ(linearDriveKindText(LinearDriveKind::BallScrew), "ball-screw");
    EXPECT_EQ(linearDriveKindText(LinearDriveKind::RackPinion), "rack-pinion");
    EXPECT_EQ(linearDriveKindText(LinearDriveKind::TimingBelt), "timing-belt");
    EXPECT_EQ(linearDriveKindText(LinearDriveKind::LinearMotor), "linear-motor");
    // 越界防御（枚举外整数——不抛、返回占位文本）。
    EXPECT_EQ(linearDriveKindText(static_cast<LinearDriveKind>(kLinearDriveKindCount)),
              "unknown-linear-drive-kind");
}

/**
 * v2 schema 注册面钉扎（§5.2/§17.2）：格式版本常量、第六表文件名、
 * 直线 owner 词表值、曲线量纲词表扩展（linear-speed/load/force——单位
 * m/s、N、W）、P-IO-7 v2 注册面六文件序；v1 注册面返回值零变化
 * （五文件——既有装配消费方零漂移）。
 */
TEST(SelLinearDriveContract, V2SchemaRegistrationFace_WP19T12_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09-S1", "SEL-01", "SEL-02"},
                  std::vector<std::string>{"AT-36"});  // R2 选型层——注册面
    // 常量词面（目录模板契约——与单元卡 §17.2 登记值逐字一致）。
    EXPECT_EQ(std::string{kCatalogFormatVersion}, "1");
    EXPECT_EQ(std::string{kCatalogFormatVersionV2}, "2");
    EXPECT_EQ(std::string{kCatalogFileLinearDrives}, "linear_drives.csv");
    EXPECT_EQ(std::string{kCurveOwnerLinearDrive}, "linear-drive");
    EXPECT_EQ(std::string{kQuantityLinearSpeed}, "linear-speed");
    EXPECT_EQ(std::string{kQuantityLoad}, "load");
    EXPECT_EQ(std::string{kQuantityForce}, "force");
    // v2 注册面：v1 五文件序后表尾追加第六文件（role=linear-drives、必备）。
    const std::vector<ManifestEntry> v2 = catalogPackageFileSchemaV2();
    ASSERT_EQ(v2.size(), std::size_t{6});
    EXPECT_EQ(v2.back().fileName, std::string{kCatalogFileLinearDrives});
    EXPECT_EQ(v2.back().role, "linear-drives");
    EXPECT_TRUE(v2.back().required);
    // v1 注册面零变化（前五条与 v1 函数返回值逐条全等——表尾追加纪律）。
    const std::vector<ManifestEntry> v1 = catalogPackageFileSchema();
    ASSERT_EQ(v1.size(), std::size_t{5});
    for (std::size_t i = 0; i < v1.size(); ++i) {
        EXPECT_EQ(v2[i], v1[i]) << "v2 注册面前五条应与 v1 逐条全等（零漂移）";
    }
}

// =====================================================================
// 接口分派可达性（acceptance 2——经既有 IHardConstraintSelector 接口面
// 消费；不留只测实现的盲区）
// =====================================================================

/**
 * 经 IHardConstraintSelector 接口引用调用 screenLinearDrives（acceptance 2
 * "经既有评估器/组合校核接口面消费"的合同面）：可行事实走接口分派产出
 * Feasible 记录（deviceKind＝表尾追加的 LinearDrive；记录键＝候选×轴）；
 * 旋转通道（screenMotors/screenGearboxes）对同一接口的既有方法签名与
 * 行为零变化（接口扩展只允许表尾追加——动态多态经基类引用分派证明）。
 */
TEST(SelLinearDriveContract, LinearScreeningViaInterfaceDispatch_WP19T12_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09-S1", "SEL-05"},
                  std::vector<std::string>{"AT-36"});  // R2 选型层——接口面消费

    const CatalogPackageSnapshot snap = makeContractSnapshot();
    const core::ObjectId axis = core::ObjectId::generate();
    const std::vector<LinearAxisWorkpointFacts> facts{makeFeasibleFacts(axis)};

    // 经基类引用分派（接口路径钉扎——非 HardConstraintSelector 静态直调）。
    const IHardConstraintSelector& selector = HardConstraintSelector{};
    const std::vector<FeasibilityRecord> records =
        selector.screenLinearDrives(snap, facts, ScreeningCriteria{}, nullptr);
    ASSERT_EQ(records.size(), std::size_t{1});
    const FeasibilityRecord& rec = records.front();
    EXPECT_EQ(rec.deviceKind, DeviceKind::LinearDrive);
    EXPECT_EQ(rec.candidateModelId, "LD-CT-001");
    EXPECT_EQ(rec.verdict, VerdictKind::Feasible)
        << "可行事实经接口分派＝Feasible（恰边界速度在容差内）";
    EXPECT_TRUE(rec.reasons.empty());
    EXPECT_TRUE(rec.gaps.empty());

    // 致命输入快速拒绝（§10.2 校验边界——非有限事实 fail-fast）。
    std::vector<LinearAxisWorkpointFacts> badFacts{makeFeasibleFacts(axis)};
    badFacts.front().forcePeak = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(static_cast<void>(selector.screenLinearDrives(snap, badFacts,
                                                               ScreeningCriteria{}, nullptr)),
                 std::invalid_argument)
        << "非有限工作点＝调用方契约违约（NFR-COR-03 fail-fast）";
}
