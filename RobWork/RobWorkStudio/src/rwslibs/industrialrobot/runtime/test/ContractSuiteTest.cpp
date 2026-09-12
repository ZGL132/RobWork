/**
 * @file   ContractSuiteTest.cpp
 * @brief  RT-T12 runtime 契约套件——§11 验证矩阵在"注入替身×真实编译链"
 *         组合下的端到端用例体（AT-16/AT-18/AT-37 的 runtime 侧载体）＋
 *         黄金数据集消费面（rt-fk-equation／rt-namemap-roundtrip／档案 rt）。
 *
 * 设计依据：
 *   - units/runtime.md §11（验证矩阵逐行——用例名逐条对应；设施清单经
 *     RuntimeTestDoubles.hpp 的 §11 规范替身）、§12 RT-T12 行（契约套件＋
 *     数据集交付）、§6.4/§6.5（四消费方读取契约与 §6.5 数值例）、
 *     units/testkit.md §10.2（runtime 行：集合/数值断言、契约夹具——
 *     FK/编译链等价数据集〔analytic，附录 D 第 4 项容差〕＋名称往返）
 *   - 任务契约 tasks/foundation/RT-T12.json（acceptance 1～4 逐条对应具名
 *     用例或执行证据，见各 TEST 注释；RT-STUB-0 见本文件与 test/README.md）
 *
 * 替身边界（RT-STUB-0，全文声明见 runtime/test/README.md）：替身只覆盖
 * 注入接口（RuntimeTestDoubles.hpp 的 Scripted/Fake/Cancel 设施）；RobWork
 * 本体与被测编译器（CanonicalModelCompiler）/工厂（RuntimeSnapshotFactory）
 * 不做替身——替身输出仅验证 runtime 契约，不构成 RobWork 算法/业务算法
 * 正确性证明；canonical 侧手写 FK 是测试对照工具，不构成产品 FK 算法证明
 * （真值以 RobWork 为消费基线，等价断言只证明编译映射正确——§11 RT-EQ-1
 * 行原文）。
 *
 * 既有覆盖说明（§11 矩阵的分文件落位，test/README.md 有全表）：RT-NM-2/3/
 * 4/5/6/7、RT-CACHE-1～4、RT-SNAP-1～4、RT-CPX-1～4、RT-RES-1/2、RT-CONT-1/2、
 * RT-AD-1/2/3、RT-CPL-1、RT-BW-2/6、RT-ID 系模型层等已随 RT-T02～T11 的
 * 单元测试交付并留痕；本套件补齐其"跨单元组合/跨进程/数据集"缺口面：
 * RT-ID-1（跨进程）、RT-ID-2/3（缓存判定集成面）、RT-BW-1/3（快照视图面）、
 * RT-BW-4（消费端反例）、RT-BW-5（四消费方一致——AT-37 载体）、RT-CAP-1/2、
 * RT-RES-3、RT-EQ-1/RT-NM-1 的数据集载体。
 *
 * 集成模式专用（TARGET sdurw_kinematics 条件增列）——被测面链接真实框架库；
 * 冒烟模式本文件不进构建（与产品侧 Snapshot.cpp 等同因，§15.4 既有登记）。
 */

#include "CompilerImpl.hpp"        // 被测：产品编译器（单元私有头——测试与 src 同权）
#include "RuntimeTestDoubles.hpp"  // §11 规范替身（Scripted 与 Fake 前缀＋CancelToggle）

#include <sdurws/ird/core/Compare.hpp>                // closeWithin（C4 公式——RT-ID-2 前置）
#include <sdurws/ird/runtime/Adapter.hpp>             // WorkCellConstView
#include <sdurws/ird/runtime/BaseWorldTransform.hpp>  // checkBaseMountConsistency（消费端一致性检查）
#include <sdurws/ird/runtime/CacheKey.hpp>            // RT-ID-2 缓存判定面
#include <sdurws/ird/runtime/CanonicalModel.hpp>
#include <sdurws/ird/runtime/Compiler.hpp>
#include <sdurws/ird/runtime/Errors.hpp>
#include <sdurws/ird/runtime/NameMap.hpp>
#include <sdurws/ird/runtime/Snapshot.hpp>

#include <sdurws/ird/testkit/Dataset.hpp>           // GoldenDataset（数据集装载＋完整性）
#include <sdurws/ird/testkit/Fixture.hpp>           // TempDir（子进程产物落盘）
#include <sdurws/ird/testkit/JsonLite.hpp>          // 数据集 JSON 读取（测试侧受限 JSON）
#include <sdurws/ird/testkit/TestPaths.hpp>         // toleranceProfileDir
#include <sdurws/ird/testkit/ToleranceProfile.hpp>  // 档案 rt（附录 D 第 4/5/12 项）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_EXPECT_CLOSE/IDENTICAL/TEST_INFO

#include <rw/kinematics/Kinematics.hpp>   // frameTframe（真实基线 FK 读取）
#include <rw/kinematics/State.hpp>
#include <rw/math/Q.hpp>
#include <rw/models/SerialDevice.hpp>
#include <rw/models/WorkCell.hpp>

#include <gtest/gtest.h>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>  // GetModuleFileNameA（子进程再入本测试可执行文件）
#endif

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird::runtime;
namespace td = sdurws::ird::runtime::testdoubles;
namespace tk = sdurws::ird::testkit;
namespace core = sdurws::ird::core;

// IRD_EXPECT_* 宏按 testkit.md §5.3.3 以未限定名展开 check 函数——本 TU
// 在匿名命名空间内使用，须显式引入（宏展开的普通名字查找落到本作用域）。
using tk::checkAllCloseWithin;
using tk::checkAtMost;
using tk::checkCloseWithin;
using tk::checkIdentical;

// =====================================================================
// 共享助手。
// =====================================================================

/// 编译夹具（provider 可空＝无资源可读——§10.0 注入可空语义）。
CompileOutcome compileHarness(td::ContractHarness& h, td::FakeResourceProvider* provider)
{
    td::ScriptedReader reader(h.description);
    td::ScriptedObjectSource objects = h.objectSource();
    td::ScriptedClosureSource closure = h.closureSource();
    CompileRequest req = h.makeRequest(reader, objects, closure);
    req.resources = provider;

    CanonicalModelCompiler compiler;
    return compiler.compile(req);
}

/// 编译夹具至已发布快照（Published 断言收敛点——失败即用例失败）。
std::shared_ptr<const RuntimeSnapshot> compilePublished(td::ContractHarness& h,
                                                        td::FakeResourceProvider* provider
                                                        = nullptr)
{
    const CompileOutcome out = compileHarness(h, provider);
    EXPECT_EQ(out.status, CompileStatus::Published)
        << "夹具应发布快照（诊断："
        << (out.diagnostics.empty() ? std::string{} : out.diagnostics.front().cause) << "）";
    return out.snapshot;
}

/// 装载容差档案 rt（testkit 两级数据根解析——testdata/tolerance/rt-runtime/v1.0.0.json；
/// 档案目录名＝profileId"rt-runtime"，"rt"只是单元卡 §11 行的助记名）。
tk::ToleranceProfile loadRtProfile()
{
    return tk::ToleranceProfile::load(tk::toleranceProfileDir("rt-runtime") / "v1.0.0.json");
}

