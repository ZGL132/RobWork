/**
 * @file   Render.cpp
 * @brief  渲染实现——FieldMatrix 提取（单次投影）＋HTML/JSON/CSV 三格式
 *         渲染器（§8.1~§8.4/§9.3；RPT-T06 产物）。
 *
 * 设计依据（与 Render.hpp 文件头一致，此处只记实现口径）：
 *   - §8.4：字段矩阵为三渲染器唯一值来源——本文件的实现纪律是"渲染器
 *     对条目字段值零自行计算"：HTML/JSON/CSV 一律经 MatrixIndex 按
 *     fieldKey 查矩阵单元格，查不到或矩阵有剩余单元格＝矩阵与报告不同源
 *     （DataInvalid——§9.3 前置"matrix 与 report 同源"的执行面）；
 *   - §8.1 总则：稳定排序（章节 order→条目 entryKey→证据 itemId→诊断
 *     orderKey）、数值 to_chars 最短往返、UTF-8 无 BOM、HTML/JSON 行尾
 *     LF（CSV 行尾归 io 方言 CRLF）、渲染错误与数据错误严格区分；
 *   - §8.2：两类声明前置断言（assertFormalPassWordingAllowed——
 *     DataInvalid 硬门槛）；限定语结构性输出（data-qualifier/qualifier
 *     列/qualifier 数组）；
 *   - §8.2①硬编码纪律的规则层（RPT-T08 交付，validateDeclarationConsistency）：
 *     渲染器在装配前对报告数据做五路前置断言——①非 Completed 结果永不携带
 *     任何声明资格（RP-STATE-1：失败/取消/中断不入正式结论——TASK-02）；
 *     ②formalPass 资格 ⇒ Verified∧Completed∧Feasible（RPT-05 五条件的报告
 *     可见面——Quick 永不 FormalPass，表 1 模式效力）；③reviewRecord 资格 ⇒
 *     Completed∧EngineeringInfeasible（§6.2 行 2）；④章节资格声明 ⇔ 所引
 *     结果资格 AND 聚合（§8.2①"条目所属结果"复核——构建器聚合不变量的
 *     渲染侧防线）；⑤覆盖漏验 ⇒ 无任何 formalPass 章节（EVI-02/§8.1 表 2②
 *     呈现口径——漏验任一启用必验工况不得输出正式通过）；
 *   - §6.4/§6.5 呈现义务锁（RPT-T08）：覆盖完备性结论（"全部启用必验工况
 *     已覆盖"或"漏验清单——整体数据不足"）、包络合并条目"包络合并（呈现
 *     方式）"标注（caseScope 跨多工况的结构触发）、downgraded-reference-
 *     value 覆盖呈现面限定语（P-EV-5 报告侧承接——数据源＝diagRefs 携带
 *     EVI-REGION-COVERAGE-DOWNGRADED）与 external-validation-incomplete
 *     外部验证边界章限定语（数据源＝externalResourceSummary Recorded 态）
 *     的报告级结构性输出；§8.2④模板纪律自检（词表全 token 呈现文案缺位
 *     ＝TemplateVersion 拒绝——模板不得移除限定语）；
 *   - §4.3.3/NFR-SEC-07：诊断脱敏双保险——渲染侧对机器产源自由文本再过
 *     IRedactionService（User 档）；范围＝诊断定位名/缺项原因/不适用原因/
 *     资格说明/评审元数据人读字段/生成者/版本串等自由文本；**不含**值文本
 *     （Invalid 态保留原文是 NFR-COR-03 硬约束，脱敏会破坏"保留原文"）、
 *     词表 token 与规范身份文本（UX-02 允许域：追溯区块/附录/JSON 机器
 *     镜像）；
 *   - KIN-12：显示单位纯投影——换算一律经 core tryConvert 唯一入口，
 *     本文件零第二换算实现；SI 基线单位表为 core R1 注册表子集（逐项
 *     UnitToken::find 校验——注册表基线变化即 DataInvalid，P-RPT-9 同步面）。
 *
 * 确定性（NFR-COR-02）：全部输出路径无时钟/随机/locale 依赖——时间戳经
 * gmtime 定长格式化（UTC 秒精度）、数值经 to_chars（locale 无关）、排序
 * 按冻结键（稳定排序）、遍历按 §8.4 冻结序；同 (report, matrix, format,
 * 版本) → 字节相同。
 *
 * 线程安全：全部渲染函数无共享可变状态（缓冲局部）——并发安全（§9.3）。
 */

#include <sdurws/ird/reporting/Render.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <charconv>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <sdurws/ird/core/Evaluation.hpp>       // core::toToken(EvaluationMode/TaskOutcome/EngineeringStatus)
#include <sdurws/ird/core/Identity.hpp>         // Id128 族 toCanonical（规范文本）
#include <sdurws/ird/diagnostics/DiagCodes.hpp> // severityToken/categoryToken（码表元数据——C-3 只读）
#include <sdurws/ird/evidence/Currentness.hpp>  // CurrentnessStatus/InvalidationReason（词表消费）
#include <sdurws/ird/evidence/Evidence.hpp>     // EvidenceItemStatus/Class（O-13 消费不重定义）
#include <sdurws/ird/evidence/Verdict.hpp>      // kDiagRegionCoverageDowngraded（P-EV-5 降级
                                                // 事实的报告内数据源码——evidence EV-T06
                                                // 建议码常量，单一定义不私抄字面）
#include <sdurws/ird/reporting/Sections.hpp>    // trySectionOrder（§5.1 词表 order——诊断 orderKey 基准）

#include "RenderText.hpp"   // token↔显示名共享表（RPT-T07 起正向/反向同表——值单源）

namespace sdurws::ird::reporting {

namespace {

// =====================================================================
// 章节显示名（§5.1 词表"章节"列的呈现映射——词表权威仍归 Sections.hpp
// 的 sectionId token，本表只是 HTML 正文的工程用语映射；词表外 sectionId
// 回退为 token 本身——呈现层永不改写身份 token）
// =====================================================================

std::string sectionDisplayName(std::string_view sectionId)
{
    // 与 §5.1 表行一一对应（token → 中文显示名；查找表冻结不改名）。
    static const std::map<std::string_view, std::string_view> kNames = {
        {kSectionProjectScheme, "项目与方案"},
        {kSectionInputSummary, "输入摘要"},
        {kSectionModel, "模型"},
        {kSectionRequirements, "需求"},
        {kSectionKinematicsCollision, "运动学与碰撞"},
        {kSectionOptimizationCandidates, "静态优化候选"},
        {kSectionDiagnostics, "诊断"},
        {kSectionExternalValidationBoundary, "外部验证边界"},
        {kSectionTrajectoryCycle, "轨迹与节拍"},
        {kSectionDynamicsEnvelope, "动力学曲线、峰值和 RMS"},
        {kSectionDrivetrainOperatingPoints, "传动工作点"},
        {kSectionSelectionBom, "选型/BOM 与淘汰依据"},
        {kSectionReviewSignoff, "评审/签署元数据"},
        {kSectionVariantDiff, "改型差异和取舍理由"},
    };
    const auto it = kNames.find(sectionId);
    if (it != kNames.end()) {
        return std::string(it->second);
    }
    return std::string(sectionId);   // 词表外——回退 token（不伪造显示名）
}

/// 章节状态徽标文案（§5.3 呈现列——"未选择（缺正式结果）"等四态）。
std::string_view sectionStatusBadge(SectionStatus status)
{
    switch (status) {
    case SectionStatus::Populated:        return "正常";
    case SectionStatus::NoFormalResult:   return "未选择（缺正式结果）";
    case SectionStatus::DataInsufficient: return "数据不足";
    case SectionStatus::NotApplicable:    return "不适用";
    }
    return "";   // 不可达（全枚举 switch——防告警面）
}

/// 工程判定显示名（§6.6"报告内呈现"列——结论列的中文工程用语；token
/// 形态见 core::toToken，AT-22 比对维度用 token，正文呈现用本表）。
std::string_view engineeringStatusDisplay(core::EngineeringStatus status)
{
    switch (status) {
    case core::EngineeringStatus::Feasible:              return "可行";
    case core::EngineeringStatus::EngineeringInfeasible: return "工程不可行";
    case core::EngineeringStatus::DataInsufficient:      return "数据不足";
    case core::EngineeringStatus::NotApplicable:         return "不适用";
    }
    return "";
}

/// 工程判定 token 反查显示名（HTML 状态列——状态维度取自矩阵单元格
/// 〔值单源〕，显示名由 core 词表 token 映射；未知 token 回退原样呈现，
/// 不伪造中文文案）。
/// RPT-T07 起正向/反向映射共享同一张表（src/RenderText.hpp——一致性
/// 检查器反查 status token 时与渲染正向不得失步，见该头文件头说明）；
/// 本包装保持 Render.cpp 内原有调用点拼写不变。
std::string engineeringStatusDisplayFromToken(std::string_view statusToken)
{
    return detail::statusTokenToDisplay(statusToken);
}

/// 结果完整性显示名（core::TaskOutcome 四值——状态事实呈现，非结论）。
/// RPT-T08 措辞冻结（RP-STATE-1，§6.2 行 5）：取消/失败/中断的结果"仅出现
/// 在诊断/状态呈现（'已取消/失败/已中断——不构成结论'）"——呈现文案钉住
/// "——不构成结论"后缀，杜绝该类结果在追溯附录等状态面被读成结论。
std::string_view taskOutcomeDisplay(core::TaskOutcome outcome)
{
    switch (outcome) {
    case core::TaskOutcome::Completed:   return "已完成";
    case core::TaskOutcome::Canceled:    return "已取消——不构成结论";
    case core::TaskOutcome::Failed:      return "已失败——不构成结论";
    case core::TaskOutcome::Interrupted: return "已中断——不构成结论";
    }
    return "";
}

/// 评估模式显示名（§8.1 表 1 效力分层——"筛选级/验证级"工程用语）。
std::string_view evaluationModeDisplay(core::EvaluationMode mode)
{
    switch (mode) {
    case core::EvaluationMode::Preview:  return "预览级";
    case core::EvaluationMode::Quick:    return "筛选级";
    case core::EvaluationMode::Verified: return "验证级";
    }
    return "";
}

/// 当前性呈现名（§6.3——Current/Superseded 两持久态＋不可判定计算形态；
/// "不可判定"是呈现词不是第三持久态，P-EV-4）。
std::string_view currentnessDisplay(const CurrentnessSnapshot& snapshot)
{
    if (!snapshot.status.has_value()) {
        return "不可判定";
    }
    return *snapshot.status == evidence::CurrentnessStatus::Current ? "当前" : "已被取代";
}

/// 矩阵当前性呈现 token（§8.4 FieldCell.currentness——"current"/
/// "superseded"/"unevaluable"；末者为不可判定计算形态的呈现 token，
/// 非第三持久态）。
std::string currentnessToken(const CurrentnessSnapshot& snapshot)
{
    if (!snapshot.status.has_value()) {
        return "unevaluable";
    }
    return *snapshot.status == evidence::CurrentnessStatus::Current ? "current" : "superseded";
}

/// 证据状态 token（evidence P-EV-8 五值实现承载词表的呈现侧 kebab 形——
/// evidence 头未公开 token 函数，词面按 evidence.md §6.2 表行原文；
/// evidence 冻结出 token 权威后按 DTB §5.4 增量同步，P-RPT-9）。
std::string_view evidenceStatusToken(evidence::EvidenceItemStatus status)
{
    switch (status) {
    case evidence::EvidenceItemStatus::Satisfied:     return "satisfied";
    case evidence::EvidenceItemStatus::Missing:       return "missing";
    case evidence::EvidenceItemStatus::Invalid:       return "invalid";
    case evidence::EvidenceItemStatus::Unverified:    return "unverified";
    case evidence::EvidenceItemStatus::NotApplicable: return "not-applicable";
    }
    return "";
}

/// 证据项类别 token（Common/Required/Suggested——evidence §6.1）。
std::string_view itemClassToken(evidence::EvidenceItemClass itemClass)
{
    switch (itemClass) {
    case evidence::EvidenceItemClass::Common:    return "common";
    case evidence::EvidenceItemClass::Required:  return "required";
    case evidence::EvidenceItemClass::Suggested: return "suggested";
    }
    return "";
}

/// 失效原因类别 token（evidence InvalidationKind 十值——词面按词表行
/// kebab 形；呈现于当前性原因清单）。
std::string_view invalidationKindToken(evidence::InvalidationKind kind)
{
    switch (kind) {
    case evidence::InvalidationKind::EvaluatorContractChanged: return "evaluator-contract-changed";
    case evidence::InvalidationKind::ObjectContentChanged:     return "object-content-changed";
    case evidence::InvalidationKind::ConfigurationChanged:     return "configuration-changed";
    case evidence::InvalidationKind::PolicyChanged:            return "policy-changed";
    case evidence::InvalidationKind::NameMapChanged:           return "name-map-changed";
    case evidence::InvalidationKind::SampleBaselineChanged:    return "sample-baseline-changed";
    case evidence::InvalidationKind::ConditionFlipped:         return "condition-flipped";
    case evidence::InvalidationKind::UpstreamResultChanged:    return "upstream-result-changed";
    case evidence::InvalidationKind::EnvironmentChanged:       return "environment-changed";
    case evidence::InvalidationKind::SliceContentChanged:      return "slice-content-changed";
    }
    return "";
}

/// 产出位置 token（evidence ProducerProcess——主/工作进程）。
std::string_view producerProcessToken(evidence::ProducerProcess producedIn)
{
    return producedIn == evidence::ProducerProcess::MainProcess ? "main-process" : "worker";
}

/// 量纲 token（core §4.4 十四类——呈现侧 kebab 形；JSON unitPreference 与
/// 追溯附录的量纲表达）。
std::string_view quantityKindToken(core::QuantityKind kind)
{
    switch (kind) {
    case core::QuantityKind::Length:              return "length";
    case core::QuantityKind::Angle:               return "angle";
    case core::QuantityKind::Mass:                return "mass";
    case core::QuantityKind::Time:                return "time";
    case core::QuantityKind::Force:               return "force";
    case core::QuantityKind::Torque:              return "torque";
    case core::QuantityKind::Inertia:             return "inertia";
    case core::QuantityKind::Power:               return "power";
    case core::QuantityKind::LinearVelocity:      return "linear-velocity";
    case core::QuantityKind::AngularVelocity:     return "angular-velocity";
    case core::QuantityKind::LinearAcceleration:  return "linear-acceleration";
    case core::QuantityKind::AngularAcceleration: return "angular-acceleration";
    case core::QuantityKind::Voltage:             return "voltage";
    case core::QuantityKind::Dimensionless:       return "dimensionless";
    }
    return "";
}

/// SourcedValue 四态 token（core §4.3 token 列原文——core 未公开 token
/// 函数，reporting 按 core.md §4.3 表行字面承载；core 冻结出公共 token
/// 面后增量切换，P-RPT-9）。
std::string_view fieldStateToken(core::FieldState state)
{
    switch (state) {
    case core::FieldState::Provided:      return "provided";
    case core::FieldState::NotProvided:   return "not-provided";
    case core::FieldState::NotApplicable: return "not-applicable";
    case core::FieldState::Invalid:       return "invalid";
    }
    return "";
}

/// 限定语呈现文案（§6.4 呈现义务列——数据驱动标记的固定文案；词面
/// 冻结、不可弱化省略〔RPT-05〕。RPT-T08 措辞冻结已在此表收口：逐 token
/// 文案为验收具名断言面，配合 assertWordingFreezeTemplateDiscipline 的
/// 词表完备性自检——任何 token 文案被删/置空＝模板面与 §6.4 词表不兼容，
/// 渲染即 TemplateVersion 拒绝〔§8.2④：模板不得移除限定语〕）。
std::string_view qualifierLabel(QualifierToken qualifier)
{
    switch (qualifier) {
    case QualifierToken::Estimated:                    return "估算";
    case QualifierToken::DataInsufficient:             return "数据不足";
    case QualifierToken::ExternalValidationIncomplete: return "外部验证未完成";
    case QualifierToken::DowngradedReferenceValue:     return "降级中——不可作为正式覆盖率结论";
    case QualifierToken::ScreeningOnly:                return "筛选级——不得单独支撑正式通过结论";
    case QualifierToken::HistoricalSuperseded:         return "输入已变化";
    case QualifierToken::NotApplicable:                return "—";
    case QualifierToken::Interrupted:                  return "已中断";
    case QualifierToken::Canceled:                     return "已取消";
    case QualifierToken::Failed:                       return "已失败";
    }
    return "";
}

/// SI 基线单位（core R1 注册表内各量纲的 SI 单位 token——tryConvert 的
/// "from" 面；表值与 core/src/Units.cpp R1 表逐项一致，注册表基线变化
/// 即 find 失败 → DataInvalid——P-RPT-9 同步面，不静默）。
std::optional<core::UnitToken> siUnitFor(core::QuantityKind kind)
{
    switch (kind) {
    case core::QuantityKind::Length:              return core::UnitToken::find("m");
    case core::QuantityKind::Angle:               return core::UnitToken::find("rad");
    case core::QuantityKind::Mass:                return core::UnitToken::find("kg");
    case core::QuantityKind::Time:                return core::UnitToken::find("s");
    case core::QuantityKind::Force:               return core::UnitToken::find("N");
    case core::QuantityKind::Torque:              return core::UnitToken::find("N*m");
    case core::QuantityKind::Inertia:             return core::UnitToken::find("kg*m^2");
    case core::QuantityKind::Power:               return core::UnitToken::find("W");
    case core::QuantityKind::LinearVelocity:      return core::UnitToken::find("m/s");
    case core::QuantityKind::AngularVelocity:     return core::UnitToken::find("rad/s");
    case core::QuantityKind::LinearAcceleration:  return core::UnitToken::find("m/s^2");
    case core::QuantityKind::AngularAcceleration: return core::UnitToken::find("rad/s^2");
    case core::QuantityKind::Voltage:             return core::UnitToken::find("V");
    case core::QuantityKind::Dimensionless:       return core::UnitToken::find("1");
    }
    return std::nullopt;
}

// =====================================================================
// 基础工具（确定性来源——无时钟/随机/locale 依赖）
// =====================================================================

/**
 * @brief 数值文本规范形（§8.1 总则——std::to_chars 最短往返表示）。
 *
 * to_chars 无精度格式重载给出"最短往返"表示（读回同值的最短字符序列，
 * locale 无关）——与 io canonical 数值口径一致（§8.3），是三格式数值
 * 逐字符一致的承载（AT-22"数值以文本规范形比对"）。
 *
 * @throws ReportError(DataInvalid) 非有限值（NaN/±Inf——§4.2 字段校验
 *         已拒，此处防御渲染面；绝不静默格式化）
 */
std::string toCharsShortest(double value)
{
    if (!std::isfinite(value)) {
        throw ReportError(ReportErrorCode::DataInvalid, "非有限数值不可渲染（NaN/±Inf）");
    }
    char buffer[64];   // double 最短往返表示上限远小于 64（≈24 字符）——容量安全
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc()) {
        // 缓冲不足在 64 字节下不可达；防御面保持显式失败而非截断。
        throw ReportError(ReportErrorCode::RenderFailed, "数值格式化失败（to_chars 缓冲）");
    }
    return std::string(buffer, result.ptr);
}

/**
 * @brief RFC3339 UTC 时间文本（秒精度——生成时间/评审时间等的呈现形）。
 *
 * 确定性：gmtime 定长格式化，无 locale 依赖；亚秒截断为呈现精度约定
 * （时间不入报告身份——§4.4，截断不影响任何身份语义）。
 */
std::string rfc3339Utc(std::chrono::system_clock::time_point when)
{
    const std::time_t t = std::chrono::system_clock::to_time_t(when);
    std::tm parts{};
#if defined(_WIN32)
    gmtime_s(&parts, &t);
#else
    gmtime_r(&t, &parts);
#endif
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
                  parts.tm_hour, parts.tm_min, parts.tm_sec);
    return buffer;
}

