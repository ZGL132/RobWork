/**
 * @file   HostCompilePipelineTest.cpp
 * @brief  宿主编译链与呈现装配适配器的模型测试（UI-T46）——编译端口端
 *         到端（合成闭包→十段链发布→快照缓存）＋名称映射真值二态＋呈现
 *         源折叠投影＋行程名称上下文转发。
 *
 * 测试锚（对齐任务契约 UI-T46 acceptance）：
 *   - 编译端口：S5 双编译真实执行（Published＝快照缓存可取；Failed＝诊断
 *     透传不提交）——ACC1；
 *   - 名称映射真值：未绑定＝诚实空二态（与 HostEmptyNameMapPort 语义逐字
 *     同构）；绑定真实呈现视图后正/反向反解与内容身份转发——ACC2；
 *   - 呈现源：RT-T14 工厂折叠投影完整（三类身份＋objectExists 正反例；
 *     无快照＝诚实 nullopt）——ACC3；
 *   - 行程名称上下文：端口现取转发（未绑定空值/绑定后解析/内容身份）——
 *     ACC4。
 *
 * 真实链路面（替身策略）：存储查询端口为内存替身（project 公共契约形状
 * ——TestQueryPort 同款缩减版，对象字节＝modeling 公共 Codec 编码的真
 * 实 RobotDesign）；编译链/呈现视图/名称映射全部为**产品实现**（十段链
 * 端到端——替身只替换存储边界，ARC-03 确定性与 MDL-06 发布语义在真实
 * 面上验证）。
 *
 * 集成树专属（if(TARGET sdurws) 门控——被测 TU 同源编入＋框架链接面；
 * 冒烟树不注册本增列，与 UI-T45 网关测试先例同款判别纪律）。
 */

#include <gtest/gtest.h>
#include <QFile> // 资源桥回归使用真实临时文件，io负责实际安全读取和摘要复核。
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <sdurws/ird/modeling/CommandHandlers.hpp>
#include <sdurws/ird/policy/SystemDefaultPolicy.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/UndoRedo.hpp>
#include <sdurws/ird/ui/IDraftController.hpp> // 完整装配门面只前置声明，调用需公共完整类型。
#include <sdurws/ird/ui/IPluginUiModule.hpp>


#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits> // 历史五变体取内嵌身份，根身份仍来自闭包引用。
#include <vector>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/modeling/Codec.hpp>          // RobotDesignCodec/kCurrentFormatVersion（真实编码）
#include <sdurws/ird/modeling/ObjectTypes.hpp>    // kRobotDesignObjectType（token 单一权威）
#include <sdurws/ird/modeling/RobotDesign.hpp>    // RobotDesign/JointEntry/LinkEntry 值模型
#include <sdurws/ird/project/CommandService.hpp>  // project::CompileRequest/ObjectWrite（S5 编排输入）
#include <sdurws/ird/project/QueryPort.hpp>       // project::IProjectQueryPort（替身契约形状）
#include <sdurws/ird/runtime/HostPresentationView.hpp>  // createHostPresentationView（真实呈现视图）
#include <sdurws/ird/kinematics/Fk.hpp>
#include <sdurws/ird/kinematics/Ik.hpp>
#include <cmath>
#include <limits>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/models/Device.hpp>
#include <sdurws/ird/modeling/ModelingPluginAssembly.hpp>
#include <sdurws/ird/runtime/Snapshot.hpp>
#include <sdurws/ird/runtime/Codec.hpp>  // A-15：真实worker物化编码，完整命名TCP不可丢失。
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

// UI-T78 渲染面用例的框架消费面（被测 TU 同源编入门控块的框架链接面——
// 图元类型甄别/分组常量/WC 对象遍历；与 HostPresentationAdapters.cpp
// 的框架依赖同源，链接面既有）。
#include <rw/geometry/Geometry.hpp>          // Geometry（名/帧绑定读取）
#include <rw/geometry/GeometryData.hpp>      // GeometryType（图元类型甄别值）
#include <rw/graphics/DrawableNode.hpp>      // DrawableNode::Physical（分组绑定断言）
#include <rw/math/Transform3D.hpp>           // 关节原点位姿构造（m/rad——单位显式）
#include <rw/models/Object.hpp>              // Object::getGeometry（图元清单独立遍历——完整类型）
#include <rw/models/WorkCell.hpp>            // WorkCell::getObjects（呈现消费遍历）

#include "plugin/HostCompilePort.hpp"            // 被测：编译端口/名称上下文（同源编入）
#include "plugin/HostPresentationAdapters.hpp"   // 被测：映射真值端口/呈现源/挂接对象
#include "plugin/KinEvaluationChannel.hpp"      // MA-03：快照与计算视图同拍换绑。

namespace sdurws::ird::ui::test {
namespace {

// =====================================================================
// 内存查询端口替身（project 公共契约形状——objectRefs/RevisionView 同构；
// modeling TestQueryPort 同款缩减版——HostCompilePort 消费面全覆盖）
// =====================================================================

class MemoryQueryPort final : public project::IProjectQueryPort {
public:
    project::RevisionView view;  ///< head()/revision()/tryRevision 应答值
    /// 对象字节表（键＝"<oid 规范文本>|<cv 规范文本>"）。
    std::map<std::string, std::vector<std::uint8_t>> objects;

    /// 登记一个闭包对象（引用进 view.objectRefs＋字节入表——一次装配）。
    void addObject(const core::ObjectId& oid, std::string token,
                   const std::vector<std::uint8_t>& bytes)
    {
        // 内容版本＝字节 SHA-256 的确定性投影（core 摘要器——与合成闭包
        // 口径同源；替身内自洽即可，语义对齐内容寻址）。
        core::ContentDigester d;
        d.update(bytes.data(), bytes.size());
        core::ContentVersion cv;
        cv.bytes = d.finalize();
        project::ObjectRef ref;
        ref.objectId = oid;
        ref.contentVersion = cv;
        ref.objectTypeToken = std::move(token);
        // digest256＝64 hex（与摘要字节一致——本替身真实申报，编译器
        // S2④ 复核按真实面通过）。
        static const char* kHex = "0123456789abcdef";
        std::string hex;
        hex.reserve(64);
        for (std::uint8_t b : cv.bytes) {
            hex.push_back(kHex[b >> 4]);
            hex.push_back(kHex[b & 0x0F]);
        }
        ref.digest256 = std::move(hex);
        view.objectRefs.push_back(std::move(ref));
        objects[oid.toCanonical() + "|" + cv.toCanonical()] = bytes;
    }

    [[nodiscard]] project::RevisionView head() const override { return view; }
    [[nodiscard]] std::optional<project::RevisionView> tryRevision(
        core::RevisionId id) const override
    {
        if (id == view.id) { return view; }
        return std::nullopt;
    }
    [[nodiscard]] project::RevisionView revision(core::RevisionId) const override
    {
        return view;
    }
    [[nodiscard]] project::ProjectMetadataView currentMetadata() const override
    {
        return {};
    }
    [[nodiscard]] std::optional<project::ProjectMetadataView> metadataAt(
        core::RevisionId) const override
    {
        return std::nullopt;
    }
    [[nodiscard]] std::vector<project::BranchTip> branchTips() const override
    {
        return {};
    }
    [[nodiscard]] std::vector<project::RevisionView> branchHistory(
        core::BranchId, std::uint32_t) const override
    {
        return {};
    }
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> tryObject(
        core::ObjectId oid, core::ContentVersion cv) const noexcept override
    {
        auto it = objects.find(oid.toCanonical() + "|" + cv.toCanonical());
        if (it == objects.end()) { return std::nullopt; }
        return it->second;
    }
    [[nodiscard]] std::vector<std::uint8_t> object(core::ObjectId oid,
                                                   core::ContentVersion cv) const override
    {
        auto bytes = tryObject(oid, cv);
        if (!bytes.has_value()) {
            throw std::logic_error("fixture: 对象字节缺失（引用断裂）");
        }
        return *bytes;
    }
    [[nodiscard]] std::vector<project::DraftInfo> listDrafts(core::BranchId) const override
    {
        return {};
    }
    [[nodiscard]] std::vector<project::RunInfo> listRuns(core::RevisionId) const override
    {
        return {};
    }
    [[nodiscard]] std::filesystem::path runDir(core::RunId) const override
    {
        return {};
    }
};

// =====================================================================
// 夹具（真实 RobotDesign canonical 字节——modeling 公共 Codec 编码）
// =====================================================================

class HostCompilePipelineTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // 最小合法根对象：1 旋转关节＋2 连杆（I-MDL-1 计数关系——与
        // modeling 命令夹具同构的最小合法形态；物性缺失＝DataInsufficient
        // 预告面，不触发硬断言——编译链不受影响）。
        modeling::RobotDesign design;
        design.displayName = "TestRobot";

