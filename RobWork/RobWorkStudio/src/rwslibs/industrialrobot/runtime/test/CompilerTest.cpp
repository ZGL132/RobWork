/**
 * @file   CompilerTest.cpp
 * @brief  RT-T11 十段编译链产品实现的单元测试——compile() 整体事务
 *         （S1～S10）、事务状态机全转移、取消（D-11）、资源摘要复查
 *         （§5.4/§8.6）、CM-0 防混入与编译只读。
 *
 * 设计依据：
 *   - units/runtime.md §5.1～§5.7（十段链/事务/取消/复查/可重入）、§8.6
 *     （资源规则总表）、§11 验证矩阵（RT-CPX 全组/RT-RES-1/2/RT-CONT-1/2
 *     ——用例名逐条对应）；§5.3（事务状态机——rollback 幂等/发布后无失败
 *     路径，经 CompileTransaction 单元级用例钉住）
 *   - 任务契约 tasks/foundation/RT-T11.json（acceptance 1～4 逐条对应具名
 *     用例或执行证据，见各 TEST 注释）
 *
 * 替身边界（RT-STUB-0）：只替身注入接口（对象字节/闭包/reader/资源/
 * 取消令牌——modeling/project/io/execution 的阶段 B 职责）；RobWork 本体
 * 与被测编译器（CanonicalModelCompiler 产品实现）不做替身。所有替身并发
 * 只读安全不在此验证（单线程使用；§5.5 注入接口约定由实现方保证）。
 *
 * 集成模式专用（TARGET sdurw_kinematics 条件增列）——被测面链接真实框架库
 * （S6/S7 编译器消费 rw/rwsim 非模板类；冒烟模式本文件不进构建）。
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/runtime/BaseWorldTransform.hpp>  // installationPresetRotation（§6.2 冻结矩阵对照）
#include <sdurws/ird/runtime/CanonicalModel.hpp>
#include <sdurws/ird/runtime/Compiler.hpp>
#include <sdurws/ird/runtime/Description.hpp>
#include <sdurws/ird/runtime/Errors.hpp>
#include <sdurws/ird/runtime/Resource.hpp>
#include <sdurws/ird/runtime/Snapshot.hpp>  // RuntimeSnapshotFactory（工厂×产品编译器组合）
#include <sdurws/ird/runtime/Sources.hpp>

// 产品实现私有头（测试与 src/ 同权——CMake 已建 include 路径；R-2 只禁
// 跨单元私有头）＋夹具（确定性 id/摘要工具）。
#include "CompilerImpl.hpp"
#include "CanonicalModelFixture.hpp"

namespace {

using namespace sdurws::ird::runtime;
using sdurws::ird::runtime::testfixture::digestOf;
using sdurws::ird::runtime::testfixture::idFrom;
using sdurws::ird::runtime::testfixture::val;
namespace core = sdurws::ird::core;

// =====================================================================
// 测试替身（§11 设施清单同款命名；只覆盖注入接口——RT-STUB-0）。
// =====================================================================

/// 对字节向量求 SHA-256（确定性——S2 摘要复核与资源摘要的申报值来源）。
core::Digest256 digestBytes(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester d;
    d.update(bytes.data(), bytes.size());
    return d.finalize();
}

/**
 * @brief 内存对象库＋修订闭包来源（适配 project ②端口的测试替身）。
 *
 * 读写纪律（RT-CONT-2 的被观测面）：本替身对外只暴露 const 只读接口
 * （IObjectBytesSource/IRevisionClosureSource 零写方法——类型层证据）；
 * 注入反例（闭包拒绝/字节损坏）经公开的编程成员在编译前设置，编译期间
 * 不再修改。全部对象字节一致（同 robot 字节）但摘要逐项登记——S2 只读
 * robot-design 对象字节，其余对象仅参与闭包校验。
 */
class InMemoryRevisionSource final : public IObjectBytesSource,
                                     public IRevisionClosureSource {
public:
    /// 闭包条目（id/token/cv/digest/bytes 一体登记——一致性由构造方保证）。
    struct Item {
        core::ObjectId id;
        std::string token;
        core::ContentVersion cv;
        core::Digest256 digest;
        std::vector<std::uint8_t> bytes;
    };

    std::vector<Item> items;                 ///< 修订可见对象全集（objectRefs 序）
    core::RevisionId revision;               ///< 本库登记的修订
    std::uint64_t seq = 0;                   ///< 修订序号
    core::BranchId branch;                   ///< 所属分支
    std::optional<core::ObjectId> rejectInClosure; ///< 编程：该 (oid,cv) 闭包判定为 false
                                                   ///<  （RT-CONT-1 注入点）
    bool hideRevision = false;               ///< 编程：tryRevision 未命中（违约轨观测）

    // ---- IObjectBytesSource（§3.3——nullopt＝不可得）----
    std::optional<std::vector<std::uint8_t>>
        tryObjectBytes(core::ObjectId objectId, core::ContentVersion version) const override
    {
        for (const Item& it : items) {
            if (it.id == objectId && it.cv == version) {
                return it.bytes;  // 值拷贝返回（optional<vector>——§3.3 所有权）
            }
        }
        return std::nullopt;
    }

    // ---- IRevisionClosureSource（§3.3）----
    std::optional<RevisionSummary> tryRevision(core::RevisionId rev) const override
    {
        if (hideRevision || !(rev == revision)) {
            return std::nullopt;  // 修订不存在（归属表：S1 UnknownObject）
        }
        RevisionSummary s;
        s.id = revision;
        s.seq = seq;
        s.branch = branch;
        for (const Item& it : items) {
            ObjectRefEntry e;
            e.objectId = it.id;
            e.contentVersion = it.cv;
            e.objectTypeToken = it.token;
            e.digest = it.digest;
            s.objectRefs.push_back(e);
        }
        return s;
    }

    bool objectInRevision(core::RevisionId rev, core::ObjectId objectId,
                          core::ContentVersion version) const override
    {
        if (rev != revision) {
            return false;
        }
        if (rejectInClosure.has_value() && objectId == *rejectInClosure) {
            return false;  // RT-CONT-1 注入：引用不在目标修订闭包
        }
        for (const Item& it : items) {
            if (it.id == objectId && it.cv == version) {
                return true;
            }
        }
        return false;
    }
};

/**
 * @brief 脚本化 reader 替身（modeling IRobotDesignReader 的阶段 A 替身）：
 *        返回预置 Description；可编程失败码或 bad_alloc（RT-CPX-4 注入面）。
 */
class ScriptedReader final : public IRobotDesignReader {
public:
    explicit ScriptedReader(RobotDesignDescription description)
        : m_description(std::move(description))
    {
    }

