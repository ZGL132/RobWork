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

}  // namespace
}  // namespace sdurws::ird::ui::test
