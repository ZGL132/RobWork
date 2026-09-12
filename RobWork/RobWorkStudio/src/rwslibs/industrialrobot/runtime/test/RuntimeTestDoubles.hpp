/**
 * @file   RuntimeTestDoubles.hpp
 * @brief  runtime 测试替身与契约夹具的单元级共享设施（§11 设施清单的
 *         规范落位）——ScriptedObjectSource/ScriptedClosureSource（内存
 *         对象库）、FakeResourceProvider（可编程缺失/变化/预算）、
 *         ScriptedReader（Description 直构）、CancelToggle（取消注入）。
 *
 * 设计依据：
 *   - units/runtime.md §11（设施清单原文："测试替身＝ScriptedObjectSource/
 *     ScriptedClosureSource（内存对象库）、FakeResourceProvider（可编程
 *     缺失/变化/预算）、ScriptedReader（Description 直构）、CancelToggle
 *     （取消注入）"）＋ §12 RT-T12 行（Scripted 与 Fake 前缀替身——本任务产物）
 *   - units/runtime.md §3.3（四个注入接口的原文契约——替身逐一适配其
 *     只读面；错误归属表见 Sources.hpp 文件头）
 *   - RT-STUB-0 替身边界声明（§11 矩阵行＋runtime/test/README.md 全文）：
 *     替身只覆盖上述注入接口（project/modeling/io/execution 的阶段 B 职责
 *     在阶段 A 的测试投影）；RobWork/rwsim 本体与被测编译器/工厂**不做
 *     替身**——替身输出仅验证 runtime 契约，不构成 RobWork 算法/业务算法
 *     正确性证明。
 *
 * 背景说明（为什么收拢为共享头）：RT-T02～T11 各测试文件曾在 TU 内各自
 * 定义同型替身（CompilerTest.cpp 的 InMemoryRevisionSource/ScriptedReader/
 * FakeResourceProvider/ScriptedCancel、SnapshotTest.cpp 的 CancelToggle）。
 * RT-T12 的契约套件（ContractSuiteTest.cpp）与后续跨单元契约测试需要同一
 * 套替身，故按 §11 命名规范收拢为单元内共享设施（runtime/test/ 下——仅
 * 测试内部可见，不入公共 include；R-2 私有头纪律同 src/ 私有头）。
 *
 * 线程约束：除 CancelToggle（原子量——供"另一线程置位"的取消注入场景）
 * 外，各替身的编程面（非 const 成员）只在编译开始前由测试线程设置；
 * 编译期间仅经 const 只读接口消费（§5.5 注入接口并发只读安全的测试侧
 * 使用纪律；跨线程并发使用同一替身不在本设施承诺面）。
 *
 * 确定性：对象 id/摘要由固定种子经 core::ContentDigester 派生（SHA-256
 * 前缀——CanonicalModelFixture 同款纪律），不用 ObjectId::generate()
 * （随机）——"同夹具"在任意两次运行、任意两个进程中字节一致（RT-ID-1
 * 跨进程身份稳定的夹具前提）。
 */

#ifndef SDURWS_IRD_RUNTIME_TEST_RUNTIMETESTDOUBLES_HPP
#define SDURWS_IRD_RUNTIME_TEST_RUNTIMETESTDOUBLES_HPP

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>

#include <sdurws/ird/runtime/Compiler.hpp>
#include <sdurws/ird/runtime/Description.hpp>
#include <sdurws/ird/runtime/Errors.hpp>
#include <sdurws/ird/runtime/Resource.hpp>
#include <sdurws/ird/runtime/Sources.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <new>
#include <optional>
#include <string>
#include <vector>