    void failWith(RuntimeErrorCode code) { m_failCode = code; }
    void throwBadAlloc() { m_throwBadAlloc = true; }
    int reads() const { return m_reads; }

    Expected<RobotDesignDescription, RuntimeError>
    read(const std::vector<std::uint8_t>&, std::uint32_t) const override
    {
        m_reads++;
        if (m_throwBadAlloc) {
            throw std::bad_alloc();  // §5.5 bad_alloc 转译的注入面
        }
        if (m_failCode.has_value()) {
            return Expected<RobotDesignDescription, RuntimeError>::err(RuntimeError(
                *m_failCode, std::string{"ScriptedReader 编程失败（joints[0]）"}));
        }
        return Expected<RobotDesignDescription, RuntimeError>::ok(m_description);
    }

private:
    RobotDesignDescription m_description;            ///< 应答描述（值拷贝）
    std::optional<RuntimeErrorCode> m_failCode;      ///< 编程失败码（err 轨）
    bool m_throwBadAlloc = false;                    ///< 编程异常
    mutable int m_reads = 0;                         ///< 调用计数（单线程观测）
};

/**
 * @brief 资源提供者替身（io IRuntimeResourceProvider 阶段 A 替身）。
 *
 * 编程模型：逐资源登记"每次调用的应答字节序列"（末项重复）——RT-RES-2 的
 * "S4 后、S10 前被替换"经两元素序列表达（第 1 次返回 A〔与申报一致〕，
 * 第 2 次〔复查〕返回 B）。全局失败编程（failWith）优先于字节应答——
 * RT-RES-1 的预算面注入。
 */
class FakeResourceProvider final : public IRuntimeResourceProvider {
public:
    struct Entry {
        core::ObjectId id;
        std::vector<std::vector<std::uint8_t>> bytesByCall; ///< 至少一项；末项重复
        mutable int calls = 0;                              ///< 调用计数（单线程观测）
    };

    void program(core::ObjectId id, std::vector<std::vector<std::uint8_t>> bytesByCall)
    {
        Entry e;
        e.id = id;
        e.bytesByCall = std::move(bytesByCall);
        m_entries.push_back(std::move(e));
    }

    void failWith(RuntimeErrorCode code) { m_failCode = code; }

    int callsOf(const core::ObjectId& id) const
    {
        for (const Entry& e : m_entries) {
            if (e.id == id) {
                return e.calls;
            }
        }
        return 0;
    }

    Expected<ResourceBytes, ResourceReadError>
    tryResourceBytes(core::ObjectId resourceId) const override
    {
        // 失败编程优先（ResourceBudget 等预算面的注入通道——RT-RES-1）。
        if (m_failCode.has_value()) {
            return Expected<ResourceBytes, ResourceReadError>::err(
                ResourceReadError{*m_failCode, resourceId, std::string{"FakeResourceProvider 编程失败"}});
        }
        for (const Entry& e : m_entries) {
            if (e.id == resourceId) {
                if (e.bytesByCall.empty()) {
                    return Expected<ResourceBytes, ResourceReadError>::err(
                        ResourceReadError{RuntimeErrorCode::ResourceMissing, resourceId,
                                          std::string{"空编程序列——按缺失处理"}});
                }
                // 借持语义（P-IO-2）：缓冲由 provider 持有至析构——调用方
                // （编译链）同步消费；摘要现算（io 侧计算后传入的同款形态）。
                const std::vector<std::uint8_t>& bytes =
                    e.bytesByCall[static_cast<std::size_t>(
                        e.calls < static_cast<int>(e.bytesByCall.size())
                            ? e.calls
                            : static_cast<int>(e.bytesByCall.size()) - 1)];
                e.calls++;
                ResourceBytes b;
                b.data = bytes.data();
                b.size = bytes.size();
                b.digest = digestBytes(bytes);
                return Expected<ResourceBytes, ResourceReadError>::ok(b);
            }
        }
        // 无编程＝资源不存在（RT-RES-1 缺失面——io 检测/runtime 定位转译）。
        return Expected<ResourceBytes, ResourceReadError>::err(
            ResourceReadError{RuntimeErrorCode::ResourceMissing, resourceId,
                              std::string{"资源未登记（FakeResourceProvider）"}});
    }

private:
    std::vector<Entry> m_entries;                     ///< 编程资源集
    std::optional<RuntimeErrorCode> m_failCode;       ///< 全局失败编程（优先）
};

/**
 * @brief 脚本化取消令牌替身：第 armAt 次轮询起返回 true（§11 CancelToggle
 *        的计数化变体——段边界轮询密度的观测面，D-11 用例输入）。
 */
class ScriptedCancel final : public ICompileCancelToken {
public:
    /// @param armAt [in] 第 armAt 次 cancellationRequested() 起返回 true
    ///        （1＝立即取消；极大值＝永不取消——只观测轮询计数）。
    explicit ScriptedCancel(std::uint64_t armAt) : m_armAt(armAt) {}

    bool cancellationRequested() const override
    {
        m_polls++;
        return m_polls >= m_armAt;
    }

    std::uint64_t polls() const { return m_polls; }

private:
    std::uint64_t m_armAt;            ///< 触发阈值（只增不减——§3.3 最终性）
    mutable std::uint64_t m_polls = 0; ///< 轮询计数（单线程观测）
};

// =====================================================================
// 编译输入夹具（Harness）——确定性 id/闭包/Description 的一站式装配。
// =====================================================================

struct Harness {
    core::ProjectId project = idFrom<core::ProjectId>("rtt11-prj");
    core::BranchId branch = idFrom<core::BranchId>("rtt11-brn");
    core::RevisionId revision = idFrom<core::RevisionId>("rtt11-rev");

    core::ObjectId robot = idFrom<core::ObjectId>("rtt11-robot");
    core::ObjectId j1 = idFrom<core::ObjectId>("rtt11-j1");
    core::ObjectId j2 = idFrom<core::ObjectId>("rtt11-j2");
    core::ObjectId l0 = idFrom<core::ObjectId>("rtt11-l0");
    core::ObjectId l1 = idFrom<core::ObjectId>("rtt11-l1");
    core::ObjectId l2 = idFrom<core::ObjectId>("rtt11-l2");
    core::ObjectId res1 = idFrom<core::ObjectId>("rtt11-res1");

    std::vector<std::uint8_t> robotBytes{'r', 't', '1', '1', '-', 'r', 'd'};

    InMemoryRevisionSource store;            ///< 闭包＋对象字节来源
    RobotDesignDescription description;      ///< reader 应答（reader 由用例构造）

