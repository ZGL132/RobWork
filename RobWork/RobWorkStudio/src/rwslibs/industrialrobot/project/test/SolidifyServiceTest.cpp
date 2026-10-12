/**
 * @file   SolidifyServiceTest.cpp
 * @brief  外部资源固化服务用例组（单元面）——§5.7 流程编排：成功固化
 *         （副本入对象库＋恰一新修订＋引用保护）、替换版本（同 oid 新
 *         cv）、复制期间源变化中止（NFR-REL-04/V17）、缺失中止（V26）、
 *         预算超限中止（V06/V16 语义）、探测透传、判据数据面（EVI-01
 *         数据半区——未固化＝不满足）、装配协议与调用方错误 fail-fast。
 *
 * 设计依据：
 *   - units/project.md §5.7（冻结签名与流程四段）、§4.6（对象库编址/
 *     引用保护——副本可读性与同 oid 多 cv）、§6.5（注册协议——token
 *     语法与重复注册拒绝）、§11（用例注明需求/AT——本组锚点见各用例
 *     注释；V17/V26 语义承载于 SolidifyStubs fake）；
 *   - 需求 CON-03（项目内不可变副本＋引用保护）、PM-01（转正式固化）、
 *     NFR-REL-04（外部源缺失/变化可检测）、EVI-01（未固化阻断正式
 *     结论——本组验证判据数据面，判定归 evidence，N-9）；
 *   - 任务契约 tasks/foundation/PRJ-T17.json acceptance 1/2/3。
 *
 * 测试口径登记（DTB §5.4）：
 *   1. fake 外部源承载 io 语义（SolidifyStubs.hpp——零 io 头消费的注入
 *      缝自证）；重关联显式提交与只读拒绝归契约面（SolidifyContractTest，
 *      §3.3 跨单元契约测试面分工）。
 *   2. "零修订"观测点＝磁盘修订目录计数（ServiceFixture 先例）；"副本
 *      可读"观测点＝查询端口 tryObject（D-10 原样字节）。
 */

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/project/PersistenceFormat.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/project/ResourceRefs.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

#include "SolidifyStubs.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace sdurws::ird::project {

using stub::FakeExternalSource;
using stub::SolidifyFixture;
using stub::testDigestOf;
using stub::testHex64;

