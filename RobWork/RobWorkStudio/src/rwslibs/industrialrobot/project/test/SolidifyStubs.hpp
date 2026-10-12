/**
 * @file   SolidifyStubs.hpp
 * @brief  PRJ-T17 固化服务测试支撑桩——IExternalResourceSource 窄接口的
 *         fake 实现（V17/V24/V26 语义注入：复制期间源变化/预算超限/
 *         缺失引用）与共享夹具。fake 是 io 未来实现的替身（O-09 注入式
 *         协作——io.md §9.8 IResourceSnapshotter 同语义的测试域形态），
 *         其编译**零 io 头消费**——注入缝自证：不依赖 io 单元的任何
 *         公共头即可实现 project 定义的消费窄接口。
 *
 * 设计依据：
 *   - units/project.md §5.7（SolidifyRequest/Result 消费面——fake 的
 *     行为契约来源）、§11（用例注明需求/AT；V17/V24/V26 语义锚点）；
 *   - units/io.md §8.3/§8.4（检测语义与固化失败责任——fake 行为的
 *     只读语义参考：digest 权威判据、复制中途变化→无部分副本、预算
 *     超限→中止；§9.8 probe 并发安全/固化会话级）；**本头严禁 include
 *     io 头**（零 io 编译依赖——ARCH §3.5 O-09 裁决口径，契约测试持续
 *     钉住）；
 *   - 任务契约 tasks/foundation/PRJ-T17.json acceptance 1～2（fake 窄
 *     接口承载：成功固化、源变化 Changed、缺失、预算超限）。
 *
 * 测试域语义约定：诊断码 IO-RES-MISSING/IO-RES-CHANGED/IO-SEC-BUDGET-
 *   EXCEEDED 直书 io.md §8.3/§9.8 收编 token——fake 模拟 io 生产者，
 *   码值语义归 io 域（io.md §9.12"建议值——随 IO-T02 注册冻结"），
 *   project 只透传不解释（§5.7）；本头不为其在 project 侧登记码值。
 *
 * 线程安全：fake 由用例串行驱动（服务级互斥保证 solidify 串行）；
 *   SolidifyFixture 不可复制。
 */

#ifndef SDURWS_IRD_PROJECT_TEST_SOLIDIFYSTUBS_HPP
#define SDURWS_IRD_PROJECT_TEST_SOLIDIFYSTUBS_HPP

#include <gtest/gtest.h>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Events.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/ResourceRefs.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

