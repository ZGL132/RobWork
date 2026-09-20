/**
 * @file   FakeArtifactSink.hpp
 * @brief  FakeArtifactSink——IReportArtifactSink＋IReportPublishedIndex 的
 *         最小确定性替身（RPT-T09 随任务交付；具名替身与替身边界声明的
 *         收口归 RPT-T11——卡行 T11 产物列原文）。
 *
 * 设计依据：
 *   - units/reporting.md §7.3（报告工件状态图——Staged→Partial→Finalized、
 *     manifest 原子发布＝唯一完整标志 D-13、abandon 未完成工件不入清单）、
 *     §7.4（幂等/冲突判定——manifest 摘要一致 IdempotentHit 零重写、不一致
 *     Conflict 不覆盖）、§9.6（sink 四方法契约——测试替身按契约实现即对
 *     project 侧实现的语义投影）、§10（可控替身清单——FakeArtifactSink：
 *     "可注入冲突/磁盘满/清理失败"）
 *   - 任务契约 tasks/foundation/RPT-T09.json acceptance 2~5（冲突/磁盘/
 *     取消注入；sink 契约与 Fake 随本任务交付）
 *
 * 替身边界声明（RP-STATE-4 同源纪律——acceptance 5"具名替身与替身边界
 *   声明随 RPT-T11 收口"的本任务先行版）：
 *   本替身仅验证 reporting 侧契约（编排顺序/幂等/冲突/取消语义/发布清单
 *   纪律），其内存态"存储"不构成 project 持久化正确性证明（磁盘编址/
 *   原子性/恢复扫描归 project——D-13/D-14 语义在 project 实现矩阵验证）；
 *   替身不伪造任何报告内容（工件字节来自被测链路的真实渲染产物）。
 *
 * 可注入故障（§10 替身清单原文三项）：
 *   - 冲突：seedPublished() 预置不同摘要的已发布记录（finalize 判 Conflict
 *     的自然路径），另有 forceConflictOnFinalize 强制注入口；
 *   - 磁盘满：diskFullAtWrite=n——第 n 次 writeArtifact 返回 DiskFull；
 *   - 清理失败：cleanupFailure=true——abandon 登记残留计数（"清理失败仅
 *     残留诊断"的替身承载：残留可经 cleanupFailedCount 观测，不破坏已发布
 *     报告）。
 *
 * 线程约束：单线程使用（被测协调器会话单线程——非线程安全替身）。
 */

#ifndef SDURWS_IRD_REPORTING_TEST_FAKEARTIFACTSINK_HPP
#define SDURWS_IRD_REPORTING_TEST_FAKEARTIFACTSINK_HPP

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>   // core::ProjectId（Id128 字节面）
#include <sdurws/ird/reporting/Archive.hpp>
#include <sdurws/ird/reporting/Errors.hpp>
#include <sdurws/ird/reporting/Identity.hpp>

namespace sdurws::ird::reporting::test_fakes {

/**
 * @brief 汇集座替身（内存态 reports/ 语义投影——见文件头替身边界声明）。
 *
 * 状态模型：会话（Staged 工件集）→finalize 提交为已发布记录（Finalized：
 * 工件字节＋manifest）；abandon 丢弃会话（未完成工件不入清单——直接不存在
 * 于 published 表，等价"manifest 缺失＝未发布"）。调用日志逐方法记录供
 * 编排顺序断言（acceptance 1"sink 调用记录"）。
 */
class FakeArtifactSink final : public IReportArtifactSink, public IReportPublishedIndex {
public:
    // ---- 观测面：调用日志（方法名＋会话＋关键参数摘要） ----

    /// 单次调用记录（method∈begin/writeArtifact/finalize/abandon）。
    struct CallRecord {
        std::string method;         ///< 方法名（字面——观测用）
        std::uint64_t session = 0;  ///< 会话编号（begin 记录分配结果）
        std::string detail;         ///< 关键参数（relPath/结束原因等）
    };

    std::vector<CallRecord> calls;   ///< 全量调用日志（append-only）

    // ---- 注入面（§10 三项故障＋只读/占用） ----

    bool readOnly = false;            ///< true＝begin 抛 ReadOnlyStore（只读项目）
    bool occupied = false;            ///< true＝begin 抛 ArchiveConflict（目录被占用）
    int diskFullAtWrite = 0;          ///< n>0＝第 n 次 writeArtifact 返回 DiskFull
    bool cleanupFailure = false;      ///< true＝abandon 登记清理失败残留
    bool forceConflictOnFinalize = false;  ///< true＝finalize 恒 Conflict（强制注入口）