/// 读取文件全文（数据集 JSON 输入；读失败显性失败——不静默跳过）。
std::string readFile(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取: " << p.string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// JsonLite 取串/取数（数据集域内 schema——缺字段即测试失败后返回缺省）。
std::string jStr(const tk::JsonValue& o, const std::string& k)
{
    const tk::JsonValue* v = o.find(k);
    if (v == nullptr) {
        ADD_FAILURE() << "JSON 缺字段: " << k;
        return {};
    }
    return v->text;
}
double jNum(const tk::JsonValue& o, const std::string& k)
{
    const tk::JsonValue* v = o.find(k);
    if (v == nullptr) {
        ADD_FAILURE() << "JSON 缺字段: " << k;
        return 0.0;
    }
    return v->number;
}

/// 诊断集中是否含指定稳定码（registryCode 单点比对——PA-1）。
bool hasDiagnostic(const std::vector<core::DiagnosticRecord>& diags, const std::string& code)
{
    for (const core::DiagnosticRecord& r : diags) {
        if (r.code == code) {
            return true;
        }
    }
    return false;
}

/// 诊断集中指定稳定码的出现次数（RT-CAP-1 的警告计数面）。
int countDiagnostic(const std::vector<core::DiagnosticRecord>& diags, const std::string& code)
{
    int n = 0;
    for (const core::DiagnosticRecord& r : diags) {
        if (r.code == code) {
            ++n;
        }
    }
    return n;
}

// ---- canonical 侧手写 FK（§11 RT-EQ-1"测试内独立小实现，仅测试用"；
//      与 WorkCellCompilerTest 同款数学，逐元素独立实现、不用 rw 算子——
//      保持对照实现与产品/基线的独立性）。----

/// 位姿对（平移 m＋旋转；读法 core §4.6）。
struct Pose
{
    rw::math::Vector3D<double> p{0.0, 0.0, 0.0};
    rw::math::Rotation3D<double> R = detail::identityTransform3D().R();
};

/// 位姿复合 T_ac＝T_ab·T_bc（逐元素独立实现）。
Pose composePose(const Pose& a, const Pose& b)
{
    Pose out;
    for (int r = 0; r < 3; ++r) {
        out.p[r] = a.p[r]
            + a.R(r, 0) * b.p[0] + a.R(r, 1) * b.p[1] + a.R(r, 2) * b.p[2];
        for (int c = 0; c < 3; ++c) {
            out.R(r, c) = a.R(r, 0) * b.R(0, c) + a.R(r, 1) * b.R(1, c)
                        + a.R(r, 2) * b.R(2, c);
        }
    }
    return out;
}

/// 测试内独立 Rodrigues（绕单位轴 axis 转 angle，单位 rad）。
rw::math::Rotation3D<double> canonicalRodrigues(const rw::math::Vector3D<double>& axis,
                                                double angle)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double t = 1.0 - c;
    const double x = axis[0], y = axis[1], z = axis[2];
    return rw::math::Rotation3D<double>(
        c + x * x * t,      x * y * t - z * s,  x * z * t + y * s,
        y * x * t + z * s,  c + y * y * t,      y * z * t - x * s,
        z * x * t - y * s,  z * y * t + x * s,  c + z * z * t);
}

/// 3×3 旋转 × 向量（测试内独立实现）。
rw::math::Vector3D<double> rotVec(const rw::math::Rotation3D<double>& R,
                                  const rw::math::Vector3D<double>& v)
{
    return rw::math::Vector3D<double>(
        R(0, 0) * v[0] + R(0, 1) * v[1] + R(0, 2) * v[2],
        R(1, 0) * v[0] + R(1, 1) * v[1] + R(1, 2) * v[2],
        R(2, 0) * v[0] + R(2, 1) * v[1] + R(2, 2) * v[2]);
}

// =====================================================================
// RT-ID-1（ARC-03/AT-16 载体）：重复编译＋跨进程身份稳定。
// 父用例在本进程编译夹具并派生子进程（本测试可执行文件经 gtest 过滤器
// 再入 Worker 用例）编译同夹具；两侧模型/快照/映射身份与 WC 结构逐字节
// 相等。夹具 id 全由固定种子派生（RuntimeTestDoubles 纪律）——跨进程
// 字节一致是"确定性编译"（ARC-03/NFR-COR-02）的进程级观测。
// =====================================================================

/// 子进程产出文件行结构：3 行身份规范文本＋帧计数行＋帧名（已排序）。
struct IdentityFingerprint
{
    std::string modelIdentity;        ///< "cid-<hex>"（模型内容身份）
    std::string snapshotIdentity;     ///< "cid-<hex>"（快照身份）
    std::string nameMapIdentity;      ///< "cid-<hex>"（映射内容身份）
    std::vector<std::string> wcNames; ///< WC 全部 Frame 名（已排序——"同名同构"名集合）
};

/// 计算当前进程内的指纹（父子两侧共用同一实现——同夹具同函数）。
IdentityFingerprint fingerprintOfProcess()
{
    td::ContractHarness h = td::ContractHarness::make(td::ContractHarness::Physics::Full);
    const std::shared_ptr<const RuntimeSnapshot> snap = compilePublished(h);

    IdentityFingerprint fp;
    if (snap == nullptr) {
        ADD_FAILURE() << "指纹夹具应发布快照";
        return fp;
    }
    fp.modelIdentity = snap->modelIdentity().toCanonical();
    fp.snapshotIdentity = snap->snapshotIdentity().toCanonical();
    fp.nameMapIdentity = snap->nameMapIdentity().toCanonical();

    // WC 结构面：全部 Frame 名（含 WORLD）——"WC 结构同名同构"的名集合
    // 观测（精确等值域——附录 D 第 12 项：名称无容差可言）。
    const WorkCellConstView& view = snap->workCell();
    for (const auto& frame : view.workCell().getFrames()) {
        fp.wcNames.push_back(frame->getName());
    }
    std::sort(fp.wcNames.begin(), fp.wcNames.end());
    return fp;
}

/// Worker 用例：仅在父进程设置 IRD_RT_CONTRACT_CHILD_OUT 时执行（否则跳过
/// ——常规全量运行中本用例是空转占位，不产生噪音）。
TEST(ContractSuite, ChildProcessWorker_RT_ID_1)
{
    const char* outPath = std::getenv("IRD_RT_CONTRACT_CHILD_OUT");
    if (outPath == nullptr) {
        GTEST_SKIP() << "非子进程模式（父用例 CrossProcessIdentityStable 派生时才执行）";
    }
    const IdentityFingerprint fp = fingerprintOfProcess();
    std::ofstream out(outPath, std::ios::binary);
    ASSERT_TRUE(static_cast<bool>(out)) << "子进程无法写指纹文件: " << outPath;
    out << fp.modelIdentity << "\n" << fp.snapshotIdentity << "\n" << fp.nameMapIdentity << "\n";
    out << fp.wcNames.size() << "\n";
    for (const std::string& n : fp.wcNames) {
        out << n << "\n";
    }
}

TEST(ContractSuite, CrossProcessIdentityStable_RT_ID_1)
{
    IRD_TEST_INFO("ARC-03", {"AT-16"}, std::nullopt, "rt-runtime@1.0.0");
    // 本进程指纹（同夹具同函数——与子进程唯一差异是进程边界）。
    const IdentityFingerprint parent = fingerprintOfProcess();

    // 定位本测试可执行文件（子进程＝同一可执行文件经 gtest 过滤器再入）。
    char exeBuf[MAX_PATH] = {0};
    ASSERT_NE(GetModuleFileNameA(nullptr, exeBuf, MAX_PATH), 0u)
        << "无法定位测试可执行文件";
    const std::string exe = exeBuf;

    // 子进程产物落 TempDir（testkit §6.2——测试写盘唯一合法位置）。
    tk::TempDir dir("rt-contract");
    const std::string childOut = (dir.path() / "child-identity.txt").string();
    const std::string childLog = (dir.path() / "child-stdout.txt").string();

#ifdef _MSC_VER
    (void)_putenv_s("IRD_RT_CONTRACT_CHILD_OUT", childOut.c_str());
#else
    setenv("IRD_RT_CONTRACT_CHILD_OUT", childOut.c_str(), 1);
#endif

    // 子进程命令行（经 std::system→cmd /c）：命令以引号开头且含重定向等
    // 特殊字符时，cmd 会剥离首尾引号——按 cmd 规则给整条命令再加一层
    // 外引号（内层引号得以保留，可执行文件路径与重定向目标都带引号）。
    const std::string inner = "\"" + exe
        + "\" --gtest_filter=ContractSuite.ChildProcessWorker_RT_ID_1 > \"" + childLog
        + "\" 2>&1";
    const int rc = std::system(("\"" + inner + "\"").c_str());
    ASSERT_EQ(rc, 0) << "子进程运行失败（exit=" << rc << "；命令：" << inner
                     << "；日志：" << childLog << "）";

#ifdef _MSC_VER
    (void)_putenv_s("IRD_RT_CONTRACT_CHILD_OUT", "");
#else
    unsetenv("IRD_RT_CONTRACT_CHILD_OUT");
#endif

    // 读取子进程指纹并逐行对照（身份/名称是精确等值域——IRD_EXPECT_IDENTICAL，
    // 附录 D 第 12 项：无容差）。
    const std::string text = readFile(childOut);
    std::istringstream lines(text);
    std::string modelLine, snapLine, mapLine, countLine;
    ASSERT_TRUE(static_cast<bool>(std::getline(lines, modelLine)));
    ASSERT_TRUE(static_cast<bool>(std::getline(lines, snapLine)));
    ASSERT_TRUE(static_cast<bool>(std::getline(lines, mapLine)));
    ASSERT_TRUE(static_cast<bool>(std::getline(lines, countLine)));
    IRD_EXPECT_IDENTICAL("cross-process.modelIdentity", parent.modelIdentity, modelLine);
    IRD_EXPECT_IDENTICAL("cross-process.snapshotIdentity", parent.snapshotIdentity, snapLine);
    IRD_EXPECT_IDENTICAL("cross-process.nameMapIdentity", parent.nameMapIdentity, mapLine);

    const int childNameCount = std::atoi(countLine.c_str());
    ASSERT_EQ(childNameCount, static_cast<int>(parent.wcNames.size()))
        << "子进程 WC 帧数与本进程不一致";
    for (int i = 0; i < childNameCount; ++i) {
        std::string name;
        ASSERT_TRUE(static_cast<bool>(std::getline(lines, name))) << "子进程指纹行缺失@" << i;
        IRD_EXPECT_IDENTICAL("cross-process.wcNames[" + std::to_string(i) + "]",
                             parent.wcNames[static_cast<std::size_t>(i)], name);
    }
}