#include "StubHandlers.hpp"  // stub::StubSink／core::ReferenceEventBus 复用

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sdurws::ird::project::stub {

// =====================================================================
// 摘要辅助（测试域——经 core ContentDigester，与产品同算法路径）
// =====================================================================

/// 字节序列的 SHA-256（core 增量摘要器——测试与产品同一算法事实）。
inline core::Digest256 testDigestOf(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    return digester.finalize();
}

/// 32 字节摘要的 64 小写 hex（无 tag——ExternalRefRecord.contentHash256
/// 的载体形态："与对象文件名 <cv> 同规则"）。
inline std::string testHex64(const core::Digest256& digest)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(digest.size() * 2);
    for (const std::uint8_t b : digest) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

// =====================================================================
// fake：外部资源源（IExternalResourceSource——io 实现的测试替身）
// =====================================================================

/**
 * @brief fake 外部源：按 refId 注册内容条目，行为注入三面（V17/V24/
 *        V26 语义——见各字段注释）。
 *
 * 行为契约（对照 io.md §8.3/§8.4 的只读语义参考）：
 *   - probe：missing 旗标 → Missing；digest 比对（登记哈希 vs 现内容）
 *     → Ok/Changed——不比对 size/mtime（digest 权威判据）；
 *   - readForSolidify：预算不足 → IO-SEC-BUDGET-EXCEEDED（V06/V16）；
 *     缺失 → IO-RES-MISSING（V26）；changedDuringCopy 旗标 → 复制后
 *     快照 B 与 A 不一致＋IO-RES-CHANGED＋**无部分副本**（canonicalBytes
 *     为空——中转清理语义，V17）；正常 → 前后快照一致＋副本字节交付。
 *
 * 线程约束：由用例串行驱动（服务级互斥保证 solidify 串行——公共头
 *   SolidifyService 线程模型）；无内部锁。
 */
class FakeExternalSource final : public IExternalResourceSource {
public:
    /// 按 refId 注册的内容条目（行为注入面）。
    struct Entry {
        std::vector<std::uint8_t> bytes;  ///< 当前源内容（fake 存储态）
        bool missing = false;             ///< true＝路径不可达（V26 注入）
    };

    /// 注册表（refId→条目；用例预置）。
    std::map<std::string, Entry> entries;
    /// true＝复制中途源被改写（V17 注入——全局旗标，用例置位/复位）。
    bool changedDuringCopy = false;
    /// 读取调用计数（观测面——"只读拒绝发生在读取之后/之前"的时序观测）。
    int readCalls = 0;

    [[nodiscard]] ExternalRefProbe probe(const ExternalResourceRecord& record) override
    {
        // 检测语义（io.md §8.3）：缺失→Missing；digest 权威比对→Ok/Changed。
        const auto it = entries.find(record.externalRefId);
        if (it == entries.end() || it->second.missing) {
            return ExternalRefProbe::Missing;
        }
        return testHex64(testDigestOf(it->second.bytes)) == record.contentHash256
            ? ExternalRefProbe::Ok
            : ExternalRefProbe::Changed;
    }

    [[nodiscard]] ExternalSourceReadResult readForSolidify(
        const ExternalResourceRecord& record, std::uint64_t budgetBytes) override
    {
        ++readCalls;
        ExternalSourceReadResult result;
        const auto it = entries.find(record.externalRefId);
        if (it == entries.end() || it->second.missing) {
            // 缺失（V26）：ok=false＋IO-RES-MISSING；无部分副本。
            result.ok = false;
            result.diagnostics.push_back(ioResourceDiag(
                "IO-RES-MISSING", record,
                "外部源不可达（fake V26 注入）path=" + record.absolutePath));
            return result;
        }
        const std::vector<std::uint8_t>& bytes = it->second.bytes;
        if (bytes.size() > budgetBytes) {
            // 预算超限（V06/V16 语义）：ok=false＋预算诊断；中止无半成品。
            result.ok = false;
            result.diagnostics.push_back(ioResourceDiag(
                "IO-SEC-BUDGET-EXCEEDED", record,
                "源字节数超预算 budget=" + std::to_string(budgetBytes)
                    + " actual=" + std::to_string(bytes.size())));
            return result;
        }
        // 快照 A（复制前）。
        result.before.sizeBytes = bytes.size();
        result.before.contentDigest = testDigestOf(bytes);
        if (changedDuringCopy) {
            // 复制中途源变化（V17）：B≠A → ok=false＋IO-RES-CHANGED＋
            // 无部分副本（canonicalBytes 清空——中转清理语义）。
            std::vector<std::uint8_t> mutated = bytes;
            mutated[0] = static_cast<std::uint8_t>(mutated[0] ^ 0xFF);
            result.after.sizeBytes = mutated.size();
            result.after.contentDigest = testDigestOf(mutated);
            result.sourceChangedDuringCopy = true;
            result.ok = false;
            result.diagnostics.push_back(ioResourceDiag(
                "IO-RES-CHANGED", record,
                "复制期间源内容变化（fake V17 注入）recorded="
                    + record.contentHash256));
            return result;
        }
        // 正常：副本交付＋前后快照一致。
        result.canonicalBytes = bytes;
        result.after = result.before;
        result.ok = true;
        return result;
    }

private:
    /// io 域诊断装配（码值归 io 生产者语义——测试域直书 io.md 收编
    /// token，project 侧透传不解释；见文件头测试域语义约定）。
    [[nodiscard]] static core::DiagnosticRecord ioResourceDiag(
        const char* code, const ExternalResourceRecord& record, const std::string& cause)
    {
        return core::DiagnosticRecord::make(
            std::string{code}, std::nullopt, std::nullopt, std::nullopt,
            "io: 外部资源固化读取失败（fake 注入——语义参考 io.md §8.3/§8.4）",
            "ref=" + record.externalRefId + " " + cause,
            "io: 检查外部源可达性与内容后重试，或走重关联流程（PM-09）");
    }
};

// =====================================================================
// 共享夹具（createNew 上下文＋固化服务装配＋fake 注入）
// =====================================================================

/**
 * @brief 固化服务用例组共享夹具：临时项目（createNew——可写上下文）＋
 *        捕获 sink＋参考总线订阅＋fake 源＋服务实例。
 *
 * 析构序纪律（成员声明序的逆序）：service 最先析构（注册表中的处理器
 *   持服务回指针——服务必须先于存储上下文消失，SolidifyServiceImpl 头
 *   注释的装配纪律），随后 source（服务持有的注入指针），最后 opened
 *   （存储上下文析构释放锁句柄）并清理临时目录。
 */
class SolidifyFixture {
public:
    std::filesystem::path dir;   ///< 临时项目目录（析构清理）
    StubSink sink;               ///< 捕获型诊断 sink（稳定码断言面）
    core::ReferenceEventBus bus; ///< 参考总线（提交事件观察）
    RecordingEventSink events;   ///< 事件收集（提交事件计数面）
    std::unique_ptr<core::IEventSubscription> subscription;
    OpenStoreResult opened;      ///< 可写存储上下文
    FakeExternalSource source;   ///< fake 外部源（注入面）
    std::unique_ptr<SolidifyService> service;  ///< 被测服务（最后声明＝最先析构）

    SolidifyFixture()
    {
        // 临时目录（进程内唯一——递增计数＋时钟后缀，ServiceFixture 同款）。
        static std::atomic<unsigned long long> seq{0};
        dir = std::filesystem::temp_directory_path()
            / ("ird-prj-t17-" + std::to_string(seq.fetch_add(1)) + "-"
               + std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()));
        opened = ProjectStoreFactory::createNew(dir, "solidify test project",
                                                &bus, &sink);
        subscription = bus.subscribe(events);
        // 被测服务装配（§6.5 注册协议——固化/重关联命令族注册进注册表）。
        service = createSolidifyService(*opened.store, &source);
    }

    ~SolidifyFixture()
    {
        service.reset();       // 先于存储上下文析构（处理器回指针纪律）
        subscription.reset();
        opened.store.reset();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    SolidifyFixture(const SolidifyFixture&) = delete;
    SolidifyFixture& operator=(const SolidifyFixture&) = delete;

    /// 主分支身份（createNew 初始分支——信封/请求的 branch 来源）。
    [[nodiscard]] core::BranchId primaryBranch() const
    {
        return opened.store->query().currentMetadata().record.primaryBranchId;
    }

    /// 构造 recorded 态记录（哈希/大小由内容实算——与 fake 条目一致的
    /// 登记事实；recordedAtUtc 固定值——测试域确定性）。
    [[nodiscard]] ExternalResourceRecord makeRecord(
        const std::string& refId, const std::vector<std::uint8_t>& bytes) const
    {
        ExternalResourceRecord record;
        record.externalRefId = refId;
        record.absolutePath = "C:/external/mesh/" + refId + ".stl";
        record.contentHash256 = testHex64(testDigestOf(bytes));
        record.sizeBytes = bytes.size();
        record.recordedAtUtc = "2026-10-12T00:00:00.000Z";
        record.state = ExternalResourceState::Recorded;
        return record;
    }

    /// 固化请求（预算默认＝1 MiB——覆盖常规用例的充足预算；目标全零＝
    /// 新分配路径）。
    [[nodiscard]] SolidifyRequest makeRequest(
        const std::string& refId,
        core::ObjectId target = core::ObjectId{},
        std::uint64_t budgetBytes = 1024 * 1024) const
    {
        SolidifyRequest request;
        request.externalRefId = refId;
        request.targetResourceObject = target;
        request.budgetBytes = budgetBytes;
        return request;
    }

    /// 磁盘修订目录计数（"零修订"断言的地面事实——ServiceFixture 同款）。
    [[nodiscard]] std::size_t revisionDirCount() const
    {
        return subdirectoryCount(dir / "revisions");
    }

    /// 磁盘对象库 oid 目录计数（"对象库无新对象"断言的观测点）。
    [[nodiscard]] std::size_t objectDirCount() const
    {
        return subdirectoryCount(dir / "objects");
    }

    /// 在修订引用集中定位 (oid,cv)（含对象类型 token——引用保护面断言）。
    [[nodiscard]] static const ObjectRef* findRef(const RevisionView& revision,
                                                  core::ObjectId oid,
                                                  core::ContentVersion cv)
    {
        for (const ObjectRef& ref : revision.objectRefs) {
            if (ref.objectId == oid && ref.contentVersion == cv) {
                return &ref;
            }
        }
        return nullptr;
    }

    /// 按 oid 定位修订引用集中的引用（oid 未知场景——新分配路径的反查）。
    [[nodiscard]] static const ObjectRef* findRefByObject(const RevisionView& revision,
                                                          core::ObjectId oid)
    {
        for (const ObjectRef& ref : revision.objectRefs) {
            if (ref.objectId == oid) {
                return &ref;
            }
        }
        return nullptr;
    }

private:
    [[nodiscard]] static std::size_t subdirectoryCount(const std::filesystem::path& root)
    {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec) || ec) {
            return 0;
        }
        std::size_t n = 0;
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (ec) {
                break;
            }
            if (entry.is_directory(ec) && !ec) {
                ++n;
            }
        }
        return n;
    }
};

}  // namespace sdurws::ird::project::stub

#endif  // SDURWS_IRD_PROJECT_TEST_SOLIDIFYSTUBS_HPP
