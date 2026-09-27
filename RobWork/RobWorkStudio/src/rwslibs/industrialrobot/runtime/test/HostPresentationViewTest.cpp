/**
 * @file   HostPresentationViewTest.cpp
 * @brief  宿主呈现视图（HostPresentationView——RT-T14，方案 B.1 D10）的
 *         RT-PRES 族用例（仅集成模式——被测面为真实编译链产物）：
 *         ①三类身份绑定（modelIdentity 对照快照/appliedRevisionId 对照
 *         工厂入参/presentationIdentity 每构造新生成）；②隔离（视图构建/
 *         销毁/重建不改变快照身份）；③反向隔离（呈现侧 State 副本修改不
 *         触快照内容）；④复用（反解查询与基座—世界变换同源——RT-NM 断言
 *         形态复用）；⑤工厂 fail-fast（空快照/全零修订整体失败）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/RT-T14.json acceptance 1~6（逐条对应见各
 *     用例的 IRD_TEST_INFO 与组注释）；runtime.md §12 RT-T14 行；
 *   - B1-SPEC §4.3（D10 隔离＋复用；三类身份"可验证绑定关系，不要求字段
 *     值相等"——v1.2 所有者二次审核口径；反向隔离；序列化旁路限定禁令）；
 *   - 先例：ContractSuiteTest 的编译夹具形态（td::ContractHarness＋
 *     compilePublished——本 TU 独立声明，不跨 TU 共享私有件）。
 *
 * 为什么仅集成模式：被测构造输入是真实编译链产物（Published 快照持
 *   rw::models::WorkCell）——冒烟模式无框架库可链（RT-T07/T08/RT-T12
 *   同因，CMake 增列处登记）。
 */

#include <gtest/gtest.h>

#include "CompilerImpl.hpp"         // 被测：产品编译器（单元私有头——测试与 src 同权）
#include "RuntimeTestDoubles.hpp"  // §11 规范替身（test/ 私有夹具——同目录引用）

#include <rw/models/SerialDevice.hpp>
#include <rw/kinematics/State.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/math/Q.hpp>

#include <sdurws/ird/runtime/Compiler.hpp>
#include <sdurws/ird/runtime/Errors.hpp>
#include <sdurws/ird/runtime/HostPresentationView.hpp>
#include <sdurws/ird/runtime/NameMap.hpp>
#include <sdurws/ird/runtime/Snapshot.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <memory>
#include <string>
#include <utility>

namespace {

using namespace sdurws::ird;
namespace td = sdurws::ird::runtime::testdoubles;
using td::ContractHarness;

// =====================================================================
// 共享助手（ContractSuiteTest 同款夹具形态——本 TU 独立声明）。
// =====================================================================

/// 编译夹具至已发布快照（Published 断言收敛点——失败即用例失败）。
std::shared_ptr<const runtime::RuntimeSnapshot> compilePublished(ContractHarness& h)
{
    td::ScriptedReader reader(h.description);
    td::ScriptedObjectSource objects = h.objectSource();
    td::ScriptedClosureSource closure = h.closureSource();
    runtime::CompileRequest req = h.makeRequest(reader, objects, closure);

    runtime::CanonicalModelCompiler compiler;
    const runtime::CompileOutcome out = compiler.compile(req);
    EXPECT_EQ(out.status, runtime::CompileStatus::Published)
        << "夹具应发布快照（诊断："
        << (out.diagnostics.empty() ? std::string{} : out.diagnostics.front().cause) << "）";
    return out.snapshot;
}

// =====================================================================
// RT-PRES-1：三类身份绑定（B1-SPEC §4.3 v1.1——可验证绑定关系）
// =====================================================================

/**
 * 身份绑定契约：modelIdentity 与快照同值（对照来源模型可验证）；
 * appliedRevisionId 等于工厂入参（对照已应用修订可验证）；presentation
 * Identity 每次构造新生成（重建即新身份）——同快照双视图的前两类相同、
 * 第三类必然不同（"不要求字段值相等"的绑定关系语义）。
 */
TEST(HostPresentationViewTest, IdentityBindingsVerifiable_RT_PRES_1)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-03", "ARC-04"}, {});
    ContractHarness h = ContractHarness::make();
    auto snapshot = compilePublished(h);