    /// 是否全连杆物性齐备（rich=true→hasDynamicWorkCell→S7 构造 DWC）。
    bool richPhysics = false;
};

/// 组装基础夹具（2 旋转关节＋3 连杆；物性按 richPhysics 提供或不提供）。
Harness makeHarness(bool richPhysics)
{
    Harness h;
    h.richPhysics = richPhysics;

    // ---- 对象库：robot-design＋2 joint＋3 link（token 与 CanonicalModel
    // 夹具同款约定；digest＝字节摘要——S2 完整性复核的登记值）。
    auto addItem = [&h](const core::ObjectId& id, const char* token) {
        InMemoryRevisionSource::Item it;
        it.id = id;
        it.token = token;
        core::ContentVersion cv;
        cv.bytes = digestOf(std::string{token} + "-cv");
        it.cv = cv;
        it.digest = digestBytes(h.robotBytes);
        it.bytes = h.robotBytes;
        h.store.items.push_back(std::move(it));
    };
    addItem(h.robot, kRobotDesignObjectType);
    addItem(h.j1, "joint");
    addItem(h.j2, "joint");
    addItem(h.l0, "link");
    addItem(h.l1, "link");
    addItem(h.l2, "link");
    h.store.revision = h.revision;
    h.store.seq = 3;
    h.store.branch = h.branch;

    // ---- Description（S3 合法域内的最小可编译链）----
    RobotDesignDescription d;
    d.descriptionContractVersion = 1;
    d.robotLocalName = "RT11_BOT";

    const double pi = 3.14159265358979323846;
    JointDescription jd1;
    jd1.objectId = h.j1;
    jd1.localName = "joint_1";
    jd1.type = JointType::Revolute;
    jd1.axis = rw::math::Vector3D<double>(0.0, 0.0, 1.0);
    jd1.origin = detail::identityTransform3D();
    jd1.lower = val(-2.0);
    jd1.upper = val(2.0);
    jd1.maxVelocity = val(3.0);
    jd1.maxAcceleration = val(10.0);
    d.joints.push_back(jd1);

    JointDescription jd2;
    jd2.objectId = h.j2;
    jd2.localName = "joint_2";
    jd2.type = JointType::Revolute;
    jd2.axis = rw::math::Vector3D<double>(0.0, 1.0, 0.0);
    jd2.origin = detail::identityTransform3D();
    jd2.lower = val(-pi);
    jd2.upper = val(pi);
    jd2.maxVelocity = val(2.0);
    d.joints.push_back(jd2);

    const char* linkNames[3] = {"base_link", "link_1", "link_2"};
    const core::ObjectId linkIds[3] = {h.l0, h.l1, h.l2};
    const double masses[3] = {5.0, 4.0, 3.0};
    for (int i = 0; i < 3; ++i) {
        LinkDescription ld;
        ld.objectId = linkIds[i];
        ld.localName = linkNames[i];
        if (h.richPhysics) {
            // 全物性（mass/com/inertia Provided）——hasDynamicWorkCell=true 的
            // S7 构造路径（§9.6 派生规则）；惯量为合法对角 SPD（质心系）。
            ld.mass = val(masses[i]);
            ld.centerOfMass = core::SourcedValue<rw::math::Vector3D<double>>::provided(
                rw::math::Vector3D<double>(0.0, 0.0, 0.05 * i), testfixture::userProv());
            ld.inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
                testfixture::diagInertia(0.01, 0.02, 0.03), testfixture::userProv());
        }
        d.links.push_back(ld);
    }
    d.base.preset = InstallationPresetToken::Ground;  // 默认地面（V15-04）
    h.description = std::move(d);
    return h;
}

/// 组装 CompileRequest（注入源由调用方持有——编译期生命周期保证；来源
/// 指针经对象身份取址，注入源实现内部只读）。
CompileRequest makeRequest(const Harness& h, IObjectBytesSource& objects,
                           IRevisionClosureSource& closure, IRobotDesignReader& reader)
{
    CompileRequest req;
    req.project = h.project;
    req.revision = h.revision;
    req.objects = &objects;
    req.closure = &closure;
    req.designReader = &reader;
    return req;
}