    int cleanupFailedCount = 0;       ///< 清理失败残留计数（"仅残留诊断"观测面）

    // ---- 已发布存储（内存态——"磁盘"投影） ----

    /// 已发布报告的存储行（Finalized 事实）。
    struct PublishedRow {
        PublishedReportRecord record;                    ///< 发布记录（内存值）
        ReportArtifactManifest manifest;                 ///< 持久化的 manifest
        std::map<std::string, std::vector<std::uint8_t>> artifacts;  ///< relPath→字节
    };

    /// 键＝projectId 字节 hex + '/' + reportId 规范文本（进程内编址投影）。
    std::map<std::string, PublishedRow> published;

    // ---- 预置/注入辅助（测试夹具用） ----

    /// 预置一条已发布记录（幂等/冲突注入的自然路径——finalize 摘要比对）。
    /// contentIdentity 与 manifestDigest 独立可设：幂等形态＝两摘要均与本次
    /// 发布一致；冲突"身份维度"＝contentIdentity 异；冲突"版本维度"＝
    /// contentIdentity 同而 manifestDigest 异（§7.4 差异定位两形态）。
    void seedPublished(core::ProjectId project, ReportId reportId,
                       core::ContentIdentity contentIdentity,
                       core::ContentIdentity manifestDigest)
    {
        PublishedRow row;
        row.record.reportId = reportId;
        row.record.contentIdentity = contentIdentity;
        row.record.manifestDigest = manifestDigest;
        row.record.archiveState = ReportArchiveState::Finalized;
        row.record.publishedAtUtc = std::chrono::system_clock::now();
        published[key(project, reportId)] = std::move(row);
    }

    /// 复位全部注入（磁盘满重试等"清除故障后再试"场景用——不影响已发布
    /// 存储与调用日志）。
    void resetInjections()
    {
        readOnly = false;
        occupied = false;
        diskFullAtWrite = 0;
        cleanupFailure = false;
        forceConflictOnFinalize = false;
        writeCounter_ = 0;
    }

    // ---- IReportArtifactSink（§9.6 契约语义的替身承载） ----

    ArtifactSessionRef begin(const ReportArtifactBeginRequest& request) override
    {
        // 前置环境检查（§9.6 前置行"存储上下文 Active ∧ writable"——异常轨）。
        if (readOnly) {
            throw ReportError(ReportErrorCode::ReadOnlyStore,
                              "fake: 存储上下文只读（writable=false）");
        }
        if (occupied) {
            throw ReportError(ReportErrorCode::ArchiveConflict,
                              "fake: reportId 目录被其他会话占用");
        }
        const std::uint64_t id = nextSessionId_++;   // 会话编号（非零——保留值纪律）
        calls.push_back({"begin", id, request.reportId.toCanonical()});
        Session session;
        session.reportId = request.reportId;
        session.project = request.project;
        sessions_[id] = std::move(session);   // Staged 态入表（目录已建）
        ArtifactSessionRef ref;
        ref.value = id;
        return ref;
    }

    ArtifactStatus writeArtifact(ArtifactSessionRef session,
                                 const ArtifactWrite& artifact) override
    {
        calls.push_back({"writeArtifact", session.value, artifact.relPath});
        auto it = sessions_.find(session.value);
        if (it == sessions_.end()) {
            return ArtifactStatus::ContextClosed;   // 会话不存在/已终结
        }
        // 磁盘满注入（§10 替身清单；计数按 writeArtifact 调用序）。
        if (diskFullAtWrite > 0 && ++writeCounter_ == diskFullAtWrite) {
            return ArtifactStatus::DiskFull;
        }
        // 只增语义（§9.6 合法调用行）：同 relPath 同字节＝续写跳过
        // （IdempotentHit）；同 relPath 异字节＝只增违约（Conflict——不覆盖）。
        auto existing = it->second.artifacts.find(artifact.relPath);
        if (existing != it->second.artifacts.end()) {
            return existing->second == artifact.bytes ? ArtifactStatus::IdempotentHit
                                                      : ArtifactStatus::Conflict;
        }
        it->second.artifacts[artifact.relPath] = artifact.bytes;
        return ArtifactStatus::Ok;
    }

