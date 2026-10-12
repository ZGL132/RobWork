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

#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/modeling/Codec.hpp>          // RobotDesignCodec/kCurrentFormatVersion（真实编码）
#include <sdurws/ird/modeling/ObjectTypes.hpp>    // kRobotDesignObjectType（token 单一权威）
#include <sdurws/ird/modeling/RobotDesign.hpp>    // RobotDesign/JointEntry/LinkEntry 值模型
#include <sdurws/ird/project/CommandService.hpp>  // project::CompileRequest/ObjectWrite（S5 编排输入）
#include <sdurws/ird/project/QueryPort.hpp>       // project::IProjectQueryPort（替身契约形状）
#include <sdurws/ird/runtime/HostPresentationView.hpp>  // createHostPresentationView（真实呈现视图）
#include <sdurws/ird/runtime/Snapshot.hpp>
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

/// 编译成功面：计划闭包→十段链发布→快照缓存可取（名称映射非空——S8
/// 产物随快照在位；编译基线对账面回读一致）。
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