/// 便捷包装：从可变夹具取只读接口地址——store 的编程面（反例注入）在
/// 编译前完成，编译期间经只读接口消费（RT-CONT-2 纪律）。
CompileRequest makeRequest(Harness& h, ScriptedReader& reader)
{
    return makeRequest(h, h.store, h.store, reader);
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

/// 诊断集与错误码集合中是否出现某个注册码（失败终态的码面断言助手）。
bool diagnosticsContain(const CompileOutcome& out, std::string_view registry)
{
    for (const core::DiagnosticRecord& r : out.diagnostics) {
        if (r.code == registry) {
            return true;
        }
    }
    return false;
}

// =====================================================================
// 用例：S1～S10 装配完整（acceptance 3 的正向面）。
// =====================================================================

/**
 * acceptance 3（十段逐步表 S1～S10 装配完整）：rich 物性夹具经产品编译器
 * compile() 全链发布——Published、快照非空、来源定位与请求一致、模型身份
 * ＝快照身份域取值、DWC 事实＝Compiled（§9.1）、发布诊断无 error 级
 * （§9.6 组合表）。
 */
TEST(CompilerTest, CompilesTenStageChainAndPublishes_RT_T11_ACC3)
{
    Harness h = makeHarness(/*richPhysics=*/true);
    ScriptedReader reader(h.description);
    CompileRequest req = makeRequest(h, h.store, h.store, reader);

    CanonicalModelCompiler compiler;
    const CompileOutcome out = compiler.compile(req);

    ASSERT_EQ(out.status, CompileStatus::Published) << "全链应发布（诊断："
        << (out.diagnostics.empty() ? std::string{} : out.diagnostics.front().cause) << "）";
    ASSERT_NE(out.snapshot, nullptr);
    const RuntimeSnapshot& snap = *out.snapshot;
    // 来源定位（S1 锚定 → §9.1 快照身份域）与请求一致。
    EXPECT_EQ(snap.revision(), req.revision);
    EXPECT_EQ(snap.project(), h.project);
    EXPECT_EQ(snap.branch(), h.branch);
    // 模型身份＝模型内容身份（装配取值口径的端到端一致——CR-05 前提）。
    EXPECT_TRUE(snap.modelIdentity() == snap.model().contentIdentity());
    // rich 物性 → DWC 编译事实＝Compiled（S7 门控正路径）。
    EXPECT_EQ(snap.dynamicWorkCellState(), DwcSnapshotState::Compiled);
    EXPECT_TRUE(snap.capabilities().hasDynamicWorkCell);
    EXPECT_TRUE(snap.capabilities().hasFullMassInertia);
    // 发布诊断无 error 级（§9.6 组合表 Published 行——码面按 registryCode）。
    for (const core::DiagnosticRecord& r : snap.diagnostics()) {
        EXPECT_NE(r.code, std::string{registryCode(RuntimeErrorCode::InputInvalid)})
            << "发布诊断不得含 error 级稳定码";
    }
}

/**
 * §5.5 可重入与稳定性（acceptance 3 的确定性面）：同输入重复 compile 产出
 * 相同快照身份（时间戳/诊断不入身份——§9.1"不入"列；同输入同身份同名称
 * ——ARC-03/NFR-COR-02）。
 */
TEST(CompilerTest, RepeatCompileIsDeterministic_SameSnapshotIdentity)
{
    Harness h = makeHarness(true);
    ScriptedReader reader(h.description);
    CompileRequest req = makeRequest(h, h.store, h.store, reader);

    CanonicalModelCompiler compiler;
    const CompileOutcome first = compiler.compile(req);
    const CompileOutcome second = compiler.compile(req);
    ASSERT_EQ(first.status, CompileStatus::Published);
    ASSERT_EQ(second.status, CompileStatus::Published);
    ASSERT_NE(first.snapshot, nullptr);
    ASSERT_NE(second.snapshot, nullptr);
    EXPECT_TRUE(first.snapshot->snapshotIdentity() == second.snapshot->snapshotIdentity())
        << "同输入两次编译的快照身份应逐字节相等（NFR-COR-02）";
}

/**
 * §10.0 组合（工厂 create × 产品编译器）：工厂经分段入口 buildCanonicalModel
 * 取得模型后编排 S6–S10——两入口的 S1～S5 语义单一源（同模型身份），
 * 工厂路径同样发布成功。
 */
TEST(CompilerTest, FactoryWithProductCompilerPublishes_ViaSegmentEntrance)
{
    Harness h = makeHarness(true);
    ScriptedReader reader(h.description);
    CompileRequest req = makeRequest(h, h.store, h.store, reader);

    CanonicalModelCompiler compiler;
    // 分段入口：S1–S5 产物（不发布快照——§10.0 属性表）。
    const Expected<CanonicalModel, RuntimeError> segment = compiler.buildCanonicalModel(req);
    ASSERT_TRUE(segment.ok()) << "分段入口应成功：" << segment.error().what();

    // 工厂×产品编译器组合（create 内部调用 buildCanonicalModel——同一模型）。
    RuntimeSnapshotFactory factory;
    const CompileOutcome out = factory.create(req, compiler);
    ASSERT_EQ(out.status, CompileStatus::Published);
    ASSERT_NE(out.snapshot, nullptr);
    EXPECT_TRUE(out.snapshot->modelIdentity() == segment.get().contentIdentity())
        << "工厂路径与分段入口应产出同一模型内容身份（单一语义源）";
}

/**
 * §6.2/§6.3 接线（S5 装配 §6 规则——v0.7④ 的全量接线验证）：倒挂预设经
 * resolveWorldBaseTransform 唯一产生入口解析，快照 worldToBase 逐元素等于
 * 冻结矩阵 R_x(π)（P-RT-4）＋吊装平移 (0,0,2) m。
 */
TEST(CompilerTest, InvertedPresetResolvedIntoWorldTransform_S5Assembly)
{
    Harness h = makeHarness(true);
    // 编辑表示：倒挂＋吊装高度 2 m（§6.5 数值例的基座面）。
    h.description.base.preset = InstallationPresetToken::Inverted;
    h.description.base.basePosition = rw::math::Vector3D<double>(0.0, 0.0, 2.0);
    ScriptedReader reader(h.description);
    CompileRequest req = makeRequest(h, h.store, h.store, reader);

    CanonicalModelCompiler compiler;
    const CompileOutcome out = compiler.compile(req);
    ASSERT_EQ(out.status, CompileStatus::Published);

    const rw::math::Transform3D<double> t = out.snapshot->worldToBase();
    // R 逐元素＝diag(1,−1,−1)（§6.2 冻结矩阵——元素精确 {0,±1}，无舍入）。
    const rw::math::Rotation3D<double> expected = installationPresetRotation(InstallationPresetToken::Inverted);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            EXPECT_EQ(t.R()(r, c), expected(r, c)) << "R(" << r << "," << c << ")";
        }
    }
    EXPECT_DOUBLE_EQ(t.P()(0), 0.0);
    EXPECT_DOUBLE_EQ(t.P()(1), 0.0);
    EXPECT_DOUBLE_EQ(t.P()(2), 2.0);  // 吊装高度，单位 m
}

// =====================================================================
// 用例：事务状态机失败转移与回滚（acceptance 1/3——各段失败→Failed 无
// 快照；S6 之后各段在产品输入面为"实现缺陷类"不可达（§5.3 原文），其
// 转移由 RT-T07/T08 单元级用例〔RT-CPX-1 的 S7 层原子性〕与 CompileTransaction
// 状态机用例共同承载，见文末。
// =====================================================================

/**
 * S2 失败转移（reader 失败→InputInvalid 含定位）：Failed、无快照、诊断含
 * 段号与对象定位；随后同一编译器可重入成功（§5.2 S1/S2"可恢复"列——
 * 修复输入后重编译）。
 */
TEST(CompilerTest, ReaderFailureFailsWithLocationThenRetrySucceeds)
{
    Harness h = makeHarness(true);
    ScriptedReader reader(h.description);
    reader.failWith(RuntimeErrorCode::InputInvalid);
    CompileRequest req = makeRequest(h, h.store, h.store, reader);

    CanonicalModelCompiler compiler;
    const CompileOutcome out = compiler.compile(req);
    EXPECT_EQ(out.status, CompileStatus::Failed);
    EXPECT_EQ(out.snapshot, nullptr) << "失败不得发布半成品（MDL-06）";
    ASSERT_FALSE(out.diagnostics.empty());
    EXPECT_EQ(out.diagnostics.front().code,
              std::string{registryCode(RuntimeErrorCode::InputInvalid)});
    EXPECT_NE(out.diagnostics.front().cause.find("S2"), std::string::npos)
        << "失败诊断应含段号定位（§5.2 归属）";

    // 修复输入（换有效 reader）后重编译——同一编译器实例（可重入，§5.2
    // S1/S2"可恢复"列）。
    ScriptedReader fixedReader(h.description);
    CompileRequest retry = makeRequest(h, h.store, h.store, fixedReader);
    const CompileOutcome retryOut = compiler.compile(retry);
    EXPECT_EQ(retryOut.status, CompileStatus::Published) << "换有效输入后应可重试成功";
}