// =====================================================================
// RT-ID-2（§4.3.6/附录 D C4 集成面）：1×10⁻¹⁵ 的浮点差——容差语义判等、
// 身份位模式判不等、缓存判定 Incompatible（reasons 含 model-changed）。
// =====================================================================

TEST(ContractSuite, TinyFloatDeltaSplitsIdentityAndCache_RT_ID_2)
{
    IRD_TEST_INFO("ARC-03", {"AT-16"}, std::nullopt, "rt-runtime@1.0.0");
    td::ContractHarness h1 = td::ContractHarness::make(td::ContractHarness::Physics::Full);
    td::ContractHarness h2 = td::ContractHarness::make(td::ContractHarness::Physics::Full);
    // 唯一差异：joint_1 限速 3.0 → 3.0＋1×10⁻¹⁵（rad/s；3.0 的 ULP≈4.4×10⁻¹⁶
    // ——差值跨 2 ULP，位模式必然不同）。容差语义下二者"近似等价"
    // （1×10⁻¹⁵ ≪ 1×10⁻⁹）——正是 §11 RT-ID-2 要否定的等价关系。
    h2.description.joints.at(0).maxVelocity = td::val(3.0 + 1e-15);
    // 前置钉住：该差值在容差语义下判等（core closeWithin——C4 公式）。
    const core::Tolerance loose = core::Tolerance::make(0.0, 1e-9);
    ASSERT_TRUE(core::closeWithin(3.0, 3.0 + 1e-15, loose))
        << "前置失效：1e-15 差值应在容差语义下判等（否则本用例失去靶心）";

    const std::shared_ptr<const RuntimeSnapshot> s1 = compilePublished(h1);
    const std::shared_ptr<const RuntimeSnapshot> s2 = compilePublished(h2);
    ASSERT_NE(s1, nullptr);
    ASSERT_NE(s2, nullptr);

    // 身份不等（位模式——§4.3.6"字节等值是唯一等价关系"）。
    EXPECT_FALSE(s1->modelIdentity() == s2->modelIdentity())
        << "1e-15 浮点差必须改变模型身份（位模式编码，无浮点近似等价）";
    EXPECT_FALSE(s1->snapshotIdentity() == s2->snapshotIdentity());

    // 缓存判定面（§9.4）：judge＝Incompatible 且 reasons 精确含
    // model-changed（分量指纹表——内容类变化全部落在 model-changed）。
    CanonicalModelCompiler compiler;
    RuntimeCompileCacheKeyBuilder builder(compiler.contractVersion(),
                                          compiler.implementationVersion());
    const CompileOptions opts;
    const CompileCacheCompatibility judge = judgeCompileCacheCompatibility(
        builder.buildKey(s2->model(), opts), builder.buildKey(s1->model(), opts));
    EXPECT_EQ(judge.verdict, CompileCacheCompatibility::Verdict::Incompatible);
    bool hasModelChanged = false;
    for (const std::string& r : judge.reasons) {
        if (r == "model-changed") {
            hasModelChanged = true;
        }
    }
    EXPECT_TRUE(hasModelChanged)
        << "reasons 应含 model-changed（实得首条："
        << (judge.reasons.empty() ? std::string{"<空>"} : judge.reasons.front()) << "）";
}

// =====================================================================
// RT-ID-3（CON-05/AT-27 集成面）：展示无关性——项目显示名/会话重开不进
// runtime 输入面（类型层：CanonicalModelHeader/CompileRequest 无显示名
// 字段——§4.3.1），"同内容重编译"即其 runtime 侧载体：身份不变＋缓存
// 判定 FullReuse（无失效类 reasons）。
// =====================================================================

TEST(ContractSuite, DisplayFieldsNeverEnterRuntimeIdentity_RT_ID_3)
{
    IRD_TEST_INFO("CON-05", {"AT-27"}, std::nullopt, "rt-runtime@1.0.0");
    td::ContractHarness h = td::ContractHarness::make(td::ContractHarness::Physics::Full);
    const std::shared_ptr<const RuntimeSnapshot> a = compilePublished(h);
    const std::shared_ptr<const RuntimeSnapshot> b = compilePublished(h); // "重开会话"重编译
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    // 身份不变（模型/快照/映射三面）。
    EXPECT_TRUE(a->modelIdentity() == b->modelIdentity());
    EXPECT_TRUE(a->snapshotIdentity() == b->snapshotIdentity());
    EXPECT_TRUE(a->nameMapIdentity() == b->nameMapIdentity());

    // 无失效类 reasons：判定＝FullReuse、reasons 空（§9.4 FullReuse 行——
    // 展示/会话类语义不在键内，CON-05/CON-06）。
    CanonicalModelCompiler compiler;
    RuntimeCompileCacheKeyBuilder builder(compiler.contractVersion(),
                                          compiler.implementationVersion());
    const CompileCacheCompatibility judge = judgeCompileCacheCompatibility(
        builder.buildKey(b->model(), CompileOptions{}),
        builder.buildKey(a->model(), CompileOptions{}));
    EXPECT_EQ(judge.verdict, CompileCacheCompatibility::Verdict::FullReuse);
    EXPECT_TRUE(judge.reasons.empty())
        << "无失效类 reasons（实得："
        << (judge.reasons.empty() ? std::string{"<空>"} : judge.reasons.front()) << "）";
}

// =====================================================================
// RT-BW-1（MDL-22/AT-37 载体面）：§6.5 数值例经快照视图（worldToBase/
// baseToWorld/gravityBase）逐元素符合手算——容差经档案 rt（附录 D 第 4 项）。
// =====================================================================

TEST(ContractSuite, NumericExampleThroughSnapshotView_RT_BW_1)
{
    IRD_TEST_INFO("MDL-22", {"AT-37"}, std::nullopt, "rt-runtime@1.0.0");
    const tk::ToleranceProfile profile = loadRtProfile();

    // 倒挂机型：R_world_base＝R_x(π)＝diag(1,−1,−1)、t＝(0,0,2) m（§6.5）。
    td::ContractHarness h = td::ContractHarness::make(td::ContractHarness::Physics::Full);
    h.description.base.preset = InstallationPresetToken::Inverted;
    h.description.base.basePosition = rw::math::Vector3D<double>(0.0, 0.0, 2.0);
    const std::shared_ptr<const RuntimeSnapshot> snap = compilePublished(h);
    ASSERT_NE(snap, nullptr);

    const rw::math::Transform3D<double> tWb = snap->worldToBase();

    // ① 点变换：p_base=(0.3,0.2,0.5) m 经 R·p_base＋t 映射到世界——
    //    p_world=(0.3,−0.2,1.5) m（§6.5 ①，Z 分量反号）。
    const rw::math::Vector3D<double> pBase(0.3, 0.2, 0.5);
    const rw::math::Vector3D<double> pWorld = rotVec(tWb.R(), pBase) + tWb.P();
    IRD_EXPECT_CLOSE("base-world.point.x", pWorld[0], 0.3, profile, "m");
    IRD_EXPECT_CLOSE("base-world.point.y", pWorld[1], -0.2, profile, "m");
    IRD_EXPECT_CLOSE("base-world.point.z", pWorld[2], 1.5, profile, "m");

    // ② 反变换：baseToWorld 的平移＝−Rᵀt＝(0,0,+2) m（§6.5 ②修正值）；
    //    把 pWorld 映射回基座系应逐位回到 pBase。
    const rw::math::Transform3D<double> tBw = snap->baseToWorld();
    EXPECT_DOUBLE_EQ(tBw.P()(0), 0.0);
    EXPECT_DOUBLE_EQ(tBw.P()(1), 0.0);
    EXPECT_DOUBLE_EQ(tBw.P()(2), 2.0) << "反解平移＝−Rᵀt＝(0,0,+2)（§6.5 ②）";
    const rw::math::Vector3D<double> pBaseBack = rotVec(tBw.R(), pWorld) + tBw.P();
    IRD_EXPECT_CLOSE("base-world.inverse.x", pBaseBack[0], 0.3, profile, "m");
    IRD_EXPECT_CLOSE("base-world.inverse.y", pBaseBack[1], 0.2, profile, "m");
    IRD_EXPECT_CLOSE("base-world.inverse.z", pBaseBack[2], 0.5, profile, "m");

    // ③ 重力投影：g_base＝Rᵀ·g_world＝(0,0,+9.81) m/s²（§6.5 ③——基座 Z
    //    指向世界 −Z；视图 gravityBase 为唯一读取点，§6.3/§6.4）。
    const rw::math::Vector3D<double> gBase = snap->gravityBase();
    IRD_EXPECT_CLOSE("base-world.gravity.x", gBase[0], 0.0, profile, "m/s^2");
    IRD_EXPECT_CLOSE("base-world.gravity.y", gBase[1], 0.0, profile, "m/s^2");
    IRD_EXPECT_CLOSE("base-world.gravity.z", gBase[2], 9.81, profile, "m/s^2");
    // 世界系重力恒定（视图值＝规范默认 (0,0,−9.81) m/s²——§4.3.2）。
    EXPECT_DOUBLE_EQ(snap->gravityWorld()[2], -9.81);
}

