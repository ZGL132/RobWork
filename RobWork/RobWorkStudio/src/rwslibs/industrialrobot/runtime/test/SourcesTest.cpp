/**
 * @file   SourcesTest.cpp
 * @brief  Sources 注入契约语义用例组——来源注入点行为与错误归属可定位
 *         （ScriptedObjectSource/ScriptedClosureSource/CancelToggle 三个
 *         §11 标准测试替身的最小形态）。
 *
 * 设计依据：
 *   - units/runtime.md §3.3（注入最小契约原文——接口签名/值类型形态）、
 *     §5.2（十段表——各注入点失败结果的错误归属）、§9.3（worker 内实现
 *     ＝随请求物化）、§11（替身清单：ScriptedObjectSource/ScriptedClosure
 *     Source/CancelToggle——本文件为其契约级最小形态；完整替身随 RT-T12）
 *   - 需求 MDL-06（零写路径——类型层）、CON-01（闭包防混入 CM-0）、
 *     TASK-01/UX-03（协作取消非错误）；任务契约
 *     tasks/foundation/RT-T02.json（acceptance 2）
 *
 * 替身边界声明（RT-STUB-0 精神，先于 RT-T12 在此登记）：本文件替身输出
 * 仅验证 runtime 注入契约自身的语义（脚本化返回/闭包判定/取消置位），
 * 不构成 project/execution 真实端口行为的证明——真实适配器归 L5 装配
 * （阶段 B）。
 */

#include <sdurws/ird/runtime/Errors.hpp>
#include <sdurws/ird/runtime/Sources.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird::runtime;
using namespace sdurws::ird::core;
// 限定符别名：using namespace 只引入成员名，不使 "core::" 限定形式可见
// ——本文件 core 强类型一律按限定形式书写（与产品代码同形），别名必须。
namespace core = sdurws::ird::core;

// =====================================================================
// 测试替身（§11 标准替身的最小契约级形态）。
// =====================================================================

/// 脚本化对象字节来源：内存 (oid,cv)→字节表，表外一律 nullopt。
class ScriptedObjectSource : public IObjectBytesSource {
public:
    /// 预置一个对象字节条目（装配期脚本——测试"编排"步骤）。
    void put(core::ObjectId id, core::ContentVersion cv, std::vector<std::uint8_t> bytes)
    {
        m_objects[{id, cv}] = std::move(bytes);
    }

    std::optional<std::vector<std::uint8_t>>
        tryObjectBytes(core::ObjectId objectId, core::ContentVersion version) const override
    {
        // 查表命中→拷贝字节；未命中→nullopt（错误归属归调用方编译链——§3.3）。
        const auto it = m_objects.find({objectId, version});
        if (it == m_objects.end()) {
            return std::nullopt;
        }
        return it->second;
    }

private:
    /// (身份, 版本)→字节；map 的键比较经 core 强类型的 operator<（字节字典序）。
    std::map<std::pair<core::ObjectId, core::ContentVersion>, std::vector<std::uint8_t>>
        m_objects;
};

/// 脚本化修订闭包来源：预置一份 RevisionSummary＋闭包成员集。
class ScriptedClosureSource : public IRevisionClosureSource {
public:
    /// 预置修订投影（本替身支持单修订——契约级最小形态足够）。
    void putSummary(RevisionSummary summary) { m_summary = std::move(summary); }

    /// 预置闭包成员 (oid, cv)（与 objectRefs 口径一致——接口契约要求）。
    void putMember(core::ObjectId id, core::ContentVersion cv)
    {
        m_closure.insert({id, cv});
    }

    std::optional<RevisionSummary> tryRevision(core::RevisionId revision) const override
    {
        // 单修订替身：身份相等返回投影，否则 nullopt（S1 归属 UnknownObject）。
        if (m_summary && m_summary->id == revision) {
            return m_summary;
        }
        return std::nullopt;
    }

