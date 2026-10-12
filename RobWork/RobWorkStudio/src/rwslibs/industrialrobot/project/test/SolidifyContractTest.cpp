/**
 * @file   SolidifyContractTest.cpp
 * @brief  外部资源固化契约用例组（跨单元契约面——§3.3"命令契约属跨
 *         单元契约测试面"分工）：重关联＝显式命令提交产生新修订
 *         （AT-21/PM-09）、命令历史可追溯（payload v1 载荷随修订留痕）、
 *         只读打开拒绝固化写（PM-07/§6.1 S1 门卫）。
 *
 * 设计依据：
 *   - units/project.md §5.7（固化/重关联流程）、§6.1（唯一写路径——
 *     重关联经命令端口 S1～S7）、§6.5（"project 即处理器"命令族注册
 *     协议）、§4.4.4（CommandRecord——payload canonical 随修订持久化）、
 *     §3.3（契约测试目标分工）、§9（只读上下文写拒绝）；
 *   - 需求 PM-09/AT-21（重关联经显式提交产生新修订）、PM-07（只读
 *     打开禁编辑——固化写同受门卫）、CON-03（发布面）；
 *   - 任务契约 tasks/foundation/PRJ-T17.json acceptance 1～2（重关联
 *     产生新修订、只读打开拒绝写、零 io 编译依赖的注入缝）。
 *
 * 测试口径登记（DTB §5.4）：
 *   1. 注入缝契约的正面表达＝fake 源（SolidifyStubs.hpp）在**零 io 头
 *      消费**下实现 project 窄接口并驱动服务——本文件与单元组同证；
 *      LinkageContractTest 的"零 io 链接边"扫描持续钉住构建图半区。
 *   2. 命令历史观测点＝磁盘 command.json（parseCommandRecord——地面
 *      事实，ArchiveServiceTest readHeadFromDisk 先例）。
 */

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/project/ResourceRefs.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

#include "Codec.hpp"
#include "SolidifyStubs.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace sdurws::ird::project {

using stub::FakeExternalSource;
using stub::SolidifyFixture;
using stub::StubSink;

namespace {

/// 二进制整读（ArchiveServiceTest readAll 先例——读失败显性失败）。
std::string readAll(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        ADD_FAILURE() << "无法读取文件: " << file.string();
        return {};
    }
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 从磁盘 HEAD 读提交指针（地面事实）。
HeadRecord readHeadFromDisk(const fs::path& projectDir)
{
    return codec::parseHeadRecord(readAll(projectDir / "HEAD"));
}

/// 修订命令留痕读取（revisions/<rev-id>/command.json——PM-12-S1 历史面）。
CommandRecord readCommandFromDisk(const fs::path& projectDir,
                                  const core::RevisionId& revision)
{
    return codec::parseCommandRecord(readAll(
        projectDir / "revisions" / revision.toCanonical() / "command.json"));
}

/// 字节串→小写十六进制（payloadCanonical 的磁盘编码形态校验用——
/// UI-T39 ②口径：域 canonical 负载以 hex 持久化）。
std::string hexOf(const std::string& text)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(text.size() * 2);
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        out.push_back(kDigits[byte >> 4]);
        out.push_back(kDigits[byte & 0x0F]);
    }
    return out;
}

/// 字节序列构造。
std::vector<std::uint8_t> bytesOf(const std::string& text)
{
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

}  // namespace

// =====================================================================
// 重关联＝显式命令提交产生新修订（AT-21/PM-09——acceptance 2）
// =====================================================================

/**
 * 用例：重关联经显式命令提交产生新修订（不改写历史）。
 * 需求/AT：AT-21（外部源重关联经显式提交）、PM-09（存储侧）、CON-03
 *   （重固化副本入对象库）、PA-2（修订只增）。
 * 前置：首次固化已完成；外部源内容已更换（用户重新登记新记录）。
 * 操作：service->relink（同 oid＝替换版本谱系）。
 * 预期：ok=true；修订目录 +1（恰一新修订）；磁盘 command.json 的
 *   commandType＝kRelinkCommandType（历史留痕区分用户动作）；摘要含
 *   记录 id；旧修订视图原样可达（branchHistory 只增）；新副本内容版本
 *   ≠旧版本且都可读（对象库只增）。
 * 观测点：磁盘计数＋command.json＋branchHistory＋tryObject 双版本。
 */
