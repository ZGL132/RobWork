/**
 * @file   SnapshotTest.cpp
 * @brief  RT-T09 测试——RuntimeSnapshot/工厂/物化（RT-SNAP-1～4、RT-CPX-4、
 *         CR-05 值供给样例、materialize §9.5 往返）。
 *
 * 设计依据（用例↔需求/验收标准追溯——§11 反例矩阵）：
 *   - RT-SNAP-1（NFR-COR-02/§8.7/§9.2）：多线程并发 view/nameMap 查询——
 *     零数据竞争＋makeState 各线程独立。TSAN 在本工程工具链不可用（MSVC），
 *     以"多线程并发压力＋逐线程结果与单线程期望逐一相等"的等价评审观测
 *     承载（证据形态登记 §15.4 v0.10——与 RT-AD-3 的控制块计数同款适配）。
 *   - RT-SNAP-2（ARCH §4.1/§9.2/§9.5）：主进程快照→物化字节→worker 重建
 *     ——身份逐字节相等可执行；D-13 核对（篡改预期身份→Failed＋诊断含
 *     期望/实得身份）。
 *   - RT-SNAP-3（TASK-03/AT-10/§9.3）：项目 A 快照持有下切 B——迟到反解
 *     用 A 快照映射（绝不用当前映射）；上下文关闭后的迟到请求 fail-fast
 *     拒绝（ContextReleased——§3.4 调用方契约违约轨）。
 *   - RT-SNAP-4（§9.3）：快照引用清零后再访问不可达——MSVC 无 ASAN，以
 *     weak_ptr 过期时序（引用计数生命周期）做等价观测（§15.4 v0.10 登记）。
 *   - RT-CPX-4（TASK-01/UX-03/§5.5）：取消（CancelToggle 在 S6 段边界置位）
 *     →Cancelled（取消诊断非 error）＋无半成品＋可重入重试成功；
 *     std::bad_alloc→ResourceBudget＋对象清理（RAII）；"临时目录清理失败"
 *     子项归 project 事务（§8.6 规则总表/§10.0"命令路径临时文件归 project
 *     事务"——runtime 零磁盘写，不在本测试范围，登记归属）。
 *   - CR-05（跨单元红线，tasks/foundation/RT-T09.json acceptance 2）：
 *     modelIdentity/nameMapIdentity/robworkBaselineVersion 的**值供给样例**
 *     ——快照暴露值＝模型/映射/采集点的计算值（值传递），evidence 侧
 *     EV-T04 以同一样例断言其 Environment 条目 `runtime.model-identity`/
 *     `runtime.robwork-baseline` 编码值与本测试输出值逐字节一致（evidence
 *     不重编码 CanonicalModel——units/evidence.md §4.1.1/§4.2.1）。
 *   - materialize §9.5 往返（acceptance 3）：载荷 encode→parse 逐字段相等
 *     ＋确定性＋防御拒绝（截断/坏 magic/非法载荷）。
 *   - RT-STUB-0（§11）：RobWork/rwsim 本体不做替身——快照视图/物化的全部
 *     断言针对真实基线库构造的 WorkCell/DWC；替身只覆盖注入接口
 *     （ICanonicalModelCompiler 的分段入口＝§10.0 原文的"测试替身注入点"）
 *     ——替身输出仅验证 runtime 契约，不构成 RobWork 算法正确性证明。
 *
 * 工程事实（两模式差异，CMake 同步登记）：本文件链接真实框架库（工厂/
 * 快照消费 S6/S7 编译器——rw/rwsim 非模板类），只在集成模式编译——冒烟
 * 模式不进测试目标（§15.4 v0.10 RT-T09 登记条目）。
 *
 * 确定性：夹具 id/摘要由固定种子派生（CanonicalModelFixture 同款）；无
 * 随机/locale 依赖（并发用例仅线程交织非确定——断言与交织无关）。
 */

#include <sdurws/ird/runtime/Compiler.hpp>   // 被测：CompileRequest/Outcome/分段接口
#include <sdurws/ird/runtime/Snapshot.hpp>   // 被测：RuntimeSnapshot/工厂/物化编码

#include <sdurws/ird/runtime/Adapter.hpp>             // RobWorkBaselineVersion
#include <sdurws/ird/runtime/BaseWorldTransform.hpp>  // gravityToBase（视图投影互证）
#include <sdurws/ird/runtime/Codec.hpp>               // rtcodec encode/encodeNameMap
#include <sdurws/ird/runtime/Errors.hpp>              // token/registryCode/isContractViolation
#include <sdurws/ird/runtime/NameMap.hpp>             // NameScope（反解断言）

#include <rw/kinematics/State.hpp>      // makeState 值拷贝（线程独立断言）
#include <rw/math/Q.hpp>                // 关节位写入/读回（State 线程私有断言）
#include <rw/models/SerialDevice.hpp>   // setQ/getQ（每线程私有 State 的行为断言）

#include "CanonicalModelFixture.hpp"  // minimal/rich 夹具（确定性种子）

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace sdurws::ird::runtime;
using sdurws::ird::runtime::testfixture::Fixture;
using sdurws::ird::runtime::testfixture::idFrom;
using sdurws::ird::runtime::testfixture::minimalFixture;
using sdurws::ird::runtime::testfixture::richFixture;
namespace core = sdurws::ird::core;

// =====================================================================
// 测试替身（§11 设施清单同款命名；只覆盖注入接口——RT-STUB-0）。
// =====================================================================

/**
 * @brief 取消令牌替身（§11 CancelToggle——原子置位/复位，可重入重试用）。
 * 置于 ScriptedCompiler 之前：后者的方法体引用本替身的完整类型。
 */
class CancelToggle final : public ICompileCancelToken {
public:
    void request() { m_flag.store(true); }
    void reset() { m_flag.store(false); }
    bool cancellationRequested() const override { return m_flag.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> m_flag{false}; ///< 取消状态（只增不减——§3.3 最终性）
};

/**
 * @brief 分段编译器替身（§10.0 buildCanonicalModel"测试替身注入点"）：
 *        返回预构模型或编程错误/异常；RobWork 本体不替身。
 *
 * compile()（整体事务入口）非 RT-T09 被测面（十段链归 RT-T11）——替身
 * 返回 Failed 并声明不实现（不伪造十段链语义）。
 */
class ScriptedCompiler final : public ICanonicalModelCompiler {
public:
    /// @param model        [in] S1–S5 的应答模型（值拷贝）
    /// @param contractVersion [in] 申报契约版本
    /// @param implVersion  [in] 申报实现版本
    ScriptedCompiler(CanonicalModel model, std::uint32_t contractVersion,
                     std::string implVersion)
        : m_model(std::move(model))
        , m_contractVersion(contractVersion)
        , m_implVersion(std::move(implVersion))
    {
    }