    bool objectInRevision(core::RevisionId revision, core::ObjectId objectId,
                          core::ContentVersion version) const override
    {
        // 修订不存在→必然不在闭包（口径一致性的平凡分支）；
        // 否则按预置成员集判定。
        if (!m_summary || m_summary->id != revision) {
            return false;
        }
        return m_closure.count({objectId, version}) > 0;
    }

private:
    std::optional<RevisionSummary> m_summary;
    std::set<std::pair<core::ObjectId, core::ContentVersion>> m_closure;
};

/// 取消开关（§11 CancelToggle 最小形态）：置位后保持（取消的最终性）。
class CancelToggle : public ICompileCancelToken {
public:
    /// 置位取消请求（此后不可撤销——§3.3 注入契约"只增不减"约束）。
    void request() { m_requested = true; }

    bool cancellationRequested() const override { return m_requested; }

private:
    bool m_requested = false;   ///< 初始未取消；置位后恒 true（最终性）
};

// =====================================================================
// 测试夹具数据构造（确定性：字节手工置位，不用随机生成——失败可复现）。
// =====================================================================

/// 构造首字节为 tag 的对象身份（其余字节零——非全零即有效）。
core::ObjectId makeObjectId(std::uint8_t tag)
{
    core::ObjectId id{};
    id.bytes[0] = tag;
    return id;
}

/// 构造首字节为 tag 的内容版本。
core::ContentVersion makeVersion(std::uint8_t tag)
{
    core::ContentVersion cv{};
    cv.bytes[0] = tag;
    return cv;
}

/// 构造首字节为 tag 的修订身份。
core::RevisionId makeRevisionId(std::uint8_t tag)
{
    core::RevisionId id{};
    id.bytes[0] = tag;
    return id;
}

/// 构造首字节为 tag 的分支身份。
core::BranchId makeBranchId(std::uint8_t tag)
{
    core::BranchId id{};
    id.bytes[0] = tag;
    return id;
}

/// 构造一份两对象引用的修订投影（字段确定性——逐字段断言可复现）。
RevisionSummary makeSummary()
{
    RevisionSummary s;
    s.id = makeRevisionId(0x21);
    s.seq = 7;                     // 序号无物理单位——project 单调序号
    s.parent = makeRevisionId(0x20);
    s.branch = makeBranchId(0x33);
    ObjectRefEntry robot;
    robot.objectId = makeObjectId(0x01);
    robot.contentVersion = makeVersion(0xA1);
    robot.objectTypeToken = "robot-design";
    robot.digest[0] = 0xD1;
    ObjectRefEntry tool;
    tool.objectId = makeObjectId(0x02);
    tool.contentVersion = makeVersion(0xA2);
    tool.objectTypeToken = "tool";
    tool.digest[0] = 0xD2;
    s.objectRefs = {robot, tool};
    return s;
}

}  // namespace

/** 接口抽象性（类型层）：三个注入接口不可实例化、只能经实现类多态使用。 */
TEST(SourcesContract, InterfacesAreAbstract_RT_SRC)
{
    // 静态断言＝编译期证据（MDL-06 零写路径的载体之一：接口只含纯虚只读方法）。
    static_assert(std::is_abstract_v<IObjectBytesSource>, "对象来源必须是纯接口");
    static_assert(std::is_abstract_v<IRevisionClosureSource>, "闭包来源必须是纯接口");
    static_assert(std::is_abstract_v<ICompileCancelToken>, "取消令牌必须是纯接口");
    SUCCEED() << "三个注入接口均为抽象类型（编译期通过）";
}

/** SRC-1 对象字节注入：脚本命中返回原字节；未命中 nullopt（S2 归属 InputInvalid）。 */
TEST(SourcesObjectBytes, ScriptedHitAndMiss_RT_SRC_S2)
{
    ScriptedObjectSource source;
    const auto oid = makeObjectId(0x01);
    const auto cv = makeVersion(0xA1);
    const std::vector<std::uint8_t> bytes{'R', 'T', '-', 'C', 'M'};
    source.put(oid, cv, bytes);

    // 经基类引用调用（多态契约面——与编译链持 IObjectBytesSource& 同形）。
    const IObjectBytesSource& iface = source;
    const auto hit = iface.tryObjectBytes(oid, cv);
    ASSERT_TRUE(hit.has_value()) << "脚本内 (oid,cv) 必须命中";
    EXPECT_EQ(*hit, bytes) << "返回字节与脚本逐字节一致（值传递）";

    // 未命中：nullopt（不是空 vector——nullopt 与"空字节"语义分离）。
    const auto miss = iface.tryObjectBytes(oid, makeVersion(0xFF));
    EXPECT_FALSE(miss.has_value()) << "表外版本必须 nullopt（不可得≠空内容）";
    // 同 (oid,cv) 重复读取确定性（§3.3 实现方约束的替身侧体现）。
    EXPECT_EQ(iface.tryObjectBytes(oid, cv), hit);
}