namespace {

/// 字节序列构造（确定性内容——"solidify-payload-<n>" 的字节形态）。
std::vector<std::uint8_t> bytesOf(const std::string& text)
{
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

}  // namespace

// =====================================================================
// 成功固化（CON-03——acceptance 1 正向）
// =====================================================================

/**
 * 用例：成功固化发布不可变副本并产生恰一新修订。
 * 需求/AT：CON-03、PM-01（转正式固化）；§5.7 流程四段正向。
 * 前置：可写上下文＋fake 源注册内容＋recorded 记录。
 * 操作：service->solidify（目标全零＝新分配路径）。
 * 预期：ok=true；新修订恰一个；副本对象以 "ExternalResource" token 入
 *   修订引用集；tryObject 读回字节与源一致（D-10 原样存储）；提交事件
 *   FIFO 恰 +2（RevisionCommitted→DependencyInvalidated，§6.8）。
 * 观测点：修订目录计数＋引用集＋tryObject＋事件计数。
 */
TEST(SolidifyServiceTest, SolidifySuccess_PublishesImmutableCopyAndNewRevision)
{
    SolidifyFixture f;
    const std::vector<std::uint8_t> meshBytes = bytesOf("mesh-bytes-v1");
    f.source.entries["res-mesh-1"] = {meshBytes, false};
    const ExternalResourceRecord record = f.makeRecord("res-mesh-1", meshBytes);

    const std::size_t revisionsBefore = f.revisionDirCount();
    const std::size_t objectsBefore = f.objectDirCount();
    const std::size_t eventsBefore = f.events.events.size();

    const SolidifyResult result
        = f.service->solidify(f.makeRequest("res-mesh-1"), f.primaryBranch(), record);

    // ok＋materializedVersion 有效（冻结签名字段语义）。
    ASSERT_TRUE(result.ok) << "成功固化应返回 ok=true";
    ASSERT_TRUE(result.materializedVersion.isValid());
    EXPECT_FALSE(result.sourceChangedDuringCopy.has_value())
        << "正常路径无复制期间变化（nullopt 语义）";
    // 恰一新修订（§6.1 S6——"成功恰好产生一个新修订"）。对象目录 +2：
    // 固化副本 oid＋本次提交的新元数据版本 oid（TxEngine 每次 commit 以
    // 新 ObjectId 生成元数据对象——实现事实，修订闭包谱系链的载体）。
    EXPECT_EQ(f.revisionDirCount(), revisionsBefore + 1);
    EXPECT_EQ(f.objectDirCount(), objectsBefore + 2);
    // 提交事件 FIFO：RevisionCommitted→DependencyInvalidated（§6.8/D-18
    // 发布面——参考总线计数观测）。
    EXPECT_EQ(f.events.events.size(), eventsBefore + 2);
    // 引用保护面：副本在 HEAD 引用集中，token＝ExternalResource（实现
    // 口径③），且 tryObject 读回与源字节逐一致（D-10）。
    const RevisionView head = f.opened.store->query().head();
    const ObjectRef* copyRef = nullptr;
    for (const ObjectRef& ref : head.objectRefs) {
        if (ref.contentVersion == result.materializedVersion) {
            copyRef = &ref;
            break;
        }
    }
    ASSERT_NE(copyRef, nullptr) << "固化副本必须被新修订引用（引用保护成立前提）";
    EXPECT_EQ(copyRef->objectTypeToken, "ExternalResource");
    const std::optional<std::vector<std::uint8_t>> stored
        = f.opened.store->query().tryObject(copyRef->objectId,
                                            result.materializedVersion);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(*stored, meshBytes) << "对象负载＝副本原样字节（D-10）";
}

/**
 * 用例：替换版本——同 oid 新 cv（§5.7"固化副本对象（新分配或替换
 *   版本）"后半；对象库内容编址的谱系连续面）。
 * 需求/AT：CON-03；§4.6"复用既有 ObjectId＋新内容版本"。
 * 前置：首次固化已完成（显式目标 oid）；源内容更新（新登记记录）。
 * 操作：以同一 oid 再次固化（新记录＋新内容）。
 * 预期：ok=true；新修订引用同 oid 的**新** cv；旧 cv 副本仍可读（对象
 *   库只增——PA-2，旧引用永不被覆盖）。
 * 观测点：findRefByObject＋tryObject 双版本。
 */
TEST(SolidifyServiceTest, SolidifyReplacementVersion_SameObjectNewContentVersion)
{
    SolidifyFixture f;
    const std::vector<std::uint8_t> v1 = bytesOf("mesh-bytes-v1");
    f.source.entries["res-mesh-1"] = {v1, false};
    const core::ObjectId target = core::ObjectId::generate();

    const SolidifyResult first = f.service->solidify(
        f.makeRequest("res-mesh-1", target), f.primaryBranch(),
        f.makeRecord("res-mesh-1", v1));
    ASSERT_TRUE(first.ok);

    // 源内容更新（外部文件被替换——用户重新登记后以同 oid 替换版本）。
    const std::vector<std::uint8_t> v2 = bytesOf("mesh-bytes-v2-updated");
    f.source.entries["res-mesh-1"] = {v2, false};

    const SolidifyResult second = f.service->solidify(
        f.makeRequest("res-mesh-1", target), f.primaryBranch(),
        f.makeRecord("res-mesh-1", v2));
    ASSERT_TRUE(second.ok);
    EXPECT_NE(second.materializedVersion, first.materializedVersion)
        << "内容不同则内容版本必不同（内容编址，§4.6）";

    // 同 oid 两个内容版本并存且都可读（只增发布——PA-2）。
    const RevisionView head = f.opened.store->query().head();
    const ObjectRef* newRef = SolidifyFixture::findRef(head, target,
                                                       second.materializedVersion);
    ASSERT_NE(newRef, nullptr) << "新版本被 HEAD 引用";
    const ObjectRef* oldRef = SolidifyFixture::findRefByObject(head, target);
    EXPECT_EQ(oldRef->contentVersion, second.materializedVersion)
        << "HEAD 引用集只含最新版本引用（全量引用集语义）";
    EXPECT_TRUE(f.opened.store->query().tryObject(target, first.materializedVersion)
                    .has_value())
        << "旧版本对象仍在对象库（只增——历史修订引用可达）";
    const std::optional<std::vector<std::uint8_t>> storedV2
        = f.opened.store->query().tryObject(target, second.materializedVersion);
    ASSERT_TRUE(storedV2.has_value());
    EXPECT_EQ(*storedV2, v2);
}

// =====================================================================
// 复制期间源变化（NFR-REL-04/V17——acceptance 1 变化半区）
// =====================================================================

/**
 * 用例：复制期间源变化 → 固化中止、零修订、无部分副本。
 * 需求/AT：NFR-REL-04（变化可检测）、CON-03（失败不产生半副本）；
 *   io.md V17 语义（sourceChangedDuringCopy＋Changed 诊断＋中转清理）。
 * 前置：fake 置 changedDuringCopy（复制后快照 B 与 A 不一致）。
 * 操作：solidify。
 * 预期：ok=false；sourceChangedDuringCopy 有值且＝复制后快照哈希（新
 *   内容对照面）；诊断透传 IO-RES-CHANGED；修订/对象目录零增长（命令
 *   未提交——实现口径①"读取在命令外"的零修订保证）。
 * 观测点：result 字段＋磁盘计数。
 */
TEST(SolidifyServiceTest, SourceChangedDuringCopy_AbortedWithZeroRevision)
{
    SolidifyFixture f;
    const std::vector<std::uint8_t> meshBytes = bytesOf("mesh-bytes-v1");
    f.source.entries["res-mesh-1"] = {meshBytes, false};
    f.source.changedDuringCopy = true;  // V17 注入：复制后源被改写
    const ExternalResourceRecord record = f.makeRecord("res-mesh-1", meshBytes);

    const std::size_t revisionsBefore = f.revisionDirCount();
    const std::size_t objectsBefore = f.objectDirCount();

    const SolidifyResult result
        = f.service->solidify(f.makeRequest("res-mesh-1"), f.primaryBranch(), record);

    // 冻结签名字段：变化检出＝有值，且值为复制后快照哈希（新内容）。
    ASSERT_FALSE(result.ok);
    ASSERT_TRUE(result.sourceChangedDuringCopy.has_value());
    // 变化后的内容＝翻转首字节的变异体——fake 的 B 快照即该内容。
    std::vector<std::uint8_t> mutated = meshBytes;
    mutated[0] = static_cast<std::uint8_t>(mutated[0] ^ 0xFF);
    EXPECT_EQ(*result.sourceChangedDuringCopy, testDigestOf(mutated));
    // io 诊断透传（§5.7"project 透传并中止固化"——码值不改写）。
    bool hasChangedDiag = false;
    for (const auto& diag : result.diagnostics) {
        if (diag.code == "IO-RES-CHANGED") {
            hasChangedDiag = true;
        }
    }
    EXPECT_TRUE(hasChangedDiag) << "Changed 比较型诊断随结果透传";
    // 零修订零对象（无部分副本——中转清理后发布面无痕）。
    EXPECT_EQ(f.revisionDirCount(), revisionsBefore);
    EXPECT_EQ(f.objectDirCount(), objectsBefore);
    // 中止后未固化记录的判据数据仍为"不满足"（EVI-01 数据半区）。
    const ExternalResourceGateData gate
        = externalResourceGateData(record, f.opened.store->query().head());
    EXPECT_FALSE(gate.gateSatisfied);
}

// =====================================================================
// 缺失与预算超限（V26/V06-V16 语义——acceptance 1 缺失/预算半区）
// =====================================================================

/**
 * 用例：外部源缺失 → 探测 Missing＋固化中止零修订。
 * 需求/AT：NFR-REL-04（缺失可检测）、PM-09（重关联触发数据）。
 * 前置：fake 条目置 missing。
 * 操作：probe＋solidify。
 * 预期：probe=Missing；solidify ok=false＋IO-RES-MISSING 透传＋零修订。
 */
TEST(SolidifyServiceTest, MissingSource_AbortedWithZeroRevision)
{
    SolidifyFixture f;
    const std::vector<std::uint8_t> meshBytes = bytesOf("missing-mesh");
    f.source.entries["res-mesh-1"] = {meshBytes, true};  // V26 注入：缺失
    const ExternalResourceRecord record = f.makeRecord("res-mesh-1", meshBytes);

    // 探测透传（PM-09 重关联触发数据面——检测判定归 io，服务直通）。
    EXPECT_EQ(f.service->probe(record), ExternalRefProbe::Missing);

    const std::size_t revisionsBefore = f.revisionDirCount();
    const SolidifyResult result
        = f.service->solidify(f.makeRequest("res-mesh-1"), f.primaryBranch(), record);

    ASSERT_FALSE(result.ok);
    EXPECT_FALSE(result.sourceChangedDuringCopy.has_value())
        << "缺失路径不伪造变化检测事实";
    bool hasMissingDiag = false;
    for (const auto& diag : result.diagnostics) {
        if (diag.code == "IO-RES-MISSING") {
            hasMissingDiag = true;
        }
    }
    EXPECT_TRUE(hasMissingDiag);
    EXPECT_EQ(f.revisionDirCount(), revisionsBefore) << "中止＝零修订";
}

/**
 * 用例：预算超限 → 固化中止零修订。
 * 需求/AT：NFR-REL-04 族（预算防护面——io.md V06/V16 语义的 project
 *   侧透传半区）。
 * 前置：源内容 1000 字节、预算 100 字节（SolidifyRequest.budgetBytes，
 *   单位＝字节）。
 * 操作：solidify。
 * 预期：ok=false＋IO-SEC-BUDGET-EXCEEDED 透传＋零修订。
 */
TEST(SolidifyServiceTest, BudgetExceeded_AbortedWithZeroRevision)
{
    SolidifyFixture f;
    const std::vector<std::uint8_t> bigBytes = bytesOf(std::string(1000, 'x'));
    f.source.entries["res-big-1"] = {bigBytes, false};
    const ExternalResourceRecord record = f.makeRecord("res-big-1", bigBytes);

    const std::size_t revisionsBefore = f.revisionDirCount();
    // 预算 100 字节 < 源 1000 字节（单位＝字节）。
    const SolidifyResult result = f.service->solidify(
        f.makeRequest("res-big-1", core::ObjectId{}, 100), f.primaryBranch(), record);

    ASSERT_FALSE(result.ok);
    bool hasBudgetDiag = false;
    for (const auto& diag : result.diagnostics) {
        if (diag.code == "IO-SEC-BUDGET-EXCEEDED") {
            hasBudgetDiag = true;
        }
    }
    EXPECT_TRUE(hasBudgetDiag);
    EXPECT_EQ(f.revisionDirCount(), revisionsBefore) << "预算中止＝零修订";
}

// =====================================================================
// 探测透传与判据数据面（NFR-REL-04 / EVI-01 数据半区——acceptance 2）
// =====================================================================

/**
 * 用例：探测三态透传（Ok/Changed/Missing——digest 权威判据）。
 * 需求/AT：NFR-REL-04（检测数据源——ExternalResourceRecord 持久化
 *   字段供 io 比对，§2.2 承接行）。
 * 操作：对三种 fake 形态逐一 probe。
 * 预期：登记哈希＝现内容→Ok；内容改写→Changed；缺失→Missing。
 */
TEST(SolidifyServiceTest, Probe_PassthroughThreeStates)
{
    SolidifyFixture f;
    const std::vector<std::uint8_t> meshBytes = bytesOf("probe-mesh");
    f.source.entries["res-p1"] = {meshBytes, false};
    const ExternalResourceRecord record = f.makeRecord("res-p1", meshBytes);

    // 未变：登记哈希与现内容一致 → Ok。
    EXPECT_EQ(f.service->probe(record), ExternalRefProbe::Ok);
    // 已变：外部内容被改写（登记哈希不变）→ Changed（digest 权威判据）。
    f.source.entries["res-p1"] = {bytesOf("probe-mesh-tampered"), false};
    EXPECT_EQ(f.service->probe(record), ExternalRefProbe::Changed);
    // 缺失：条目置 missing → Missing。
    f.source.entries["res-p1"] = {meshBytes, true};
    EXPECT_EQ(f.service->probe(record), ExternalRefProbe::Missing);
}

/**
 * 用例：判据数据面两半区（EVI-01——"未固化阻断正式结论"的数据来源）。
 * 需求/AT：EVI-01 证据不足口径、CON-03；N-9 边界（本面只供数据，
 *   判定归 evidence——用例断言的是数据事实而非"阻断"结论）。
 * 操作：对（a）recorded 记录、（b）已固化记录、（c）materialized 但
 *   引用不属于目标修订的记录，分别计算 externalResourceGateData。
 * 预期：（a）gateSatisfied=false；（b）state/copy 双真→true；（c）
 *   copyReferencedByRevision=false→false（引用保护必须在目标修订上
 *   成立，单看状态不够）。
 */
TEST(SolidifyServiceTest, GateData_NotMaterializedVersusReferenced)
{
    SolidifyFixture f;
    const std::vector<std::uint8_t> meshBytes = bytesOf("gate-mesh");
    f.source.entries["res-gate"] = {meshBytes, false};
    const ExternalResourceRecord recorded = f.makeRecord("res-gate", meshBytes);
    const RevisionView headBefore = f.opened.store->query().head();

    // (a) 未固化：state=recorded → 判据不满足（阻断数据面成立）。
    const ExternalResourceGateData gateBefore
        = externalResourceGateData(recorded, headBefore);
    EXPECT_EQ(gateBefore.state, ExternalResourceState::Recorded);
    EXPECT_FALSE(gateBefore.copyReferencedByRevision);
    EXPECT_FALSE(gateBefore.gateSatisfied);

    // 固化 → 统一记录更新（state/oid/cv 由调用方按结果回填）。
    const SolidifyResult result
        = f.service->solidify(f.makeRequest("res-gate"), f.primaryBranch(), recorded);
    ASSERT_TRUE(result.ok);
    const RevisionView headAfter = f.opened.store->query().head();
    // 反查副本 oid（新分配路径——引用集中按 cv 定位）。
    const ObjectRef* copyRef = nullptr;
    for (const ObjectRef& ref : headAfter.objectRefs) {
        if (ref.contentVersion == result.materializedVersion) {
            copyRef = &ref;
            break;
        }
    }
    ASSERT_NE(copyRef, nullptr);
    ExternalResourceRecord materialized = recorded;
    materialized.state = ExternalResourceState::Materialized;
    materialized.resourceObject = copyRef->objectId;
    materialized.materializedVersion = result.materializedVersion;

    // (b) 已固化且副本被目标修订引用 → 判据满足。
    const ExternalResourceGateData gateAfter
        = externalResourceGateData(materialized, headAfter);
    EXPECT_EQ(gateAfter.state, ExternalResourceState::Materialized);
    EXPECT_TRUE(gateAfter.copyReferencedByRevision);
    EXPECT_TRUE(gateAfter.gateSatisfied);

    // (c) materialized 状态但引用不属于目标修订 → 判据不满足
    // （gateSatisfied 是"状态＋引用可达"的合取——单状态不充分）。
    ExternalResourceRecord stale = materialized;
    stale.materializedVersion = core::ContentVersion::fromCanonical(
        std::string("cv-") + std::string(64, '0'));
    const ExternalResourceGateData gateStale
        = externalResourceGateData(stale, headAfter);
    EXPECT_FALSE(gateStale.copyReferencedByRevision);
    EXPECT_FALSE(gateStale.gateSatisfied);
}

// =====================================================================
// 草稿形态映射与 token 词表（§4.4.5 互转——acceptance 3 公共头面）
// =====================================================================

/**
 * 用例：统一形态↔草稿形态投影 round-trip（§4.4.5 冻结字段集合；固化
 *   结果字段不进草稿形态）＋state token 词表互转。
 * 需求/AT：NFR-REL-04（记录持久化形态）、§4.4.5 字段级契约。
 * 操作：makeRecord → toDraftExternalRef → externalResourceFromDraft。
 * 预期：五字段逐一相等；state token="recorded" 回读 Recorded；未知
 *   token → nullopt（不猜测）；materialized 记录投影后 oid/cv 不出现
 *   在草稿形态（§4.4.5 冻结面）。
 */
TEST(SolidifyServiceTest, DraftRecordProjection_RoundTripAndTokenTable)
{
    SolidifyFixture f;
    const std::vector<std::uint8_t> meshBytes = bytesOf("draft-projection");
    const ExternalResourceRecord record = f.makeRecord("res-proj", meshBytes);

    // token 词表两态互转。
    EXPECT_STREQ(toToken(ExternalResourceState::Recorded), "recorded");
    EXPECT_STREQ(toToken(ExternalResourceState::Materialized), "materialized");
    EXPECT_EQ(externalResourceStateFromToken("recorded"),
              ExternalResourceState::Recorded);
    EXPECT_EQ(externalResourceStateFromToken("materialized"),
              ExternalResourceState::Materialized);
    EXPECT_FALSE(externalResourceStateFromToken("frozen").has_value())
        << "未知 token 不猜测（严格词表）";

    // 统一→草稿：§4.4.5 字段逐一相等。
    const ExternalRefRecord draft = toDraftExternalRef(record);
    EXPECT_EQ(draft.externalRefId, record.externalRefId);
    EXPECT_EQ(draft.absolutePath, record.absolutePath);
    EXPECT_EQ(draft.contentHash256, record.contentHash256);
    EXPECT_EQ(draft.sizeBytes, record.sizeBytes);
    EXPECT_EQ(draft.recordedAtUtc, record.recordedAtUtc);
    EXPECT_EQ(draft.state, "recorded");

    // 草稿→统一：round-trip 相等（recorded 态）；materialized 记录的
    // 固化结果字段不进草稿形态（投影后 oid/cv 恒零——不伪造）。
    const auto roundTrip = externalResourceFromDraft(draft);
    ASSERT_TRUE(roundTrip.has_value());
    EXPECT_EQ(*roundTrip, record);
    ExternalResourceRecord materialized = record;
    materialized.state = ExternalResourceState::Materialized;
    materialized.resourceObject = core::ObjectId::generate();
    materialized.materializedVersion = core::ContentVersion::fromCanonical(
        std::string("cv-") + std::string(64, 'a'));
    const ExternalRefRecord draftMaterialized = toDraftExternalRef(materialized);
    EXPECT_EQ(draftMaterialized.state, "materialized");
    const auto back = externalResourceFromDraft(draftMaterialized);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->state, ExternalResourceState::Materialized);
    EXPECT_FALSE(back->resourceObject.isValid());
    EXPECT_FALSE(back->materializedVersion.isValid())
        << "草稿形态不携带固化结果（§4.4.5 冻结字段集合）";
}