        modeling::JointEntry joint;
        joint.objectId = core::ObjectId::generate();
        joint.localName = "J1";
        joint.type = modeling::JointType::Revolute;
        joint.axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(0.0, 0.0, 1.0),
            core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        joint.origin = core::SourcedValue<modeling::JointPose>::provided(
            modeling::JointPose{},
            core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        joint.bounds = core::SourcedValue<modeling::JointLimits>::provided(
            modeling::JointLimits{0.0, 1.0},  // rad——单位显式（§2.5 纪律）
            core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        design.joints.push_back(std::move(joint));

        modeling::LinkEntry base;
        base.objectId = core::ObjectId::generate();
        base.localName = "base";
        design.links.push_back(std::move(base));
        modeling::LinkEntry link1;
        link1.objectId = core::ObjectId::generate();
        link1.localName = "link1";
        design.links.push_back(std::move(link1));

        // 真实编码（RobotDesignCodec——canonical 字节；与生产流同一路径）。
        modeling::RobotDesignCodec codec;
        auto encoded = codec.encode(modeling::ObjectVariant{std::move(design)},
                                    modeling::kCurrentFormatVersion);
        ASSERT_TRUE(encoded.ok()) << "夹具编码失败：" << encoded.error().detail;
        m_designBytes = std::move(encoded.get());

        // 存储替身装配：基线闭包含旧版根对象（编译计划写入覆盖同 oid——
        // 合成闭包覆盖逻辑的受测面）。修订投影三身份（id/branch/seq）须
        // 有效——S5 header 的来源定位三元组（§4.3.1 空 id 拒绝面）。
        m_query->view.id = core::RevisionId::generate();
        m_query->view.branch = core::BranchId::generate();
        m_query->view.seq = 1;
        m_rootId = core::ObjectId::generate();
        m_query->addObject(m_rootId, std::string(modeling::kRobotDesignObjectType),
                           {0x01, 0x02});  // 旧版占位字节（计划写入覆盖后不再被读）
    }

    /// 编译请求装配（计划写入＝真实设计字节覆盖根对象——生产 S5 同款输入；
    /// CompileRequest 持 const 引用成员——就地聚合构造，无默认构造形态）。
    project::CompileRequest makeCompileRequest()
    {
        m_plannedWrites.clear();
        project::ObjectWrite write;
        write.objectId = core::ObjectId{m_rootId};
        write.objectTypeToken = std::string(modeling::kRobotDesignObjectType);
        write.payloadCanonical = m_designBytes;
        m_plannedWrites.push_back(std::move(write));
        return project::CompileRequest{*m_query, m_plannedWrites, m_query->view.id};
    }

    std::shared_ptr<MemoryQueryPort> m_query = std::make_shared<MemoryQueryPort>();
    core::ObjectId m_rootId;
    std::vector<std::uint8_t> m_designBytes;
    std::vector<project::ObjectWrite> m_plannedWrites;  ///< 计划写入（请求持引用——调用期存活）
};

// =====================================================================
// ACC1——编译端口端到端（Published 快照缓存／Failed 诊断透传）
// =====================================================================

/**
 * @brief MA-03/CON-06：直接解绑视图也必须释放旧碰撞快照。
 * 生产编译器生成真实快照，局部编译端口先析构，弱引用准确观察执行器持有；
 * 没有在途任务时解绑必须释放，而不能给后续新视图复用旧场景。
 */
TEST_F(HostCompilePipelineTest, DetachViewReleasesCollisionSnapshot_MA03)
{
    IRD_TEST_INFO("CON-06", {}, std::nullopt);
    auto snapshot = [this] {
        HostModelCompilePort port({m_query.get(), core::ProjectId::generate(),
            std::string(modeling::kRobotDesignObjectType), nullptr});
        const auto result = port.compileWorkCellAndDwc(makeCompileRequest());
        EXPECT_TRUE(result.ok);
        return port.lastPublishedSnapshot();
    }();
    ASSERT_TRUE(snapshot);
    std::weak_ptr<const runtime::RuntimeSnapshot> lease = snapshot;
    KinEvaluationExecutor executor(KinEvaluationExecutor::Deps{});
    executor.attachSnapshot(snapshot, snapshot->snapshotIdentity());
    snapshot.reset();
    EXPECT_FALSE(lease.expired()) << "已绑定时计算视图与碰撞设施共同持有快照";
    executor.attachView(nullptr, core::ContentIdentity{});
    EXPECT_TRUE(lease.expired()) << "解绑后仍残留旧碰撞场景，可能跨项目消费";
}

/// 编译成功面：计划闭包→十段链发布→快照缓存可取（名称映射非空——S8
/// 产物随快照在位；编译基线对账面回读一致）。
/**
 * @brief A-15/REQ-01：真实建模对象字节→宿主十段编译→真实FK/IK的完整目录链。
 * 查询替身只提供对象字节；Codec、安装复合、内容身份、求解均为产品实现。
 */
TEST_F(HostCompilePipelineTest, NamedTcpClosureCompilesAndSolvesRealNumerics_REQ01_A15)
{
    IRD_TEST_INFO("REQ-01", {"AT-23"}, std::nullopt);
    const modeling::RobotDesignCodec codec;
    const auto decoded = codec.decode(m_designBytes, modeling::kCurrentFormatVersion);
    ASSERT_TRUE(decoded.ok());
    auto root = std::get<modeling::RobotDesign>(decoded.get());
    modeling::ToolDefinition tool;
    tool.objectId = core::ObjectId::generate(); tool.localName = "named-tool";
    tool.mountInterface = rw::math::Transform3D<double>(rw::math::Vector3D<double>(0.2, 0.3, 0.4),
        rw::math::Rotation3D<double>(0,-1,0,1,0,0,0,0,1));
    tool.tcpList = {{"a", rw::math::Transform3D<double>(rw::math::Vector3D<double>(0.5,0,0),
                        rw::math::Rotation3D<double>::identity()), ""},
                   {"b", rw::math::Transform3D<double>(rw::math::Vector3D<double>(0,0.4,0),
                        rw::math::Rotation3D<double>::identity()), ""}};
    root.toolRefs = {tool.objectId}; root.defaultTcp = modeling::TcpRef{tool.objectId, "a"};
    const auto rootBytes = codec.encode(modeling::ObjectVariant(root), modeling::kCurrentFormatVersion);
    const auto toolBytes = codec.encode(modeling::ObjectVariant(tool), modeling::kCurrentFormatVersion);
    ASSERT_TRUE(rootBytes.ok()); ASSERT_TRUE(toolBytes.ok());
    m_designBytes = rootBytes.get();
    m_query->addObject(tool.objectId, std::string(modeling::kToolDefinitionObjectType), toolBytes.get());
    HostModelCompilePort port({m_query.get(), core::ProjectId::generate(),
        std::string(modeling::kRobotDesignObjectType), nullptr});
    const auto result = port.compileWorkCellAndDwc(makeCompileRequest());
    ASSERT_TRUE(result.ok);
    const auto snapshot = port.lastPublishedSnapshot(); ASSERT_TRUE(snapshot);
    ASSERT_EQ(snapshot->model().tools()[0].namedTcpOffsets.size(), 2U);
    // 跨进程前置也走真实物化工厂：同一1.2字节必须保留完整目录与身份。
    // 快照选项直接取原发布值，避免测试自行猜测编译开关造成身份漂移。
    runtime::snapshotcodec::MaterializedPayload payload;
    payload.revision.id = snapshot->revision(); payload.revision.seq = snapshot->revisionSeq();
    payload.revision.branch = snapshot->branch();
    payload.revision.objectRefs = snapshot->model().header().objectRefs;
    payload.canonicalModelBytes = runtime::rtcodec::encode(snapshot->model());
    payload.nameMapBytes = runtime::rtcodec::encodeNameMap(snapshot->nameMap());
    payload.options = snapshot->compileOptions();
    payload.compilerContractVersion = snapshot->compilerContractVersion();
    payload.compilerVersion = snapshot->compilerVersion();
    payload.expectedModelIdentity = snapshot->modelIdentity();
    payload.expectedNameMapIdentity = snapshot->nameMapIdentity();
    const auto materialized = runtime::RuntimeSnapshotFactory().materialize(
        runtime::snapshotcodec::encode(payload));
    ASSERT_NE(materialized.snapshot, nullptr);
    EXPECT_EQ(materialized.snapshot->modelIdentity(), snapshot->modelIdentity());
    EXPECT_EQ(materialized.snapshot->model(), snapshot->model());
    // 仅借用已发布不可变快照。适配器是IKinRuntimeView端口，不包含计算替身。
    struct View final : kinematics::IKinRuntimeView {
        const runtime::CanonicalModel& value;
        explicit View(const runtime::CanonicalModel& model) : value(model) {}
        const runtime::CanonicalModel& model() const override { return value; }
        rw::math::Transform3D<double> worldToBase() const override { return value.world().T_world_base; }
    } view(materialized.snapshot->model());
    const double q = 0.3;
    for (const auto& row : std::vector<std::pair<std::string, rw::math::Vector3D<double>>>{
            {"a", {0.2,0.8,0.4}}, {"b", {-0.2,0.3,0.4}}}) {
        kinematics::TcpRef tcp; tcp.toolObject = tool.objectId; tcp.tcpKey = row.first;
        const auto fk = kinematics::FkEvaluator().evaluate(view, tcp, {q}); ASSERT_TRUE(fk.ok());
        const auto p = row.second;
        const rw::math::Vector3D<double> expected(std::cos(q)*p[0]-std::sin(q)*p[1],
                                                std::sin(q)*p[0]+std::cos(q)*p[1], p[2]);
        EXPECT_LT((fk.get().tcpInBase.P() - expected).norm2(), 1e-12);
        // 安装旋转Rz(π/2)＋关节角q，独立解析，不复用FK输出作为IK目标。
        const double theta = q + 1.5707963267948966;
        kinematics::IkRequest request;
        request.modelView = &view; request.tcp = tcp;
        request.targetInBase = rw::math::Transform3D<double>(expected,
            rw::math::Rotation3D<double>(std::cos(theta),-std::sin(theta),0,
                                         std::sin(theta),std::cos(theta),0,0,0,1));
        request.initialValues = {{0.1}}; request.referenceQ = {0};
        request.iterationLimit = 200; request.intervals = kinematics::evaluationIntervals(view);
        const auto solved = kinematics::IkSolver().solve(request);
        ASSERT_EQ(solved.outcomeKind, kinematics::IkOutcomeKind::SolutionsFound);
        ASSERT_FALSE(solved.solutionSet.solutions.empty());
        EXPECT_NEAR(solved.solutionSet.solutions.front().q[0], q, 1e-3);
    }
}

TEST_F(HostCompilePipelineTest, CompilePort_PublishesSnapshotAndCachesIt)
{
    HostModelCompilePort port(HostModelCompilePort::Deps{
        m_query.get(), core::ProjectId::generate(),
        std::string(modeling::kRobotDesignObjectType), nullptr});

    const project::CompileResult result = port.compileWorkCellAndDwc(makeCompileRequest());
    if (!result.ok) {
        // 失败诊断全量输出（定位辅助——断言前的可观测面，不改变判定）。
        for (const auto& diag : result.diagnostics) {
            std::cerr << "  [diag] " << diag.code << ": " << diag.cause << "\n";
        }
    }
    EXPECT_TRUE(result.ok) << "端到端编译应发布";

    // 快照缓存面（呈现 source 的供数契约——同一产物传递）。
    const auto snapshot = port.lastPublishedSnapshot();
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(snapshot->modelIdentity().isValid());
    EXPECT_GT(snapshot->nameMap().size(), 0u) << "S8 名称映射应随快照在位";
    // 编译基线对账（发布归属修订的编排取数面——呈现事实构造的输入）。
    EXPECT_TRUE(port.lastPublishedBaseRevision() == m_query->view.id);
}

/** @brief REQ-10：新场景计划位于根之前时也按真实token解引用并完成十段链。 */
TEST_F(HostCompilePipelineTest, ScenePlanRetainsTypeAndDoesNotMasqueradeAsRoot_REQ10_WP14T12)
{
    IRD_TEST_INFO("REQ-10", {"AT-23"}, std::nullopt);
    const modeling::RobotDesignCodec codec;
    const auto decoded=codec.decode(m_designBytes,modeling::kCurrentFormatVersion);
    ASSERT_TRUE(decoded.ok());
    auto design=std::get<modeling::RobotDesign>(decoded.get());
    modeling::SceneObject fixture;
    fixture.objectId=core::ObjectId::generate(); fixture.localName="pick-fixture";
    fixture.worldPose.P()=rw::math::Vector3D<>(0.4,0.2,0.1); // 世界系m，不预乘安装姿态。
    const std::vector<std::uint8_t> geometryBytes{'s','o','l','i','d',' ','f','i','x','t','u','r','e'};
    modeling::ResourceRef resource;
    resource.resourceId="fixture-geometry"; resource.state=modeling::ResourceState::Solidified;
    core::ContentDigester digest; digest.update(geometryBytes.data(),geometryBytes.size());
    resource.contentDigest=digest.finalize();
    core::ContentVersion version; version.bytes=resource.contentDigest;
    resource.solidifiedObject=modeling::SolidifiedRef{core::ObjectId::generate(),version};
    m_query->addObject(resource.solidifiedObject->objectId,"resource",geometryBytes);
    design.resourceManifest.push_back(resource);
    fixture.geometry=modeling::GeometryRef{}; fixture.geometry->resourceRefId=resource.resourceId;
    design.sceneRefs.push_back(fixture.objectId);
    const auto sceneBytes=codec.encode(modeling::ObjectVariant(fixture),modeling::kCurrentFormatVersion);
    const auto rootBytes=codec.encode(modeling::ObjectVariant(design),modeling::kCurrentFormatVersion);
    ASSERT_TRUE(sceneBytes.ok()); ASSERT_TRUE(rootBytes.ok());
    const std::vector<project::ObjectWrite> writes{
        {fixture.objectId,std::string(modeling::kSceneObjectObjectType),sceneBytes.get()},
        {m_rootId,std::string(modeling::kRobotDesignObjectType),rootBytes.get()}};
    HostProjectClosureView closure(*m_query,std::string(modeling::kRobotDesignObjectType),&writes);
    const auto root=closure.tryObjectByToken(modeling::kRobotDesignObjectType);
    const auto scene=closure.tryObject(fixture.objectId);
    ASSERT_TRUE(root); ASSERT_TRUE(scene);
    EXPECT_EQ(root->bytes,rootBytes.get()) << "根检索不能依赖计划槽顺序";
    EXPECT_EQ(scene->objectTypeToken,modeling::kSceneObjectObjectType);
    EXPECT_EQ(scene->bytes,sceneBytes.get());
    HostModelCompilePort port({m_query.get(),core::ProjectId::generate(),
        std::string(modeling::kRobotDesignObjectType),nullptr});
    const project::CompileRequest request{*m_query,writes,m_query->view.id};
    const auto result=port.compileWorkCellAndDwc(request);
    for(const auto& diag:result.diagnostics){ std::cerr<<diag.code<<": "<<diag.cause<<'\n'; }
    EXPECT_TRUE(result.ok) << "新场景载荷不应丢类型，也不能被当作根对象解码";
}

/** @brief REQ-10：仅改既有场景时，从基线取根、从计划取场景，不能按首条载荷猜根。 */
TEST_F(HostCompilePipelineTest, SceneOnlyUpdateUsesBaselineRoot_REQ10_WP14T12)
{
    IRD_TEST_INFO("REQ-10", {"AT-23"}, std::nullopt);
    const modeling::RobotDesignCodec codec;
    const auto decoded=codec.decode(m_designBytes,modeling::kCurrentFormatVersion);
    ASSERT_TRUE(decoded.ok());
    auto design=std::get<modeling::RobotDesign>(decoded.get());
    modeling::SceneObject fixture;
    fixture.objectId=core::ObjectId::generate(); fixture.localName="pick-fixture";
    const std::vector<std::uint8_t> geometryBytes{'s','o','l','i','d',' ','f','i','x','t','u','r','e'};
    modeling::ResourceRef resource;
    resource.resourceId="fixture-geometry"; resource.state=modeling::ResourceState::Solidified;
    core::ContentDigester digest; digest.update(geometryBytes.data(),geometryBytes.size());
    resource.contentDigest=digest.finalize();
    core::ContentVersion version; version.bytes=resource.contentDigest;
    resource.solidifiedObject=modeling::SolidifiedRef{core::ObjectId::generate(),version};
    design.resourceManifest.push_back(resource);
    fixture.geometry=modeling::GeometryRef{}; fixture.geometry->resourceRefId=resource.resourceId;
    design.sceneRefs.push_back(fixture.objectId);
    const auto rootBytes=codec.encode(modeling::ObjectVariant(design),modeling::kCurrentFormatVersion);
    const auto oldSceneBytes=codec.encode(modeling::ObjectVariant(fixture),modeling::kCurrentFormatVersion);
    ASSERT_TRUE(rootBytes.ok()); ASSERT_TRUE(oldSceneBytes.ok());
    m_query->view.objectRefs.clear(); m_query->objects.clear();
    m_query->addObject(resource.solidifiedObject->objectId,"resource",geometryBytes);
    m_query->addObject(m_rootId,std::string(modeling::kRobotDesignObjectType),rootBytes.get());
    m_query->addObject(fixture.objectId,std::string(modeling::kSceneObjectObjectType),oldSceneBytes.get());
    fixture.worldPose.P()=rw::math::Vector3D<>(0.6,0.3,0.2); // 更新世界系位置，单位m。
    const auto sceneBytes=codec.encode(modeling::ObjectVariant(fixture),modeling::kCurrentFormatVersion);
    ASSERT_TRUE(sceneBytes.ok());
    const std::vector<project::ObjectWrite> writes{
        {fixture.objectId,std::string(modeling::kSceneObjectObjectType),sceneBytes.get()}};
    HostProjectClosureView closure(*m_query,std::string(modeling::kRobotDesignObjectType),&writes);
    const auto root=closure.tryObjectByToken(modeling::kRobotDesignObjectType);
    const auto scene=closure.tryObject(fixture.objectId);
    ASSERT_TRUE(root); ASSERT_TRUE(scene);
    EXPECT_EQ(root->bytes,rootBytes.get()) << "无根计划时必须读取基线根";
    EXPECT_EQ(scene->bytes,sceneBytes.get()) << "场景应读新计划，不能退回旧场景";
    HostModelCompilePort port({m_query.get(),core::ProjectId::generate(),
        std::string(modeling::kRobotDesignObjectType),nullptr});
    const project::CompileRequest request{*m_query,writes,m_query->view.id};
    const auto result=port.compileWorkCellAndDwc(request);
    for(const auto& diag:result.diagnostics){ std::cerr<<diag.code<<": "<<diag.cause<<'\n'; }
    EXPECT_TRUE(result.ok) << "部件单独更新必须保留基线根，不要求根冗余写入";
}

/** @brief REQ-10/CON-03：Recorded资源经真实io读取，变化/缺失拒绝且不替换原快照。 */
TEST_F(HostCompilePipelineTest, RecordedResourceIsRereadAndFailurePreservesSnapshot_REQ10_WP14T12)
{
    IRD_TEST_INFO("REQ-10", {"AT-23"}, std::nullopt);
    QTemporaryDir directory; ASSERT_TRUE(directory.isValid());
    const QString path=directory.filePath("fixture.stl");
    const QByteArray content("solid fixture\nendsolid fixture\n");
    QFile file(path); ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(content),content.size()); file.close();
    const modeling::RobotDesignCodec codec;
    const auto decoded=codec.decode(m_designBytes,modeling::kCurrentFormatVersion);
    ASSERT_TRUE(decoded.ok());
    auto design=std::get<modeling::RobotDesign>(decoded.get());
    modeling::ResourceRef resource; resource.resourceId="fixture-resource";
    core::ContentDigester digest; digest.update(content.constData(),static_cast<std::size_t>(content.size()));
    resource.contentDigest=digest.finalize();
    resource.externalRecord=modeling::ExternalResourceRecord{path.toStdString(),resource.contentDigest};
    design.resourceManifest.push_back(resource);
    const auto bytes=codec.encode(modeling::ObjectVariant(design),modeling::kCurrentFormatVersion);
    ASSERT_TRUE(bytes.ok());
    const std::vector<project::ObjectWrite> writes{
        {m_rootId,std::string(modeling::kRobotDesignObjectType),bytes.get()}};
    HostModelCompilePort port({m_query.get(),core::ProjectId::generate(),
        std::string(modeling::kRobotDesignObjectType),nullptr});
    const project::CompileRequest request{*m_query,writes,m_query->view.id};
    const auto first=port.compileWorkCellAndDwc(request);
    for(const auto& diag:first.diagnostics){std::cerr<<diag.code<<": "<<diag.cause<<'\n';}
    ASSERT_TRUE(first.ok) << "真实Recorded资源必须能从现有io桥读取";
    const auto original=port.lastPublishedSnapshot(); ASSERT_NE(original,nullptr);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly|QIODevice::Truncate));
    ASSERT_EQ(file.write("changed",7),7); file.close();
    const auto changed=port.compileWorkCellAndDwc(request);
    EXPECT_FALSE(changed.ok);
    EXPECT_TRUE(std::any_of(changed.diagnostics.begin(),changed.diagnostics.end(),[](const auto& d){
        return d.cause.find("resource-changed")!=std::string::npos;}));
    EXPECT_EQ(port.lastPublishedSnapshot(),original) << "资源变化不能替换已发布快照";
    ASSERT_TRUE(file.remove());
    const auto missing=port.compileWorkCellAndDwc(request);
    EXPECT_FALSE(missing.ok);
    EXPECT_TRUE(std::any_of(missing.diagnostics.begin(),missing.diagnostics.end(),[](const auto& d){
        return d.cause.find("resource-missing")!=std::string::npos;}));
    EXPECT_EQ(port.lastPublishedSnapshot(),original);
}

