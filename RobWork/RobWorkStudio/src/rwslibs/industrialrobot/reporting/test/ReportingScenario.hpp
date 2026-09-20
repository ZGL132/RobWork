/**
 * @file   ReportingScenario.hpp
 * @brief  RP-\* 契约套件（ReportingContractSuiteTest.cpp 专用）的场景组装
 *         设施——查询端口替身/io 最小 canonical 注入缝/报告工厂/渲染与
 *         文件观测辅助（RPT-T11 落位；仅被契约套件单一翻译单元包含，
 *         不跨 TU 复用——无 ODR 面）。
 *
 * 设计依据：
 *   - units/reporting.md §10.1（用例矩阵——本头是 32 组 RP-\* 用例的
 *     "前置/操作"列承载）、§7.4/§9.5/§9.6（导出/归档契约的注入缝消费）、
 *     §10 替身边界声明（本头组装的一切替身输出仅验证 reporting 契约）
 *   - units/testkit.md §2.4（T-1 允许形态——TempDir/DeterministicEnv 经
 *     测试目标消费）、§6.2/§6.3（临时目录资源隔离/确定性环境）
 *   - 任务契约 tasks/foundation/RPT-T11.json acceptance 1/4/5
 *
 * 边界声明（与四具名替身共用——test/README.md §3 全文）：本头的 io
 * canonical writer/reader 为"io 行为的最小确定性镜像"（RFC4180 引号化＋
 * CRLF＋方言行＋§5.3 单引号前缀守卫——io.md 转义唯一实现点的契约测试
 * IO-V01/V02 同源规则），真实 io 行为归 io 验证矩阵（SA-12——本套件不
 * 第二实现编码器，仅为 reporting 注入缝提供最小驱动）。
 *
 * 线程约束：单线程（gtest 串行；场景对象随用例构造析构）。
 */

#ifndef SDURWS_IRD_REPORTING_TEST_REPORTINGSCENARIO_HPP
#define SDURWS_IRD_REPORTING_TEST_REPORTINGSCENARIO_HPP

#include <gtest/gtest.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Redaction.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/evidence/Envelope.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/reporting/Archive.hpp>
#include <sdurws/ird/reporting/Bundle.hpp>
#include <sdurws/ird/reporting/Builder.hpp>
#include <sdurws/ird/reporting/Consistency.hpp>
#include <sdurws/ird/reporting/Errors.hpp>
#include <sdurws/ird/reporting/Export.hpp>
#include <sdurws/ird/reporting/Identity.hpp>
#include <sdurws/ird/reporting/Render.hpp>
#include <sdurws/ird/reporting/ReportModel.hpp>
#include <sdurws/ird/reporting/SectionProvider.hpp>
#include <sdurws/ird/reporting/Sections.hpp>
#include <sdurws/ird/testkit/Fixture.hpp>   // testkit::TempDir/DeterministicEnv（T-1 允许形态）

#include "FakeArtifactSink.hpp"
#include "FakeArchiveWriter.hpp"
#include "ScriptedResultSource.hpp"
#include "ScriptedSectionProvider.hpp"

namespace sdurws::ird::reporting::rp_scenario {

namespace fs = std::filesystem;
using test_fakes::FakeArchiveWriter;
using test_fakes::FakeArtifactSink;
using test_fakes::ScriptedResultSource;
using test_fakes::ScriptedSectionProvider;

// =====================================================================
// 固定身份（确定性——禁随机；与具名替身脚本值同一铺位纪律）
// =====================================================================

inline core::ProjectId fixedProject() { return test_fakes::scriptedId<core::ProjectId>(0x11); }
inline core::BranchId fixedBranch() { return test_fakes::scriptedId<core::BranchId>(0x12); }
inline core::RevisionId fixedRevision() { return test_fakes::scriptedId<core::RevisionId>(0x13); }
inline core::RunId runAt(std::uint8_t seed) { return test_fakes::scriptedId<core::RunId>(seed); }
inline core::ObjectId caseAt(std::uint8_t seed) { return test_fakes::scriptedId<core::ObjectId>(seed); }

/// 固定时刻（rp 场景时间基准——UNIX 秒；禁系统时钟，确定性前提）。
inline std::chrono::system_clock::time_point fixedTime(std::int64_t unixSeconds)
{
    return std::chrono::system_clock::time_point(std::chrono::seconds(unixSeconds));
}

// =====================================================================
// 替身一：project 查询端口（记录全部调用——零项目写/锚定计数的事实面；
// 与 test_fakes::ScriptedResultSource 同为具名替身的协作投影，非四具名
// 清单成员——FakeQueryPort 保持套件自持）
// =====================================================================

class FakeQueryPort final : public project::IProjectQueryPort {
public:
    bool revisionExists = true;
    std::vector<project::RunInfo> runs{};
    project::RevisionView view{};

    FakeQueryPort()
    {
        view.id = fixedRevision();
        view.seq = 7;
        view.branch = fixedBranch();
        view.commandSummary = "RP 套件锚定修订";
        view.hasUnresolvedPayload = false;
    }

    mutable std::vector<std::string> callLog;
    mutable int headCalls = 0;
    mutable int tryRevisionCalls = 0;
    mutable int listRunsCalls = 0;