    /// 编程 buildCanonicalModel 失败：返回 err（该码）而非模型。
    void failWith(RuntimeErrorCode code) { m_errorCode = code; }
    /// 编程 buildCanonicalModel 抛 std::bad_alloc（RT-CPX-4 的注入点——
    /// §11"另注入 std::bad_alloc（替身 reader 抛）"的等价注入面）。
    void throwBadAlloc() { m_throwBadAlloc = true; }
    /// 编程：被调用时对指定令牌置位取消（模拟"CancelToggle 在 S6 段边界
    /// 置位"——buildCanonicalModel 返回后、工厂 S6 检查点之前生效）。
    /// 类型为测试内替身 CancelToggle（request() 非接口方法——编译期确定）。
    void armCancelOnCall(CancelToggle* token) { m_cancelToArm = token; }

    CompileOutcome compile(const CompileRequest&) override
    {
        // 整体事务入口＝RT-T11 交付面；替身不实现（如实返回 Failed——
        // RT-T09 用例不调用本入口，此处仅满足接口完备性）。
        CompileOutcome out;
        out.status = CompileStatus::Failed;
        return out;
    }

    Expected<CanonicalModel, RuntimeError> buildCanonicalModel(const CompileRequest&) override
    {
        if (m_cancelToArm != nullptr) {
            m_cancelToArm->request();  // 调用点置位——模拟 S5/S6 之间进入取消窗口
        }
        if (m_throwBadAlloc) {
            throw std::bad_alloc();
        }
        if (m_errorCode.has_value()) {
            return Expected<CanonicalModel, RuntimeError>::err(
                RuntimeError(*m_errorCode, std::string{"ScriptedCompiler 注入错误"}));
        }
        return Expected<CanonicalModel, RuntimeError>::ok(m_model);
    }