// =====================================================================
// RT-BW-3（MDL-22/V15-04 快照面）：Description 未配置 base——编译发布、
// T_world_base 精确恒等（元素 ∈{0,1}）、安装预设语义＝Ground、无 error 诊断。
// =====================================================================

TEST(ContractSuite, DefaultBaseCompilesToIdentityTransform_RT_BW_3)
{
    IRD_TEST_INFO("MDL-22", {"AT-37"}, std::nullopt, "rt-runtime@1.0.0");
    td::ContractHarness h = td::ContractHarness::make(td::ContractHarness::Physics::None);
    // "未配置 base"的 Description 值域形态：preset 保持缺省枚举值 Ground
    // （V15-04——缺省即地面）、basePosition 保持 (0,0,0) m——harness 构造
    // 即此形态，本用例不改写任何 base 字段。
    const std::shared_ptr<const RuntimeSnapshot> snap = compilePublished(h);
    ASSERT_NE(snap, nullptr);

    const rw::math::Transform3D<double> t = snap->worldToBase();
    const rw::math::Rotation3D<double> identityR = detail::identityTransform3D().R();
    for (int r = 0; r < 3; ++r) {
        EXPECT_EQ(t.P()(r), 0.0) << "平移分量 " << r << " 应精确为 0 m";
        for (int c = 0; c < 3; ++c) {
            EXPECT_EQ(t.R()(r, c), identityR(r, c))
                << "旋转元素 (" << r << "," << c << ") 应精确为 {0,1}";
        }
    }
    EXPECT_EQ(snap->model().world().presetToken(), InstallationPresetToken::Ground)
        << "未显式配置＝按 Ground 解释（V15-04）";
    // 无 error 级诊断（发布门禁——§9.6 组合表；能力缺失仅警告级）。
    for (const core::DiagnosticRecord& rec : snap->diagnostics()) {
        EXPECT_NE(rec.code, std::string{registryCode(RuntimeErrorCode::InputInvalid)})
            << "默认地面编译不得产 error 级诊断";
    }
}

// =====================================================================
// RT-BW-4（AT-37 反例·消费端）：替身消费方把 worldToBase() 又乘一次安装
// 旋转——其自检（checkBaseMountConsistency，S9 规则的消费端镜像）检出
// ≈T·T 形态，等式 worldToBase()==快照字段被判定失败，反例映射
// BaseWorldInconsistent；正确消费方零偏差。
// =====================================================================

TEST(ContractSuite, ConsumerDoubleApplicationIntercepted_RT_BW_4)
{
    IRD_TEST_INFO("MDL-22", {"AT-37"}, std::nullopt, "rt-runtime@1.0.0");
    td::ContractHarness h = td::ContractHarness::make(td::ContractHarness::Physics::Full);
    h.description.base.preset = InstallationPresetToken::Inverted;
    h.description.base.basePosition = rw::math::Vector3D<double>(0.0, 0.0, 2.0);
    const std::shared_ptr<const RuntimeSnapshot> snap = compilePublished(h);
    ASSERT_NE(snap, nullptr);

    // 消费端契约面（§6.4/§6.6 AT-37 观测点前半）：视图 worldToBase() 与
    // 快照 T_world_base 逐元素一致——唯一读取点（§6.3）。
    const rw::math::Transform3D<double> fromView = snap->worldToBase();
    const rw::math::Transform3D<double>& fromField = snap->model().world().T_world_base;
    for (int r = 0; r < 3; ++r) {
        EXPECT_EQ(fromView.P()(r), fromField.P()(r)) << "视图/字段平移分量 " << r;
        for (int c = 0; c < 3; ++c) {
            EXPECT_EQ(fromView.R()(r, c), fromField.R()(r, c))
                << "视图/字段旋转元素 (" << r << "," << c << ")";
        }
    }

    // 正确消费方：候选＝视图值——自检零偏差（不误判）。
    EXPECT_FALSE(checkBaseMountConsistency(fromView, fromField).has_value())
        << "正确消费方的候选不得被一致性检查拦截";

    // 缺陷消费方（AT-37 反例）：把安装变换又乘一次（R²＝I——渲染回正、
    // 重力矩符号翻转的假象）；自检必须检出且呈 T² 形态→按消费侧映射
    // BaseWorldInconsistent 处置（实现缺陷类失败，不得放行——§6.6）。
    const rw::math::Transform3D<double> doubleApplied = fromView * fromView;
    const std::optional<BaseMountDeviation> dev
        = checkBaseMountConsistency(doubleApplied, fromField);
    ASSERT_TRUE(dev.has_value()) << "二次叠加必须被消费端自检拦截（AT-37 反例）";
    EXPECT_TRUE(dev->doubleAppliedPattern) << "偏差应呈 ≈T·T 形态";
    // 偏差比较字段（§11 RT-BW-4 观测点：实际值/期望值/单位——实际＝候选与
    // 权威的逐元素最大偏差，期望＝容差内零偏差；单位 m / rad，第 4 项）。
    EXPECT_GT(dev->maxPositionDeviation, 1e-9) << "平移偏差应超容差（单位 m）";
    EXPECT_GT(dev->maxRotationDeviation, 1e-9) << "旋转偏差应超容差（单位 rad）";
    // 消费侧的码面映射：S9 失败码（registryCode 单点——PA-1）。
    EXPECT_STREQ(registryCode(RuntimeErrorCode::BaseWorldInconsistent).data(),
                 "RT-BASE-WORLD-INCONSISTENT");
}

// =====================================================================
// RT-BW-5（AT-37 主载体）：倒挂机型——kinematics/trajectory/dynamics/policy
// 四消费替身各取 IRuntimeModelView，按 §6.4 各自读取路径计算 FK/重力/
// 场景位姿，四路结果与 T_world_base·FK_device／Rᵀ·g 全等（附录 D 第 4 项）。
// =====================================================================