/**
 * @brief HTML 文本转义（&, <, >, ", ' 全量——正文与属性值共用本函数，
 *        防 HTML 注入；转义属 HTML 模板渲染职责，io 无 HTML 编码器，
 *        不构成"第二套编码器"——SA-12 约束面是 CSV/JSON canonical）。
 */
std::string htmlEscape(std::string_view raw)
{
    std::string out;
    out.reserve(raw.size());
    for (const char c : raw) {
        switch (c) {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '"':  out += "&quot;"; break;
        case '\'': out += "&#39;";  break;
        default:   out += c;        break;
        }
    }
    return out;
}

/**
 * @brief 身份缩略形（UX-02——正文工程用语：tag＋前 8 位 hex＋"…"）。
 *
 * 完整规范文本只出现在追溯区块/附录/JSON 机器镜像（§8.1）；正文表格用
 * 缩略形定位（§8.1 头块"修订 r-…〔seq〕"原文的缩略呈现）。
 */
std::string abbreviateId(const std::string& canonical)
{
    // 规范形 "tag-<hex>"：保留 tag 与连字符，hex 取前 8 位加省略号；
    // 非 "tag-hex" 形态（不可达——Id128 规范文本恒有 tag）原样返回。
    const auto dash = canonical.find('-');
    if (dash == std::string::npos || canonical.size() <= dash + 1 + 8) {
        return canonical;
    }
    return canonical.substr(0, dash + 1 + 8) + "…";
}

/// 摘要前 12 位小写 hex（§8.3 证据引用 "itemId@digest 前 12 hex"）。
std::string digestPrefix12(const core::Digest256& digest)
{
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(12);
    for (std::size_t i = 0; i < 6 && i < digest.size(); ++i) {
        const std::uint8_t b = digest[i];
        out += kHex[(b >> 4) & 0x0F];
        out += kHex[b & 0x0F];
    }
    return out;
}

/// 分号连接（CSV qualifier/evidence_ref/case_scope 多值列——§8.3）。
std::string joinSemicolon(const std::vector<std::string>& parts)
{
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out += ';';
        }
        out += parts[i];
    }
    return out;
}

// =====================================================================
// 渲染前置与矩阵同源验证（§9.3 前置行的执行面）
// =====================================================================

/**
 * @brief 渲染前置校验（§9.3：报告已冻结＋章节模型版本兼容）。
 *
 * @throws ReportError Usage（报告身份为零——非 make() 产出，调用方违约）
 *         / TemplateVersion（sectionModelVersion 与渲染器支持的
 *         kSectionModelVersion 不一致——§8.2④ 模板版本兼容纪律）
 */
void validateRenderPreconditions(const ReviewReport& report)
{
    if (!report.contentIdentity().isValid()) {
        throw ReportError(ReportErrorCode::Usage,
                          "渲染前置违约：报告未冻结（contentIdentity 为零——ReviewReport::make 产出即非零）");
    }
    if (report.sectionModelVersion() != std::string(kSectionModelVersion)) {
        throw ReportError(ReportErrorCode::TemplateVersion,
                          "章节模型版本不兼容：报告 " + report.sectionModelVersion()
                              + "，渲染器支持 " + std::string(kSectionModelVersion));
    }
}

/**
 * @brief 矩阵同源索引（§8.4 值单源的查找面＋双向完整性验证）。
 *
 * consume()：按 fieldKey 取单元格并标记消费——报告字段查不到单元格＝
 * 矩阵与报告不同源（DataInvalid）。assertFullyConsumed()：矩阵存在报告
 * 未引用的剩余单元格＝同判（反向不同源）。双向检查是"任何格式对同一
 * fieldKey 的输出只能来自同一 FieldCell"的结构性保证：值只可能来自
 * consume() 返回的唯一单元格。
 */
class MatrixIndex {
public:
    explicit MatrixIndex(const FieldMatrix& matrix)
    {
        for (std::size_t i = 0; i < matrix.size(); ++i) {
            const auto inserted = m_byKey.emplace(matrix[i].fieldKey, &matrix[i]);
            if (!inserted.second) {
                // fieldKey 全报告唯一（三段唯一性）——重复键＝矩阵非法。
                throw ReportError(ReportErrorCode::DataInvalid,
                                  "字段矩阵含重复 fieldKey：" + matrix[i].fieldKey);
            }
        }
    }

    /// 按键消费单元格（缺失＝矩阵/报告不同源——DataInvalid）。
    const FieldCell& consume(const std::string& fieldKey)
    {
        const auto it = m_byKey.find(fieldKey);
        if (it == m_byKey.end()) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "字段矩阵与报告不同源：报告字段无矩阵单元格 " + fieldKey);
        }
        m_consumed.insert(fieldKey);
        return *it->second;
    }

    /// 全消费断言（矩阵剩余单元格＝同判 DataInvalid）。
    void assertFullyConsumed() const
    {
        if (m_consumed.size() != m_byKey.size()) {
            for (const auto& [key, cell] : m_byKey) {
                (void)cell;
                if (m_consumed.find(key) == m_consumed.end()) {
                    throw ReportError(ReportErrorCode::DataInvalid,
                                      "字段矩阵与报告不同源：矩阵单元格无报告字段 " + key);
                }
            }
        }
    }

private:
    std::unordered_map<std::string, const FieldCell*> m_byKey;   ///< fieldKey → 单元格
    std::unordered_set<std::string> m_consumed;                  ///< 已消费键集
};

// =====================================================================
// 稳定排序原语（§8.1 总则——章节 order／条目 entryKey／证据 itemId／
// 诊断 orderKey）
// =====================================================================

/// 章节条目的 entryKey 稳定序下标（遍历序＝§8.4 冻结序的条目段；键唯一
/// ——稳定性在此是防御性的，不改变结果）。
std::vector<std::size_t> entryOrderIndices(const ReviewReportSection& section)
{
    std::vector<std::size_t> indices(section.entries.size());
    for (std::size_t i = 0; i < indices.size(); ++i) {
        indices[i] = i;
    }
    std::stable_sort(indices.begin(), indices.end(),
                     [&section](std::size_t a, std::size_t b) {
                         return section.entries[a].entryKey < section.entries[b].entryKey;
                     });
    return indices;
}

/// 报告级诊断引用的 orderKey 序（§8.1"诊断按 orderKey"——orderKey＝
/// 〔来源章节 §5.1 order、稳定码、局部名、来源运行、出现次数〕字典序；
/// 组合键全来自冻结字段，同报告同序）。
std::vector<const DiagRefEntry*> orderedDiagRefs(const std::vector<DiagRefEntry>& diagRefs)
{
    std::vector<const DiagRefEntry*> ordered;
    ordered.reserve(diagRefs.size());
    for (const DiagRefEntry& entry : diagRefs) {
        ordered.push_back(&entry);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const DiagRefEntry* a, const DiagRefEntry* b) {
                  // 章节序：词表外 sectionId 排词表后（0xFFFF 兜底——构建
                  // 校验保证 sourceSection 为词表内，防御面不静默归零）。
                  const auto orderA = trySectionOrder(a->sourceSection);
                  const auto orderB = trySectionOrder(b->sourceSection);
                  const std::uint16_t keyA = orderA.value_or(0xFFFF);
                  const std::uint16_t keyB = orderB.value_or(0xFFFF);
                  if (keyA != keyB)                              return keyA < keyB;
                  if (a->code != b->code)                        return a->code < b->code;
                  if (a->localName != b->localName)              return a->localName < b->localName;
                  const std::string runA = a->sourceRun.has_value() ? a->sourceRun->toCanonical() : "";
                  const std::string runB = b->sourceRun.has_value() ? b->sourceRun->toCanonical() : "";
                  if (runA != runB)                              return runA < runB;
                  return a->occurrences < b->occurrences;
              });
    return ordered;
}

/// 证据引用的 itemId 序（§8.1 总则"证据按 itemId"——itemId 唯一键，
/// sourceSection 为同 id 多来源条的次序键）。
std::vector<const EvidenceRefEntry*> orderedEvidenceRefs(const std::vector<EvidenceRefEntry>& refs)
{
    std::vector<const EvidenceRefEntry*> ordered;
    ordered.reserve(refs.size());
    for (const EvidenceRefEntry& entry : refs) {
        ordered.push_back(&entry);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const EvidenceRefEntry* a, const EvidenceRefEntry* b) {
                  if (a->itemId != b->itemId)            return a->itemId < b->itemId;
                  return a->sourceSection < b->sourceSection;
              });
    return ordered;
}

/// 报告级结果引用按 runId 规范文本序（三格式 resultRefs/附录呈现的稳定
/// 序——runId 唯一，规范文本字典序即字节序）。
std::vector<const ResultRefSnapshot*> orderedResultRefs(const std::vector<ResultRefSnapshot>& refs)
{
    std::vector<const ResultRefSnapshot*> ordered;
    ordered.reserve(refs.size());
    for (const ResultRefSnapshot& ref : refs) {
        ordered.push_back(&ref);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const ResultRefSnapshot* a, const ResultRefSnapshot* b) {
                  return a->runId.toCanonical() < b->runId.toCanonical();
              });
    return ordered;
}

// =====================================================================
// 措辞冻结规则层（§8.2①硬编码纪律＋§6.4/§6.5 呈现义务锁——RPT-T08 交付）
// =====================================================================

/**
 * @brief 覆盖完备性判定（EVI-02/§6.5——五态计数的呈现侧推导）。
 *
 * "漏验任一启用必验工况"的计数语义（evidence CaseCoverageMatrix 五态的
 * 呈现投影，§6.5）：未执行/执行无效/执行失败任一非零即为漏验；已执行与
 * 不适用之和不足必验总数同样判漏（计数间自洽的兜底——正常计数下与前一
 * 条等价）。覆盖判定的权威在 evidence（汇总门禁②级——PA-1），本函数只
 * 服务于呈现与渲染器前置断言（不构成第二套资格判定——§2.1 C-2）。
 *
 * @param coverage [in] 报告级覆盖摘要（§4.2 coverageSummary——冻结值）
 * @return true＝存在漏验（呈现"漏验清单"分支——§6.5）；false＝无漏验
 *         （含 totalRequired==0 的"无启用必验工况"形态——P-EV-7 的呈现
 *         侧如实表达，判定语义仍归 evidence）
 */
bool coverageIncomplete(const CaseCoverageSummary& coverage) noexcept
{
    return coverage.notExecuted > 0 || coverage.invalid > 0 || coverage.failed > 0
           || coverage.executed + coverage.notApplicable != coverage.totalRequired;
}

/**
 * @brief 覆盖率参考值是否处于降级状态（P-EV-5 报告侧数据源探测）。
 *
 * downgraded-reference-value 限定语（§6.4 行 4）的触发数据源在 evidence
 * 侧为 RegionCoverageEvidence.downgraded==true（KIN-04）；该事实进入报告
 * 模型的投影＝diagRefs 携带 evidence 建议码 EVI-REGION-COVERAGE-DOWNGRADED
 * （EV-T06 ④级降级裁定随诊断出账——Verdict.hpp kDiagRegionCoverageDowngraded，
 * 常量单一定义不私抄字面）。条目级关联键仍未在 §4 报告模型冻结（v0.9
 * 登记），故本限定语的呈现义务锁定在**报告级覆盖呈现面**（覆盖摘要块），
 * 不全局伪标注到条目单元格（ERR-01）。
 *
 * @param report [in] 冻结报告（只读）
 * @return true＝diagRefs 携带降级码——覆盖呈现面必须携带降级限定语
 */