    project::RevisionView head() const override
    {
        record("head");
        return view;
    }

    std::optional<project::RevisionView> tryRevision(core::RevisionId id) const override
    {
        record("tryRevision");
        if (!revisionExists || !(id == view.id)) {
            return std::nullopt;
        }
        return view;
    }

    std::vector<project::RunInfo> listRuns(core::RevisionId) const override
    {
        record("listRuns");
        return runs;
    }

    // —— 构建器绝不可调用的方法：调用即留痕判红（零项目写/零越权读反向
    //    断言面；noexcept 的 tryObject 以留痕＋空返回表达）。 ——
    project::RevisionView revision(core::RevisionId) const override { unexpected("revision"); }
    project::ProjectMetadataView currentMetadata() const override { unexpected("currentMetadata"); }
    std::optional<project::ProjectMetadataView> metadataAt(core::RevisionId) const override
    {
        unexpected("metadataAt");
    }
    std::vector<project::BranchTip> branchTips() const override { unexpected("branchTips"); }
    std::vector<project::RevisionView> branchHistory(core::BranchId, std::uint32_t) const override
    {
        unexpected("branchHistory");
    }
    std::optional<std::vector<std::uint8_t>> tryObject(core::ObjectId,
                                                       core::ContentVersion) const noexcept override
    {
        record("tryObject");
        return std::nullopt;
    }
    std::vector<std::uint8_t> object(core::ObjectId, core::ContentVersion) const override
    {
        unexpected("object");
    }
    std::vector<project::DraftInfo> listDrafts(core::BranchId) const override
    {
        unexpected("listDrafts");
    }
    std::filesystem::path runDir(core::RunId) const override { unexpected("runDir"); }

private:
    void record(const char* method) const
    {
        callLog.emplace_back(method);
        if (std::string(method) == "head") {
            ++headCalls;
        } else if (std::string(method) == "tryRevision") {
            ++tryRevisionCalls;
        } else if (std::string(method) == "listRuns") {
            ++listRunsCalls;
        }
    }
    [[noreturn]] static void unexpected(const char* method)
    {
        throw std::logic_error(std::string{"FakeQueryPort: 构建器调用了意外方法 "} + method);
    }
};

/// finalize 运行信息（listRuns 口径的清单行）。
inline project::RunInfo runInfoOf(const core::RunId& run, const std::string& evalKey)
{
    project::RunInfo info;
    info.runId = run;
    info.task = test_fakes::scriptedTask(run);
    info.runKind = "evaluation";
    info.evaluationKey = evalKey;
    info.finalizedAtUtc = "2026-09-20T00:00:00Z";
    return info;
}

// =====================================================================
// 取消令牌替身（手工置位/条件置位——检查点行为断言）
// =====================================================================

class ManualCancelToken final : public ReportCancelToken {
public:
    bool cancelled = false;
    bool isCancelled() const override { return cancelled; }
};

class AlwaysCancelToken final : public ReportCancelToken {
public:
    bool isCancelled() const override { return true; }
};

// =====================================================================
// io 最小 canonical 注入缝（CSV 转义/引号化＋JSON canonical＋原子文件目标
// ——P-RPT-1 注入形态；真实行为归 io 验证矩阵，本处为最小确定性镜像）
// =====================================================================

/// 单元格文本→文件层文本（io §5.3 前缀守卫＋RFC4180 引号化的镜像）。
inline std::string csvEscapeText(const std::string& text)
{
    std::string guarded = text;
    static const std::string kGuardChars = "=+-@'";
    if (!guarded.empty()
        && kGuardChars.find(guarded.front()) != std::string::npos) {
        guarded.insert(guarded.begin(), '\'');   // 恰好一个 ' 前缀（io §5.3）
    }
    const bool needQuote = guarded.find_first_of(",\"\r\n") != std::string::npos;
    if (!needQuote) {
        return guarded;
    }
    std::string out = "\"";
    for (const char c : guarded) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out += c;
        }
    }
    out += "\"";
    return out;
}

/// 文件层字段→单元格原文（自反——unescapeCsvText(escapeCsvText(s))==s）。
inline std::string csvUnescapeField(std::string field)
{
    // 引号剥离（成对包围形态）＋双引号还原。
    if (field.size() >= 2 && field.front() == '"' && field.back() == '"') {
        field = field.substr(1, field.size() - 2);
        std::string out;
        for (std::size_t i = 0; i < field.size(); ++i) {
            if (field[i] == '"' && i + 1 < field.size() && field[i + 1] == '"') {
                out += '"';
                ++i;
            } else {
                out += field[i];
            }
        }
        field = std::move(out);
    }
    // 前缀守卫剥离（恰一个 '——io §5.3 自反性）。
    if (field.size() >= 1 && field.front() == '\'') {
        field.erase(field.begin());
    }
    return field;
}

/// 数值 to_chars 最短往返（io canonical 同口径）。
inline std::string toCharsShortest(double v)
{
    char buf[64];
    const auto res = std::to_chars(buf, buf + sizeof(buf), v);
    return std::string(buf, res.ptr);
}