// =====================================================================
// 装配协议与调用方错误（§6.5/§5.3.5——fail-fast 面）
// =====================================================================

/**
 * 用例：注册协议——token 遵守冻结语法、注册后可查、重复创建服务
 *   fail-fast。
 * 需求/AT：§6.5（注册协议）、§4.4.4（token 语法 ^[a-z0-9-]{3,64}——
 *   O-35 无点 kebab 口径）、§5.3.5（重复注册边界拒绝）。
 * 操作：registeredCommandTypes 检查＋二次 createSolidifyService。
 * 预期：两 token 在册且语法合规；重复创建抛 std::invalid_argument。
 */
TEST(SolidifyServiceTest, RegistrationProtocol_TokensRegisteredDuplicateFailsFast)
{
    SolidifyFixture f;

    // token 在册（§6.5——注册经 HandlerRegistry 装配通道）。
    const std::vector<std::string> registered
        = f.opened.store->commands().registeredCommandTypes();
    const auto contains = [&registered](const char* token) {
        for (const auto& t : registered) {
            if (t == token) {
                return true;
            }
        }
        return false;
    };
    EXPECT_TRUE(contains(kSolidifyCommandType));
    EXPECT_TRUE(contains(kRelinkCommandType));

    // token 语法自检：^[a-z0-9-]{3,64}$（§4.4.4 冻结语法——无点 kebab，
    // O-35 裁决口径；手工判定免 regex 依赖）。
    const auto syntaxOk = [](const char* token) {
        const std::string t(token);
        if (t.size() < 3 || t.size() > 64) {
            return false;
        }
        for (const char c : t) {
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
                return false;
            }
        }
        return true;
    };
    EXPECT_TRUE(syntaxOk(kSolidifyCommandType));
    EXPECT_TRUE(syntaxOk(kRelinkCommandType));

    // 重复创建服务＝同 token 重复注册 → std::invalid_argument（fail-fast）。
    FakeExternalSource anotherSource;
    EXPECT_THROW(createSolidifyService(*f.opened.store, &anotherSource),
                 std::invalid_argument);
}