bool hasDowngradedCoverageReference(const ReviewReport& report)
{
    for (const DiagRefEntry& diag : report.diagRefs()) {
        if (diag.code == evidence::kDiagRegionCoverageDowngraded) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 声明资格与报告事实的前置一致性断言（§8.2①硬编码纪律的执行面；
 *        三格式渲染器在装配前统一调用——数据一致性与格式无关）。
 *
 * §8.2①原文："渲染器对字段做前置断言——违反即 DataInvalid 构造失败，
 * 不存在'渲染了再说'路径"。资格的**权威计算**归 evidence 纯检查（§6.2
 * 不自算），本断言不重算资格，只核对已冻结数据之间的**内部一致性**——
 * 构建器产出的报告经 evidence 检查本应自洽，此处的价值是把手造/上游
 * 异常数据在渲染边界拦下（与单位冻结集闸同性质的防御面，RP-STATE-1
 * "渲染器前置断言生效"的具名验收面）。五路断言：
 *
 * ①非 Completed 结果（Canceled/Failed/Interrupted——TASK-02 表 3 该行
 *   engineeringStatus==NotApplicable、不得含正式结论字段）永不携带任何
 *   声明资格——失败/取消/中断不入正式结论（RP-STATE-1）；
 * ②formalPass 资格 ⇒ Verified ∧ Completed ∧ Feasible（RPT-05 五条件中
 *   报告可见的三条件；Quick 永不满足 FormalPass——§8.1 表 1 模式效力，
 *   acceptance 4）；
 * ③reviewRecord 资格 ⇒ Completed ∧ EngineeringInfeasible（§6.2 行 2——
 *   评审记录声明只随不可行结果出现）；
 * ④章节资格声明 ⇔ 所引结果资格的 AND 聚合（§8.2①"字样仅当条目所属结果
 *   的 eligibility.formalPass==true 才可输出"——构建器聚合不变量的渲染侧
 *   复核；两类声明独立核对、互不推导）；
 * ⑤覆盖漏验 ⇒ 任何章节不得声明 formalPass（EVI-02/§8.1 表 2②呈现口径：
 *   漏验任一启用必验工况整体 DataInsufficient，不得输出正式通过）。
 *
 * @throws ReportError DataInvalid 任一断言不成立（detail 含定位与断言号）
 */
void validateDeclarationConsistency(const ReviewReport& report)
{
    // 断言①②③：逐结果资格快照与结果事实的内部一致性（§4.3.1 三轴字段
    // ——outcome/engineeringStatus/mode 为冻结事实，资格为 evidence 快照）。
    for (const ResultRefSnapshot& ref : report.resultRefs()) {
        const std::string runKey = ref.runId.toCanonical();
        if (ref.outcome != core::TaskOutcome::Completed
            && (ref.eligibility.formalPass || ref.eligibility.reviewRecord)) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "§8.2①断言①违约：非 Completed 结果携带声明资格（失败/取消/"
                              "中断不入正式结论——RP-STATE-1/TASK-02）：" + runKey);
        }
        if (ref.eligibility.formalPass
            && (ref.mode != core::EvaluationMode::Verified
                || ref.outcome != core::TaskOutcome::Completed
                || ref.engineeringStatus != core::EngineeringStatus::Feasible)) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "§8.2①断言②违约：formalPass 资格要求 Verified∧Completed∧"
                              "Feasible（RPT-05 五条件；Quick 永不满足 FormalPass——表 1）"
                              "：" + runKey);
        }
        if (ref.eligibility.reviewRecord
            && (ref.outcome != core::TaskOutcome::Completed
                || ref.engineeringStatus != core::EngineeringStatus::EngineeringInfeasible)) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "§8.2①断言③违约：reviewRecord 资格要求 Completed∧"
                              "EngineeringInfeasible（§6.2 行 2）：" + runKey);
        }
    }

    // 断言④：章节声明 ⇔ 所引结果资格 AND 聚合（§4.3.4 EligibilityNote
    // 聚合语义的渲染侧复核；sourceResults ⊆ resultRefs 由 make 字段校验
    // 保证，防御面查不到即不同源拒绝）。
    for (const ReviewReportSection& section : report.sections()) {
        if (!section.eligibilityNote.has_value()) {
            continue;   // 无资格说明的章节无声明面——跳过
        }
        for (const core::RunId& run : section.sourceResults) {
            const std::string runKey = run.toCanonical();
            const ResultRefSnapshot* ref = nullptr;
            for (const ResultRefSnapshot& candidate : report.resultRefs()) {
                if (candidate.runId.toCanonical() == runKey) {
                    ref = &candidate;
                    break;
                }
            }
            if (ref == nullptr) {
                throw ReportError(ReportErrorCode::DataInvalid,
                                  "§8.2①断言④违约：章节所引结果不在 resultRefs（不同源）："
                                      + section.sectionId + " <- " + runKey);
            }
            if (section.eligibilityNote->formalPassAllowed && !ref->eligibility.formalPass) {
                throw ReportError(ReportErrorCode::DataInvalid,
                                  "§8.2①断言④违约：章节声明 formalPass 但所引结果资格"
                                  "不成立（§8.2①'条目所属结果'复核）：" + section.sectionId
                                      + " <- " + runKey);
            }
            if (section.eligibilityNote->reviewRecordAllowed && !ref->eligibility.reviewRecord) {
                throw ReportError(ReportErrorCode::DataInvalid,
                                  "§8.2①断言④违约：章节声明评审记录但所引结果资格不成立"
                                  "（两声明独立核对——§6.2）：" + section.sectionId
                                      + " <- " + runKey);
            }
        }
    }

    // 断言⑤：覆盖漏验 ⇒ 任何章节不得声明 formalPass（EVI-02——"正式计算
    // 与正式判定必须覆盖全部启用的必验工况，不得漏验"；报告可见的覆盖
    // 计数与声明并存即数据矛盾，渲染边界拒绝——§8.1 表 2②呈现口径）。
    if (coverageIncomplete(report.coverageSummary())) {
        for (const ReviewReportSection& section : report.sections()) {
            if (section.eligibilityNote.has_value()
                && section.eligibilityNote->formalPassAllowed) {
                throw ReportError(ReportErrorCode::DataInvalid,
                                  "§8.2①断言⑤违约：漏验启用必验工况时章节声明 formalPass"
                                  "（EVI-02/§8.1 表 2②——不得输出正式通过）："
                                      + section.sectionId);
            }
        }
    }
}

/**
 * @brief 措辞冻结的模板纪律自检（§8.2④——"模板不得移除限定语"的执行面）。
 *
 * §8.2④原文："模板版本升级若删限定语＝版本不兼容拒绝——TemplateVersion
 * 错误"。本自检把该纪律机械化：迭代 §6.4 词表全 10 个 token，任一呈现
 * 文案缺位（表项被删/置空）＝模板面与措辞冻结词表不兼容——以 TemplateVersion
 * 拒绝（不静默输出残缺限定语）；同时钉住三格式模板版本下限（限定语结构性
 * 输出自模板 v1 起为强制面，kHtmlTemplateVersion/kJsonTemplateVersion/
 * kCsvTemplateVersion 不允许降到该下限之下）。当前表完整时本检查恒通过
 * （防御性不变量——词表演化时的失败报警器，与 MatrixIndex 重复键拒绝同
 * 性质）；逐 token 结构性输出的正向验收面由单元测试具名锁定。
 *
 * @throws ReportError TemplateVersion 词表文案缺位或模板版本低于强制下限
 */
void assertWordingFreezeTemplateDiscipline()
{
    // §6.4 词表全 10 token（ReportModel.hpp QualifierToken 枚举序——表行序）。
    static constexpr QualifierToken kAllQualifierTokens[] = {
        QualifierToken::Estimated,
        QualifierToken::DataInsufficient,
        QualifierToken::ExternalValidationIncomplete,
        QualifierToken::DowngradedReferenceValue,
        QualifierToken::ScreeningOnly,
        QualifierToken::HistoricalSuperseded,
        QualifierToken::NotApplicable,
        QualifierToken::Interrupted,
        QualifierToken::Canceled,
        QualifierToken::Failed,
    };
    for (const QualifierToken qualifier : kAllQualifierTokens) {
        if (qualifierLabel(qualifier).empty()) {
            throw ReportError(ReportErrorCode::TemplateVersion,
                              "§8.2④模板纪律违约：§6.4 限定语词表存在缺位呈现文案（模板"
                              "不得移除限定语——版本不兼容拒绝）");
        }
    }
    // 模板版本下限（限定语结构性输出强制面——v1 起；低于下限＝版本不兼容）。
    constexpr std::uint32_t kQualifierMandatoryTemplateFloor = 1;
    if (kHtmlTemplateVersion < kQualifierMandatoryTemplateFloor
        || kJsonTemplateVersion < kQualifierMandatoryTemplateFloor
        || kCsvTemplateVersion < kQualifierMandatoryTemplateFloor) {
        throw ReportError(ReportErrorCode::TemplateVersion,
                          "§8.2④模板纪律违约：模板版本低于限定语强制面下限（版本不兼容"
                          "拒绝）");
    }
}

// =====================================================================
// 脱敏（§4.3.3 双保险渲染侧——NFR-SEC-07）
// =====================================================================

/**
 * @brief 自由文本脱敏（User 档——报告类直接消费方口径，见 diagnostics
 *        Redaction.hpp Tier 语义）。
 *
 * 脱敏服务绝不抛出（内部失败整条降级 [REDACTED:redaction-failed]）——
 * 宁可信息缺失不泄露（diagnostics §7.3② 铁律），渲染侧直接消费其输出。
 */
std::string redactText(const diagnostics::IRedactionService& redaction, std::string_view raw)
{
    return redaction.redact(raw, diagnostics::LogTier::User);
}

// =====================================================================
// 数值/单位投影（KIN-12——core 唯一换算入口）
// =====================================================================

/**
 * @brief 字段值投影（§8.4 valueRepr 的计算——提取期一次性，三格式共享）。
 *
 * 规则（§8.3/§8.1/RP-CONS-4 逐条）：
 *   - 文本字段 → 文本原样（呈现即内容）；
 *   - 数值 Provided → 显示单位换算（core tryConvert；field.unit 缺席＝
 *     无量纲，SI 值直出）后 to_chars；
 *   - NotProvided → "未提供"、NotApplicable → "不适用"（§8.3 值列规则）；
 *   - Invalid → 保留原文（NFR-COR-03——不静默转 0，脱敏不适用于本态）；
 *   - 曲线类（renderHint==CurveRef 且无内联值）→ 引用文本
 *     "run-<hex>#<fieldPath>"（§8.1 复杂结构以引用呈现——不复制大数组）。
 *
 * 单位一致性闸：field.unit 与报告级 UnitPreference 冻结集同量纲条目
 * 不一致 → DataInvalid（unitPreference 冻结入身份的执行侧——KIN-12
 * 显示单位纯投影的投影源唯一性）。
 *
 * @throws ReportError DataInvalid（单位冻结集冲突／SI 基线缺位／换算
 *         非有限——均为报告数据或上游基线问题，非渲染层故障）
 */
struct ValueProjection {
    std::string text;                    ///< 值文本规范形（valueRepr）
    std::optional<std::string> unit;     ///< 显示单位符号（无量纲缺席）
};

ValueProjection projectFieldValue(const ReviewReport& report, const ReviewReportSection& section,
                                  const SectionEntryView& entry, const FieldValue& field)
{
    // 曲线类复杂结构：CurveRef 提示章节中无内联值（无数值无文本）的字段
    // 以结果绑定引用呈现——runId＋fieldPath 即 §8.1 的"ResultBinding 的
    // runId＋fieldPath＋JSON 路径"三要素（fieldPath 为 envelope 载荷内
    // JSON 路径形态，fieldKey 为报告侧对齐键）。
    if (section.renderHint == RenderHint::CurveRef && !field.text.has_value()
        && field.quantity.state() != core::FieldState::Provided) {
        return {entry.result.runId.toCanonical() + "#" + entry.result.fieldPath, std::nullopt};
    }

    // 纯文本字段（与 Provided 数值互斥——字段校验原语保证）。
    if (field.text.has_value()) {
        return {*field.text, std::nullopt};
    }

    switch (field.quantity.state()) {
    case core::FieldState::Provided: {
        const double siValue = field.quantity.tryValue().value();
        if (!field.unit.has_value()) {
            // 无量纲字段（§4.3 单位可选行——单位缺席即无量纲计数）。
            return {toCharsShortest(siValue), std::nullopt};
        }
        // 单位冻结集一致性（同量纲条目 token 必须与 UnitPreference 一致；
        // 冻结集无该量纲条目＝SI 显示，字段自带显示单位亦合法——提供方
        // 按 §9.2 请求上下文解析后的投影值）。
        const auto preferred = report.unitPreference().displayUnits.find(field.unit->kind());
        if (preferred != report.unitPreference().displayUnits.end()
            && !(preferred->second == *field.unit)) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "字段显示单位与报告级 UnitPreference 冻结集不一致：字段 "
                                  + field.key + " 携带 " + std::string(field.unit->symbol()));
        }
        const auto siUnit = siUnitFor(field.unit->kind());
        if (!siUnit.has_value() || !siUnit->isValid()) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "SI 基线单位缺位（core R1 注册表基线变化——P-RPT-9 同步面）");
        }
        const auto display = core::tryConvert(siValue, *siUnit, *field.unit);
        if (!display.has_value()) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "显示单位换算非有限/溢出：字段 " + field.key);
        }
        return {toCharsShortest(*display), std::string(field.unit->symbol())};
    }
    case core::FieldState::NotProvided:
        return {std::string("未提供"), std::nullopt};     // §8.3 值列规则
    case core::FieldState::NotApplicable:
        return {std::string("不适用"), std::nullopt};     // ERR-01 显式标记
    case core::FieldState::Invalid:
        break;   // 循环外统一取原文（switch 全枚举形态）
    }
    return {field.quantity.invalidRawInput(), std::nullopt};   // NFR-COR-03 保留原文
}

/**
 * @brief 比较型诊断单侧值投影（diagnostics.csv/HTML 三要素列——与字段
 *        投影同规则：换算经 core、四态呈现词、Invalid 保留原文）。
 */
ValueProjection projectComparativeValue(const core::ComparativeValue& value)
{
    if (value.quantity.state() == core::FieldState::Provided) {
        const double siValue = value.quantity.tryValue().value();
        const auto siUnit = siUnitFor(value.unit.kind());
        if (!siUnit.has_value() || !siUnit->isValid()) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "SI 基线单位缺位（比较型诊断单位投影）");
        }
        const auto display = core::tryConvert(siValue, *siUnit, value.unit);
        if (!display.has_value()) {
            throw ReportError(ReportErrorCode::DataInvalid, "比较型诊断显示换算非有限/溢出");
        }
        return {toCharsShortest(*display), std::string(value.unit.symbol())};
    }
    if (value.quantity.state() == core::FieldState::NotProvided) {
        return {std::string("未提供"), std::nullopt};
    }
    if (value.quantity.state() == core::FieldState::NotApplicable) {
        return {std::string("不适用"), std::nullopt};
    }
    return {value.quantity.invalidRawInput(), std::nullopt};
}

// =====================================================================
// 限定语推导（§6.4——提取期一次性、固定推导序、去重；RPT-T08 在此
// 词面集合上收口措辞与具名用例）
// =====================================================================

void appendQualifier(std::vector<QualifierToken>& out, QualifierToken token)
{
    // 固定推导序＋去重（data-insufficient 可能由多条数据源触发——一词
    // 一位，不重复标注）。
    if (std::find(out.begin(), out.end(), token) == out.end()) {
        out.push_back(token);
    }
}

/**
 * @brief 条目字段限定语推导（§6.4 触发数据源列中已入 §4 报告模型者；
 *        推导序＝§6.4 表行序——确定性组成部分）。
 *
 * RPT-T08 词表锁定（acceptance 3——逐 token 触发数据源与呈现义务）：
 * 本函数承载 §6.4 词表 8 项中可入单元格的 8 个 token（estimated/
 * data-insufficient/external-validation-incomplete 之外的全部条目级触发
 * ＋not-applicable＋outcome 三态）；external-validation-incomplete 与
 * downgraded-reference-value 的**条目级**关联键（外部资源↔条目、
 * RegionCoverageEvidence.downgraded↔参考值字段）未在 §4 报告模型冻结
 * ——不在单元格上伪标注（ERR-01），其**报告级**呈现义务由渲染器的
 * 外部验证边界章（Recorded 态限定语）与覆盖摘要块（EVI-REGION-COVERAGE-
 * DOWNGRADED 诊断码——P-EV-5 承接）承接（v0.9 登记的条目级关联键收口
 * 安排不变，随框架章节数据源任务落地）。
 */
std::vector<QualifierToken> qualifiersFor(const ReviewReportSection& section,
                                          const SectionEntryView& entry,
                                          const FieldValue& field,
                                          const ResultRefSnapshot& bound)
{
    std::vector<QualifierToken> out;

    // ①not-applicable：字段四态显式标记（"—"＋原因——不伪造数值）。
    if (field.quantity.state() == core::FieldState::NotApplicable) {
        appendQualifier(out, QualifierToken::NotApplicable);
    }
    // ②estimated：字段来源为几何估算（ValueProvenance.kind——§6.4 行 1）。
    if (field.quantity.state() == core::FieldState::Provided
        && field.quantity.provenance().kind == core::ProvenanceKind::GeometricEstimate) {
        appendQualifier(out, QualifierToken::Estimated);
    }
    // ③data-insufficient（证据状态源）：绑定证据 Missing/Invalid/Unverified
    //   ——不满足态跟随结论出现（§6.4 行 2）。
    for (const EvidenceBinding& binding : entry.evidence) {
        if (binding.status == evidence::EvidenceItemStatus::Missing
            || binding.status == evidence::EvidenceItemStatus::Invalid
            || binding.status == evidence::EvidenceItemStatus::Unverified) {
            appendQualifier(out, QualifierToken::DataInsufficient);
            break;   // 任一不满足即触发——一词一位
        }
    }
    // ④data-insufficient（工程判定源）：绑定结果汇总判定。
    if (bound.engineeringStatus == core::EngineeringStatus::DataInsufficient) {
        appendQualifier(out, QualifierToken::DataInsufficient);
    }
    // ⑤data-insufficient（章节状态源）：§5.3"选中＋缺失项全量清单＋数据
    //   不足限定语"——章节级状态跟随数值与结论出现（§8.2④）。
    if (section.status == SectionStatus::DataInsufficient) {
        appendQualifier(out, QualifierToken::DataInsufficient);
    }
    // ⑥screening-only：Quick 模式——"不得单独支撑正式通过"（§8.1 表 1）。
    if (bound.mode == core::EvaluationMode::Quick) {
        appendQualifier(out, QualifierToken::ScreeningOnly);
    }
    // ⑦historical-superseded：当前性 Superseded——"输入已变化"标记。
    if (bound.currentness.status.has_value()
        && *bound.currentness.status == evidence::CurrentnessStatus::Superseded) {
        appendQualifier(out, QualifierToken::HistoricalSuperseded);
    }
    // ⑧outcome 状态事实（§6.4 行 8——interrupted/canceled/failed；构建
    //   校验保证结论条目只绑 Completed 结果，本推导为词表完备性承载）。
    switch (bound.outcome) {
    case core::TaskOutcome::Completed: break;
    case core::TaskOutcome::Interrupted: appendQualifier(out, QualifierToken::Interrupted); break;
    case core::TaskOutcome::Canceled:    appendQualifier(out, QualifierToken::Canceled);    break;
    case core::TaskOutcome::Failed:      appendQualifier(out, QualifierToken::Failed);      break;
    }
    return out;
}

/// 证据引用规范文本集（§8.3"itemId@digest 前 12 hex"——无摘要绑定
/// （Missing 等无凭据态）为裸 itemId；itemId 序去重）。
std::vector<std::string> evidenceRefTexts(const SectionEntryView& entry)
{
    std::vector<const EvidenceBinding*> ordered;
    ordered.reserve(entry.evidence.size());
    for (const EvidenceBinding& binding : entry.evidence) {
        ordered.push_back(&binding);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const EvidenceBinding* a, const EvidenceBinding* b) {
                  return a->itemId < b->itemId;
              });
    std::vector<std::string> refs;
    for (const EvidenceBinding* binding : ordered) {
        std::string ref = binding->itemId;
        if (binding->digest.has_value()) {
            ref += '@';
            ref += digestPrefix12(*binding->digest);
        }
        if (std::find(refs.begin(), refs.end(), ref) == refs.end()) {
            refs.push_back(std::move(ref));
        }
    }
    return refs;
}

}  // namespace

