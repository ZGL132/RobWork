/**
 * @file   PresentationBridgeContractTest.cpp
 * @brief  宿主运行时发布桥（UI-T20）与 runtime RT-T14 契约面的对接契约
 *         （集成模式专属——HostPresentationView.hpp 依赖 rw 头，冒烟模式
 *         无框架库可链）：①ui 端口面消费的 RT-T14 面形状钉（编译期——
 *         工厂签名/三类身份访问器/反解查询面任一漂移即编译失败，P-PR-7
 *         "对端单侧冻结互为起点不私改"同款钉）；②工厂唯一入口的实例级
 *         契约（空快照 fail-fast——RuntimeError/InputInvalid，P-RT-4"呈现
 *         侧私建第二构造路径禁止"的入口行为锚）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T20.json acceptance 1/2（"宿主呈现
 *     视图（RT-T14 HostPresentationView）"的对接半区；模型套件
 *     RuntimePublishBridgeTest 以替身承载编排语义——对端形状漂移由本 TU
 *     编译期捕获，UI-T14 PeerBridgeContractTest 先例同型）；
 *   - units/runtime.md §12 RT-T14 行（工厂唯一构造入口＋三类身份只读
 *     对账面）、§8.3（只读视图）；
 *   - O-31 裁决的测试侧惯例：对端公共头直链归测试目标（产品面零对端
 *     知识不变）——runtime 直链为本目标集成模式专属增列（ird_gates SUB
 *     命中随任务登记，登记提交件 traceability/wp10-t20-gate-registrations.md）。
 *
 * 诚实边界（真实视图轮次的不可达性登记）：RT-T14 具体编译器
 *   （CanonicalModelCompiler）与 RuntimeSnapshot 构造均为 runtime 单元
 *   私有（R-2 红线——跨单元私有头禁止），公共面无"从规范模型直达已发布
 *   快照"的入口——ui 契约测试无法在越界前提下构造真实视图实例。因此本
 *   TU 的对接证据＝编译期形状钉＋工厂入口行为锚；真实视图的全链路轮次
 *   由（a）runtime 侧 RT-PRES 族用例（HostPresentationViewTest，同单元
 *   私有夹具）与（b）后续装配任务的 L5 适配器（UI-T21+ 消费本桥时落位）
 *   承载。登记义务见落位登记注。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Identity.hpp>

#include <sdurws/ird/runtime/Errors.hpp>
#include <sdurws/ird/runtime/HostPresentationView.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/RuntimePublishBridge.hpp>

#include <memory>
#include <optional>
#include <string>
#include <type_traits>

namespace {

using namespace sdurws::ird;
namespace runtime = sdurws::ird::runtime;

// =====================================================================
// 形状钉（编译期——ui 端口面消费的 RT-T14 面漂移即编译失败）
// =====================================================================

// 工厂唯一入口签名（适配器只允许经此构造呈现——P-RT-4 的类型层锚点；
// RuntimePublishBridgeTest 的 source 替身按本签名折叠投影）。
static_assert(std::is_same_v<
              decltype(&runtime::createHostPresentationView),
              runtime::HostPresentationViewHandle (*)(std::shared_ptr<const runtime::RuntimeSnapshot>,
                                                      core::RevisionId)>,
              "RT-T14 工厂签名漂移：ui 适配面契约需随增量修订同步（P-PR-7 同款钉）");
// 三类身份访问器（投影三字段的语义源——访问器签名漂移即编译失败）。
static_assert(std::is_same_v<decltype(&runtime::HostPresentationView::modelIdentity),
                             const core::ContentIdentity& (runtime::HostPresentationView::*)()
                                 const noexcept>,
              "modelIdentity 访问器漂移（B1-SPEC §4.3 绑定①的语义源）");
static_assert(std::is_same_v<decltype(&runtime::HostPresentationView::appliedRevisionId),
                             const core::RevisionId& (runtime::HostPresentationView::*)()
                                 const noexcept>,
              "appliedRevisionId 访问器漂移（B1-SPEC §4.3 绑定②的语义源）");
static_assert(std::is_same_v<decltype(&runtime::HostPresentationView::presentationIdentity),
                             const core::ObjectId& (runtime::HostPresentationView::*)()
                                 const noexcept>,
              "presentationIdentity 访问器漂移（B1-SPEC §4.3 绑定③的语义源）");
// 反解查询面（投影 objectExists 闭包的语义源——判别形漂移即编译失败）。
static_assert(std::is_same_v<decltype(&runtime::HostPresentationView::resolveObjectId),
                             runtime::Expected<runtime::RuntimeName, runtime::RuntimeNameError> (
                                 runtime::HostPresentationView::*)(core::ObjectId) const noexcept>,
              "resolveObjectId 反解面漂移（NameMap 同源存在性的语义锚）");
// 只读视图面（outlet 适配器交给宿主的载体来源——漂移即编译失败）。
static_assert(std::is_same_v<decltype(&runtime::HostPresentationView::workCell),
                             const runtime::WorkCellConstView& (runtime::HostPresentationView::*)()
                                 const noexcept>,
              "workCell 只读视图面漂移（宿主刷新载体的来源契约）");

// =====================================================================
// 工厂唯一入口的实例级契约（公共面可达的行为锚）
// =====================================================================

/** 空快照 fail-fast：工厂对空快照整体拒绝（RuntimeError——调用方错误
 *  轨）——"呈现构造只能从已发布快照出发"在入口的行为证明（无半成品）。 */
TEST(PresentationBridgeContract, FactoryRejectsNullSnapshot_UI_T20_CTR)
{
    const std::optional<core::RevisionId> revision = core::RevisionId::tryFromCanonical(
        std::string("rev-") + std::string(32, 'a'));
    ASSERT_TRUE(revision.has_value()) << "测试修订应可解析（合法词法）";
    // 工厂对空快照 fail-fast（RuntimeError 码＝InputInvalid——RT-T14 工厂
    // 契约原文"呈现构造失败整体失败不返回半成品"）。RuntimeError 不可
    // 被空快照路径绕过＝P-RT-4 的入口行为锚。
    EXPECT_THROW((void)runtime::createHostPresentationView(nullptr, *revision),
                 runtime::RuntimeError);
}

}  // namespace