    std::uint32_t contractVersion() const noexcept override { return m_contractVersion; }
    std::string implementationVersion() const noexcept override { return m_implVersion; }

private:
    CanonicalModel m_model;                          ///< 应答模型
    std::uint32_t m_contractVersion = 1;             ///< 申报契约版本
    std::string m_implVersion;                       ///< 申报实现版本
    std::optional<RuntimeErrorCode> m_errorCode;     ///< 编程错误（err 轨）
    bool m_throwBadAlloc = false;                    ///< 编程异常（bad_alloc）
    CancelToggle* m_cancelToArm = nullptr;           ///< 调用时置位的取消令牌（测试替身）
};

// =====================================================================
// 快照构造便捷入口（create 成功路径；断言 Published 后返回快照）。
// =====================================================================

/// rich/minimal 夹具 → Published 快照（失败即断言失败）。
std::shared_ptr<const RuntimeSnapshot> createPublished(const CanonicalModel& model,
                                                       const CompileOptions& options,
                                                       ScriptedCompiler& compiler)
{
    CompileRequest request;
    request.revision = model.header().revision;
    request.options = options;
    const CompileOutcome out = RuntimeSnapshotFactory{}.create(request, compiler);
    EXPECT_EQ(out.status, CompileStatus::Published) << "create 应发布快照（诊断："
                                                    << (out.diagnostics.empty()
                                                            ? std::string{}
                                                            : out.diagnostics.front().cause)
                                                    << "）";
    EXPECT_NE(out.snapshot, nullptr);
    return out.snapshot;
}

/// 从快照组装 worker 物化载荷（§9.5 内容面——测试侧的"execution 派发"
/// 组装视角；RevisionSummary 从模型头投影）。
snapshotcodec::MaterializedPayload payloadFrom(const RuntimeSnapshot& snapshot,
                                               const CompileRequest& request)
{
    snapshotcodec::MaterializedPayload payload;
    payload.revision.id = snapshot.revision();
    payload.revision.seq = snapshot.revisionSeq();
    payload.revision.parent = std::nullopt;
    payload.revision.branch = snapshot.branch();
    payload.revision.objectRefs = snapshot.model().header().objectRefs;
    payload.canonicalModelBytes = rtcodec::encode(snapshot.model());
    payload.nameMapBytes = rtcodec::encodeNameMap(snapshot.nameMap());
    payload.options = request.options;
    payload.compilerContractVersion = snapshot.compilerContractVersion();
    payload.compilerVersion = snapshot.compilerVersion();
    payload.expectedModelIdentity = snapshot.modelIdentity();
    payload.expectedNameMapIdentity = snapshot.nameMapIdentity();
    return payload;
}

// =====================================================================
// RT-T09 主路径：发布字段全量核对（§9.1 字段表）＋视图面（§8.3）。
// =====================================================================

TEST(SnapshotTest, CreatePublishesSnapshotFields_RT_T09)
{
    // rich 夹具（全物性→DWC Compiled；含一条模型警告）。
    const CanonicalModel model = richFixture().build();
    ScriptedCompiler compiler(model, 42u, std::string{"test-compiler-0.9"});
    const CompileOptions options;  // 默认集（全请求）
    const auto snapshot = createPublished(model, options, compiler);
    ASSERT_NE(snapshot, nullptr);

    // ---- §9.1 字段逐项（身份四元组）----
    EXPECT_TRUE(snapshot->project() == model.header().project);
    EXPECT_TRUE(snapshot->branch() == model.header().branch);
    EXPECT_TRUE(snapshot->revision() == model.header().revision);
    EXPECT_EQ(snapshot->revisionSeq(), model.header().revisionSeq);

    // ---- 身份块（CR-05 值供给①②：与计算值同一）----
    EXPECT_TRUE(snapshot->modelIdentity() == model.contentIdentity());
    EXPECT_TRUE(snapshot->nameMapIdentity()
                == buildRuntimeNameMap(model).contentIdentity());
    EXPECT_EQ(snapshot->nameMapRuleVersion(), 1u);  // §7.2 规则表初始版本
    EXPECT_TRUE(snapshot->workCellCompileIdentity().isValid());

    // ---- DWC 事实（rich 全物性→Compiled；缺失清单恒空）----
    EXPECT_EQ(snapshot->dynamicWorkCellState(), DwcSnapshotState::Compiled);
    EXPECT_TRUE(snapshot->skippedDynamicObjects().empty());
    EXPECT_TRUE(snapshot->capabilities().hasDynamicWorkCell);

    // ---- 版本面（CR-05 值供给③④）----
    EXPECT_EQ(snapshot->compilerContractVersion(), 42u);
    EXPECT_EQ(snapshot->compilerVersion(), std::string{"test-compiler-0.9"});
    EXPECT_EQ(snapshot->robworkBaselineVersion(), RobWorkBaselineVersion::capture().text);

    // ---- 编码器版本三元组（与编码头一致——§9.1 合法列）----
    const SnapshotCodecVersions expectedCodec{rtcodec::kVersionMajor,
                                              rtcodec::kVersionMinor,
                                              rtcodec::kNameMapVersionMajor,
                                              rtcodec::kNameMapVersionMinor,
                                              snapshotcodec::kVersionMajor,
                                              snapshotcodec::kVersionMinor};
    EXPECT_TRUE(snapshot->codecVersions() == expectedCodec);

    // ---- 选项/清单/能力/诊断（§9.1"与 model 一致"列）----
    EXPECT_TRUE(snapshot->compileOptions() == options);
    ASSERT_EQ(snapshot->resourceManifest().size(), model.resourceManifest().size());
    for (std::size_t i = 0; i < model.resourceManifest().size(); ++i) {
        EXPECT_TRUE(snapshot->resourceManifest().at(i).resourceId
                    == model.resourceManifest().at(i).resourceId);
    }
    EXPECT_TRUE(snapshot->capabilities().hasFullMassInertia);
    // 诊断＝模型警告块（S7 无警告——Compiled 路径）。
    ASSERT_EQ(snapshot->diagnostics().size(), model.diagnostics().size());

    // ---- 观测性字段（不入身份——值可读）----
    EXPECT_EQ(snapshot->createdFrom(), SnapshotOrigin::Command);
    EXPECT_NE(snapshot->createdAtUtc().time_since_epoch().count(), 0);
    EXPECT_TRUE(snapshot->snapshotIdentity().isValid());

    // ---- 视图面（§8.3：model/nameMap/capabilities/workCell/重力投影/State）----
    EXPECT_GT(snapshot->nameMap().size(), 0u);
    EXPECT_GT(snapshot->workCell().frameCount(), 0u);
    // 基座—世界读取点与规则函数互证（§6.4——地面安装：R=I、g 投影恒等）。
    // 分量访问＝Vector3D::operator[]（rw 基线接口；x() 为静态单位向量）。
    const auto t = snapshot->worldToBase();
    EXPECT_DOUBLE_EQ(t.P()[0], 0.0);
    EXPECT_DOUBLE_EQ(t.P()[2], 0.0);
    const auto gBase = snapshot->gravityBase();
    EXPECT_DOUBLE_EQ(gBase[2], -9.81);  // 单位 m/s²（地面安装投影恒等）
    const auto dwc = snapshot->tryDynamicWorkCell();
    EXPECT_TRUE(dwc.ok());
    EXPECT_GT(dwc.get().bodyCount(), 0u);  // rich：3 连杆体＋1 工具体
    const auto state = snapshot->makeState();
    // State 可写入（每线程私有工作区——与设备 setQ 组合在 RT-SNAP-1 钉住）。
    EXPECT_EQ(&state != nullptr, true);
    // ⑥端口：绑定映射解析（§7.3）。
    const auto resolved =
        snapshot->nameResolver().resolveObjectId(idFrom<core::ObjectId>("j1"));
    EXPECT_TRUE(resolved.ok());
}

// =====================================================================
// 快照身份确定性（RT-ID-1 快照层）：同输入同身份；观测性不入身份。
// =====================================================================

TEST(SnapshotTest, SnapshotIdentityDeterministicAndObservabilityExcluded_RT_ID_1)
{
    const CanonicalModel model = richFixture().build();
    ScriptedCompiler compiler(model, 7u, std::string{"impl-1.0"});
    const CompileOptions options;

    // 同输入两次 create：身份逐字节相等（无环境依赖——NFR-COR-02）。
    const auto s1 = createPublished(model, options, compiler);
    const auto s2 = createPublished(model, options, compiler);
    ASSERT_NE(s1, nullptr);
    ASSERT_NE(s2, nullptr);
    EXPECT_TRUE(s1->snapshotIdentity() == s2->snapshotIdentity());
    EXPECT_TRUE(s1->workCellCompileIdentity() == s2->workCellCompileIdentity());

    // Fields 编码面：身份"入"字段扰动→身份变（§9.1"入"列的负向钉住）。
    snapshotidentity::Fields f1;
    f1.project = model.header().project;
    f1.branch = model.header().branch;
    f1.revision = model.header().revision;
    f1.revisionSeq = model.header().revisionSeq;
    f1.modelIdentity = model.contentIdentity();
    f1.nameMapIdentity = s1->nameMapIdentity();
    f1.nameMapRuleVersion = 1u;
    f1.workCellCompileIdentity = s1->workCellCompileIdentity();
    f1.dynamicWorkCellState = DwcSnapshotState::Compiled;
    f1.compilerContractVersion = 7u;
    f1.compilerVersion = "impl-1.0";
    f1.robworkBaselineVersion = RobWorkBaselineVersion::capture().text;
    f1.codecVersions = s1->codecVersions();
    f1.compileOptions = options;
    f1.resourceManifest = model.resourceManifest();
    f1.capabilities = model.capabilities();
    const core::ContentIdentity base = snapshotidentity::compute(f1);
    EXPECT_TRUE(base == s1->snapshotIdentity())
        << "Fields 组装与快照内计算应逐字节一致（同一编码域）";

    // 扰动 1：编译器实现版本（身份"入"）→身份变。
    snapshotidentity::Fields f2 = f1;
    f2.compilerVersion = "impl-1.1";
    EXPECT_FALSE(snapshotidentity::compute(f2) == base);

    // 扰动 2：revisionSeq（快照层入身份——与模型层的"不入"区分，§9.1 行 1）。
    snapshotidentity::Fields f3 = f1;
    f3.revisionSeq = f1.revisionSeq + 1;
    EXPECT_FALSE(snapshotidentity::compute(f3) == base);

    // 扰动 3：基线版本（基线变化＝产物不可比——NFR-DEP-05）→身份变。
    snapshotidentity::Fields f4 = f1;
    f4.robworkBaselineVersion = "vendored-other";
    EXPECT_FALSE(snapshotidentity::compute(f4) == base);
    // 排除面（§9.1"不入"列）：diagnostics/createdAtUtc/createdFrom 不在
    // Fields——类型层不可能进入编码域（结构层排除强于运行时排除）。
}

// =====================================================================
// WC 层缓存键公式（§9.1 workCellCompileIdentity——RT-T10 复用单点）。
// =====================================================================

TEST(SnapshotTest, WorkCellCompileIdentityFormula_RT_T09)
{
    const core::ContentIdentity mid = core::ContentIdentity::fromCanonical(
        "cid-00000000000000000000000000000000000000000000000000000000000000a1");
    const SnapshotCodecVersions codec{1, 0, 1, 0, 1, 0};
    const CompileOptions options;
    const core::ContentIdentity key1 = snapshotidentity::computeWorkCellCompileIdentity(
        mid, 1u, "impl-1.0", "vendored-b", 1u, codec, options);

    // 分量逐项扰动→键变（§9.1 公式全集，geometryDetail 为单值枚举不扰动）。
    EXPECT_FALSE(snapshotidentity::computeWorkCellCompileIdentity(
                     core::ContentIdentity::fromCanonical(
                         "cid-00000000000000000000000000000000000000000000000000000000000000a2"),
                     1u, "impl-1.0", "vendored-b", 1u, codec, options)
                 == key1)
        << "modelIdentity 变→键变";
    EXPECT_FALSE(snapshotidentity::computeWorkCellCompileIdentity(
                     mid, 2u, "impl-1.0", "vendored-b", 1u, codec, options)
                 == key1)
        << "compilerContractVersion 变→键变";
    EXPECT_FALSE(snapshotidentity::computeWorkCellCompileIdentity(
                     mid, 1u, "impl-2.0", "vendored-b", 1u, codec, options)
                 == key1)
        << "compilerVersion 变→键变";
    EXPECT_FALSE(snapshotidentity::computeWorkCellCompileIdentity(
                     mid, 1u, "impl-1.0", "vendored-b2", 1u, codec, options)
                 == key1)
        << "robworkBaselineVersion 变→键变（基线升级＝新键，§9.4）";
    EXPECT_FALSE(snapshotidentity::computeWorkCellCompileIdentity(
                     mid, 1u, "impl-1.0", "vendored-b", 2u, codec, options)
                 == key1)
        << "nameMapRuleVersion 变→键变（规则变化＝名称可能变）";
    {
        SnapshotCodecVersions codec2 = codec;
        codec2.canonicalModelMajor = 2;
        EXPECT_FALSE(snapshotidentity::computeWorkCellCompileIdentity(
                         mid, 1u, "impl-1.0", "vendored-b", 1u, codec2, options)
                     == key1)
            << "codecVersions 变→键变（编码升版＝全体身份变化）";
    }

    // D-04 分层：requestDynamicWorkCell（capabilityLevel）**不入 WC 键**。
    CompileOptions optionsDwcOff = options;
    optionsDwcOff.requestDynamicWorkCell = false;
    EXPECT_TRUE(snapshotidentity::computeWorkCellCompileIdentity(
                    mid, 1u, "impl-1.0", "vendored-b", 1u, codec, optionsDwcOff)
                == key1)
        << "requestDynamicWorkCell 只入 DWC 层键（DWC 无关子集——§9.1 公式）";

    // includeCollisionGeometry 入 WC 键（DWC 无关子集的成员面）。
    CompileOptions optionsNoCollision = options;
    optionsNoCollision.includeCollisionGeometry = false;
    EXPECT_FALSE(snapshotidentity::computeWorkCellCompileIdentity(
                     mid, 1u, "impl-1.0", "vendored-b", 1u, codec, optionsNoCollision)
                 == key1)
        << "includeCollisionGeometry 变→WC 键变";

    // 非法输入 fail-fast：全零 modelIdentity／空版本串（§9.1 合法列"非零"）。
    core::ContentIdentity zero;
    EXPECT_THROW(snapshotidentity::computeWorkCellCompileIdentity(
                     zero, 1u, "impl-1.0", "vendored-b", 1u, codec, options),
                 RuntimeError);
    EXPECT_THROW(snapshotidentity::computeWorkCellCompileIdentity(
                     mid, 1u, std::string{}, "vendored-b", 1u, codec, options),
                 RuntimeError);
}

// =====================================================================
// RT-SNAP-1：只读并发（§9.2/§8.7——等价评审观测，见文件头说明）。
// =====================================================================

TEST(SnapshotTest, ConcurrentReadOnlyView_RT_SNAP_1)
{
    const CanonicalModel model = richFixture().build();
    ScriptedCompiler compiler(model, 1u, std::string{"impl-conc"});
    const auto snapshot = createPublished(model, CompileOptions{}, compiler);
    ASSERT_NE(snapshot, nullptr);

    // 并发前单线程期望值（各线程结果必须逐一相等——与交织无关）。
    const auto expectT = snapshot->worldToBase();
    const auto expectG = snapshot->gravityBase();
    const core::ObjectId j1 = idFrom<core::ObjectId>("j1");
    const std::string j1Name = snapshot->nameResolver().resolveObjectId(j1).get().fullName;

    constexpr int kThreads = 8;
    constexpr int kIterations = 200;
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&snapshot, &expectT, &expectG, &j1Name, &failures, kIterations, t]() {
            // 每线程私有 State（§9.2——makeState 值拷贝，跨线程绝不共享）；
            // 设备视图（只读句柄——setQ 写的是 State 不是设备）。
            const auto device = snapshot->workCell().findDevice("IRB_T");
            if (device == nullptr) {
                ++failures;
                return;
            }
            // 每线程不同的关节位写入值（交织无关的可区分载荷）。
            const rw::math::Q own(2, 0.01 * (t + 1), -0.02 * (t + 1));
            rw::kinematics::State state = snapshot->makeState();
            device->setQ(own, state);  // 写本线程 State——其他线程不可见
            for (int i = 0; i < kIterations; ++i) {
                // 只读视图/映射查询与重力投影——发布后不可变值的并发只读。
                const auto resolved = snapshot->nameResolver().resolveRuntimeName(j1Name);
                if (!resolved.ok()) {
                    ++failures;
                    return;
                }
                if (!(snapshot->worldToBase().P() == expectT.P())) {
                    ++failures;
                    return;
                }
                if (!(snapshot->gravityBase() == expectG)) {
                    ++failures;
                    return;
                }
                // 线程私有 State 读回本线程写入值（未被其他线程改写——
                // "State 是唯一可变工作区且线程私有"，§8.7）。
                if (!(device->getQ(state) == own)) {
                    ++failures;
                    return;
                }
            }
        });
    }
    for (std::thread& th : threads) {
        th.join();
    }
    EXPECT_EQ(failures.load(), 0)
        << "并发只读查询全部成功且与单线程期望一致；各线程 State 写入互不干扰"
           "（等价评审观测——无 TSAN，§15.4 v0.10）";
}

