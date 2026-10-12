/**
 * @file   PackageStubs.hpp
 * @brief  PRJ-T18 另存为/项目包测试支撑桩——IPackageCodec 窄接口的
 *         fake 实现（导出/导入两向内存搬运：roundtrip 字节表、取消
 *         检查点、预算超限、损坏包/路径穿越样例诊断）、IFileOps 故障
 *         注入 fake（复制/发布失败路径驱动）与共享夹具（健康源项目
 *         组装：修订＋结果运行＋草稿＋报告树）。
 *
 * 设计依据：
 *   - units/project.md §5.8/§8.8～§8.10（服务与协议消费面——fake 的
 *     行为契约来源）、§11（用例注明需求/AT；PRJ-TX-11 复制一致性）；
 *   - units/io.md §7/§9.12（导出六步/导入九步的只读语义参考与 io 域
 *     诊断 token——fake 行为的对齐基准）；**本头严禁 include io 单元
 *     的任何头**（零 io 编译依赖——ARCH §3.5 O-09 裁决口径，契约测试
 *     持续钉住）；
 *   - 任务契约 tasks/foundation/PRJ-T18.json acceptance 1～3（fake 窄
 *     接口承载：导出 roundtrip、损坏包、取消、预算超限、路径穿越）。
 *
 * 测试域语义约定（SolidifyStubs 先例同款）：诊断码 IO-PACK-HASH-
 *   MISMATCH/IO-SEC-PATH-ESCAPE/IO-SEC-BUDGET-EXPAND/IO-SEC-BUDGET-TEMP
 *   直书 io.md §9.12 收编 token——fake 模拟 io 生产者，码值语义归 io
 *   域（io.md §9.12"建议值——随 IO-T02 注册冻结"），project 只透传不
 *   解释；本头不为其在 project 侧登记码值。
 *
 * 线程安全：fake 由用例串行驱动（服务级互斥保证操作串行）；夹具不
 *   可复制。
 */

#ifndef SDURWS_IRD_PROJECT_TEST_PACKAGESTUBS_HPP
#define SDURWS_IRD_PROJECT_TEST_PACKAGESTUBS_HPP

#include <gtest/gtest.h>

#include <sdurws/ird/core/Events.hpp>
#include <sdurws/ird/project/ArchivePort.hpp>
#include <sdurws/ird/project/DraftService.hpp>
#include <sdurws/ird/project/PackageService.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>
#include <sdurws/ird/testkit/Fixture.hpp>

#include "StubHandlers.hpp"  // stub::StubSink／AppendObjectHandler 复用

