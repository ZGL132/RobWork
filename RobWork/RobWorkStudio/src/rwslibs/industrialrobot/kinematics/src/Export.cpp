/**
 * @file   Export.cpp
 * @brief  结果导出值行实现（KIN-08/§7.4）——行构建（解行/诊断行/覆盖
 *         行）与 JSON/CSV 文本编码；来源声明在文件头（契约面见
 *         Export.hpp 文件头注）。
 *
 * 设计依据：units/kinematics.md §7.4/§9.8；REQUIREMENTS KIN-08/AT-04；
 * 任务契约 tasks/foundation/WP-15-T09.json acceptance 2（登记随卡
 * §14.6 v0.9）。
 *
 * 实现要点：
 *   - 浮点文本＝std::to_chars 最短往返表示（表示唯一、可精确回读；
 *     C++17 <charconv>——MSVC 2022 全量支持浮点重载）。非有限值输出
 *     "inf"/"-inf"/"nan" 词形（JSON 中以字符串承载——JSON 数值语法
 *     不含非有限字面量；CSV 中原样承载）。
 *   - 全程零 locale 依赖（to_chars 不受全局 locale 影响；无流格式化）
 *     ——跨进程/跨线程同字节（NFR-COR-01/02）。
 *   - 本翻译单元零 io/零 project 消费（§2.3 不拥有清单）——"导出不
 *     产生修订"（AT-04）是结构保证，非运行时承诺。
 */

#include <sdurws/ird/kinematics/Export.hpp>

#include <sdurws/ird/core/Evaluation.hpp>  // toToken（模式小写 token）

#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace sdurws::ird::kinematics {

namespace {

// =====================================================================
// 本地词表（导出面唯一书写点——小写连字符，core §4.7 token 惯例）
// =====================================================================

/// 来源 token（§7.4 两分的文本形态）。
const char* exportSourceToken(KinExportSource s)
{
    switch (s) {
    case KinExportSource::ArchivedRun:
        return "archived-run";   ///< results/<run-id>/ 归档
    case KinExportSource::SessionMemory:
        break;                   ///< 会话内存（fallthrough 统一出口）
    }
    return "session-memory";
}

/// 过滤原因 token（SolutionFilterReason 三值的一一映射——硬过滤顺序
/// ①残差复验②限位③碰撞，顺序即语义）。
const char* filterReasonToken(SolutionFilterReason r)
{
    switch (r) {
    case SolutionFilterReason::ResidualRecheck:
        return "residual-recheck";
    case SolutionFilterReason::JointLimit:
        return "joint-limit";
    case SolutionFilterReason::Collision:
        break;
    }
    return "collision";
}

/// 样本口径 token（SampleKind 两值——position/pose）。
const char* sampleKindToken(SampleKind k)
{
    switch (k) {
    case SampleKind::Position:
        return "position";
    case SampleKind::Pose:
        break;
    }
    return "pose";
}

// =====================================================================
// 文本装配原语（JSON/CSV 共用）
// =====================================================================

/// 浮点 → 最短往返十进制文本（确定性：表示唯一；非有限值→inf/-inf/nan
/// 词形——调用方决定 JSON 侧是否加引号）。
std::string formatDouble(double v)
{
    char buf[64];  // 最短往返表示最长约 24 字符（double）；64 字节裕量充足
    const auto res = std::to_chars(buf, buf + sizeof(buf), v);
    // to_chars 对 double（含 inf/nan）在 MSVC 上不失败（value_too_large
    // 仅对整数目标过短——缓冲区 64 字节恒满足）；断言面防御即可。
    if (res.ec != std::errc()) {
        throw std::logic_error(
            "Export：浮点文本化失败（缓冲区不足——实现错误，fail-fast）");
    }
    return std::string{buf, res.ptr};
}

/// 浮点 → JSON 数值/字符串片段：有限值输出裸数值；非有限值输出带引号
/// 词形（JSON 数值语法不含 inf/nan——以字符串承载并保留词形可读性）。
std::string jsonNumber(double v)
{
    if (std::isfinite(v)) {
        return formatDouble(v);
    }
    // -nan 的负号无语义（NaN 无序）——统一归一为 "nan" 词形。
    std::string text = formatDouble(v);
    if (text.rfind("-nan", 0) == 0) { text = "nan"; }
    return "\"" + text + "\"";
}

/// JSON 字符串转义（引号/反斜杠/控制字符——\u00XX 小写十六进制；
/// UTF-8 多字节序列原样透传，本导出面的字符串域均为 ASCII 词形）。
std::string jsonString(const std::string& raw)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(raw.size() + 2);
    out += '"';
    for (const char ch : raw) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                // 其余控制字符（RFC 8259 要求转义）——\u00xx 小写词形。
                out += "\\u00";
                out += kHex[(c >> 4) & 0xF];
                out += kHex[c & 0xF];
            } else {
                out += ch;  // 可打印 ASCII／UTF-8 透传
            }
            break;
        }
    }
    out += '"';
    return out;
}