namespace fourconsumer {

/// 消费替身 1/4——kinematics：世界系设备端位姿＝T_world_base·T_base_end
/// （基座系复合路径；§6.4 行 1——FK/IK 在设备链上表达后经安装变换入世界）。
rw::math::Transform3D<double> kinematicsFk(const RuntimeSnapshot& snap,
                                           const rw::models::SerialDevice& device,
                                           const rw::kinematics::State& state)
{
    const rw::math::Transform3D<double> tWb = snap.worldToBase();
    const rw::math::Transform3D<double> tBe = device.baseTend(state);
    return rw::math::Transform3D<double>(tWb.P() + rotVec(tWb.R(), tBe.P()),
                                         tWb.R() * tBe.R());
}

/// 消费替身 2/4——trajectory：同一端位姿经世界系直读（worldTframe 路径；
/// §6.4 行 2——路径点/连续变换在 T_world_base 之后的设备链上表达）。
rw::math::Transform3D<double> trajectoryFk(const RuntimeSnapshot& snap,
                                           const rw::models::SerialDevice& device,
                                           const rw::kinematics::State& state)
{
    const rw::kinematics::Frame* const world = snap.workCell().workCell().getWorldFrame();
    return rw::kinematics::Kinematics::frameTframe(world, device.getEnd(), state);
}

/// 消费替身 3/4——dynamics：基座系重力经 gravityBase()（Rᵀ·g 投影公式
/// 单点；§6.4 行 3——DYN-01 重力投影）。
rw::math::Vector3D<double> dynamicsGravity(const RuntimeSnapshot& snap)
{
    return snap.gravityBase();
}

/// 消费替身 4/4——policy：世界系场景固连位姿直读（环境几何不随安装姿态
/// 旋转——§6.4 禁止清单 3；碰撞评估共享同一 WC）。
rw::math::Transform3D<double> policyScenePose(const RuntimeSnapshot& snap,
                                              const std::string& sceneFrameName,
                                              const rw::kinematics::State& state)
{
    const rw::kinematics::Frame* const f = snap.workCell().findFrame(sceneFrameName);
    EXPECT_NE(f, nullptr) << "场景 Frame 缺失: " << sceneFrameName;
    const rw::math::Transform3D<double> t = rw::kinematics::Kinematics::worldTframe(f, state);
    return t;
}

/// 场景对象加进夹具（世界系固连；几何引用命中清单——S5 资源引用约束）。
void addSceneObject(td::ContractHarness& h, std::vector<std::uint8_t>& meshOut)
{
    const std::vector<std::uint8_t> mesh{'s', 'c', 'e', 'n', 'e'};
    ResourceRef ref;
    ref.resourceId = h.res1;
    ref.contentDigest = td::digestBytes(mesh);  // 内容摘要入身份（§8.6）
    ref.state = ResourceState::Recorded;
    ref.accessVersion = 1;
    h.description.resourceRefs.push_back(ref);

    SceneObjectDescription so;
    so.objectId = td::idFrom<core::ObjectId>("rt-suite-scene1");
    so.localName = "obstacle_1";
    so.worldPose = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(1.0, -0.5, 0.8),  // 世界系坐标，单位 m
        detail::identityTransform3D().R());
    so.geometry.resource = ref;  // GeometryRef 包装（§4.2——Description 侧形态）
    h.description.scene.push_back(so);

    // 闭包登记场景对象（字节/摘要一致——S2 完整性复核的申报值形态）。
    h.store.items.push_back(td::ScriptedObjectLibrary::Item{
        so.objectId, std::string{"scene"}, td::cvFrom("rt-suite-scene1-cv"),
        td::digestBytes(mesh), mesh});
    meshOut = mesh;
}

}  // namespace fourconsumer

TEST(ContractSuite, FourConsumersConsistentOnInvertedModel_RT_BW_5)
{
    using namespace fourconsumer;
    IRD_TEST_INFO("MDL-22", {"AT-37"}, std::nullopt, "rt-runtime@1.0.0");
    const double kTol = 1e-9;  // 附录 D 第 4 项（与档案 rt 同尺度——逐元素）。

    // 倒挂机型（安装分量非恒等——二次叠加/漏乘在这条路径上必现形）＋
    // 场景对象（policy 面）＋全物性（DWC 存在——dynamics 面有 DWC 可言）。
    td::ContractHarness h = td::ContractHarness::make(td::ContractHarness::Physics::Full);
    h.description.base.preset = InstallationPresetToken::Inverted;
    h.description.base.basePosition = rw::math::Vector3D<double>(0.0, 0.0, 2.0);
    std::vector<std::uint8_t> sceneMesh;
    addSceneObject(h, sceneMesh);

    // Recorded 资源需 provider 可读（S4 读取＋S10 前复查＝两次同内容调用）。
    td::FakeResourceProvider provider;
    provider.program(h.res1, {sceneMesh, sceneMesh});

    const std::shared_ptr<const RuntimeSnapshot> snap = compilePublished(h, &provider);
    ASSERT_NE(snap, nullptr);

    // 设备与线程私有 State（§8.7/§9.2——每消费替身各自 makeState）。
    const std::string deviceName = snap->nameMap().resolveObjectId(h.robot).get().fullName;
    const rw::core::Ptr<const rw::models::SerialDevice> device
        = snap->workCell().findDevice(deviceName);
    ASSERT_TRUE(!device.isNull()) << "设备应存在: " << deviceName;
    const rw::math::Q q(2, 0.4, -0.9);  // 单位 rad（确定性构型，限位内）

    // ---- kinematics 替身：基座系复合路径 ----
    rw::kinematics::State stateKin = snap->makeState();
    device->setQ(q, stateKin);
    const rw::math::Transform3D<double> fkKin = kinematicsFk(*snap, *device, stateKin);

    // ---- trajectory 替身：世界系直读路径 ----
    rw::kinematics::State stateTraj = snap->makeState();
    device->setQ(q, stateTraj);
    const rw::math::Transform3D<double> fkTraj = trajectoryFk(*snap, *device, stateTraj);

    // 两路 FK 结果互等（＝T_world_base·FK_device 的两种 §6.4 读取路径；
    // 任何安装分量漏乘/二次叠加都会在倒挂 R_x(π) 上破坏此等式）。
    for (int r = 0; r < 3; ++r) {
        EXPECT_NEAR(fkKin.P()(r), fkTraj.P()(r), kTol)
            << "kinematics/trajectory 端位姿平移分量 " << r << " 互等（AT-37）";
        for (int c = 0; c < 3; ++c) {
            EXPECT_NEAR(fkKin.R()(r, c), fkTraj.R()(r, c), kTol)
                << "kinematics/trajectory 端位姿旋转元素 (" << r << "," << c << ")";
        }
    }

    // ---- dynamics 替身：gravityBase()（Rᵀ·g 投影）与手算 Rᵀ·g_world 互等
    //      （Rᵀ 逐元素手算——不依赖 rw inverse；倒挂下 g_base 翻到 +Z）----
    const rw::math::Vector3D<double> gDyn = dynamicsGravity(*snap);
    const rw::math::Rotation3D<double> R = snap->worldToBase().R();
    const rw::math::Vector3D<double> gWorld = snap->gravityWorld();
    const rw::math::Vector3D<double> gManual(
        R(0, 0) * gWorld[0] + R(1, 0) * gWorld[1] + R(2, 0) * gWorld[2],
        R(0, 1) * gWorld[0] + R(1, 1) * gWorld[1] + R(2, 1) * gWorld[2],
        R(0, 2) * gWorld[0] + R(1, 2) * gWorld[1] + R(2, 2) * gWorld[2]);
    for (int r = 0; r < 3; ++r) {
        EXPECT_NEAR(gDyn[r], gManual[r], kTol) << "dynamics 重力投影分量 " << r << "（Rᵀ·g）";
    }
    EXPECT_NEAR(gDyn[2], 9.81, kTol) << "倒挂下基座系重力沿 +Z_base（§6.5 ③）";

    // ---- policy 替身：世界系场景位姿（不预乘安装旋转——与模型字段互等）----
    const std::string sceneName
        = snap->nameMap().resolveObjectId(td::idFrom<core::ObjectId>("rt-suite-scene1"))
              .get()
              .fullName;
    rw::kinematics::State statePol = snap->makeState();
    device->setQ(q, statePol);  // 场景位姿与关节位无关——同 State 纪律下读取
    const rw::math::Transform3D<double> scenePol
        = policyScenePose(*snap, sceneName, statePol);
    const CanonicalSceneObject& sceneModel = snap->model().scene().front();
    for (int r = 0; r < 3; ++r) {
        EXPECT_NEAR(scenePol.P()(r), sceneModel.worldPose.P()(r), kTol)
            << "policy 场景位姿平移分量 " << r << "（世界系固连，无安装分量）";
    }

    // ---- 四替身共享同一快照：view.worldToBase() 与快照字段逐元素一致
    //      （§6.6 AT-37 反例观测点的前半——一致是"任何二次叠加必失败"的
    //      判据基线；逐元素精确相等——同一计算单点的两次读取）----
    const rw::math::Transform3D<double> tView = snap->worldToBase();
    const rw::math::Transform3D<double>& tField = snap->model().world().T_world_base;
    for (int r = 0; r < 3; ++r) {
        EXPECT_EQ(tView.P()(r), tField.P()(r));
        for (int c = 0; c < 3; ++c) {
            EXPECT_EQ(tView.R()(r, c), tField.R()(r, c));
        }
    }
}

// =====================================================================
// RT-CAP-1（§9.6）：混合缺失输入（物性/摩擦/几何/工具）——各能力位正确、
// 警告恰一条（S7 门控降级的事件面）且 subject 定位链序首个缺失连杆、
// 发布不被阻断。
// =====================================================================

