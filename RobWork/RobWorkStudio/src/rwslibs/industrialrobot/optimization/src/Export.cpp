/**
 * @file   Export.cpp
 * @brief  优化证据一站式导出的实现（WP-20-T09）——请求校验/三元组对账/
 *         资格终判/六工件数据面组装/io canonical 写出编排。
 *
 * 实现分层（对应 Export.hpp 头注四件事）：
 *   1. buildExportBundle：纯函数组装（校验序固定→三元组对账→资格终判→
 *      限定语推导→六工件数据面）；
 *   2. writeExportBundle：注入端口驱动（JSON/CSV/MD/包四通道——io 设施
 *      唯一写盘点，本文件零 fstream/零自有编码/零自有转义）。
 *
 * 线程约束：全部纯函数/无共享可变状态（buildExportBundle const 可并发；
 *   writeExportBundle 串行驱动注入的 io 会话对象）。
 * 确定性：同请求同产出（无时钟/无随机——createdAtUtc 按记录面纪元毫秒
 *   透传；候选/任务/审计行序＝运行结果编排序）。
 */

#include <sdurws/ird/optimization/Export.hpp>

#include <sdurws/ird/core/Evaluation.hpp>   // core::toToken(EvaluationMode/
                                            //  EngineeringStatus)——记录面
                                            //  token 的唯一书写点
#include <sdurws/ird/core/Identity.hpp>     // core::TaskIdentity——任务五元组
#include <sdurws/ird/io/IoDiagnostics.hpp>  // io::errorCodeToken——环境错误
                                            //  稳定码文本（结构化返回账面）
#include <sdurws/ird/optimization/CandidatePatch.hpp>  // canonicalize/patchIdentity
                                            //  ——补丁 canonical 与补丁身份
                                            //  （包条目与补丁摘要列的唯一来源）
#include <sdurws/ird/optimization/DiagCodes.hpp>  // kOptInputInvalid/
                                            //  kOptExportContractStale——码值
                                            //  常量唯一书写点（禁拼码）
#include <sdurws/ird/optimization/Objective.hpp>  // metricDefinitions/
                                            //  toToken(MetricDirection)——指标
                                            //  词表消费（方向单一权威）
#include <sdurws/ird/optimization/Run.hpp>  // isSearchEmpty——搜索空投影

#include <cstdio>
#include <limits>
#include <utility>

namespace sdurws::ird::optimization {
namespace {

// =====================================================================
// 内部小工具（确定性书写原语——全部纯函数）
// =====================================================================

/// 枚举/词表值转 UTF-8 文本（token 生命周期静态——安全拷入 std::string）。
std::string tokenText(std::string_view v)
{
    return std::string(v);
}

/// 纪元毫秒（UTC——createdAtUtc 审计面的确定性书写：整型透传不经 locale，
/// 规避各平台 strftime/localtime 差异；时区语义由字段名 utc 声明）。
std::int64_t epochMs(std::chrono::system_clock::time_point tp)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               tp.time_since_epoch())
        .count();
}

// ---- JSON DOM 组装辅助（io::JsonValue 受限 DOM 的构造侧面——canonical
//      键序由 io 写出侧按字典序统一重排〔JsonWriteOptions.profile==nullptr
//      ＝字典序 canonical〕，此处声明序只为可读性）----

io::JsonValue jStr(std::string s)
{
    io::JsonValue v;
    v.type = io::JsonValue::Type::String;
    v.stringValue = std::move(s);
    return v;
}

io::JsonValue jInt(std::int64_t i)
{
    io::JsonValue v;
    v.type = io::JsonValue::Type::Integer;
    v.integerValue = i;
    return v;
}

/// u64 → JSON 数值（超过 int64 正域时按 io §5.9.1"超范围整数原文透传为
/// String"同源口径降为十进制字符串——不静默截断，语义由键名文档承载）。
io::JsonValue jU64(std::uint64_t u)
{
    if (u <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return jInt(static_cast<std::int64_t>(u));
    }
    char buf[24];  // u64 十进制最长 20 位——24 字节缓冲富余
    std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(u));
    return jStr(buf);
}

/// double → JSON Real（非有限值＝调用方组装违约——io 写出侧会拒绝非有限
/// Real（IO-FORMAT-JSON-NUMBER），此处提前以调用方错误轨拦截并定位字段，
/// 避免失败推迟到写盘面难以定位；NFR-COR-03 不静默转 0）。
io::JsonValue jReal(double d, const char* field)
{
    if (!(d == d) || d - d != 0) {  // NaN/±Inf 判定（位语义判定，语义同
                                    //  std::isfinite——零额外依赖）
        throw OptimizationError(
            kOptInputInvalid,
            std::string("OPT-INPUT-INVALID: 导出数据面含非有限实数（字段 ")
                + field
                + "——运行记录面存在非法数值，导出组装拒绝（NFR-COR-03："
                  "不静默转 0/不推迟到写盘）)");
    }
    io::JsonValue v;
    v.type = io::JsonValue::Type::Real;
    v.realValue = d;
    return v;
}

io::JsonValue jBool(bool b)
{
    io::JsonValue v;
    v.type = io::JsonValue::Type::Boolean;
    v.boolValue = b;
    return v;
}

io::JsonValue jNull()
{
    io::JsonValue v;  // 默认即 Null
    return v;
}

io::JsonValue jArr(std::vector<io::JsonValue> items)
{
    io::JsonValue v;
    v.type = io::JsonValue::Type::Array;
    v.items = std::move(items);
    return v;
}

io::JsonValue jObj(std::vector<io::JsonMember> members)
{
    io::JsonValue v;
    v.type = io::JsonValue::Type::Object;
    v.members = std::move(members);
    return v;
}

io::JsonMember jKey(std::string key, io::JsonValue value)
{
    io::JsonMember m;
    m.key = std::move(key);
    m.value = std::move(value);
    return m;
}

// =====================================================================
// 请求校验（buildExportBundle 执行序第 1 步——调用方违约 fail-fast）
// =====================================================================

/**
 * @brief 请求组装校验（确定性首错；全部消息带比较型定位——ERR-01）。
 *
 * 检查项（序固定）：
 *   ① run 身份面：runId/project/branch/revision/snapshotId/baselineRoot/
 *      baselineCv 非保留值（导出结论面的可追溯性锚——任一空值即不可追溯，
 *      NFR-COR-04）；
 *   ② 归档资格：archivePhase==Archived（导出源＝归档不可变历史——CON-02/
 *      PA-2；会话内结果不在导出范围——T09 @pre）；
 *   ③ spec 自身合法：validateRunSpec（T08 六条——profileId "opt" 等）；
 *   ④ 绑定一致性：spec 与 run 的身份四元组逐一相等（§11.4"导出请求必须
 *      绑定输入快照、运行身份"——研究定义副本与结论面错绑即组装违约）；
 *   ⑤ 当前面 Profile 完整：profileId=="opt"＋version 非空＋contentIdentity
 *      非保留值（三元组①当前面的可对账性）；
 *   ⑥ 记录面版本已登记：两版本 ≥1（0＝未登记——无法对账；记录面缺失是
 *      组装错误不是口径未冻结，不以"—"糊弄）。
 */