// =====================================================================
// 公共面实现：格式 token／字段矩阵提取
// =====================================================================

std::string_view token(ReportRenderFormat format) noexcept
{
    switch (format) {
    case ReportRenderFormat::Html: return "html";
    case ReportRenderFormat::Json: return "json";
    case ReportRenderFormat::Csv:  return "csv";
    }
    return "";
}

FieldMatrix extractFieldMatrix(const ReviewReport& report)
{
    // 前置：报告已冻结（make() 产出即保证——防御绕过构造的调用方违约）。
    if (!report.contentIdentity().isValid()) {
        throw ReportError(ReportErrorCode::Usage,
                          "字段矩阵提取前置违约：报告未冻结（contentIdentity 为零）");
    }

    // 结果引用索引（runId 规范文本 → 引用；绑定越界即报告数据非法——
    // 构建校验已保证 sourceResults ⊆ resultRefs，防御面保持显式失败）。
    std::map<std::string, const ResultRefSnapshot*> resultIndex;
    for (const ResultRefSnapshot& ref : report.resultRefs()) {
        resultIndex.emplace(ref.runId.toCanonical(), &ref);
    }

    FieldMatrix matrix;
    // 遍历序（§8.4 冻结序）：章节按存储序（构建校验保证 order 严格递增
    // ——存储序即 §5 order 序），条目按 entryKey 稳定序，字段按存储序。
    for (const ReviewReportSection& section : report.sections()) {
        for (const std::size_t entryIndex : entryOrderIndices(section)) {
            const SectionEntryView& entry = section.entries[entryIndex];
            const auto boundIt = resultIndex.find(entry.result.runId.toCanonical());
            if (boundIt == resultIndex.end()) {
                throw ReportError(ReportErrorCode::DataInvalid,
                                  "条目结果绑定越界（resultRefs 无该运行）："
                                      + entry.result.runId.toCanonical());
            }
            const ResultRefSnapshot& bound = *boundIt->second;

            // 字段键条目内唯一性防御（§9.2 fields 键唯一——构建校验保证，
            // 重复键会使矩阵 fieldKey 碰撞，显式拒绝不静默）。
            std::unordered_set<std::string> fieldKeys;
            for (const FieldValue& field : entry.fields) {
                if (!fieldKeys.insert(field.key).second) {
                    throw ReportError(ReportErrorCode::DataInvalid,
                                      "条目字段键重复：" + section.sectionId + "."
                                          + entry.entryKey + "." + field.key);
                }

                FieldCell cell;
                cell.fieldKey = section.sectionId + "." + entry.entryKey + "." + field.key;
                cell.sectionId = section.sectionId;
                cell.entryKey = entry.entryKey;
                const ValueProjection projection = projectFieldValue(report, section, entry, field);
                cell.valueRepr = projection.text;
                cell.unit = projection.unit;
                // status 维度＝绑定结果工程判定 token（§6.6 结论列——AT-22
                // 七元组的 status；core toToken 同码同串）。
                cell.status = std::string(core::toToken(bound.engineeringStatus));
                cell.qualifier = qualifiersFor(section, entry, field, bound);
                cell.resultRef = bound.runId.toCanonical();
                cell.evidenceRef = evidenceRefTexts(entry);
                for (const core::ObjectId& caseId : entry.caseScope) {
                    cell.caseScope.push_back(caseId.toCanonical());
                }
                cell.currentness = currentnessToken(bound.currentness);
                matrix.push_back(std::move(cell));
            }
        }
    }
    return matrix;
}

// =====================================================================
// 公共面实现：CSV 单元格／JSON DOM／内存目标
// =====================================================================

ReportCsvCell ReportCsvCell::textCell(std::string value)
{
    ReportCsvCell cell;
    cell.kind = Kind::Text;
    cell.text = std::move(value);
    return cell;
}

ReportCsvCell ReportCsvCell::integerCell(std::int64_t value)
{
    ReportCsvCell cell;
    cell.kind = Kind::Integer;
    cell.integer = value;
    return cell;
}

ReportCsvCell ReportCsvCell::numberCell(double value)
{
    if (!std::isfinite(value)) {
        // NaN/±Inf 不得进入 CSV 数值通道（io to_chars 面同样拒绝——双闸
        // 中 reporting 侧先行，错误语义与字段校验一致）。
        throw ReportError(ReportErrorCode::DataInvalid, "CSV 数值单元格拒绝非有限值");
    }
    ReportCsvCell cell;
    cell.kind = Kind::Number;
    cell.number = value;
    return cell;
}

ReportJsonDom ReportJsonDom::nullValue()
{
    return ReportJsonDom{};   // 默认即 Null
}

ReportJsonDom ReportJsonDom::boolean(bool value)
{
    ReportJsonDom dom;
    dom.m_type = Type::Bool;
    dom.m_bool = value;
    return dom;
}

ReportJsonDom ReportJsonDom::number(double value)
{
    if (!std::isfinite(value)) {
        // JSON 无 NaN/±Inf 字面——canonical 写出前在 DOM 边界拒绝。
        throw ReportError(ReportErrorCode::DataInvalid, "JSON 数值拒绝非有限值（NaN/±Inf）");
    }
    ReportJsonDom dom;
    dom.m_type = Type::Number;
    dom.m_number = value;
    return dom;
}

ReportJsonDom ReportJsonDom::string(std::string value)
{
    ReportJsonDom dom;
    dom.m_type = Type::String;
    dom.m_string = std::move(value);
    return dom;
}

ReportJsonDom ReportJsonDom::array(std::vector<ReportJsonDom> items)
{
    ReportJsonDom dom;
    dom.m_type = Type::Array;
    dom.m_array = std::move(items);
    return dom;
}

ReportJsonDom ReportJsonDom::object(std::vector<std::pair<std::string, ReportJsonDom>> members)
{
    // 对象键唯一性（DOM 边界纪律——canonical JSON 对象键重复即文档二义，
    // 装配期拒绝而非写出生成非法文档）。
    std::unordered_set<std::string> seen;
    seen.reserve(members.size());
    for (const auto& [key, value] : members) {
        (void)value;
        if (!seen.insert(key).second) {
            throw ReportError(ReportErrorCode::DataInvalid, "JSON 对象重复键：" + key);
        }
    }
    ReportJsonDom dom;
    dom.m_type = Type::Object;
    dom.m_object = std::move(members);
    return dom;
}

bool ReportJsonDom::booleanValue() const
{
    if (m_type != Type::Bool) {
        throw ReportError(ReportErrorCode::Usage, "JSON DOM 布尔载荷前置违约（type 非 Bool）");
    }
    return m_bool;
}

double ReportJsonDom::numberValue() const
{
    if (m_type != Type::Number) {
        throw ReportError(ReportErrorCode::Usage, "JSON DOM 数值载荷前置违约（type 非 Number）");
    }
    return m_number;
}

const std::string& ReportJsonDom::stringValue() const
{
    if (m_type != Type::String) {
        throw ReportError(ReportErrorCode::Usage, "JSON DOM 字符串载荷前置违约（type 非 String）");
    }
    return m_string;
}

const std::vector<ReportJsonDom>& ReportJsonDom::arrayItems() const
{
    if (m_type != Type::Array) {
        throw ReportError(ReportErrorCode::Usage, "JSON DOM 数组载荷前置违约（type 非 Array）");
    }
    return m_array;
}

const std::vector<std::pair<std::string, ReportJsonDom>>& ReportJsonDom::objectMembers() const
{
    if (m_type != Type::Object) {
        throw ReportError(ReportErrorCode::Usage, "JSON DOM 对象载荷前置违约（type 非 Object）");
    }
    return m_object;
}

MemoryReportOutputTarget::MemoryReportOutputTarget(std::vector<std::uint8_t>* buffer) noexcept
    : m_buffer(buffer)
{
}

bool MemoryReportOutputTarget::write(const std::uint8_t* bytes, std::size_t n)
{
    if (m_aborted) {
        return false;   // 放弃后拒绝再写——防半途产物混入
    }
    if (n > 0) {
        m_buffer->insert(m_buffer->end(), bytes, bytes + n);
    }
    return true;
}

bool MemoryReportOutputTarget::commit()
{
    // 内存态无原子替换语义（§9.3 副作用行——落盘归导出服务经 io 原子
    // 协议）；提交＝确认缓冲内容即为产物。
    return !m_aborted;
}

void MemoryReportOutputTarget::abort()
{
    m_aborted = true;   // 缓冲属主（渲染器）据语义丢弃/保留——目标只标失效
}

// =====================================================================
// HTML 渲染器（§8.1）
// =====================================================================

namespace {

/**
 * @brief HTML 渲染上下文（一次 render() 的局部装配态——缓冲＋矩阵索引
 *        ＋脱敏引用；无共享可变状态，并发安全的载体形态）。
 */
struct HtmlRenderContext {
    const ReviewReport& report;
    MatrixIndex& matrix;
    const diagnostics::IRedactionService& redaction;
    std::string out;   ///< 文档缓冲（LF 行尾——§8.1 行尾纪律）