// =====================================================================
// RT-SNAP-2：worker 隔离物化（§9.2/§9.5）＋D-13 拒绝。
// =====================================================================

TEST(SnapshotTest, WorkerMaterializeRoundtrip_RT_SNAP_2)
{
    const CanonicalModel model = richFixture().build();
    ScriptedCompiler compiler(model, 11u, std::string{"impl-mat"});
    CompileRequest request;
    request.revision = model.header().revision;
    const auto mainSnapshot = createPublished(model, request.options, compiler);
    ASSERT_NE(mainSnapshot, nullptr);

    // execution 派发视角：组装载荷＋确定性编码（同载荷逐字节相等）。
    const snapshotcodec::MaterializedPayload payload = payloadFrom(*mainSnapshot, request);
    const std::vector<std::uint8_t> bytes = snapshotcodec::encode(payload);
    const std::vector<std::uint8_t> bytesAgain = snapshotcodec::encode(payload);
    EXPECT_TRUE(bytes == bytesAgain) << "IRDMAT1 编码确定性（NFR-COR-02）";

    // worker 侧：materialize 重建独立快照（不共享主进程内存——进程内等价
    // 观测为"全部值经字节往返重建，无一引用主进程对象"）。
    RuntimeSnapshotFactory workerFactory;
    const CompileOutcome workerOut = workerFactory.materialize(bytes);
    ASSERT_EQ(workerOut.status, CompileStatus::Published);
    ASSERT_NE(workerOut.snapshot, nullptr);
    const auto& worker = *workerOut.snapshot;

    // 身份核对（§9.2"重建后 modelIdentity/nameMapIdentity 相等"＋更强断言：
    // snapshotIdentity 亦相等——编译器版本面随载荷值传递，"入"字段全同）。
    EXPECT_TRUE(worker.modelIdentity() == mainSnapshot->modelIdentity());
    EXPECT_TRUE(worker.nameMapIdentity() == mainSnapshot->nameMapIdentity());
    EXPECT_TRUE(worker.snapshotIdentity() == mainSnapshot->snapshotIdentity());

    // 可执行：视图查询与主进程产物同值（worldToBase/帧数/⑥端口）。
    EXPECT_TRUE(worker.worldToBase().P() == mainSnapshot->worldToBase().P());
    EXPECT_EQ(worker.workCell().frameCount(), mainSnapshot->workCell().frameCount());
    EXPECT_EQ(worker.dynamicWorkCellState(), DwcSnapshotState::Compiled);
    const core::ObjectId j1 = idFrom<core::ObjectId>("j1");
    const auto mainName = mainSnapshot->nameResolver().resolveObjectId(j1);
    const auto workerName = worker.nameResolver().resolveObjectId(j1);
    ASSERT_TRUE(mainName.ok());
    ASSERT_TRUE(workerName.ok());
    EXPECT_EQ(workerName.get().fullName, mainName.get().fullName);

    // createdFrom 标记 worker 路径（§9.1 createdFrom 枚举——不入身份，
    // 故身份相等与来源标记正交）。
    EXPECT_EQ(worker.createdFrom(), SnapshotOrigin::Worker);
    EXPECT_EQ(mainSnapshot->createdFrom(), SnapshotOrigin::Command);
}