void validateExportRequest(const ExportRequest& request)
{
    const OptimizationRunResult& run = request.run;
    if (!run.runId.isValid() || !run.project.isValid() || !run.branch.isValid()
        || !run.revision.isValid() || !run.snapshotId.isValid()
        || !run.baselineRoot.isValid() || !run.baselineCv.isValid()) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: 导出请求的运行结果身份面含保留值"
                                "（runId/project/branch/revision/snapshotId/baselineRoot/"
                                "baselineCv 须全部非空——导出可追溯性锚，NFR-COR-04）");
    }
    if (run.archivePhase != ArchivePhase::Archived) {
        throw OptimizationError(
            kOptInputInvalid,
            "OPT-INPUT-INVALID: 导出源须为已归档运行结果（archivePhase=pending——"
            "会话内结果不在导出范围：归档不可变历史是导出源的结构性资格，"
            "CON-02/PA-2；先经归档面登记 ArchivePhase=archived 再导出）");
    }
    validateRunSpec(request.spec);  // T08 六条（含 profileId=="opt" 等）——复用不复制
    if (!(request.spec.project == run.project) || !(request.spec.branch == run.branch)
        || !(request.spec.revision == run.revision)
        || !(request.spec.snapshotId == run.snapshotId)) {
        throw OptimizationError(
            kOptInputInvalid,
            "OPT-INPUT-INVALID: 研究定义（spec）与运行结果（run）的身份四元组不一致"
            "（导出请求必须绑定同一输入快照与运行身份——§11.4 导出绑定面；"
            "P-OPT-6 研究定义副本须与结论面同源）");
    }
    if (request.currentProfile.profileId != std::string(kOptProfileId)
        || request.currentProfile.version.empty()
        || !request.currentProfile.contentIdentity.isValid()) {
        throw OptimizationError(
            kOptInputInvalid,
            "OPT-INPUT-INVALID: 当前面 Profile 引用不完整（profileId 须为 \"opt\"、"
            "version 非空、contentIdentity 非保留值——三元组①当前面须为装配注册表"
            "的权威查询值，三元组校验无法对账即组装违约）");
    }
    if (request.evaluatorContractVersionAtRun < 1U
        || request.exportContractVersionAtRun < 1U) {
        throw OptimizationError(
            kOptInputInvalid,
            "OPT-INPUT-INVALID: 运行记录面契约版本未登记（评估器/导出契约版本须 ≥1，"
            "0＝未登记——记录面缺失属组装错误非口径未冻结，三元组对账以登记值为准）");
    }
}

// =====================================================================
// 状态与限定语推导（执行序第 3~4 步——status 字段强制）
// =====================================================================

/**
 * @brief 当前性投影的稳定 token（"current"/"superseded"/"unevaluable"/
 *        "not-provided"——候选 CSV 当前列与 MD 当前性节的确定性书写）。
 *
 * 未提供＝"not-provided"（不默认 Current——EV-CUR-2 消费侧纪律）。
 */
std::string currentnessToken(
    const std::optional<evidence::CurrentnessResult>& currentness)
{
    if (!currentness.has_value()) {
        return "not-provided";
    }
    if (!currentness->status.has_value()) {
        return "unevaluable";
    }
    return *currentness->status == evidence::CurrentnessStatus::Current
               ? "current"
               : "superseded";
}

/// Verified 批（screeningOnly==false——正式面记录）是否存在 DataInsufficient
/// 候选（§11.2"数据不足不得冒充完整正式导出"的运行级观测；Quick 批的
/// DataInsufficient 属筛选面素材，不升运行级限定语——Quick 记录本就不
/// 支撑正式结论，EVI-01 表 1）。
bool verifiedBatchHasDataInsufficient(const OptimizationRunResult& run)
{
    for (const auto& rec : run.candidates) {
        if (!rec.screeningOnly && rec.status == CandidateStatus::DataInsufficient) {
            return true;
        }
    }
    return false;
}

// =====================================================================
// 工件 1：研究结果 JSON 组装（§11.4 行 1 字段契约——P-OPT-6 副本承载）
// =====================================================================

/// Profile 引用 → JSON 三分量对象。
io::JsonValue profileJson(const evidence::EvidenceProfileRef& p)
{
    return jObj({jKey("profileId", jStr(p.profileId)),
                 jKey("version", jStr(p.version)),
                 jKey("contentIdentity", jStr(p.contentIdentity.toCanonical()))});
}

/// config.opt 载荷 → JSON（研究定义副本的配置块——字段集＝T06
/// OptimizationConfiguration 全量：阶段/种子/并行/预算/策略/目标/绑定）。
io::JsonValue configJson(const OptimizationConfiguration& config)
{
    std::vector<io::JsonValue> objectives;
    for (const auto& entry : config.objectives.entries) {
        objectives.push_back(jObj(
            {jKey("metric", jStr(tokenText(toToken(entry.metricId)))),
             // 方向不在目标条目存储——唯一来源＝metricDirectionOf（词表冻结，
             // §7.2），此处按词表消费（单一权威，无第二口径）。
             jKey("direction",
                  jStr(tokenText(toToken(metricDirectionOf(entry.metricId))))),
             jKey("toleranceRelative",
                  jReal(entry.tolerance.relative, "objectives.toleranceRelative")),
             jKey("toleranceAbsolute",
                  jReal(entry.tolerance.absolute, "objectives.toleranceAbsolute"))}));
    }

    std::vector<io::JsonValue> variables;
    for (const auto& b : config.variables) {
        std::vector<io::JsonValue> enums;
        for (const auto& e : b.enumValues) {
            enums.push_back(jStr(e));
        }
        variables.push_back(jObj(
            {jKey("bindingId", jStr(b.bindingId)),
             jKey("kind", jStr(tokenText(toToken(b.kind)))),
             jKey("unit",
                  jStr(b.unit.isValid() ? std::string(b.unit.symbol())
                                        : std::string())),
             jKey("lowerBound", jReal(b.lowerBound, "variables.lowerBound")),
             jKey("upperBound", jReal(b.upperBound, "variables.upperBound")),
             jKey("step", jReal(b.step, "variables.step")),
             jKey("defaultValue", jReal(b.defaultValue, "variables.defaultValue")),
             jKey("defaultValueIndex", jU64(b.defaultValueIndex)),
             jKey("enumValues", jArr(std::move(enums))),
             jKey("locked", jBool(b.locked)),
             jKey("authorized", jBool(b.authorized)),
             jKey("authorityFieldPath", jStr(b.authorityFieldPath)),
             jKey("diagSubject", jStr(b.diagSubject))}));
    }

    return jObj({jKey("schemaVersion", jU64(config.schemaVersion)),
                 jKey("stage", jStr(tokenText(toToken(config.stage)))),
                 jKey("seed", jU64(config.seed)),
                 jKey("strategyId", jStr(config.strategyId)),
                 jKey("parallel",
                      jObj({jKey("threadCount", jU64(config.parallel.threadCount)),
                            jKey("maxInFlightBatches",
                                 jU64(config.parallel.maxInFlightBatches))})),
                 jKey("budget",
                      jObj({jKey("maxCandidates", jU64(config.budget.maxCandidates)),
                            jKey("maxVerifiedCandidates",
                                 jU64(config.budget.maxVerifiedCandidates)),
                            jKey("maxGenerations", jU64(config.budget.maxGenerations)),
                            jKey("maxWallClockS", jU64(config.budget.maxWallClockS))})),
                 jKey("objectives", jArr(std::move(objectives))),
                 jKey("variables", jArr(std::move(variables)))});
}

/// 单条淘汰原因 → JSON（管线淘汰与编排层追加同构——RejectionReason 全字段）。
io::JsonValue rejectionJson(const RejectionReason& r)
{
    return jObj({jKey("stage", jStr(tokenText(toToken(r.stage)))),
                 jKey("sourceId", jStr(r.sourceId)),
                 jKey("reasonToken", jStr(r.reasonToken)),
                 jKey("subject", jStr(r.subject)),
                 jKey("mode", jStr(tokenText(core::toToken(r.mode))))});
}