    auto view1 = runtime::createHostPresentationView(snapshot, h.revision);
    auto view2 = runtime::createHostPresentationView(snapshot, h.revision);

    // 绑定 1：modelIdentity 对照快照（同一事实的读取路径恒等）。
    EXPECT_EQ(snapshot->modelIdentity(), view1->modelIdentity());
    // 绑定 2：appliedRevisionId 对照工厂入参（宿主供给的归属事实原样承载）。
    EXPECT_EQ(h.revision, view1->appliedRevisionId());
    // 绑定 3：presentationIdentity 每构造新生成（非零且两视图互异——重建
    // 即新身份；快照未变时前两类恒等，恰是"绑定关系≠字段值相等"）。
    EXPECT_TRUE(view1->presentationIdentity().isValid());
    EXPECT_NE(view1->presentationIdentity(), view2->presentationIdentity());
    EXPECT_EQ(view1->modelIdentity(), view2->modelIdentity());
}

// =====================================================================
// RT-PRES-2：隔离（视图构建/销毁/重建零快照身份影响）
// =====================================================================

/**
 * 生命周期隔离契约（§9.1）：呈现视图不进运行身份/缓存身份/当前性判定——
 * 结构性证据＝快照身份在视图构建/销毁/重建前后逐位不变；视图销毁不影响
 * 快照存活（另一视图与原始句柄仍可读，引用计数共享）。
 */
TEST(HostPresentationViewTest, ViewLifecycleDoesNotTouchSnapshotIdentity_RT_PRES_2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-03"}, {});
    ContractHarness h = ContractHarness::make();
    auto snapshot = compilePublished(h);
    const auto identityBefore = snapshot->snapshotIdentity();

    auto view = runtime::createHostPresentationView(snapshot, h.revision);
    EXPECT_EQ(identityBefore, snapshot->snapshotIdentity())
        << "视图构建改变快照身份（隔离违约）";

    // 销毁重建：旧视图丢弃、新视图建立——快照身份仍逐位不变。
    view.reset();
    auto viewAgain = runtime::createHostPresentationView(snapshot, h.revision);
    EXPECT_EQ(h.revision, viewAgain->appliedRevisionId());
    EXPECT_EQ(identityBefore, snapshot->snapshotIdentity())
        << "视图重建改变快照身份（隔离违约）";
    EXPECT_EQ(identityBefore, snapshot->snapshotIdentity());
}

// =====================================================================
// RT-PRES-3：反向隔离（呈现侧 State 副本修改不触快照内容）
// =====================================================================

/**
 * 反向隔离契约（B1-SPEC §4.3 v1.1）：makeState() 是值拷贝——呈现会话对
 * 副本的任何修改（本用例以设备关节角注入为代表性修改——点动/播放驱动
 * 会话姿态的真实形态，ContractSuiteTest 同款 setQ 语义）不得改变快照
 * 内容身份；视图反解面与变换面也不受副本修改影响。
 */
TEST(HostPresentationViewTest, PresentationStateMutationIsolatedFromSnapshot_RT_PRES_3)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-03"}, {});
    ContractHarness h = ContractHarness::make();
    auto snapshot = compilePublished(h);
    auto view = runtime::createHostPresentationView(snapshot, h.revision);

    const auto contentIdentityBefore = snapshot->snapshotIdentity();
    const auto worldToBaseBefore = view->worldToBase();

    // 呈现侧修改 State 副本（设备关节角 setQ——State 值语义，不触 WorkCell）。
    const std::string deviceName = view->resolveObjectId(h.robot).get().fullName;
    const rw::core::Ptr<const rw::models::SerialDevice> device
        = view->workCell().findDevice(deviceName);
    ASSERT_TRUE(!device.isNull()) << "设备应存在: " << deviceName;
    const rw::math::Q q(2, 0.4, -0.9);  // 单位 rad（确定性构型，限位内）
    rw::kinematics::State presentationState = view->makeState();
    device->setQ(q, presentationState);

    // 快照内容身份与视图面逐位不变（修改只存在于呈现副本内——反向隔离）。
    EXPECT_EQ(contentIdentityBefore, snapshot->snapshotIdentity())
        << "呈现副本修改改变快照内容（反向隔离违约）";
    EXPECT_EQ(worldToBaseBefore, view->worldToBase());
}