/// 编译失败面：非法对象字节→ok=false＋诊断非空（MDL-06——失败不提交，
/// 诊断透传不吞）。
TEST_F(HostCompilePipelineTest, CompilePort_FailsWithDiagnosticsOnGarbageBytes)
{
    HostModelCompilePort port(HostModelCompilePort::Deps{
        m_query.get(), core::ProjectId::generate(),
        std::string(modeling::kRobotDesignObjectType), nullptr});

    // 请求就地构造（CompileRequest 持 const 引用成员——坏字节写入局部
    // 计划表后聚合构造）。
    std::vector<project::ObjectWrite> badWrites;
    project::ObjectWrite bad;
    bad.objectId = core::ObjectId{m_rootId};
    bad.objectTypeToken = std::string(modeling::kRobotDesignObjectType);
    bad.payloadCanonical = {0xDE, 0xAD, 0xBE, 0xEF};  // 非法负载
    badWrites.push_back(std::move(bad));
    const project::CompileRequest request{*m_query, badWrites, m_query->view.id};

    const project::CompileResult result = port.compileWorkCellAndDwc(request);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.diagnostics.empty()) << "失败面诊断应全量透传";
    EXPECT_EQ(port.lastPublishedSnapshot(), nullptr) << "失败不发布（无半成品）";
}

/// 引用图摘要字母表违约面（UI-T77——F-563 修复）：digest256 长度合法
/// （64）但含非 hex 字符——旧实现 toSummary（合成闭包装配第一步）逐字符
/// std::stoi 抛 invalid_argument（draft.apply 处理器链无捕获→异常穿透
/// ＝进程退出，F-557 家族崩出面）。修复后违约＝全零摘要如实投递不抛：
/// 本流根对象被计划写入同 oid 覆盖（digest 按计划载荷自洽重算——§6.6
/// 合成口径），主链照常发布。第一断言＝不崩（修复前此路径在合成第一步
/// 即抛，测试进程直接带出）；次断言＝主链不受违约条目影响（覆盖自洽）。
TEST_F(HostCompilePipelineTest, CompilePort_CorruptedDigestDoesNotCrash)
{
    // 损坏引用图摘要（64 字符全 'g'——长度守卫内的字母表违约形态）。
    ASSERT_FALSE(m_query->view.objectRefs.empty());
    m_query->view.objectRefs.front().digest256.assign(64, 'g');

    HostModelCompilePort port(HostModelCompilePort::Deps{
        m_query.get(), core::ProjectId::generate(),
        std::string(modeling::kRobotDesignObjectType), nullptr});

    const project::CompileResult result =
        port.compileWorkCellAndDwc(makeCompileRequest());
    if (!result.ok) {
        for (const auto& diag : result.diagnostics) {
            std::cerr << "  [diag] " << diag.code << ": " << diag.cause << "\n";
        }
    }
    EXPECT_TRUE(result.ok)
        << "违约条目被计划写入覆盖自洽——主链照常（修复前此路径抛 "
           "invalid_argument＝进程崩出）；若本断言失败且诊断含摘要复核"
           "拒绝，说明覆盖语义被破坏";
}

// =====================================================================
// ACC2——名称映射真值二态（空→绑定→正/反解）
// =====================================================================

/// 空二态：未绑定＝两向 nullopt（与诚实空映射语义逐字同构——L2/L3 的
/// 合法触发面，零语义漂移）。
TEST_F(HostCompilePipelineTest, NameMapPort_EmptyBeforeBinding)
{
    HostRuntimeNameMapPort port;
    EXPECT_FALSE(port.hasPresentation());
    EXPECT_EQ(port.resolveObjectIdFromRuntimeName("TestRobot"), std::nullopt);
    EXPECT_EQ(port.resolveRuntimeName(core::ObjectId::generate()), std::nullopt);
}

/// 绑定真值面：真实编译产物呈现视图绑定后——设备名反解成功、正/反向
/// 往返闭合、未知名如实 nullopt（ARC-04 不猜测）。
TEST_F(HostCompilePipelineTest, NameMapPort_ResolvesAfterPresentationBinding)
{
    // 先经真实编译链产出快照（ACC1 同链路），再构造呈现视图。
    HostModelCompilePort port(HostModelCompilePort::Deps{
        m_query.get(), core::ProjectId::generate(),
        std::string(modeling::kRobotDesignObjectType), nullptr});
    ASSERT_TRUE(port.compileWorkCellAndDwc(makeCompileRequest()).ok);
    const auto snapshot = port.lastPublishedSnapshot();
    ASSERT_NE(snapshot, nullptr);

    const auto view = runtime::createHostPresentationView(
        snapshot, m_query->view.id);
    ASSERT_NE(view, nullptr);

    HostRuntimeNameMapPort namePort;
    namePort.bindPresentation(view);
    ASSERT_TRUE(namePort.hasPresentation());

    // 正向：对象→运行时名（快照名称映射首条——确定性取值）。
    const auto& entries = snapshot->nameMap().entries();
    ASSERT_FALSE(entries.empty());
    const auto forward = namePort.resolveRuntimeName(entries.front().objectId);
    ASSERT_TRUE(forward.has_value());
    EXPECT_EQ(*forward, entries.front().fullName);

    // 反向：运行时名→对象（往返闭合——SA-05 双射的产品面验证）。
    const auto backward = namePort.resolveObjectIdFromRuntimeName(entries.front().fullName);
    ASSERT_TRUE(backward.has_value());
    EXPECT_TRUE(*backward == entries.front().objectId);

    // 未知名如实空（ARC-04——不猜测不伪造）。
    EXPECT_EQ(namePort.resolveObjectIdFromRuntimeName("NoSuch.Frame"), std::nullopt);

    // 解绑回空二态（会话拆除拍——幂等对称收口）。
    namePort.clearPresentation();
    EXPECT_FALSE(namePort.hasPresentation());
    EXPECT_EQ(namePort.resolveObjectIdFromRuntimeName(entries.front().fullName),
              std::nullopt);
}