/** SRC-2 修订投影注入：字段逐项往返；未知修订 nullopt（S1 归属 UnknownObject）。 */
TEST(SourcesRevision, SummaryRoundtripAndUnknownRevision_RT_SRC_S1)
{
    ScriptedClosureSource source;
    const RevisionSummary scripted = makeSummary();
    source.putSummary(scripted);
    // 闭包成员集与 objectRefs 口径一致（接口契约：两方法不得自相矛盾）。
    for (const auto& ref : scripted.objectRefs) {
        source.putMember(ref.objectId, ref.contentVersion);
    }

    const IRevisionClosureSource& iface = source;
    const auto got = iface.tryRevision(scripted.id);
    ASSERT_TRUE(got.has_value());
    // 逐字段断言（投影为纯值结构——任何字段漂移都可视）。
    EXPECT_EQ(got->id, scripted.id);
    EXPECT_EQ(got->seq, 7u);
    ASSERT_TRUE(got->parent.has_value());
    EXPECT_EQ(*got->parent, *scripted.parent);
    EXPECT_EQ(got->branch, scripted.branch);
    ASSERT_EQ(got->objectRefs.size(), scripted.objectRefs.size());
    for (std::size_t i = 0; i < scripted.objectRefs.size(); ++i) {
        EXPECT_EQ(got->objectRefs[i].objectId, scripted.objectRefs[i].objectId);
        EXPECT_EQ(got->objectRefs[i].contentVersion, scripted.objectRefs[i].contentVersion);
        EXPECT_EQ(got->objectRefs[i].objectTypeToken, scripted.objectRefs[i].objectTypeToken);
        EXPECT_EQ(got->objectRefs[i].digest, scripted.objectRefs[i].digest);
    }

    // 未知修订：nullopt——错误归属（文件头表）：UnknownObject（§5.2 S1）。
    const auto unknown = iface.tryRevision(makeRevisionId(0xEE));
    EXPECT_FALSE(unknown.has_value());
}