/// 单候选评估记录 → JSON（候选集数组元素——§11.4 行 1"候选集（CandidateId/
/// 补丁/状态/八项指标/淘汰原因）"）。
io::JsonValue candidateJson(const TwoStageRunRecord& rec)
{
    // 八项指标全量（MetricId 枚举序——缺值＝null＋gapToken 原样，"—"语义
    // 在 JSON 侧承载为 null＋缺口 token，绝不输出 0 冒充——NFR-COR-03）。
    std::vector<io::JsonValue> metrics;
    for (const auto& m : rec.metrics.metrics) {
        metrics.push_back(jObj(
            {jKey("metric", jStr(tokenText(toToken(m.metricId)))),
             jKey("valueSi",
                  m.valueSi.has_value() ? jReal(*m.valueSi, "metrics.valueSi")
                                        : jNull()),
             jKey("gapToken", jStr(tokenText(m.gapToken))),
             jKey("detail", jStr(m.detail))}));
    }

    // 淘汰原因（管线记录在前、编排层追加在后——保持产出序；R1 追加面＝
    // Quick 预算线淘汰 kRejectQuickScreenedBudget）。
    std::vector<io::JsonValue> rejections;
    for (const auto& r : rec.evaluation.rejections) {
        rejections.push_back(rejectionJson(r));
    }
    for (const auto& r : rec.extraRejections) {
        rejections.push_back(rejectionJson(r));
    }

    std::vector<io::JsonValue> facts;
    for (const auto& f : rec.evaluation.constraintFacts) {
        facts.push_back(jObj({jKey("constraint", jStr(tokenText(toToken(f.constraintId)))),
                              jKey("verdict", jStr(tokenText(toToken(f.verdict)))),
                              jKey("evaluationKey", jStr(f.evaluationKey)),
                              jKey("subject", jStr(f.subject)),
                              jKey("actualText", jStr(f.actualText)),
                              jKey("requiredText", jStr(f.requiredText)),
                              jKey("unitSymbol", jStr(f.unitSymbol)),
                              jKey("detail", jStr(f.detail))}));
    }

    std::vector<io::JsonValue> unmet;
    for (const auto& u : rec.formalPassUnmetConditions) {
        unmet.push_back(jStr(u));
    }

    return jObj(
        {jKey("candidateId", jStr(rec.candidateId.toCanonical())),
         jKey("patchId", jStr(patchIdentity(rec.patch).toCanonical())),
         jKey("isBaseline", jBool(rec.isBaseline)),
         jKey("label", jStr(rec.patch.label)),
         jKey("mode", jStr(tokenText(core::toToken(rec.mode)))),
         jKey("screeningOnly", jBool(rec.screeningOnly)),
         jKey("status", jStr(tokenText(toToken(rec.status)))),
         jKey("engineeringStatus", jStr(tokenText(core::toToken(rec.engineeringStatus)))),
         jKey("formalPassEligible", jBool(rec.formalPassEligible)),
         jKey("formalPassUnmetConditions", jArr(std::move(unmet))),
         jKey("sliceId", jStr(rec.sliceId.toCanonical())),
         jKey("cacheHit", jBool(rec.cacheHit)),
         jKey("metrics", jArr(std::move(metrics))),
         jKey("rejections", jArr(std::move(rejections))),
         jKey("constraintFacts", jArr(std::move(facts)))});
}

// =====================================================================
// 工件 2~4：三张 CSV 的表头/行集组装（§11.4 行 2/3/4 字段契约）
// =====================================================================

/// 候选 CSV 表头（§11.4 行 2："逐候选：CandidateId/补丁摘要/八项指标
///（"—"原样）/状态/淘汰原因/当前性"——列序即字段契约，v1 冻结）。
std::vector<io::IoString> candidatesCsvHeaderRow()
{
    return {"candidate_id",              "patch_id",
            "is_baseline",               "label",
            "mode",                      "screening_only",
            "status",                    "engineering_status",
            "formal_pass_eligible",      "metric_envelope",
            "metric_structural_mass",    "metric_min_joint_margin",
            "metric_cycle_time",         "metric_device_cost",
            "metric_device_mass",        "metric_joint_positive_work",
            "metric_min_drive_margin",   "rejection_reasons",
            "cache_hit",                 "currentness",
            "slice_id"};
}

/// 单候选 → 候选 CSV 行（八项指标列按 MetricId 枚举序——rec.metrics.metrics
/// 即枚举序八槽，直接对位；缺值写文本 "—" 原样——§11.4 行 2 明文，io 文本
/// 单元格承担转义，本面零自有转义）。
std::vector<io::CsvCell> candidatesCsvRow(const TwoStageRunRecord& rec,
                                          const std::string& currentness)
{
    std::vector<io::CsvCell> row;
    row.reserve(candidatesCsvHeaderRow().size());
    row.push_back(io::CsvCell::fromText(rec.candidateId.toCanonical()));
    row.push_back(io::CsvCell::fromText(patchIdentity(rec.patch).toCanonical()));
    row.push_back(io::CsvCell::fromText(rec.isBaseline ? "true" : "false"));
    row.push_back(io::CsvCell::fromText(rec.patch.label));  // 补丁标签（不参与
                                                            // 身份——呈现列）
    row.push_back(io::CsvCell::fromText(core::toToken(rec.mode)));
    row.push_back(io::CsvCell::fromText(rec.screeningOnly ? "true" : "false"));
    row.push_back(io::CsvCell::fromText(tokenText(toToken(rec.status))));
    row.push_back(io::CsvCell::fromText(core::toToken(rec.engineeringStatus)));
    row.push_back(io::CsvCell::fromText(rec.formalPassEligible ? "true" : "false"));
    // 八项指标（按 MetricId 枚举序逐槽取值——八槽全量契约由 valueOf 强制
    // （形状违约 std::invalid_argument fail-fast）；值 SI 真值，"—"为缺值
    // 原样标记——DOPT-13/§11.4 行 2"'—'原样"）。
    for (const std::uint8_t slot : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U}) {
        const MetricId id = static_cast<MetricId>(slot);
        const std::optional<double> v = rec.metrics.valueOf(id);
        row.push_back(v.has_value() ? io::CsvCell::fromReal(*v)
                                    : io::CsvCell::fromText("\xE2\x80\x94"));
    }
    // 淘汰原因（管线＋编排层追加，分号连接——保持产出序；无淘汰＝空文本）。
    std::string reasons;
    for (const auto& r : rec.evaluation.rejections) {
        if (!reasons.empty()) {
            reasons += ";";
        }
        reasons += r.reasonToken;
    }
    for (const auto& r : rec.extraRejections) {
        if (!reasons.empty()) {
            reasons += ";";
        }
        reasons += r.reasonToken;
    }
    row.push_back(io::CsvCell::fromText(reasons));
    row.push_back(io::CsvCell::fromText(rec.cacheHit ? "true" : "false"));
    row.push_back(io::CsvCell::fromText(currentness));  // 当前性（运行级投影——
                                                        // OPT-VER-148 消费面）
    row.push_back(io::CsvCell::fromText(rec.sliceId.toCanonical()));
    return row;
}

/// 任务明细 CSV 表头（§11.4 行 3："逐 execution 任务：五元组/mode/批数/
/// 起止/取消与失败记录"——批数/起止/取消失败四列为 execution 登记面字段，
/// R1 进程内编排的 RunTaskRecord 不承载（如实空单元格——不伪造执行事实；
/// R2 任务提交通道落位后由映射记录填充）。
std::vector<io::IoString> tasksCsvHeaderRow()
{
    return {"task_project",  "task_branch",     "task_revision",
            "task_run",      "task_attempt",    "mode",
            "batch_count",   "started_utc",     "finished_utc",
            "cancel_or_failure_record"};
}

/// 单任务映射 → 任务明细 CSV 行（五元组＋mode 为 R1 真实承载字段；其余四
/// 列空单元格——R1 无 execution 登记面，不冒充执行事实）。
std::vector<io::CsvCell> tasksCsvRow(const RunTaskRecord& t)
{
    return {io::CsvCell::fromText(t.task.project.toCanonical()),
            io::CsvCell::fromText(t.task.branch.toCanonical()),
            io::CsvCell::fromText(t.task.revision.toCanonical()),
            io::CsvCell::fromText(t.task.run.toCanonical()),
            io::CsvCell::fromText(t.task.attempt.toCanonical()),
            io::CsvCell::fromText(core::toToken(t.mode)),
            io::CsvCell::empty(),  // batch_count（R2）
            io::CsvCell::empty(),  // started_utc（R2）
            io::CsvCell::empty(),  // finished_utc（R2）
            io::CsvCell::empty()}; // cancel_or_failure_record（R2）
}

/// 审计计数名称常量（AT-34 对账锚——行名一经交付不得改动）。
/// 审计 CSV 表头（key-value 行式——机器对账友好：测试按 count_name 检索
/// value 与重放统计逐一比对）。
std::vector<io::IoString> auditCsvHeaderRow()
{
    return {"count_name", "value", "note"};
}

