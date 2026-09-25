/**
 * @file   PackageWorkCellTest.cpp
 * @brief  WorkCell/DWC XML 外供导出的单元测试（WP-13-T13 ACC5 具名自证——
 *        消费 runtime 编译产物〔S6/S7 非模板类〕，仅集成模式编译——
 *        ReadinessTest/CommandHandlersTest 同款 TARGET gating 先例）。
 *
 * 用例面（acceptance 5：数据源＝RuntimeSnapshot 只读视图；modeling 仅编排
 * 写出〔io AtomicWriter〕、不改造内容；零修订、零失效）：
 *   - WorkCellDwcXmlExportFromSnapshotView  （ACC5 主用例：真实编译快照→
 *        两面 XML 原子落盘→内容逐项只读核对〔帧/设备/关节限位/重力/体物
 *        性〕→确定性与 V-29 失败恢复）
 *
 * 快照装配（测试域自持，不入公共面）：本 TU 复用 runtime 公共面——
 * CanonicalModelBuilder（S5 真实构造）＋RuntimeSnapshotFactory（S6~S10
 * 真实编译编排）＋ICanonicalModelCompiler 注入替身（SnapshotTest 同款
 * ScriptedCompiler 模式——替身应答 S1~S5 模型，工厂执行 S6 WC/S7 DWC）。
 * runtime 私有/测试头零 include（R-2——夹具自持）。
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/modeling/DiagCodes.hpp>       // MDL-EXPORT-FAILED 常量（诊断码断言）
#include <sdurws/ird/modeling/Package.hpp>
#include <sdurws/ird/runtime/CanonicalModel.hpp>   // CanonicalModelBuilder＋Canonical* 值类型（S5）
#include <sdurws/ird/runtime/Compiler.hpp>         // ICanonicalModelCompiler/CompileRequest/Outcome
#include <sdurws/ird/runtime/Snapshot.hpp>         // RuntimeSnapshot/RuntimeSnapshotFactory（S6~S10）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

namespace {

using namespace sdurws::ird;
using namespace sdurws::ird::modeling;
using namespace sdurws::ird::runtime;

// =====================================================================
// 测试域夹具（runtime 公共面自持——R-2：零跨单元私有/测试头 include）
// =====================================================================

/// 用户输入来源标记（methodTag 语法合规——BridgeFixtures 同款纪律）。
core::ValueProvenance prov()
{
    return core::ValueProvenance::make(core::ProvenanceKind::UserProvided,
                                       std::nullopt, std::nullopt,
                                       std::string("test-fixture"));
}

/// 已提供值（SourcedValue 便捷—— mass/限速/质心/惯量装配）。
core::SourcedValue<double> val(double v)
{
    return core::SourcedValue<double>::provided(v, prov());
}

/// SPD 对角惯量（kg·m²，质心系——合法域内对角张量）。
rw::math::InertiaMatrix<double> diagInertia(double i1, double i2, double i3)
{
    return rw::math::InertiaMatrix<double>(i1, 0, 0, 0, i2, 0, 0, 0, i3);
}

/// 测试用身份（由字符串种子确定性生成——重复装配同身份）。
template <class Id>
Id idFrom(const std::string& seed)
{
    auto parsed = Id::tryFromCanonical(seed);
    if (parsed.has_value()) { return *parsed; }
    return Id::generate();
}

/**
 * @brief 装配最小可编译 CanonicalModel（2 旋转关节＋3 连杆全物性——S7 DWC
 *        能力就绪；Identity 块/闭包引用/世界/链逐项满足 S5 构造不变量）。
 */