TEST(SnapshotTest, WorkerIdentityMismatchRejected_D_13)
{
    const CanonicalModel model = richFixture().build();
    ScriptedCompiler compiler(model, 11u, std::string{"impl-mat"});
    CompileRequest request;
    request.revision = model.header().revision;
    const auto mainSnapshot = createPublished(model, request.options, compiler);
    ASSERT_NE(mainSnapshot, nullptr);

    // 篡改 1：预期 modelIdentity 换成映射身份（通道错配/错包形态）→
    // materialize Failed＋诊断含期望/实得身份（§10.0 materialize 行）。
    snapshotcodec::MaterializedPayload payload = payloadFrom(*mainSnapshot, request);
    payload.expectedModelIdentity = payload.expectedNameMapIdentity;
    RuntimeSnapshotFactory factory;
    const CompileOutcome out1 = factory.materialize(snapshotcodec::encode(payload));
    EXPECT_EQ(out1.status, CompileStatus::Failed);
    EXPECT_EQ(out1.snapshot, nullptr) << "身份核对失败不发布快照（D-13）";
    ASSERT_FALSE(out1.diagnostics.empty());
    EXPECT_NE(out1.diagnostics.front().cause.find(mainSnapshot->modelIdentity().toCanonical()),
              std::string::npos)
        << "诊断含期望身份（cid 规范文本）";
    EXPECT_NE(out1.diagnostics.front().cause.find(mainSnapshot->nameMapIdentity().toCanonical()),
              std::string::npos)
        << "诊断含实得身份（cid 规范文本）";

    // 篡改 2：模型字节被改但预期身份未同步（半传输/篡改形态）→rtcodec
    // parse 的身份复核先行拒绝（D-13 的模型层前置，Codec.hpp parse 校验链④）。
    snapshotcodec::MaterializedPayload tampered = payloadFrom(*mainSnapshot, request);
    ASSERT_FALSE(tampered.canonicalModelBytes.empty());
    tampered.canonicalModelBytes.at(tampered.canonicalModelBytes.size() - 1) ^= 0xFFu;
    const CompileOutcome out2 = factory.materialize(snapshotcodec::encode(tampered));
    EXPECT_EQ(out2.status, CompileStatus::Failed);
    EXPECT_EQ(out2.snapshot, nullptr);
}

// =====================================================================
// 物化载荷编解码面（§9.5 往返——acceptance 3）。
// =====================================================================