// =====================================================================
// ACC3——呈现源折叠投影（RT-T14 工厂完整构造＋objectExists 正反例）
// =====================================================================

/** @brief MDL-07/T30：首末连杆必须解析到已编译的实际呈现帧，不能返回悬空LinkFrame别名。 */
TEST_F(HostCompilePipelineTest, NameMapPort_BoundaryLinksResolveExistingFrames_MDL07_T30)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-07"},{"AT-01"},std::nullopt);
    HostModelCompilePort compiler({m_query.get(),core::ProjectId::generate(),
        std::string(modeling::kRobotDesignObjectType),nullptr});
    ASSERT_TRUE(compiler.compileWorkCellAndDwc(makeCompileRequest()).ok);
    const auto snapshot=compiler.lastPublishedSnapshot(); ASSERT_NE(snapshot,nullptr);
    const auto view=runtime::createHostPresentationView(snapshot,m_query->view.id);
    HostRuntimeNameMapPort names; names.bindPresentation(view);
    const auto decoded=modeling::RobotDesignCodec{}.decode(m_designBytes,modeling::kCurrentFormatVersion);
    ASSERT_TRUE(decoded.ok()); const auto& robot=std::get<modeling::RobotDesign>(decoded.get());
    // 使用真实两连杆编译产物：base只落BaseFrame，末连杆只落Flange；
    // 主身份仍是LinkFrame，不能更改runtime权威来使此UI适配测试假绿。
    for(const auto& link:robot.links){
        const auto primary=view->resolveObjectId(link.objectId); ASSERT_TRUE(primary.ok());
        EXPECT_EQ(primary.get().scope,runtime::NameScope::LinkFrame);
        EXPECT_EQ(view->workCell().findFrame(primary.get().fullName),nullptr);
        const auto target=names.resolveRuntimeName(link.objectId); ASSERT_TRUE(target.has_value());
        EXPECT_NE(view->workCell().findFrame(*target),nullptr) << link.localName;
        EXPECT_EQ(names.resolveObjectIdFromRuntimeName(*target),std::optional<core::ObjectId>{link.objectId});
    }
    // 设备不是Frame，不得把机器人身份改选成BaseMount；普通关节保持原名。
    for(const auto id:{m_rootId,robot.joints.front().objectId}){
        const auto primary=view->resolveObjectId(id); ASSERT_TRUE(primary.ok());
        EXPECT_EQ(names.resolveRuntimeName(id),std::optional<std::string>{primary.get().fullName});
    }
    names.clearPresentation(); EXPECT_FALSE(names.resolveRuntimeName(robot.links.front().objectId));
}

/// 折叠投影面：真实快照→投影完整（三类身份有效＋载体非空＋objectExists
/// 对映射内对象 true、映射外 false）；无快照＝诚实 nullopt。
TEST_F(HostCompilePipelineTest, PresentationSource_FoldsCompleteProjection)
{
    HostModelCompilePort port(HostModelCompilePort::Deps{
        m_query.get(), core::ProjectId::generate(),
        std::string(modeling::kRobotDesignObjectType), nullptr});
    ASSERT_TRUE(port.compileWorkCellAndDwc(makeCompileRequest()).ok);

    HostPresentationSource source(HostPresentationSource::Deps{
        [&port]() -> std::shared_ptr<const runtime::RuntimeSnapshot> {
            return port.lastPublishedSnapshot();
        },
        nullptr,   // studio 可空（headless——挂接对象对空宿主诚实失败）
        nullptr});

    const ui::PresentationEventFacts facts{
        ui::PresentationEventKind::Publish,
        core::ProjectId::generate(),
        m_query->view.id,
        port.lastPublishedSnapshot()->modelIdentity(),
    };
    const auto projection = source.fetchPresentation(facts);
    ASSERT_TRUE(projection.has_value());
    EXPECT_TRUE(projection->isComplete()) << "完整构造前置——桥侧最后防线";
    // 三类身份同源对账（modelIdentity 与事件事实一致；appliedRevision 与
    // 工厂入参一致；presentationIdentity 非零）。
    EXPECT_TRUE(projection->modelIdentity == facts.modelIdentity);
    EXPECT_TRUE(projection->appliedRevisionId == facts.appliedRevision);
    EXPECT_TRUE(projection->presentationIdentity.isValid());

    // objectExists 正反例（映射内对象 true；随机身份 false——存在性判定
    // 与名称映射同源的契约面）。
    const auto& entries = port.lastPublishedSnapshot()->nameMap().entries();
    EXPECT_TRUE(projection->objectExists(entries.front().objectId));
    EXPECT_FALSE(projection->objectExists(core::ObjectId::generate()));

    // 无快照面：清空供数缝——诚实 nullopt（construct-failed 轨的输入）。
    HostPresentationSource emptySource(HostPresentationSource::Deps{
        []() -> std::shared_ptr<const runtime::RuntimeSnapshot> { return nullptr; },
        nullptr, nullptr});
    EXPECT_EQ(emptySource.fetchPresentation(facts), std::nullopt);
}

/// 挂接对象面：空宿主＝apply 诚实失败（网关"失败保持原状"事务语义的
/// 输入面——headless 可驱）。
TEST_F(HostCompilePipelineTest, PresentationObject_ApplyWithoutStudioFailsHonestly)
{
    HostModelCompilePort port(HostModelCompilePort::Deps{
        m_query.get(), core::ProjectId::generate(),
        std::string(modeling::kRobotDesignObjectType), nullptr});
    ASSERT_TRUE(port.compileWorkCellAndDwc(makeCompileRequest()).ok);
    const auto view = runtime::createHostPresentationView(
        port.lastPublishedSnapshot(), m_query->view.id);

    const HostWorkCellPresentationObject object(nullptr, view);
    EXPECT_FALSE(object.apply()) << "空宿主＝挂接诚实失败（不虚构挂接）";
    // 对称收口幂等（空动作——移除路径零副作用）。
    object.remove();
}

// =====================================================================
// ACC4——行程名称上下文（端口现取转发）
// =====================================================================

TEST(HostRuntimeNameContextTest, ForwardsPortBindingState)
{
    // 未绑定＝三方法如实空值（行程评估"名称不可解析"诊断轨的输入面——
    // 首编译前的诚实降级形态）。
    HostRuntimeNameMapPort namePort;
    const HostRuntimeNameContext context(&namePort);
    EXPECT_EQ(context.tryObjectId("TestRobot"), std::nullopt);
    EXPECT_EQ(context.tryRuntimeName(core::ObjectId::generate()), std::nullopt);
    EXPECT_FALSE(context.nameMapContentIdentity().isValid());

    // 空指针＝装配违约 fail-fast。
    EXPECT_THROW((void)(HostRuntimeNameContext(nullptr)), std::invalid_argument);
}

// =====================================================================
// UI-T78——发布模型几何呈现分组校准（WP-10-T05 阶段 B 渲染面：
// RT-T15 图元消费覆盖／重编译确定性＋零残留机制／分组绑定）
// =====================================================================