namespace sdurws::ird::runtime::testdoubles {

// =====================================================================
// 确定性派生工具（CanonicalModelFixture 同款纪律——id/摘要由固定种子派生）。
// =====================================================================

using sdurws::ird::runtime::detail::identityTransform3D;

/// 固定种子的 SHA-256 摘要（测试专用确定性来源——非产品路径）。
inline core::Digest256 digestOf(const std::string& seed)
{
    core::ContentDigester d;
    d.update(seed.data(), seed.size());
    return d.finalize();
}

/// 对字节向量求 SHA-256（S2 完整性申报值/资源摘要的确定性来源）。
inline core::Digest256 digestBytes(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester d;
    d.update(bytes.data(), bytes.size());
    return d.finalize();
}

/// 从固定种子派生 16 字节强类型 id（前 16 字节；测试值——非密码学用途）。
template <typename Id>
inline Id idFrom(const std::string& seed)
{
    const core::Digest256 d = digestOf(seed);
    Id id;
    std::copy(d.begin(), d.begin() + 16, id.bytes.begin());
    return id;
}

/// 内容版本（32 字节摘要——CON-01 身份/版本包络的测试值）。
inline core::ContentVersion cvFrom(const std::string& seed)
{
    core::ContentVersion cv;
    cv.bytes = digestOf(seed);
    return cv;
}

/// Provided 态标量（用户输入来源——测试数值注入入口）。
inline core::SourcedValue<double> val(double v)
{
    return core::SourcedValue<double>::provided(
        v, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
}

// =====================================================================
// ScriptedObjectLibrary——内存对象库（两个 Scripted 来源的共享后备存储；
// §11"ScriptedObjectSource/ScriptedClosureSource（内存对象库）"）。
// =====================================================================

/**
 * @brief 修订闭包的内存对象库（值承载——两个 Scripted 替身共同消费）。
 *
 * 结构：一个修订（id/序号/分支）＋一组条目（对象 id、类型 token、内容
 * 版本、登记摘要、对象字节）。编程面（公开成员）只在编译前由测试设置；
 * 编译期间经两个 Scripted 替身的只读接口消费。
 * 线程约束：非线程安全——仅测试线程访问（编程期与观测期）。
 */
struct ScriptedObjectLibrary {
    /// 闭包条目（id/token/cv/digest/bytes 一体登记——一致性由构造方保证）。
    struct Item {
        core::ObjectId id;                    ///< 对象稳定身份
        std::string token;                    ///< 对象类型 token（如 "robot-design"）
        core::ContentVersion cv;              ///< 修订锁定的内容版本
        core::Digest256 digest;               ///< 登记摘要（S2 完整性复核的申报值）
        std::vector<std::uint8_t> bytes;      ///< 对象 canonical 字节
    };

    std::vector<Item> items;                  ///< 修订可见对象全集（objectRefs 序）
    core::RevisionId revision;                ///< 本库登记的修订
    std::uint64_t seq = 0;                    ///< 修订序号（project 侧分配的测试值）
    core::BranchId branch;                    ///< 所属分支
    std::optional<core::ObjectId> rejectInClosure; ///< 编程：该对象闭包判定 false（CM-0 注入）
    std::optional<core::ObjectId> hideBytes;  ///< 编程：该对象字节不可得（S2 缺失注入）
    bool hideRevision = false;                ///< 编程：tryRevision 未命中（违约轨注入）

    /// 登记一条目（id/seed/token 一站式——digest＝"<seed>-bytes" 的摘要，
    /// 字节＝"<seed>-bytes" 的 ASCII；digest 与字节天然一致）。
    /// ★ 实现细节：字节序列先落具名局部 string 再 assign——避免在
    ///   assign 参数位置构造临时 string（MSVC 上曾观测到该写法触发
    ///   vector 长度校验误报，具名临时语义等价且确定）。
    void add(const core::ObjectId& id, const std::string& seed, const std::string& token)
    {
        Item it;
        it.id = id;
        it.token = token;
        it.cv = cvFrom(seed + "-cv");
        const std::string bytes = seed + "-bytes";
        it.bytes.assign(bytes.begin(), bytes.end());
        it.digest = digestBytes(it.bytes);
        items.push_back(std::move(it));
    }
};

// =====================================================================
// ScriptedObjectSource——对象字节只读来源替身（§11 命名；IObjectBytesSource）。
// =====================================================================

/**
 * @brief 内存对象库的字节来源投影（project ②端口 tryObject 的阶段 A 替身）。
 *
 * 只读接口（零写方法——MDL-06 类型层纪律）；nullopt＝不可得（归属表：
 * S2 InputInvalid——Sources.hpp 文件头）。编程点＝库的 hideBytes。
 * 并发约束：编译期间只读消费（编程面在库上，编译前完成）。
 */
class ScriptedObjectSource final : public IObjectBytesSource {
public:
    explicit ScriptedObjectSource(const ScriptedObjectLibrary& library)
        : m_library(&library)
    {
    }

    std::optional<std::vector<std::uint8_t>>
        tryObjectBytes(core::ObjectId objectId, core::ContentVersion version) const override
    {
        // 编程注入：对象字节不可得（S2 InputInvalid 的定位面——§5.2 S2）。
        if (m_library->hideBytes.has_value() && objectId == *m_library->hideBytes) {
            return std::nullopt;
        }
        for (const ScriptedObjectLibrary::Item& it : m_library->items) {
            if (it.id == objectId && it.cv == version) {
                return it.bytes;  // 值拷贝返回（optional<vector>——§3.3 所有权）
            }
        }
        return std::nullopt;
    }

private:
    const ScriptedObjectLibrary* m_library; ///< 被投影的库（不拥有——调用方持有）
};

// =====================================================================
// ScriptedClosureSource——修订闭包只读来源替身（§11 命名；IRevisionClosureSource）。
// =====================================================================

/**
 * @brief 内存对象库的闭包来源投影（project ②端口 revision()＋闭包判定）。
 *
 * tryRevision 未命中→nullopt（S1 UnknownObject 违约轨）；objectInRevision
 * false→CM-0 防混入判定输入（S2 StructureInvalid——RT-CONT-1）。编程点＝
 * 库的 rejectInClosure/hideRevision。
 */
class ScriptedClosureSource final : public IRevisionClosureSource {
public:
    explicit ScriptedClosureSource(const ScriptedObjectLibrary& library)
        : m_library(&library)
    {
    }

    std::optional<RevisionSummary> tryRevision(core::RevisionId rev) const override
    {
        if (m_library->hideRevision || !(rev == m_library->revision)) {
            return std::nullopt;  // 修订不存在（归属表：S1 UnknownObject）
        }
        RevisionSummary s;
        s.id = m_library->revision;
        s.seq = m_library->seq;
        s.branch = m_library->branch;
        for (const ScriptedObjectLibrary::Item& it : m_library->items) {
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
        if (!(rev == m_library->revision)) {
            return false;
        }
        // CM-0 编程注入：引用不在目标修订闭包（RT-CONT-1 判定输入）。
        if (m_library->rejectInClosure.has_value() && objectId == *m_library->rejectInClosure) {
            return false;
        }
        for (const ScriptedObjectLibrary::Item& it : m_library->items) {
            if (it.id == objectId && it.cv == version) {
                return true;
            }
        }
        return false;
    }

private:
    const ScriptedObjectLibrary* m_library; ///< 被投影的库（不拥有——调用方持有）
};

// =====================================================================
// ScriptedReader——RobotDesign 解析器替身（§11 命名；Description 直构）。
// =====================================================================

/**
 * @brief modeling IRobotDesignReader 的阶段 A 替身：返回预置 Description。
 *
 * 编程面：failWith（err 轨错误码——S2 归属 InputInvalid 等）、throwBadAlloc
 * （§5.5 内存不足→ResourceBudget 转译的注入面）、resetTo（换有效描述——
 * "修复输入后重编译"场景）。reads() 供调用计数断言（单线程观测）。
 */
class ScriptedReader final : public IRobotDesignReader {
public:
    explicit ScriptedReader(RobotDesignDescription description)
        : m_description(std::move(description))
    {
    }

    /// 编程失败码（read 返回 err 轨——不抛、不静默，NFR-COR-03）。
    void failWith(RuntimeErrorCode code) { m_failCode = code; }
    /// 编程：read 抛 std::bad_alloc（§5.5 bad_alloc 转译的注入面）。
    void throwBadAlloc() { m_throwBadAlloc = true; }
    /// 换有效描述（"修复输入后重编译"——同一替身实例的应答更新）。
    void resetTo(RobotDesignDescription description) { m_description = std::move(description); }
    /// 累计读取次数（单线程观测——确定性断言用）。
    int reads() const { return m_reads; }

    Expected<RobotDesignDescription, RuntimeError>
    read(const std::vector<std::uint8_t>&, std::uint32_t) const override
    {
        m_reads++;
        if (m_throwBadAlloc) {
            throw std::bad_alloc();  // §5.5：内存不足→ResourceBudget（编译链转译）
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

// =====================================================================
// FakeResourceProvider——资源提供者替身（§11 命名；可编程缺失/变化/预算）。
// =====================================================================

/**
 * @brief io IRuntimeResourceProvider 的阶段 A 替身（io 侧检测/预算/缺失
 *        的可编程投影；runtime 在 S4 就地转译定位——§8.6 职责边界）。
 *
 * 编程模型（缺失/变化/预算三面齐备——§11 原文括注）：
 *   - 未登记 id                 → ResourceMissing（缺失面——io 检测/runtime 定位）；
 *   - 逐资源字节应答序列（末项重复）→ 第 1 次读取与 S10 前复查可返回不同
 *     字节（RT-RES-2"编译中替换"的表达面；两次同内容＝稳定通过）；
 *   - failWith(ResourceBudget)  → 预算面（io 侧预算超限的原始检测投影）；
 *   - failWith(ResourceChanged) → "读取时刻与登记之间被替换"的原始检测投影。
 * 借持语义（P-IO-2 裁决同款）：返回的 ResourceBytes 缓冲由本替身的编程
 * 条目持有（编译链同步消费）；摘要现算（io 计算后传入的同款形态）。
 */
class FakeResourceProvider final : public IRuntimeResourceProvider {
public:
    struct Entry {
        core::ObjectId id;                                  ///< 资源对象身份
        std::vector<std::vector<std::uint8_t>> bytesByCall; ///< ≥1 项；末项重复
        mutable int calls = 0;                              ///< 调用计数（单线程观测）
    };

    /// 登记资源的字节应答序列（至少一项——空序列按缺失处理）。
    void program(core::ObjectId id, std::vector<std::vector<std::uint8_t>> bytesByCall)
    {
        Entry e;
        e.id = id;
        e.bytesByCall = std::move(bytesByCall);
        m_entries.push_back(std::move(e));
    }

    /// 全局失败编程（优先于字节应答——预算/变化面的注入通道）。
    void failWith(RuntimeErrorCode code) { m_failCode = code; }

    /// 指定资源的累计读取次数（"S4 读取＋S10 前复查"时点的行为证据）。
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
        // 失败编程优先（预算/变化的原始检测投影——三码分立的注入面）。
        if (m_failCode.has_value()) {
            return Expected<ResourceBytes, ResourceReadError>::err(
                ResourceReadError{*m_failCode, resourceId,
                                  std::string{"FakeResourceProvider 编程失败"}});
        }
        for (const Entry& e : m_entries) {
            if (e.id == resourceId) {
                if (e.bytesByCall.empty()) {
                    return Expected<ResourceBytes, ResourceReadError>::err(
                        ResourceReadError{RuntimeErrorCode::ResourceMissing, resourceId,
                                          std::string{"空编程序列——按缺失处理"}});
                }
                // 借持视图：缓冲归本替身（Entry 内 vector）；末项重复——
                // 两次同内容＝"读取与复查一致"的稳定面（RT-RES-2 对照）。
                const std::size_t idx = static_cast<std::size_t>(
                    e.calls < static_cast<int>(e.bytesByCall.size())
                        ? e.calls
                        : static_cast<int>(e.bytesByCall.size()) - 1);
                const std::vector<std::uint8_t>& bytes = e.bytesByCall[idx];
                e.calls++;
                ResourceBytes b;
                b.data = bytes.data();
                b.size = bytes.size();
                b.digest = digestBytes(bytes);
                return Expected<ResourceBytes, ResourceReadError>::ok(b);
            }
        }
        // 无编程＝资源不存在（缺失面——io 检测/runtime 定位转译，§8.6）。
        return Expected<ResourceBytes, ResourceReadError>::err(
            ResourceReadError{RuntimeErrorCode::ResourceMissing, resourceId,
                              std::string{"资源未登记（FakeResourceProvider）"}});
    }

private:
    std::vector<Entry> m_entries;               ///< 编程资源集
    std::optional<RuntimeErrorCode> m_failCode; ///< 全局失败编程（优先）
};

// =====================================================================
// CancelToggle——取消令牌替身（§11 命名；原子置位——"另一线程注入"形态）。
// =====================================================================

/**
 * @brief 编译取消注入替身（execution 取消通道的阶段 A 投影）。
 *
 * 原子布尔承载——request() 可在测试的任意线程调用（模拟 UI/execution 侧
 * 置位），编译线程经 cancellationRequested() 轮询（§3.3：实现方内部自行
 * 同步）。最终性纪律（§3.3"只增不减"）由使用方遵守：本替身提供 reset()
 * 仅供"同令牌复用"的测试便利，产品语义下置位后不反悔。
 */
class CancelToggle final : public ICompileCancelToken {
public:
    /// 置位取消请求（任意线程——原子 store）。
    void request() { m_flag.store(true, std::memory_order_relaxed); }
    /// 复位（仅测试复用便利；产品语义置位后不反悔——§3.3）。
    void reset() { m_flag.store(false, std::memory_order_relaxed); }

    bool cancellationRequested() const override
    {
        return m_flag.load(std::memory_order_relaxed);
    }

private:
    std::atomic<bool> m_flag{false}; ///< 取消状态（原子——跨线程注入的同步点）
};

// =====================================================================
// ContractHarness——契约套件一站式装配（闭包＋Description＋CompileRequest）。
// =====================================================================

/**
 * @brief 契约套件编译输入夹具：确定性闭包＋可配置 Description。
 *
 * 配置面（make 后、编译前调整公开成员）：
 *   - physics：None（全缺——降级路径）| Full（全连杆 Provided——DWC 路径）
 *     | Partial（仅 link_1 缺 mass——混合缺失面 RT-CAP-1）；
 *   - preset/basePosition：安装预设（默认 Ground 恒等；可改 Inverted 等）。
 * 单位纪律：Description 内全部数值 SI 真值（m/rad/kg/rad/s——§4.4）。
 * 确定性：全部 id/版本/摘要由固定种子派生——跨进程字节一致（RT-ID-1）。
 */
struct ContractHarness {
    // ---- 确定性身份（固定种子——跨进程一致的夹具前提）----
    core::ProjectId project = idFrom<core::ProjectId>("rt-suite-prj");
    core::BranchId branch = idFrom<core::BranchId>("rt-suite-brn");
    core::RevisionId revision = idFrom<core::RevisionId>("rt-suite-rev");

    core::ObjectId robot = idFrom<core::ObjectId>("rt-suite-robot");
    core::ObjectId j1 = idFrom<core::ObjectId>("rt-suite-j1");
    core::ObjectId j2 = idFrom<core::ObjectId>("rt-suite-j2");
    core::ObjectId l0 = idFrom<core::ObjectId>("rt-suite-l0");
    core::ObjectId l1 = idFrom<core::ObjectId>("rt-suite-l1");
    core::ObjectId l2 = idFrom<core::ObjectId>("rt-suite-l2");
    core::ObjectId res1 = idFrom<core::ObjectId>("rt-suite-res1");

    /// 物性配置（影响 hasDynamicWorkCell 派生——§9.6）。
    enum class Physics { None, Full, Partial };
    Physics physics = Physics::None;

    ScriptedObjectLibrary store;        ///< 闭包＋对象字节（两个 Scripted 源的后备）
    RobotDesignDescription description; ///< reader 应答

    /// 组装闭包＋Description（两关节链——契约套件的标准夹具形态）。
    static ContractHarness make(Physics physics = Physics::None)
    {
        ContractHarness h;
        h.physics = physics;
        h.store.revision = h.revision;
        h.store.seq = 3;
        h.store.branch = h.branch;
        h.store.add(h.robot, "rt-suite-robot", kRobotDesignObjectType);
        h.store.add(h.j1, "rt-suite-j1", "joint");
        h.store.add(h.j2, "rt-suite-j2", "joint");
        h.store.add(h.l0, "rt-suite-l0", "link");
        h.store.add(h.l1, "rt-suite-l1", "link");
        h.store.add(h.l2, "rt-suite-l2", "link");

        // ---- Description（S3 合法域内的最小可编译链；SI 真值——§4.4）----
        RobotDesignDescription d;
        d.descriptionContractVersion = 1;
        d.robotLocalName = "RT_SUITE_BOT";

        const double pi = 3.14159265358979323846;
        const core::ObjectId jointIds[2] = {h.j1, h.j2};
        for (std::size_t i = 0; i < 2; ++i) {
            JointDescription jd;
            jd.objectId = jointIds[i];
            jd.localName = "joint_" + std::to_string(i + 1);
            jd.type = JointType::Revolute;
            jd.axis = rw::math::Vector3D<double>(0.0, 0.0, 1.0); // 单位向量（无量纲）
            jd.origin = identityTransform3D();
            jd.lower = val(-2.0);                // 单位 rad
            jd.upper = val(i == 0 ? 2.0 : pi);   // 单位 rad
            jd.maxVelocity = val(3.0);           // 单位 rad/s
            jd.maxAcceleration = val(10.0);      // 单位 rad/s²
            d.joints.push_back(jd);
        }

        const core::ObjectId linkIds[3] = {h.l0, h.l1, h.l2};
        const char* linkNames[3] = {"base_link", "link_1", "link_2"};
        const double masses[3] = {5.0, 4.0, 3.0}; // 单位 kg
        for (std::size_t i = 0; i < 3; ++i) {
            LinkDescription ld;
            ld.objectId = linkIds[i];
            ld.localName = linkNames[i];
            // 物性按配置注入：Full＝全连杆；Partial＝link_1 缺 mass
            // （RT-CAP-1 混合缺失面）；None＝全缺（降级面）。
            if (h.physics == Physics::Full
                || (h.physics == Physics::Partial && i != 1)) {
                ld.mass = val(masses[i]);  // 单位 kg
                ld.centerOfMass = core::SourcedValue<rw::math::Vector3D<double>>::provided(
                    rw::math::Vector3D<double>(0.0, 0.0, 0.05 * static_cast<double>(i)),
                    core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
                ld.inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
                    rw::math::InertiaMatrix<double>(0.01, 0, 0, 0, 0.02, 0, 0, 0, 0.03),
                    core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
            }
            d.links.push_back(ld);
        }
        d.base.preset = InstallationPresetToken::Ground; // 默认地面（V15-04）
        h.description = std::move(d);
        return h;
    }

    /// 组装 CompileRequest（注入源由调用方持有——编译期生命周期保证）。
    CompileRequest makeRequest(ScriptedReader& reader,
                               ScriptedObjectSource& objects,
                               ScriptedClosureSource& closure) const
    {
        CompileRequest req;
        req.project = project;
        req.revision = revision;
        req.objects = &objects;
        req.closure = &closure;
        req.designReader = &reader;
        return req;
    }

    /// 闭包＋对象字节只读源（轻量视图对象——调用方在编译期持有）。
    ScriptedObjectSource objectSource() const { return ScriptedObjectSource(store); }
    ScriptedClosureSource closureSource() const { return ScriptedClosureSource(store); }
};

}  // namespace sdurws::ird::runtime::testdoubles

#endif  // SDURWS_IRD_RUNTIME_TEST_RUNTIMETESTDOUBLES_HPP