/**
 * 用例：调用方错误 fail-fast——请求/记录失配、零预算、已固化记录。
 * 需求/AT：AGENTS §3 错误二分（调用方契约违约＝异常 fail-fast，不走
 *   返回值轨）。
 * 操作：四路非法调用。
 * 预期：一律 std::invalid_argument 且零修订（fail-fast 先于提交）。
 */
TEST(SolidifyServiceTest, InvalidArguments_FailFast)
{
    SolidifyFixture f;
    const std::vector<std::uint8_t> meshBytes = bytesOf("failfast-mesh");
    f.source.entries["res-ff"] = {meshBytes, false};
    const ExternalResourceRecord record = f.makeRecord("res-ff", meshBytes);
    const std::size_t revisionsBefore = f.revisionDirCount();

    // （a）externalRefId 空。
    SolidifyRequest emptyId = f.makeRequest("res-ff");
    emptyId.externalRefId.clear();
    EXPECT_THROW((void)f.service->solidify(emptyId, f.primaryBranch(), record),
                 std::invalid_argument);
    // （b）请求与记录失配。
    EXPECT_THROW((void)f.service->solidify(f.makeRequest("res-other"),
                                           f.primaryBranch(), record),
                 std::invalid_argument);
    // （c）零预算（实现口径：不代默认值——调用方显式给值）。
    EXPECT_THROW((void)f.service->solidify(f.makeRequest("res-ff", core::ObjectId{}, 0),
                                           f.primaryBranch(), record),
                 std::invalid_argument);
    // （d）已固化记录重复固化（状态迁移只能经命令——翻新走 relink）。
    SolidifyResult first;
    EXPECT_NO_THROW(first = f.service->solidify(
                        f.makeRequest("res-ff"), f.primaryBranch(), record));
    ASSERT_TRUE(first.ok);
    ExternalResourceRecord materialized = record;
    materialized.state = ExternalResourceState::Materialized;
    materialized.materializedVersion = first.materializedVersion;
    // 反查 oid（oid 未知——materialized.resourceObject 未回填也必须被
    // 状态检查拒绝：fail-fast 在状态面而非字段完整性面）。
    materialized.resourceObject = core::ObjectId::generate();
    EXPECT_THROW((void)f.service->solidify(
                     f.makeRequest("res-ff"), f.primaryBranch(), materialized),
                 std::invalid_argument);

    // 全部 fail-fast 路径零修订（除（d）前置的成功固化外）。
    EXPECT_EQ(f.revisionDirCount(), revisionsBefore + 1);
}

}  // namespace sdurws::ird::project