/**
 * @brief 审计 CSV 行集（§11.4 行 4 字段契约——R1 记账面全量＋R2 列按
 *        "—"导出）。
 *
 * 行序（确定性——v1 冻结）：
 *   生成数/Quick 评估数/Quick 淘汰数/Verified 复核数/缓存命中数/缓存查找数/
 *   诊断读取数/不兼容数/回放不可用数/去重数（Pareto dedupCount——T05 审计
 *   入口）/灵敏度样本数/鲁棒性样本数/误淘汰审计样本数/误淘汰率/95% 置信
 *   上界。
 * R2 四行（灵敏度/鲁棒性/误淘汰审计）与两统计列（误淘汰率/置信上界）＝
 * P-OPT-5/P-OPT-7 未冻结口径——按"—"导出（OPT-12 注：口径未冻结项按
 * "—"/待裁决口径导出），绝不伪造数值。
 */
std::vector<std::vector<io::CsvCell>> auditCsvRows(const OptimizationRunResult& run)
{
    // "—"的 UTF-8 字节（em dash——OPT-07 展示词表的导出面原样标记）。
    const io::CsvCell dash = io::CsvCell::fromText("\xE2\x80\x94");
    const TwoStageRunAuditCounts& a = run.audit;
    return {
        {io::CsvCell::fromText("candidates_generated"), io::CsvCell::fromInt(a.candidatesGenerated),
         io::CsvCell::fromText("生成候选数（含基线）")},
        {io::CsvCell::fromText("quick_evaluated"), io::CsvCell::fromInt(a.quickEvaluated),
         io::CsvCell::fromText("Quick 批实际评估数（命中回放不计）")},
        {io::CsvCell::fromText("quick_screened_out"), io::CsvCell::fromInt(a.quickScreenedOut),
         io::CsvCell::fromText("Quick 淘汰数（保守筛选预算线）")},
        {io::CsvCell::fromText("verified_evaluated"), io::CsvCell::fromInt(a.verifiedEvaluated),
         io::CsvCell::fromText("Verified 复核数（实际评估）")},
        {io::CsvCell::fromText("cache_full_hits"), io::CsvCell::fromInt(a.cacheFullHits),
         io::CsvCell::fromText("缓存命中数（命中≠Current）")},
        {io::CsvCell::fromText("cache_lookups"), io::CsvCell::fromInt(a.cacheLookups),
         io::CsvCell::fromText("缓存查找总数")},
        {io::CsvCell::fromText("cache_diagnostic_only"), io::CsvCell::fromInt(a.cacheDiagnosticOnly),
         io::CsvCell::fromText("诊断性读取数（CON-04）")},
        {io::CsvCell::fromText("cache_incompatible"), io::CsvCell::fromInt(a.cacheIncompatible),
         io::CsvCell::fromText("不兼容拒绝数（D-13 双向）")},
        {io::CsvCell::fromText("cache_replay_unavailable"), io::CsvCell::fromInt(a.cacheReplayUnavailable),
         io::CsvCell::fromText("命中但无会话载荷回放数（如实重算）")},
        {io::CsvCell::fromText("duplicates_dropped"), io::CsvCell::fromInt(run.pareto.duplicatesDropped),
         io::CsvCell::fromText("去重数（Pareto dedupCount）")},
        {io::CsvCell::fromText("sensitivity_samples"), dash,
         io::CsvCell::fromText("R2（P-OPT-5 口径未冻结——待裁决）")},
        {io::CsvCell::fromText("robustness_samples"), dash,
         io::CsvCell::fromText("R2（P-OPT-5 口径未冻结——待裁决）")},
        {io::CsvCell::fromText("false_rejection_audit_samples"), dash,
         io::CsvCell::fromText("R2 误淘汰审计（P-OPT-7 计算式未冻结——待裁决）")},
        {io::CsvCell::fromText("false_rejection_rate"), dash,
         io::CsvCell::fromText("R2 误淘汰率（P-OPT-7——待裁决）")},
        {io::CsvCell::fromText("ci95_upper_bound"), dash,
         io::CsvCell::fromText("95% 置信上界（P-OPT-7——待裁决）")},
    };
}

// =====================================================================
// 工件 5：Markdown 证据报告组装（§11.4 行 5——内容组装归本单元；限定语
// 纪律 RPT-05：估算/数据不足/未完成复核保留限定语原样）
// =====================================================================

/// 指标值或 "—"（带缺口 token）的 MD 文本（OPT-07 展示纪律的 MD 侧承载）。
std::string metricMdText(const MetricComputation& m)
{
    if (m.valueSi.has_value()) {
        char buf[40];  // %.17g 上限约 32 字符——40 字节缓冲富余
        std::snprintf(buf, sizeof(buf), "%.17g", *m.valueSi);
        return buf;
    }
    std::string s = "\xE2\x80\x94";  // "—"（em dash 原样——DOPT-13）
    if (!m.gapToken.empty()) {
        s += "（";
        s += m.gapToken;
        s += "）";
    }
    return s;
}

/**
 * @brief Markdown 证据报告全文（章节序固定——v1 冻结；全部数值/状态取自
 *        运行记录面，零二次判定——工程结论唯一归 evidence，本面只呈现）。
 *
 * 限定语纪律（RPT-05 落点）：①状态节显式携带导出状态＋限定语全清单；
 * ②逐候选的 screeningOnly/DataInsufficient/淘汰原因/未满足资格条件原样
 * 保留；③指标 "—" 缺口附 gapToken；④R2 审计列以"—（待裁决）"呈现。
 */