inline std::string csvCellText(const ReportCsvCell& cell)
{
    switch (cell.kind) {
    case ReportCsvCell::Kind::Empty: return {};
    case ReportCsvCell::Kind::Text: return csvEscapeText(cell.text);
    case ReportCsvCell::Kind::Integer: return std::to_string(cell.integer);
    case ReportCsvCell::Kind::Number: return toCharsShortest(cell.number);
    }
    return {};
}

/// canonical CSV writer（方言行＋CRLF 行尾——§8.3 镜像；转义形态不入结构层
/// 的对偶：读取侧逐字段还原）。
class CanonicalCsvWriter final : public IReportCsvWriter {
public:
    bool open(IReportOutputTarget&& target, bool emitDialectMarker) override
    {
        m_target = &target;
        if (emitDialectMarker) {
            static const std::string kMarker
                = "#rwcsv1 delimiter=, quote=\" eol=CRLF encoding=utf-8\r\n";
            return m_target->write(reinterpret_cast<const std::uint8_t*>(kMarker.data()),
                                   kMarker.size());
        }
        return true;
    }
    bool writeHeader(const std::vector<std::string>& columns) override
    {
        return writeRowTexts(columns);
    }
    bool writeRow(const std::vector<ReportCsvCell>& cells) override
    {
        std::vector<std::string> texts;
        texts.reserve(cells.size());
        for (const ReportCsvCell& cell : cells) {
            texts.push_back(csvCellText(cell));
        }
        return writeRowTexts(texts);
    }
    bool finish() override { return true; }

private:
    bool writeRowTexts(const std::vector<std::string>& texts)
    {
        if (m_target == nullptr) {
            return false;
        }
        std::string line;
        for (std::size_t i = 0; i < texts.size(); ++i) {
            if (i > 0) {
                line += ',';
            }
            line += texts[i];
        }
        line += "\r\n";
        return m_target->write(reinterpret_cast<const std::uint8_t*>(line.data()), line.size());
    }
    IReportOutputTarget* m_target = nullptr;
};

/// canonical JSON writer（2 空格缩进镜像——与 RenderTest 套件同款最小形）。
class CanonicalJsonWriter final : public IReportJsonWriter {
public:
    bool write(IReportOutputTarget&& target, const ReportJsonDom& dom) override
    {
        const std::string text = encode(dom, 0);
        IReportOutputTarget& t = target;
        return t.write(reinterpret_cast<const std::uint8_t*>(text.data()), text.size())
               && t.commit();
    }

private:
    static std::string escape(const std::string& s)
    {
        std::string out;
        for (const char c : s) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out += c;
                }
            }
        }
        return out;
    }
    static std::string encode(const ReportJsonDom& dom, int depth)
    {
        const std::string pad(static_cast<std::size_t>(depth) * 2, ' ');
        switch (dom.type()) {
        case ReportJsonDom::Type::Null: return "null";
        case ReportJsonDom::Type::Bool: return dom.booleanValue() ? "true" : "false";
        case ReportJsonDom::Type::Number: return toCharsShortest(dom.numberValue());
        case ReportJsonDom::Type::String:
            return "\"" + escape(dom.stringValue()) + "\"";
        case ReportJsonDom::Type::Array: {
            if (dom.arrayItems().empty()) { return "[]"; }
            std::string out = "[\n";
            for (std::size_t i = 0; i < dom.arrayItems().size(); ++i) {
                out += pad + "  " + encode(dom.arrayItems()[i], depth + 1);
                if (i + 1 < dom.arrayItems().size()) { out += ","; }
                out += "\n";
            }
            return out + pad + "]";
        }
        case ReportJsonDom::Type::Object: {
            if (dom.objectMembers().empty()) { return "{}"; }
            std::string out = "{\n";
            for (std::size_t i = 0; i < dom.objectMembers().size(); ++i) {
                out += pad + "  " + "\"" + escape(dom.objectMembers()[i].first) + "\": "
                       + encode(dom.objectMembers()[i].second, depth + 1);
                if (i + 1 < dom.objectMembers().size()) { out += ","; }
                out += "\n";
            }
            return out + pad + "}";
        }
        }
        return "null";
    }
};

/// CSV 表（读取侧结构——与 Consistency.hpp ReportCsvTable 同形）。
using ParsedTable = std::vector<std::vector<std::string>>;