/**
 * RT-CONT-1（CM-0 防混入）：对象源返回不属于目标修订的 (oid,cv)——S2 全量
 * 闭包校验拒绝，Failed＋StructureInvalid，无快照。
 */
TEST(CompilerTest, ObjectOutsideClosureRejected_RT_CONT_1)
{
    Harness h = makeHarness(true);
    h.store.rejectInClosure = h.j1;  // 编程：joint_1 对象不在目标修订闭包
    ScriptedReader reader(h.description);
    CompileRequest req = makeRequest(h, h.store, h.store, reader);

    CanonicalModelCompiler compiler;
    const CompileOutcome out = compiler.compile(req);
    EXPECT_EQ(out.status, CompileStatus::Failed);
    EXPECT_EQ(out.snapshot, nullptr);
    ASSERT_FALSE(out.diagnostics.empty());
    EXPECT_EQ(out.diagnostics.front().code,
              std::string{registryCode(RuntimeErrorCode::StructureInvalid)});
    EXPECT_NE(out.diagnostics.front().cause.find("闭包"), std::string::npos)
        << "诊断应说明混入成因（CM-0 防混入）";
}

/**
 * S2 字节完整性（CM-0 的字节面）：robot-design 对象字节与修订登记摘要不符
 * ——StructureInvalid 拒绝（防替换/损坏的字节通道）。
 */
TEST(CompilerTest, ObjectBytesDigestMismatchRejected_CM0ByteFace)
{
    Harness h = makeHarness(true);
    // 损坏 robot 条目：登记摘要改为另一字节序列的摘要（字节被替换形态）。
    const std::vector<std::uint8_t> tampered{'t', 'a', 'm', 'p', 'e', 'r', 'e', 'd'};
    h.store.items[0].bytes = tampered;  // 实得字节与登记 digest 不再一致
    ScriptedReader reader(h.description);
    CompileRequest req = makeRequest(h, h.store, h.store, reader);

    CanonicalModelCompiler compiler;
    const CompileOutcome out = compiler.compile(req);
    EXPECT_EQ(out.status, CompileStatus::Failed);
    EXPECT_EQ(out.snapshot, nullptr);
    ASSERT_FALSE(out.diagnostics.empty());
    EXPECT_EQ(out.diagnostics.front().code,
              std::string{registryCode(RuntimeErrorCode::StructureInvalid)});
}

/**
 * RT-CPX-3（MDL-06 断言分域）：mass=−1（Provided 而非法）→Failed＋
 * InputInvalid 定位连杆字段（与能力缺失严格区分——RT-CPX-2 对照）。
 */
TEST(CompilerTest, MassProvidedNegativeFailsLocated_RT_CPX_3)
{
    Harness h = makeHarness(true);
    // 注入非法已提供值：连杆 1 质量 −1 kg（S3 硬校验——fieldPath 定位）。
    h.description.links[1].mass =
        core::SourcedValue<double>::provided(-1.0, testfixture::userProv());
    ScriptedReader reader(h.description);
    CompileRequest req = makeRequest(h, h.store, h.store, reader);

    CanonicalModelCompiler compiler;
    const CompileOutcome out = compiler.compile(req);
    EXPECT_EQ(out.status, CompileStatus::Failed);
    EXPECT_EQ(out.snapshot, nullptr);
    ASSERT_FALSE(out.diagnostics.empty());
    EXPECT_EQ(out.diagnostics.front().code,
              std::string{registryCode(RuntimeErrorCode::InputInvalid)});
    // 就地定位：诊断文本含字段路径 links[1].mass（ValidationIssue 契约）。
    EXPECT_NE(out.diagnostics.front().cause.find("links[1].mass"), std::string::npos)
        << "InputInvalid 应定位非法对象与字段（RT-CPX-3 断言面）";
}

/**
 * RT-CPX-2（DYN-06/MDL-06 断言分域）：全连杆物性 NotProvided——Published＋
 * hasDynamicWorkCell=false＋SkippedNoPhysics＋缺失清单非空＋警告级
 * RT-CAPABILITY-MISSING（能力缺失降级，非失败非非法——§5.6 正交表）。
 */
TEST(CompilerTest, MassNotProvidedPublishesDegraded_RT_CPX_2)
{
    Harness h = makeHarness(/*richPhysics=*/false);  // 物性全缺
    ScriptedReader reader(h.description);
    CompileRequest req = makeRequest(h, h.store, h.store, reader);

    CanonicalModelCompiler compiler;
    const CompileOutcome out = compiler.compile(req);
    ASSERT_EQ(out.status, CompileStatus::Published) << "能力缺失是降级不是失败（§5.6）";
    ASSERT_NE(out.snapshot, nullptr);
    EXPECT_FALSE(out.snapshot->capabilities().hasDynamicWorkCell);
    EXPECT_EQ(out.snapshot->dynamicWorkCellState(), DwcSnapshotState::SkippedNoPhysics);
    EXPECT_FALSE(out.snapshot->skippedDynamicObjects().empty())
        << "Skipped 应携带缺失对象清单（链序——§9.1）";
    EXPECT_TRUE(hasDiagnostic(out.snapshot->diagnostics(), "RT-CAPABILITY-MISSING"))
        << "物性缺失应有恰类警告级诊断（§9.6）";
}

/**
 * S5 段失败转移（builder 不变量——objectId ∈ objectRefs）：Description 的
 * 关节携带闭包外 ObjectId——S3 不覆盖对象身份（无 ObjectId 语义），S5
 * builder 拦截 →Failed＋StructureInvalid（段定位 S5）。
 */
TEST(CompilerTest, JointObjectIdOutsideRefsRejectedAtS5)
{
    Harness h = makeHarness(true);
    // 注入：joint_2 的对象身份不在闭包 objectRefs（builder 值级不变量面）。
    h.description.joints[1].objectId = idFrom<core::ObjectId>("rtt11-ghost-joint");
    ScriptedReader reader(h.description);
    CompileRequest req = makeRequest(h, h.store, h.store, reader);

    CanonicalModelCompiler compiler;
    const CompileOutcome out = compiler.compile(req);
    EXPECT_EQ(out.status, CompileStatus::Failed);
    EXPECT_EQ(out.snapshot, nullptr);
    ASSERT_FALSE(out.diagnostics.empty());
    EXPECT_EQ(out.diagnostics.front().code,
              std::string{registryCode(RuntimeErrorCode::StructureInvalid)});
    EXPECT_NE(out.diagnostics.front().cause.find("S5"), std::string::npos)
        << "builder 违约应在失败诊断中定位到 S5 段";
}

// =====================================================================
// 用例：取消与 bad_alloc（RT-CPX-4 / D-11——acceptance 1/2）。
// =====================================================================