std::string evidenceReportMarkdown(const ExportRequest& request,
                                   const ExportStatus status,
                                   const std::vector<std::string>& qualifiers,
                                   const ExportContractCheck& contract,
                                   const std::string& currentness)
{
    const OptimizationRunResult& run = request.run;
    std::string md;
    md.reserve(4096);  // 典型研究（≤256 候选）的报告体量预估——避免反复扩容

    // ---- 标题与状态节（限定语清单显式化——"不得冒充完整正式导出"的
    //      第一呈现面）----
    md += "# 优化证据报告\n\n";
    md += "- 导出状态：";
    md += status == ExportStatus::Formal
              ? "formal（正式完整导出）"
              : "qualified（限定语导出——不得作为完整正式结论引用）";
    md += "\n";
    if (!qualifiers.empty()) {
        md += "- 限定语：";
        for (std::size_t i = 0; i < qualifiers.size(); ++i) {
            if (i > 0) {
                md += "；";
            }
            md += qualifiers[i];
        }
        md += "\n";
    }
    md += "- 运行身份：";
    md += run.runId.toCanonical();
    md += "（阶段 ";
    md += toToken(run.runPhase);
    md += "；归档 ";
    md += toToken(run.archivePhase);
    md += "；正式导出资格位 ";
    md += run.allowFormalExport ? "true" : "false";
    md += "）\n";
    md += "- 研究输入：";
    md += run.project.toCanonical();
    md += " / ";
    md += run.branch.toCanonical();
    md += " / ";
    md += run.revision.toCanonical();
    md += "（快照 ";
    md += run.snapshotId.toCanonical();
    md += "）\n";

    // ---- 比较基准声明（§11.4 行 5"基线与候选比较基准声明"）----
    md += "- 比较基准声明：全部候选相对同一基线根对象（";
    md += run.baselineRoot.toCanonical();
    md += " @ ";
    md += run.baselineCv.toCanonical();
    md += "）与同一冻结输入快照评估；基线候选（空补丁）恒参与比较（OPT-11）。\n";
    if (isSearchEmpty(run)) {
        md += "- 搜索结果：可行集为空（OPT-SEARCH-EMPTY——数据不足语义，"
              "非任务不可行；本报告不含有效候选结论）。\n";
    }
    md += "\n";

    // ---- 契约三元组节（记录面 vs 当前面逐维对账——RP-CUR-2 同源）----
    md += "## 契约三元组\n\n";
    md += "| 维度 | 结果 |\n| --- | --- |\n";
    md += contract.profileCurrent ? "| Profile | 一致 |\n" : "| Profile | 漂移 |\n";
    md += contract.evaluatorCurrent ? "| 评估器契约版本 | 一致 |\n"
                                    : "| 评估器契约版本 | 漂移 |\n";
    md += contract.exportCurrent ? "| 导出契约版本 | 一致 |\n"
                                 : "| 导出契约版本 | 漂移 |\n";
    md += contract.current() ? "\n结论：三元组当前，正式导出可用。\n\n"
                             : "\n结论：三元组漂移——正式导出已阻断（"
                               "OPT-EXPORT-CONTRACT-STALE；本工件仅限限定语消费）。\n\n";

    // ---- 当前性节（evidence 投影消费——OPT-VER-148；零判定复制）----
    md += "## 当前性\n\n";
    md += "- 状态：";
    md += currentness;
    md += "（evidence computeCurrentness 投影——本报告不自行判定）\n";
    if (request.currentness.has_value()) {
        for (const auto& r : request.currentness->reasons) {
            md += "- 失效原因：";
            md += r.detail;
            md += "\n";
        }
    }
    md += "\n";

    // ---- 任务明细节（R1 进程内编排＝空映射如实呈现——不伪造执行事实）----
    md += "## 任务明细\n\n";
    if (run.tasks.empty()) {
        md += "无 execution 任务映射（R1 进程内编排——如实空集；"
              "R2 任务提交通道随 WP-21 落位）。\n\n";
    } else {
        md += "| project | branch | revision | run | attempt | mode |\n";
        md += "| --- | --- | --- | --- | --- | --- |\n";
        for (const auto& t : run.tasks) {
            md += "| " + t.task.project.toCanonical() + " | "
                  + t.task.branch.toCanonical() + " | "
                  + t.task.revision.toCanonical() + " | " + t.task.run.toCanonical()
                  + " | " + t.task.attempt.toCanonical() + " | "
                  + core::toToken(t.mode) + " |\n";
        }
        md += "\n";
    }

    // ---- 逐候选证据节（Profile "opt" v1 项引用——§11.1 事实清单四必需项；
    //      Quick 记录携带 screening-only 限定语——EVI-01 表 1）----
    md += "## 候选证据（Profile \"opt\" v1）\n\n";
    for (const auto& rec : run.candidates) {
        md += "### 候选 ";
        md += rec.candidateId.toCanonical();
        md += "\n\n";
        md += "- 状态：";
        md += toToken(rec.status);
        md += "（工程判定 ";
        md += core::toToken(rec.engineeringStatus);
        md += "；模式 ";
        md += core::toToken(rec.mode);
        md += rec.screeningOnly
                  ? "；**screening-only——Quick 记录不支撑正式结论（EVI-01 表 1）**"
                  : "";
        md += "）\n";
        // opt.candidate-patch（补丁 canonical＋身份）。
        md += "- opt.candidate-patch：补丁 ";
        md += patchIdentity(rec.patch).toCanonical();
        md += "（";
        md += std::to_string(rec.patch.items.size());
        md += " 项覆盖）\n";
        // opt.compile-identity（候选编译身份的切片承载——sliceId 即候选编译
        // 身份链的冻结切片；完整编译身份物化随 P-OPT-2 裁决，诚实登记）。
        md += "- opt.compile-identity：评估切片 ";
        md += rec.sliceId.toCanonical();
        md += "\n";
        // opt.hard-constraint-record（逐约束 verdict＋比较型数值）。
        md += "- opt.hard-constraint-record：";
        if (rec.evaluation.constraintFacts.empty()) {
            md += "无约束事实记录\n";
        } else {
            md += "\n";
            for (const auto& f : rec.evaluation.constraintFacts) {
                md += "  - ";
                md += toToken(f.constraintId);
                md += "＝";
                md += toToken(f.verdict);
                if (!f.actualText.empty()) {
                    md += "（实际 ";
                    md += f.actualText;
                    md += " / 要求 ";
                    md += f.requiredText;
                    md += " / 单位 ";
                    md += f.unitSymbol;
                    md += "）";
                }
                if (!f.detail.empty()) {
                    md += "——";
                    md += f.detail;
                }
                md += "\n";
            }
        }
        // 八项指标（"—"原样＋缺口 token——RPT-05 限定语保留）。
        md += "- 指标（SI）：包络 ";
        md += metricMdText(rec.metrics.metrics[static_cast<std::size_t>(MetricId::Envelope)]);
        md += "；结构质量 ";
        md += metricMdText(
            rec.metrics.metrics[static_cast<std::size_t>(MetricId::StructuralMass)]);
        md += "；最小关节裕量 ";
        md += metricMdText(
            rec.metrics.metrics[static_cast<std::size_t>(MetricId::MinJointMargin)]);
        md += "；节拍 ";
        md += metricMdText(rec.metrics.metrics[static_cast<std::size_t>(MetricId::CycleTime)]);
        md += "；器件成本 ";
        md += metricMdText(rec.metrics.metrics[static_cast<std::size_t>(MetricId::DeviceCost)]);
        md += "；器件质量 ";
        md += metricMdText(rec.metrics.metrics[static_cast<std::size_t>(MetricId::DeviceMass)]);
        md += "；正机械功 ";
        md += metricMdText(
            rec.metrics.metrics[static_cast<std::size_t>(MetricId::JointPositiveWork)]);
        md += "；最小驱动裕量 ";
        md += metricMdText(
            rec.metrics.metrics[static_cast<std::size_t>(MetricId::MinDriveMargin)]);
        md += "\n";
        // 淘汰原因与正式通过资格（数据不足/未完成复核限定语原样保留）。
        std::string reasons;
        for (const auto& r : rec.evaluation.rejections) {
            if (!reasons.empty()) {
                reasons += "；";
            }
            reasons += r.reasonToken;
        }
        for (const auto& r : rec.extraRejections) {
            if (!reasons.empty()) {
                reasons += "；";
            }
            reasons += r.reasonToken;
        }
        md += "- 淘汰原因：";
        md += reasons.empty() ? "无" : reasons;
        md += "\n";
        md += "- 正式通过资格：";
        md += rec.formalPassEligible ? "eligible" : "not-eligible";
        if (!rec.formalPassUnmetConditions.empty()) {
            md += "（未满足：";
            for (std::size_t i = 0; i < rec.formalPassUnmetConditions.size(); ++i) {
                if (i > 0) {
                    md += "；";
                }
                md += rec.formalPassUnmetConditions[i];
            }
            md += "）";
        }
        md += "\n\n";
    }

    // ---- Pareto 非支配集节（§7.4 双标记呈现——非支配≠工程通过）----
    md += "## Pareto 非支配集\n\n";
    md += "- 可行集 ";
    md += std::to_string(run.pareto.feasibleIds.size());
    md += "，非支配集 ";
    md += std::to_string(run.pareto.nondominatedIds.size());
    md += "，去重 ";
    md += std::to_string(run.pareto.duplicatesDropped);
    md += "；非支配≠工程通过（工程判定唯一归 evidence 五条件）。\n\n";

    // ---- 审计节（与重放统计一致——AT-34；R2 列"—（待裁决）"）----
    md += "## 审计计数\n\n";
    md += "| 计数 | 值 | 说明 |\n| --- | --- | --- |\n";
    md += "| 生成候选数 | " + std::to_string(run.audit.candidatesGenerated)
          + " | 含基线 |\n";
    md += "| Quick 评估数 | " + std::to_string(run.audit.quickEvaluated)
          + " | 命中回放不计 |\n";
    md += "| Quick 淘汰数 | " + std::to_string(run.audit.quickScreenedOut)
          + " | 保守筛选预算线 |\n";
    md += "| Verified 复核数 | " + std::to_string(run.audit.verifiedEvaluated)
          + " | 实际评估 |\n";
    md += "| 缓存命中数 | " + std::to_string(run.audit.cacheFullHits)
          + " | 命中≠Current |\n";
    md += "| 去重数 | " + std::to_string(run.pareto.duplicatesDropped)
          + " | Pareto dedupCount |\n";
    md += "| 灵敏度样本数 | \xE2\x80\x94 | R2（待裁决） |\n";
    md += "| 鲁棒性样本数 | \xE2\x80\x94 | R2（待裁决） |\n";
    md += "| 误淘汰审计样本数 | \xE2\x80\x94 | R2（P-OPT-7 待裁决） |\n";
    md += "| 误淘汰率 | \xE2\x80\x94 | R2（P-OPT-7 待裁决） |\n";
    md += "| 95% 置信上界 | \xE2\x80\x94 | R2（P-OPT-7 待裁决） |\n\n";

    // ---- 工件清单节（一站式导出的自描述——六工件对账）----
    md += "## 导出工件清单\n\n";
    md += "1. " + std::string(kExportResearchJsonName)
          + "（研究结果 JSON——研究定义副本，P-OPT-6）\n";
    md += "2. " + std::string(kExportCandidatesCsvName) + "（候选 CSV）\n";
    md += "3. " + std::string(kExportTasksCsvName) + "（任务明细 CSV）\n";
    md += "4. " + std::string(kExportAuditCsvName) + "（审计 CSV——AT-34 对账面）\n";
    md += "5. " + std::string(kExportEvidenceReportName) + "（本报告）\n";
    md += "6. " + std::string(kExportCandidatePackageName)
          + "（候选模型包——补丁 canonical＋来源标识，P-OPT-2 裁决前形态）\n";
    return md;
}

}  // namespace