/// canonical CSV reader（转义还原——§8.3 回读面的最小镜像；CSV 工件为
/// 主表/诊断附表/覆盖附表三份文档顺序拼接〔Render.hpp 类注〕——读取侧按
/// 文档边界切分多表，检查器按冻结列序取用对应表；解析为全文流式——
/// 引号字段内的换行/逗号不切分，RFC4180 语义）。
class CanonicalCsvReader final : public IReportCsvReader {
public:
    bool readTables(const std::uint8_t* bytes, std::size_t size,
                    std::vector<ReportCsvTable>& out) override
    {
        if (bytes == nullptr && size != 0) {
            return false;
        }
        std::string text(bytes == nullptr ? "" : reinterpret_cast<const char*>(bytes), size);
        ParsedTable current;
        bool inTable = false;
        for (const std::string& line : splitLines(text)) {
            if (line.rfind("#rwcsv1", 0) == 0) {
                continue;   // 方言标识行（§8.3——不入表结构）
            }
            if (line.empty()) {
                // 文档间分隔（渲染器以 LF 空行分隔三份文档——Render.cpp
                // appendSeparator）：收当前表。
                flushTable(current, inTable, out);
                continue;
            }
            if (isHeaderLine(line)) {
                // 新文档头行（field_key/code/case_id 冻结首列——三份列序
                // 表的第一列字面）。
                flushTable(current, inTable, out);
                current.push_back(parseRfc4180Row(line));
                inTable = true;
                continue;
            }
            if (!inTable) {
                continue;   // 头行前的杂散行（不可达——防御）
            }
            current.push_back(parseRfc4180Row(line));
        }
        flushTable(current, inTable, out);
        return !out.empty();
    }

private:
    /// 收表（表头在位的当前表入 out——首行为表头，其余为数据行）。
    static void flushTable(ParsedTable& current, bool& inTable,
                           std::vector<ReportCsvTable>& out)
    {
        if (inTable && !current.empty()) {
            ReportCsvTable table;
            table.header = current.front();
            for (std::size_t i = 1; i < current.size(); ++i) {
                table.rows.push_back(current[i]);
            }
            out.push_back(std::move(table));
        }
        current.clear();
        inTable = false;
    }

    /// 文档头行判定（三份冻结列序的首列字面——field_key/code/case_id）。
    static bool isHeaderLine(const std::string& line)
    {
        return line.rfind("field_key", 0) == 0 || line.rfind("code,", 0) == 0
               || line.rfind("case_id,", 0) == 0;
    }

    /// 全文按行切分（引号感知——引号字段内的换行不切分行；行尾 \r 剥离；
    /// 空行保留为分隔标记）。
    static std::vector<std::string> splitLines(const std::string& text)
    {
        std::vector<std::string> lines;
        std::string line;
        bool inQuotes = false;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (inQuotes) {
                line += c;
                if (c == '"') {
                    // 双引号转义（""——仍处引号内）；单引号＝引号收口。
                    if (i + 1 < text.size() && text[i + 1] == '"') {
                        line += '"';
                        ++i;
                    } else {
                        inQuotes = false;
                    }
                }
                continue;
            }
            if (c == '"' && line.find_last_of(',') == line.size() - 1) {
                // 字段首引号开引（前一位为逗号或行首——引号字段进入）。
                inQuotes = true;
                line += c;
                continue;
            }
            if (c == '\n') {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                lines.push_back(line);
                line.clear();
                continue;
            }
            line += c;
        }
        if (!line.empty()) {
            if (line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(line);
        }
        return lines;
    }

    /// 单行 RFC4180 解析（引号字段/双引号转义——镜像纪律）。
    static std::vector<std::string> parseRfc4180Row(const std::string& line)
    {
        std::vector<std::string> row;
        std::string field;
        bool inQuotes = false;
        bool fieldQuoted = false;
        const auto endField = [&] {
            row.push_back(csvUnescapeField(fieldQuoted ? "\"" + field + "\"" : field));
            field.clear();
            fieldQuoted = false;
        };
        for (std::size_t i = 0; i < line.size(); ++i) {
            const char c = line[i];
            if (inQuotes) {
                if (c == '"') {
                    if (i + 1 < line.size() && line[i + 1] == '"') {
                        field += '"';
                        ++i;
                    } else {
                        inQuotes = false;
                    }
                } else {
                    field += c;
                }
            } else if (c == '"' && field.empty()) {
                inQuotes = true;
                fieldQuoted = true;
            } else if (c == ',') {
                endField();
            } else {
                field += c;
            }
        }
        endField();
        return row;
    }
};

/// 原子文件目标（真实文件——<target>.<serial>.tmp 协议近似；ArchiveExportTest
/// 同款形态，替换原子性为替身近似）。
class FileAtomicTarget final : public IReportOutputTarget {
public:
    FileAtomicTarget(fs::path path, ReplacePolicy policy, int tmpSerial, bool failCommit)
        : path_(std::move(path)), policy_(policy), failCommit_(failCommit),
          tmpSerial_(tmpSerial)
    {
    }

    bool write(const std::uint8_t* bytes, std::size_t n) override
    {
        if (finished_) {
            return false;
        }
        buffer_.insert(buffer_.end(), bytes, bytes + n);
        return true;
    }

    bool commit() override
    {
        if (finished_) {
            return false;
        }
        finished_ = true;
        const fs::path tmp = tmpPath();
        {
            std::ofstream file(tmp, std::ios::binary);
            file.write(reinterpret_cast<const char*>(buffer_.data()),
                       static_cast<std::streamsize>(buffer_.size()));
            if (!file) {
                std::error_code rm;
                fs::remove(tmp, rm);
                return false;
            }
        }
        if (failCommit_) {
            std::error_code rm;
            fs::remove(tmp, rm);
            return false;
        }
        std::error_code ec;
        if (policy_ == ReplacePolicy::NeverOverwrite && fs::exists(path_, ec)) {
            std::error_code rm;
            fs::remove(tmp, rm);
            return false;
        }
        if (fs::exists(path_, ec)) {
            fs::remove(path_, ec);   // 替身近似（Windows rename 不覆盖）
            if (ec) {
                std::error_code rm;
                fs::remove(tmp, rm);
                return false;
            }
        }
        fs::rename(tmp, path_, ec);
        if (ec) {
            std::error_code rm;
            fs::remove(tmp, rm);
            return false;
        }
        return true;
    }