TEST(SolidifyContractTest, Relink_ExplicitCommandProducesNewRevision)
{
    SolidifyFixture f;
    const std::vector<std::uint8_t> v1 = bytesOf("relink-source-v1");
    f.source.entries["res-link"] = {v1, false};
    const ExternalResourceRecord recorded = f.makeRecord("res-link", v1);

    // 首次固化（显式目标 oid——重关联替换版本谱系的锚）。
    const core::ObjectId target = core::ObjectId::generate();
    const SolidifyResult first = f.service->solidify(
        f.makeRequest("res-link", target), f.primaryBranch(), recorded);
    ASSERT_TRUE(first.ok);
    const std::size_t revisionsAfterFirst = f.revisionDirCount();

    // 外部源更换（用户重新选择文件——io 重新登记路径＋哈希）。
    const std::vector<std::uint8_t> v2 = bytesOf("relink-source-v2-new-file");
    f.source.entries["res-link"] = {v2, false};
    const ExternalResourceRecord newRecord = f.makeRecord("res-link", v2);

    // 重关联（显式命令——PM-09/AT-21 的存储侧语义）。
    const SolidifyResult second = f.service->relink(
        f.makeRequest("res-link", target), f.primaryBranch(), newRecord);

    // 恰一新修订＋替换版本（同 oid 新 cv）。
    ASSERT_TRUE(second.ok);
    EXPECT_EQ(f.revisionDirCount(), revisionsAfterFirst + 1);
    EXPECT_TRUE(second.materializedVersion.isValid());
    EXPECT_NE(second.materializedVersion, first.materializedVersion);

    // 历史留痕：磁盘 command.json 的 commandType＝重关联 token（§4.4.4
    // ——命令留痕随修订持久化）；摘要前缀区分用户动作（PM-12-S1 面支配
    // 的事实通道）。
    const HeadRecord head = readHeadFromDisk(f.dir);
    const CommandRecord command = readCommandFromDisk(f.dir, head.revisionId);
    EXPECT_EQ(command.commandType, kRelinkCommandType);
    EXPECT_EQ(command.payloadFormatVersion, 1u);
    EXPECT_NE(command.summary.find("res-link"), std::string::npos)
        << "命令摘要携带记录 id（历史浏览展示面）";

    // 修订只增：既有修订视图原样可达（branchHistory 含新旧两修订，seq
    // 单调——PA-2/AT-21"新修订不改写历史"）。
    const std::vector<RevisionView> history
        = f.opened.store->query().branchHistory(f.primaryBranch(), 10);
    ASSERT_GE(history.size(), 3u);  // r0（初始）＋固化＋重关联
    EXPECT_EQ(history[0].id, head.revisionId);
    EXPECT_GT(history[0].seq, history[1].seq);
    EXPECT_GT(history[1].seq, history[2].seq);

    // 双版本副本都可读（对象库只增——旧引用永不被覆盖）。
    EXPECT_TRUE(f.opened.store->query().tryObject(target, first.materializedVersion)
                    .has_value());
    const std::optional<std::vector<std::uint8_t>> storedV2
        = f.opened.store->query().tryObject(target, second.materializedVersion);
    ASSERT_TRUE(storedV2.has_value());
    EXPECT_EQ(*storedV2, v2) << "重关联副本＝新源原样字节（D-10）";
}

/**
 * 用例：命令历史可追溯——payload v1 载荷随修订留痕（固化事实的追溯面）。
 * 需求/AT：CON-03（固化事实可追溯）、§4.4.4（CommandRecord.payloadCanonical
 *   原样持久化——D-10 透传）、PM-12-S1（历史浏览消费面）。
 * 前置：一次成功固化。
 * 操作：读磁盘 command.json，hex 解码 payloadCanonical 前缀。
 * 预期：载荷以 "IRD-PRJ-SOLIDIFY-PAYLOAD-V1" 魔数开头（实现口径④），
 *   且含记录 id 的 hex 编码（ref= 行——追溯键在历史中可达）。
 */