// =====================================================================
// 公共实现：状态 token／产品 io 端口／组装／写出编排
// =====================================================================

std::string_view toToken(ExportStatus s) noexcept
{
    switch (s) {
    case ExportStatus::Formal:
        return "formal";
    case ExportStatus::Qualified:
        return "qualified";
    }
    return "qualified";  // 封闭枚举不可达分支——保守返回限定语语义
}

ExportIoPorts makeDefaultExportIoPorts()
{
    ExportIoPorts ports;
    // 真 io 设施（§9.11 工厂——产品装配形态；测试可整体替换本结构注入
    // 计数替身——P-RPT-1 注入形态的两侧：产品侧供真、测试侧供替身）。
    ports.jsonWriterFactory = [] { return io::makeJsonWriter(); };
    ports.csvWriterFactory = [] { return io::makeCsvWriter(); };
    ports.atomicWriter = io::makeAtomicFileWriter();
    // packageWriter 恒空：候选包通道的产品适配器（桥 project 快照＋io
    // IPackageExporter）随 P-OPT-2/MDL-20 联调落位——R1 由装配层/测试
    // 供给，此处不预建占位（NFR-MNT-04）。
    ports.packageWriter = nullptr;
    return ports;
}

ExportBundleData OptimizationExportProvider::buildExportBundle(
    const ExportRequest& request) const
{
    // ---- 第 1 步：请求校验（调用方违约 fail-fast——见匿名命名空间注）。
    validateExportRequest(request);
    const OptimizationRunResult& run = request.run;

    // ---- 第 2 步：契约三元组对账（记录面 vs 当前面——逐维独立记录，
    //      断言定位到维度；对账是纯比较，零注册表查询——当前面值由调用方
    //      自装配注册表投影随请求携带，同 ProjectBaselineState 先例）。
    ExportContractCheck contract;
    contract.profileCurrent = (request.currentProfile == request.spec.profile);
    contract.evaluatorCurrent
        = (request.evaluatorContractVersionAtRun == kOptStaticScreenContractVersion);
    contract.exportCurrent
        = (request.exportContractVersionAtRun == kOptExportContractVersion);

    // ---- 第 3 步：资格终判（formal 请求无资格即阻断——§12.2 调用示例
    //      非法 2"OPT-EXPORT-CONTRACT-STALE 语义拒绝"，绝不降级为限定语
    //      导出"悄悄放行"）。
    if (request.formal) {
        if (!run.allowFormalExport) {
            // 取消/失败/中断等非 Completed 运行——TASK-02：不得冒充正式导出
            // （消息定位到运行阶段——比较型定位纪律）。
            throw OptimizationError(
                kOptExportContractStale,
                "OPT-EXPORT-CONTRACT-STALE: 正式导出被拒绝——运行无正式导出资格"
                "（runPhase=" + tokenText(toToken(run.runPhase))
                    + "，allowFormalExport=false——取消/失败/中断结果不得冒充完整"
                      "正式导出，TASK-02/NFR-REL-03）");
        }
        if (!contract.current()) {
            // 三元组漂移——版本与结果记录不符即阻断（§11.4；消息定位到
            // 具体漂移维度）。
            std::string dims;
            if (!contract.profileCurrent) {
                dims += "Profile 身份 ";
            }
            if (!contract.evaluatorCurrent) {
                dims += "评估器契约版本 ";
            }
            if (!contract.exportCurrent) {
                dims += "导出契约版本 ";
            }
            throw OptimizationError(
                kOptExportContractStale,
                "OPT-EXPORT-CONTRACT-STALE: 正式导出被拒绝——契约三元组漂移（"
                    + dims + "与运行记录不符——reporting RP-CUR-2 同源，"
                             "契约过期阻断正式导出）");
        }
    }

    // ---- 第 4 步：状态与限定语推导（Formal 资格＝formal 请求＋allowFormal-
    //      Export＋三元组当前，三条件与上方阻断面同判据）。限定语**无条件
    //      推导**（两类型分立）：
    //      · 资格缺失类（run-not-completed/contract-stale）只可能出现在
    //        Qualified（formal 阻断面保证）；
    //      · 事实自述类（search-empty/verified-data-insufficient/当前性三态）
    //        在 Formal 工件上同样携带——"formal"认证的是运行完成＋契约
    //        当前，不是空集/数据不足/当前性未核的事实消失（RPT-05 限定语
    //        纪律：工件自我声明，绝不静默）。产出序＝kExportQualifier*
    //        常量登记序（确定性）。
    const bool formalEligible
        = request.formal && run.allowFormalExport && contract.current();
    const ExportStatus status = formalEligible ? ExportStatus::Formal
                                               : ExportStatus::Qualified;
    std::vector<std::string> qualifiers;
    if (run.runPhase != RunPhase::Completed) {
        qualifiers.emplace_back(kExportQualifierRunNotCompleted);
    }
    if (isSearchEmpty(run)) {
        qualifiers.emplace_back(kExportQualifierSearchEmpty);
    }
    if (verifiedBatchHasDataInsufficient(run)) {
        qualifiers.emplace_back(kExportQualifierVerifiedDataInsufficient);
    }
    if (!contract.current()) {
        qualifiers.emplace_back(kExportQualifierContractStale);
    }
    // 当前性三态限定语（互斥；未携带＝not-provided——绝不默认 Current）。
    const std::string cur = currentnessToken(request.currentness);
    if (cur == "superseded") {
        qualifiers.emplace_back(kExportQualifierSuperseded);
    } else if (cur == "unevaluable") {
        qualifiers.emplace_back(kExportQualifierCurrentnessUnevaluable);
    } else if (cur == "not-provided") {
        qualifiers.emplace_back(kExportQualifierCurrentnessNotProvided);
    }

    // ---- 第 5 步：六工件数据面组装（§11.4 行 1→6；各组装规则见对应
    //      匿名命名空间函数注）。
    ExportBundleData bundle;
    bundle.status = status;
    bundle.qualifierTokens = qualifiers;
    bundle.contract = contract;

    // 工件 1：研究结果 JSON（P-OPT-6 研究定义副本＋运行/身份/配置/候选/
    // Pareto/审计/当前性/契约全量—— canonical 写出后即跨会话恢复源）。
    std::vector<io::JsonValue> qualifierJson;
    for (const auto& q : qualifiers) {
        qualifierJson.push_back(jStr(q));
    }
    std::vector<io::JsonValue> taskJson;
    for (const auto& t : run.tasks) {
        taskJson.push_back(jObj({jKey("project", jStr(t.task.project.toCanonical())),
                                 jKey("branch", jStr(t.task.branch.toCanonical())),
                                 jKey("revision", jStr(t.task.revision.toCanonical())),
                                 jKey("run", jStr(t.task.run.toCanonical())),
                                 jKey("attempt", jStr(t.task.attempt.toCanonical())),
                                 jKey("mode", jStr(core::toToken(t.mode)))}));
    }
    std::vector<io::JsonValue> candidateJsonList;
    for (const auto& rec : run.candidates) {
        candidateJsonList.push_back(candidateJson(rec));
    }
    std::vector<io::JsonValue> paretoJson;
    for (const auto& e : run.pareto.entries) {
        paretoJson.push_back(
            jObj({jKey("candidateId", jStr(e.candidateId.toCanonical())),
                  jKey("isBaseline", jBool(e.isBaseline)),
                  jKey("nondominationRank", jU64(e.nondominationRank)),
                  jKey("paretoNondominated", jBool(e.paretoNondominated))}));
    }
    std::vector<io::JsonValue> nondominatedJson;
    for (const auto& id : run.pareto.nondominatedIds) {
        nondominatedJson.push_back(jStr(id.toCanonical()));
    }
    std::vector<io::JsonValue> feasibleJson;
    for (const auto& id : run.pareto.feasibleIds) {
        feasibleJson.push_back(jStr(id.toCanonical()));
    }
    // 当前性块（投影消费——reasons 原样；未携带＝provided=false）。
    io::JsonValue currentnessJson = jObj(
        {jKey("provided", jBool(request.currentness.has_value())),
         jKey("status", jStr(currentnessToken(request.currentness))),
         jKey("unevaluableCause",
              request.currentness.has_value() && !request.currentness->status.has_value()
                  ? jStr(request.currentness->unevaluableCause.has_value()
                             ? (*request.currentness->unevaluableCause
                                == evidence::UnevaluableCause::CrossContext
                                    ? "cross-context"
                                    : "unresolved-dependency")
                             : "")
                  : jStr("")),
         jKey("reasons", [&] {
             std::vector<io::JsonValue> rs;
             if (request.currentness.has_value()) {
                 for (const auto& r : request.currentness->reasons) {
                     rs.push_back(jObj({jKey("dependencyKey", jStr(r.dependencyKey)),
                                        jKey("kind", jInt(static_cast<std::int64_t>(r.kind))),
                                        jKey("detail", jStr(r.detail))}));
                 }
             }
             return jArr(std::move(rs));
         }())});
    // 契约块（记录面/当前面双值——对账凭据全量呈现）。
    const io::JsonValue contractJson = jObj(
        {jKey("current", jBool(contract.current())),
         jKey("profileCurrent", jBool(contract.profileCurrent)),
         jKey("evaluatorCurrent", jBool(contract.evaluatorCurrent)),
         jKey("exportCurrent", jBool(contract.exportCurrent)),
         jKey("recordedProfile", profileJson(request.spec.profile)),
         jKey("currentProfile", profileJson(request.currentProfile)),
         jKey("evaluatorContractVersionAtRun", jU64(request.evaluatorContractVersionAtRun)),
         jKey("evaluatorContractVersionCurrent", jU64(kOptStaticScreenContractVersion)),
         jKey("exportContractVersionAtRun", jU64(request.exportContractVersionAtRun)),
         jKey("exportContractVersionCurrent", jU64(kOptExportContractVersion))});

    bundle.researchJson.root = jObj(
        {jKey("schema",
              jObj({jKey("kind", jStr("ird-opt-research")),
                    jKey("exportContractVersion", jU64(kOptExportContractVersion))})),
         jKey("export",
              jObj({jKey("status", jStr(tokenText(toToken(status)))),
                    jKey("formalRequested", jBool(request.formal)),
                    jKey("qualifiers", jArr(std::move(qualifierJson))),
                    jKey("contract", contractJson)})),
         jKey("run",
              jObj({jKey("runId", jStr(run.runId.toCanonical())),
                    jKey("runPhase", jStr(tokenText(toToken(run.runPhase)))),
                    jKey("archivePhase", jStr(tokenText(toToken(run.archivePhase)))),
                    jKey("allowFormalExport", jBool(run.allowFormalExport)),
                    jKey("searchEmpty", jBool(isSearchEmpty(run)))})),
         jKey("identity",
              jObj({jKey("project", jStr(run.project.toCanonical())),
                    jKey("branch", jStr(run.branch.toCanonical())),
                    jKey("revision", jStr(run.revision.toCanonical())),
                    jKey("snapshotId", jStr(run.snapshotId.toCanonical())),
                    jKey("baselineRoot", jStr(run.baselineRoot.toCanonical())),
                    jKey("baselineCv", jStr(run.baselineCv.toCanonical())),
                    // EvaluatorSetId（运行登记的评估器集摘要——§4.2 辅助身份，
                    // 随请求记录面供给；保留值＝未登记，"—"导出口径以 null 承载）。
                    jKey("evaluatorSetId",
                         request.evaluatorSetId.isValid()
                             ? jStr(request.evaluatorSetId.toCanonical())
                             : jNull())})),
         jKey("research",
              jObj({jKey("project", jStr(request.spec.project.toCanonical())),
                    jKey("branch", jStr(request.spec.branch.toCanonical())),
                    jKey("revision", jStr(request.spec.revision.toCanonical())),
                    jKey("snapshotId", jStr(request.spec.snapshotId.toCanonical())),
                    jKey("createdBy", jStr(request.spec.createdBy)),
                    jKey("createdAtUtcEpochMs", jInt(epochMs(request.spec.createdAtUtc))),
                    jKey("profile", profileJson(request.spec.profile)),
                    jKey("config", configJson(request.spec.config))})),
         jKey("tasks", jArr(std::move(taskJson))),
         jKey("candidates", jArr(std::move(candidateJsonList))),
         jKey("pareto",
              jObj({jKey("entries", jArr(std::move(paretoJson))),
                    jKey("nondominatedIds", jArr(std::move(nondominatedJson))),
                    jKey("feasibleIds", jArr(std::move(feasibleJson))),
                    jKey("duplicatesDropped", jU64(run.pareto.duplicatesDropped))})),
         jKey("audit",
              jObj({jKey("candidatesGenerated", jU64(run.audit.candidatesGenerated)),
                    jKey("quickEvaluated", jU64(run.audit.quickEvaluated)),
                    jKey("quickScreenedOut", jU64(run.audit.quickScreenedOut)),
                    jKey("verifiedEvaluated", jU64(run.audit.verifiedEvaluated)),
                    jKey("cacheLookups", jU64(run.audit.cacheLookups)),
                    jKey("cacheFullHits", jU64(run.audit.cacheFullHits)),
                    jKey("cacheDiagnosticOnly", jU64(run.audit.cacheDiagnosticOnly)),
                    jKey("cacheIncompatible", jU64(run.audit.cacheIncompatible)),
                    jKey("cacheReplayUnavailable", jU64(run.audit.cacheReplayUnavailable))})),
         jKey("currentness", currentnessJson)});

    // 工件 2：候选 CSV（行序＝run.candidates 编排序——确定性）。
    bundle.candidatesCsvHeader = candidatesCsvHeaderRow();
    const std::string curToken = currentnessToken(request.currentness);
    for (const auto& rec : run.candidates) {
        bundle.candidatesCsvRows.push_back(candidatesCsvRow(rec, curToken));
    }

    // 工件 3：任务明细 CSV（R1 常态＝仅表头——空映射如实呈现）。
    bundle.tasksCsvHeader = tasksCsvHeaderRow();
    for (const auto& t : run.tasks) {
        bundle.tasksCsvRows.push_back(tasksCsvRow(t));
    }

    // 工件 4：审计 CSV（与重放统计一致——AT-34 对账面）。
    bundle.auditCsvHeader = auditCsvHeaderRow();
    bundle.auditCsvRows = auditCsvRows(run);

    // 工件 5：Markdown 证据报告（内容组装归本单元——限定语纪律 RPT-05）。
    bundle.evidenceReportMarkdown
        = evidenceReportMarkdown(request, status, qualifiers, contract, curToken);

    // 工件 6：候选模型包条目集（补丁 canonical＋来源标识清单——P-OPT-2
    // 裁决前的候选设计工件承载；包内清单 JSON 经 io canonicalizeJson 单点
    // canonical——SA-12 集合第二编码器）。
    // 包内清单（来源标识 manifest——roundtrip 对账键）。
    std::vector<io::JsonValue> manifestCandidates;
    std::vector<io::JsonValue> entryPaths;
    for (const auto& rec : run.candidates) {
        const std::string cid = rec.candidateId.toCanonical();
        manifestCandidates.push_back(
            jObj({jKey("candidateId", jStr(cid)),
                  jKey("patchId", jStr(patchIdentity(rec.patch).toCanonical())),
                  jKey("sliceId", jStr(rec.sliceId.toCanonical())),
                  jKey("mode", jStr(core::toToken(rec.mode))),
                  jKey("status", jStr(tokenText(toToken(rec.status)))),
                  jKey("patchEntryPath",
                       jStr("candidates/" + cid + ".patch.canonical"))}));
        entryPaths.push_back(jStr("candidates/" + cid + ".patch.canonical"));
        // 候选设计工件条目＝补丁 canonical 字节（IRDOPTP1——候选描述的
        // 唯一编码层，§5.5；完整候选设计字节物化随 P-OPT-2 裁决升级）。
        ExportPackageEntry entry;
        entry.packPath = "candidates/" + cid + ".patch.canonical";
        entry.bytes = canonicalize(rec.patch);
        bundle.candidatePackageEntries.push_back(std::move(entry));
    }
    io::JsonDocument manifestDoc;
    manifestDoc.root = jObj(
        {jKey("schema",
              jObj({jKey("kind", jStr("ird-opt-candidate-package")),
                    jKey("exportContractVersion", jU64(kOptExportContractVersion))})),
         jKey("source",
              jObj({jKey("runId", jStr(run.runId.toCanonical())),
                    jKey("project", jStr(run.project.toCanonical())),
                    jKey("branch", jStr(run.branch.toCanonical())),
                    jKey("revision", jStr(run.revision.toCanonical())),
                    jKey("snapshotId", jStr(run.snapshotId.toCanonical())),
                    jKey("baselineRoot", jStr(run.baselineRoot.toCanonical())),
                    jKey("baselineCv", jStr(run.baselineCv.toCanonical()))})),
         jKey("candidates", jArr(std::move(manifestCandidates)))});
    const io::JsonWriteOptions canonicalOptions{};  // profile=nullptr→字典序
    const io::IoResult<io::IoString> manifestBytes
        = io::canonicalizeJson(manifestDoc, canonicalOptions);
    if (!manifestBytes) {
        // 包清单 canonical 化失败（非有限数值等——组装面数据缺陷）：以
        // 调用方错误轨抛出并定位（io 错误码透传进消息——fail-fast，不带
        // 病组装半套工件）。
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: 候选包清单 canonical 化失败（io 码 "
                                    + std::string(io::errorCodeToken(
                                          manifestBytes.error.code))
                                    + "——数据面存在非法数值，见 detail）");
    }
    ExportPackageEntry manifestEntry;
    manifestEntry.packPath = "candidate-package/manifest.json";
    manifestEntry.bytes.assign(manifestBytes.value.begin(), manifestBytes.value.end());
    bundle.candidatePackageEntries.push_back(manifestEntry);

    return bundle;
}