TEST(SnapshotTest, MaterializeCodecRoundtripAndValidation_RT_9_5)
{
    const CanonicalModel model = richFixture().build();
    ScriptedCompiler compiler(model, 3u, std::string{"impl-cdc"});
    CompileRequest request;
    request.revision = model.header().revision;
    const auto snapshot = createPublished(model, request.options, compiler);
    ASSERT_NE(snapshot, nullptr);
    const snapshotcodec::MaterializedPayload payload = payloadFrom(*snapshot, request);

    // 往返：encode→parse 逐字段相等（§9.5 内容面全集）。
    const std::vector<std::uint8_t> bytes = snapshotcodec::encode(payload);
    const auto decoded = snapshotcodec::parse(bytes);
    ASSERT_TRUE(decoded.ok()) << "载荷往返解码失败：" << decoded.error().what();
    EXPECT_TRUE(decoded.get().revision.id == payload.revision.id);
    EXPECT_EQ(decoded.get().revision.seq, payload.revision.seq);
    EXPECT_EQ(decoded.get().revision.parent.has_value(), payload.revision.parent.has_value());
    EXPECT_TRUE(decoded.get().revision.branch == payload.revision.branch);
    ASSERT_EQ(decoded.get().revision.objectRefs.size(), payload.revision.objectRefs.size());
    EXPECT_EQ(decoded.get().revision.objectRefs.front().objectTypeToken,
              payload.revision.objectRefs.front().objectTypeToken);
    EXPECT_TRUE(decoded.get().canonicalModelBytes == payload.canonicalModelBytes);
    EXPECT_TRUE(decoded.get().nameMapBytes == payload.nameMapBytes);
    EXPECT_TRUE(decoded.get().options == payload.options);
    EXPECT_EQ(decoded.get().compilerContractVersion, payload.compilerContractVersion);
    EXPECT_EQ(decoded.get().compilerVersion, payload.compilerVersion);
    EXPECT_TRUE(decoded.get().expectedModelIdentity == payload.expectedModelIdentity);
    EXPECT_TRUE(decoded.get().expectedNameMapIdentity == payload.expectedNameMapIdentity);

    // 防御拒绝 1：截断（半传输）→InputInvalid。
    const std::vector<std::uint8_t> truncated(bytes.begin(), bytes.end() - 1);
    const auto cutTail = snapshotcodec::parse(truncated);
    EXPECT_FALSE(cutTail.ok());
    EXPECT_EQ(cutTail.error().code(), RuntimeErrorCode::InputInvalid);

    // 防御拒绝 2：坏 magic（非 IRDMAT1 家族）→InputInvalid。
    std::vector<std::uint8_t> badMagic = bytes;
    badMagic.at(0) = 'X';
    EXPECT_FALSE(snapshotcodec::parse(badMagic).ok());

    // 防御拒绝 3：尾随字节（拼接错位）→InputInvalid。
    std::vector<std::uint8_t> withTail = bytes;
    withTail.push_back(0u);
    EXPECT_FALSE(snapshotcodec::parse(withTail).ok());

    // 防御拒绝 4：非法载荷（空模型字节）→encode fail-fast（NFR-COR-03）。
    snapshotcodec::MaterializedPayload empty = payload;
    empty.canonicalModelBytes.clear();
    EXPECT_THROW(snapshotcodec::encode(empty), RuntimeError);
}

// =====================================================================
// RT-SNAP-3：项目切换后的旧快照与迟到结果（§9.3/AT-10）。
// =====================================================================

TEST(SnapshotTest, StaleSnapshotSurvivesProjectSwitch_RT_SNAP_3)
{
    // 项目 A：rich 快照（运行中——r1 绑定 S_A）。
    Fixture fixtureA = richFixture();
    const CanonicalModel modelA = fixtureA.build();
    ScriptedCompiler compilerA(modelA, 1u, std::string{"impl-a"});
    const auto snapshotA = createPublished(modelA, CompileOptions{}, compilerA);
    ASSERT_NE(snapshotA, nullptr);

    // 项目 B（切换目标）：不同 project/branch/设备名——不同模型内容＝
    // 不同快照（映射独立成立，RT-NM-4 精神）。
    Fixture fixtureB = richFixture();
    fixtureB.header.project = idFrom<core::ProjectId>("prj-B");
    fixtureB.header.branch = idFrom<core::BranchId>("brn-B");
    fixtureB.header.revision = idFrom<core::RevisionId>("rev-B");
    fixtureB.chain.robotLocalName = "IRB_U";
    fixtureB.chain.deviceName = "IRB_U";
    const CanonicalModel modelB = fixtureB.build();
    ScriptedCompiler compilerB(modelB, 1u, std::string{"impl-a"});
    const auto snapshotB = createPublished(modelB, CompileOptions{}, compilerB);
    ASSERT_NE(snapshotB, nullptr);
    EXPECT_FALSE(snapshotA->nameMapIdentity() == snapshotB->nameMapIdentity());

    // 场景 1：切换后 A 的在途运行迟到——名称反解用 S_A 绑定映射（绝不用
    // 当前映射）：S_A 反解成功且名字仍是 A 的（"IRB_T.joint_1"）。
    const core::ObjectId j1 = idFrom<core::ObjectId>("j1");
    const auto staleResolve = snapshotA->nameResolver().resolveObjectId(j1);
    ASSERT_TRUE(staleResolve.ok()) << "旧快照迟到反解照常服务（§9.3 零外部依赖）";
    EXPECT_EQ(staleResolve.get().fullName, "IRB_T.joint_1");
    // 正向同名解析在两快照各自命中各自对象（映射随快照独立）。
    const auto inA = snapshotA->nameResolver().resolveRuntimeName("IRB_T.joint_1");
    const auto inB = snapshotB->nameResolver().resolveRuntimeName("IRB_U.joint_1");
    EXPECT_TRUE(inA.ok());
    EXPECT_TRUE(inB.ok());
    // A 的旧名不在 B 的映射（映射已随 B 重建——无旧名残留，RT-NM-5 面）。
    EXPECT_FALSE(snapshotB->nameResolver().resolveRuntimeName("IRB_T.joint_1").ok());

    // 场景 2：已释放上下文的迟到请求——需要重新解析对象（S1 重锚定）时
    // 注入源已释放→ContextReleased 拒绝（§9.3"迟到请求若需重新解析对象
    // 而注入源已释放"；§3.4 总纲：调用方契约违约走 fail-fast 异常轨——
    // S1 归属表 Sources.hpp 文件头）。替身编译器按归属表转译 err。
    ScriptedCompiler releasedCompiler(modelA, 1u, std::string{"impl-a"});
    releasedCompiler.failWith(RuntimeErrorCode::ContextReleased);
    CompileRequest lateRequest;
    lateRequest.revision = modelA.header().revision;
    bool thrown = false;
    try {
        (void)RuntimeSnapshotFactory{}.create(lateRequest, releasedCompiler);
        ADD_FAILURE() << "已释放上下文的迟到编译请求应 fail-fast 拒绝（§9.3）";
    } catch (const RuntimeError& e) {
        thrown = true;
        EXPECT_EQ(e.code(), RuntimeErrorCode::ContextReleased);
        EXPECT_TRUE(isContractViolation(e.code())) << "契约违约码不走诊断收集（§3.4）";
    }
    EXPECT_TRUE(thrown);

    // 场景 3（对照）：纯快照内操作不依赖上下文——S_A 反解/只读查询在
    // "上下文已释放"后照常服务（§9.3"纯快照内操作照常服务"）。
    EXPECT_TRUE(snapshotA->nameResolver().resolveObjectId(j1).ok());
}