TEST(ContractSuite, MixedMissingInputsDeriveCorrectCapabilities_RT_CAP_1)
{
    IRD_TEST_INFO("MDL-06", {"AT-01"}, std::nullopt);
    td::ContractHarness h = td::ContractHarness::make(td::ContractHarness::Physics::Partial);
    // 混合缺失形态（harness Partial＋零摩擦＋零几何＋零工具/场景）：
    // 物性——link_1 缺 mass/com/inertia；摩擦——全关节未提供；几何——
    // 无任何 collision/scene；工具——空集。
    const std::shared_ptr<const RuntimeSnapshot> snap = compilePublished(h);
    ASSERT_NE(snap, nullptr) << "能力缺失是降级不是失败（§5.6）";

    const RuntimeCapability& cap = snap->capabilities();
    EXPECT_FALSE(cap.hasDynamicWorkCell) << "有连杆物性缺失→无 DWC（§9.6 派生）";
    EXPECT_FALSE(cap.hasFullMassInertia) << "物性不全→无全物性位";
    EXPECT_FALSE(cap.hasFrictionModel) << "摩擦未提供→无摩擦模型位";
    EXPECT_FALSE(cap.hasCollisionGeometry) << "无几何引用→无碰撞几何位";
    EXPECT_FALSE(cap.hasTools) << "工具空集→无工具位";
    EXPECT_FALSE(cap.hasScene);
    EXPECT_FALSE(cap.hasCouplingMatrix);
    EXPECT_TRUE(cap.hasJointVelocityLimits) << "全关节限速已提供→限速位为 true";
    EXPECT_TRUE(cap.hasWorkCell);
    EXPECT_TRUE(cap.hasBidirectionalNameMap);

    // 诊断清单：能力缺失警告＝S7 门控降级的事件面（§9.6"每个缺失能力一条
    // 警告"的已实现落点＝DWC 跳过事件恰一条），subject 定位链序首个物性
    // 缺失连杆（link_1——RT-CAP-1 观测点；其余缺失类别的能力事实由能力位
    // 承载——§9.6 派生规则，不产 error/额外警告）。
    EXPECT_EQ(countDiagnostic(snap->diagnostics(), "RT-CAPABILITY-MISSING"), 1)
        << "S7 门控降级应恰产一条能力缺失警告";
    bool subjectIsFirstMissing = false;
    for (const core::DiagnosticRecord& rec : snap->diagnostics()) {
        if (rec.code == "RT-CAPABILITY-MISSING" && rec.subject.has_value()
            && rec.subject->bytes == h.l1.bytes) {
            subjectIsFirstMissing = true;
        }
    }
    EXPECT_TRUE(subjectIsFirstMissing) << "警告 subject 应定位首个缺失连杆（link_1）";
    // 无 error 级码（降级非失败——与 RT-CPX-3 的 provided 非法严格区分）。
    EXPECT_FALSE(hasDiagnostic(snap->diagnostics(),
                               std::string{registryCode(RuntimeErrorCode::InputInvalid)}));
}

// =====================================================================
// RT-CAP-2（§5.6）：能力缺失不升级——快照交替身消费方按声明自行处置
// （dynamics 替身→DataInsufficient 域判定）；runtime 输出面无任何
// EngineeringStatus 类工程判定字段（类型层不存在——静态扫描＋行为断言）。
// =====================================================================

namespace cap2consumer {

/// dynamics 域替身的消费判定（域声明自带语义——runtime 不代判）。
enum class ConsumerVerdict { Proceed, DataInsufficient };

/// 按"重力 RNEA 需要 DWC"的域声明处置能力声明（DYN-06——判定归 dynamics）。
ConsumerVerdict consumeForGravityRnea(const RuntimeSnapshot& snap)
{
    if (!snap.capabilities().hasDynamicWorkCell) {
        return ConsumerVerdict::DataInsufficient;  // 域内判定——runtime 零参与
    }
    return ConsumerVerdict::Proceed;
}

}  // namespace cap2consumer

TEST(ContractSuite, MissingCapabilityNeverEscalates_RT_CAP_2)
{
    using namespace cap2consumer;
    IRD_TEST_INFO("MDL-06", {"AT-01"}, std::nullopt);
    td::ContractHarness h = td::ContractHarness::make(td::ContractHarness::Physics::None);
    const std::shared_ptr<const RuntimeSnapshot> snap = compilePublished(h);
    ASSERT_NE(snap, nullptr) << "能力缺失快照正常发布（降级非失败）";

    // 行为面：替身消费方按声明处置——DataInsufficient 由 dynamics 域给出，
    // runtime 输出（CompileOutcome/RuntimeSnapshot/公共头）不含任何
    // EngineeringStatus 类工程判定字段。
    EXPECT_EQ(consumeForGravityRnea(*snap), ConsumerVerdict::DataInsufficient);

    // 类型层面：runtime 公共头零 EngineeringStatus 符号（BuildRedLine 扫描
    // 同款——include 树全量文本扫描，出现即失败）。
    const std::filesystem::path incRoot
        = std::filesystem::path{IRD_RUNTIME_UNIT_ROOT} / "runtime" / "include";
    ASSERT_TRUE(std::filesystem::exists(incRoot)) << "公共头根不存在: " << incRoot.string();
    for (auto it = std::filesystem::recursive_directory_iterator(incRoot);
         it != std::filesystem::recursive_directory_iterator();) {
        // 先处理当前条目再推进（end 迭代器不可解引用——推进后立即判停）。
        if (it->is_regular_file() && it->path().extension() == ".hpp") {
            std::ifstream in(it->path(), std::ios::binary);
            ASSERT_TRUE(static_cast<bool>(in));
            std::ostringstream ss;
            ss << in.rdbuf();
            const std::string text = ss.str();
            EXPECT_EQ(text.find("EngineeringStatus"), std::string::npos)
                << "runtime 公共头不得出现工程判定字段: " << it->path().string();
        }
        std::error_code ec;
        it.increment(ec);  // C++17：无参重载不存在——error_code 版本（BuildRedLine 同款）
        if (ec) {
            break;
        }
    }
}

// =====================================================================
// RT-RES-3（§8.6 路径不作身份）：同内容资源仅换 sourcePathHint——重编译
// modelIdentity 不变（资源以内容摘要入身份；路径仅追溯提示）。
// =====================================================================

TEST(ContractSuite, SourcePathHintIsNotIdentity_RT_RES_3)
{
    IRD_TEST_INFO("ARC-04", {"AT-18"}, std::nullopt, "rt-runtime@1.0.0");
    const std::vector<std::uint8_t> mesh{'m', 'e', 's', 'h', '-', 'r', '3'};

    td::ContractHarness h1 = td::ContractHarness::make(td::ContractHarness::Physics::None);
    ResourceRef ref1;
    ref1.resourceId = h1.res1;
    ref1.contentDigest = td::digestBytes(mesh);
    ref1.sourcePathHint = std::string{"assets/mesh_v1.stl"};  // 路径提示 A
    ref1.state = ResourceState::Recorded;
    ref1.accessVersion = 1;
    h1.description.resourceRefs.push_back(ref1);

    td::ContractHarness h2 = h1;  // 同闭包同字节——仅换路径提示
    h2.description.resourceRefs.front().sourcePathHint = std::string{"assets/other/mesh.stl"};

    // 两次编译×（S4 读取＋S10 前复查）＝四次同内容调用（末项重复）。
    td::FakeResourceProvider provider;
    provider.program(h1.res1, {mesh, mesh, mesh, mesh});

    CanonicalModelCompiler compiler;
    const CompileOutcome out1 = compileHarness(h1, &provider);
    const CompileOutcome out2 = compileHarness(h2, &provider);
    ASSERT_EQ(out1.status, CompileStatus::Published);
    ASSERT_EQ(out2.status, CompileStatus::Published);
    ASSERT_NE(out1.snapshot, nullptr);
    ASSERT_NE(out2.snapshot, nullptr);
    IRD_EXPECT_IDENTICAL("rt-res-3.modelIdentity",
                         out1.snapshot->modelIdentity().toCanonical(),
                         out2.snapshot->modelIdentity().toCanonical());
}

// =====================================================================
// RT-EQ-1 数据集面（ARC-03/AT-16，附录 D 第 4 项）：rt-fk-equation
// （analytic-case）三方对照——canonical 手写 FK（测试对照工具）与 RobWork
// Device FK（≤1e-9，测试内断言）与数据集期望值（经档案 rt 的
// IRD_EXPECT_CLOSE）。数据集由 testkit 装载（完整性 SHA-256 校验——§4.5）。
// =====================================================================