    ArtifactStatus finalize(ArtifactSessionRef session,
                            const ReportArtifactManifest& manifest) override
    {
        calls.push_back({"finalize", session.value, manifest.reportId.toCanonical()});
        auto it = sessions_.find(session.value);
        if (it == sessions_.end()) {
            return ArtifactStatus::ContextClosed;
        }
        if (forceConflictOnFinalize) {
            return ArtifactStatus::Conflict;   // 强制注入（§10"可注入冲突"）
        }
        const std::string k = key(manifest.project, manifest.reportId);
        auto publishedIt = published.find(k);
        if (publishedIt != published.end()) {
            // 幂等/冲突判定（§9.6 finalize 注释——manifest 摘要比对）。
            return publishedIt->second.record.manifestDigest == manifest.manifestDigest
                       ? ArtifactStatus::IdempotentHit   // 零重写（不触碰已发布字节）
                       : ArtifactStatus::Conflict;       // 不覆盖（RPT-ARCHIVE-CONFLICT）
        }
        // 提交：Staged→Finalized（manifest 原子发布＝唯一完整标志的替身
        // 形态——内存态无原子性可言，原子性归 project 实现矩阵验证）。
        PublishedRow row;
        row.record.reportId = manifest.reportId;
        row.record.contentIdentity = manifest.contentIdentity;
        row.record.archiveState = ReportArchiveState::Finalized;
        row.record.manifestDigest = manifest.manifestDigest;
        row.record.artifactRelPaths.push_back(std::string(kManifestRelPath));
        for (const ArtifactManifestEntry& e : manifest.artifacts) {
            row.record.artifactRelPaths.push_back(e.relPath);
        }
        row.record.publishedAtUtc = manifest.finalizedAtUtc;
        row.manifest = manifest;
        row.artifacts = std::move(it->second.artifacts);
        published[k] = std::move(row);
        sessions_.erase(it);
        return ArtifactStatus::Ok;
    }

    void abandon(ArtifactSessionRef session, ReportEndReason reason) override
    {
        calls.push_back({"abandon", session.value, std::string(token(reason))});
        // 责任终结：会话丢弃（未完成工件不入清单——PM-08："文件存在≠已
        // 发布"的替身形态：Staged 工件随会话消失，published 表不动）。
        sessions_.erase(session.value);
        if (cleanupFailure) {
            // "清理失败仅残留诊断"（§7.4 行 9）：残留以计数观测（诊断通道
            // 归 project 实现——替身只证明"责任终结不抛出、已发布零影响"）。
            ++cleanupFailedCount;
        }
    }

    // ---- IReportPublishedIndex（只读清单——与存储同源） ----

    std::optional<PublishedReportRecord> tryPublished(core::ProjectId project,
                                                      ReportId reportId) const override
    {
        auto it = published.find(key(project, reportId));
        if (it == published.end()) {
            return std::nullopt;
        }
        return it->second.record;
    }

    std::vector<PublishedReportRecord> listPublished(core::ProjectId project) const override
    {
        // 仅 Finalized（published 表只收 Finalized 行——未完成工件天然不在）；
        // reportId 字典序稳定（IReportPublishedIndex 契约）。替身单项目场景
        // 不按项目过滤（真实实现按 project 过滤——编址键含 projectId）。
        std::vector<PublishedReportRecord> out;
        for (const auto& entry : published) {
            out.push_back(entry.second.record);
        }
        std::sort(out.begin(), out.end(),
                  [](const PublishedReportRecord& a, const PublishedReportRecord& b) {
                      return a.reportId < b.reportId;
                  });
        (void)project;
        return out;
    }

    // ---- 会话观测（测试断言辅助） ----

    /// 当前活跃（未终结）会话数——abandon/finalize 成功后应归零。
    std::size_t activeSessions() const { return sessions_.size(); }

private:
    /// 会话内存态（Staged 工件集）。
    struct Session {
        ReportId reportId;
        core::ProjectId project;
        std::map<std::string, std::vector<std::uint8_t>> artifacts;
    };

    static std::string key(core::ProjectId project, ReportId reportId)
    {
        // Id128 字节以 hex 拼接（进程内键，非持久化规范文本——reportId 段
        // 用其规范文本便于日志可读）。
        std::string k;
        char buf[3];
        for (std::uint8_t b : project.bytes) {
            static_cast<void>(std::snprintf(buf, sizeof(buf), "%02x", static_cast<unsigned>(b)));
            k += buf;
        }
        k += '/';
        k += reportId.toCanonical();
        return k;
    }

    std::uint64_t nextSessionId_ = 1;   ///< 会话编号分配（非零起始）
    int writeCounter_ = 0;              ///< writeArtifact 调用计数（磁盘满注入定位）
    std::map<std::uint64_t, Session> sessions_;   ///< 活跃会话表
};

}  // namespace sdurws::ird::reporting::test_fakes

#endif  // SDURWS_IRD_REPORTING_TEST_FAKEARTIFACTSINK_HPP