/// 双关节本地设计编码（渲染面用例供数——关节原点带平移使连杆跨度非零，
/// RT-T15 零跨度诚实缺省纪律下才有圆柱图元产生；夹具 1 关节零跨度模型
/// 只产法兰标记盒，两族图元各至少其一由本设计覆盖）。
std::vector<std::uint8_t> encodeTwoJointDesignBytes()
{
    modeling::RobotDesign design;
    design.displayName = "CalibRobot";

    // 两个旋转关节：原点沿 X 各平移 0.3/0.25 m（单位 m——跨度真值）。
    // 关节轴 Z（rad 制限位 0..1——与夹具同构的最小合法申报面）。
    const double kJointOffsets[2] = {0.3, 0.25};
    for (int j = 0; j < 2; ++j) {
        modeling::JointEntry joint;
        joint.objectId = core::ObjectId::generate();
        joint.localName = "J" + std::to_string(j + 1);
        joint.type = modeling::JointType::Revolute;
        joint.axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(0.0, 0.0, 1.0),
            core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        joint.origin = core::SourcedValue<modeling::JointPose>::provided(
            modeling::JointPose{rw::math::Transform3D<>(
                rw::math::Vector3D<double>(kJointOffsets[j], 0.0, 0.0),
                rw::math::Rotation3D<>::identity())},
            core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        joint.bounds = core::SourcedValue<modeling::JointLimits>::provided(
            modeling::JointLimits{0.0, 1.0},  // rad——单位显式（§2.5 纪律）
            core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        design.joints.push_back(std::move(joint));
    }
    // 连杆＝关节数+1（I-MDL-1 计数关系）：base/link1（各挂圆柱——子关节
    // 平移非零）＋link2（末连杆＝法兰——标记盒）。
    const char* kLinkNames[3] = {"base", "link1", "link2"};
    for (const char* name : kLinkNames) {
        modeling::LinkEntry link;
        link.objectId = core::ObjectId::generate();
        link.localName = name;
        design.links.push_back(std::move(link));
    }

    modeling::RobotDesignCodec codec;
    auto encoded = codec.encode(modeling::ObjectVariant{std::move(design)},
                                modeling::kCurrentFormatVersion);
    EXPECT_TRUE(encoded.ok()) << "本地设计编码失败：" << encoded.error().detail;
    return encoded.ok() ? std::move(encoded.get()) : std::vector<std::uint8_t>{};
}

/// 渲染面用例的编译供给（本地设计→真实十段链发布→RT-T14 工厂唯一入口
/// 构造呈现视图——与产品呈现半区同路径取已发布 WC；返回视图持快照存活期，
/// 调用方持句柄即持 WC）。
runtime::HostPresentationViewHandle compileLocalDesignView(
    const std::vector<std::uint8_t>& designBytes)
{
    // 独立存储替身（夹具 m_query 是成员——本用例自建保持用例自包含）。
    auto query = std::make_shared<MemoryQueryPort>();
    query->view.id = core::RevisionId::generate();
    query->view.branch = core::BranchId::generate();
    query->view.seq = 1;
    const core::ObjectId rootId = core::ObjectId::generate();
    query->addObject(rootId, std::string(modeling::kRobotDesignObjectType),
                     {0x01, 0x02});

    std::vector<project::ObjectWrite> writes;
    project::ObjectWrite write;
    write.objectId = core::ObjectId{rootId};
    write.objectTypeToken = std::string(modeling::kRobotDesignObjectType);
    write.payloadCanonical = designBytes;
    writes.push_back(std::move(write));

    HostModelCompilePort port(HostModelCompilePort::Deps{
        query.get(), core::ProjectId::generate(),
        std::string(modeling::kRobotDesignObjectType), nullptr});
    const project::CompileRequest request{*query, writes, query->view.id};
    const project::CompileResult result = port.compileWorkCellAndDwc(request);
    if (!result.ok) {
        for (const auto& diag : result.diagnostics) {
            std::cerr << "  [diag] " << diag.code << ": " << diag.cause << "\n";
        }
    }
    EXPECT_TRUE(result.ok) << "本地设计编译应发布（渲染面供数前置）";
    // RT-T14 工厂唯一入口（D10——零第二构造路径；归属修订＝编译基线）。
    return runtime::createHostPresentationView(port.lastPublishedSnapshot(),
                                               query->view.id);
}

/// 渲染面·覆盖：真实编译产物（RT-T15 图元挂接后）的校准清单恰好覆盖全部
/// 发布模型 drawable（对象几何逐一在册＋帧绑定），且两族图元（圆柱/盒）
/// 各至少其一在册——UI-T78 渲染面消费 RT-T15 产物的具名自证
/// （acceptance 1 渲染面；ird_gates 无关——纯呈现消费面）。
TEST_F(HostCompilePipelineTest,
       PublishedModelGeometry_CalibrationPlanCoversCompiledPrimitives_UI_T78)
{
    IRD_TEST_INFO("UX-11", {"AT-23"}, std::nullopt);

    // 真实编译＋RT-T14 工厂路径取 WC（与产品呈现半区同源——快照存活期
    // 随视图句柄）。
    const auto view = compileLocalDesignView(encodeTwoJointDesignBytes());
    ASSERT_NE(view, nullptr);
    const rw::models::WorkCell::Ptr workcell = view->hostPresentationWorkCell();
    ASSERT_FALSE(workcell.isNull()) << "呈现视图应携带已发布 WC（渲染面供数）";

    // 独立遍历（与被测规划器同规则——框架 updateSceneGraph 契约；此处
    // 重走一遍作为对照基准，两清单应逐元素相等）。
    std::vector<std::string> expectedNames;
    std::size_t cylinders = 0;  // 圆柱图元计数（RT-T15 连杆族——在册性断言）
    std::size_t boxes = 0;      // 标记盒图元计数（RT-T15 法兰/TCP/场景族）
    for (const auto& object : workcell->getObjects()) {
        ASSERT_FALSE(object.isNull());
        for (const auto& geometry : object->getGeometry(workcell->getDefaultState())) {
            ASSERT_FALSE(geometry.isNull());
            // RT-T15 载体纪律：图元必绑帧（随帧位姿/拾取回映射的前提）。
            EXPECT_TRUE(geometry->getFrame() != nullptr)
                << "图元 " << geometry->getName() << " 未绑定帧（载体形态违约）";
            expectedNames.push_back(geometry->getName());
            const auto type = geometry->getGeometryData()->getType();
            if (type == rw::geometry::GeometryData::CylinderPrim) { ++cylinders; }
            if (type == rw::geometry::GeometryData::BoxPrim) { ++boxes; }
        }
    }

    // 被测规划半区：清单恰好覆盖（无遗漏无多出——分组校准的作用面即
    // 发布模型 drawable 全集）。
    const std::vector<std::string> plan = collectPublishedModelDrawableNames(*workcell);
    ASSERT_EQ(plan.size(), expectedNames.size())
        << "校准清单与发布模型 drawable 全集不等（覆盖面失真）";
    for (std::size_t i = 0; i < expectedNames.size(); ++i) {
        EXPECT_EQ(plan[i], expectedNames[i])
            << "第 " << i << " 项 drawable 名失真（框架落景同名契约）";
    }
    // 两族图元各至少其一（本设计：2 圆柱——base/link1 非零跨度；1 盒——
    // 法兰标记盒）：渲染面消费 RT-T15 图元的产物面自证。
    EXPECT_GE(cylinders, std::size_t{1}) << "连杆圆柱图元未入册（RT-T15 消费缺口）";
    EXPECT_GE(boxes, std::size_t{1}) << "标记盒图元未入册（RT-T15 消费缺口）";
}

/// 渲染面·确定性＋零残留机制：同模型两次编译——校准清单逐元素相等
/// （NFR-COR-01 同模型同图元的呈现侧对账面）且产物 WC 为新实例（框架
/// setWorkcell 按对象身份识别换 WC→场景节点树整体重建＝发布/重编译/
/// 项目切换三拍零残留的机制依据；具名用例，acceptance 1 生命周期半区）。
TEST_F(HostCompilePipelineTest,
       PublishedModelGeometry_CalibrationDeterministicAcrossRecompile_UI_T78)
{
    IRD_TEST_INFO("UX-11", {}, std::nullopt);

    const auto designBytes = encodeTwoJointDesignBytes();
    const auto firstView = compileLocalDesignView(designBytes);
    ASSERT_NE(firstView, nullptr);
    const rw::models::WorkCell::Ptr firstWc = firstView->hostPresentationWorkCell();
    ASSERT_FALSE(firstWc.isNull());
    const std::vector<std::string> firstPlan = collectPublishedModelDrawableNames(*firstWc);
    ASSERT_FALSE(firstPlan.empty()) << "首编译产物应携带图元（渲染面供数）";

    // 重编译＝同模型再次发布（产品重编译拍的链路面——新快照新 WC）。
    const auto secondView = compileLocalDesignView(designBytes);
    ASSERT_NE(secondView, nullptr);
    const rw::models::WorkCell::Ptr secondWc = secondView->hostPresentationWorkCell();
    ASSERT_FALSE(secondWc.isNull());
    const std::vector<std::string> secondPlan = collectPublishedModelDrawableNames(*secondWc);

    // 确定性：两次编译产物图元清单逐元素相等（同模型同图元——呈现分组
    // 校准的输入稳定面，NFR-COR-01）。
    ASSERT_EQ(firstPlan.size(), secondPlan.size());
    for (std::size_t i = 0; i < firstPlan.size(); ++i) {
        EXPECT_EQ(firstPlan[i], secondPlan[i])
            << "重编译后第 " << i << " 项 drawable 名漂移（确定性违约）";
    }
    // 零残留机制：重编译产物 WC 是新实例（指针不同——两快照同时存活，
    // 地址比较有效）。框架 RobWorkStudio::setWorkcell 的 identity 检查据
    // 此走"关旧开新"路径，WorkCellScene::setWorkCell 整树重建（旧 drawable
    // 全部随旧节点树销毁＝发布/重编译/项目切换三拍零残留）。
    EXPECT_NE(firstWc.get(), secondWc.get())
        << "重编译产物应为新 WC 实例（换 WC 整树重建的机制前提）";
}

/// 渲染面·分组绑定：呈现分组常量钉扎为 DrawableNode::Physical——发布
/// 模型 drawable 归"物理对象"渲染组管辖（UX-11 第 6 项渲染分组显隐对
/// 本批 drawable 生效的机制绑定；产品主相机缺省掩码含 Physical 组＝
/// 缺省可见，具名用例，acceptance 3 清单边界半区）。
TEST_F(HostCompilePipelineTest,
       PublishedModelGeometry_CalibrationGroupBindingIsPhysical_UI_T78)
{
    IRD_TEST_INFO("UX-11", {}, std::nullopt);

    // 单一书写点钉扎：分组常量＝Physical（"场景中的物理对象"组）。改值
    // 必须同步契约头 kPublishedModelRenderGroup 注释的机理登记（缺省
    // 掩码面/与骨架 Virtual 组的分层关系）——本断言即防"顺手改组"。
    EXPECT_EQ(kPublishedModelRenderGroup,
              static_cast<int>(rw::graphics::DrawableNode::Physical))
        << "发布模型呈现分组漂移——渲染分组显隐管辖面随之漂移，须修订登记";
}

}  // namespace
}  // namespace sdurws::ird::ui::test

namespace sdurws::ird::ui::test {
namespace {
/** @brief PM-04/AT-21：没有权威读取装配时不能把旧草稿直接换成新修订锚。 */
TEST(HostModelingConflict, MissingRevisionReaderRejectsWithoutReanchoring_PM04_AT21)
{
    IRD_TEST_INFO("PM-04",{"AT-21"},std::nullopt);
    auto domain=modeling::createModelingPluginAssembly();domain.seedTemplateSession();
    const auto branch=core::BranchId::generate();const auto base=core::RevisionId::generate();
    domain.bindSessionAnchor(branch,base);auto& source=domain.modelingDraftSource();
    const auto before=source.buildDraftDocument();
    EXPECT_THROW(source.rebuildOnRevision(core::RevisionId::generate().toCanonical()),std::runtime_error);
    EXPECT_EQ(source.buildDraftDocument().payload,before.payload);
    EXPECT_EQ(source.buildDraftDocument().baseRevisionId,base)<<"未完整读取权威工作集不得宣称已重建";
}

/** @brief PM-04/AT-21：完整基线替换与待迁移草稿原子保留，错误回调不得污染旧值。 */
TEST_F(HostCompilePipelineTest, ConflictRebuildReadsCompleteFixedRevisionAndRetainsOldDraft_PM04_AT21)
{
    IRD_TEST_INFO("PM-04",{"AT-21"},std::nullopt);
    // 原S5夹具基线是故意不可解码的旧占位字节，由plannedWrites覆盖；
    // 本测试读取已保存修订，必须登记真实Codec根，不能把S5覆盖前置套到重开。
    m_query->view.objectRefs.clear();m_query->objects.clear();
    m_query->addObject(m_rootId,std::string(modeling::kRobotDesignObjectType),m_designBytes);
    auto domain=modeling::createModelingPluginAssembly();domain.seedTemplateSession();
    const auto branch=m_query->view.branch;domain.bindSessionAnchor(branch,core::RevisionId::generate());
    auto& source=domain.modelingDraftSource();const auto old=source.buildDraftDocument();
    domain.bindRevisionDraftReader([&](const core::BranchId& b,const core::RevisionId& r){
        return readModelingRevisionDraft(*m_query,b,r);
    });
    source.rebuildOnRevision(m_query->view.id.toCanonical());
    const auto actual=source.buildDraftDocument();
    const auto expected=readModelingRevisionDraft(*m_query,branch,m_query->view.id);ASSERT_TRUE(expected);
    EXPECT_EQ(actual.payload,expected->payload);EXPECT_EQ(actual.baseRevisionId,m_query->view.id);
    ASSERT_TRUE(domain.retainedConflictDraft());EXPECT_EQ(domain.retainedConflictDraft()->payload,old.payload);
    EXPECT_EQ(domain.retainedConflictDraft()->baseRevisionId,old.baseRevisionId);
    EXPECT_FALSE(domain.module->buildDraftCommand(modeling::kModuleHandle))<<"新基线不能继承旧编辑摘要";
    // 已有待迁移素材也属于失败原子性；空返回只代表明确无根，不能由错字节推导。
    for(int fault=0;fault<5;++fault){
        domain.bindRevisionDraftReader([&,fault](const core::BranchId&,const core::RevisionId&)
            ->std::optional<DraftDocumentProjection>{
            if(fault==0){throw std::runtime_error("实际读取失败");}
            auto invalid=*expected;
            if(fault==1){invalid.moduleId="requirements";}
            if(fault==2){invalid.baseRevisionId=core::RevisionId::generate();}
            if(fault==3){invalid.payload="broken-frame";}
            if(fault==4){invalid=old;invalid.baseRevisionId=m_query->view.id;}
            return invalid;
        });
        EXPECT_THROW(source.rebuildOnRevision(m_query->view.id.toCanonical()),std::runtime_error)<<fault;
        EXPECT_EQ(source.buildDraftDocument().payload,actual.payload);
        EXPECT_EQ(domain.retainedConflictDraft()->payload,old.payload);
        EXPECT_EQ(domain.retainedConflictDraft()->baseRevisionId,old.baseRevisionId);
    }
    EXPECT_THROW(source.rebuildOnRevision("bad-revision"),std::invalid_argument);
    domain.onSessionDetached();EXPECT_FALSE(domain.retainedConflictDraft());
}

/** @brief PM-04：固定修订无根是合法空态；跨分支、多个根或坏字节必须定位失败。 */
TEST_F(HostCompilePipelineTest, RevisionDraftReaderDistinguishesEmptyFromCorruption_PM04_AT21)
{
    IRD_TEST_INFO("PM-04",{"AT-21"},std::nullopt);
    m_query->view.objectRefs.clear();m_query->objects.clear();
    m_query->addObject(m_rootId,std::string(modeling::kRobotDesignObjectType),m_designBytes);
    const auto original=m_query->view;const auto originalBytes=m_query->objects;
    EXPECT_THROW(readModelingRevisionDraft(*m_query,core::BranchId::generate(),original.id),std::runtime_error);
    m_query->view.objectRefs.clear();EXPECT_FALSE(readModelingRevisionDraft(*m_query,original.branch,original.id));
    auto domain=modeling::createModelingPluginAssembly();domain.seedTemplateSession();
    domain.bindSessionAnchor(original.branch,core::RevisionId::generate());const auto old=domain.modelingDraftSource().buildDraftDocument();
    domain.bindRevisionDraftReader([&](const core::BranchId& b,const core::RevisionId& r){return readModelingRevisionDraft(*m_query,b,r);});
    domain.modelingDraftSource().rebuildOnRevision(original.id.toCanonical());
    EXPECT_EQ(domain.modelingDraftSource().buildDraftDocument().baseRevisionId,original.id);
    EXPECT_FALSE(domain.module->buildDraftCommand(modeling::kModuleHandle));
    ASSERT_TRUE(domain.retainedConflictDraft());EXPECT_EQ(domain.retainedConflictDraft()->payload,old.payload);
    m_query->view=original;m_query->view.objectRefs.push_back(original.objectRefs.front());
    EXPECT_THROW(readModelingRevisionDraft(*m_query,original.branch,original.id),std::runtime_error);
    m_query->view=original;for(auto& item:m_query->objects){item.second={0xff};}
    EXPECT_THROW(readModelingRevisionDraft(*m_query,original.branch,original.id),std::runtime_error);
    m_query->objects=originalBytes;
}

/** @brief MDL-17/AT-04：真实编译WC上恢复关节，独立核对末端矩阵；非法请求不改State。 */
TEST_F(HostCompilePipelineTest, NamedPoseStagesRealStateAndRejectsAtomically_MDL17_AT04)
{
    IRD_TEST_INFO("MDL-17",{},std::nullopt);
    HostModelCompilePort port(HostModelCompilePort::Deps{m_query.get(),core::ProjectId::generate(),std::string(modeling::kRobotDesignObjectType),nullptr});
    ASSERT_TRUE(port.compileWorkCellAndDwc(makeCompileRequest()).ok);
    const auto snapshot=port.lastPublishedSnapshot();ASSERT_NE(snapshot,nullptr);
    auto view=runtime::createHostPresentationView(snapshot,m_query->view.id);
    auto cell=view->hostPresentationWorkCell();
    const auto device=cell->findDevice(snapshot->model().chain().deviceName);
    ASSERT_FALSE(device.isNull());
    auto state=cell->getDefaultState();
    modeling::ModelingSessionPoseRequest request;
    request.branch=snapshot->branch();request.rootObjectId=m_rootId;
    for(const auto& joint:snapshot->model().chain().joints){request.jointIds.push_back(joint.objectId);}
    request.jointConfiguration={0.5};std::string reason;
    ASSERT_TRUE(stageModelingSessionPose(request,*snapshot,*cell,state,reason))<<reason;
    EXPECT_DOUBLE_EQ(device->getQ(state)[0],0.5);
    const auto end=rw::kinematics::Kinematics::worldTframe(device->getEnd(),state);
    EXPECT_NEAR(end.R()(0,0),std::cos(0.5),1e-12);
    EXPECT_NEAR(end.R()(1,0),std::sin(0.5),1e-12);
    const auto accepted=device->getQ(state);
    for(int mode=0;mode<5;++mode){
        auto invalid=request;
        if(mode==0){invalid.branch=core::BranchId::generate();}
        if(mode==1){invalid.rootObjectId=core::ObjectId::generate();}
        if(mode==2){invalid.jointIds[0]=core::ObjectId::generate();}
        if(mode==3){invalid.jointConfiguration[0]=2.0;} // 原夹具限位[0,1] rad。
        if(mode==4){invalid.jointConfiguration[0]=std::numeric_limits<double>::quiet_NaN();}
        EXPECT_FALSE(stageModelingSessionPose(invalid,*snapshot,*cell,state,reason))<<mode;
        EXPECT_FALSE(reason.empty());EXPECT_EQ(device->getQ(state),accepted);
    }
}

} // namespace
} // namespace sdurws::ird::ui::test

