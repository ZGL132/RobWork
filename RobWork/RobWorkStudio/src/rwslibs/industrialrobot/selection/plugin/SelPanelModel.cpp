/**
 * @file   SelPanelModel.cpp
 * @brief  selection 插件面板模型层流的实现翻译单元（零 Qt——数据归一、
 *         查表与呈现素材合成；零计算红线执行面）。
 *
 * 设计依据：SelPanelModel.hpp 文件头（L-Sx 流清单与错误语义口径）。
 * 零计算红线执行面（卡 §3.4）：本 TU 全部为缝归一、常量词表、字符串
 * 拼接与数值格式化——无任何选型计算符号（契约测试全文词表扫描钉住；
 * 排序唯一在计算库——本 TU 零排序调用，呈现序＝缝给定序）。
 */

#include "SelPanelModel.hpp"

#include <cmath>
#include <cstdio>
#include <utility>

namespace sdurws::ird::selection {

// =====================================================================
// L-S3c 范围外呈现键（wp19-t08 双面一致性——唯一书写点）。
// =====================================================================

std::string selOutOfScopePresentationKey()
{
    // 与域内范围外词表同词形（格 note 与缺口维两承载面同词——呈现键
    // 归一到此单点；文案值归宿主文案资源，UX-02）。
    return "axis-out-of-scope";
}

// =====================================================================
// L-S1 就绪投影合成（纯透传——判定权威在域就绪校验，插件不复制）。
// =====================================================================

SelReadinessRow readinessProjection(const SelModuleSessionState& session)
{
    SelReadinessRow row;
    // 域注册键＝ui.md §6.5 词表值（kSelDomainKey——宿主汇聚对账锚）。
    row.domainKey = kSelDomainKey;
    // 其余字段逐项透传（零加工——第二真值防线的结构承载）。
    row.verdict = session.verdict;
    row.inputComplete = session.inputComplete;
    row.missingItemKeys = session.missingItemKeys;
    row.hasActiveTask = session.hasActiveTask;
    return row;
}

// =====================================================================
// L-S2/L-S3 行集归一（空缝→空行集＋未装配标记——Empty 显式语义）。
// =====================================================================

std::vector<SelCatalogRow> catalogRowsPresented(const SelPanelServices& services,
                                                bool* outNotAssembled)
{
    // 未装配标记先行清零（出参语义：仅"缝空"时置 true——空清单不是
    // 未装配，两态区分呈现）。
    if (outNotAssembled != nullptr) {
        *outNotAssembled = false;
    }
    if (!static_cast<bool>(services.catalogRows)) {
        if (outNotAssembled != nullptr) {
            *outNotAssembled = true;
        }
        return {};  // 未装配→空态（不伪造行，NFR-COR-03）
    }
    return services.catalogRows();  // 透传（呈现序＝组装侧权威序）
}

std::string catalogDisplayText(const SelCatalogRow& row)
{
    // 目录身份词面拼接（"ID 版本"——UX-02 允许的用户可读身份词面；
    // 空字段以"不适用"占位词形承载——ui.md §6.6"不伪造 0/空值"纪律）。
    const std::string idPart =
        row.catalogId.empty() ? std::string{"(不适用)"} : row.catalogId;
    const std::string versionPart =
        row.version.empty() ? std::string{"(不适用)"} : row.version;
    return idPart + " " + versionPart;
}

std::vector<SelCandidateRow> candidateRowsPresented(
    const SelPanelServices& services, bool* outNotAssembled)
{
    if (outNotAssembled != nullptr) {
        *outNotAssembled = false;
    }
    if (!static_cast<bool>(services.candidateRows)) {
        if (outNotAssembled != nullptr) {
            *outNotAssembled = true;
        }
        return {};
    }
    return services.candidateRows();
}

std::vector<SelRejectionRow> rejectionRowsPresented(
    const SelPanelServices& services, bool* outNotAssembled)
{
    if (outNotAssembled != nullptr) {
        *outNotAssembled = false;
    }
    if (!static_cast<bool>(services.rejectionRows)) {
        if (outNotAssembled != nullptr) {
            *outNotAssembled = true;
        }
        return {};
    }
    return services.rejectionRows();
}

std::vector<SelGapRow> gapRowsPresented(const SelPanelServices& services,
                                        bool* outNotAssembled)
{
    if (outNotAssembled != nullptr) {
        *outNotAssembled = false;
    }
    if (!static_cast<bool>(services.gapRows)) {
        if (outNotAssembled != nullptr) {
            *outNotAssembled = true;
        }
        return {};
    }
    return services.gapRows();
}

std::string formatMetric(double value, const std::string& unitToken)
{
    // 非有限数守卫（NaN/Inf 不进用户文本——呈现"不适用"占位；正常
    // 数值路径不受影响）。
    if (!std::isfinite(value)) {
        return "(不适用)";
    }
    // "%g" 最短有效数字形态（6 位有效数字——呈现精度，非计算精度；
    // AT-27：显示形态零回流身份路径）。无单位词面＝纯数值。
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%g", value);
    std::string text = buffer;
    if (!unitToken.empty()) {
        text += " ";
        text += unitToken;
    }
    return text;
}

// =====================================================================
// L-S4 回填提交流（出口缝受理＋会话记录——AT-30 呈现素材合成）。
// =====================================================================

SelBackfillRecord submitBackfill(SelModuleSessionState& session,
                                 const SelPanelServices& services,
                                 const std::string& commandToken)
{
    SelBackfillRecord record;
    record.commandToken = commandToken;

    // 第一步：出口缝空态检查（未装配＝装配缺陷的可见呈现——诚实
    // 反馈，不静默丢弃；拒绝键入会话记录供工作流页清单呈现）。
    if (!static_cast<bool>(services.backfillSubmit)) {
        record.accepted = false;
        record.rejectionKey = kSelRejectOutletMissing;
    } else {
        // 第二步：可用性查询（宿主权威判定——只读模式/前置缺失的
        // 拦截在宿主；可用性缝未装配＝按可用呈现，不本地拦截）。
        const bool available = !static_cast<bool>(services.backfillAvailability)
                               || services.backfillAvailability(commandToken);
        if (!available) {
            record.accepted = false;
            record.rejectionKey = kSelRejectUnavailable;
        } else {
            // 第三步：经出口缝发起（token→宿主命令管线——真实修订/
            // 事务语义归宿主命令服务，本插件零事务知识）。
            services.backfillSubmit(commandToken);
            record.accepted = true;
            // AT-30 复算提示素材（四域全量＋不沿用——呈现素材位；
            // 领域词表唯一书写点在本 TU 顶部头文件常量，与计算库
            // 复算提示域词表的逐字对账由契约测试钉住）。
            record.notice.domainLabelKeys = {kSelRecalcDomainKinematics,
                                             kSelRecalcDomainDynamics,
                                             kSelRecalcDomainSelection,
                                             kSelRecalcDomainOptimization};
            record.notice.retainPriorConclusion = false;
        }
    }

    // 会话记录追加（呈现缓冲——超容量从头丢最旧；截断是呈现语义，
    // 完整修订史归 project）。
    session.recentBackfills.push_back(record);
    while (session.recentBackfills.size() > kSelBackfillRecordCapacity) {
        session.recentBackfills.erase(session.recentBackfills.begin());
    }
    return record;
}

// =====================================================================
// L-S5 文案解析流（UX-02——零哈希泄漏守卫）。
// =====================================================================

namespace {

/**
 * @brief 哈希形态检测（64 位十六进制串——内容摘要词形；UX-02 守卫
 *         的判形谓词。64＝SHA-256 十六进制长度，与域内摘要词面同形）。
 *
 * @param text [in] 待检文本
 * @return true＝文本恰为 64 个十六进制字符（疑似摘要泄漏）
 */
bool isDigestShapedText(const std::string& text)
{
    if (text.size() != 64) {
        return false;
    }
    for (const char c : text) {
        const bool hexDigit =
            (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
            || (c >= 'A' && c <= 'F');
        if (!hexDigit) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string resolvePanelText(const SelPanelServices& services,
                             const std::string& titleKey,
                             bool* outFellBack)
{
    // 兜底标记先行清零（出参语义：仅兜底路径置 true）。
    if (outFellBack != nullptr) {
        *outFellBack = false;
    }
    // 缝空→键名原文兜底（开发态可见缺口——dynamics 同纪律）。
    if (!static_cast<bool>(services.textResolver)) {
        if (outFellBack != nullptr) {
            *outFellBack = true;
        }
        return titleKey;
    }
    const std::string resolved = services.textResolver(titleKey);
    // 哈希形态守卫（摘要泄漏处置——呈现宁可键名也不可哈希）。
    if (isDigestShapedText(resolved)) {
        if (outFellBack != nullptr) {
            *outFellBack = true;
        }
        return titleKey;
    }
    return resolved;
}

}  // namespace sdurws::ird::selection