    /// 脱敏后追加重量（人读自由文本专用——见 Render.cpp 文件头范围表）。
    void appendRedacted(std::string_view raw) { out += htmlEscape(redactText(redaction, raw)); }
    void appendEscaped(std::string_view raw) { out += htmlEscape(raw); }
};

/// HTML 起手（doctype/head/内联样式——零外部依赖零脚本：无外链、无
/// <script>、无 <link>；样式内联满足离线打开一致）。
void beginHtmlDocument(HtmlRenderContext& ctx, const std::string& title)
{
    ctx.out += "<!DOCTYPE html>\n<html lang=\"zh-CN\">\n<head>\n";
    ctx.out += "<meta charset=\"utf-8\">\n";
    ctx.out += "<title>" + htmlEscape(title) + "</title>\n";
    // 内联样式（§8.1"样式内联"——离线一致；无任何外链资源）。
    ctx.out +=
        "<style>\n"
        "body{font-family:sans-serif;margin:2em auto;max-width:64em;}\n"
        "table{border-collapse:collapse;margin:.5em 0;}\n"
        "th,td{border:1px solid #999;padding:.2em .5em;text-align:left;vertical-align:top;}\n"
        ".badge{display:inline-block;border:1px solid #666;border-radius:.2em;"
        "padding:0 .4em;margin-left:.5em;font-size:.85em;}\n"
        ".qualifier{background:#ffec99;border:1px solid #caa53d;border-radius:.2em;"
        "padding:0 .3em;margin-left:.3em;font-size:.85em;}\n"
        ".merge-note{color:#555;border:1px dotted #999;border-radius:.2em;"
        "padding:0 .3em;margin-left:.3em;font-size:.85em;}\n"
        ".trace-block{border:1px dashed #999;padding:.5em;margin:.5em 0;}\n"
        ".unit{color:#555;}\n"
        ".currentness{color:#8a6d3b;}\n"
        "nav#toc a{display:block;}\n"
        "</style>\n</head>\n<body>\n";
}

/// 报告头块（§8.1：标题/级别/数据源〔缩略形＋seq〕/生成时间/生成者/
/// 版本/取代链——正文工程用语；追溯区块单独成块承载完整身份规范文本）。
void appendReportHeader(HtmlRenderContext& ctx)
{
    const ReviewReport& report = ctx.report;
    const bool levelC = report.level() == ReportLevel::C;
    const std::string levelName = levelC ? "正式评审级报告" : "基础级报告";

    ctx.out += "<h1>评审报告（" + htmlEscape(levelName) + "）</h1>\n";
    ctx.out += "<table id=\"report-header\">\n";
    ctx.out += "<tr><th>级别</th><td>" + htmlEscape(levelName) + "</td></tr>\n";
    // 数据源缩略形（UX-02——完整规范文本在追溯区块；§8.1 头块"修订
    // r-…〔seq〕"原文的缩略呈现）。
    ctx.out += "<tr><th>数据源</th><td>" + htmlEscape(abbreviateId(report.project().toCanonical()))
               + " / " + htmlEscape(abbreviateId(report.branch().toCanonical())) + " / "
               + htmlEscape(abbreviateId(report.revision().toCanonical()))
               + "〔seq " + std::to_string(report.revisionSeq()) + "〕</td></tr>\n";
    if (report.snapshotId().has_value()) {
        ctx.out += "<tr><th>统一快照</th><td>" + htmlEscape(abbreviateId(report.snapshotId()->toCanonical()))
                   + "</td></tr>\n";
    }
    ctx.out += "<tr><th>报告版本</th><td>" + std::to_string(report.reportVersion()) + "</td></tr>\n";
    if (report.supersedes().has_value()) {
        ctx.out += "<tr><th>取代</th><td>" + htmlEscape(abbreviateId(report.supersedes()->toCanonical()))
                   + "</td></tr>\n";
    }
    ctx.out += "<tr><th>生成时间</th><td>" + htmlEscape(rfc3339Utc(report.generatedAtUtc())) + "</td></tr>\n";
    ctx.out += "<tr><th>生成者</th><td>";
    ctx.appendRedacted(report.generatedBy());
    ctx.out += "</td></tr>\n";
    ctx.out += "</table>\n";

    // 追溯区块（UX-02 允许域——完整身份规范文本/Schema 版本限定于此）。
    ctx.out += "<div class=\"trace-block\" id=\"report-trace\">报告身份（追溯区块）：ReportId "
               + htmlEscape(report.reportId().toCanonical()) + "；内容身份 "
               + htmlEscape(report.contentIdentity().toCanonical()) + "；数据身份 "
               + htmlEscape(report.dataIdentity().toCanonical()) + "；章节模型 "
               + htmlEscape(report.sectionModelVersion()) + "；生成器 ";
    ctx.appendRedacted(report.generatorVersion());
    ctx.out += "</div>\n";

    // 目录（§8.1——章节锚点导航，§5 order）。
    ctx.out += "<nav id=\"toc\"><h2>目录</h2>\n";
    for (const ReviewReportSection& section : report.sections()) {
        ctx.out += "<a href=\"#sec-" + htmlEscape(section.sectionId) + "\">"
                   + htmlEscape(sectionDisplayName(section.sectionId)) + "</a>\n";
    }
    ctx.out += "<a href=\"#report-diagnostics\">诊断汇总</a>\n";
    ctx.out += "<a href=\"#report-external\">外部验证边界</a>\n";
    if (levelC) {
        ctx.out += "<a href=\"#report-review\">评审签署</a>\n";
    }
    ctx.out += "<a href=\"#report-appendix\">追溯附录</a>\n</nav>\n";
}

/// "正式通过结论"字样输出门（§8.2① 前置断言——渲染器对字段做前置断言：
/// 字样仅在 formalPass 资格成立时才可输出，违反即 DataInvalid 构造失败，
/// 不存在"渲染了再说"路径。RPT-T08 措辞冻结收口：本门是全部渲染文本中
/// "正式通过"短语的唯一合法出源（否定声明措辞已避开该短语——RP-STATE-1
/// 的"正文无'正式通过'字样〔HTML 文本扫描计数=0〕"机械验收面）。
void appendFormalPassDesignation(HtmlRenderContext& ctx, bool formalPassAllowed)
{
    if (!formalPassAllowed) {
        throw ReportError(ReportErrorCode::DataInvalid,
                          "§8.2① 前置断言违约：资格不成立时禁止渲染\"正式通过结论\"字样");
    }
    ctx.out += "<p class=\"declaration\">正式通过结论（资格成立）</p>\n";
}

/// 逐章节渲染（§8.1 章节结构：徽标/条目表 data-field 锚/缺项清单/当前性
/// 标记/限定语标记/折叠追溯区块〔证据表＋结果引用＋资格声明〕/章节诊断）。
void appendSection(HtmlRenderContext& ctx, const ReviewReportSection& section)
{
    ctx.out += "<section id=\"sec-" + htmlEscape(section.sectionId) + "\">\n";
    ctx.out += "<h2>" + htmlEscape(sectionDisplayName(section.sectionId))
               + "<span class=\"badge\">" + htmlEscape(sectionStatusBadge(section.status))
               + "</span></h2>\n";

    // 章节当前性标记（§8.1——引用 Superseded 结果的章节必填，呈现于
    // 章节头；不可判定形态以"不可判定"呈现——非第三持久态）。
    if (section.currentness.has_value()) {
        ctx.out += "<p class=\"currentness\">当前性：" + htmlEscape(currentnessDisplay(*section.currentness));
        // Superseded 原因清单（§6.3"过期附原因"）。
        for (const evidence::InvalidationReason& reason : section.currentness->reasons) {
            ctx.out += "；" + htmlEscape(reason.dependencyKey) + "（"
                       + htmlEscape(invalidationKindToken(reason.kind)) + "）";
        }
        ctx.out += "</p>\n";
    }

    // 缺项全量清单（§4.6——缺失一律显示清单，"—"不得掩盖缺失；原因
    // 为机器产源自由文本——过脱敏）。
    if (!section.missingItems.empty()) {
        ctx.out += "<h3>缺项清单</h3>\n<ul>\n";
        for (const MissingItemView& missing : section.missingItems) {
            ctx.out += "<li>";
            ctx.appendEscaped(missing.itemId);
            ctx.out += "——";
            ctx.appendRedacted(missing.reason);
            ctx.out += "</li>\n";
        }
        ctx.out += "</ul>\n";
    }

    // 条目表（§8.1——每数据单元格 data-field 机器可提取锚；值/单位/状态/
    // 限定语全部取自矩阵单元格〔值单源——§8.4〕；限定语以 data-qualifier
    // 标记结构性输出，逐 token 不可省略——§6.4/§8.2③）。
    if (!section.entries.empty()) {
        ctx.out += "<h3>条目</h3>\n<table data-section=\"" + htmlEscape(section.sectionId) + "\">\n";
        ctx.out += "<tr><th>条目</th><th>字段</th><th>值</th><th>单位</th><th>工程判定</th></tr>\n";
        for (const std::size_t entryIndex : entryOrderIndices(section)) {
            const SectionEntryView& entry = section.entries[entryIndex];
            // 包络合并标注（§6.5/DYN-07 呈现侧——RPT-T08）：条目 caseScope
            // 跨多个工况＝多工况合并呈现形态，标注"包络合并（呈现方式）"。
            // 触发数据源＝条目 caseScope（§4.3.4"该结论覆盖的工况集"——数据
            // 驱动，非自由标注）；语义仅为呈现方式声明（表 2⑤"多工况包络
            // 合并仅为呈现方式"），不替代逐工况条目、不参与覆盖核算（覆盖
            // 核算归 coverageSummary 呈现面与 evidence 门禁）。逐条目标注
            // 一次（首字段行），不逐字段重复。
            const bool envelopeMerged = entry.caseScope.size() > 1;
            bool mergeNoted = false;
            for (const FieldValue& field : entry.fields) {
                const std::string fieldKey =
                    section.sectionId + "." + entry.entryKey + "." + field.key;
                const FieldCell& cell = ctx.matrix.consume(fieldKey);
                ctx.out += "<tr data-entry=\"" + htmlEscape(entry.entryKey) + "\">";
                ctx.out += "<td>" + htmlEscape(entry.entryKey);
                if (envelopeMerged && !mergeNoted) {
                    ctx.out += "<span class=\"merge-note\" data-case-merge=\"envelope\">"
                               "包络合并（呈现方式）</span>";
                    mergeNoted = true;
                }
                ctx.out += "</td>";
                ctx.out += "<td>" + htmlEscape(field.key) + "</td>";
                ctx.out += "<td data-field=\"" + htmlEscape(fieldKey) + "\">"
                           + htmlEscape(cell.valueRepr);
                // 限定语标记（§6.4——逐 token 结构性输出；token 为
                // data-qualifier 机器锚、文案为 §6.4 呈现义务列）。
                for (const QualifierToken qualifier : cell.qualifier) {
                    ctx.out += "<span class=\"qualifier\" data-qualifier=\""
                               + htmlEscape(token(qualifier)) + "\">"
                               + htmlEscape(qualifierLabel(qualifier)) + "</span>";
                }
                ctx.out += "</td>";
                ctx.out += "<td class=\"unit\">"
                           + (cell.unit.has_value() ? htmlEscape(*cell.unit) : std::string("&mdash;"))
                           + "</td>";
                // 状态列：矩阵单元格 status token → 工程用语显示（值单源
                // ——状态维度与三格式比对同源）。
                ctx.out += "<td>"
                           + (cell.status.has_value()
                                  ? htmlEscape(engineeringStatusDisplayFromToken(*cell.status))
                                  : std::string("&mdash;"))
                           + "</td>";
                ctx.out += "</tr>\n";
            }
        }
        ctx.out += "</table>\n";
    }

    // 资格声明块（§8.2①②——两类声明独立、措辞硬门槛；renderHint 为
    // MetadataBlock 的框架章节无条目声明面，资格说明缺席即跳过）。
    if (section.eligibilityNote.has_value()) {
        ctx.out += "<div class=\"trace-block\" data-eligibility=\"" + htmlEscape(section.sectionId)
                   + "\">\n";
        if (section.eligibilityNote->formalPassAllowed) {
            // §8.2①前置断言的执行点："正式通过结论"字样仅在资格成立时
            // 可输出——门在 appendFormalPassDesignation 内（violation →
            // DataInvalid，不存在"渲染了再说"路径）。资格与事实的五路
            // 前置一致性已在渲染入口经 validateDeclarationConsistency
            // 断言（RPT-T08 规则层）。
            appendFormalPassDesignation(ctx, true);
        }
        if (section.eligibilityNote->reviewRecordAllowed) {
            // §8.2②：不可行结论的独立声明措辞——§8.2②原文引号形"经验证
            // 的不可行结论（正式评审记录）"（RPT-T08 对齐收口），与"正式
            // 通过"绝不互换；措辞不含"通过"字样（RP-STATE-3"永不输出
            // '通过'"的机械可断言面）。
            ctx.out += "<p class=\"declaration\">经验证的不可行结论（正式评审记录）——资格成立</p>\n";
        }
        if (!section.eligibilityNote->formalPassAllowed
            && !section.eligibilityNote->reviewRecordAllowed) {
            // 未达任何声明资格——呈现事实，不渲染任何声明字样。措辞钉住
            // "不构成正式结论"（§6.2"不构成结论"词族）：机械扫描口径下
            // 不含"正式通过"短语（RP-STATE-1 扫描计数=0 的前提之一——
            // RPT-T08 措辞调整，原"未达正式通过"表述见单元卡 §14.4 v0.11）。
            ctx.out += "<p class=\"declaration\">不构成正式结论（资格不成立）</p>\n";
        }
        if (!section.eligibilityNote->note.empty()) {
            ctx.out += "<p>";
            ctx.appendRedacted(section.eligibilityNote->note);
            ctx.out += "</p>\n";
        }
        ctx.out += "</div>\n";
    }

    // 折叠追溯区块（§8.1"结果引用（runId＋评估键＋快照身份——追溯区块，
    // 折叠呈现）"；证据表与章节诊断同置——身份规范文本的章节内允许域）。
    const bool hasEvidence =
        std::any_of(ctx.report.evidenceRefs().begin(), ctx.report.evidenceRefs().end(),
                    [&section](const EvidenceRefEntry& e) { return e.sourceSection == section.sectionId; });
    if (hasEvidence || !section.sourceResults.empty() || !section.diagnostics.empty()) {
        ctx.out += "<details class=\"trace-block\"><summary>追溯区块</summary>\n";

        // 证据表（§8.1——itemId/状态/产物摘要/工况范围/原因；itemId 与
        // 摘要规范文本为追溯域允许内容）。
        if (hasEvidence) {
            ctx.out += "<h4>证据</h4>\n<table data-evidence=\"" + htmlEscape(section.sectionId)
                       + "\">\n<tr><th>itemId</th><th>状态</th><th>产物摘要</th><th>工况范围</th><th>说明</th></tr>\n";
            for (const EvidenceRefEntry* entry : orderedEvidenceRefs(ctx.report.evidenceRefs())) {
                if (entry->sourceSection != section.sectionId) {
                    continue;   // 只呈现本章节来源的证据（引用关系可反向导航）
                }
                std::string caseScope;
                if (entry->caseScope.has_value()) {
                    for (std::size_t i = 0; i < entry->caseScope->size(); ++i) {
                        if (i > 0) {
                            caseScope += "、";
                        }
                        caseScope += abbreviateId((*entry->caseScope)[i].toCanonical());
                    }
                }
                ctx.out += "<tr><td>" + htmlEscape(entry->itemId) + "</td><td>"
                           + htmlEscape(evidenceStatusToken(entry->status)) + "</td><td>";
                if (entry->artifactDigest.has_value()) {
                    ctx.out += "sha256-" + htmlEscape(digestPrefix12(*entry->artifactDigest)) + "…";
                } else {
                    ctx.out += "&mdash;";
                }
                ctx.out += "</td><td>" + (caseScope.empty() ? std::string("&mdash;") : htmlEscape(caseScope))
                           + "</td><td>";
                if (entry->notApplicableReason.has_value()) {
                    ctx.appendRedacted(*entry->notApplicableReason);
                } else if (entry->invalidReasonCode.has_value()) {
                    ctx.appendEscaped(*entry->invalidReasonCode);
                } else {
                    ctx.out += "&mdash;";
                }
                ctx.out += "</td></tr>\n";
            }
            ctx.out += "</table>\n";
        }

        // 结果引用（本章节 sourceResults 的追溯行——runId/评估键/快照身份
        // 完整规范文本：追溯区块为 UX-02 允许域）。
        if (!section.sourceResults.empty()) {
            ctx.out += "<h4>结果引用</h4>\n<table data-results=\"" + htmlEscape(section.sectionId)
                       + "\">\n<tr><th>runId</th><th>评估键</th><th>快照身份</th></tr>\n";
            for (const core::RunId& runId : section.sourceResults) {
                for (const ResultRefSnapshot& ref : ctx.report.resultRefs()) {
                    if (!(ref.runId == runId)) {
                        continue;   // 构建校验保证可解析——防御面跳过未知运行
                    }
                    ctx.out += "<tr><td>" + htmlEscape(ref.runId.toCanonical()) + "</td><td>"
                               + htmlEscape(ref.evaluationKey) + "</td><td>"
                               + htmlEscape(ref.snapshotId.toCanonical()) + "</td></tr>\n";
                    break;
                }
            }
            ctx.out += "</table>\n";
        }

        // 章节诊断（§8.1——稳定码/对象定位/三要素/occurrences；定位名
        // 过脱敏双保险）。
        if (!section.diagnostics.empty()) {
            ctx.out += "<h4>诊断</h4>\n<table data-diagnostics=\"" + htmlEscape(section.sectionId)
                       + "\">\n<tr><th>稳定码</th><th>级别</th><th>对象定位</th><th>比较（实际/期望）</th><th>次数</th></tr>\n";
            for (const DiagRefEntry& diag : section.diagnostics) {
                ctx.out += "<tr><td>" + htmlEscape(diag.code) + "</td><td>"
                           + htmlEscape(diagnostics::severityToken(diag.severity)) + "</td><td>";
                if (diag.localName.has_value()) {
                    ctx.appendRedacted(*diag.localName);
                } else if (diag.runtimeName.has_value()) {
                    ctx.appendRedacted(*diag.runtimeName);
                } else if (diag.subject.has_value()) {
                    ctx.out += htmlEscape(abbreviateId(diag.subject->toCanonical()));
                } else {
                    ctx.out += "&mdash;";
                }
                ctx.out += "</td><td>";
                if (diag.comparison.has_value()) {
                    const ValueProjection actual = projectComparativeValue(diag.comparison->actual);
                    const ValueProjection expected =
                        projectComparativeValue(diag.comparison->expected);
                    ctx.out += htmlEscape(actual.text)
                               + (actual.unit.has_value() ? " " + htmlEscape(*actual.unit) : "")
                               + " / " + htmlEscape(expected.text)
                               + (expected.unit.has_value() ? " " + htmlEscape(*expected.unit) : "");
                } else {
                    ctx.out += "&mdash;";
                }
                ctx.out += "</td><td>" + std::to_string(diag.occurrences) + "</td></tr>\n";
            }
            ctx.out += "</table>\n";
        }
        ctx.out += "</details>\n";
    }

    ctx.out += "</section>\n";
}

/// 诊断汇总章（§8.1"诊断汇总章（diagnostics）"——报告级 diagRefs 全量；
/// 定位名过脱敏双保险，主体对象缩略形呈现）。
void appendDiagnosticsSummary(HtmlRenderContext& ctx)
{
    ctx.out += "<section id=\"report-diagnostics\">\n<h2>诊断汇总</h2>\n";
    if (ctx.report.diagRefs().empty()) {
        ctx.out += "<p>无诊断引用。</p>\n</section>\n";
        return;
    }
    ctx.out += "<table>\n<tr><th>稳定码</th><th>级别</th><th>类别</th><th>对象定位</th>"
               "<th>比较（实际/期望）</th><th>次数</th><th>来源章节</th></tr>\n";
    for (const DiagRefEntry* diag : orderedDiagRefs(ctx.report.diagRefs())) {
        ctx.out += "<tr><td>" + htmlEscape(diag->code) + "</td><td>"
                   + htmlEscape(diagnostics::severityToken(diag->severity)) + "</td><td>"
                   + htmlEscape(diagnostics::categoryToken(diag->category)) + "</td><td>";
        if (diag->localName.has_value()) {
            ctx.appendRedacted(*diag->localName);
        } else if (diag->runtimeName.has_value()) {
            ctx.appendRedacted(*diag->runtimeName);
        } else if (diag->subject.has_value()) {
            ctx.out += htmlEscape(abbreviateId(diag->subject->toCanonical()));
        } else {
            ctx.out += "&mdash;";
        }
        ctx.out += "</td><td>";
        if (diag->comparison.has_value()) {
            const ValueProjection actual = projectComparativeValue(diag->comparison->actual);
            const ValueProjection expected = projectComparativeValue(diag->comparison->expected);
            ctx.out += htmlEscape(actual.text)
                       + (actual.unit.has_value() ? " " + htmlEscape(*actual.unit) : "") + " / "
                       + htmlEscape(expected.text)
                       + (expected.unit.has_value() ? " " + htmlEscape(*expected.unit) : "");
        } else {
            ctx.out += "&mdash;";
        }
        ctx.out += "</td><td>" + std::to_string(diag->occurrences) + "</td><td>"
                   + htmlEscape(sectionDisplayName(diag->sourceSection)) + "</td></tr>\n";
    }
    ctx.out += "</table>\n</section>\n";
}

/// 外部验证边界章（§8.1——外部资源状态〔Recorded＝未固化〕表）。
/// RPT-T08 限定语呈现义务锁（§6.4 行 3）：Recorded（未固化）/复现要素缺项
/// ⇒ external-validation-incomplete 限定语强制呈现——本章即其报告级结构
/// 承载位（data-qualifier 机器锚＋冻结文案"外部验证未完成"，不可弱化为
/// 普通标注）；条目级关联键未入模型（v0.9 登记），不到单元格上伪标注。
void appendExternalValidationBoundary(HtmlRenderContext& ctx)
{
    ctx.out += "<section id=\"report-external\">\n<h2>外部验证边界</h2>\n";
    if (ctx.report.externalResourceSummary().empty()) {
        ctx.out += "<p>无外部资源依赖。</p>\n</section>\n";
        return;
    }
    ctx.out += "<table>\n<tr><th>资源</th><th>固化状态</th><th>来源</th></tr>\n";
    for (const ExternalResourceStateEntry& entry : ctx.report.externalResourceSummary()) {
        ctx.out += "<tr><td>" + htmlEscape(entry.resourceId) + "</td><td>"
                   + (entry.state == ExternalResourceState::Recorded ? "已记录（未固化）" : "已固化");
        if (entry.state == ExternalResourceState::Recorded) {
            // §6.4 行 3 呈现义务：限定语随未固化资源出现（结构性标记）。
            ctx.out += "<span class=\"qualifier\" data-qualifier=\"external-validation-incomplete\">"
                       + htmlEscape(qualifierLabel(QualifierToken::ExternalValidationIncomplete))
                       + "</span>";
        }
        ctx.out += "</td><td>";
        ctx.appendRedacted(entry.source);
        ctx.out += "</td></tr>\n";
    }
    ctx.out += "</table>\n</section>\n";
}

/// 评审签署块（§8.1——C 级：signoff/comments/changeLog/variantDiff；
/// 签署仅记录事实——不赋予也不撤销工程资格，§4.5）。
void appendReviewBlock(HtmlRenderContext& ctx)
{
    const ReviewMetadata& review = ctx.report.review();
    ctx.out += "<section id=\"report-review\">\n<h2>评审签署</h2>\n<table>\n";
    ctx.out += "<tr><th>评审人</th><td>";
    if (review.reviewer.has_value()) {
        ctx.appendRedacted(*review.reviewer);
    } else {
        ctx.out += "&mdash;";
    }
    ctx.out += "</td></tr>\n<tr><th>评审时间</th><td>"
               + (review.reviewedAtUtc.has_value() ? htmlEscape(rfc3339Utc(*review.reviewedAtUtc))
                                                   : std::string("&mdash;"))
               + "</td></tr>\n";
    ctx.out += "<tr><th>签署状态</th><td>";
    if (review.signOff.state == SignOffState::State::Signed) {
        ctx.out += "已签署（";
        ctx.appendRedacted(review.signOff.signer);
        ctx.out += " " + htmlEscape(rfc3339Utc(review.signOff.signedAtUtc)) + "）";
    } else {
        ctx.out += "未签署";
    }
    ctx.out += "</td></tr>\n</table>\n";

    if (!review.comments.empty()) {
        ctx.out += "<h3>评审意见</h3>\n<table>\n<tr><th>作者</th><th>时间</th><th>内容</th></tr>\n";
        for (const ReviewComment& comment : review.comments) {
            ctx.out += "<tr><td>";
            ctx.appendRedacted(comment.author);
            ctx.out += "</td><td>" + htmlEscape(rfc3339Utc(comment.atUtc)) + "</td><td>";
            ctx.appendRedacted(comment.text);
            ctx.out += "</td></tr>\n";
        }
        ctx.out += "</table>\n";
    }

    if (review.variantDiff.has_value()) {
        ctx.out += "<h3>改型差异</h3>\n<p>基线 "
                   + htmlEscape(abbreviateId(review.variantDiff->baselineRevision.toCanonical()))
                   + " → 候选 "
                   + htmlEscape(abbreviateId(review.variantDiff->candidateRevision.toCanonical()))
                   + "；基准一致性："
                   + (review.variantDiff->baselineCheckResult.consistent ? "一致" : "不一致")
                   + "</p>\n";
        if (!review.variantDiff->tradeOffs.empty()) {
            ctx.out += "<ul>\n";
            for (const TradeOff& tradeOff : review.variantDiff->tradeOffs) {
                ctx.out += "<li>";
                ctx.appendRedacted(tradeOff.topic);
                ctx.out += "——";
                ctx.appendRedacted(tradeOff.rationale);
                ctx.out += "</li>\n";
            }
            ctx.out += "</ul>\n";
        }
    }

    if (!review.changeLog.empty()) {
        ctx.out += "<h3>变更记录</h3>\n<table>\n<tr><th>版本</th><th>操作者</th><th>时间</th><th>原因</th></tr>\n";
        for (const ReportVersionEntry& entry : review.changeLog) {
            ctx.out += "<tr><td>v" + std::to_string(entry.reportVersion) + "</td><td>";
            ctx.appendRedacted(entry.actor);
            ctx.out += "</td><td>" + htmlEscape(rfc3339Utc(entry.atUtc)) + "</td><td>";
            ctx.appendRedacted(entry.reason);
            ctx.out += "</td></tr>\n";
        }
        ctx.out += "</table>\n";
    }
    ctx.out += "</section>\n";
}

/// 追溯附录（§8.1——resultRefs 全表〔五元组/模式/outcome/engineeringStatus/
/// 资格两声明/快照身份〕＋复现要素＋单位选择＋覆盖摘要；完整身份规范
/// 文本的文档级允许域）。
void appendTraceAppendix(HtmlRenderContext& ctx)
{
    ctx.out += "<section id=\"report-appendix\">\n<h2>追溯附录</h2>\n";

    // 结果引用全表（runId 序——§8.1 稳定排序；资格两声明独立呈现）。
    // 列名以资格 token 词面承载（RPT-T08 措辞收口：附录为追溯域〔UX-02
    // 允许域〕，且 RP-STATE-1 的"正文无'正式通过'字样"机械扫描口径要求
    // 声明短语只在资格成立声明中出现——原"正式通过资格"列名见单元卡
    // §14.4 v0.11 登记调整）。
    ctx.out += "<h3>结果引用</h3>\n<table>\n<tr><th>runId</th><th>任务（prj/brn/rev/run/att）</th>"
               "<th>评估键</th><th>模式</th><th>结果完整性</th><th>工程判定</th>"
               "<th>formalPass 资格</th><th>reviewRecord 资格</th><th>快照身份</th><th>当前性</th></tr>\n";
    for (const ResultRefSnapshot* ref : orderedResultRefs(ctx.report.resultRefs())) {
        ctx.out += "<tr><td>" + htmlEscape(ref->runId.toCanonical()) + "</td><td>"
                   + htmlEscape(abbreviateId(ref->task.project.toCanonical())) + " / "
                   + htmlEscape(abbreviateId(ref->task.branch.toCanonical())) + " / "
                   + htmlEscape(abbreviateId(ref->task.revision.toCanonical())) + " / "
                   + htmlEscape(abbreviateId(ref->task.run.toCanonical())) + " / "
                   + std::to_string(ref->task.attempt.value) + "</td><td>"
                   + htmlEscape(ref->evaluationKey) + "</td><td>"
                   + htmlEscape(evaluationModeDisplay(ref->mode)) + "</td><td>"
                   + htmlEscape(taskOutcomeDisplay(ref->outcome)) + "</td><td>"
                   + htmlEscape(engineeringStatusDisplay(ref->engineeringStatus)) + "</td><td>"
                   + (ref->eligibility.formalPass ? "成立" : "不成立") + "</td><td>"
                   + (ref->eligibility.reviewRecord ? "成立" : "不成立") + "</td><td>"
                   + htmlEscape(ref->snapshotId.toCanonical()) + "</td><td>"
                   + htmlEscape(currentnessDisplay(ref->currentness)) + "</td></tr>\n";
    }
    ctx.out += "</table>\n";

    // 复现要素（§8.1 附录——版本族全量；版本串过脱敏【无害过滤】）。
    const evidence::ReproductionBlock& repro = ctx.report.reproduction();
    ctx.out += "<h3>复现要素</h3>\n<table>\n<tr><th>产品版本</th><td>";
    ctx.appendRedacted(repro.productVersion);
    ctx.out += "</td></tr>\n<tr><th>评估契约版本</th><td>";
    ctx.appendRedacted(repro.evidenceContractVersion);
    ctx.out += "</td></tr>\n";
    if (!repro.codecVersions.empty()) {
        ctx.out += "<tr><th>codec 版本族</th><td>";
        for (std::size_t i = 0; i < repro.codecVersions.size(); ++i) {
            if (i > 0) {
                ctx.out += "、";
            }
            ctx.appendRedacted(repro.codecVersions[i]);
        }
        ctx.out += "</td></tr>\n";
    }
    if (repro.compilerContractVersion.has_value()) {
        ctx.out += "<tr><th>编译器契约版本</th><td>";
        ctx.appendRedacted(*repro.compilerContractVersion);
        ctx.out += "</td></tr>\n";
    }
    if (repro.collisionBackendVersion.has_value()) {
        ctx.out += "<tr><th>碰撞后端版本</th><td>";
        ctx.appendRedacted(*repro.collisionBackendVersion);
        ctx.out += "</td></tr>\n";
    }
    ctx.out += "</table>\n";

    // 单位选择（§4.2 冻结集——附录呈现；std::map 迭代序＝量纲枚举序）。
    if (!ctx.report.unitPreference().displayUnits.empty()) {
        ctx.out += "<h3>单位选择</h3>\n<p>";
        bool first = true;
        for (const auto& [kind, unit] : ctx.report.unitPreference().displayUnits) {
            if (!first) {
                ctx.out += "、";
            }
            first = false;
            ctx.out += htmlEscape(quantityKindToken(kind)) + "＝" + htmlEscape(unit.symbol());
        }
        ctx.out += "</p>\n";
    }

    // 覆盖摘要（§6.5——五态计数聚合＋覆盖完备性结论；RPT-T08 RP-COV-1）。
    const CaseCoverageSummary& coverage = ctx.report.coverageSummary();
    ctx.out += "<h3>必验工况覆盖摘要</h3>\n<p>必验总数 " + std::to_string(coverage.totalRequired)
               + "；已执行 " + std::to_string(coverage.executed) + "；未执行 "
               + std::to_string(coverage.notExecuted) + "；执行无效 "
               + std::to_string(coverage.invalid) + "；不适用 "
               + std::to_string(coverage.notApplicable) + "；执行失败 "
               + std::to_string(coverage.failed) + "</p>\n";
    // 覆盖完备性结论（§6.5 原文："全部启用必验工况已覆盖"或"漏验清单"；
    // EVI-02 呈现口径——漏验即整体数据不足缺项呈现，配合渲染入口断言⑤
    // 不存在漏验与 formalPass 声明并存的报告）。逐工况漏验名单的逐行数据
    // 未入 §4 模型（coverage.csv 零数据行——v0.9 登记），此处以计数呈现
    // 漏验清单规模，不伪造逐工况状态（ERR-01）。
    if (coverageIncomplete(coverage)) {
        const std::uint64_t missed =
            coverage.notExecuted + coverage.invalid + coverage.failed;
        ctx.out += "<p>覆盖完备性：漏验 " + std::to_string(missed)
                   + " 项启用必验工况（未执行 " + std::to_string(coverage.notExecuted)
                   + "、执行无效 " + std::to_string(coverage.invalid) + "、执行失败 "
                   + std::to_string(coverage.failed) + "）——整体数据不足（缺项呈现）</p>\n";
    } else if (coverage.totalRequired == 0) {
        // P-EV-7 的呈现侧如实表达：无启用必验工况（判定语义归 evidence，
        // 此处不产出"已覆盖"的空真表述）。
        ctx.out += "<p>覆盖完备性：无启用必验工况</p>\n";
    } else {
        ctx.out += "<p>覆盖完备性：全部启用必验工况已覆盖</p>\n";
    }
    // P-EV-5 报告侧承接（RPT-T08 acceptance 6）：覆盖率参考值降级限定语为
    // 消费侧强制呈现——不得弱化为普通标注、不得省略。数据源＝diagRefs 携
    // 带 EVI-REGION-COVERAGE-DOWNGRADED（evidence 降级裁定的报告内投影，
    // 见 hasDowngradedCoverageReference）；结构性输出＝data-qualifier 机器
    // 锚＋§6.4 冻结文案（JSON 镜像面同源，见 buildReportDom coverage 块）。
    if (hasDowngradedCoverageReference(ctx.report)) {
        ctx.out += "<p><span class=\"qualifier\" data-qualifier=\"downgraded-reference-value\">"
                   + htmlEscape(qualifierLabel(QualifierToken::DowngradedReferenceValue))
                   + "</span></p>\n";
    }

    ctx.out += "</section>\n";
}

}  // namespace