CanonicalModel compileReadyModel()
{
    CanonicalModelHeader header;
    header.project = idFrom<core::ProjectId>("prj-pkg-wc");
    header.branch = idFrom<core::BranchId>("brn-pkg-wc");
    header.revision = idFrom<core::RevisionId>("rev-pkg-wc-1");
    header.revisionSeq = 1;
    header.descriptionContractVersion = 1;
    header.compilerContractVersion = 1;
    // builtFrom＝Description canonical 字节摘要（Digest256 直接是 32 字节数组
    // ——测试确定性值）。
    for (int i = 0; i < 32; ++i) {
        header.builtFrom[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i + 1);
    }

    // 修订闭包引用（根＋关节＋连杆逐一登记——S5"∈objectRefs"复核面）。
    const core::ObjectId robot = core::ObjectId::generate();
    const core::ObjectId j1 = core::ObjectId::generate();
    const core::ObjectId j2 = core::ObjectId::generate();
    std::vector<core::ObjectId> linkIds;
    for (int i = 0; i < 3; ++i) {
        linkIds.push_back(core::ObjectId::generate());
    }
    auto addRef = [&header](const core::ObjectId& id, const char* token) {
        ObjectRefEntry entry;
        entry.objectId = id;
        entry.contentVersion.bytes[0] = 0x5A;   // 非零 cv（CON-01 测试值）
        entry.objectTypeToken = token;
        for (int i = 0; i < 32; ++i) {
            entry.digest[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i + 3);
        }
        header.objectRefs.push_back(entry);
    };
    addRef(robot, "robot-design");
    addRef(j1, "joint");
    addRef(j2, "joint");
    for (const core::ObjectId& linkId : linkIds) {
        addRef(linkId, "link");
    }

    RobotChain chain;
    chain.robotObjectId = robot;
    chain.robotLocalName = "IRB_PKG";
    chain.deviceName = "IRB_PKG";

    CanonicalJoint joint1;
    joint1.objectId = j1;
    joint1.localName = "joint_1";
    joint1.type = runtime::JointType::Revolute;
    joint1.axis = rw::math::Vector3D<double>(0.0, 0.0, 1.0);
    joint1.bounds = runtime::JointBounds{-1.5, 1.5};   // rad
    joint1.maxVelocity = val(2.5);
    chain.joints.push_back(joint1);

    CanonicalJoint joint2;
    joint2.objectId = j2;
    joint2.localName = "joint_2";
    joint2.type = runtime::JointType::Revolute;
    joint2.axis = rw::math::Vector3D<double>(0.0, 1.0, 0.0);
    joint2.bounds = runtime::JointBounds{-2.0, 2.0};   // rad
    joint2.maxVelocity = val(2.0);
    chain.joints.push_back(joint2);

    const double masses[3] = {5.0, 4.0, 3.0};   // kg（全物性——DWC 编译就绪）
    for (int i = 0; i < 3; ++i) {
        CanonicalLink link;
        link.objectId = linkIds[static_cast<std::size_t>(i)];
        link.localName = (i == 0) ? "base_link" : ("link_" + std::to_string(i));
        link.mass = val(masses[i]);
        link.centerOfMass =
            core::SourcedValue<rw::math::Vector3D<double>>::provided(
                rw::math::Vector3D<double>(0.0, 0.0, 0.05), prov());
        link.inertia =
            core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
                diagInertia(0.01, 0.02, 0.03), prov());
        chain.links.push_back(link);
    }

    CanonicalModelBuilder builder;
    builder.setHeader(header)
        .setWorld(WorldPlacement{})
        .setChain(chain);
    return builder.build();
}

/**
 * @brief S1~S5 注入替身（SnapshotTest ScriptedCompiler 同款模式——应答预构
 *        模型，工厂执行 S6~S10 真实编译）。
 */
class ModelCompiler final : public ICanonicalModelCompiler {
public:
    explicit ModelCompiler(CanonicalModel model) : m_model(std::move(model)) {}

    CompileOutcome compile(const CompileRequest&) override
    {
        // 整体事务入口＝RT-T11 交付面；本用例走分段入口（工厂编排），替身
        // 不实现本入口（如实 Failed——接口完备性占位）。
        CompileOutcome out;
        out.status = CompileStatus::Failed;
        return out;
    }
    runtime::Expected<CanonicalModel, RuntimeError>
        buildCanonicalModel(const CompileRequest&) override
    {
        return runtime::Expected<CanonicalModel, RuntimeError>::ok(m_model);
    }
    std::uint32_t contractVersion() const noexcept override { return 1; }
    std::string implementationVersion() const noexcept override { return "test-1"; }

private:
    CanonicalModel m_model;
};

/// 编译发布便捷入口（Published 断言＋快照返回——失败诊断直出）。
std::shared_ptr<const RuntimeSnapshot>
    createPublished(const CanonicalModel& model, const CompileOptions& options)
{
    ModelCompiler compiler(model);
    CompileRequest request;
    request.revision = model.header().revision;
    request.options = options;
    const CompileOutcome out = RuntimeSnapshotFactory{}.create(request, compiler);
    EXPECT_EQ(out.status, CompileStatus::Published) << "快照应发布（诊断："
                                                    << (out.diagnostics.empty()
                                                            ? std::string{}
                                                            : out.diagnostics.front().cause)
                                                    << "）";
    return out.snapshot;
}