/** SRC-3 错误归属可定位：四个注入点的失败结果 ↔ 归属错误码逐项钉住（acceptance 2）。 */
TEST(SourcesAttribution, InjectionPointsMapToCodes_RT_SRC_ACCEPT2)
{
    // 装配：一个两对象闭包＋对象字节来源（只给对象 1 的字节）。
    ScriptedClosureSource closure;
    const RevisionSummary scripted = makeSummary();
    closure.putSummary(scripted);
    for (const auto& ref : scripted.objectRefs) {
        closure.putMember(ref.objectId, ref.contentVersion);
    }
    ScriptedObjectSource objects;

    const IRevisionClosureSource& closureIface = closure;

    // 归属对 1：S1 修订不存在（tryRevision nullopt）→ UnknownObject。
    // 断言归属码的冻结属性（token/注册码空/Context 类）而非仅名字——
    // "可定位"＝码面三张表可查（registryCode 空＝fail-fast 轨不发稳定码）。
    EXPECT_FALSE(closureIface.tryRevision(makeRevisionId(0xEE)).has_value());
    {
        const auto code = RuntimeErrorCode::UnknownObject;
        EXPECT_EQ(token(code), "runtime/unknown-object");
        EXPECT_TRUE(isContractViolation(code)) << "S1 归属码走契约违约（fail-fast）轨";
        EXPECT_EQ(category(code), RuntimeErrorCategory::Context);
    }

    // 归属对 2：S2 引用不在闭包（objectInRevision false）→ StructureInvalid
    // （CM-0 防混入：注入一个不在闭包内的 (oid,cv)——RT-CONT-1 的注入侧）。
    const auto strangerId = makeObjectId(0x99);
    EXPECT_FALSE(closureIface.objectInRevision(scripted.id, strangerId, makeVersion(0xB0)));
    {
        const auto code = RuntimeErrorCode::StructureInvalid;
        EXPECT_EQ(token(code), "runtime/structure-invalid");
        EXPECT_EQ(registryCode(code), "RT-STRUCTURE-INVALID");
        EXPECT_EQ(category(code), RuntimeErrorCategory::Input);
    }
    // 闭包内成员：true（正例——判定不是恒 false）。
    const auto& member = scripted.objectRefs.front();
    EXPECT_TRUE(closureIface.objectInRevision(scripted.id, member.objectId,
                                              member.contentVersion));

    // 归属对 3：S2 对象字节不可得（tryObjectBytes nullopt）→ InputInvalid
    // （含对象定位——字节缺失与结构非法两类归属严格分离）。
    EXPECT_FALSE(objects.tryObjectBytes(member.objectId, member.contentVersion).has_value());
    {
        const auto code = RuntimeErrorCode::InputInvalid;
        EXPECT_EQ(token(code), "runtime/input-invalid");
        EXPECT_EQ(registryCode(code), "RT-INPUT-INVALID");
        EXPECT_EQ(category(code), RuntimeErrorCategory::Input);
    }

    // 归属对 4：段边界取消（cancellationRequested true）→ Cancelled
    // （非错误路径：UX-03——取消诊断非 error 级，注册码为正常路径 RT-CANCELLED）。
    CancelToggle toggle;
    EXPECT_FALSE(toggle.cancellationRequested()) << "初始未取消";
    toggle.request();
    EXPECT_TRUE(toggle.cancellationRequested());
    {
        const auto code = RuntimeErrorCode::Cancelled;
        EXPECT_EQ(token(code), "runtime/cancelled");
        EXPECT_EQ(registryCode(code), "RT-CANCELLED");
        EXPECT_EQ(category(code), RuntimeErrorCategory::Cancelled);
        EXPECT_TRUE(isCancellation(code));
        EXPECT_FALSE(isContractViolation(code)) << "取消不是调用方违约";
    }
}

/** SRC-4 取消最终性：置位后保持 true（§3.3"只增不减"注入约束）。 */
TEST(SourcesCancel, ToggleIsFinal_RT_SRC_TASK01)
{
    CancelToggle toggle;
    const ICompileCancelToken& iface = toggle;
    // 多次轮询（编译器段边界行为模拟）：未置位前恒 false。
    for (int i = 0; i < 3; ++i) {
        EXPECT_FALSE(iface.cancellationRequested());
    }
    toggle.request();   // 经实现对象置位（execution 侧装配动作——接口只有只读查询）
    // 置位后多次轮询恒 true——取消不可反悔（最终性由令牌保证）。
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(iface.cancellationRequested());
    }
}

/** SRC-5 生命周期：经基类指针析构（虚析构契约——L5 装配期持有形态）。 */
TEST(SourcesLifetime, DeleteThroughBasePointer_RT_SRC)
{
    // 装配期创建（L5 持有）、编译请求经基类指针携持——析构必须经虚析构
    // 正确销毁实现类（内存正确性；ASAN 场景下泄漏即失败）。
    std::unique_ptr<IObjectBytesSource> objects = std::make_unique<ScriptedObjectSource>();
    std::unique_ptr<IRevisionClosureSource> closure = std::make_unique<ScriptedClosureSource>();
    std::unique_ptr<ICompileCancelToken> cancel = std::make_unique<CancelToggle>();
    EXPECT_NE(objects, nullptr);
    EXPECT_NE(closure, nullptr);
    EXPECT_NE(cancel, nullptr);
    // unique_ptr 析构（经虚析构）——无静态断言可表达，运行期正常返回即证据。
    SUCCEED() << "三个实现对象经基类指针构造与析构均正常";
}