HtmlReportRenderer::HtmlReportRenderer(const diagnostics::IRedactionService& redaction)
    : m_redaction(&redaction)
{
}

RenderOutcome HtmlReportRenderer::render(const ReviewReport& report, const FieldMatrix& matrix,
                                         ReportRenderFormat format)
{
    // 实现类只服务自己的格式词位（§9.3"新格式＝新实现"——他格式请求＝
    // 调用方违约）。
    if (format != ReportRenderFormat::Html) {
        ReportError error(ReportErrorCode::Usage,
                          "HtmlReportRenderer 仅支持 html 格式，收到 " + std::string(token(format)));
        return RenderOutcome{std::nullopt, std::move(error)};
    }

    try {
        validateRenderPreconditions(report);
        // RPT-T08 规则层（§8.2①硬编码纪律＋§8.2④模板纪律）——装配前的
        // 数据一致性与模板词表自检，三格式统一执行（与格式无关）。
        validateDeclarationConsistency(report);
        assertWordingFreezeTemplateDiscipline();
        MatrixIndex index(matrix);

        HtmlRenderContext ctx{report, index, *m_redaction, {}};
        const bool levelC = report.level() == ReportLevel::C;
        beginHtmlDocument(ctx, levelC ? "评审报告（正式评审级报告）" : "评审报告（基础级报告）");
        appendReportHeader(ctx);
        for (const ReviewReportSection& section : report.sections()) {
            appendSection(ctx, section);
        }
        // 文档级区块（§8.1 结构序：诊断汇总→外部验证边界→评审签署〔C〕
        // →追溯附录）。
        appendDiagnosticsSummary(ctx);
        appendExternalValidationBoundary(ctx);
        if (levelC) {
            appendReviewBlock(ctx);
        }
        appendTraceAppendix(ctx);
        index.assertFullyConsumed();
        ctx.out += "</body>\n</html>\n";

        // 工件组装（摘要经 core ContentDigester 唯一算法——§4.1）。
        RenderArtifact artifact;
        artifact.format = ReportRenderFormat::Html;
        artifact.bytes.assign(ctx.out.begin(), ctx.out.end());
        core::ContentDigester digester;
        digester.update(artifact.bytes.data(), artifact.bytes.size());
        artifact.digest = digester.finalize();
        artifact.rendererVersion = kReportRendererVersion;
        artifact.templateVersion = kHtmlTemplateVersion;
        artifact.sourceReportIdentity = report.contentIdentity();
        return RenderOutcome{std::move(artifact), std::nullopt};
    } catch (const ReportError& error) {
        // 失败＝无部分产物外泄（§9.3 后置行——缓冲局部，异常即弃）。
        return RenderOutcome{std::nullopt, error};
    }
}

// =====================================================================
// JSON 渲染器（§8.3——ird-report-json/1）
// =====================================================================