ExportWriteResult writeExportBundle(const ExportBundleData& bundle,
                                    const ExportIoPorts& ports,
                                    const std::filesystem::path& targetDirectory)
{
    // ---- 编排校验（调用方组装违约 fail-fast——四端口缺一即拒绝：六工件
    //      要么全套要么明确失败，不出"三件套冒充全套"）。
    if (!ports.jsonWriterFactory || !ports.csvWriterFactory || !ports.atomicWriter
        || ports.packageWriter == nullptr) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: 导出 io 端口未装配完整"
                                "（jsonWriterFactory/csvWriterFactory/atomicWriter/"
                                "packageWriter 四端口缺一不可——P-RPT-1 注入形态，"
                                "产品装配 makeDefaultExportIoPorts()＋包缝适配器）");
    }
    if (targetDirectory.empty()) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: 导出目标目录为空"
                                "（io 不代建目录——目标目录须已存在）");
    }

    ExportWriteResult result;

    // ---- 工件 1：研究结果 JSON（canonical 写出——字典序键序由 io 单点；
    //      文件目标内部即"暂存＋原子替换"，失败目标不变）。
    result.researchJson.target = targetDirectory / kExportResearchJsonName;
    {
        std::unique_ptr<io::IJsonWriter> writer = ports.jsonWriterFactory();
        const io::IoResult<void> r = writer->write(
            io::JsonOutputTarget::file(result.researchJson.target), bundle.researchJson,
            io::JsonWriteOptions{});  // profile=nullptr → 字典序 canonical
        result.researchJson.ok = static_cast<bool>(r);
        if (!r) {
            result.researchJson.errorCode = std::string(io::errorCodeToken(r.error.code));
        }
    }

    // ---- 工件 2~4：三张 CSV（每张一个会话——open→header→rows→finish；
    //      RAII：任何一步失败时 writer 析构即放弃，暂存清理、目标不变，
    //      io §9.4；finish 成功＝原子替换就位）。
    const auto writeCsv = [](ExportArtifactOutcome& outcome,
                             std::function<std::unique_ptr<io::ICsvWriter>()> factory,
                             const std::vector<io::IoString>& header,
                             const std::vector<std::vector<io::CsvCell>>& rows) {
        std::unique_ptr<io::ICsvWriter> writer = factory();
        io::IoResult<void> r = writer->open(io::CsvOutputTarget::file(outcome.target),
                                            io::CsvWriteOptions{}); // rwDefault＋标识行
        if (r) {
            r = writer->writeHeader(header);
        }
        for (const auto& row : rows) {
            if (!r) {
                break;  // 首错即停——错误码保留首个（io 会话失败后作废）
            }
            r = writer->writeRow(row);
        }
        if (r) {
            r = writer->finish();  // 原子替换就位（同目录暂存＋rename）
        }
        outcome.ok = static_cast<bool>(r);
        if (!r) {
            outcome.errorCode = std::string(io::errorCodeToken(r.error.code));
        }
    };
    result.candidatesCsv.target = targetDirectory / kExportCandidatesCsvName;
    writeCsv(result.candidatesCsv, ports.csvWriterFactory, bundle.candidatesCsvHeader,
             bundle.candidatesCsvRows);
    result.tasksCsv.target = targetDirectory / kExportTasksCsvName;
    writeCsv(result.tasksCsv, ports.csvWriterFactory, bundle.tasksCsvHeader,
             bundle.tasksCsvRows);
    result.auditCsv.target = targetDirectory / kExportAuditCsvName;
    writeCsv(result.auditCsv, ports.csvWriterFactory, bundle.auditCsvHeader,
             bundle.auditCsvRows);

    // ---- 工件 5：Markdown 证据报告（prepare→write→commit 原子协议；
    //      失败路径 abort——目标零接触，io §9.10）。
    result.evidenceReport.target = targetDirectory / kExportEvidenceReportName;
    {
        const io::IoResult<io::AtomicTarget> prepared = ports.atomicWriter->prepare(
            result.evidenceReport.target, io::ReplacePolicy::OverwriteAtomic);
        if (!prepared) {
            result.evidenceReport.ok = false;
            result.evidenceReport.errorCode
                = std::string(io::errorCodeToken(prepared.error.code));
        } else {
            io::AtomicTarget target = prepared.value;
            io::IoResult<void> r = target.write(bundle.evidenceReportMarkdown);
            if (r) {
                r = ports.atomicWriter->commit(target);
            }
            if (!r) {
                // 失败清理（幂等——已终态 abort 亦成功；目标不变）。
                (void)ports.atomicWriter->abort(target);
                result.evidenceReport.errorCode
                    = std::string(io::errorCodeToken(r.error.code));
            }
            result.evidenceReport.ok = static_cast<bool>(r);
        }
    }

    // ---- 工件 6：候选模型包（通道缝——条目集原子就位语义归缝实现）。
    result.candidatePackage.target = targetDirectory / kExportCandidatePackageName;
    {
        const io::IoResult<void> r
            = ports.packageWriter->writePackage(bundle.candidatePackageEntries,
                                                result.candidatePackage.target);
        result.candidatePackage.ok = static_cast<bool>(r);
        if (!r) {
            result.candidatePackage.errorCode
                = std::string(io::errorCodeToken(r.error.code));
        }
    }

    return result;
}

}  // namespace sdurws::ird::optimization