#include "SaveAsServiceImpl.hpp"  // 故障注入构造面（src/ 私有头——同单元
                                  // 测试消费既定形态，R-2 先例）

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace sdurws::ird::project::stub {

// =====================================================================
// fake：包编解码（IPackageCodec——io 实现的测试替身）
// =====================================================================

/**
 * @brief fake 包编解码：导出＝从快照视图读出全部条目入内存字节表并
 *        "原子落位"目标文件；导入＝从字节表向临时区写出（roundtrip
 *        通道）。行为注入六面（见各字段注释——取消/预算/失败样例）。
 *
 * 行为契约（对照 io.md §7 的只读语义参考）：
 *   - 导出六步的替身缩编：枚举→逐文件读出（预算/取消检查点每文件）→
 *     内存清单即"包"→完整性自检（读得字节数 vs 清单声明 size）→原子
 *     替换＝目标文件写出（失败/取消时目标零产出——真实 io 的同款保证）；
 *   - 导入九步的替身缩编：包读取（未导出过的路径＝损坏包）→路径
 *     穿越/预算注入→逐条目写出临时区（取消检查点每条目）。
 *
 * 线程约束：由用例串行驱动（服务级互斥保证操作串行）；无内部锁。
 */
class FakePackageCodec final : public IPackageCodec {
public:
    // ---- 行为注入面（用例按需置位；默认全关＝健康通道） ----

    /// 取消检查点：第 N 个文件处理后返回取消（1-based；0＝首个检查点
    /// 即取消；负数＝不注入——导出/导入共用一个计数器口径）。
    int cancelAfterFile = -1;
    /// 累计字节预算（单位＝字节；超限即失败——导出注入
    /// IO-SEC-BUDGET-TEMP、导入注入 IO-SEC-BUDGET-EXPAND，io.md §9.12
    /// 同族码）；默认＝实际无限。
    std::uint64_t budgetBytes = ~static_cast<std::uint64_t>(0);
    /// 强制失败码（非空＝操作开始即产出该 io 域码失败——损坏包样例
    /// 用 "IO-PACK-HASH-MISMATCH"、穿越样例用 "IO-SEC-PATH-ESCAPE"）。
    std::string forcedFailCode;
    /// 导出枚举完成后的钩子（D-17 快照一致性驱动面——用例在钩子内对
    /// 源项目并发提交，验证包内容锚定快照 HEAD 而非提交后状态）。
    std::function<void()> afterEnumerate;

    // ---- 观测面 ----

    /// 最近一次导出的快照清单（enumerate 原样——D-17 断言面）。
    std::vector<PackFileEntry> lastEnumerated;
    /// 导出处理的文件数（进度对照）。
    std::uint64_t exportFileCount = 0;
    /// 导入处理的条目数。
    std::uint64_t importFileCount = 0;

    [[nodiscard]] PackageCodecOutcome exportPack(ISnapshotFileSource& view,
                                                 const std::filesystem::path& targetFile,
                                                 const CancelCallback& onCancel,
                                                 const ProgressCallback& onProgress) override
    {
        PackageCodecOutcome out;
        if (!forcedFailCode.empty()) {
            out.diagnostics.push_back(ioPackDiag(forcedFailCode, "导出失败注入"));
            return out;
        }
        // 步①～②（替身缩编）：枚举冻结清单＋逐文件读出。
        const std::vector<PackFileEntry> entries = view.enumerate();
        lastEnumerated = entries;
        if (afterEnumerate) {
            // D-17 驱动点：快照已冻结、逐文件读取未开始（或进行中）——
            // 用例在此对源项目并发提交。
            afterEnumerate();
        }
        std::map<std::string, std::vector<std::uint8_t>> staged;
        std::uint64_t total = 0;
        int index = 0;
        for (const PackFileEntry& entry : entries) {
            ++index;
            // 取消检查点（每文件——§7.2"取消检查点每文件"）：取消即
            // 中止，目标零产出（无"可被误识别的正式包"）。
            if (cancelAfterFile >= 0 && index > cancelAfterFile) {
                if (onCancel && onCancel()) {
                    out.canceled = true;
                    return out;
                }
            }
            std::vector<std::uint8_t> bytes;
            if (!view.read(entry.packPath, bytes)) {
                out.diagnostics.push_back(ioPackDiag("IO-RES-NOT-FOUND",
                                                     "快照文件不可读: " + entry.packPath));
                return out;
            }
            // 预算（TempAreaBytes 面——实现承担，窄面累计即超限即失败）。
            total += bytes.size();
            if (total > budgetBytes) {
                out.diagnostics.push_back(ioPackDiag(
                    "IO-SEC-BUDGET-TEMP",
                    "导出累计字节超预算 budget=" + std::to_string(budgetBytes)
                        + " actual=" + std::to_string(total)));
                return out;
            }
            // 完整性自检（步⑤替身——读得字节数 vs 清单声明 size；drafts
            // 的替换窗口按 ISnapshotFileSource 稳定版本语义容忍，此处
            // fake 从严比对——健康通道二者恒等）。
            if (bytes.size() != entry.sizeBytes) {
                out.diagnostics.push_back(ioPackDiag(
                    "IO-PACK-HASH-MISMATCH",
                    "导出自检不符: " + entry.packPath + " declared="
                        + std::to_string(entry.sizeBytes)
                        + " actual=" + std::to_string(bytes.size())));
                return out;
            }
            staged.emplace(entry.packPath, std::move(bytes));
            if (onProgress) {
                onProgress(static_cast<std::uint64_t>(index),
                           static_cast<std::uint64_t>(entries.size()));
            }
        }
        // 步⑥（替身）：原子替换＝目标文件写出（真实 io 的失败/取消不
        // 破坏先前包保证，由"替换前零目标写"的结构序承载——本 fake 先
        // 全量就绪再落目标，同序）。
        {
            std::ofstream out_file(targetFile, std::ios::binary | std::ios::trunc);
            out_file << "fake-rwpack";
        }
        packStore[std::filesystem::path(targetFile).generic_string()] = std::move(staged);
        exportFileCount = static_cast<std::uint64_t>(entries.size());
        out.ok = true;
        out.fileCount = exportFileCount;
        return out;
    }

    [[nodiscard]] PackageCodecOutcome unpackInto(const std::filesystem::path& packFile,
                                                 const std::filesystem::path& stagingDir,
                                                 const CancelCallback& onCancel,
                                                 const ProgressCallback& onProgress) override
    {
        PackageCodecOutcome out;
        if (!forcedFailCode.empty()) {
            out.diagnostics.push_back(ioPackDiag(forcedFailCode, "导入失败注入"));
            return out;
        }
        // 步③（替身）：包读取——未导出过的路径＝包不可读（损坏包样例
        // 的另一驱动形态）。
        const auto it = packStore.find(std::filesystem::path(packFile).generic_string());
        if (it == packStore.end()) {
            out.diagnostics.push_back(ioPackDiag("IO-FORMAT-PACK-ZIP",
                                                 "包不可读（未导出/损坏——fake 注入）"));
            return out;
        }
        std::uint64_t total = 0;
        for (const auto& [packPath, bytes] : it->second) {
            total += bytes.size();
        }
        if (total > budgetBytes) {
            out.diagnostics.push_back(ioPackDiag(
                "IO-SEC-BUDGET-EXPAND",
                "解包累计超预算 budget=" + std::to_string(budgetBytes)
                    + " actual=" + std::to_string(total)));
            return out;
        }
        // 步④～⑤（替身）：逐条目写出临时区（取消检查点每条目）。
        int index = 0;
        for (const auto& [packPath, bytes] : it->second) {
            ++index;
            if (cancelAfterFile >= 0 && index > cancelAfterFile) {
                if (onCancel && onCancel()) {
                    out.canceled = true;
                    return out;
                }
            }
            static const std::string kPayloadPrefix = "payload/";
            if (packPath.rfind(kPayloadPrefix, 0) != 0) {
                out.diagnostics.push_back(ioPackDiag("IO-FORMAT-PACK-ENTRY",
                                                     "条目缺 payload/ 前缀: " + packPath));
                return out;
            }
            const std::filesystem::path target
                = stagingDir / std::filesystem::path(packPath.substr(kPayloadPrefix.size()));
            std::error_code ec;
            std::filesystem::create_directories(target.parent_path(), ec);
            std::ofstream out_file(target, std::ios::binary | std::ios::trunc);
            out_file.write(reinterpret_cast<const char*>(bytes.data()),
                           static_cast<std::streamsize>(bytes.size()));
            if (onProgress) {
                onProgress(static_cast<std::uint64_t>(index),
                           static_cast<std::uint64_t>(it->second.size()));
            }
        }
        importFileCount = static_cast<std::uint64_t>(it->second.size());
        out.ok = true;
        out.fileCount = importFileCount;
        return out;
    }

private:
    /// 内存"包"仓库（targetFile/packFile 路径→字节表——roundtrip 通道；
    /// 进程内替身，不落真实 ZIP——编解码细节归 io 域实现）。
    std::map<std::string, std::map<std::string, std::vector<std::uint8_t>>> packStore;

    /// io 域诊断装配（码值归 io 生产者语义——测试域直书 io.md §9.12
    /// 收编 token，project 侧透传不解释；SolidifyStubs 先例同款）。
    [[nodiscard]] static core::DiagnosticRecord ioPackDiag(const std::string& code,
                                                          const std::string& cause)
    {
        return core::DiagnosticRecord::make(
            code, std::nullopt, std::nullopt, std::nullopt,
            "io: 项目包编解码失败（fake 注入——语义参考 io.md §7/§9.12）",
            cause,
            "io: 核对包来源与完整性后重试");
    }
};

// =====================================================================
// fake：文件操作通道（IFileOps 故障注入——复制/发布失败路径驱动）
// =====================================================================

/**
 * @brief 脚本化文件操作通道：默认全部转发真实 Win32 实现；按注入面
 *        制造失败（写中途失败＝复制路径的 F2 型注入；发布失败＝目标
 *        占用竞态）。AtomicFile 构造注入先例（D-10 形态）的消费面。
 */
class ScriptedFileOps final : public win32::IFileOps {
public:
    win32::Win32FileOps real;      ///< 真实实现（转发目标——无状态）
    int failWriteChunkAt = -1;     ///< 第 N 次 writeChunk 返回写故障（1-based；-1＝不注入）
    int writeChunkCalls = 0;       ///< writeChunk 调用计数（观测面）
    bool failPublishNew = false;   ///< true＝publishNew 恒失败（目标占用码）

    [[nodiscard]] win32::FileResult openWriteThrough(const std::wstring& path,
                                                     HANDLE* handle) override
    {
        return real.openWriteThrough(path, handle);
    }
    [[nodiscard]] win32::FileResult writeChunk(HANDLE handle, const char* data,
                                               std::size_t length) override
    {
        ++writeChunkCalls;
        if (failWriteChunkAt > 0 && writeChunkCalls == failWriteChunkAt) {
            // 写故障（ERROR_WRITE_FAULT＝29——映射表落 WriteRejected）。
            return win32::FileResult{false, 29};
        }
        return real.writeChunk(handle, data, length);
    }
    [[nodiscard]] win32::FileResult flush(HANDLE handle) override
    {
        return real.flush(handle);
    }
    [[nodiscard]] win32::FileResult closeHandle(HANDLE handle) override
    {
        return real.closeHandle(handle);
    }
    [[nodiscard]] win32::FileResult publishNew(const std::wstring& tempPath,
                                               const std::wstring& targetPath) override
    {
        if (failPublishNew) {
            // 目标占用（ERROR_FILE_EXISTS＝80——发布期竞态的原始码）。
            return win32::FileResult{false, 80};
        }
        return real.publishNew(tempPath, targetPath);
    }
    [[nodiscard]] win32::FileResult replaceExisting(const std::wstring& tempPath,
                                                    const std::wstring& targetPath) override
    {
        return real.replaceExisting(tempPath, targetPath);
    }
    [[nodiscard]] win32::FileResult createDirectories(const std::wstring& path) override
    {
        return real.createDirectories(path);
    }
    [[nodiscard]] win32::FileResult removeTree(const std::wstring& path) override
    {
        return real.removeTree(path);
    }
};

// =====================================================================
// 共享夹具（健康源项目组装：修订＋结果运行＋草稿＋报告树）
// =====================================================================

/**
 * @brief 另存为/项目包用例组共享夹具：健康源项目（createNew——可写
 *        上下文）组装四类内容（r1 提交对象、一个完整运行、一份草稿、
 *        三份报告——引用运行/无引用/引用缺失运行各一）＋fake 编解码＋
 *        参考总线。
 *
 * 报告树的组装口径：reports/ 的写入端口归 reporting（阶段 B 接入，
 * project 只存储）——夹具按 §4.1 reports 行的磁盘形态直接落盘
 * （report.json＋工件文件），这是测试域的数据准备而非生产写路径。
 *
 * 析构序纪律：service/packageService 最先析构（持宿主引用），随后
 * opened（存储上下文析构释放锁句柄）并清理临时目录。
 */
class PackageFixture {
public:
    std::filesystem::path dir;         ///< 源项目目录（析构清理）
    StubSink sink;                     ///< 捕获型诊断 sink（开发诊断断言面）
    core::ReferenceEventBus bus;       ///< 参考总线（新上下文打开注入）
    RecordingEventSink events;         ///< 事件收集（提交事件计数面）
    std::unique_ptr<core::IEventSubscription> subscription;
    OpenStoreResult opened;            ///< 可写源上下文
    FakePackageCodec codec;            ///< fake 包编解码（注入面）
    std::unique_ptr<ISaveAsService> saveAsService;    ///< 被测服务（最后声明＝最先析构）
    std::unique_ptr<IPackageService> packageService;  ///< 被测服务（同上）

    PackageFixture(const std::string& name)
    {
        static std::atomic<unsigned long long> seq{0};
        dir = std::filesystem::temp_directory_path()
            / (name + "-" + std::to_string(seq.fetch_add(1)) + "-"
               + std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()));
        opened = ProjectStoreFactory::createNew(dir, name + " project", &bus, &sink);
        subscription = bus.subscribe(events);
        // 内容组装收敛到普通成员函数（构造函数体内不可 return 携值表达
        // ——gtest FAIL 展开的 return 语句在构造函数触发 C2534；成员
        // 函数内 ADD_FAILURE＋提前 return 即可安全中止组装）。
        buildHealthyContent();

        // 服务装配（被测面——工厂形态；故障注入用例经实现类型另建）。
        saveAsService = createSaveAsService(*opened.store, &bus, &sink);
        packageService = createPackageService(opened.store.get(), &codec, &bus, &sink);
    }

    ~PackageFixture()
    {
        packageService.reset();
        saveAsService.reset();
        subscription.reset();
        opened.store.reset();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    PackageFixture(const PackageFixture&) = delete;
    PackageFixture& operator=(const PackageFixture&) = delete;

    /// 源存储上下文句柄。
    [[nodiscard]] ProjectStore& store() const { return *opened.store; }

    /// 主分支身份（信封/草稿构造用）。
    [[nodiscard]] core::BranchId primaryBranch() const
    {
        return opened.store->query().currentMetadata().record.primaryBranchId;
    }

    /// 追加对象信封（D-17 并发提交钩子用——r2 提交）。
    [[nodiscard]] CommandEnvelope appendEnvelope(const std::string& payload) const
    {
        CommandEnvelope envelope;
        envelope.branch = primaryBranch();
        envelope.commandType = "test-append-object";
        envelope.payloadFormatVersion = 1;
        envelope.payloadCanonical.assign(payload.begin(), payload.end());
        return envelope;
    }

    /// 本次组装的运行身份（报告引用与五元组断言用）。
    core::RunId runId{};

private:
    /// 健康内容组装（r1 提交＋草稿＋完整运行＋报告树三份；任一前置
    /// 失败＝ADD_FAILURE＋提前返回——组装中止，后续用例断言会暴露）。
    void buildHealthyContent()
    {
        // 处理器注册（§6.5 装配协议——提交对象用）。
        opened.store->handlerRegistry().registerHandler(
            std::make_unique<AppendObjectHandler>());

        // r1：提交一个对象（健康历史——闭包行走/引用走查的素材）。
        CommandEnvelope envelope;
        envelope.branch
            = opened.store->query().currentMetadata().record.primaryBranchId;
        envelope.commandType = "test-append-object";
        envelope.payloadFormatVersion = 1;
        envelope.payloadCanonical = {'r', '1'};
        const CommandResult committed = opened.store->commands().submit(envelope);
        if (!committed.status.committed()) {
            ADD_FAILURE() << "夹具前置提交失败（预期 Committed）";
            return;
        }

        // 一份草稿（drafts 勾选素材——归属三元组随当前项目）。
        DraftDocument doc;
        doc.projectId = opened.store->projectId();
        doc.branchId = opened.store->query().currentMetadata().record.primaryBranchId;
        doc.moduleId = "modeling";
        doc.baseRevisionId = opened.store->query().head().id;
        doc.payload = "{\"draft\":1}";
        doc.savedAtUtc = "2026-10-12T00:00:00Z";
        doc.origin = DraftOrigin::Manual;
        const SaveResult saved = opened.store->drafts().save(doc);
        if (!saved.ok) {
            ADD_FAILURE() << "夹具前置草稿保存失败";
            return;
        }

        // 一个完整运行（results 勾选素材——D-13 manifest 发布＝完整）。
        ArchiveRequest request;
        request.task.project = opened.store->projectId();
        request.task.branch
            = opened.store->query().currentMetadata().record.primaryBranchId;
        request.task.revision = opened.store->query().head().id;
        request.task.run = core::RunId::generate();
        request.task.attempt = core::AttemptId{1};
        request.runDir = dir / "results" / request.task.run.toCanonical();
        request.runKind = "kinematics-eval";
        request.evaluationKey = "eval-key-1";
        auto session = opened.store->archive().begin(request);
        ArchiveBatch batch;
        ArchiveItem item;
        item.relPath = "envelope.json";
        item.bytes = {'{', '}', '\n'};
        batch.items.push_back(item);
        const ArchiveStatus written = opened.store->archive().writeBatch(session, batch);
        if (!written.ok) {
            ADD_FAILURE() << "夹具前置归档批次失败";
            return;
        }
        RunManifest manifest;
        manifest.taskIdentity = request.task;
        manifest.runKind = request.runKind;
        manifest.evaluationKey = request.evaluationKey;
        manifest.finalizedAtUtc = "2026-10-12T00:00:01Z";
        manifest.items.push_back(RunManifestItem{"envelope.json",
                                                 runItemHash(batch.items[0].bytes),
                                                 batch.items[0].bytes.size()});
        // manifestDigest 由归档端口 finalize 计算回填（调用方不预填——
        // ArchivePort finalize 契约）。
        const ArchiveStatus finalized
            = opened.store->archive().finalize(session, manifest);
        if (!finalized.ok) {
            ADD_FAILURE() << "夹具前置归档发布失败";
            return;
        }
        runId = request.task.run;

        // 报告树三份（§8.9 素材）：rep-with-run 引用本运行（勾选
        // results 时随行复制）；rep-no-ref 无引用（恒复制）；
        // rep-missing-ref 引用不存在的运行（恒跳过＋诊断列出）。
        writeReport("rep-with-run",
                    "{\"runs\":[\"" + request.task.run.toCanonical() + "\"]}");
        writeReport("rep-no-ref", "{\"note\":\"no refs\"}");
        writeReport("rep-missing-ref",
                    std::string("{\"runs\":[\"run-") + std::string(32, 'f') + "\"]}");
    }

    /// 直接落盘一份报告目录（§4.1 reports 行磁盘形态——测试数据准备）。
    void writeReport(const std::string& reportId, const std::string& reportJson)
    {
        const std::filesystem::path reportDir = dir / "reports" / reportId;
        std::error_code ec;
        std::filesystem::create_directories(reportDir, ec);
        std::ofstream out(reportDir / "report.json", std::ios::binary | std::ios::trunc);
        out << reportJson;
        std::ofstream artifact(reportDir / "artifact.txt", std::ios::binary | std::ios::trunc);
        artifact << "artifact-of-" << reportId;
    }

    /// 批次字节的 SHA-256 hex（RunManifestItem.sha256——与归档端口同
    /// 算法路径：core ContentDigester）。
    [[nodiscard]] static std::string runItemHash(const std::vector<std::uint8_t>& bytes)
    {
        core::ContentDigester digester;
        digester.update(bytes.data(), bytes.size());
        const core::Digest256 digest = digester.finalize();
        static constexpr char kDigits[] = "0123456789abcdef";
        std::string hex;
        for (const std::uint8_t b : digest) {
            hex.push_back(kDigits[b >> 4]);
            hex.push_back(kDigits[b & 0x0F]);
        }
        return hex;
    }
};

}  // namespace sdurws::ird::project::stub

#endif  // SDURWS_IRD_PROJECT_TEST_PACKAGESTUBS_HPP