/**
 * RT-CPX-4 取消分支：令牌在首个段边界轮询即置位→Cancelled（非 error 诊断
 * ——UX-03）、无快照；同请求重试（不再取消）→Published（可重入重试——
 * §5.5；无半成品残留）。
 */
TEST(CompilerTest, CancelAtStageBoundaryReturnsCancelledThenRetry_RT_CPX_4)
{
    Harness h = makeHarness(true);
    ScriptedReader reader(h.description);
    CompileRequest req = makeRequest(h, h.store, h.store, reader);

    ScriptedCancel cancel(1);  // 第 1 次轮询即取消（S1 前边界）
    req.cancel = &cancel;

    CanonicalModelCompiler compiler;
    const CompileOutcome out = compiler.compile(req);
    EXPECT_EQ(out.status, CompileStatus::Cancelled);
    EXPECT_EQ(out.snapshot, nullptr) << "取消不产生部分快照（§5.5）";
    ASSERT_FALSE(out.diagnostics.empty());
    EXPECT_EQ(out.diagnostics.front().code,
              std::string{registryCode(RuntimeErrorCode::Cancelled)});
    EXPECT_TRUE(diagnosticsContain(out, "RT-CANCELLED"));

    // 可重入重试：永不取消的同请求——同一编译器实例直接成功。
    ScriptedCancel idleCancel(std::numeric_limits<std::uint64_t>::max());
    CompileRequest retry = makeRequest(h, h.store, h.store, reader);
    retry.cancel = &idleCancel;
    const CompileOutcome retryOut = compiler.compile(retry);
    EXPECT_EQ(retryOut.status, CompileStatus::Published);
}

/**
 * RT-CPX-4 bad_alloc 分支（§5.5"内存不足→ResourceBudget＋对象清理"）：
 * reader 抛 std::bad_alloc→Failed＋ResourceBudget 诊断（S2 段异常的转译；
 * 瞬态产物由事务 RAII 清理）。
 */
TEST(CompilerTest, BadAllocTranslatedToResourceBudget_RT_CPX_4)
{
    Harness h = makeHarness(true);
    ScriptedReader reader(h.description);
    reader.throwBadAlloc();
    CompileRequest req = makeRequest(h, h.store, h.store, reader);

    CanonicalModelCompiler compiler;
    const CompileOutcome out = compiler.compile(req);
    EXPECT_EQ(out.status, CompileStatus::Failed);
    EXPECT_EQ(out.snapshot, nullptr);
    ASSERT_FALSE(out.diagnostics.empty());
    EXPECT_EQ(out.diagnostics.front().code,
              std::string{registryCode(RuntimeErrorCode::ResourceBudget)});
}

/**
 * D-11（acceptance 2）正向面：无取消令牌（cancel=nullptr＝不可取消）时
 * 编译完整执行并发布——编译器内部不存在任何超时/自取消机制（实现文件
 * 无时钟读取/无期限检查——D-11"取消只经外部请求"的静态事实，验收审查
 * 承载；本用例是其行为面：无令牌输入时编译不被中断）。
 */
TEST(CompilerTest, NoTokenCompilesToPublish_NoInternalTimeout_D11_ACC2)
{
    Harness h = makeHarness(true);
    ScriptedReader reader(h.description);
    CompileRequest req = makeRequest(h, h.store, h.store, reader);
    req.cancel = nullptr;  // 显式不可取消（§3.3 可空语义）

    CanonicalModelCompiler compiler;
    const CompileOutcome out = compiler.compile(req);
    EXPECT_EQ(out.status, CompileStatus::Published) << "无外部取消请求时编译不应被中断";
    EXPECT_NE(out.snapshot, nullptr);
}

/**
 * D-11（acceptance 2）轮询密度面：每段边界与资源逐项循环轮询取消令牌
 * （§5.5）。无资源夹具的轮询点＝S1 前、S3 前、S5 前、S6 前、S7 前、S8 前、
 * S10 前＝恰 7 次（S4 资源循环与 S10 前复查循环无资源时不产生轮询）——
 * 精确断言钉住边界密度（取消时窗的下界由轮询密度保证，TASK-01）。
 */
TEST(CompilerTest, CancelPollsEveryStageBoundary_D11PollDensity)
{
    Harness h = makeHarness(false);  // 无资源引用——轮询点计数确定
    ScriptedReader reader(h.description);
    CompileRequest req = makeRequest(h, h.store, h.store, reader);

    ScriptedCancel idleCancel(std::numeric_limits<std::uint64_t>::max());  // 永不置位
    req.cancel = &idleCancel;

    CanonicalModelCompiler compiler;
    const CompileOutcome out = compiler.compile(req);
    ASSERT_EQ(out.status, CompileStatus::Published);
    // 轮询点清单（§5.3 状态机推进序——实现 CompilerImpl.cpp 逐段注释）：
    // ①S1 前 ②S2 前 ③S3 前 ④S5 前 ⑤S6 前 ⑥S7 前 ⑦S8 前 ⑧S10 前（S4 资源
    // 循环与 S10 前复查循环为逐资源轮询——无资源时不产生）。
    EXPECT_EQ(idleCancel.polls(), 8ull) << "段边界取消轮询密度应恰为 8（无资源夹具）";
}

// =====================================================================
// 用例：资源（RT-RES-1/2——acceptance 4）。
// =====================================================================

/**
 * RT-RES-1 缺失面：清单声明资源而 provider 未登记→Failed＋ResourceMissing
 * （resourceId 定位——四类诊断严格区分的"缺失"支）。
 */
TEST(CompilerTest, ResourceMissingFailsLocated_RT_RES_1)
{
    Harness h = makeHarness(false);
    // 登记一条资源引用（Recorded；申报摘要＝A 的摘要）。
    const std::vector<std::uint8_t> meshA{'m', 'e', 's', 'h', '-', 'a'};
    ResourceRef ref;
    ref.resourceId = h.res1;
    ref.contentDigest = digestBytes(meshA);
    ref.state = ResourceState::Recorded;
    ref.accessVersion = 1;
    h.description.resourceRefs.push_back(ref);

    ScriptedReader reader(h.description);
    FakeResourceProvider provider;  // 不登记 res1——资源不存在
    CompileRequest req = makeRequest(h, h.store, h.store, reader);
    req.resources = &provider;

    CanonicalModelCompiler compiler;
    const CompileOutcome out = compiler.compile(req);
    EXPECT_EQ(out.status, CompileStatus::Failed);
    EXPECT_EQ(out.snapshot, nullptr);
    ASSERT_FALSE(out.diagnostics.empty());
    EXPECT_EQ(out.diagnostics.front().code,
              std::string{registryCode(RuntimeErrorCode::ResourceMissing)});
    EXPECT_NE(out.diagnostics.front().cause.find(h.res1.toCanonical()), std::string::npos)
        << "ResourceMissing 应定位 resourceId（§8.6 转译契约）";
}