TEST(ContractSuite, GoldenFkEquationDataset_RT_EQ_1)
{
    IRD_TEST_INFO("ARC-03", {"AT-16"}, tk::DatasetRef{"rt-fk-equation", "1.0.0"}, "rt-runtime@1.0.0");
    const tk::GoldenDataset ds = tk::GoldenDataset::load({"rt-fk-equation", "1.0.0"});
    const tk::ToleranceProfile profile = loadRtProfile();

    // ---- 输入面：model.json → 闭包＋Description（数据集完全决定夹具）----
    const tk::JsonValue model = tk::parseJson(readFile(ds.resolveInput("inputs/model.json")));
    const tk::JsonValue* seeds = model.find("objectSeeds");
    ASSERT_NE(seeds, nullptr);
    const tk::JsonValue* jointsJson = model.find("joints");
    ASSERT_NE(jointsJson, nullptr);
    ASSERT_EQ(jointsJson->items.size(), 3u) << "数据集链定义应为 3 关节";

    td::ContractHarness h = td::ContractHarness::make(td::ContractHarness::Physics::None);
    // 数据集对象种子 → 确定性 id（seed→id 派生纪律与替身一致）；对象库
    // 重建为数据集闭包（robot＋3 joint＋4 link）。
    const std::string robotSeed = jStr(*seeds, "robot");
    h.robot = td::idFrom<core::ObjectId>(robotSeed);
    h.store = td::ScriptedObjectLibrary{};
    h.store.revision = h.revision;
    h.store.seq = 1;
    h.store.branch = h.branch;
    h.store.add(h.robot, robotSeed, kRobotDesignObjectType);

    RobotDesignDescription d;
    d.descriptionContractVersion
        = static_cast<std::uint32_t>(jNum(model, "descriptionContractVersion"));
    d.robotLocalName = jStr(model, "robotLocalName");

    const core::ObjectId jointIds[3] = {td::idFrom<core::ObjectId>(jStr(*seeds, "j1")),
                                        td::idFrom<core::ObjectId>(jStr(*seeds, "j2")),
                                        td::idFrom<core::ObjectId>(jStr(*seeds, "j3"))};
    const core::ObjectId linkIds[4] = {td::idFrom<core::ObjectId>(jStr(*seeds, "l0")),
                                       td::idFrom<core::ObjectId>(jStr(*seeds, "l1")),
                                       td::idFrom<core::ObjectId>(jStr(*seeds, "l2")),
                                       td::idFrom<core::ObjectId>(jStr(*seeds, "l3"))};
    for (std::size_t i = 0; i < 3; ++i) {
        h.store.add(jointIds[i], jStr(*seeds, "j" + std::to_string(i + 1)), "joint");
        const tk::JsonValue& jj = jointsJson->items[i];
        JointDescription jd;
        jd.objectId = jointIds[i];
        jd.localName = jStr(jj, "localName");
        jd.type = JointType::Revolute;
        const tk::JsonValue* axis = jj.find("axis");
        ASSERT_NE(axis, nullptr);
        jd.axis = rw::math::Vector3D<double>(axis->items[0].number, axis->items[1].number,
                                             axis->items[2].number);
        const tk::JsonValue* tr = jj.find("originTranslation");
        ASSERT_NE(tr, nullptr);
        jd.origin = rw::math::Transform3D<double>(
            rw::math::Vector3D<double>(tr->items[0].number, tr->items[1].number,
                                       tr->items[2].number),
            detail::identityTransform3D().R());
        jd.lower = td::val(jNum(jj, "lower"));                      // 单位 rad
        jd.upper = td::val(jNum(jj, "upper"));                      // 单位 rad
        jd.maxVelocity = td::val(jNum(jj, "maxVelocity"));          // 单位 rad/s
        jd.maxAcceleration = td::val(jNum(jj, "maxAcceleration"));  // 单位 rad/s²
        d.joints.push_back(jd);
    }
    const tk::JsonValue* linksJson = model.find("links");
    ASSERT_NE(linksJson, nullptr);
    for (std::size_t i = 0; i < 4; ++i) {
        h.store.add(linkIds[i], jStr(*seeds, "l" + std::to_string(i)), "link");
        LinkDescription ld;
        ld.objectId = linkIds[i];
        ld.localName = linksJson->items[i].text;
        d.links.push_back(ld);
    }
    h.description = std::move(d);
    const std::shared_ptr<const RuntimeSnapshot> snap = compilePublished(h);
    ASSERT_NE(snap, nullptr);

    // 设备与逐关节 Frame（名称唯一来源＝映射——R-4 纪律）。
    const std::string deviceName = snap->nameMap().resolveObjectId(h.robot).get().fullName;
    const rw::core::Ptr<const rw::models::SerialDevice> device
        = snap->workCell().findDevice(deviceName);
    ASSERT_TRUE(!device.isNull());
    const rw::kinematics::Frame* jointFrames[3];
    for (std::size_t i = 0; i < 3; ++i) {
        jointFrames[i] = snap->workCell().findFrame(
            snap->nameMap().resolveObjectId(jointIds[i]).get().fullName);
        ASSERT_NE(jointFrames[i], nullptr) << "关节 Frame 缺失@" << i;
    }

    // ---- 期望面：fk.json（闭式 FK——独立参考依据）----
    const tk::JsonValue expected
        = tk::parseJson(readFile(ds.resolveExpected("expected/fk.json")));
    const tk::JsonValue* samples = expected.find("samples");
    ASSERT_NE(samples, nullptr);
    ASSERT_EQ(samples->items.size(), 4u) << "数据集应含 4 构型样本";

    const double kToolTol = 1e-9;  // 手写 FK 与基线 FK 的测试内对照容差（第 4 项）。
    for (std::size_t s = 0; s < samples->items.size(); ++s) {
        const tk::JsonValue& sample = samples->items[s];
        const std::string cfgId = jStr(sample, "id");
        const tk::JsonValue* qArr = sample.find("q");
        ASSERT_NE(qArr, nullptr);
        ASSERT_EQ(qArr->items.size(), 3u);

        // WC 侧：构型写入线程私有 State（q_rw＝数据集 q——zeroOffset=0）。
        rw::kinematics::State state = snap->makeState();
        device->setQ(rw::math::Q(3, qArr->items[0].number, qArr->items[1].number,
                                 qArr->items[2].number),
                     state);

        // canonical 侧：从设备基座系逐级复合（q_auth＝zeroOffset＋q_rw）。
        const tk::JsonValue* jexp = sample.find("joints");
        ASSERT_NE(jexp, nullptr);
        Pose canonical;
        for (std::size_t i = 0; i < 3; ++i) {
            const CanonicalJoint& cj = snap->model().chain().joints.at(i);
            const Pose anchor = composePose(canonical, Pose{cj.origin.P(), cj.origin.R()});
            const rw::math::Rotation3D<double> rotAfter
                = canonicalRodrigues(cj.axis, cj.zeroOffset + qArr->items[i].number);
            const rw::math::Vector3D<double> handAxis = rotVec(anchor.R, cj.axis);

            // ① 手写 FK ↔ RobWork Device FK（测试内对照——≤1e-9，第 4/5 项）。
            const rw::math::Transform3D<double> rwT = rw::kinematics::Kinematics::frameTframe(
                device->getBase(), jointFrames[i], state);
            for (int r = 0; r < 3; ++r) {
                ASSERT_NEAR(rwT.P()(r), anchor.p[r], kToolTol)
                    << cfgId << " 关节 " << i << " 原点分量 " << r << "（手写↔基线）";
                ASSERT_NEAR(rotVec(rwT.R(), rw::math::Vector3D<double>(0.0, 0.0, 1.0))[r],
                            handAxis[r], kToolTol)
                    << cfgId << " 关节 " << i << " 轴线分量 " << r << "（手写↔基线）";
            }

            // ② 手写 FK ↔ 数据集期望（独立参考依据——经档案 rt 容差，
            //    fieldPath "fk[<s>].origin.*"/"fk[<s>].axis.*"）。
            const tk::JsonValue& je = jexp->items[i];
            const tk::JsonValue* eo = je.find("origin");
            const tk::JsonValue* ea = je.find("axis");
            ASSERT_NE(eo, nullptr);
            ASSERT_NE(ea, nullptr);
            const std::string pfx = "fk[" + std::to_string(s) + "]";
            IRD_EXPECT_CLOSE(pfx + ".origin.x", anchor.p[0], eo->items[0].number, profile, "m");
            IRD_EXPECT_CLOSE(pfx + ".origin.y", anchor.p[1], eo->items[1].number, profile, "m");
            IRD_EXPECT_CLOSE(pfx + ".origin.z", anchor.p[2], eo->items[2].number, profile, "m");
            IRD_EXPECT_CLOSE(pfx + ".axis.x", handAxis[0], ea->items[0].number, profile, "1");
            IRD_EXPECT_CLOSE(pfx + ".axis.y", handAxis[1], ea->items[1].number, profile, "1");
            IRD_EXPECT_CLOSE(pfx + ".axis.z", handAxis[2], ea->items[2].number, profile, "1");

            canonical = composePose(
                anchor, Pose{rw::math::Vector3D<double>(0.0, 0.0, 0.0), rotAfter});
        }
    }

    // 边界样例在位（附录 D C4：零值/近零/正负抵消——manifest edgeCases 的
    // sampleRefs 指向的构型 id 必须真实存在于期望面）。
    bool hasZero = false, hasNearZero = false, hasCancel = false;
    for (const tk::JsonValue& sample : samples->items) {
        const std::string id = jStr(sample, "id");
        hasZero = hasZero || id == "cfg-zero";
        hasNearZero = hasNearZero || id == "cfg-near-zero";
        hasCancel = hasCancel || id == "cfg-sign-cancel";
    }
    EXPECT_TRUE(hasZero && hasNearZero && hasCancel)
        << "数据集应含零值/近零/正负抵消样例（附录 D C4）";
}