// =====================================================================
// RT-SNAP-4：释放后引用生命周期（§9.3——等价观测，见文件头说明）。
// =====================================================================

TEST(SnapshotTest, ReferenceReleaseLifecycle_RT_SNAP_4)
{
    const CanonicalModel model = richFixture().build();
    ScriptedCompiler compiler(model, 1u, std::string{"impl-rel"});
    // 非 const：本用例要显式 reset 释放快照侧引用（生命周期观测点）。
    auto snapshot = createPublished(model, CompileOptions{}, compiler);
    ASSERT_NE(snapshot, nullptr);

    // 持有面 1：view 副本借持 Ptr（§8.3"视图借持引用计数"）——快照句柄
    // 释放后视图仍保活 WC（消费方持有视图期间的存活保证）。
    WorkCellConstView heldView = snapshot->workCell();
    const std::size_t frames = heldView.frameCount();
    std::weak_ptr<const RuntimeSnapshot> observer = snapshot;

    snapshot.reset();  // 非持 const——释放快照侧最后引用（析构观测点）
    // 快照本体已析构（弱引用过期＝对象确实销毁——悬垂访问不可能经
    // shared_ptr 通道发生；MSVC 无 ASAN 的等价观测，§15.4 v0.10）。
    EXPECT_TRUE(observer.expired());
    // 视图借持的 WC 仍可只读查询（存活至最后引用释放——§8.3 生命周期；
    // heldView 析构即 RAII 释放，无残留引用面）。
    EXPECT_EQ(heldView.frameCount(), frames);
}

// =====================================================================
// RT-CPX-4：取消与内存不足（§5.5——可重入重试/预算转译）。
// =====================================================================

TEST(SnapshotTest, CancelAtStageBoundary_RT_CPX_4)
{
    const CanonicalModel model = richFixture().build();
    ScriptedCompiler compiler(model, 1u, std::string{"impl-cancel"});
    CancelToggle toggle;
    CompileRequest request;
    request.revision = model.header().revision;
    request.cancel = &toggle;

    // 编程"CancelToggle 在 S6 段边界置位"：替身在 buildCanonicalModel 被
    // 调用时置位——工厂于 S1–S5 返回后的段边界检查点观测到取消。
    compiler.armCancelOnCall(&toggle);
    const CompileOutcome cancelled = RuntimeSnapshotFactory{}.create(request, compiler);
    EXPECT_EQ(cancelled.status, CompileStatus::Cancelled);
    EXPECT_EQ(cancelled.snapshot, nullptr) << "取消无半成品（MDL-06/§5.3）";
    ASSERT_EQ(cancelled.diagnostics.size(), 1u);
    EXPECT_EQ(cancelled.diagnostics.front().code,
              std::string{registryCode(RuntimeErrorCode::Cancelled)})
        << "取消诊断码＝RT-CANCELLED（非 error 级——UX-03/D-11）";

    // 可重入重试成功（§5.5——同工厂复用、令牌复位后重编译）。
    toggle.reset();
    ScriptedCompiler retryCompiler(model, 1u, std::string{"impl-cancel"});
    const auto retried = createPublished(model, request.options, retryCompiler);
    EXPECT_NE(retried, nullptr);
}

TEST(SnapshotTest, BadAllocBecomesResourceBudget_RT_CPX_4)
{
    const CanonicalModel model = richFixture().build();
    ScriptedCompiler compiler(model, 1u, std::string{"impl-alloc"});
    CompileRequest request;
    request.revision = model.header().revision;

    // 注入 std::bad_alloc（§11"替身 reader 抛"的等价注入面——S2 段异常）。
    compiler.throwBadAlloc();
    const CompileOutcome out = RuntimeSnapshotFactory{}.create(request, compiler);
    EXPECT_EQ(out.status, CompileStatus::Failed);
    EXPECT_EQ(out.snapshot, nullptr) << "资源不足不发布快照（§5.5）";
    ASSERT_EQ(out.diagnostics.size(), 1u);
    EXPECT_EQ(out.diagnostics.front().code,
              std::string{registryCode(RuntimeErrorCode::ResourceBudget)})
        << "bad_alloc→ResourceBudget（§5.5——对象已由 RAII 清理）";

    // 清理后重编译成功（无残留状态——工厂无状态、局部产物已析构）。
    ScriptedCompiler retryCompiler(model, 1u, std::string{"impl-alloc"});
    const auto retried = createPublished(model, request.options, retryCompiler);
    EXPECT_NE(retried, nullptr);
}

// =====================================================================
// create 失败面：err 转译与契约违约重抛（§3.4/§5.2 归属表）。
// =====================================================================

TEST(SnapshotTest, CreateFailureDiagnosticsAndContractViolation_RT_T09)
{
    const CanonicalModel model = richFixture().build();
    CompileRequest request;
    request.revision = model.header().revision;

    // 环境/输入错误（InputInvalid）→Failed＋稳定码诊断＋无快照。
    ScriptedCompiler failing(model, 1u, std::string{"impl-f"});
    failing.failWith(RuntimeErrorCode::InputInvalid);
    const CompileOutcome failed =
        RuntimeSnapshotFactory{}.create(request, failing);
    EXPECT_EQ(failed.status, CompileStatus::Failed);
    EXPECT_EQ(failed.snapshot, nullptr);
    ASSERT_FALSE(failed.diagnostics.empty());
    EXPECT_EQ(failed.diagnostics.front().code,
              std::string{registryCode(RuntimeErrorCode::InputInvalid)});

    // 调用方契约违约（UnknownObject——修订不存在）→fail-fast 重抛（§3.4
    // 总纲：不走诊断收集）。
    ScriptedCompiler unknown(model, 1u, std::string{"impl-f"});
    unknown.failWith(RuntimeErrorCode::UnknownObject);
    bool thrown = false;
    try {
        (void)RuntimeSnapshotFactory{}.create(request, unknown);
    } catch (const RuntimeError& e) {
        thrown = true;
        EXPECT_EQ(e.code(), RuntimeErrorCode::UnknownObject);
    }
    EXPECT_TRUE(thrown) << "UnknownObject 保持异常轨（调用方契约违约）";
}