// =====================================================================
// RT-PRES-4：复用（反解与基座—世界变换同源——RT-NM 断言形态复用）
// =====================================================================

/**
 * 复用契约（D10/AT-37）：视图反解查询与快照 nameMap 直查**逐字节一致**
 * （同一 RuntimeNameMap 实例的转发——RT-NM 双向往返断言形态）；worldTo
 * Base/baseToWorld 与计算侧同值（P-RT-4 单点——零二次变换）。
 */
TEST(HostPresentationViewTest, ResolutionAndTransformReuseSameSource_RT_PRES_4)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-03", "ARC-04"}, {});
    ContractHarness h = ContractHarness::make();
    auto snapshot = compilePublished(h);
    auto view = runtime::createHostPresentationView(snapshot, h.revision);

    // 正解：视图路径 vs 快照直查——同值（AT-37 单一消费口径的结构面）。
    const auto viaView = view->resolveObjectId(h.robot);
    const auto viaSnapshot = snapshot->nameMap().resolveObjectId(h.robot);
    ASSERT_TRUE(viaView.ok());
    ASSERT_TRUE(viaSnapshot.ok());
    EXPECT_EQ(viaSnapshot.get().fullName, viaView.get().fullName);

    // 反解：经视图正解所得名回查对象——ObjectId 往返一致（RT-NM 形态）。
    const auto back = view->resolveRuntimeName(viaView.get().fullName);
    ASSERT_TRUE(back.ok());
    EXPECT_EQ(h.robot, back.get().objectId);

    // 未命中显性化：视图路径与快照路径同判 UnknownObject（不默认命中）。
    const auto missViaView = view->resolveObjectId(core::ObjectId::generate());
    const auto missViaSnapshot = snapshot->nameMap().resolveObjectId(core::ObjectId::generate());
    EXPECT_FALSE(missViaView.ok());
    EXPECT_FALSE(missViaSnapshot.ok());

    // 基座—世界变换同源（P-RT-4——视图零本地变换算术）。
    EXPECT_EQ(snapshot->worldToBase(), view->worldToBase());
    EXPECT_EQ(snapshot->baseToWorld(), view->baseToWorld());
}

// =====================================================================
// RT-PRES-5：工厂 fail-fast（呈现构造失败整体失败不返回半成品）
// =====================================================================

/**
 * fail-fast 契约（acceptance 6）：空快照（非 Published 产物的调用方违约）
 * 与全零修订（不可对账的归属事实）都在工厂即抛 RuntimeError(InputInvalid)
 * ——不产出半成品视图。
 */
TEST(HostPresentationViewTest, FactoryFailsFastOnInvalidInput_RT_PRES_5)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-03"}, {});
    ContractHarness h = ContractHarness::make();
    auto snapshot = compilePublished(h);

    // 空快照：调用方错误（须为 Published 产物）。
    try {
        runtime::createHostPresentationView(nullptr, h.revision);
        FAIL() << "空快照应被工厂拒绝";
    } catch (const runtime::RuntimeError& e) {
        EXPECT_EQ(runtime::RuntimeErrorCode::InputInvalid, e.code());
    }

    // 全零修订：呈现视图必须可对账到具体修订（isValid()==false 即违约）。
    try {
        runtime::createHostPresentationView(snapshot, core::RevisionId{});
        FAIL() << "全零修订应被工厂拒绝";
    } catch (const runtime::RuntimeError& e) {
        EXPECT_EQ(runtime::RuntimeErrorCode::InputInvalid, e.code());
    }
}

}  // namespace