    void abort() override
    {
        finished_ = true;
        buffer_.clear();
        std::error_code ec;
        fs::remove(tmpPath(), ec);
    }

private:
    fs::path tmpPath() const
    {
        char hex[9];
        std::snprintf(hex, sizeof(hex), "%08x", static_cast<unsigned>(tmpSerial_));
        return path_.parent_path() / (path_.filename().string() + "." + hex + ".tmp");
    }

    fs::path path_;
    ReplacePolicy policy_;
    bool failCommit_;
    int tmpSerial_;
    bool finished_ = false;
    std::vector<std::uint8_t> buffer_;
};

/// io 工厂（canonical writer/reader＋文件目标；调用计数供"幂等零写"断言）。
class ScenarioIoFactory final : public IReportIoFactory {
public:
    bool failCommit = false;              ///< commit 失败注入（OverwriteAtomic 失败半区）
    mutable int atomicTargetCreated = 0;  ///< makeAtomicTarget 计数（幂等无操作＝不增）

    std::unique_ptr<IReportCsvWriter> makeCsvWriter() const override
    {
        return std::make_unique<CanonicalCsvWriter>();
    }
    std::unique_ptr<IReportJsonWriter> makeJsonWriter() const override
    {
        return std::make_unique<CanonicalJsonWriter>();
    }
    std::unique_ptr<IReportOutputTarget> makeAtomicTarget(const fs::path& path,
                                                          ReplacePolicy policy) const override
    {
        ++atomicTargetCreated;
        return std::make_unique<FileAtomicTarget>(path, policy, serial_++, failCommit);
    }

private:
    mutable int serial_ = 1;   ///< 临时文件序号（8hex 递增——进程内唯一）
};

/// CSV 回读工厂（canonical reader——一致性检查 CSV 维度载体）。
class ScenarioCsvReaderFactory final : public IReportCsvReaderFactory {
public:
    std::unique_ptr<IReportCsvReader> makeCsvReader() const override
    {
        return std::make_unique<CanonicalCsvReader>();
    }
};

/// 报告值解析替身（IReportResolver 的 map 承载）。
class MapResolver final : public IReportResolver {
public:
    void put(const ReviewReport& report)
    {
        reports_.insert_or_assign(report.reportId().toCanonical(), report);
    }
    std::optional<ReviewReport> tryResolve(ReportId reportId) const override
    {
        auto it = reports_.find(reportId.toCanonical());
        return it == reports_.end() ? std::nullopt : std::optional{it->second};
    }

private:
    std::map<std::string, ReviewReport> reports_;
};

/// 真实脱敏服务（C-3 登记边的真实实现——RP-SAN-1 双保险凭证；RenderTest
/// 同款进程级单例形态）。
inline diagnostics::RedactionService& redactionService()
{
    static diagnostics::RedactionService service;
    return service;
}

// =====================================================================
// 渲染/文件观测辅助
// =====================================================================

/// 内存渲染三格式（返回字节文本；矩阵单次投影内联——§8.4 纪律的用例面）。
inline std::string renderText(const ReviewReport& report, ReportRenderFormat format)
{
    const FieldMatrix matrix = extractFieldMatrix(report);
    RenderOutcome outcome;
    switch (format) {
    case ReportRenderFormat::Html:
        // Html 渲染器注入脱敏（C-3 双保险）；Json/Csv 渲染器注入 io 工厂
        // （canonical 写出载体）＋脱敏（§9.3 注入形态）。
        outcome = HtmlReportRenderer(redactionService()).render(report, matrix, format);
        break;
    case ReportRenderFormat::Json: {
        // 工厂为借用引用——本作用域存活期覆盖 render() 调用。
        ScenarioIoFactory factory;
        outcome = JsonReportRenderer(factory, redactionService()).render(report, matrix, format);
        break;
    }
    case ReportRenderFormat::Csv: {
        ScenarioIoFactory factory;
        outcome = CsvReportRenderer(factory, redactionService()).render(report, matrix, format);
        break;
    }
    }
    if (!outcome.artifact.has_value()) {
        ADD_FAILURE() << "渲染无产物（format=" << std::string(token(format)) << "）error=" << (outcome.error.has_value() ? outcome.error->what() : "无错误对象");
        return {};
    }
    return std::string(outcome.artifact->bytes.begin(), outcome.artifact->bytes.end());
}