/// JSON 缩进辅助（2 空格 × 层级）。
std::string indent(std::size_t level)
{
    return std::string(level * 2, ' ');
}

/// CSV 字段转义（RFC 4180：含逗号/引号/换行的字段加引号、内部引号加倍；
/// 其余原样）。
std::string csvField(const std::string& raw)
{
    const bool needsQuoting = raw.find_first_of(",\"\r\n") != std::string::npos;
    if (!needsQuoting) {
        return raw;
    }
    std::string out;
    out.reserve(raw.size() + 2);
    out += '"';
    for (const char ch : raw) {
        if (ch == '"') {
            out += "\"\"";  // 内部引号加倍
        } else {
            out += ch;
        }
    }
    out += '"';
    return out;
}

/// CSV 行装配（逐字段转义后逗号拼接）。
std::string csvLine(const std::vector<std::string>& fields)
{
    std::string out;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) {
            out += ',';
        }
        out += csvField(fields[i]);
    }
    out += '\n';
    return out;
}

/// bool → "1"/"0"（CSV 列的布尔词形——紧凑、无歧义）。
const char* boolToken(bool v)
{
    return v ? "1" : "0";
}

/// bool → JSON 布尔字面量（true/false——RFC 8259；与 CSV 的 1/0 词形
/// 分列，各自语法面）。
const char* jsonBool(bool v)
{
    return v ? "true" : "false";
}

/// 可选 ObjectId → canonical 文本（空值→空串——CSV 空字段/JSON null 的
/// 统一来源）。
std::string oidText(const std::optional<core::ObjectId>& oid)
{
    return oid.has_value() ? oid->toCanonical() : std::string{};
}

}  // namespace

// =====================================================================
// 行构建（结果值 → 行结构）
// =====================================================================