TEST(SolidifyContractTest, CommandHistory_PayloadV1Traceable)
{
    SolidifyFixture f;
    const std::vector<std::uint8_t> meshBytes = bytesOf("traceable-mesh");
    f.source.entries["res-trace"] = {meshBytes, false};
    const ExternalResourceRecord record = f.makeRecord("res-trace", meshBytes);

    const SolidifyResult result
        = f.service->solidify(f.makeRequest("res-trace"), f.primaryBranch(), record);
    ASSERT_TRUE(result.ok);

    const HeadRecord head = readHeadFromDisk(f.dir);
    const CommandRecord command = readCommandFromDisk(f.dir, head.revisionId);
    EXPECT_EQ(command.commandType, kSolidifyCommandType);
    // payloadCanonical＝域 canonical 负载（磁盘 hex 编码、parseCommandRecord
    // 解码回原样字节——UI-T39 口径）——前缀＝载荷 v1 魔数（原样 ASCII）；
    // ref= 行的自由文本字段为 hex 编码（实现口径④——记录 id 以其 hex
    // 形态可追溯）。CR-02：本断言只比对透传字节，零哈希计算。
    EXPECT_NE(command.payloadCanonical.find("IRD-PRJ-SOLIDIFY-PAYLOAD-V1"),
              std::string::npos)
        << "载荷 v1 魔数随修订留痕";
    EXPECT_NE(command.payloadCanonical.find("ref=" + hexOf("res-trace")),
              std::string::npos)
        << "记录 id（hex 编码）在载荷中可追溯";
}

// =====================================================================
// 只读打开拒绝固化写（PM-07——acceptance 2"只读打开拒绝写"）
// =====================================================================

/**
 * 用例：只读上下文的固化提交被 S1 门卫拒绝（写入口统一防线——固化
 *   不豁免）。
 * 需求/AT：PM-07（只读打开禁编辑——§6.8 写入口权限检查清单含"重
 *   关联"）、§6.1 S1（not-writable 拒绝）、V24 族（只读面语义）。
 * 前置：可写上下文持锁；同项目第二实例只读打开（PM-07 降级路径）＋
 *   只读侧装配服务。
 * 操作：只读侧 service->solidify。
 * 预期：ok=false；只读侧 sink 捕获 PRJ-LOCK-HELD（S1 用户诊断）；错误
 *   码 LockHeldByOther（门卫稳定码，§5.0）；可写侧修订计数不变（只读
 *   上下文无任何磁盘写入）。
 * 观测点：result/error＋sink＋磁盘计数。
 */
TEST(SolidifyContractTest, ReadOnlyStore_SolidifyRejectedWithLockHeld)
{
    SolidifyFixture f;
    const std::vector<std::uint8_t> meshBytes = bytesOf("readonly-mesh");
    f.source.entries["res-ro"] = {meshBytes, false};
    const ExternalResourceRecord record = f.makeRecord("res-ro", meshBytes);
    const std::size_t revisionsBefore = f.revisionDirCount();

    // 同项目第二实例只读打开（PM-07——不阻塞等待，降级只读）＋只读侧
    // 服务装配（注册进只读上下文自己的注册表——上下文级装配面）。
    OpenStoreRequest roRequest;
    roRequest.path = f.dir;
    roRequest.mode = OpenMode::ReadOnly;
    roRequest.eventBus = nullptr;
    StubSink roSink;
    roRequest.diagnostics = &roSink;
    OpenStoreResult readonly = ProjectStoreFactory::open(roRequest);
    ASSERT_NE(readonly.store, nullptr);
    ASSERT_FALSE(readonly.writable) << "写锁被持有——第二实例必为只读（PM-07）";
    FakeExternalSource roSource;
    roSource.entries["res-ro"] = {meshBytes, false};
    const std::unique_ptr<SolidifyService> roService
        = createSolidifyService(*readonly.store, &roSource);

    // 只读侧固化 → 命令端口 S1 拒绝（写入口统一防线——服务不旁路）。
    const SolidifyResult result = roService->solidify(
        f.makeRequest("res-ro"), readonly.store->query()
                                     .currentMetadata()
                                     .record.primaryBranchId,
        record);

    EXPECT_FALSE(result.ok);
    // 用户级诊断 PRJ-LOCK-HELD 经只读侧 sink 产出（S1 门卫通道——注意
    // 冻结签名 SolidifyResult 不携带 StoreError/status 面〔§5.7 四字段
    // 零偏差〕，稳定码观测点在 sink：码值经 diagrec::makeLockHeld 装配）。
    EXPECT_TRUE(roSink.hasUserCode("PRJ-LOCK-HELD"));
    // 零磁盘写入（只读上下文无任何修订产生）。
    EXPECT_EQ(f.revisionDirCount(), revisionsBefore);

    // 只读侧服务先于可写侧上下文析构（scoped——处理器回指针纪律）。
    readonly.store.reset();
}

}  // namespace sdurws::ird::project