/// 限定语 span 剥离后的子串计数（RP-STATE-1"正文无'正式通过'字样"机械
/// 验收面——限定语冻结文案本身含"正式通过"子串，属限定语通道非声明正文；
/// RenderTest 同款口径）。
inline std::size_t countOutsideQualifierSpans(const std::string& html, const std::string& needle)
{
    std::string stripped = html;
    static const std::string kOpen = "<span class=\"qualifier\"";
    static const std::string kClose = "</span>";
    for (auto pos = stripped.find(kOpen); pos != std::string::npos;
         pos = stripped.find(kOpen, pos)) {
        const auto end = stripped.find(kClose, pos);
        if (end == std::string::npos) {
            break;
        }
        stripped.erase(pos, end + kClose.size() - pos);
    }
    std::size_t count = 0;
    for (auto pos = stripped.find(needle); pos != std::string::npos;
         pos = stripped.find(needle, pos + needle.size())) {
        ++count;
    }
    return count;
}

inline bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

/// 文件字节 SHA-256（core 唯一算法——文件哈希比对观测面）。
inline core::Digest256 fileSha256(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        ADD_FAILURE() << "无法读取文件: " << file.string();
        return core::Digest256{};
    }
    core::ContentDigester digester;
    char buf[4096];
    while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
        digester.update(buf, static_cast<std::size_t>(in.gcount()));
        if (in.eof()) {
            break;
        }
    }
    return digester.finalize();
}