namespace sdurws::ird::ui::test {
namespace {
/** @brief MDL-17：零偏已折叠、Fixed不占q、移动轴m单位，独立几何参照核对。 */
TEST_F(HostCompilePipelineTest, NamedPoseMixedChainKeepsZeroOffsetAndFixedSemantics_MDL17_AT04)
{
    IRD_TEST_INFO("MDL-17",{},std::nullopt);
    modeling::RobotDesignCodec codec;
    const auto decoded=codec.decode(m_designBytes,modeling::kCurrentFormatVersion);ASSERT_TRUE(decoded.ok());
    auto design=std::get<modeling::RobotDesign>(decoded.get());
    design.joints[0].zeroOffset=0.2; // rad，编译桥折叠入origin，恢复q_rw需为0.6-0.2。
    const auto provided=core::ValueProvenance::make(core::ProvenanceKind::UserProvided);
    auto fixed=design.joints[0];fixed.objectId=core::ObjectId::generate();fixed.localName="fixed";
    fixed.type=modeling::JointType::Fixed;fixed.zeroOffset=0;
    fixed.bounds=core::SourcedValue<modeling::JointLimits>::notProvided();
    modeling::JointPose fixedPose(rw::math::Transform3D<>(rw::math::Vector3D<>(0,0,0.2),rw::math::Rotation3D<>::identity()));
    fixed.origin=core::SourcedValue<modeling::JointPose>::provided(fixedPose,provided);
    auto slide=design.joints[0];slide.objectId=core::ObjectId::generate();slide.localName="slide";
    slide.type=modeling::JointType::Prismatic;slide.zeroOffset=0;
    slide.axis=core::SourcedValue<rw::math::Vector3D<>>::provided({1,0,0},provided);
    slide.bounds=core::SourcedValue<modeling::JointLimits>::provided({-0.5,0.5},provided);
    design.joints.push_back(fixed);design.joints.push_back(slide);
    for(const auto* name:{"fixed-link","slide-link"}){
        auto link=design.links.back();link.objectId=core::ObjectId::generate();link.localName=name;
        design.links.push_back(link);
    }
    const auto encoded=codec.encode(modeling::ObjectVariant{design},modeling::kCurrentFormatVersion);
    ASSERT_TRUE(encoded.ok());m_designBytes=encoded.get();
    HostModelCompilePort port({m_query.get(),core::ProjectId::generate(),std::string(modeling::kRobotDesignObjectType),nullptr});
    ASSERT_TRUE(port.compileWorkCellAndDwc(makeCompileRequest()).ok);
    const auto snapshot=port.lastPublishedSnapshot();
    const auto view=runtime::createHostPresentationView(snapshot,m_query->view.id);
    const auto cell=view->hostPresentationWorkCell();auto state=cell->getDefaultState();
    const auto device=cell->findDevice(snapshot->model().chain().deviceName);
    modeling::ModelingSessionPoseRequest request;
    request.branch=snapshot->branch();request.rootObjectId=m_rootId;
    for(const auto& joint:design.joints){request.jointIds.push_back(joint.objectId);}
    request.jointConfiguration={0.4,0.0,0.1};std::string reason;
    ASSERT_TRUE(stageModelingSessionPose(request,*snapshot,*cell,state,reason))<<reason;
    ASSERT_EQ(device->getQ(state).size(),2U);
    const auto end=rw::kinematics::Kinematics::worldTframe(device->getEnd(),state);
    EXPECT_NEAR(end.P()[0],0.1*std::cos(0.6),1e-12);
    EXPECT_NEAR(end.P()[1],0.1*std::sin(0.6),1e-12);EXPECT_NEAR(end.P()[2],0.2,1e-12);
    const auto previous=device->getQ(state);
    request.jointConfiguration[2]=0.8;
    EXPECT_FALSE(stageModelingSessionPose(request,*snapshot,*cell,state,reason));
    EXPECT_EQ(device->getQ(state),previous);
    request.jointConfiguration.pop_back();
    EXPECT_FALSE(stageModelingSessionPose(request,*snapshot,*cell,state,reason));
    EXPECT_EQ(device->getQ(state),previous);
    rw::models::WorkCell other("other");
    EXPECT_FALSE(stageModelingSessionPose(request,*snapshot,other,state,reason));
    EXPECT_EQ(device->getQ(state),previous);
}
} // namespace

/** @brief PM-04/ARC-04：回执按冻结(oid,cv)读取，HEAD前移或删除不能改变旧结果。 */
TEST_F(HostCompilePipelineTest, CommittedClosureStaysOnExactRevision_PM04_AT21)
{
    IRD_TEST_INFO("PM-04",{"AT-21"},std::nullopt);
    m_query->view.objectRefs.clear();
    m_query->addObject(m_rootId,std::string(modeling::kRobotDesignObjectType),m_designBytes);
    const auto fixed=m_query->view;
    HostRevisionClosureView closure(*m_query,fixed);
    // 不替换查询算法；仅移动端口的HEAD并提供同ID新字节，旧字节仍完整保留。
    m_query->view.id=core::RevisionId::generate();m_query->view.objectRefs.clear();
    m_query->addObject(m_rootId,std::string(modeling::kRobotDesignObjectType),{9,8,7});
    const auto old=closure.tryObject(m_rootId);ASSERT_TRUE(old);
    EXPECT_EQ(old->bytes,m_designBytes);
    m_query->view.objectRefs.clear();
    const auto byToken=closure.tryObjectByToken(modeling::kRobotDesignObjectType);
    ASSERT_TRUE(byToken);EXPECT_EQ(byToken->bytes,m_designBytes);
    EXPECT_FALSE(closure.tryObject(core::ObjectId::generate()));
}

/** @brief PM-04/08/18：真实存储+正式编译+同源回执的完整增改删、逆放与磁盘重开。 */
TEST_F(HostCompilePipelineTest, CompleteWorkingSetDiskApplyAndHistory_PM04_AT21)
{
    IRD_TEST_INFO((std::vector<std::string>{"PM-04","PM-08","PM-18","MDL-06"}),{"AT-21"});
    QTemporaryDir dir;ASSERT_TRUE(dir.isValid());
    auto opened=project::ProjectStoreFactory::createNew(
        std::filesystem::path(dir.path().toStdWString())/"complete.rwdesign","完整工作集");
    auto store=std::move(opened.store);
    auto domain=modeling::createModelingPluginAssembly();
    HostRuntimeNameMapPort nameMap;HostRuntimeNameContext runtimeNames(&nameMap);
    HostDraftAwareNameContext names({&runtimeNames,[&](const core::ObjectId& id)->std::optional<std::string>{
        std::string name;return domain.tryDraftObjectName(id,name)?std::optional<std::string>{name}:std::nullopt;
    }});
    auto evaluator=policy::makeJointLimitEvaluator();policy::SystemDefaultPolicyProvider provider;
    modeling::HandlerServices services{{evaluator.get(),&names},&provider,policy::systemDefaultPolicyObjectId(),std::nullopt};
    modeling::registerModelingCommandHandlers(store->handlerRegistry(),services);
    auto compile=std::make_unique<HostModelCompilePort>(HostModelCompilePort::Deps{
        &store->query(),store->projectId(),std::string(modeling::kRobotDesignObjectType),nullptr});
    store->commands().attachCompilePort(compile.get());
    const modeling::RobotDesignCodec codec;
    const auto hex=[&](const auto& value){
        const auto encoded=codec.encode(modeling::ObjectVariant{value},modeling::kCurrentFormatVersion);
        if(!encoded.ok()){throw std::logic_error("完整链测试编码失败："+encoded.error().detail);}
        return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(encoded.get().data()),
            static_cast<qsizetype>(encoded.get().size())).toHex());
    };
    const auto decode=[&](const QJsonValue& value){
        const auto raw=QByteArray::fromHex(value.toString().toLatin1());
        const auto decoded=codec.decode(std::vector<std::uint8_t>(raw.begin(),raw.end()),modeling::kCurrentFormatVersion);
        if(!decoded.ok()){throw std::logic_error("完整链测试读回失败："+decoded.error().detail);}
        return decoded.get();
    };
    const auto savedDoc=[&](){return QJsonDocument::fromJson(QByteArray::fromStdString(
        domain.modelingDraftSource().buildDraftDocument().payload)).object();};
    auto root=std::get<modeling::RobotDesign>(codec.decode(m_designBytes,modeling::kCurrentFormatVersion).get());
    modeling::ToolDefinition tool;tool.objectId=core::ObjectId::generate();tool.localName="gripper";
    tool.tcpList={{"tip",rw::math::Transform3D<>(rw::math::Vector3D<>(0,0,0.2)),""}};
    modeling::SceneObject scene;scene.objectId=core::ObjectId::generate();scene.localName="fixed-marker";
    scene.worldPose.P()[0]=1.2; // 世界系m，无几何固定帧不能被伪造为碰撞物。
    modeling::PoseSet poses;poses.objectId=core::ObjectId::generate();
    poses.entries={{"homeConfiguration",{0.2},"参考"}};
    modeling::DrivetrainDesign drivetrain;drivetrain.objectId=core::ObjectId::generate();
    drivetrain.ratioPerJoint={core::SourcedValue<double>::provided(100.0,
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided))};
    drivetrain.frictionPerJoint.resize(1);drivetrain.torqueLimitsPerJoint.resize(1);
    const auto setParts=[&](QJsonObject& doc){
        root.toolRefs={tool.objectId};root.defaultTcp=modeling::TcpRef{tool.objectId,"tip"};
        root.sceneRefs={scene.objectId};root.poseSetRef=poses.objectId;root.drivetrainRef=drivetrain.objectId;
        doc["root"]=hex(root);doc["tools"]=QJsonArray{hex(tool)};doc["scenes"]=QJsonArray{hex(scene)};
        doc["poses"]=hex(poses);doc["drivetrain"]=hex(drivetrain);
    };
    QJsonObject document{{"format","ird-modeling-draft"},{"version",1},{"rootId",QJsonValue(QJsonValue::Null)}};
    setParts(document);
    domain.bindSessionAnchor(store->query().head().branch,store->query().head().id);
    const auto stage=[&](QJsonObject doc){
        doc["changes"]=QJsonArray{QJsonObject{{"subject","full.apply"},{"summary","完整工作集编辑"}}};
        ui::DraftDocumentProjection frame;frame.moduleId="modeling";frame.baseRevisionId=store->query().head().id;
        frame.payload=QJsonDocument(doc).toJson(QJsonDocument::Compact).toStdString();
        domain.modelingDraftSource().adoptRestoredDocument(frame);
        // void恢复口没有返回值；先核对真实接受的五对象，不能让坏夹具沿旧好草稿误提交。
        const auto actual=savedDoc();
        for(const auto* key:{"root","tools","scenes","poses","drivetrain"}){
            EXPECT_EQ(actual[key],doc[key])<<"草稿前置未接受："<<key;
            if(actual[key]!=doc[key]){return false;}
        }
        return true;
    };
    const auto apply=[&](){
        const auto before=store->query().head();const auto envelope=domain.module->buildDraftCommand("modeling");
        if(!envelope){ADD_FAILURE()<<"草稿未生成完整命令";return project::CommandResult{};}
        auto result=store->commands().submit(*envelope);
        for(const auto& diag:result.diagnostics){std::cerr<<diag.code<<"\n";}
        EXPECT_TRUE(result.committed());
        if(result.committed()){
            EXPECT_EQ(store->query().head().seq,before.seq+1);
            acceptModelingCommittedSession(domain,store->query(),result);
            EXPECT_FALSE(domain.module->buildDraftCommand("modeling"));
        }
        return result;
    };
    // 拒绝路径使用同一个真实草稿源、命令服务和编译端口；每次检查零修订及完整草稿未丢失。
    const auto reject=[&](const QJsonObject& candidate,project::ICommandInteraction* interaction){
        if(!stage(candidate)){ADD_FAILURE()<<"拒绝用例前置未接受，禁止提交旧草稿";return project::CommandResult{};}
        const auto before=store->query().head();
        const auto frame=domain.modelingDraftSource().buildDraftDocument().payload;
        const auto envelope=domain.module->buildDraftCommand("modeling");
        if(!envelope){ADD_FAILURE()<<"拒绝场景未组装命令";return project::CommandResult{};}
        const auto result=store->commands().submit(*envelope,interaction);
        EXPECT_FALSE(result.committed());EXPECT_EQ(store->query().head().id,before.id);
        EXPECT_EQ(domain.modelingDraftSource().buildDraftDocument().payload,frame);
        return result;
    };
    // 域草稿解码已拒绝缺部件，因此命令层结构反例直接改运输槽，仍走真实project入口。
    ASSERT_TRUE(stage(document));auto validEnvelope=*domain.module->buildDraftCommand("modeling");
    auto incomplete=*modeling::tryDecodeCommandPayload(validEnvelope.payloadCanonical);incomplete.objects.pop_back();
    auto badEnvelope=validEnvelope;badEnvelope.payloadCanonical=modeling::encodeCommandPayload(incomplete);
    const auto initial=store->query().head();const auto initialFrame=domain.modelingDraftSource().buildDraftDocument().payload;
    const auto bad=store->commands().submit(badEnvelope);
    EXPECT_EQ(bad.status.rejection,project::CommandStatus::Rejection::InvalidPayload);
    EXPECT_EQ(store->query().head().id,initial.id);
    EXPECT_EQ(domain.modelingDraftSource().buildDraftDocument().payload,initialFrame);
    auto invalidRoot=root;invalidRoot.joints.front().type=modeling::JointType::Continuous;
    invalidRoot.joints.front().bounds=core::SourcedValue<modeling::JointLimits>{}; // Continuous不能携有限bounds。
    auto hard=document;hard["root"]=hex(invalidRoot);
    const auto hardResult=reject(hard,nullptr);
    EXPECT_EQ(hardResult.status.rejection,project::CommandStatus::Rejection::HardAssertFailed);
    class Decline final:public project::ICommandInteraction {
    public:
        int calls=0; ///< 确认端口唯一替身，数值与策略判定均走产品实现。
        bool isAlive()const override{return true;} ///< 本轮同步交互存活。
        std::optional<std::vector<core::ConfirmationCredential>> requestConfirmations(
            const std::vector<core::ConfirmableFinding>& findings)override
        {++calls;EXPECT_FALSE(findings.empty());return std::nullopt;} ///< 模拟用户明确拒绝。
    } decline;
    auto wideRoot=root;wideRoot.joints.front().bounds=core::SourcedValue<modeling::JointLimits>::provided(
        {0.0,40.0},core::ValueProvenance::make(core::ProvenanceKind::UserProvided)); // rad，超过缺省整周阈值。
    auto wide=document;wide["root"]=hex(wideRoot);
    const auto declined=reject(wide,&decline);
    EXPECT_EQ(declined.status.rejection,project::CommandStatus::Rejection::ConfirmationsRejected);EXPECT_EQ(decline.calls,1);
    auto unavailableRoot=root;modeling::ResourceRef resource;resource.resourceId="missing-mesh";
    resource.externalRecord=modeling::ExternalResourceRecord{dir.path().toStdString()+"/missing.stl",{}};
    unavailableRoot.resourceManifest={resource};auto unavailableScene=scene;
    unavailableScene.geometry=modeling::GeometryRef{"missing-mesh"};
    auto unavailable=document;unavailable["root"]=hex(unavailableRoot);unavailable["scenes"]=QJsonArray{hex(unavailableScene)};
    const auto compileFailed=reject(unavailable,nullptr);
    EXPECT_TRUE(compileFailed.status.failed());EXPECT_FALSE(compileFailed.diagnostics.empty());
    EXPECT_FALSE(compile->lastPublishedSnapshot()); // 实际io/编译失败不能发布伪成功快照。
    ASSERT_TRUE(stage(document));const auto firstResult=apply();ASSERT_TRUE(firstResult.committed());
    const auto stale=store->commands().submit(validEnvelope);
    EXPECT_EQ(stale.status.rejection,project::CommandStatus::Rejection::StaleRevision);
    EXPECT_EQ(store->query().head().id,*firstResult.newRevision);
    // PM-04/AT-29：真实拒绝后详情必须保留原base，并取目标分支权威tip；
    // 不能让控制器拿着空tip重建或把旧草稿自动迁到HEAD。
    ui::CommandResultProjection rejected;
    rejected.rejectionReason="stale-revision";
    auto rejectedDraft=domain.modelingDraftSource().buildDraftDocument();
    EXPECT_EQ(rejectedDraft.branchId,validEnvelope.branch);
    rejectedDraft.baseRevisionId=*validEnvelope.expectedRevision; // 对应实际被拒的旧信封。
    const auto staleProjection=projectModelingCommandResult(store->query(),
        rejectedDraft,rejected);
    ASSERT_TRUE(staleProjection.staleDetail);
    EXPECT_EQ(staleProjection.staleDetail->draftBaseRevision,validEnvelope.expectedRevision->toCanonical());
    EXPECT_EQ(staleProjection.staleDetail->currentTipRevision,firstResult.newRevision->toCanonical());
    EXPECT_FALSE(staleProjection.staleDetail->advancedSummary.empty());
    EXPECT_FALSE(staleProjection.staleDetail->involvedObjectNames.empty());
    auto invalidDraft=rejectedDraft;invalidDraft.branchId=core::BranchId::generate();
    EXPECT_THROW(projectModelingCommandResult(store->query(),invalidDraft,rejected),std::runtime_error);
    invalidDraft=rejectedDraft;invalidDraft.baseRevisionId={};
    EXPECT_THROW(projectModelingCommandResult(store->query(),invalidDraft,rejected),std::invalid_argument);
    invalidDraft=rejectedDraft;invalidDraft.baseRevisionId=core::RevisionId::generate();
    EXPECT_THROW(projectModelingCommandResult(store->query(),invalidDraft,rejected),std::exception);
    // 非Stale不需要文档身份，也不允许顺手触发完整修订读取或携冲突详情。
    auto otherResult=rejected;otherResult.rejectionReason="not-writable";
    EXPECT_FALSE(projectModelingCommandResult(store->query(),{},otherResult).staleDetail);
    otherResult.status=ui::CommandResultProjection::Status::Committed;
    otherResult.newRevision=firstResult.newRevision;
    const auto successProjection=projectModelingCommandResult(store->query(),{},otherResult);
    EXPECT_EQ(successProjection.newRevision,firstResult.newRevision);EXPECT_FALSE(successProjection.staleDetail);
    const auto first=store->query().head();HostRevisionClosureView firstClosure(store->query(),first);
    document=savedDoc();root=std::get<modeling::RobotDesign>(decode(document["root"]));
    tool=std::get<modeling::ToolDefinition>(decode(document["tools"].toArray().at(0)));
    scene=std::get<modeling::SceneObject>(decode(document["scenes"].toArray().at(0)));
    poses=std::get<modeling::PoseSet>(decode(document["poses"]));
    drivetrain=std::get<modeling::DrivetrainDesign>(decode(document["drivetrain"]));
    ASSERT_TRUE(root.defaultTcp);EXPECT_EQ(root.defaultTcp->toolOid,tool.objectId);
    const auto firstToolId=tool.objectId;const auto firstPoseId=poses.objectId;
    EXPECT_EQ(root.toolRefs.front(),tool.objectId);EXPECT_EQ(root.poseSetRef,poses.objectId);
    ASSERT_TRUE(compile->lastPublishedSnapshot());EXPECT_EQ(compile->lastPublishedSnapshot()->model().tools().size(),1U);
    // 第二次仅修改已有部件，正式身份/对象集数量必须保持，不能再创建临时副本。
    tool.displayName="修改后的夹爪";setParts(document);ASSERT_TRUE(stage(document));
    ASSERT_TRUE(apply().committed());EXPECT_EQ(store->query().head().objectRefs.size(),first.objectRefs.size());
    document=savedDoc();EXPECT_EQ(std::get<modeling::ToolDefinition>(decode(document["tools"].toArray().at(0))).objectId,firstToolId);
    // 移除活动引用保留历史字节，之后重新创建第二个位姿集/传动也不能触发多活动对象错误。
    root.toolRefs.clear();root.defaultTcp.reset();root.sceneRefs.clear();root.poseSetRef.reset();root.drivetrainRef.reset();
    document["root"]=hex(root);document["tools"]=QJsonArray{};document["scenes"]=QJsonArray{};
    document["poses"]=QJsonValue(QJsonValue::Null);document["drivetrain"]=QJsonValue(QJsonValue::Null);
    ASSERT_TRUE(stage(document));ASSERT_TRUE(apply().committed());const auto removed=store->query().head();
    EXPECT_EQ(removed.objectRefs.size(),first.objectRefs.size());EXPECT_TRUE(compile->lastPublishedSnapshot()->model().tools().empty());
    document=savedDoc();tool.objectId=core::ObjectId::generate();scene.objectId=core::ObjectId::generate();
    poses.objectId=core::ObjectId::generate();drivetrain.objectId=core::ObjectId::generate();
    setParts(document);ASSERT_TRUE(stage(document));ASSERT_TRUE(apply().committed());
    const auto second=store->query().head();document=savedDoc();
    const auto secondPoses=std::get<modeling::PoseSet>(decode(document["poses"]));
    EXPECT_NE(secondPoses.objectId,firstPoseId);EXPECT_EQ(second.objectRefs.size(),first.objectRefs.size()+4U);
    auto undone=store->undoRedo().undo(second.branch);ASSERT_TRUE(undone.committed());
    acceptModelingCommittedSession(domain,store->query(),undone);
    EXPECT_TRUE(savedDoc()["poses"].isNull());EXPECT_TRUE(savedDoc()["tools"].toArray().isEmpty());
    auto redone=store->undoRedo().redo(second.branch);ASSERT_TRUE(redone.committed());
    acceptModelingCommittedSession(domain,store->query(),redone);
    EXPECT_EQ(std::get<modeling::PoseSet>(decode(savedDoc()["poses"])).objectId,secondPoses.objectId);
    // PM-18/B4c-3a：新完整命令已留下两份历史单例，现在真实执行旧五token。
    // 不只测prepare；每次都经项目事务/正式编译及同源权威回执，再逐命令撤销/重做。
    const std::vector<std::string_view> legacyTokens={modeling::kCmdApplyRobotDesign,
        modeling::kCmdApplyToolDefinition,modeling::kCmdApplySceneObjects,
        modeling::kCmdApplyNamedPoses,modeling::kCmdApplyDrivetrainDesign};
    const auto submitLegacy=[&](std::string_view token,const modeling::CommandPayload& payload){
        auto envelope=validEnvelope; // 请求信封不分配命令身份，持久取号仍唯一归project。
        envelope.branch=store->query().head().branch;envelope.expectedRevision=store->query().head().id;
        envelope.commandType=std::string(token);envelope.payloadCanonical=modeling::encodeCommandPayload(payload);
        const auto result=store->commands().submit(envelope);
        EXPECT_TRUE(result.committed())<<std::string(token);
        if(result.committed()){acceptModelingCommittedSession(domain,store->query(),result);}
        return result.committed();
    };
    for(std::size_t i=0;i<legacyTokens.size();++i){
        SCOPED_TRACE(std::string(legacyTokens[i]));const auto current=savedDoc();
        const char* keys[]={"root","tools","scenes","poses","drivetrain"};
        const auto part=(i==1||i==2)?current[keys[i]].toArray().at(0):current[keys[i]];
        auto value=decode(part);
        if(i==0){std::get<modeling::RobotDesign>(value).notes="旧根命令仍可编辑";}
        if(i==1){std::get<modeling::ToolDefinition>(value).displayName="旧工具命令仍可编辑";}
        if(i==2){std::get<modeling::SceneObject>(value).worldPose.P()[1]=0.35;}
        if(i==3){std::get<modeling::PoseSet>(value).entries={{"inspection",{0.35},"旧命令用户姿态"}};}
        if(i==4){std::get<modeling::DrivetrainDesign>(value).ratioPerJoint[0]=
            core::SourcedValue<double>::provided(120.0,core::ValueProvenance::make(core::ProvenanceKind::UserProvided));}
        modeling::PayloadObjectSlot slot;slot.allocateNew=false;slot.objectTypeToken=
            std::string(i==0?modeling::kRobotDesignObjectType:i==1?modeling::kToolDefinitionObjectType:
                i==2?modeling::kSceneObjectObjectType:i==3?modeling::kNamedPoseSetObjectType:modeling::kRobotDrivetrainObjectType);
        slot.objectId=i==0?*core::ObjectId::tryFromCanonical(current["rootId"].toString().toStdString()):
            std::visit([](const auto& typed)->core::ObjectId{
                using T=std::decay_t<decltype(typed)>;
                if constexpr(std::is_same_v<T,modeling::RobotDesign>){return {};}
                else{return typed.objectId;}
            },value);
        const auto encoded=codec.encode(value,modeling::kCurrentFormatVersion);ASSERT_TRUE(encoded.ok());
        slot.objectBytes=encoded.get();modeling::CommandPayload edit;edit.objects={slot};
        ASSERT_TRUE(submitLegacy(legacyTokens[i],edit));const auto after=savedDoc();
        // project现行§5.5按tip.inverse撤销；连续undo会撤销上一条逆命令，
        // 不能把它当作未声明的多级游标栈。逐条严格核对完整值，保留真实旧token历史面。
        const auto result=store->undoRedo().undo(second.branch);ASSERT_TRUE(result.committed());
        acceptModelingCommittedSession(domain,store->query(),result);EXPECT_EQ(savedDoc(),current);
        const auto replay=store->undoRedo().redo(second.branch);ASSERT_TRUE(replay.committed());
        acceptModelingCommittedSession(domain,store->query(),replay);EXPECT_EQ(savedDoc(),after);
    }
    // 旧历史根槽只携字节，需从当前固定修订找回第一份已不活动的四类部件。
    const auto oldRoot=firstClosure.tryObjectByToken(modeling::kRobotDesignObjectType);ASSERT_TRUE(oldRoot);
    modeling::PayloadObjectSlot rootHistory;rootHistory.allocateNew=false;
    rootHistory.objectTypeToken=std::string(modeling::kRobotDesignObjectType);
    rootHistory.objectId=*core::ObjectId::tryFromCanonical(savedDoc()["rootId"].toString().toStdString());
    rootHistory.objectBytes=oldRoot->bytes;modeling::CommandPayload restore;
    restore.mode=modeling::CommandPayload::Mode::Restore;restore.objects={rootHistory};
    for(const auto token:legacyTokens){
        const auto before=savedDoc();ASSERT_TRUE(submitLegacy(token,restore));
        EXPECT_EQ(std::get<modeling::PoseSet>(decode(savedDoc()["poses"])).objectId,firstPoseId);
        EXPECT_EQ(std::get<modeling::ToolDefinition>(decode(savedDoc()["tools"].toArray().at(0))).objectId,firstToolId);
        // 数据恢复不等于正式编译快照恢复；旧named-poses族逆槽若改变运行时根，
        // 也必须双编译，不能沿其纯位姿正向命令的false位跳过真实运动学/三维输入。
        ASSERT_TRUE(compile->lastPublishedSnapshot());ASSERT_EQ(compile->lastPublishedSnapshot()->model().tools().size(),1U);
        EXPECT_EQ(compile->lastPublishedSnapshot()->model().tools().front().objectId,firstToolId)<<std::string(token);
        const auto restored=savedDoc();
        auto inverse=store->undoRedo().undo(second.branch);ASSERT_TRUE(inverse.committed());
        acceptModelingCommittedSession(domain,store->query(),inverse);EXPECT_EQ(savedDoc(),before);
        EXPECT_EQ(compile->lastPublishedSnapshot()->model().tools().front().objectId,
            std::get<modeling::ToolDefinition>(decode(before["tools"].toArray().at(0))).objectId)<<std::string(token);
        auto replay=store->undoRedo().redo(second.branch);ASSERT_TRUE(replay.committed());
        acceptModelingCommittedSession(domain,store->query(),replay);EXPECT_EQ(savedDoc(),restored);
        inverse=store->undoRedo().undo(second.branch);ASSERT_TRUE(inverse.committed());
        acceptModelingCommittedSession(domain,store->query(),inverse);EXPECT_EQ(savedDoc(),before);
    }
    const auto oldTool=firstClosure.tryObject(firstToolId);ASSERT_TRUE(oldTool);
    EXPECT_TRUE(std::get<modeling::ToolDefinition>(codec.decode(oldTool->bytes,modeling::kCurrentFormatVersion).get()).displayName.empty());
    // B4c-3b-1：HEAD已多次前进，重新编辑仍必须读指定的首个完整修订，
    // 原最终草稿保留原base；不能从最新HEAD偷读，也不能给旧内容直接换锚。
    const auto beforeRebuild=domain.modelingDraftSource().buildDraftDocument();
    const auto finalHead=store->query().head();
    domain.bindRevisionDraftReader([&](const core::BranchId& b,const core::RevisionId& r){
        return readModelingRevisionDraft(store->query(),b,r);
    });
    domain.modelingDraftSource().rebuildOnRevision(first.id.toCanonical());
    const auto firstDraft=readModelingRevisionDraft(store->query(),first.branch,first.id);ASSERT_TRUE(firstDraft);
    EXPECT_EQ(domain.modelingDraftSource().buildDraftDocument().payload,firstDraft->payload);
    ASSERT_TRUE(domain.retainedConflictDraft());EXPECT_EQ(domain.retainedConflictDraft()->payload,beforeRebuild.payload);
    EXPECT_EQ(domain.retainedConflictDraft()->baseRevisionId,beforeRebuild.baseRevisionId);
    EXPECT_EQ(store->query().head().id,finalHead.id)<<"重建不产生项目修订，也不重试提交";
    HostRevisionClosureView finalClosure(store->query(),finalHead);std::string receiptError;
    const auto finalRoot=core::ObjectId::tryFromCanonical(savedDoc()["rootId"].toString().toStdString());ASSERT_TRUE(finalRoot);
    ASSERT_TRUE(domain.acceptAppliedSession(finalClosure,*finalRoot,finalHead.branch,finalHead.id,receiptError))<<receiptError;
    // PM-04/B4c-3d：另一个编辑入口真实更新场景，域不能依赖本次onCommitted
    // 回执。只收到项目修订事件时也须通过已装配reader读完整根/活动闭包。
    {
        const auto before=domain.modelingDraftSource().buildDraftDocument();
        const auto parts=savedDoc()["scenes"].toArray();ASSERT_FALSE(parts.isEmpty());
        auto changed=std::get<modeling::SceneObject>(decode(parts.at(0)));
        changed.worldPose.P()[2]=0.628; // 世界系m，刻意区别已有修订。
        const auto bytes=codec.encode(modeling::ObjectVariant{changed},modeling::kCurrentFormatVersion);ASSERT_TRUE(bytes.ok());
        modeling::CommandPayload update;update.objects.push_back({false,changed.objectId,
            std::string(modeling::kSceneObjectObjectType),bytes.get()});
        auto request=validEnvelope;request.branch=finalHead.branch;request.expectedRevision=finalHead.id;
        request.commandType=std::string(modeling::kCmdApplySceneObjects);
        request.payloadCanonical=modeling::encodeCommandPayload(update);
        const auto result=store->commands().submit(request);ASSERT_TRUE(result.committed());
        const auto tip=store->query().head();
        const auto expected=readModelingRevisionDraft(store->query(),tip.branch,tip.id);ASSERT_TRUE(expected);
        EXPECT_EQ(domain.modelingDraftSource().buildDraftDocument().payload,before.payload);
        domain.onRevisionCommitted(tip.branch,tip.id);
        EXPECT_EQ(domain.modelingDraftSource().buildDraftDocument().payload,expected->payload);
        EXPECT_EQ(domain.modelingDraftSource().buildDraftDocument().baseRevisionId,tip.id);
        EXPECT_FALSE(domain.hasPendingDraft());EXPECT_EQ(store->query().head().id,tip.id);
    }
    const auto finalFrame=domain.modelingDraftSource().buildDraftDocument().payload;
    const auto path=store->canonicalPath();const auto finalRevision=store->query().head();
    EXPECT_EQ(store->requestClose(),0U);compile.reset();store.reset();
    project::OpenStoreRequest request;request.path=path;request.mode=project::OpenMode::ReadOnly;
    auto reopened=project::ProjectStoreFactory::open(request);store=std::move(reopened.store);
    HostRevisionClosureView restoredClosure(store->query(),store->query().head());
    const auto restoredRoot=core::ObjectId::tryFromCanonical(savedDoc()["rootId"].toString().toStdString());ASSERT_TRUE(restoredRoot);
    auto restored=modeling::createModelingPluginAssembly();std::string error;
    ASSERT_TRUE(restored.restoreCommittedSession(restoredClosure,*restoredRoot,finalRevision.branch,finalRevision.id,error))<<error;
    EXPECT_EQ(restored.modelingDraftSource().buildDraftDocument().payload,finalFrame);
    modeling::registerModelingCommandHandlers(store->handlerRegistry(),services);
    auto readOnlyAttempt=validEnvelope;readOnlyAttempt.branch=finalRevision.branch;readOnlyAttempt.expectedRevision=finalRevision.id;
    const auto readOnly=store->commands().submit(readOnlyAttempt);
    EXPECT_EQ(readOnly.status.rejection,project::CommandStatus::Rejection::NotWritable);
    EXPECT_EQ(restored.modelingDraftSource().buildDraftDocument().payload,finalFrame);
    EXPECT_EQ(store->query().head().id,finalRevision.id); // 重开浏览零项目修订。
}
} // namespace sdurws::ird::ui::test