// =====================================================================
// RT-NM-1 数据集面（ARC-04/MDL-14/AT-18）：rt-namemap-roundtrip
// （contract-fixture）——全量 ObjectId→Name→Id 与 Name→Id→Name 双向往返
// （IRD_EXPECT_IDENTICAL 精确等值——附录 D 第 12 项无容差）。
// =====================================================================

TEST(ContractSuite, GoldenNameMapRoundtripDataset_RT_NM_1)
{
    IRD_TEST_INFO("ARC-04", {"AT-18"}, tk::DatasetRef{"rt-namemap-roundtrip", "1.0.0"},
                  "rt-runtime@1.0.0");
    const tk::GoldenDataset ds = tk::GoldenDataset::load({"rt-namemap-roundtrip", "1.0.0"});

    // ---- 输入面：robot.json → 闭包＋Description（2 关节命名夹具）----
    const tk::JsonValue model = tk::parseJson(readFile(ds.resolveInput("inputs/robot.json")));
    const tk::JsonValue* seeds = model.find("objectSeeds");
    ASSERT_NE(seeds, nullptr);
    const tk::JsonValue* jointsJson = model.find("joints");
    ASSERT_NE(jointsJson, nullptr);
    ASSERT_EQ(jointsJson->items.size(), 2u);

    td::ContractHarness h = td::ContractHarness::make(td::ContractHarness::Physics::None);
    const std::string robotSeed = jStr(*seeds, "robot");
    h.robot = td::idFrom<core::ObjectId>(robotSeed);
    h.store = td::ScriptedObjectLibrary{};
    h.store.revision = h.revision;
    h.store.seq = 1;
    h.store.branch = h.branch;
    h.store.add(h.robot, robotSeed, kRobotDesignObjectType);

    RobotDesignDescription d;
    d.descriptionContractVersion
        = static_cast<std::uint32_t>(jNum(model, "descriptionContractVersion"));
    d.robotLocalName = jStr(model, "robotLocalName");
    const core::ObjectId jointIds[2] = {td::idFrom<core::ObjectId>(jStr(*seeds, "j1")),
                                        td::idFrom<core::ObjectId>(jStr(*seeds, "j2"))};
    const core::ObjectId linkIds[3] = {td::idFrom<core::ObjectId>(jStr(*seeds, "l0")),
                                       td::idFrom<core::ObjectId>(jStr(*seeds, "l1")),
                                       td::idFrom<core::ObjectId>(jStr(*seeds, "l2"))};
    for (std::size_t i = 0; i < 2; ++i) {
        h.store.add(jointIds[i], jStr(*seeds, "j" + std::to_string(i + 1)), "joint");
        const tk::JsonValue& jj = jointsJson->items[i];
        JointDescription jd;
        jd.objectId = jointIds[i];
        jd.localName = jStr(jj, "localName");
        jd.type = JointType::Revolute;
        jd.axis = rw::math::Vector3D<double>(0.0, 0.0, 1.0);
        const tk::JsonValue* tr = jj.find("originTranslation");
        ASSERT_NE(tr, nullptr);
        jd.origin = rw::math::Transform3D<double>(
            rw::math::Vector3D<double>(tr->items[0].number, tr->items[1].number,
                                       tr->items[2].number),
            detail::identityTransform3D().R());
        jd.lower = td::val(jNum(jj, "lower"));
        jd.upper = td::val(jNum(jj, "upper"));
        jd.maxVelocity = td::val(jNum(jj, "maxVelocity"));
        jd.maxAcceleration = td::val(jNum(jj, "maxAcceleration"));
        d.joints.push_back(jd);
    }
    for (std::size_t i = 0; i < 3; ++i) {
        h.store.add(linkIds[i], jStr(*seeds, "l" + std::to_string(i)), "link");
        LinkDescription ld;
        ld.objectId = linkIds[i];
        ld.localName = model.find("links")->items[i].text;
        d.links.push_back(ld);
    }
    h.description = std::move(d);
    const std::shared_ptr<const RuntimeSnapshot> snap = compilePublished(h);
    ASSERT_NE(snap, nullptr);
    const RuntimeNameMap& map = snap->nameMap();

    // ---- 期望面：namemap.json 全量条目——双向往返（AT-18）----
    const tk::JsonValue expected
        = tk::parseJson(readFile(ds.resolveExpected("expected/namemap.json")));
    const tk::JsonValue* entries = expected.find("entries");
    ASSERT_NE(entries, nullptr);

    const std::map<std::string, NameScope> scopeOf = {
        {"Device", NameScope::Device},       {"Joint", NameScope::Joint},
        {"LinkFrame", NameScope::LinkFrame}, {"BaseMount", NameScope::BaseMount},
        {"BaseFrame", NameScope::BaseFrame}, {"Flange", NameScope::Flange},
    };
    // 身份作用域（§7.1——resolveObjectId 的返回值来源）：robot→Device、
    // joint→Joint、link→LinkFrame；BaseMount/BaseFrame/Flange 是共享对象
    // ObjectId 的派生条目——只可经名称正向解析（resolveObjectId 恒返回
    // 身份作用域条目，§7.3"身份条目规则"）。
    const std::set<std::string> identityScopes = {"Device", "Joint", "LinkFrame"};

    // 正向：ObjectId → RuntimeName（身份作用域条目逐条命中期望全名）。
    for (std::size_t i = 0; i < entries->items.size(); ++i) {
        const tk::JsonValue& e = entries->items[i];
        const core::ObjectId id = td::idFrom<core::ObjectId>(jStr(e, "seed"));
        const std::string fullName = jStr(e, "fullName");
        const std::string scope = jStr(e, "scope");
        const std::string pfx = "namemap[" + std::to_string(i) + "]";

        if (identityScopes.count(scope) != 0u) {
            // 身份作用域：resolveObjectId 必命中本条目（§7.3 身份条目规则）。
            const Expected<RuntimeName, RuntimeNameError> fwd = map.resolveObjectId(id);
            ASSERT_TRUE(fwd.ok()) << pfx << " 正向反解未命中: " << fullName;
            IRD_EXPECT_IDENTICAL(pfx + ".forward.fullName", fwd.get().fullName, fullName);
            EXPECT_EQ(fwd.get().scope, scopeOf.at(scope)) << pfx << " 范围不符";
            IRD_EXPECT_IDENTICAL(pfx + ".forward.localName", fwd.get().localName,
                                 jStr(e, "authoritativeLocalName"));
        } else {
            // 派生作用域：resolveObjectId 返回所属对象的身份条目（不等于本
            // 条目）——断言其不抛且返回身份作用域（§7.3 的方向性契约）。
            const Expected<RuntimeName, RuntimeNameError> fwd = map.resolveObjectId(id);
            ASSERT_TRUE(fwd.ok()) << pfx << " 所属对象的身份条目应存在";
            EXPECT_NE(fwd.get().fullName, fullName)
                << pfx << " 派生条目不得被 resolveObjectId 返回（身份条目规则）";
        }

        // 反向：RuntimeName → ObjectId（整串精确匹配——不拆段不猜前缀）。
        // 身份与派生条目都登记在 fullName 索引中——双向全量往返（AT-18）。
        const Expected<ObjectRef, RuntimeResolveError> rev = map.resolveRuntimeName(fullName);
        ASSERT_TRUE(rev.ok()) << pfx << " 反向解析未命中: " << fullName;
        EXPECT_TRUE(rev.get().objectId.bytes == id.bytes)
            << pfx << " 往返对象身份应逐字节一致（AT-18 双向往返）";
    }

    // 双射计数相等（RT-NM-1：映射条目应与期望一一对应——无遗漏/无多余，
    // MDL-14 全量生成）；映射内容身份随快照（CON-06——数据集载体的身份面）。
    EXPECT_EQ(map.size(), entries->items.size());
    EXPECT_FALSE(snap->nameMapIdentity().toCanonical().empty());
}

}  // namespace