/**
 * RT-RES-1 变化面：读取内容与清单申报摘要不符（S4 时点的替换）→Failed＋
 * ResourceChanged（"摘要不符"支——与缺失/预算严格分立）。
 */
TEST(CompilerTest, ResourceDigestMismatchAtS4Fails_RT_RES_1)
{
    Harness h = makeHarness(false);
    const std::vector<std::uint8_t> declared{'d', 'e', 'c', 'l', 'a', 'r', 'e', 'd'};
    ResourceRef ref;
    ref.resourceId = h.res1;
    ref.contentDigest = digestBytes(declared);  // 申报 A
    ref.state = ResourceState::Recorded;
    ref.accessVersion = 1;
    h.description.resourceRefs.push_back(ref);

    ScriptedReader reader(h.description);
    FakeResourceProvider provider;
    // 实得 B（与申报不符——S4 摘要复核拦截）。
    provider.program(h.res1, {{'o', 't', 'h', 'e', 'r'}});
    CompileRequest req = makeRequest(h, h.store, h.store, reader);
    req.resources = &provider;

    CanonicalModelCompiler compiler;
    const CompileOutcome out = compiler.compile(req);
    EXPECT_EQ(out.status, CompileStatus::Failed);
    EXPECT_EQ(out.snapshot, nullptr);
    ASSERT_FALSE(out.diagnostics.empty());
    EXPECT_EQ(out.diagnostics.front().code,
              std::string{registryCode(RuntimeErrorCode::ResourceChanged)});
}

/**
 * RT-RES-1 预算面：provider 报预算超限→Failed＋ResourceBudget（io 检测/
 * runtime 转译的同码透传——三码分立的第三支）。
 */
TEST(CompilerTest, ResourceBudgetFails_RT_RES_1)
{
    Harness h = makeHarness(false);
    const std::vector<std::uint8_t> meshA{'m', 'e', 's', 'h'};
    ResourceRef ref;
    ref.resourceId = h.res1;
    ref.contentDigest = digestBytes(meshA);
    ref.state = ResourceState::Recorded;
    ref.accessVersion = 1;
    h.description.resourceRefs.push_back(ref);

    ScriptedReader reader(h.description);
    FakeResourceProvider provider;
    provider.program(h.res1, {meshA});
    provider.failWith(RuntimeErrorCode::ResourceBudget);  // 预算超限编程
    CompileRequest req = makeRequest(h, h.store, h.store, reader);
    req.resources = &provider;

    CanonicalModelCompiler compiler;
    const CompileOutcome out = compiler.compile(req);
    EXPECT_EQ(out.status, CompileStatus::Failed);
    EXPECT_EQ(out.snapshot, nullptr);
    ASSERT_FALSE(out.diagnostics.empty());
    EXPECT_EQ(out.diagnostics.front().code,
              std::string{registryCode(RuntimeErrorCode::ResourceBudget)});
}

/**
 * RT-RES-2（§5.4 编译中替换）：S4 读取（第 1 次，内容 A 与申报一致）之后、
 * S10 发布之前 provider 内容被替换（第 2 次复读返回 B）——发布前摘要复查
 * 拦截：Failed＋ResourceChanged；且复读确实发生（provider 恰被调用 2 次
 * ——复查时点在 S10 前的行为证据）。
 */
TEST(CompilerTest, ResourceSwappedMidCompileFailsBeforePublish_RT_RES_2)
{
    Harness h = makeHarness(false);
    const std::vector<std::uint8_t> meshA{'m', 'e', 's', 'h', '-', 'A'};
    const std::vector<std::uint8_t> meshB{'m', 'e', 's', 'h', '-', 'B'};
    ResourceRef ref;
    ref.resourceId = h.res1;
    ref.contentDigest = digestBytes(meshA);  // 申报＝A（S4 读取时刻的内容）
    ref.state = ResourceState::Recorded;     // Recorded 参与复查（§5.4）
    ref.accessVersion = 1;
    h.description.resourceRefs.push_back(ref);

    ScriptedReader reader(h.description);
    FakeResourceProvider provider;
    provider.program(h.res1, {meshA, meshB});  // 第 1 次＝A；第 2 次（复查）＝B
    CompileRequest req = makeRequest(h, h.store, h.store, reader);
    req.resources = &provider;

    CanonicalModelCompiler compiler;
    const CompileOutcome out = compiler.compile(req);
    EXPECT_EQ(out.status, CompileStatus::Failed) << "替换窗口必须被发布前复查封闭";
    EXPECT_EQ(out.snapshot, nullptr);
    ASSERT_FALSE(out.diagnostics.empty());
    EXPECT_EQ(out.diagnostics.front().code,
              std::string{registryCode(RuntimeErrorCode::ResourceChanged)});
    EXPECT_EQ(provider.callsOf(h.res1), 2) << "S4 读取＋S10 前复查＝恰两次调用";
}

/**
 * RT-RES-2 免复查面：同一替换编程下的 Solidified 资源不受影响（项目内
 * 不可变副本的存储不可变保证——§5.4/§8.6）——Published 且复读不发生
 * （恰一次调用）。
 */
TEST(CompilerTest, SolidifiedResourceSkipsRecheck_RT_RES_2)
{
    Harness h = makeHarness(false);
    const std::vector<std::uint8_t> meshA{'m', 'e', 's', 'h', '-', 'A'};
    const std::vector<std::uint8_t> meshB{'m', 'e', 's', 'h', '-', 'B'};
    ResourceRef ref;
    ref.resourceId = h.res1;
    ref.contentDigest = digestBytes(meshA);
    ref.state = ResourceState::Solidified;  // 已固化——免复查（存储不可变）
    ref.accessVersion = 1;
    h.description.resourceRefs.push_back(ref);

    ScriptedReader reader(h.description);
    FakeResourceProvider provider;
    provider.program(h.res1, {meshA, meshB});  // 若被复读将返回 B——不应发生
    CompileRequest req = makeRequest(h, h.store, h.store, reader);
    req.resources = &provider;

    CanonicalModelCompiler compiler;
    const CompileOutcome out = compiler.compile(req);
    EXPECT_EQ(out.status, CompileStatus::Published) << "Solidified 资源免复查（§5.4）";
    EXPECT_NE(out.snapshot, nullptr);
    EXPECT_EQ(provider.callsOf(h.res1), 1) << "仅 S4 读取一次（无复查调用）";
}