std::vector<KinSolutionExportRow> buildSolutionExportRows(const IkSolutionSet& set)
{
    std::vector<KinSolutionExportRow> rows;
    rows.reserve(set.solutions.size());
    // 逐解整理：rank＝输入序下标（生产端不变式——solutions 已按 §6.3
    // 四键稳定排序，导出面不重排不筛选——NFR-MNT-04 的整理面分工）。
    for (std::size_t i = 0; i < set.solutions.size(); ++i) {
        const KinematicSolution& s = set.solutions[i];
        KinSolutionExportRow row;
        row.rank = i;
        row.snapshotId = set.requestIdentity.snapshotId;
        row.pointOid = set.targetRef.pointOid;
        row.conditionId = set.targetRef.conditionId;
        row.q = s.q;
        row.positionResidual = s.positionResidual;
        row.orientationResidual = s.orientationResidual;
        row.minimumJointMargin = s.minimumJointMargin;
        row.manipulability = s.manipulability;
        row.conditionNumber = s.conditionNumber;
        row.collisionEvaluated = s.collisionStatus.evaluated;
        row.collisionInCollision = s.collisionStatus.inCollision;
        row.sourceInitIndex = s.sourceInitIndex;
        row.iterations = s.iterations;
        row.signature = s.signature;
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<KinFilteredSolutionExportRow> buildFilteredSolutionExportRows(
    const IkSolutionSet& set)
{
    std::vector<KinFilteredSolutionExportRow> rows;
    rows.reserve(set.filteredRecords.size());
    // 逐诊断记录整理（保持 filteredRecords 原序——初值序，确定性）；
    // 碰撞对象对成对展平为 canonical 文本列（竖线分隔——R-4：身份
    // canonical 直出，无名称拼接）。
    for (const FilteredSolutionRecord& r : set.filteredRecords) {
        KinFilteredSolutionExportRow row;
        row.snapshotId = set.requestIdentity.snapshotId;
        row.pointOid = set.targetRef.pointOid;
        row.conditionId = set.targetRef.conditionId;
        row.reason = filterReasonToken(r.reason);
        row.q = r.q;
        row.positionResidual = r.positionResidual;
        row.orientationResidual = r.orientationResidual;
        row.minimumJointMargin = r.minimumJointMargin;
        row.manipulability = r.manipulability;
        row.collisionPairCount = r.objectIdPairs.size();
        for (const core::ObjectId& oid : r.objectIdPairs) {
            if (!row.collisionPairs.empty()) {
                row.collisionPairs += '|';
            }
            row.collisionPairs += oid.toCanonical();
        }
        row.sourceInitIndex = r.sourceInitIndex;
        row.iterations = r.iterations;
        row.signature = r.signature;
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<KinCoverageExportRow> buildCoverageExportRows(
    const RegionCoverageComputation& computation)
{
    // ---- 双射前提（§7.2 完整性——与 computeCoverage 的守恒自检同轨：
    // 导出面不静默修补失配数据，fail-fast 交调用方）----
    if (computation.results.results.size() != computation.samples.samples.size()) {
        throw std::logic_error(
            "Export：覆盖行构建检测到样本/结果数量不一致（双射前提违约"
            "——§7.2；调用方数据错误）");
    }
    std::unordered_map<std::uint64_t, const SampleResultRecord*> byIndex;
    byIndex.reserve(computation.results.results.size());
    for (const SampleResultRecord& r : computation.results.results) {
        // 重复 sampleIndex＝双射违约（同键第二出现即失败，不取后者静默）。
        if (!byIndex.emplace(r.sampleIndex, &r).second) {
            throw std::logic_error(
                "Export：覆盖行构建检测到重复 sampleIndex（双射前提违约"
                "——§7.2）");
        }
    }

    // ---- 分组 tally（键＝regionOid；分组序＝samples 首现序——用有序
    // 的"首现序键表＋每键两计数桶"避免 unordered_map 的迭代不确定序）----
    struct GroupTally {
        core::ObjectId regionOid;
        // 位置/位姿两口径各自六计数（planned 即该口径样本数——分母）。
        std::uint64_t planned[2] = {0, 0};
        std::uint64_t reached[2] = {0, 0};
        std::uint64_t unreachable[2] = {0, 0};
        std::uint64_t dataInsufficient[2] = {0, 0};
        std::uint64_t notRun[2] = {0, 0};
        std::uint64_t notApplicable[2] = {0, 0};
    };
    std::vector<GroupTally> groups;  // 首现序（确定性输出序）
    std::unordered_map<std::string, std::size_t> groupIndex;  // oid canonical→组下标

    for (const SampleRecord& sample : computation.samples.samples) {
        // 样本→结果对齐（双射查找；缺失即违约——每样本恰一结果）。
        const auto it = byIndex.find(sample.sampleIndex);
        if (it == byIndex.end()) {
            throw std::logic_error(
                "Export：覆盖行构建检测到样本无对应结果（双射前提违约"
                "——§7.2）");
        }
        const SampleState state = it->second->state;

        // 组定位（首现建组——样本生成序即分组输出序，确定性）。
        const std::string oidKey = sample.regionObjectId.toCanonical();
        auto git = groupIndex.find(oidKey);
        if (git == groupIndex.end()) {
            groupIndex.emplace(oidKey, groups.size());
            groups.push_back(GroupTally{sample.regionObjectId, {}, {}, {}, {}, {}, {}});
            git = groupIndex.find(oidKey);
        }
        GroupTally& g = groups[git->second];

        // 口径桶（Position→0／Pose→1；按枚举数值直取——两值词表）。
        const std::size_t k = sample.kind == SampleKind::Position ? 0 : 1;
        g.planned[k] += 1;  // 分母＝计划样本总数（逐样本即计划产物——R8）
        switch (state) {
        case SampleState::Reached:          g.reached[k] += 1; break;
        case SampleState::Unreachable:      g.unreachable[k] += 1; break;
        case SampleState::DataInsufficient: g.dataInsufficient[k] += 1; break;
        case SampleState::NotRun:           g.notRun[k] += 1; break;
        case SampleState::NotApplicable:    g.notApplicable[k] += 1; break;
        }
    }

    // ---- 行输出（每区域 position 行在前、pose 行在后；零样本口径不产行
    // ——分母信息归 CoverageResult 聚合面，行面只承载事实计数）----
    std::vector<KinCoverageExportRow> rows;
    for (const GroupTally& g : groups) {
        for (const std::size_t k : {std::size_t{0}, std::size_t{1}}) {
            if (g.planned[k] == 0) {
                continue;  // 该区域无此口径计划样本——不产行（见函数注）
            }
            KinCoverageExportRow row;
            row.snapshotId = computation.snapshotId;
            row.regionOid = g.regionOid;
            row.kind = sampleKindToken(k == 0 ? SampleKind::Position : SampleKind::Pose);
            row.planned = g.planned[k];
            row.reached = g.reached[k];
            row.unreachable = g.unreachable[k];
            row.dataInsufficient = g.dataInsufficient[k];
            row.notRun = g.notRun[k];
            row.notApplicable = g.notApplicable[k];
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

KinExportPackage buildKinExportPackage(KinExportHeader header,
                                       std::vector<KinSolutionExportRow> solutions,
                                       std::vector<KinFilteredSolutionExportRow> filtered,
                                       std::vector<KinCoverageExportRow> coverage)
{
    // 来源两分纪律（§7.4——导出文件头的来源声明必须自洽：归档必带
    // runId、会话内存必不带；矛盾形态属调用方错误，fail-fast 不落盘）。
    const bool archived = header.source == KinExportSource::ArchivedRun;
    if (archived && header.runId.empty()) {
        throw std::invalid_argument(
            "Export：ArchivedRun 来源要求 runId 非空（§7.4 来源声明纪律"
            "——调用方错误）");
    }
    if (!archived && !header.runId.empty()) {
        throw std::invalid_argument(
            "Export：SessionMemory 来源要求 runId 为空（§7.4 来源声明纪律"
            "——调用方错误）");
    }
    KinExportPackage pkg;
    pkg.header = std::move(header);
    pkg.solutions = std::move(solutions);
    pkg.filteredSolutions = std::move(filtered);
    pkg.coverage = std::move(coverage);
    return pkg;
}

// =====================================================================
// JSON 编码（字段定序＝契约序——Export.hpp 文件头注的文档形状）
// =====================================================================

namespace {

/// 单条解行 → JSON 对象片段（字段定序固定——增列走 formatVersion）。
std::string jsonSolutionRow(const KinSolutionExportRow& r, std::size_t level)
{
    const std::string pad = indent(level);
    std::string out = pad + "{\n";
    out += pad + "  \"rank\": " + std::to_string(r.rank) + ",\n";
    out += pad + "  \"snapshotId\": " + jsonString(r.snapshotId.toCanonical()) + ",\n";
    out += pad + "  \"pointOid\": " + jsonString(r.pointOid.toCanonical()) + ",\n";
    out += pad + "  \"conditionId\": "
        + (r.conditionId.has_value() ? jsonString(r.conditionId->toCanonical())
                                     : "null") + ",\n";
    out += pad + "  \"q\": [";
    for (std::size_t i = 0; i < r.q.size(); ++i) {
        if (i > 0) { out += ", "; }
        out += jsonNumber(r.q[i]);
    }
    out += "],\n";
    out += pad + "  \"positionResidualM\": " + jsonNumber(r.positionResidual) + ",\n";
    out += pad + "  \"orientationResidualRad\": " + jsonNumber(r.orientationResidual) + ",\n";
    out += pad + "  \"minimumJointMargin\": " + jsonNumber(r.minimumJointMargin) + ",\n";
    out += pad + "  \"manipulability\": " + jsonNumber(r.manipulability) + ",\n";
    out += pad + "  \"conditionNumber\": " + jsonNumber(r.conditionNumber) + ",\n";
    out += pad + "  \"collisionEvaluated\": " + jsonBool(r.collisionEvaluated) + ",\n";
    out += pad + "  \"collisionInCollision\": " + jsonBool(r.collisionInCollision) + ",\n";
    out += pad + "  \"sourceInitIndex\": " + std::to_string(r.sourceInitIndex) + ",\n";
    out += pad + "  \"iterations\": " + std::to_string(r.iterations) + ",\n";
    out += pad + "  \"signature\": " + jsonString(r.signature) + "\n";
    out += pad + "}";
    return out;
}

/// 单条诊断行 → JSON 对象片段。
std::string jsonFilteredRow(const KinFilteredSolutionExportRow& r, std::size_t level)
{
    const std::string pad = indent(level);
    std::string out = pad + "{\n";
    out += pad + "  \"snapshotId\": " + jsonString(r.snapshotId.toCanonical()) + ",\n";
    out += pad + "  \"pointOid\": " + jsonString(r.pointOid.toCanonical()) + ",\n";
    out += pad + "  \"conditionId\": "
        + (r.conditionId.has_value() ? jsonString(r.conditionId->toCanonical())
                                     : "null") + ",\n";
    out += pad + "  \"reason\": " + jsonString(r.reason) + ",\n";
    out += pad + "  \"q\": [";
    for (std::size_t i = 0; i < r.q.size(); ++i) {
        if (i > 0) { out += ", "; }
        out += jsonNumber(r.q[i]);
    }
    out += "],\n";
    out += pad + "  \"positionResidualM\": " + jsonNumber(r.positionResidual) + ",\n";
    out += pad + "  \"orientationResidualRad\": " + jsonNumber(r.orientationResidual) + ",\n";
    out += pad + "  \"minimumJointMargin\": " + jsonNumber(r.minimumJointMargin) + ",\n";
    out += pad + "  \"manipulability\": " + jsonNumber(r.manipulability) + ",\n";
    out += pad + "  \"collisionPairCount\": " + std::to_string(r.collisionPairCount) + ",\n";
    out += pad + "  \"collisionPairs\": " + jsonString(r.collisionPairs) + ",\n";
    out += pad + "  \"sourceInitIndex\": " + std::to_string(r.sourceInitIndex) + ",\n";
    out += pad + "  \"iterations\": " + std::to_string(r.iterations) + ",\n";
    out += pad + "  \"signature\": " + jsonString(r.signature) + "\n";
    out += pad + "}";
    return out;
}

/// 单条覆盖行 → JSON 对象片段。
std::string jsonCoverageRow(const KinCoverageExportRow& r, std::size_t level)
{
    const std::string pad = indent(level);
    std::string out = pad + "{\n";
    out += pad + "  \"snapshotId\": " + jsonString(r.snapshotId.toCanonical()) + ",\n";
    out += pad + "  \"regionOid\": " + jsonString(r.regionOid.toCanonical()) + ",\n";
    out += pad + "  \"kind\": " + jsonString(r.kind) + ",\n";
    out += pad + "  \"planned\": " + std::to_string(r.planned) + ",\n";
    out += pad + "  \"reached\": " + std::to_string(r.reached) + ",\n";
    out += pad + "  \"unreachable\": " + std::to_string(r.unreachable) + ",\n";
    out += pad + "  \"dataInsufficient\": " + std::to_string(r.dataInsufficient) + ",\n";
    out += pad + "  \"notRun\": " + std::to_string(r.notRun) + ",\n";
    out += pad + "  \"notApplicable\": " + std::to_string(r.notApplicable) + "\n";
    out += pad + "}";
    return out;
}

/// 行数组 → JSON 数组片段（空数组输出 []——形状稳定）。
template <typename RowT>
std::string jsonArray(const std::vector<RowT>& rows, std::size_t level,
                      std::string (*itemEncoder)(const RowT&, std::size_t))
{
    if (rows.empty()) {
        return "[]";
    }
    std::string out = "[\n";
    for (std::size_t i = 0; i < rows.size(); ++i) {
        out += itemEncoder(rows[i], level + 1);
        if (i + 1 < rows.size()) {
            out += ',';
        }
        out += '\n';
    }
    out += indent(level) + "]";
    return out;
}

}  // namespace

std::string encodeKinExportJson(const KinExportPackage& package)
{
    const KinExportHeader& h = package.header;
    // 顶层字段定序固定（format→formatVersion→source→runId→snapshotId→
    // mode→三行族）；runId 仅归档来源携带（来源两分纪律的文本面）。
    std::string out;
    out += "{\n";
    out += "  \"format\": \"ird-kin-export\",\n";
    out += "  \"formatVersion\": 1,\n";
    out += "  \"source\": " + jsonString(exportSourceToken(h.source)) + ",\n";
    if (h.source == KinExportSource::ArchivedRun) {
        out += "  \"runId\": " + jsonString(h.runId) + ",\n";
    }
    out += "  \"snapshotId\": " + jsonString(h.snapshotId.toCanonical()) + ",\n";
    out += "  \"mode\": " + jsonString(core::toToken(h.mode)) + ",\n";
    out += "  \"solutions\": " + jsonArray(package.solutions, 1, &jsonSolutionRow) + ",\n";
    out += "  \"filteredSolutions\": "
        + jsonArray(package.filteredSolutions, 1, &jsonFilteredRow) + ",\n";
    out += "  \"coverage\": " + jsonArray(package.coverage, 1, &jsonCoverageRow) + "\n";
    out += "}\n";
    return out;
}

// =====================================================================
// CSV 编码（'#' 注释头＋三段表——列定序固定）
// =====================================================================

std::string encodeKinExportCsv(const KinExportPackage& package)
{
    const KinExportHeader& h = package.header;
    std::string out;

    // ---- 文件头（§7.4 来源声明义务——'%' 非法字符零容忍的固定格式）----
    out += "# ird-kin-export format=csv version=1\n";
    out += std::string{"# source: "} + exportSourceToken(h.source) + "\n";
    if (h.source == KinExportSource::ArchivedRun) {
        out += "# run-id: " + h.runId + "\n";
    }
    out += "# snapshot-id: " + h.snapshotId.toCanonical() + "\n";
    out += std::string{"# mode: "} + core::toToken(h.mode) + "\n";

    // ---- 段 1：可行解行（q 逐自由度展开列 q0..qN-1——同段等宽；段内
    // 最大自由度定列数，短行右补空字段——防御面，正常输入恒等长）----
    out += "# section: solutions\n";
    std::size_t maxQ = 0;
    for (const KinSolutionExportRow& r : package.solutions) {
        maxQ = std::max(maxQ, r.q.size());
    }
    {
        std::vector<std::string> cols{"rank", "snapshotId", "pointOid", "conditionId"};
        for (std::size_t i = 0; i < maxQ; ++i) {
            cols.push_back("q" + std::to_string(i));
        }
        cols.insert(cols.end(),
                    {"positionResidualM", "orientationResidualRad",
                     "minimumJointMargin", "manipulability", "conditionNumber",
                     "collisionEvaluated", "collisionInCollision",
                     "sourceInitIndex", "iterations", "signature"});
        out += csvLine(cols);
    }
    for (const KinSolutionExportRow& r : package.solutions) {
        std::vector<std::string> f{std::to_string(r.rank),
                                   r.snapshotId.toCanonical(),
                                   r.pointOid.toCanonical(),
                                   oidText(r.conditionId)};
        for (std::size_t i = 0; i < maxQ; ++i) {
            f.push_back(i < r.q.size() ? formatDouble(r.q[i]) : std::string{});
        }
        f.insert(f.end(),
                 {formatDouble(r.positionResidual), formatDouble(r.orientationResidual),
                  formatDouble(r.minimumJointMargin), formatDouble(r.manipulability),
                  formatDouble(r.conditionNumber), boolToken(r.collisionEvaluated),
                  boolToken(r.collisionInCollision), std::to_string(r.sourceInitIndex),
                  std::to_string(r.iterations), r.signature});
        out += csvLine(f);
    }

    // ---- 段 2：过滤诊断行（同段等宽 q 列——与段 1 同规则）----
    out += "# section: filtered-solutions\n";
    maxQ = 0;
    for (const KinFilteredSolutionExportRow& r : package.filteredSolutions) {
        maxQ = std::max(maxQ, r.q.size());
    }
    {
        std::vector<std::string> cols{"snapshotId", "pointOid", "conditionId", "reason"};
        for (std::size_t i = 0; i < maxQ; ++i) {
            cols.push_back("q" + std::to_string(i));
        }
        cols.insert(cols.end(),
                    {"positionResidualM", "orientationResidualRad",
                     "minimumJointMargin", "manipulability", "collisionPairCount",
                     "collisionPairs", "sourceInitIndex", "iterations", "signature"});
        out += csvLine(cols);
    }
    for (const KinFilteredSolutionExportRow& r : package.filteredSolutions) {
        std::vector<std::string> f{r.snapshotId.toCanonical(),
                                   r.pointOid.toCanonical(),
                                   oidText(r.conditionId),
                                   r.reason};
        for (std::size_t i = 0; i < maxQ; ++i) {
            f.push_back(i < r.q.size() ? formatDouble(r.q[i]) : std::string{});
        }
        f.insert(f.end(),
                 {formatDouble(r.positionResidual), formatDouble(r.orientationResidual),
                  formatDouble(r.minimumJointMargin), formatDouble(r.manipulability),
                  std::to_string(r.collisionPairCount), r.collisionPairs,
                  std::to_string(r.sourceInitIndex), std::to_string(r.iterations),
                  r.signature});
        out += csvLine(f);
    }

    // ---- 段 3：区域覆盖行（定列——六计数）----
    out += "# section: coverage\n";
    out += csvLine({"snapshotId", "regionOid", "kind", "planned", "reached",
                    "unreachable", "dataInsufficient", "notRun", "notApplicable"});
    for (const KinCoverageExportRow& r : package.coverage) {
        out += csvLine({r.snapshotId.toCanonical(), r.regionOid.toCanonical(), r.kind,
                        std::to_string(r.planned), std::to_string(r.reached),
                        std::to_string(r.unreachable),
                        std::to_string(r.dataInsufficient), std::to_string(r.notRun),
                        std::to_string(r.notApplicable)});
    }
    return out;
}

}  // namespace sdurws::ird::kinematics