/// 文件字节全量读取（可比对/可解析）。
inline std::string readFileText(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        ADD_FAILURE() << "无法读取文件: " << file.string();
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline std::vector<std::uint8_t> asBytes(const std::string& text)
{
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

// =====================================================================
// 场景组装（构建场景/导出场景——32 组 RP 用例的"前置"列承载）
// =====================================================================

/// 逐运行脚本规格（envelope 形态/当前性/评估模式——单点变异面）。
struct RunSpec {
    core::RunId run;
    /// envelope 形态：feasible（缺省）/data-insufficient/infeasible/
    /// canceled/failed/interrupted。
    std::string kind = "feasible";
    /// 当前性：current（缺省）/superseded/unevaluable。
    std::string currentness = "current";
    /// 评估模式覆盖（"quick"＝ScreeningOnly 限定语源；空＝Verified）。
    std::string mode;
    /// DataInsufficient 的缺失项清单（kind=="data-insufficient" 时生效）。
    std::vector<std::string> missingItems;
    /// 覆盖工况集（空＝单工况缺省——RP-COV-1 多工况脚本覆写）。
    std::vector<core::ObjectId> caseScope;
};

/// 逐运行装配（evidence 合法组合-only 的唯一通道——具名替身脚本面）。
inline evidence::ResultEnvelope quickEnvelope(const core::RunId& run,
                                              const std::vector<core::ObjectId>& caseScope);

inline void scriptRun(ScriptedResultSource& source, const RunSpec& spec)
{
    const core::RunId run = spec.run;
    evidence::ResultEnvelope envelope;
    if (spec.kind == "feasible") {
        envelope = test_fakes::feasibleEnvelope(run, test_fakes::ScriptedBindingIds{},
                                                spec.caseScope);
    } else if (spec.kind == "data-insufficient") {
        std::vector<std::string> reasons;
        for (const std::string& item : spec.missingItems) {
            reasons.push_back("缺失项（" + item + "）——正式证据未产出");
        }
        envelope = test_fakes::dataInsufficientEnvelope(run, spec.missingItems, reasons);
    } else if (spec.kind == "infeasible") {
        envelope = test_fakes::infeasibleEnvelope(run);
    } else if (spec.kind == "canceled") {
        envelope = test_fakes::notCompletedEnvelope(run, core::TaskOutcome::Canceled);
    } else if (spec.kind == "failed") {
        envelope = test_fakes::notCompletedEnvelope(run, core::TaskOutcome::Failed);
    } else if (spec.kind == "interrupted") {
        envelope = test_fakes::notCompletedEnvelope(run, core::TaskOutcome::Interrupted);
    } else {
        FAIL() << "未知 RunSpec.kind: " << spec.kind;
        return;
    }
    if (spec.mode == "quick" && spec.kind == "feasible") {
        // Quick 模式（表 1"筛选级"——ScreeningOnly 限定语源；合法组合：
        // make() 仅拒 Preview）。envelope 不可变——以同源草稿重造。
        envelope = quickEnvelope(run, spec.caseScope);
    }
    source.withEnvelope(run, std::move(envelope));

    // 资格脚本与结果形态对齐（§8.2① 渲染器前置断言的合法前置：非 Feasible
    // 判定的结果其正式通过资格如实为不成立，Quick 模式永不满足 FormalPass
    // 〔表 1/断言②〕——真实适配器调 evidence 纯检查的自然结论；替身按
    // 脚本显式登记，不重实现判定逻辑。infeasible 的两声明脚本由用例按需
    // 登记——评审记录资格是其用例面变异点，不在本处缺省注册）。
    if (spec.kind == "data-insufficient" || spec.kind == "canceled"
        || spec.kind == "failed" || spec.kind == "interrupted"
        || (spec.kind == "feasible" && spec.mode == "quick")) {
        ReportEligibilityChecks checks;
        checks.formalPass = test_fakes::scriptedEligibility(false);
        checks.reviewRecord = test_fakes::scriptedEligibility(false);
        source.withEligibility(run, checks);
    }

    if (spec.currentness == "superseded") {
        evidence::InvalidationReason reason;
        reason.dependencyKey = "robot-design";
        reason.kind = evidence::InvalidationKind::ObjectContentChanged;
        reason.detail = "TCP 对象内容变化（r7→r8）";
        source.withCurrentness(run, test_fakes::scriptedCurrentness(
                                        evidence::CurrentnessStatus::Superseded, {reason}));
    } else if (spec.currentness == "unevaluable") {
        source.withCurrentness(run, test_fakes::scriptedCurrentness(std::nullopt));
    }
}

/// Quick 模式包络（ScreeningOnly 限定语源——kind=="feasible" 场景合法）。
inline evidence::ResultEnvelope quickEnvelope(const core::RunId& run,
                                              const std::vector<core::ObjectId>& caseScope)
{
    evidence::ResultEnvelopeDraft d;
    d.task = test_fakes::scriptedTask(run);
    d.evaluationKey = "kin-batch-ik";
    d.evaluatorContractVersion = 7;
    d.mode = core::EvaluationMode::Quick;
    d.snapshotId = test_fakes::scriptedCid(0x10);
    d.sliceId = test_fakes::scriptedCid(0x30);
    d.inputBaselineId = test_fakes::scriptedCid(0x50);
    std::vector<core::ObjectId> cases = caseScope;
    if (cases.empty()) {
        cases = {test_fakes::scriptedId<core::ObjectId>(0x21)};
    }
    d.caseScope.caseIds = std::move(cases);
    d.profile.profileId = "kin";
    d.profile.version = "1.0.0";
    d.profile.contentIdentity = test_fakes::scriptedCid(0x60);
    d.outcome = core::TaskOutcome::Completed;
    d.engineeringStatus = core::EngineeringStatus::Feasible;
    d.evidence = test_fakes::scriptedManifest(test_fakes::ScriptedBindingIds{});
    d.payload = evidence::DomainPayloadDraft{"kin.batch-ik.v1", {0x01}};
    d.producer.productVersion = "industrialrobot-designer 0.1.0";
    return evidence::ResultEnvelope::make(std::move(d));
}

/// 章节内容形态选项（提供方脚本——§5.3 四态任一的单点变异面）。
struct ContentSpec {
    std::string sectionId = std::string(kSectionKinematicsCollision);
    ReportLevel minLevel = ReportLevel::B;
    SectionStatus status = SectionStatus::Populated;
    std::vector<std::string> missingItems;         // NoFormalResult/DataInsufficient 缺项
    std::string notApplicableReason;               // NotApplicable 原因
    core::RunId boundRun{};                        // 条目结果绑定（Populated 时生效）
    bool withTextField = false;                    // 文本字段（RP-CONS-2 特殊字符源）
    std::string textFieldValue;                    // 文本字段值
    bool withCurveEntry = false;                   // 曲线引用条目（RP-CONS-4）
    core::ProvenanceKind provenance = core::ProvenanceKind::UserProvided;
    std::vector<core::ObjectId> entryCaseScope;    // 条目工况（空＝单工况缺省）
};

/// 组装章节内容（Populated 条目绑定与包络清单一致——构建校验通过面；
/// 状态变异（缺项/不适用/数据不足）按 §5.3 presence 纪律装配）。
inline SectionContent sectionContentOf(const ContentSpec& spec)
{
    SectionContent content;
    content.providerContractVersion = 3;
    content.status = spec.status;

    const core::RunId bound = spec.boundRun;
    if (spec.status == SectionStatus::Populated) {
        SectionEntryView entry;
        entry.entryKey = spec.withCurveEntry ? "cycle-curve" : "ik-converged";
        FieldValue field;
        field.key = "kin.solved-count";
        field.quantity = core::SourcedValue<double>::provided(
            1.0, core::ValueProvenance::make(spec.provenance));
        if (spec.withTextField) {
            // 文本字段（与 Provided 数值互斥——§4.3 FieldValue.text）：
            // RP-CONS-2 的特殊字符样例与 RP-CONS-4 的中文名经此进入单元格。
            field.text = spec.textFieldValue;
        }
        entry.fields = {field};
        entry.result.runId = bound;
        entry.result.fieldPath = "payload.task-points[0].converged";
        EvidenceBinding binding;
        binding.itemId = "kin.reach-per-task-point";
        binding.status = evidence::EvidenceItemStatus::Satisfied;
        binding.digest = test_fakes::scriptedCid(0x10).bytes;
        std::vector<core::ObjectId> scope = spec.entryCaseScope;
        if (scope.empty()) {
            scope = {test_fakes::scriptedId<core::ObjectId>(0x21)};
        }
        binding.caseScope = scope;
        entry.evidence = {binding};
        entry.caseScope = scope;
        entry.jump.runId = bound;
        entry.jump.caseId = scope.front();
        content.entries = {entry};
        content.renderHint = spec.withCurveEntry ? RenderHint::CurveRef : RenderHint::Table;
    } else if (spec.status == SectionStatus::NoFormalResult
               || spec.status == SectionStatus::DataInsufficient) {
        std::vector<std::string> items = spec.missingItems;
        if (items.empty()) {
            items = {std::string("kin.required-evidence")};
        }
        for (const std::string& item : items) {
            MissingItemView missing;
            missing.itemId = item;
            missing.reason = spec.status == SectionStatus::NoFormalResult
                                 ? "该域无已完成的评估运行"
                                 : "证据未在正式条件下产出（缺失项全量清单）";
            content.missingItems.push_back(std::move(missing));
        }
    } else {   // NotApplicable
        content.notApplicableReason = spec.notApplicableReason.empty()
                                          ? "本工况集不适用该域评估"
                                          : spec.notApplicableReason;
    }
    return content;
}

/// 注册域提供方（具名替身 ScriptedSectionProvider——B/C 级按词表级别）。
inline void registerContent(SectionRegistry& registry, const ContentSpec& spec)
{
    registry.registerProvider(std::make_unique<ScriptedSectionProvider>(
        spec.sectionId, spec.minLevel, std::vector<std::string>{"kin-batch-ik"},
        sectionContentOf(spec)));
}

/// 构建场景（查询端口＋具名结果源＋注册表——构建器注入三元组的套件形态）。
struct BuildScene {
    FakeQueryPort queryPort;
    ScriptedResultSource resultSource;
    SectionRegistry registry;
    std::vector<RunSpec> runs;

    /// 登记运行脚本＋finalize 清单（listRuns 口径）。
    void addRun(const RunSpec& spec)
    {
        runs.push_back(spec);
        scriptRun(resultSource, spec);
        queryPort.runs.push_back(runInfoOf(spec.run, "kin-batch-ik"));
    }

    /// 注册 kin 章节内容（缺省＝绑定首运行的 Populated）。
    void addKinContent(const ContentSpec& spec)
    {
        ContentSpec s = spec;
        if (s.boundRun == core::RunId{} && !runs.empty()) {
            s.boundRun = runs.front().run;
        }
        registerContent(registry, s);
    }

    ReportBuildRequest request(ReportLevel level) const
    {
        ReportBuildRequest request;
        request.project = fixedProject();
        request.branch = fixedBranch();
        request.revision = fixedRevision();
        request.level = level;
        for (const RunSpec& spec : runs) {
            request.resultRuns.push_back(spec.run);
        }
        return request;
    }

    /// 构建器实例（每次调用独立——ReviewReportBuilder 不可拷贝/移动，经
    /// 堆承载；unique_ptr 临时在调用全表达式内存活，`->build(...)` 直达）。
    std::unique_ptr<ReviewReportBuilder> builder()
    {
        return std::make_unique<ReviewReportBuilder>(queryPort, resultSource, registry);
    }
};

/// 标准单运行场景（B 级＋kin Populated——各用例单点变异的基线）。
inline BuildScene standardScene(const core::RunId& run = runAt(0x14))
{
    BuildScene scene;
    scene.addRun(RunSpec{run});
    scene.addKinContent(ContentSpec{});
    return scene;
}

/// 失败形态断言（report 空＋error 码面——§9.1 后置"失败＝无报告对象"）。
inline void expectBuildFailure(const ReportBuildOutcome& outcome, ReportErrorCode code)
{
    ASSERT_EQ(outcome.report, nullptr) << "失败路径不得产生报告对象（§4.2①）";
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code(), code);
}

/// 取消形态断言（report 空＋error 缺席＋零诊断——UX-03）。
inline void expectCanceled(const ReportBuildOutcome& outcome)
{
    EXPECT_EQ(outcome.report, nullptr);
    EXPECT_FALSE(outcome.error.has_value());
    EXPECT_TRUE(outcome.diagnostics.empty());
}

// =====================================================================
// 导出/归档场景（ReportExportService 五注入的套件形态）
// =====================================================================

/// 导出场景五注入（io 工厂/脱敏/汇集座/清单/回读/解析——借用生命周期随
/// 本对象；TempDir 为 testkit 设施——资源隔离＋失败保留）。
struct ExportScene {
    ScenarioIoFactory ioFactory;
    ScenarioCsvReaderFactory csvReaders;
    FakeArtifactSink sink;      // 具名替身（项目内归档＋只读清单同源）
    MapResolver resolver;

    explicit ExportScene(const ReviewReport& report) { resolver.put(report); }

    /// 导出服务实例（五注入借用随本对象——服务不可拷贝/移动，经堆承载；
    /// unique_ptr 临时在调用全表达式内存活）。
    std::unique_ptr<ReportExportService> service()
    {
        return std::make_unique<ReportExportService>(ioFactory, redactionService(), sink, sink,
                                                     csvReaders, resolver);
    }
};

/// 渲染工件（format 字节重渲染——manifest 装配源；与导出链同源字节）。
inline RenderArtifact artifactOf(const ReviewReport& report, ReportRenderFormat format)
{
    const std::string text = renderText(report, format);
    RenderArtifact a;
    a.format = format;
    a.bytes = asBytes(text);
    core::ContentDigester digester;
    digester.update(a.bytes.data(), a.bytes.size());
    a.digest = digester.finalize();
    a.rendererVersion = kReportRendererVersion;
    a.templateVersion = kHtmlTemplateVersion;
    a.sourceReportIdentity = report.contentIdentity();
    return a;
}

}  // namespace sdurws::ird::reporting::rp_scenario

#endif  // SDURWS_IRD_REPORTING_TEST_REPORTINGSCENARIO_HPP