/**
 * §8.6 规则总表（Recorded 不代阻断）：Recorded 资源正常编译发布，但产出
 * 警告级"正式评估前须固化"诊断（建议码 RT-RESOURCE-RECORDED——PA-1 登记
 * 面，收编前不私裁为错误码）。
 */
TEST(CompilerTest, RecordedResourceWarnsNotBlocked)
{
    Harness h = makeHarness(false);
    const std::vector<std::uint8_t> meshA{'m', 'e', 's', 'h', '-', 'A'};
    ResourceRef ref;
    ref.resourceId = h.res1;
    ref.contentDigest = digestBytes(meshA);
    ref.state = ResourceState::Recorded;
    ref.accessVersion = 1;
    h.description.resourceRefs.push_back(ref);

    ScriptedReader reader(h.description);
    FakeResourceProvider provider;
    provider.program(h.res1, {meshA, meshA});  // 两次同内容（S4＋复查一致）
    CompileRequest req = makeRequest(h, h.store, h.store, reader);
    req.resources = &provider;

    CanonicalModelCompiler compiler;
    const CompileOutcome out = compiler.compile(req);
    ASSERT_EQ(out.status, CompileStatus::Published) << "Recorded 只警告不阻断（§8.6）";
    EXPECT_TRUE(hasDiagnostic(out.diagnostics, "RT-RESOURCE-RECORDED"));
}

// =====================================================================
// 用例：编译只读与调用方违约（RT-CONT-2 / §3.4 总纲）。
// =====================================================================

/**
 * RT-CONT-2（MDL-06 编译期间不修改项目历史）：类型层——四个注入接口
 * （IObjectBytesSource/IRevisionClosureSource/IRobotDesignReader/
 * IRuntimeResourceProvider）的全部契约方法均为 const（零写方法——写路径
 * 在类型层不存在，Sources.hpp/Description.hpp/Resource.hpp 定义）；行为层
 * ——编译前后对象库全部字节哈希逐项一致且闭包判定零翻转（编译只读取）。
 */
TEST(CompilerTest, CompileLeavesObjectStoreUnchanged_RT_CONT_2)
{
    Harness h = makeHarness(true);
    ScriptedReader reader(h.description);
    CompileRequest req = makeRequest(h, h.store, h.store, reader);

    // 编译前快照：全部对象字节逐项哈希＋闭包判定的逐项真值。
    std::vector<core::Digest256> before;
    for (const InMemoryRevisionSource::Item& it : h.store.items) {
        before.push_back(digestBytes(it.bytes));
    }
    std::vector<char> closureBefore;
    for (const InMemoryRevisionSource::Item& it : h.store.items) {
        closureBefore.push_back(
            h.store.objectInRevision(h.revision, it.id, it.cv) ? 1 : 0);
    }

    CanonicalModelCompiler compiler;
    const CompileOutcome out = compiler.compile(req);
    ASSERT_EQ(out.status, CompileStatus::Published);

    // 编译后复核：字节与闭包真值逐项不变（零写路径的行为证据）。
    for (std::size_t i = 0; i < h.store.items.size(); ++i) {
        EXPECT_TRUE(digestBytes(h.store.items[i].bytes) == before[i])
            << "对象字节应编译前后一致（对象 " << i << "）";
        EXPECT_EQ(h.store.objectInRevision(h.revision, h.store.items[i].id,
                                           h.store.items[i].cv),
                  closureBefore[i] == 1)
            << "闭包判定应编译前后一致（对象 " << i << "）";
    }
}

/**
 * §3.4 总纲（调用方契约违约 fail-fast）：对不存在修订的编译请求——S1 以
 * UnknownObject 异常终止（不走诊断收集——违约轨；与工厂 create 同轨）。
 */
TEST(CompilerTest, UnknownRevisionFailsFast_UnknownObject)
{
    Harness h = makeHarness(true);
    ScriptedReader reader(h.description);
    CompileRequest req = makeRequest(h, h.store, h.store, reader);
    req.revision = idFrom<core::RevisionId>("rtt11-unknown-rev");  // 未登记修订

    CanonicalModelCompiler compiler;
    try {
        compiler.compile(req);
        ADD_FAILURE() << "对不存在修订的编译请求应 fail-fast（UnknownObject）";
    } catch (const RuntimeError& e) {
        EXPECT_EQ(e.code(), RuntimeErrorCode::UnknownObject);
    }
}

/**
 * S0 前置校验：请求未提供项目上下文（project 全零）→Failed＋InputInvalid
 * （可恢复——补值后重编译；§4.3.1 header.project 必填的请求面来源）。
 */
TEST(CompilerTest, MissingProjectContextRejected)
{
    Harness h = makeHarness(true);
    ScriptedReader reader(h.description);
    CompileRequest req = makeRequest(h, h.store, h.store, reader);
    req.project = core::ProjectId{};  // 全零——未提供项目上下文

    CanonicalModelCompiler compiler;
    const CompileOutcome out = compiler.compile(req);
    EXPECT_EQ(out.status, CompileStatus::Failed);
    EXPECT_EQ(out.snapshot, nullptr);
    ASSERT_FALSE(out.diagnostics.empty());
    EXPECT_EQ(out.diagnostics.front().code,
              std::string{registryCode(RuntimeErrorCode::InputInvalid)});
}

// =====================================================================
// 用例：事务状态机单元级（§5.3——rollback 幂等/发布后无失败路径；S6 之后
// 各段的失败在产品输入面为"实现缺陷类"不可达〔§5.3 原文〕，状态机转移的
// 完备性以本用例＋RT-T07/T08 单元级失败用例共同承载）。
// =====================================================================

TEST(CompilerTest, TransactionRollbackIsIdempotentAndPublishIsTerminal)
{
    CompileTransaction tx;
    EXPECT_EQ(tx.stage(), CompileStage::Idle);

    // 工作态推进→回滚→RolledBack（幂等：重复回滚停留 RolledBack）。
    tx.advance(CompileStage::Anchored);
    tx.advance(CompileStage::Parsed);
    tx.rollback();
    EXPECT_EQ(tx.stage(), CompileStage::RolledBack);
    tx.rollback();
    EXPECT_EQ(tx.stage(), CompileStage::RolledBack) << "重复进入回滚应幂等（§5.3）";

    // 发布后无失败路径（§5.3 原文）：Published 状态的 rollback 被拒绝。
    tx.advance(CompileStage::Published);
    EXPECT_EQ(tx.stage(), CompileStage::Published);
    tx.rollback();
    EXPECT_EQ(tx.stage(), CompileStage::Published) << "发布后的回滚不得改变终态（§5.3）";
}

}  // namespace