// =====================================================================
// DWC 状态两成因（§9.1 dynamicWorkCellState／§9.4 capabilityLevel）。
// =====================================================================

TEST(SnapshotTest, DwcStateBothCauses_RT_T09)
{
    const CanonicalModel model = minimalFixture().build();  // 全连杆物性 NotProvided
    CompileRequest request;
    request.revision = model.header().revision;

    // 成因 1：物性缺失（请求 DWC 而门控跳过）——Skipped＋缺失清单（链序）
    // ＋能力位 false＋S7 警告随诊断。
    ScriptedCompiler compilerRequested(model, 1u, std::string{"impl-dwc"});
    const auto skippedByPhysics = createPublished(model, request.options, compilerRequested);
    ASSERT_NE(skippedByPhysics, nullptr);
    EXPECT_EQ(skippedByPhysics->dynamicWorkCellState(), DwcSnapshotState::SkippedNoPhysics);
    EXPECT_EQ(skippedByPhysics->skippedDynamicObjects().size(), model.chain().links.size());
    EXPECT_FALSE(skippedByPhysics->capabilities().hasDynamicWorkCell);
    EXPECT_FALSE(skippedByPhysics->tryDynamicWorkCell().ok());
    ASSERT_FALSE(skippedByPhysics->diagnostics().empty());
    EXPECT_EQ(skippedByPhysics->diagnostics().back().code, std::string{"RT-CAPABILITY-MISSING"});

    // 成因 2：未请求 DWC（capabilityLevel=false，§9.4）——Skipped＋空缺失
    // 清单（无物性缺失事实）；快照仍合法发布（D-03 声明完整的发布态）。
    CompileOptions noDwc;
    noDwc.requestDynamicWorkCell = false;
    ScriptedCompiler compilerUnrequested(model, 1u, std::string{"impl-dwc"});
    const auto skippedByRequest = createPublished(model, noDwc, compilerUnrequested);
    ASSERT_NE(skippedByRequest, nullptr);
    EXPECT_EQ(skippedByRequest->dynamicWorkCellState(), DwcSnapshotState::SkippedNoPhysics);
    EXPECT_TRUE(skippedByRequest->skippedDynamicObjects().empty())
        << "未请求路径无缺失清单（清单只承载物性缺失事实）";
    // tryDynamicWorkCell 错误面：错误＋定位能力项（§8.3——非崩溃路径）。
    const auto dwcView = skippedByRequest->tryDynamicWorkCell();
    ASSERT_FALSE(dwcView.ok());
    EXPECT_EQ(dwcView.error().code(), RuntimeErrorCode::UnknownObject);
}

// =====================================================================
// CR-05：值供给样例（与 EV-T04 联合断言一致——acceptance 2）。
// =====================================================================

TEST(SnapshotTest, Cr05ValueSupplyEnvironmentEntries_CR_05)
{
    // ── 值供给样例（与 EV-T04 共享同一样例的对齐锚点）────────────────
    // 样例＝固定种子 rich 夹具（CanonicalModelFixture：种子 "prj-A"/
    // "brn-A"/"rev-1"/"robot"/"j1"…）＋编译器版本 (1u, "cr05-sample-1.0")
    // ＋RobWorkBaselineVersion::capture()。EV-T04 侧以相同夹具种子与相同
    // baseline 串构造样例，断言其切片 Environment 条目
    //   runtime.model-identity ＝ 本测试 modelIdentityText
    //   runtime.robwork-baseline ＝ 本测试 baselineText
    // 逐字节一致（evidence 不重算/重编码 CanonicalModel——CR-05 裁决；
    // nameMapIdentity 经快照 nameMapRef 同样值传递）。
    const CanonicalModel model = richFixture().build();
    ScriptedCompiler compiler(model, 1u, std::string{"cr05-sample-1.0"});
    const auto snapshot = createPublished(model, CompileOptions{}, compiler);
    ASSERT_NE(snapshot, nullptr);

    // 组装方视角：从快照**值传递**录入 Environment 条目（无任何重编码）。
    const std::string modelIdentityText = snapshot->modelIdentity().toCanonical();
    const std::string baselineText = snapshot->robworkBaselineVersion();
    const std::string nameMapIdentityText = snapshot->nameMapIdentity().toCanonical();

    // 断言 1：快照暴露值＝各真值源的计算值（值供给的同一性）。
    EXPECT_TRUE(snapshot->modelIdentity() == model.contentIdentity())
        << "modelIdentity＝CanonicalModel.contentIdentity（runtime 计算）";
    EXPECT_TRUE(snapshot->nameMapIdentity()
                == buildRuntimeNameMap(model).contentIdentity())
        << "nameMapIdentity＝RuntimeNameMap.contentIdentity（runtime 计算）";
    EXPECT_EQ(baselineText, RobWorkBaselineVersion::capture().text)
        << "robworkBaselineVersion＝采集点单值（§8.4 随快照记录）";

    // 断言 2：规范文本可解析回同一身份（条目值是可核对的规范编码——
    // evidence 侧 Environment 条目的值形态）。
    const core::ContentIdentity roundtrip =
        core::ContentIdentity::fromCanonical(modelIdentityText);
    EXPECT_TRUE(roundtrip == snapshot->modelIdentity());
    const core::ContentIdentity mapRoundtrip =
        core::ContentIdentity::fromCanonical(nameMapIdentityText);
    EXPECT_TRUE(mapRoundtrip == snapshot->nameMapIdentity());

    // 断言 3（样例确定性）：同一样例重复供给三值逐字节一致——EV-T04 的
    // 联合断言因此可跨单元跨进程复核（NFR-COR-02）。
    const auto snapshotAgain = createPublished(model, CompileOptions{}, compiler);
    ASSERT_NE(snapshotAgain, nullptr);
    EXPECT_EQ(snapshotAgain->modelIdentity().toCanonical(), modelIdentityText);
    EXPECT_EQ(snapshotAgain->nameMapIdentity().toCanonical(), nameMapIdentityText);
    EXPECT_EQ(snapshotAgain->robworkBaselineVersion(), baselineText);

    // 断言 4（基线文本形态）：非空、无空白（可直接作为条目值与编码域字节
    // ——RobWorkBaselineVersion 契约）。
    ASSERT_FALSE(baselineText.empty());
    for (const char c : baselineText) {
        EXPECT_FALSE(std::isspace(static_cast<unsigned char>(c)) != 0)
            << "基线文本含空白字符（契约禁止）";
    }
}

}  // namespace