namespace {

/// DOM 装配小工具（可读性——语义同 ReportJsonDom 工厂）。
ReportJsonDom jstr(std::string value) { return ReportJsonDom::string(std::move(value)); }
ReportJsonDom jnum(double value) { return ReportJsonDom::number(value); }
ReportJsonDom jbool(bool value) { return ReportJsonDom::boolean(value); }
ReportJsonDom jobj(std::vector<std::pair<std::string, ReportJsonDom>> members)
{
    return ReportJsonDom::object(std::move(members));
}
ReportJsonDom jarr(std::vector<ReportJsonDom> items) { return ReportJsonDom::array(std::move(items)); }

/**
 * @brief 当前性快照 DOM（§8.3——四态/原因/不可判定诊断全镜像；status
 *         为 null＝不可判定计算形态，unevaluableNote 随诊断词表镜像）。
 */
ReportJsonDom currentnessDom(const CurrentnessSnapshot& snapshot,
                             const diagnostics::IRedactionService& redaction)
{
    std::vector<std::pair<std::string, ReportJsonDom>> members;
    members.emplace_back("status",
                         snapshot.status.has_value()
                             ? jstr(std::string(*snapshot.status == evidence::CurrentnessStatus::Current
                                                    ? "current"
                                                    : "superseded"))
                             : ReportJsonDom::nullValue());
    members.emplace_back("evaluatedAgainst",
                         jobj({{"headRevision", jstr(snapshot.evaluatedAgainst.headRevision.toCanonical())},
                               {"contextSummary",
                                jstr(redactText(redaction, snapshot.evaluatedAgainst.contextSummary))}}));
    members.emplace_back("computedAtUtc", jstr(rfc3339Utc(snapshot.computedAtUtc)));
    std::vector<ReportJsonDom> reasons;
    for (const evidence::InvalidationReason& reason : snapshot.reasons) {
        reasons.push_back(jobj({{"dependencyKey", jstr(reason.dependencyKey)},
                                {"kind", jstr(std::string(invalidationKindToken(reason.kind)))},
                                {"detail", jstr(redactText(redaction, reason.detail))}}));
    }
    members.emplace_back("reasons", jarr(std::move(reasons)));
    return jobj(std::move(members));
}

/// 诊断引用 DOM（localName/runtimeName 过脱敏——双保险；码/严重/类别为
/// 词表 token 不过脱敏）。
ReportJsonDom diagRefDom(const DiagRefEntry& entry, const diagnostics::IRedactionService& redaction)
{
    std::vector<std::pair<std::string, ReportJsonDom>> members;
    members.emplace_back("code", jstr(entry.code));
    members.emplace_back("severity", jstr(std::string(diagnostics::severityToken(entry.severity))));
    members.emplace_back("category", jstr(std::string(diagnostics::categoryToken(entry.category))));
    members.emplace_back("subject",
                         entry.subject.has_value() ? jstr(entry.subject->toCanonical())
                                                   : ReportJsonDom::nullValue());
    members.emplace_back("localName",
                         entry.localName.has_value()
                             ? jstr(redactText(redaction, *entry.localName))
                             : ReportJsonDom::nullValue());
    members.emplace_back("runtimeName",
                         entry.runtimeName.has_value()
                             ? jstr(redactText(redaction, *entry.runtimeName))
                             : ReportJsonDom::nullValue());
    if (entry.comparison.has_value()) {
        const ValueProjection actual = projectComparativeValue(entry.comparison->actual);
        const ValueProjection expected = projectComparativeValue(entry.comparison->expected);
        members.emplace_back(
            "comparison",
            jobj({{"actual",
                   jobj({{"value", jstr(actual.text)},
                         {"unit", actual.unit.has_value() ? jstr(*actual.unit)
                                                          : ReportJsonDom::nullValue()}})},
                  {"expected",
                   jobj({{"value", jstr(expected.text)},
                         {"unit", expected.unit.has_value() ? jstr(*expected.unit)
                                                            : ReportJsonDom::nullValue()}})}}));
    } else {
        members.emplace_back("comparison", ReportJsonDom::nullValue());
    }
    members.emplace_back("occurrences", jnum(static_cast<double>(entry.occurrences)));
    members.emplace_back("sourceSection", jstr(entry.sourceSection));
    members.emplace_back("sourceRun",
                         entry.sourceRun.has_value() ? jstr(entry.sourceRun->toCanonical())
                                                     : ReportJsonDom::nullValue());
    return jobj(std::move(members));
}

/// 结果引用快照 DOM（§4.3.1 全字段机器镜像——追溯附录的结构化形态）。
ReportJsonDom resultRefDom(const ResultRefSnapshot& ref, const diagnostics::IRedactionService& redaction)
{
    std::vector<ReportJsonDom> caseScope;
    for (const core::ObjectId& caseId : ref.caseScope) {
        caseScope.push_back(jstr(caseId.toCanonical()));
    }
    std::vector<std::pair<std::string, ReportJsonDom>> members;
    members.emplace_back("runId", jstr(ref.runId.toCanonical()));
    members.emplace_back("task",
                         jobj({{"project", jstr(ref.task.project.toCanonical())},
                               {"branch", jstr(ref.task.branch.toCanonical())},
                               {"revision", jstr(ref.task.revision.toCanonical())},
                               {"run", jstr(ref.task.run.toCanonical())},
                               {"attempt", jnum(static_cast<double>(ref.task.attempt.value))}}));
    members.emplace_back("evaluationKey", jstr(ref.evaluationKey));
    members.emplace_back("evaluatorContractVersion", jnum(ref.evaluatorContractVersion));
    members.emplace_back("mode", jstr(std::string(core::toToken(ref.mode))));
    members.emplace_back("outcome", jstr(std::string(core::toToken(ref.outcome))));
    members.emplace_back("engineeringStatus", jstr(std::string(core::toToken(ref.engineeringStatus))));
    members.emplace_back("snapshotId", jstr(ref.snapshotId.toCanonical()));
    members.emplace_back("sliceId", jstr(ref.sliceId.toCanonical()));
    members.emplace_back("inputBaselineId", jstr(ref.inputBaselineId.toCanonical()));
    members.emplace_back("caseScope", jarr(std::move(caseScope)));
    members.emplace_back("eligibility",
                         jobj({{"formalPass", jbool(ref.eligibility.formalPass)},
                               {"reviewRecord", jbool(ref.eligibility.reviewRecord)}}));
    members.emplace_back("currentness", currentnessDom(ref.currentness, redaction));
    members.emplace_back("producer",
                         jobj({{"producedIn", jstr(std::string(producerProcessToken(ref.producer.producedIn)))},
                               {"productVersion",
                                jstr(redactText(redaction, ref.producer.productVersion))}}));
    return jobj(std::move(members));
}

/**
 * @brief 报告全镜像 DOM（§8.3——顶层 17 键冻结集，键序即装配序）。
 *
 * 条目字段值一律取自矩阵单元格（值单源——§8.4）；state token 来自报告
 * 字段结构（结构来自报告、值来自矩阵）；qualifier 数组来自矩阵（§6.4
 * 结构性输出——任何格式省略限定语＝ConsistencyMismatch，§8.5）。
 */
ReportJsonDom buildReportDom(const ReviewReport& report, MatrixIndex& matrix,
                             const diagnostics::IRedactionService& redaction)
{
    // ---- report（报告对象身份块）----
    const ReportJsonDom reportDom = jobj(
        {{"reportId", jstr(report.reportId().toCanonical())},
         {"level", jstr(std::string(token(report.level())))},
         {"reportVersion", jnum(report.reportVersion())},
         {"supersedes", report.supersedes().has_value()
                            ? jstr(report.supersedes()->toCanonical())
                            : ReportJsonDom::nullValue()},
         {"sectionModelVersion", jstr(report.sectionModelVersion())}});

    // ---- source（数据源锚——完整规范文本：机器镜像不受 UX-02 缩略约束）----
    const ReportJsonDom sourceDom =
        jobj({{"project", jstr(report.project().toCanonical())},
              {"branch", jstr(report.branch().toCanonical())},
              {"revision", jstr(report.revision().toCanonical())},
              {"revisionSeq", jnum(static_cast<double>(report.revisionSeq()))},
              {"snapshotId", report.snapshotId().has_value()
                                 ? jstr(report.snapshotId()->toCanonical())
                                 : ReportJsonDom::nullValue()},
              {"inputSliceId", report.inputSliceId().has_value()
                                   ? jstr(report.inputSliceId()->toCanonical())
                                   : ReportJsonDom::nullValue()}});

    // ---- identity（双内容身份——幂等判定唯一依据的镜像）----
    const ReportJsonDom identityDom =
        jobj({{"dataIdentity", jstr(report.dataIdentity().toCanonical())},
              {"contentIdentity", jstr(report.contentIdentity().toCanonical())}});

    // ---- unitPreference（§4.2 冻结集镜像——std::map 迭代序＝量纲枚举序，
    //      确定性由容器保证）----
    std::vector<ReportJsonDom> unitPrefs;
    for (const auto& [kind, unit] : report.unitPreference().displayUnits) {
        unitPrefs.push_back(
            jobj({{"quantityKind", jstr(std::string(quantityKindToken(kind)))},
                  {"unit", jstr(std::string(unit.symbol()))}}));
    }
    const ReportJsonDom unitPrefDom = jarr(std::move(unitPrefs));

    // ---- resultRefs（runId 序）----
    std::vector<ReportJsonDom> resultRefs;
    for (const ResultRefSnapshot* ref : orderedResultRefs(report.resultRefs())) {
        resultRefs.push_back(resultRefDom(*ref, redaction));
    }
    const ReportJsonDom resultRefsDom = jarr(std::move(resultRefs));

    // ---- evidenceRefs（itemId 序——§8.1"证据按 itemId"）----
    std::vector<ReportJsonDom> evidenceRefs;
    for (const EvidenceRefEntry* entry : orderedEvidenceRefs(report.evidenceRefs())) {
        std::vector<std::pair<std::string, ReportJsonDom>> members;
        members.emplace_back("itemId", jstr(entry->itemId));
        members.emplace_back("itemClass", jstr(std::string(itemClassToken(entry->itemClass))));
        members.emplace_back("status", jstr(std::string(evidenceStatusToken(entry->status))));
        members.emplace_back("artifactDigest",
                             entry->artifactDigest.has_value()
                                 ? jstr("sha256-" + digestPrefix12(*entry->artifactDigest) + "…")
                                 : ReportJsonDom::nullValue());
        if (entry->caseScope.has_value()) {
            std::vector<ReportJsonDom> caseScope;
            for (const core::ObjectId& caseId : *entry->caseScope) {
                caseScope.push_back(jstr(caseId.toCanonical()));
            }
            members.emplace_back("caseScope", jarr(std::move(caseScope)));
        } else {
            members.emplace_back("caseScope", ReportJsonDom::nullValue());
        }
        members.emplace_back("subject",
                             entry->subject.has_value() ? jstr(entry->subject->toCanonical())
                                                        : ReportJsonDom::nullValue());
        members.emplace_back("notApplicableReason",
                             entry->notApplicableReason.has_value()
                                 ? jstr(redactText(redaction, *entry->notApplicableReason))
                                 : ReportJsonDom::nullValue());
        members.emplace_back("invalidReasonCode",
                             entry->invalidReasonCode.has_value()
                                 ? jstr(*entry->invalidReasonCode)
                                 : ReportJsonDom::nullValue());
        members.emplace_back("sourceSection", jstr(entry->sourceSection));
        evidenceRefs.push_back(jobj(std::move(members)));
    }
    const ReportJsonDom evidenceRefsDom = jarr(std::move(evidenceRefs));

    // ---- diagRefs（orderKey 序；人读字段过脱敏）----
    std::vector<ReportJsonDom> diagRefs;
    for (const DiagRefEntry* entry : orderedDiagRefs(report.diagRefs())) {
        diagRefs.push_back(diagRefDom(*entry, redaction));
    }
    const ReportJsonDom diagRefsDom = jarr(std::move(diagRefs));

    // ---- currentnessSummary（逐结果当前性快照——§4.2）----
    std::vector<ReportJsonDom> perResult;
    for (const ResultCurrentnessEntry& entry : report.currentnessSummary().perResult) {
        perResult.push_back(jobj({{"runId", jstr(entry.runId.toCanonical())},
                                  {"currentness", currentnessDom(entry.currentness, redaction)}}));
    }
    const ReportJsonDom currentnessSummaryDom = jobj({{"perResult", jarr(std::move(perResult))}});

    // ---- coverageSummary（五态计数——§4.2；逐工况行数据未入模型，
    //      见 CSV 渲染器类注登记）----
    const CaseCoverageSummary& coverage = report.coverageSummary();
    // P-EV-5 报告侧承接（RPT-T08——§6.4 结构性输出的覆盖呈现面）：降级
    // 限定语 token 数组（数据源＝hasDowngradedCoverageReference，与 HTML
    // 覆盖块同源——值单源）。空报告无降级时为空数组（确定性形态）。
    std::vector<ReportJsonDom> coverageQualifiers;
    if (hasDowngradedCoverageReference(report)) {
        coverageQualifiers.push_back(
            jstr(std::string(token(QualifierToken::DowngradedReferenceValue))));
    }
    const ReportJsonDom coverageDom =
        jobj({{"totalRequired", jnum(static_cast<double>(coverage.totalRequired))},
              {"executed", jnum(static_cast<double>(coverage.executed))},
              {"notExecuted", jnum(static_cast<double>(coverage.notExecuted))},
              {"invalid", jnum(static_cast<double>(coverage.invalid))},
              {"notApplicable", jnum(static_cast<double>(coverage.notApplicable))},
              {"failed", jnum(static_cast<double>(coverage.failed))},
              {"qualifier", jarr(std::move(coverageQualifiers))}});

    // ---- externalResourceSummary（CON-03——Recorded/Solidified 词表）----
    // §6.4 行 3 呈现义务（RPT-T08）：Recorded（未固化）条目随 external-
    // validation-incomplete 限定语 token（与 HTML 边界章 data-qualifier
    // 同源——值单源）；Solidified 条目恒空数组。
    std::vector<ReportJsonDom> external;
    for (const ExternalResourceStateEntry& entry : report.externalResourceSummary()) {
        std::vector<ReportJsonDom> externalQualifiers;
        if (entry.state == ExternalResourceState::Recorded) {
            externalQualifiers.push_back(
                jstr(std::string(token(QualifierToken::ExternalValidationIncomplete))));
        }
        external.push_back(
            jobj({{"resourceId", jstr(entry.resourceId)},
                  {"state", jstr(entry.state == ExternalResourceState::Recorded ? "recorded"
                                                                               : "solidified")},
                  {"source", jstr(redactText(redaction, entry.source))},
                  {"qualifier", jarr(std::move(externalQualifiers))}}));
    }
    const ReportJsonDom externalDom = jarr(std::move(external));

    // ---- reproduction（复现要素——evidence 值拷贝的镜像）----
    const evidence::ReproductionBlock& repro = report.reproduction();
    std::vector<ReportJsonDom> codecVersions;
    for (const std::string& version : repro.codecVersions) {
        codecVersions.push_back(jstr(redactText(redaction, version)));
    }
    const ReportJsonDom reproductionDom = jobj(
        {{"productVersion", jstr(redactText(redaction, repro.productVersion))},
         {"evidenceContractVersion", jstr(redactText(redaction, repro.evidenceContractVersion))},
         {"codecVersions", jarr(std::move(codecVersions))},
         {"compilerContractVersion",
          repro.compilerContractVersion.has_value()
              ? jstr(redactText(redaction, *repro.compilerContractVersion))
              : ReportJsonDom::nullValue()},
         {"collisionBackendVersion",
          repro.collisionBackendVersion.has_value()
              ? jstr(redactText(redaction, *repro.collisionBackendVersion))
              : ReportJsonDom::nullValue()}});

    // ---- sections（章节全镜像：条目字段值取自矩阵——值单源）----
    std::vector<ReportJsonDom> sections;
    for (const ReviewReportSection& section : report.sections()) {
        std::vector<ReportJsonDom> entries;
        for (const std::size_t entryIndex : entryOrderIndices(section)) {
            const SectionEntryView& entry = section.entries[entryIndex];
            std::vector<ReportJsonDom> fields;
            for (const FieldValue& field : entry.fields) {
                const std::string fieldKey =
                    section.sectionId + "." + entry.entryKey + "." + field.key;
                const FieldCell& cell = matrix.consume(fieldKey);
                std::vector<ReportJsonDom> qualifiers;
                for (const QualifierToken qualifier : cell.qualifier) {
                    qualifiers.push_back(jstr(std::string(token(qualifier))));
                }
                // SourcedValue 四态显式 token（§8.3）——state 来自报告字段
                // 结构，value 来自矩阵（值单源），methodTag 为来源方法标记
                // （Provided 态——§6.4"估算＋methodTag"的机器镜像）。
                fields.push_back(jobj(
                    {{"key", jstr(field.key)},
                     {"fieldKey", jstr(fieldKey)},
                     {"state", jstr(std::string(fieldStateToken(field.quantity.state())))},
                     {"value", jstr(cell.valueRepr)},
                     {"unit", cell.unit.has_value() ? jstr(*cell.unit) : ReportJsonDom::nullValue()},
                     {"methodTag",
                      field.quantity.state() == core::FieldState::Provided
                              && field.quantity.provenance().methodTag.has_value()
                          ? jstr(redactText(redaction, *field.quantity.provenance().methodTag))
                          : ReportJsonDom::nullValue()},
                     {"qualifier", jarr(std::move(qualifiers))}}));
            }
            std::vector<ReportJsonDom> evidenceBindings;
            for (const EvidenceBinding& binding : entry.evidence) {
                evidenceBindings.push_back(
                    jobj({{"itemId", jstr(binding.itemId)},
                          {"status", jstr(std::string(evidenceStatusToken(binding.status)))},
                          {"digest", binding.digest.has_value()
                                         ? jstr("sha256-" + digestPrefix12(*binding.digest) + "…")
                                         : ReportJsonDom::nullValue()}}));
            }
            std::vector<ReportJsonDom> caseScope;
            for (const core::ObjectId& caseId : entry.caseScope) {
                caseScope.push_back(jstr(caseId.toCanonical()));
            }
            std::vector<std::pair<std::string, ReportJsonDom>> entryMembers;
            entryMembers.emplace_back("entryKey", jstr(entry.entryKey));
            entryMembers.emplace_back("fields", jarr(std::move(fields)));
            entryMembers.emplace_back("result",
                                      jobj({{"runId", jstr(entry.result.runId.toCanonical())},
                                            {"fieldPath", jstr(entry.result.fieldPath)}}));
            entryMembers.emplace_back("evidence", jarr(std::move(evidenceBindings)));
            entryMembers.emplace_back("caseScope", jarr(std::move(caseScope)));
            entryMembers.emplace_back(
                "jump",
                jobj({{"objectId", entry.jump.objectId.has_value()
                                       ? jstr(entry.jump.objectId->toCanonical())
                                       : ReportJsonDom::nullValue()},
                      {"caseId", entry.jump.caseId.has_value()
                                     ? jstr(entry.jump.caseId->toCanonical())
                                     : ReportJsonDom::nullValue()},
                      {"runId", entry.jump.runId.has_value()
                                    ? jstr(entry.jump.runId->toCanonical())
                                    : ReportJsonDom::nullValue()}}));
            entries.push_back(jobj(std::move(entryMembers)));
        }
        std::vector<ReportJsonDom> missingItems;
        for (const MissingItemView& missing : section.missingItems) {
            missingItems.push_back(jobj({{"itemId", jstr(missing.itemId)},
                                         {"reason", jstr(redactText(redaction, missing.reason))}}));
        }
        std::vector<ReportJsonDom> sectionDiags;
        for (const DiagRefEntry& diag : section.diagnostics) {
            sectionDiags.push_back(diagRefDom(diag, redaction));
        }
        std::vector<std::pair<std::string, ReportJsonDom>> sectionMembers;
        sectionMembers.emplace_back("sectionId", jstr(section.sectionId));
        sectionMembers.emplace_back("sectionVersion", jnum(section.sectionVersion));
        sectionMembers.emplace_back("selected", jbool(section.selected));
        sectionMembers.emplace_back("status", jstr(std::string(token(section.status))));
        sectionMembers.emplace_back("entries", jarr(std::move(entries)));
        sectionMembers.emplace_back("missingItems", jarr(std::move(missingItems)));
        sectionMembers.emplace_back(
            "currentness",
            section.currentness.has_value() ? currentnessDom(*section.currentness, redaction)
                                            : ReportJsonDom::nullValue());
        sectionMembers.emplace_back("diagnostics", jarr(std::move(sectionDiags)));
        sectionMembers.emplace_back(
            "eligibilityNote",
            section.eligibilityNote.has_value()
                ? jobj({{"formalPassAllowed", jbool(section.eligibilityNote->formalPassAllowed)},
                        {"reviewRecordAllowed", jbool(section.eligibilityNote->reviewRecordAllowed)},
                        {"note", jstr(redactText(redaction, section.eligibilityNote->note))}})
                : ReportJsonDom::nullValue());
        sectionMembers.emplace_back("renderHint", jstr(std::string(token(section.renderHint))));
        sectionMembers.emplace_back("order", jnum(section.order));
        sections.push_back(jobj(std::move(sectionMembers)));
    }
    const ReportJsonDom sectionsDom = jarr(std::move(sections));

    // ---- review（评审/签署元数据全镜像——§4.5）----
    const ReviewMetadata& review = report.review();
    std::vector<ReportJsonDom> comments;
    for (const ReviewComment& comment : review.comments) {
        comments.push_back(
            jobj({{"author", jstr(redactText(redaction, comment.author))},
                  {"atUtc", jstr(rfc3339Utc(comment.atUtc))},
                  {"text", jstr(redactText(redaction, comment.text))},
                  {"sectionId", comment.sectionId.has_value()
                                    ? jstr(*comment.sectionId)
                                    : ReportJsonDom::nullValue()}}));
    }
    std::vector<std::pair<std::string, ReportJsonDom>> reviewMembers;
    reviewMembers.emplace_back("basisRevision", jstr(review.basisRevision.toCanonical()));
    reviewMembers.emplace_back("basisSnapshot",
                               review.basisSnapshot.has_value()
                                   ? jstr(review.basisSnapshot->toCanonical())
                                   : ReportJsonDom::nullValue());
    reviewMembers.emplace_back("reviewer",
                               review.reviewer.has_value()
                                   ? jstr(redactText(redaction, *review.reviewer))
                                   : ReportJsonDom::nullValue());
    reviewMembers.emplace_back("reviewedAtUtc",
                               review.reviewedAtUtc.has_value()
                                   ? jstr(rfc3339Utc(*review.reviewedAtUtc))
                                   : ReportJsonDom::nullValue());
    reviewMembers.emplace_back("comments", jarr(std::move(comments)));
    reviewMembers.emplace_back(
        "signOff",
        review.signOff.state == SignOffState::State::Signed
            ? jobj({{"state", jstr("signed")},
                    {"signer", jstr(redactText(redaction, review.signOff.signer))},
                    {"signedAtUtc", jstr(rfc3339Utc(review.signOff.signedAtUtc))},
                    {"statementDigest", jstr("sha256-" + digestPrefix12(review.signOff.statementDigest) + "…")}})
            : jobj({{"state", jstr("unsigned")},
                    {"signer", ReportJsonDom::nullValue()},
                    {"signedAtUtc", ReportJsonDom::nullValue()},
                    {"statementDigest", ReportJsonDom::nullValue()}}));
    if (review.variantDiff.has_value()) {
        std::vector<ReportJsonDom> tradeOffs;
        for (const TradeOff& tradeOff : review.variantDiff->tradeOffs) {
            tradeOffs.push_back(
                jobj({{"topic", jstr(redactText(redaction, tradeOff.topic))},
                      {"rationale", jstr(redactText(redaction, tradeOff.rationale))},
                      {"sectionId", tradeOff.sectionId.has_value()
                                        ? jstr(*tradeOff.sectionId)
                                        : ReportJsonDom::nullValue()}}));
        }
        reviewMembers.emplace_back(
            "variantDiff",
            jobj({{"baselineRevision", jstr(review.variantDiff->baselineRevision.toCanonical())},
                  {"candidateRevision", jstr(review.variantDiff->candidateRevision.toCanonical())},
                  {"modelDiffRef", jstr(review.variantDiff->modelDiffRef.toCanonical())},
                  {"baselineCheckResult",
                   jbool(review.variantDiff->baselineCheckResult.consistent)},
                  {"tradeOffs", jarr(std::move(tradeOffs))}}));
    } else {
        reviewMembers.emplace_back("variantDiff", ReportJsonDom::nullValue());
    }
    std::vector<ReportJsonDom> changeLog;
    for (const ReportVersionEntry& entry : review.changeLog) {
        changeLog.push_back(
            jobj({{"reportId", jstr(entry.reportId.toCanonical())},
                  {"reportVersion", jnum(entry.reportVersion)},
                  {"atUtc", jstr(rfc3339Utc(entry.atUtc))},
                  {"actor", jstr(redactText(redaction, entry.actor))},
                  {"reason", jstr(redactText(redaction, entry.reason))}}));
    }
    reviewMembers.emplace_back("changeLog", jarr(std::move(changeLog)));
    const ReportJsonDom reviewDom = jobj(std::move(reviewMembers));

    // ---- 顶层（17 键冻结集——kJsonTopLevelKeys 顺序即装配序）----
    return jobj({{"schemaVersion", jstr(std::string(kJsonSchemaVersion))},
                 {"report", reportDom},
                 {"source", sourceDom},
                 {"identity", identityDom},
                 {"unitPreference", unitPrefDom},
                 {"resultRefs", resultRefsDom},
                 {"evidenceRefs", evidenceRefsDom},
                 {"diagRefs", diagRefsDom},
                 {"currentnessSummary", currentnessSummaryDom},
                 {"coverageSummary", coverageDom},
                 {"externalResourceSummary", externalDom},
                 {"reproduction", reproductionDom},
                 {"sections", sectionsDom},
                 {"review", reviewDom},
                 {"generatedAtUtc", jstr(rfc3339Utc(report.generatedAtUtc()))},
                 {"generatedBy", jstr(redactText(redaction, report.generatedBy()))},
                 {"generatorVersion", jstr(redactText(redaction, report.generatorVersion()))}});
}

/**
 * @brief 顶层未知字段自检（NFR-DEP-04 执行侧——产出物结构上不可携带
 *        冻结键集之外的字段；装配序即键序，自检为内部不变量的显式面）。
 *
 * @return 首个未知键；nullopt＝全部键在冻结集内
 */
std::optional<std::string> firstUnknownJsonTopLevelKey(const ReportJsonDom& dom)
{
    const auto& members = dom.objectMembers();
    for (const auto& [key, value] : members) {
        (void)value;
        const auto found = std::find_if(kJsonTopLevelKeys.begin(), kJsonTopLevelKeys.end(),
                                        [&key](std::string_view frozen) { return frozen == key; });
        if (found == kJsonTopLevelKeys.end()) {
            return key;
        }
    }
    return std::nullopt;
}

}  // namespace