/// 读文件文本（测试侧自持 I/O）。
std::string readFile(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

// =====================================================================
// ACC5：WC/DWC XML 外供导出（数据源＝快照只读视图；零修订、零失效）
// =====================================================================

/**
 * @brief ACC5 主用例：真实编译快照→两面 XML 原子落盘；内容逐项只读核对
 *        （帧/设备/关节限位/重力/体物性——"不改造内容"的观测面）；确定性
 *        ＋V-29 失败恢复＋DWC 能力缺失拒绝面。
 */
TEST(MdlPackageWorkCell, WorkCellDwcXmlExportFromSnapshotView_WP13T13_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-20", "MDL-22"},
                  std::vector<std::string>{"V-29"});

    // ---- 真实编译快照（S5 替身应答＋S6/S7/S8/S10 工厂真实执行）----
    const auto snapshot = createPublished(compileReadyModel(), CompileOptions{});
    ASSERT_NE(snapshot, nullptr);
    ASSERT_TRUE(snapshot->capabilities().hasDynamicWorkCell)
        << "夹具全物性——DWC 能力就绪（S7 编译）";

    // 用例临时目录（唯一路径——进程内计数）。
    static int counter = 0;
    const std::filesystem::path dir = std::filesystem::temp_directory_path()
        / ("ird-pkg-wc-test-" + std::to_string(++counter));
    std::filesystem::create_directories(dir);

    const std::filesystem::path wcFile = dir / "workcell.xml";
    const std::filesystem::path dwcFile = dir / "dynamic.xml";

    // ---- 两面导出（modeling 仅编排写出——io AtomicWriter 原子落盘）----
    std::vector<core::DiagnosticRecord> diags;
    WorkCellExportTarget target;
    target.wcTargetFile = wcFile;
    target.dwcTargetFile = dwcFile;
    const PackageExportOutcome exported = exportWorkCellXml(*snapshot, target, diags);

    ASSERT_TRUE(exported.ok) << "导出失败：" << exported.error.detail;
    EXPECT_TRUE(diags.empty()) << "成功轨不产诊断";
    EXPECT_EQ(exported.entryCount, 2u) << "WC＋DWC 两文件";
    ASSERT_TRUE(std::filesystem::exists(wcFile));
    ASSERT_TRUE(std::filesystem::exists(dwcFile));

    // ---- WC 面内容核对（只读视图逐项——"不改造内容"的观测面）----
    const std::string wcXml = readFile(wcFile);
    EXPECT_NE(wcXml.find("<ird-workcell-export"), std::string::npos);
    EXPECT_NE(wcXml.find("IRB_PKG"), std::string::npos) << "设备名透传（R-4——不消歧不改写）";
    EXPECT_NE(wcXml.find("joint_1"), std::string::npos);
    EXPECT_NE(wcXml.find("-1.5"), std::string::npos) << "关节下限 rad 原值";
    EXPECT_NE(wcXml.find("1.5"), std::string::npos) << "关节上限 rad 原值";
    EXPECT_NE(wcXml.find("WORLD"), std::string::npos) << "基线根帧在帧表";

    // ---- DWC 面内容核对（重力/体物性透传——基线 SI 值零换算）----
    const std::string dwcXml = readFile(dwcFile);
    EXPECT_NE(dwcXml.find("<ird-dwc-export"), std::string::npos);
    EXPECT_NE(dwcXml.find("-9.81"), std::string::npos)
        << "默认重力 g=(0,0,−9.81) 透传（WorldPlacement 缺省——MDL-22 面）";
    EXPECT_NE(dwcXml.find("base_link"), std::string::npos) << "体名透传";
    EXPECT_NE(dwcXml.find("5"), std::string::npos) << "体质量 kg 原值（5.0 连杆）";

    // ---- 确定性（同快照→同 XML 字节——NFR-COR-02）----
    const std::filesystem::path wcAgain = dir / "workcell-again.xml";
    WorkCellExportTarget again;
    again.wcTargetFile = wcAgain;
    const PackageExportOutcome rerun = exportWorkCellXml(*snapshot, again, diags);
    ASSERT_TRUE(rerun.ok);
    EXPECT_EQ(readFile(wcFile), readFile(wcAgain));

    // ---- V-29：目标已存在＋NeverOverwrite——旧输出完好＋MDL-EXPORT-FAILED ----
    const std::filesystem::path guarded = dir / "guarded.xml";
    {
        std::ofstream out(guarded, std::ios::binary);
        out << "PREVIOUS";
    }
    std::vector<core::DiagnosticRecord> failDiags;
    WorkCellExportTarget conflict;
    conflict.wcTargetFile = guarded;
    conflict.replace = io::ReplacePolicy::NeverOverwrite;
    const PackageExportOutcome rejected = exportWorkCellXml(*snapshot, conflict, failDiags);
    ASSERT_FALSE(rejected.ok);
    EXPECT_EQ(rejected.error.code, ModelingErrorCode::ExportFailed);
    ASSERT_FALSE(failDiags.empty());
    EXPECT_EQ(failDiags.front().code, std::string(kMdlExportFailed));
    EXPECT_EQ(readFile(guarded), std::string("PREVIOUS")) << "旧输出完好（V-29）";

    // ---- DWC 能力缺失拒绝面：requestDynamicWorkCell=false 的快照 ----
    CompileOptions noDwc;
    noDwc.requestDynamicWorkCell = false;
    const auto wcOnly = createPublished(compileReadyModel(), noDwc);
    ASSERT_NE(wcOnly, nullptr);
    std::vector<core::DiagnosticRecord> dwcDiags;
    WorkCellExportTarget dwcMissing;
    dwcMissing.dwcTargetFile = dir / "nope.xml";
    const PackageExportOutcome noCapability =
        exportWorkCellXml(*wcOnly, dwcMissing, dwcDiags);
    ASSERT_FALSE(noCapability.ok) << "无 DWC 能力而请求 DWC 面——拒绝";
    EXPECT_EQ(noCapability.error.code, ModelingErrorCode::ExportFailed);

    std::filesystem::remove_all(dir);
}

}  // namespace