JsonReportRenderer::JsonReportRenderer(const IReportIoFactory& ioFactory,
                                       const diagnostics::IRedactionService& redaction)
    : m_ioFactory(&ioFactory)
    , m_redaction(&redaction)
{
}

RenderOutcome JsonReportRenderer::render(const ReviewReport& report, const FieldMatrix& matrix,
                                         ReportRenderFormat format)
{
    if (format != ReportRenderFormat::Json) {
        ReportError error(ReportErrorCode::Usage,
                          "JsonReportRenderer 仅支持 json 格式，收到 " + std::string(token(format)));
        return RenderOutcome{std::nullopt, std::move(error)};
    }

    try {
        validateRenderPreconditions(report);
        // RPT-T08 规则层（§8.2①/§8.2④——三格式统一，见 HTML 渲染器同位注）。
        validateDeclarationConsistency(report);
        assertWordingFreezeTemplateDiscipline();
        MatrixIndex index(matrix);

        ReportJsonDom dom = buildReportDom(report, index, *m_redaction);
        index.assertFullyConsumed();

        // 未知字段拒绝（NFR-DEP-04 执行侧）——产出侧自检。
        if (const auto unknown = firstUnknownJsonTopLevelKey(dom); unknown.has_value()) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "JSON 顶层出现冻结键集之外的字段：" + *unknown);
        }

        // canonical 写出经注入 io IJsonWriter（键序固定/to_chars/2 空格
        // 缩进/LF——§8.3；reporting 不自建序列化器，SA-12）。
        std::vector<std::uint8_t> buffer;
        MemoryReportOutputTarget target(&buffer);
        auto writer = m_ioFactory->makeJsonWriter();
        if (writer == nullptr || !writer->write(std::move(target), dom)) {
            throw ReportError(ReportErrorCode::RenderFailed, "JSON canonical 写出失败（io 适配）");
        }

        RenderArtifact artifact;
        artifact.format = ReportRenderFormat::Json;
        artifact.bytes = std::move(buffer);
        core::ContentDigester digester;
        digester.update(artifact.bytes.data(), artifact.bytes.size());
        artifact.digest = digester.finalize();
        artifact.rendererVersion = kReportRendererVersion;
        artifact.templateVersion = kJsonTemplateVersion;
        artifact.sourceReportIdentity = report.contentIdentity();
        return RenderOutcome{std::move(artifact), std::nullopt};
    } catch (const ReportError& error) {
        return RenderOutcome{std::nullopt, error};
    }
}

// =====================================================================
// CSV 渲染器（§8.3——#rwcsv1 主表＋诊断/覆盖附表）
// =====================================================================

namespace {

/// 主表冻结列序（§8.3 原文 11 列——列序冻结，只增不改位）。
const std::vector<std::string>& csvMainColumns()
{
    static const std::vector<std::string> kColumns = {
        "field_key", "section_id", "item_label", "value", "unit", "status",
        "qualifier", "result_ref", "evidence_ref", "case_scope", "currentness",
    };
    return kColumns;
}

/// 诊断附表冻结列序（§8.3"code/subject/localName/severity/category/
/// comparison 三要素/occurrences/sourceRun"——comparison 三要素展开为
/// 实际/期望两值＋单位四列，单位为 ComparativeValue 的组成部分）。
const std::vector<std::string>& csvDiagColumns()
{
    static const std::vector<std::string> kColumns = {
        "code", "subject", "local_name", "severity", "category",
        "comparison_actual", "comparison_actual_unit",
        "comparison_expected", "comparison_expected_unit",
        "occurrences", "source_run",
    };
    return kColumns;
}

/// 覆盖附表冻结列序（§8.3"caseId/status/runId"）。
const std::vector<std::string>& csvCoverageColumns()
{
    static const std::vector<std::string> kColumns = {"case_id", "status", "run_id"};
    return kColumns;
}

/// 文本或空字段（空值＝空字段——与"不适用"文本区分，§8.3）。
ReportCsvCell textOrEmpty(const std::optional<std::string>& value)
{
    return value.has_value() ? ReportCsvCell::textCell(*value) : ReportCsvCell::empty();
}

}  // namespace

CsvReportRenderer::CsvReportRenderer(const IReportIoFactory& ioFactory,
                                     const diagnostics::IRedactionService& redaction)
    : m_ioFactory(&ioFactory)
    , m_redaction(&redaction)
{
}

RenderOutcome CsvReportRenderer::render(const ReviewReport& report, const FieldMatrix& matrix,
                                        ReportRenderFormat format)
{
    if (format != ReportRenderFormat::Csv) {
        ReportError error(ReportErrorCode::Usage,
                          "CsvReportRenderer 仅支持 csv 格式，收到 " + std::string(token(format)));
        return RenderOutcome{std::nullopt, std::move(error)};
    }

    try {
        validateRenderPreconditions(report);
        // RPT-T08 规则层（§8.2①/§8.2④——三格式统一，见 HTML 渲染器同位注）。
        validateDeclarationConsistency(report);
        assertWordingFreezeTemplateDiscipline();
        MatrixIndex index(matrix);

        // 单一 CSV 工件＝三份 CSV 文档顺序拼接（见 Render.hpp 类注）——
        // 缓冲为本渲染器局部（零 I/O），文档间以空行（LF）分隔。
        std::vector<std::uint8_t> buffer;
        const auto appendSeparator = [&buffer]() {
            const std::uint8_t lf = '\n';
            buffer.push_back(lf);
        };

        // ---- 主表 report.csv（矩阵序＝§8.4 冻结序；值单源）----
        {
            auto writer = m_ioFactory->makeCsvWriter();
            if (writer == nullptr) {
                throw ReportError(ReportErrorCode::RenderFailed, "CSV 写出器缺位（io 适配）");
            }
            MemoryReportOutputTarget target(&buffer);
            if (!writer->open(std::move(target), /*emitDialectMarker=*/true)) {
                throw ReportError(ReportErrorCode::RenderFailed, "CSV 主表打开失败（io 适配）");
            }
            if (!writer->writeHeader(csvMainColumns())) {
                throw ReportError(ReportErrorCode::RenderFailed, "CSV 主表表头写出失败");
            }
            for (const FieldCell& cell : matrix) {
                // 逐行消费矩阵单元格（同源验证的消费标记——本渲染器的行
                // 序即矩阵序，按 fieldKey 消费即双向完整性的 CSV 半区）。
                index.consume(cell.fieldKey);
                // 矩阵单元格逐行落表（行列对齐键＝fieldKey；行序＝矩阵序
                // ——提取序即 §8.4 冻结序，同报告同字节）。
                std::vector<ReportCsvCell> row;
                row.push_back(ReportCsvCell::textCell(cell.fieldKey));
                row.push_back(ReportCsvCell::textCell(cell.sectionId));
                row.push_back(ReportCsvCell::textCell(cell.entryKey));
                row.push_back(ReportCsvCell::textCell(cell.valueRepr));   // 值列文本规范形
                row.push_back(textOrEmpty(cell.unit));
                row.push_back(textOrEmpty(cell.status));
                std::vector<std::string> qualifierTokens;
                for (const QualifierToken qualifier : cell.qualifier) {
                    qualifierTokens.emplace_back(token(qualifier));
                }
                row.push_back(ReportCsvCell::textCell(joinSemicolon(qualifierTokens)));
                row.push_back(textOrEmpty(cell.resultRef));
                row.push_back(cell.evidenceRef.empty()
                                  ? ReportCsvCell::empty()
                                  : ReportCsvCell::textCell(joinSemicolon(cell.evidenceRef)));
                row.push_back(cell.caseScope.empty()
                                  ? ReportCsvCell::empty()
                                  : ReportCsvCell::textCell(joinSemicolon(cell.caseScope)));
                row.push_back(textOrEmpty(cell.currentness));
                if (!writer->writeRow(row)) {
                    throw ReportError(ReportErrorCode::RenderFailed, "CSV 主表行写出失败");
                }
            }
            if (!writer->finish()) {
                throw ReportError(ReportErrorCode::RenderFailed, "CSV 主表终结失败");
            }
        }
        appendSeparator();

        // ---- 诊断附表 diagnostics.csv（orderKey 序；local_name 过脱敏）----
        {
            auto writer = m_ioFactory->makeCsvWriter();
            if (writer == nullptr) {
                throw ReportError(ReportErrorCode::RenderFailed, "CSV 写出器缺位（io 适配）");
            }
            MemoryReportOutputTarget target(&buffer);
            if (!writer->open(std::move(target), true)) {
                throw ReportError(ReportErrorCode::RenderFailed, "CSV 诊断附表打开失败（io 适配）");
            }
            if (!writer->writeHeader(csvDiagColumns())) {
                throw ReportError(ReportErrorCode::RenderFailed, "CSV 诊断附表表头写出失败");
            }
            for (const DiagRefEntry* entry : orderedDiagRefs(report.diagRefs())) {
                std::vector<ReportCsvCell> row;
                row.push_back(ReportCsvCell::textCell(entry->code));
                row.push_back(entry->subject.has_value()
                                  ? ReportCsvCell::textCell(entry->subject->toCanonical())
                                  : ReportCsvCell::empty());
                row.push_back(entry->localName.has_value()
                                  ? ReportCsvCell::textCell(
                                      redactText(*m_redaction, *entry->localName))
                                  : ReportCsvCell::empty());
                row.push_back(ReportCsvCell::textCell(
                    std::string(diagnostics::severityToken(entry->severity))));
                row.push_back(ReportCsvCell::textCell(
                    std::string(diagnostics::categoryToken(entry->category))));
                if (entry->comparison.has_value()) {
                    const ValueProjection actual = projectComparativeValue(entry->comparison->actual);
                    const ValueProjection expected =
                        projectComparativeValue(entry->comparison->expected);
                    row.push_back(ReportCsvCell::textCell(actual.text));
                    row.push_back(actual.unit.has_value() ? ReportCsvCell::textCell(*actual.unit)
                                                          : ReportCsvCell::empty());
                    row.push_back(ReportCsvCell::textCell(expected.text));
                    row.push_back(expected.unit.has_value()
                                      ? ReportCsvCell::textCell(*expected.unit)
                                      : ReportCsvCell::empty());
                } else {
                    // 非比较型诊断：三要素四列全空字段（与"不适用"区分）。
                    row.emplace_back();
                    row.emplace_back();
                    row.emplace_back();
                    row.emplace_back();
                }
                row.push_back(ReportCsvCell::integerCell(static_cast<std::int64_t>(entry->occurrences)));
                row.push_back(entry->sourceRun.has_value()
                                  ? ReportCsvCell::textCell(entry->sourceRun->toCanonical())
                                  : ReportCsvCell::empty());
                if (!writer->writeRow(row)) {
                    throw ReportError(ReportErrorCode::RenderFailed, "CSV 诊断附表行写出失败");
                }
            }
            if (!writer->finish()) {
                throw ReportError(ReportErrorCode::RenderFailed, "CSV 诊断附表终结失败");
            }
        }
        appendSeparator();

        // ---- 覆盖附表 coverage.csv（列序冻结；逐工况行数据未入 §4 模型
        //      ——本任务交付表头＋方言行表面，零数据行如实登记，见
        //      Render.hpp 类注；不伪造逐工况状态——ERR-01）----
        {
            auto writer = m_ioFactory->makeCsvWriter();
            if (writer == nullptr) {
                throw ReportError(ReportErrorCode::RenderFailed, "CSV 写出器缺位（io 适配）");
            }
            MemoryReportOutputTarget target(&buffer);
            if (!writer->open(std::move(target), true)) {
                throw ReportError(ReportErrorCode::RenderFailed, "CSV 覆盖附表打开失败（io 适配）");
            }
            if (!writer->writeHeader(csvCoverageColumns())) {
                throw ReportError(ReportErrorCode::RenderFailed, "CSV 覆盖附表表头写出失败");
            }
            // 数据行待逐工况覆盖数据入模型后填充（单元卡 §14.4 v0.9 登记）。
            if (!writer->finish()) {
                throw ReportError(ReportErrorCode::RenderFailed, "CSV 覆盖附表终结失败");
            }
        }
        index.assertFullyConsumed();

        RenderArtifact artifact;
        artifact.format = ReportRenderFormat::Csv;
        artifact.bytes = std::move(buffer);
        core::ContentDigester digester;
        digester.update(artifact.bytes.data(), artifact.bytes.size());
        artifact.digest = digester.finalize();
        artifact.rendererVersion = kReportRendererVersion;
        artifact.templateVersion = kCsvTemplateVersion;
        artifact.sourceReportIdentity = report.contentIdentity();
        return RenderOutcome{std::move(artifact), std::nullopt};
    } catch (const ReportError& error) {
        return RenderOutcome{std::nullopt, error};
    }
}

}  // namespace sdurws::ird::reporting
