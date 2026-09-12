/**
 * @file   Evidence.hpp
 * @brief  证据 Profile 与证明契约承载——证据项状态与证据清单（§6.2）、
 *         RequiredEvidenceProfile 数据结构与通用必需项（§6.1）、确定性
 *         不可行证明与字段级校验器 validateProof（§6.3）、搜索未果记录
 *         （§6.3 末）、多工况覆盖矩阵与区域采样证据校验（§6.6），以及
 *         Verified 模式前置固化校验（§6.2，CON-03）。
 *
 * 设计依据：
 *   - units/evidence.md §6.1（RequiredEvidenceProfile 数据结构——EVI-01/
 *     §8.1 表 4 承载；D-14：Profile 内容归需求表 4、evidence 只拥有结构/
 *     注册/消费/校验）、§6.2（证据项状态五值/证据清单/Verified 前置校验）、
 *     §6.3（证明契约：三类证明＋逐字段校验＋SearchExhaustedRecord）、
 *     §6.6（CaseCoverageMatrix 校验规则＋RegionCoverageEvidence 分母/
 *     降级/零样本）、§9.5（Profile 注册期校验的语法面）、§3.1 组成表
 *     （Evidence.hpp 行）、§12 EV-T05 行（产物＝Evidence.hpp/.cpp）
 *   - 需求 EVI-01（§8.1 表 2 五级优先级/表 4 Profile）、CON-03（外部资源
 *     固化——Verified 前置）、EVI-02（工况全覆盖）、ERR-01（不适用显式
 *     标记/不伪造）、KIN-04 R8（分母/降级/零样本）、NFR-COR-02（校验输出
 *     确定性——issue 顺序稳定）、NFR-COR-03（不静默通过）
 *   - 任务契约 tasks/foundation/EV-T05.json（≙WP-05-T05）acceptance 1～4：
 *     ①EV-VER-6/7、EV-COV-2/3 用例通过；②validateProof 逐字段反例全部
 *     拒绝；③Profile 明细归需求 §8.1 表 4、不复制双账本（D-14）；
 *     ④O-14 未决保守字面——覆盖证据校验按 §6.6 字面实现（覆盖矩阵分母＝
 *     快照冻结态必验工况集，schema 权威归 requirements 卡 WP-14-T01）
 *
 * 背景说明（本头在证据链上的位置——为什么"证据"值得一个独立契约头）：
 *   汇总器（§6.4，EV-T06）给工程判定定级时唯一可以采信的东西是**逐字段
 *   校验过的证据**，而不是"评估器说它做完了"的单一状态声明（§1.1 声明
 *   的落地）。本头交付三件武器：
 *   ①Profile（§6.1）——"这个域**应该**有什么证据"的登记面（明细归需求
 *     表 4，evidence 只承载结构与通用必需项——D-14 防双账本）；
 *   ②证据清单与证明（§6.2/§6.3）——"实际拿到的证据长什么样、什么算
 *     合格"的校验面：证明必须逐字段齐备（validateProof），证据项状态
 *     五值各带 presence 纪律（Satisfied 必带产物摘要、Invalid 必带原因
 *     诊断、NotApplicable 必带原因——ERR-01 不伪造）；
 *   ③覆盖证据（§6.6）——"必验工况是否都跑过、区域覆盖率分母是否完整"
 *     的核对面（EVI-02/KIN-04）。
 *   五级决策表（§6.4）消费本头的校验结果定级——本头只判定"证据合不合格"，
 *   不判定"工程可不可行"（后者归 EV-T06）。
 *
 * 实现形态说明：值类型为聚合结构（公共成员＋成对 ==/!=，无 setter——
 * EV-T02 DependencyEntry/EV-T03 AnalysisSnapshot 同款纪律）；校验器为
 * 非抛出纯函数（返回 issue 清单，可恢复查询路径——§2.1"try* 分轨"先例），
 * 实现集中在 src/Evidence.cpp（本头只放契约与纯声明＋轻量 inline 词形
 * 闸门）。issue 顺序＝检查序（固定），同一坏输入必得同一 issue 序列
 * （NFR-COR-02，测试逐条断言可用下标）。
 *
 * 消费的 core 契约（core.md v0.1 基线——P-EV-1 状态锚点）：
 *   ObjectId/RunId（core Identity.hpp）、ContentIdentity/Digest256/
 *   ContentDigester（core Digest.hpp——Profile 身份摘要唯一经它，CR-02）、
 *   EvaluationMode（core Evaluation.hpp——Verified 前置校验的模式入参）、
 *   DiagnosticRecord（core DiagData.hpp——Invalid 证据的原因诊断承载）。
 *
 * 线程安全：本头全部实体为纯值/纯函数（无共享可变状态），并发只读安全；
 * 证明/证据/覆盖结构冻结后按只读值对待（消费方为 EV-T06 汇总器）。
 */

#ifndef SDURWS_IRD_EVIDENCE_EVIDENCE_HPP
#define SDURWS_IRD_EVIDENCE_EVIDENCE_HPP

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Dependency.hpp>
#include <sdurws/ird/evidence/Errors.hpp>
#include <sdurws/ird/evidence/Slice.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sdurws::ird::evidence {

// =====================================================================
// RequiredEvidenceProfile 数据结构（§6.1——EVI-01/§8.1 表 4 承载）
// =====================================================================

/**
 * @brief 证据项类别（§6.1 原文三值）。
 *
 * Common＝通用必需（表 4"通用必需项"，evidence 内建 commonRequiredItems()，
 * 域 Profile 不得重复登记——单一权威，NFR-MNT-03）；Required＝域必需
 * （表 4 各行"必需证据项"，适用且缺失→DataInsufficient）；Suggested＝域
 * 建议（缺失不阻断，单独标注）。
 */
enum class EvidenceItemClass : std::uint8_t {
    Common,    ///< 通用必需（evidence 内建；域登记 Common 类即注册拒绝——§6.1 注册行）
    Required,  ///< 域必需（适用且缺失→DataInsufficient——表 4 各行）
    Suggested, ///< 域建议（缺失不阻断，单独标注——表 4 各行）
};

/**
 * @brief Profile 项的适用条件（§6.1：{conditionToken, referencedKeys[]}）。
 *
 * 实现口径（登记单元卡 v0.6）：与 §4.2.1 ApplicabilityCondition 同形同义
 * （条件 token 归域登记＋决定条件的依赖键集），直接复用 Dependency.hpp
 * 的类型——同一词形两处出现（切片声明侧/Profile 侧）只维护一份语法契约，
 * 防漂移。"无条件＝Always"的承载：EvidenceProfileItem::applicability 为
 * nullopt（presence 语义，§5.2"optional 缺失与空值不等价"同源）。
 *
 * 值语义；线程安全：纯值。
 */
using Applicability = ApplicabilityCondition;

/**
 * @brief 判断 Profile 项 id 是否符合 "<域>.<项>" 词形（§6.1 itemId 注释）。
 *
 * 实现口径（登记单元卡 v0.6）：设计给出形状 "<域>.<项>" 与示例
 * "kin.ik-convergence-per-point"，未冻结字符级语法；本闸门按示例归纳为
 * **恰一个点、两段各为 kebab token**（[a-z][a-z0-9-]{0,62}）——与依赖键
 * （允许多点）不同：itemId 是"域内唯一项名"的两段式，多点会使域/项边界
 * 歧义。域/项 token 词表归需求表 4/各域（N-5），本闸门只做词形下限。
 *
 * @param itemId [in] 待检项 id（如 "trj.ik-continuity-per-cartesian-segment"）
 * @return 词形合法 true；空串/无点/多点/段空/段首非小写/词表外字符 false
 *
 * 确定性：纯函数、无 locale 依赖（NFR-COR-02）。
 */
inline bool isValidProfileItemId(std::string_view itemId) noexcept
{
    // 形状闸门：恰一个点（两段式"域.项"——多点/无点均非两段式）。
    const std::size_t dot = itemId.find('.');
    if (dot == std::string_view::npos || itemId.find('.', dot + 1) != std::string_view::npos) {
        return false;
    }
    // 两段各自 kebab：首字符小写字母、其余 [a-z0-9-]、长度 1~63。
    const auto kebab = [](std::string_view part) {
        if (part.empty() || part.size() > 63) {
            return false;
        }
        if (part[0] < 'a' || part[0] > 'z') {
            return false;
        }
        for (const char c : part.substr(1)) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
            if (!ok) {
                return false;
            }
        }
        return true;
    };
    return kebab(itemId.substr(0, dot)) && kebab(itemId.substr(dot + 1));
}

/**
 * @brief 判断 Profile id 是否为表 4 的五个评估域之一（§6.1 profileId 注释：
 *        "kin"|"trj"|"dyn"|"sel"|"opt"）。
 *
 * @param profileId [in] 待检域 id
 * @return 属五域词表 true
 *
 * 说明：profileId 词表**封闭**（需求 §8.1 表 4 冻结的五域——运动学/轨迹/
 * 动力学/选型/优化；P-05 冻结产物），与开放的域 token 不同：Profile 按
 * "评估域"整域登记，新增域＝需求表 4 变更（走需求变更流程并同步本闸门）。
 */
inline bool isDomainProfileId(std::string_view profileId) noexcept
{
    return profileId == "kin" || profileId == "trj" || profileId == "dyn"
        || profileId == "sel" || profileId == "opt";
}

/**
 * @brief 单条 Profile 项（§6.1 EvidenceProfileItem 字段表）。
 *
 * D-14 纪律：本结构只承载"一行证据项长什么样"（id/类别/人读说明/适用
 * 条件/替代标志），**各域的具体行明细归 REQUIREMENTS §8.1 表 4**——
 * evidence 不内建任何域 Profile 实例（域按表 4 逐行实例化后注册，§13
 * 交接），不复制明细防双账本。
 *
 * 值语义；线程安全：纯值。
 */
struct EvidenceProfileItem {
    /// 稳定 id："<域>.<项>"（isValidProfileItemId 词形），如
    /// "kin.ik-convergence-per-point"（§6.1 原文示例）。
    std::string itemId;
    /// 三类之一（Common 类禁止域登记——§6.1 注册行/§9.5）。
    EvidenceItemClass itemClass = EvidenceItemClass::Required;
    /// 人读说明（锚定表 4 行文——内容权威在需求侧，本字段是登记时抄录的
    /// 展示文案，非身份判定语义；禁 NUL——编码安全下限）。
    std::string description;
    /// 适用条件：nullopt＝Always（无条件）；有值时条件不满足的项在证据
    /// 核对中显式标记 NotApplicable、不计缺失（C2/ERR-01——EV-VER-7）。
    std::optional<Applicability> applicability;
    /// 替代标志（C2/C6）：true＝成功产物类——存在有效不可行证明时按
    /// "因不可行而不适用"记录、不计缺失；false＝通用门禁类——不豁免。
    /// 一致性约束（§6.1 注册行）：Common 类必须为 false（C6 通用门禁
    /// 不被替代规则豁免）；域项真假由域按表 4 行文申报（哪些行属"成功
    /// 产物类"是域知识，evidence 不域判）。
    bool substitutableByInfeasibility = false;

    bool operator==(const EvidenceProfileItem& o) const
    {
        return itemId == o.itemId && itemClass == o.itemClass
            && description == o.description && applicability == o.applicability
            && substitutableByInfeasibility == o.substitutableByInfeasibility;
    }
    bool operator!=(const EvidenceProfileItem& o) const { return !(*this == o); }
};

/**
 * @brief 域必需证据 Profile（§6.1 RequiredEvidenceProfile——按评估域登记）。
 *
 * 生命周期：域评估器注册前先注册 Profile（§13 接入顺序约束："任何业务
 * 评估器注册前，其 Profile 必须先注册"）；注册时 contentIdentity 由
 * evidence 计算（域不可申报——computeProfileContentIdentity），重复
 * (profileId, version) 注册拒绝归 EvidenceProfileRegistry（§9.5，EV-T10）。
 *
 * envelope.evidence 绑定 profileId+version+contentIdentity 三元组（§6.1
 * 表"Profile 版本与内容身份参与追溯/兼容"行）：profile 内容变化→新
 * contentIdentity→旧结果对同 profile 请求不可直接复用（CON-04/§8.2 消费）。
 *
 * 值语义；线程安全：纯值（注册后按不可变对待）。
 */
struct RequiredEvidenceProfile {
    /// 评估域 id（isDomainProfileId 五域词表："kin"|"trj"|"dyn"|"sel"|"opt"）。
    std::string profileId;
    /// 域登记版本（语义化版本串；非空＋无 NUL——域演进其 Profile 时升版）。
    std::string version;
    /// Profile 内容身份（SHA-256；注册时由 evidence 对规范编码计算——
    /// 非调用方申报值；本字段是注册器写入的落位承载）。
    core::ContentIdentity contentIdentity;
    /// 域必需项（表 4 各行"必需证据项"逐行实例化——明细权威在需求侧）。
    std::vector<EvidenceProfileItem> required;
    /// 域建议项（表 4 各行"建议证据项"；缺失不阻断、单独标注）。
    std::vector<EvidenceProfileItem> suggested;

    bool operator==(const RequiredEvidenceProfile& o) const
    {
        return profileId == o.profileId && version == o.version
            && contentIdentity == o.contentIdentity && required == o.required
            && suggested == o.suggested;
    }
    bool operator!=(const RequiredEvidenceProfile& o) const { return !(*this == o); }
};

/**
 * @brief 通用必需项（§6.1：evidence 内建 commonRequiredItems()）。
 *
 * 表 4"通用必需项"原文三行的结构化承载（**任何 Verified 评估隐式附加**，
 * 域 Profile 不得重复登记——单一权威）：
 *   ①完整 AnalysisSnapshot 身份（含已解析策略与 RuntimeNameMap 内容身份
 *     ——CON-06）；
 *   ②必验工况覆盖矩阵（EVI-02）；
 *   ③评估模式与证据等级标识。
 *
 * D-14 边界：本函数只承载**通用**三行（表 4 通用必需项原文）——各域明细
 * 行（运动学/轨迹/动力学/选型/优化）不在此处、也**不应**在任何 evidence
 * 代码中出现（域按表 4 自行实例化注册，§13 交接）；三个 itemId 的
 * "common" 域前缀为 evidence 侧稳定键（登记单元卡 v0.6），内容权威仍归
 * 需求表 4。
 *
 * @return 三条 Common 项（每次调用新构造——纯函数；全部 Common 类、
 *         substitutableByInfeasibility==false〔C6 通用门禁不豁免〕、
 *         无适用条件〔恒适用〕）
 *
 * 线程安全：可重入纯函数（无共享状态）。
 */
std::vector<EvidenceProfileItem> commonRequiredItems();

// =====================================================================
// Profile 注册期校验（§6.1 注册行＋§9.5 语法面——交叉校验归评估器注册时）
// =====================================================================

/**
 * @brief Profile 注册期校验问题码。
 *
 * 范围＝§9.5 原文划定的 Profile 注册期语法面："Profile 条件的
 * referencedKeys **语法校验**在 Profile 注册时做，与评估器声明的交叉校验
 * 在评估器注册时做"——即本表只含 item 语法/条件词形/替代标志与 itemClass
 * 一致性/Common 禁止域登记（§6.1 注册行四组）；referencedKeys 与评估器
 * 声明键/快照事实键的**闭包**核对不在此处（EV-T10 评估器注册时做）。
 * 值一经交付不得改动/插入，只能表尾追加并留痕。
 */
enum class ProfileIssueCode : std::uint8_t {
    ProfileIdInvalid,           ///< profileId 不在五域词表（§6.1 原文 "kin"|"trj"|"dyn"|"sel"|"opt"）
    ProfileVersionInvalid,      ///< version 空/含 NUL（语义化版本串下限）
    ItemIdSyntax,               ///< itemId 词形违约（"<域>.<项>"——isValidProfileItemId）
    ItemDescriptionInvalid,     ///< description 空/含 NUL（人读说明是登记契约的一部分——表 4 行文锚定）
    CommonItemInDomainProfile,  ///< Common 类禁止域登记（§6.1 注册行——单一权威）
    SubstitutableFlagInconsistent, ///< Common 类替代标志必须 false（C6：通用门禁不被替代豁免）
    ApplicabilityTokenInvalid,  ///< conditionToken 空/含 NUL（isWellFormedToken 下限）
    ReferencedKeysEmpty,        ///< referencedKeys 空集（条件不由任何键决定——永不重解析）
    ReferencedKeySyntax,        ///< referencedKey 语法非法（isValidDependencyKey——§9.5 语法校验）
};

/**
 * @brief 单条 Profile 校验问题（可定位：涉事 itemId＋合并序列下标＋中文说明）。
 *
 * index 语义：涉事项在 required∪suggested 合并序列中的下标（先 required
 * 后 suggested）；Profile 级问题取 npos。issues 顺序＝Profile 级问题在前、
 * 项级按合并序列×检查序（确定性——NFR-COR-02）。
 */
struct ProfileIssue {
    /// 集合级问题的 index 保留值（"无涉事下标"）。
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    ProfileIssueCode code;   ///< 机器可判别问题码
    std::string itemId;      ///< 涉事项 id（Profile 级问题为空串）
    std::size_t index = npos; ///< 涉事项下标（合并序列；Profile 级＝npos）
    std::string message;     ///< 中文开发诊断（定位字段与原因）
};

/**
 * @brief Profile 注册期校验器（§6.1 注册行＋§9.5 语法面的纯函数承载）。
 *
 * 规则集（全部来自设计原文，顺序＝检查序）：
 *   1. Profile 级：profileId ∈ 五域词表；version 非空＋无 NUL；
 *   2. 项级（required 与 suggested 合并序列逐项）：
 *      a. itemId 词形（isValidProfileItemId）；
 *      b. description 非空＋无 NUL（表 4 行文锚定——空说明＝登记不完整）；
 *      c. Common 类禁止域登记（Common 项唯一来源＝commonRequiredItems）；
 *      d. 替代标志与 itemClass 一致性：Common ⇒ substitutable 必为 false
 *         （C6 通用门禁不豁免）；域项真假不判（域知识，§6.1 字段注释）；
 *      e. 适用条件词形：conditionToken 非空＋无 NUL、referencedKeys 非空
 *         且每键过 isValidDependencyKey（§9.5——闭包核对归评估器注册时）。
 *
 * @param profile [in] 待检 Profile（域实例化后的注册候选；调用方持有，
 *                本函数不修改）
 * @return 问题清单（空＝通过；顺序确定——NFR-COR-02）
 *
 * 复杂度：O(I×R)，I＝项数、R＝平均 referencedKeys 数（注册期一次性）。
 *
 * 线程安全：可重入纯函数。
 */
std::vector<ProfileIssue> validateEvidenceProfile(const RequiredEvidenceProfile& profile);

/**
 * @brief 计算 Profile 内容身份（§6.1："contentIdentity 由 evidence 计算
 *        （canonical 编码摘要）"——域不可申报）。
 *
 * 实现口径（登记单元卡 v0.6）：对 Profile 的 canonical 身份投影编码计算
 * SHA-256——magic "IRDPRF1"＋codec 版本字节＋profileId/version/required/
 * suggested 逐字段规范编码；required 与 suggested 各自按 itemId 字典序
 * 规范化（同内容任意条目序同身份，NFR-COR-02）；字符串长度前缀大端、
 * optional 适用条件带 presence 字节（缺失≠空——§5.2 同源纪律）。编码为
 * **非往返载体**（SliceCodec baseline-projection 同款纪律——身份投影不是
 * 数据交换格式，无 parse），唯一消费方式＝本函数摘要（CR-02：摘要唯一经
 * core::ContentDigester）。
 *
 * @param profile [in] 待计算 Profile（不做语义校验——校验归
 *                validateEvidenceProfile/注册器；本函数对任意字节内容
 *                给出确定身份，垃圾入给出确定身份出、由注册门禁拦截）
 * @return Profile 内容身份（同内容恒同身份；内容任一变化→身份变化——
 *         "profile 内容变化→旧结果不可直接复用"的凭据，§8.2 消费）
 *
 * 复杂度：O(n log n)（排序，n＝项数）＋O(编码长度)（摘要）。
 *
 * 线程安全：可重入纯函数。
 */
core::ContentIdentity computeProfileContentIdentity(const RequiredEvidenceProfile& profile);

// =====================================================================
// 证据项状态与证据清单（§6.2）
// =====================================================================

/**
 * @brief 证据项状态五值（§6.2 表——P-EV-8：实现承载词表，非需求级冻结
 *        契约；需求侧未来定义枚举则增量对齐，O-13 保守字面）。
 *
 * 判定后果（§6.2 原文）：Satisfied＝满足；Missing/Invalid/Unverified＝
 * 不满足（Missing 全量列出、Invalid 附原因诊断、Unverified 诊断区别于
 * Missing——如 Quick 产物用于 Verified 判定，表 1"Quick 不得单独支撑
 * 正式通过"）；NotApplicable＝**不计缺失**（C2/ERR-01——原因必填）。
 */
enum class EvidenceItemStatus : std::uint8_t {
    Satisfied,     ///< 产物存在且通过绑定校验（摘要/快照/工况/对象范围/产生者）
    Missing,       ///< 无产物——不满足，全量列出（不因首个缺失短路，表 2 ④）
    Invalid,       ///< 产物存在但绑定校验失败——不满足，附 invalidReason 诊断（ERR-01 不伪造）
    Unverified,    ///< 产物存在但未在满足正式要求的条件下验证（P-EV-8 实现承载；
                   ///  Quick 产物用于 Verified 判定＝本态——表 1）
    NotApplicable, ///< 适用条件不满足，显式标记——不计缺失，notApplicableReason 必填
};

/**
 * @brief 单条证据项（§6.2 EvidenceItem 字段表）。
 *
 * presence 纪律（§6.2 原文，validateEvidenceItems 逐条执行）：
 *   - status==Satisfied ⇒ artifactDigest 必填（产物摘要——绑定校验锚）；
 *   - status==Invalid ⇒ invalidReason 必填（ERR-01：不伪造、留原因诊断）；
 *   - status==NotApplicable ⇒ notApplicableReason 必填（C2/ERR-01：
 *     "不适用显式标记"——EV-VER-7 的观测面）；
 *   - Missing/Unverified 无附加必填（无产物/未验证——没有可填的凭据）。
 *
 * 证据一律引用 ObjectId（CON-06 反解口径：运行时名称仅作辅助显示，不在
 * 本结构——名称反解归消费方经 runtime⑥端口）。
 *
 * 值语义；线程安全：纯值。
 */
struct EvidenceItem {
    /// 证据项 id（对应 Profile 项的 itemId——"<域>.<项>" 词形同闸门）。
    std::string itemId;
    /// 五值状态（见枚举注释——各态 presence 纪律）。
    EvidenceItemStatus status = EvidenceItemStatus::Missing;
    /// 产物摘要（Satisfied 必填且非零——Digest256 保留值为空）。
    std::optional<core::Digest256> artifactDigest;
    /// 工况范围（可选——单工况证据逐工况记录；每 id 必须在快照冻结必验
    /// 工况集内——绑定校验见 validateEvidenceManifestBinding，防错误工况
    /// 引用，EV-COV-2 同源）。
    std::optional<std::vector<CaseId>> caseScope;
    /// 对象范围（可选——证据针对的对象；必须在快照 objectClosure 内）。
    std::optional<core::ObjectId> subject;
    /// 不适用原因（NotApplicable 必填且非空——C2/ERR-01）。
    std::optional<std::string> notApplicableReason;
    /// 无效原因（Invalid 必填——稳定诊断记录；诊断码值权威归 diagnostics
    /// StableCodeRegistry，本处承载记录本体，PA-1）。
    std::optional<core::DiagnosticRecord> invalidReason;

    bool operator==(const EvidenceItem& o) const
    {
        return itemId == o.itemId && status == o.status
            && artifactDigest == o.artifactDigest && caseScope == o.caseScope
            && subject == o.subject && notApplicableReason == o.notApplicableReason
            && invalidReason == o.invalidReason;
    }
    bool operator!=(const EvidenceItem& o) const { return !(*this == o); }
};

/**
 * @brief 证据清单（§6.2 EvidenceManifest——逐项证据与快照/切片/Profile
 *        绑定的登记面）。
 *
 * §6.2 原文："EvidenceManifest 记录 snapshotId/sliceId/profile 身份，逐项
 * 证据的 caseScope ⊆ 快照 caseSet、subject ∈ 快照 objectClosure（汇总器
 * 校验，防错误工况/对象引用——EV-COV-2）"。Profile 身份三元组
 * （profileId+version+contentIdentity）即 envelope.evidence 的绑定面
 * （§6.1 表——缓存/复用兼容判定按它比对，§8.2）。
 *
 * 值语义；线程安全：纯值（汇总输入，构造后按只读对待）。
 */
struct EvidenceManifest {
    core::ContentIdentity snapshotId;  ///< 证据所属快照（须与被汇总结果一致——非零）
    SliceId sliceId;                   ///< 证据所属切片（§6.2 原文记录面——非零）
    std::string profileId;             ///< 绑定 Profile 域 id（isDomainProfileId 五域词表）
    std::string profileVersion;        ///< 绑定 Profile 版本（非空＋无 NUL）
    core::ContentIdentity profileContentIdentity; ///< 绑定 Profile 内容身份（非零）
    /// 逐项证据（对应 Profile 项的产出状态；状态语义见 EvidenceItemStatus）。
    std::vector<EvidenceItem> items;

    bool operator==(const EvidenceManifest& o) const
    {
        return snapshotId == o.snapshotId && sliceId == o.sliceId
            && profileId == o.profileId && profileVersion == o.profileVersion
            && profileContentIdentity == o.profileContentIdentity && items == o.items;
    }
    bool operator!=(const EvidenceManifest& o) const { return !(*this == o); }
};

/**
 * @brief 证据项 presence 纪律校验问题码（§6.2 各态必填面）。
 * 值一经交付不得改动/插入，只能表尾追加并留痕。
 */
enum class EvidenceItemIssueCode : std::uint8_t {
    ItemIdSyntax,               ///< itemId 词形违约（"<域>.<项>"——isValidProfileItemId）
    SatisfiedDigestMissing,     ///< Satisfied 必填 artifactDigest（缺失或全零保留值）
    InvalidReasonMissing,       ///< Invalid 必填 invalidReason（ERR-01：不伪造、留原因）
    NotApplicableReasonMissing, ///< NotApplicable 必填非空 notApplicableReason（C2/ERR-01）
};

/**
 * @brief 单条证据项校验问题（可定位：涉事 items 下标＋中文说明）。
 */
struct EvidenceItemIssue {
    /// 集合级问题的 index 保留值（本校验器无集合级问题——保留对齐）。
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    EvidenceItemIssueCode code; ///< 机器可判别问题码
    std::string itemId;         ///< 涉事证据项 id
    std::size_t index = npos;   ///< 涉事项在 items 中的下标
    std::string message;        ///< 中文开发诊断
};

/**
 * @brief 证据项 presence 纪律校验器（§6.2 各态必填面的纯函数承载）。
 *
 * 规则集（顺序＝检查序，逐项独立——一次看全全部问题）：
 *   1. itemId 词形（与 Profile 项同闸门——证据按 itemId 对应 Profile 项）；
 *   2. Satisfied ⇒ artifactDigest 存在且非全零（保留值摘要＝无凭据的
 *      "已满足"——伪造面，NFR-COR-03）；
 *   3. Invalid ⇒ invalidReason 存在（ERR-01：绑定校验失败必须留原因诊断）；
 *   4. NotApplicable ⇒ notApplicableReason 存在且非空（C2：不适用显式
 *      标记——EV-VER-7 观测面）。
 *
 * @param items [in] 待检证据项集（清单候选；调用方持有，本函数不修改）
 * @return 问题清单（空＝全部通过；顺序＝输入顺序×检查序，确定性）
 *
 * 线程安全：可重入纯函数。
 */
std::vector<EvidenceItemIssue> validateEvidenceItems(const std::vector<EvidenceItem>& items);

/**
 * @brief 清单绑定校验问题码（§6.2 绑定面——防错误工况/对象引用）。
 * 值一经交付不得改动/插入，只能表尾追加并留痕。
 */
enum class ManifestBindingIssueCode : std::uint8_t {
    ManifestIdentityInvalid,   ///< 清单身份面非法（snapshotId/sliceId/profile 三元组保留值或词形违约）
    CaseScopeNotInSnapshot,    ///< caseScope 含快照冻结必验工况集外的工况（错误工况引用——EV-COV-2 同源）
    SubjectNotInObjectClosure, ///< subject 不在快照 objectClosure 内（错误对象引用）
};

/**
 * @brief 单条清单绑定问题（可定位：涉事 items 下标＋中文说明）。
 */
struct ManifestBindingIssue {
    /// 集合级问题的 index 保留值（清单身份面问题＝npos）。
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    ManifestBindingIssueCode code; ///< 机器可判别问题码
    std::string itemId;            ///< 涉事证据项 id（清单级问题为空串）
    std::size_t index = npos;      ///< 涉事项在 items 中的下标（清单级＝npos）
    std::string message;           ///< 中文开发诊断
};

/**
 * @brief 证据清单对快照的绑定校验（§6.2："逐项证据的 caseScope ⊆ 快照
 *        caseSet、subject ∈ 快照 objectClosure（汇总器校验，防错误工况/
 *        对象引用——EV-COV-2）"的纯函数承载）。
 *
 * 消费时机：EV-T06 汇总器在采信清单前调用（防评估器引用错误工况/对象
 * 的证据）；本函数只核对引用闭包，不判证据状态对错（状态归
 * validateEvidenceItems＋汇总决策表）。
 *
 * @param manifest [in] 待检清单（评估产出；调用方持有，本函数不修改）
 * @param snapshot [in] 冻结快照（绑定事实来源——必验工况集与对象闭包）
 * @return 问题清单（空＝绑定成立；顺序＝清单级在前、项级按输入序，确定性）
 *
 * 线程安全：可重入纯函数。
 */
std::vector<ManifestBindingIssue>
validateEvidenceManifestBinding(const EvidenceManifest& manifest, const AnalysisSnapshot& snapshot);

/**
 * @brief 完备性核对结果（§6.2 证据清单对 Profile 的完备性核对——
 *        EvidenceMissing 码面的数据源）。
 *
 * requiredGaps＝必需项不满足清单（**全量列出、不因首个缺失短路**——表 2
 * ④）；suggestedGaps＝建议项缺失清单（缺失不阻断、单独标注——表 4）。
 * NotApplicable 项**不出现在任何清单**（不计缺失——C2/ERR-01，EV-VER-7
 * 的判定面："整体不因该缺项降级"由此保证）。
 */
struct EvidenceCompletenessResult {
    /// 必需项不满足（itemId＋其证据状态：Missing/Invalid/Unverified——
    /// 汇总层据此列缺失项并区分诊断）。
    struct Gap {
        std::string itemId;                              ///< 不满足的 Profile 项 id
        EvidenceItemStatus status = EvidenceItemStatus::Missing; ///< 不满足原因态
    };
    std::vector<Gap> requiredGaps;      ///< 必需项不满足清单（全量、保序）
    std::vector<std::string> suggestedGaps; ///< 建议项缺失 id（单独标注、不阻断）
};

/**
 * @brief 证据清单对 Profile 的完备性核对（§6.2"证据清单对
 *        RequiredEvidenceProfile 的完备性核对"的纯函数承载）。
 *
 * 规则（§6.2 逐值后果表）：
 *   - Profile.required 逐项查找清单（按 itemId 首个匹配——清单构造方
 *     保证 id 唯一，重复项为构造方违约、此处不判定）：
 *     未出现→Missing；状态 Satisfied→满足；NotApplicable→**不计缺失**
 *     （EV-VER-7）；Missing/Invalid/Unverified→不满足入 requiredGaps；
 *   - Profile.suggested 逐项：未出现或不满足→suggestedGaps（不阻断），
 *     NotApplicable 同样不计；
 *   - 通用必需项不在本核对（§6.1："任何 Verified 评估隐式附加"——其
 *     门禁在汇总②级，EV-T06；D-14：域明细不进 evidence，本函数也只
 *     消费域传入的 Profile 结构）。
 *
 * @param profile  [in] 域 Profile（期望面；调用方持有）
 * @param manifest [in] 证据清单（实际面；调用方持有）
 * @return 完备性结果（requiredGaps 空＝必需证据齐备〔就适用者而言〕）
 *
 * 线程安全：可重入纯函数。
 */
EvidenceCompletenessResult checkEvidenceCompleteness(const RequiredEvidenceProfile& profile,
                                                     const EvidenceManifest& manifest);

// =====================================================================
// Verified 模式前置校验（§6.2——CON-03/PM-01；快照有效性的一部分）
// =====================================================================

/**
 * @brief 模式前置校验问题码（§6.2 validateForMode(Verified) 两要求）。
 * 值一经交付不得改动/插入，只能表尾追加并留痕。
 */
enum class SnapshotModeIssueCode : std::uint8_t {
    ExternalResourceNotSolidified, ///< Verified 模式存在 state==Recorded 的被消费外部资源（CON-03：未固化即阻断正式结论——证据不足口径）
    ReproductionIncomplete,        ///< 复现块版本要素不完整（productVersion/evidenceContractVersion 空——NFR-COR-02/RPT-03）
};

/**
 * @brief 单条模式前置问题（可定位：涉事 externalResources 下标）。
 */
struct SnapshotModeIssue {
    /// 非资源条目问题的 index 保留值（复现块问题＝npos）。
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    SnapshotModeIssueCode code; ///< 机器可判别问题码
    std::size_t index = npos;   ///< 涉事外部资源下标（复现块问题＝npos）
    std::string message;        ///< 中文开发诊断
};

/**
 * @brief 快照的模式前置校验（§6.2 validateForMode 的纯函数承载）。
 *
 * 规则（§6.2 原文）：
 *   - mode==Verified：被消费外部资源**全部** state==Solidified（CON-03/
 *     PM-01：未固化即阻断正式结论，证据不足口径——命中返回
 *     ExternalResourceNotSolidified，逐条列出涉事资源）；**加**复现块
 *     版本要素完整性（productVersion/evidenceContractVersion 非空——
 *     builder 已挡，此处对手工构造的快照值复核，防线不因调用路径而缺）。
 *   - mode==Quick/Preview：**不强制固化**（其产物本就不支撑正式结论——
 *     表 1）；复现块完整性仍查（快照有效性通用面，与模式无关）。
 *
 * @param snapshot [in] 待检快照（冻结值；调用方持有）
 * @param mode     [in] 目标评估模式（core Evaluation.hpp 词表）
 * @return 问题清单（空＝前置满足；顺序＝资源条目按输入序、复现块最后，
 *         确定性——NFR-COR-02）
 *
 * 消费时机：②级通用证据门禁的组成校验（§6.4.1"快照身份……Verified
 * 前置固化校验"——门禁装配归 EV-T06 汇总器；本函数为其可复用原语）。
 *
 * 线程安全：可重入纯函数。
 */
std::vector<SnapshotModeIssue> validateSnapshotForMode(const AnalysisSnapshot& snapshot,
                                                       core::EvaluationMode mode);

// =====================================================================
// 确定性不可行证明契约（§6.3——表 2 ③/C5/C8 承载）
// =====================================================================

/**
 * @brief 证明类别（§6.3 原文三类——C5/C8：数值搜索未果**不是**证明，
 *        走 SearchExhaustedRecord→DataInsufficient 口径）。
 */
enum class ProofCategory : std::uint8_t {
    AnalyticBound,            ///< 解析界限（如"目标距离 > Σ连杆长"——覆盖全部可能解）
    ConstraintContradiction,  ///< 约束矛盾（必验工况与关节限位/碰撞几何的显式矛盾——对任务定义成立）
    MandatoryStateCollision,  ///< 必经状态碰撞（任务强制且不可选择的状态被证实碰撞——C8 任务级作用域）
};

/**
 * @brief 必经状态描述（§6.3 MandatoryStateDescriptor——
 *        MandatoryStateCollision 类证明的条件必填字段）。
 *
 * 实现口径（登记单元卡 v0.6，O-13 保守字面）：stateKind 以**域登记
 * token** 承载（设计词形 "TaskPointConfig|StartPoint|EndPoint|…"——省略号
 * 表开放词表，词表权威归需求/域；evidence 只做"非空＋无 NUL"编码安全
 * 下限，不私裁封闭枚举）；必经性依据（"为什么不可选择"）为必填非空串
 * ——它是 validateProof 字段级校验的对象（缺一即 Invalid，EV-VER-6）。
 *
 * 值语义；线程安全：纯值。
 */
struct MandatoryStateDescriptor {
    /// 必经状态种类 token（域登记词表：TaskPointConfig|StartPoint|EndPoint|…）。
    std::string stateKind;
    /// 为什么不可选择（必经性依据——非空；如"任务点构型是任务定义强制
    /// 要求的末端状态，无替代选择"）。
    std::string nonSelectabilityBasis;
    /// 必经状态所属对象（任务点/起点/终点等——须非零保留值）。
    core::ObjectId objectId;

    bool operator==(const MandatoryStateDescriptor& o) const
    {
        return stateKind == o.stateKind && nonSelectabilityBasis == o.nonSelectabilityBasis
            && objectId == o.objectId;
    }
    bool operator!=(const MandatoryStateDescriptor& o) const { return !(*this == o); }
};

/**
 * @brief 碰撞对象对（§6.3 CollisionPair——"对象 ID 对＋判定（构型级证据
 *        来源）"）。
 *
 * 实现口径（登记单元卡 v0.6）：proof 中的 collisionPairs 是碰撞主张的
 * **证据清单**——判定为否（inCollision==false）的对不能支撑碰撞证明，
 * validateProof 以 CollisionPairInvalid 拒绝（证据字段语义：不伪造碰撞）；
 * 另要求 A≠B（自身与自身不构成"对"）。
 *
 * 值语义；线程安全：纯值。
 */
struct CollisionPair {
    core::ObjectId objectIdA;   ///< 对象 A（须非零）
    core::ObjectId objectIdB;   ///< 对象 B（须非零且 ≠A）
    bool inCollision = true;    ///< 判定（证明证据须为 true——见结构注释）

    bool operator==(const CollisionPair& o) const
    {
        return objectIdA == o.objectIdA && objectIdB == o.objectIdB
            && inCollision == o.inCollision;
    }
    bool operator!=(const CollisionPair& o) const { return !(*this == o); }
};

/// coverageClaim 规范 token：解析界限/约束矛盾类证明的覆盖范围声明
/// （§6.3 coverageClaim 注释原文——"覆盖全部允许选择"）。
inline constexpr std::string_view kCoverageClaimAllAlternatives{"覆盖全部允许选择"};

/// coverageClaim 规范 token：必经状态碰撞类证明的覆盖范围声明
/// （§6.3 coverageClaim 注释原文——"该必经状态"）。
inline constexpr std::string_view kCoverageClaimMandatoryState{"该必经状态"};

/**
 * @brief 确定性不可行证明（§6.3 DeterministicInfeasibilityProof——表 2 ③
 *        的合格证据形态）。
 *
 * C8 作用域规则（§6.3"作用域规则的承载"原文）：构型/路径级碰撞**不进入**
 * 本契约——某 IK 解碰撞＝该解被硬过滤（SearchExhaustedRecord.
 * filteredSolutions）；某候选路径碰撞＝该路径淘汰并重规划；只有当碰撞
 * 对象是必经状态（mandatoryState 不可选择性依据成立）或证明覆盖全部
 * 允许选择（AnalyticBound/ConstraintContradiction）时才合法携带。
 * 三类之外的"证明"（如"多初值全发散"）→ validateProof 拒绝
 * （EV-VER-2/3/4 反例面）。
 *
 * 消费纪律：汇总器**绝不凭单一状态字段（如 bool provenInfeasible）采信
 * 证明**（§6.3 原文）——必须经 validateProof 逐字段校验通过后方可进入
 * 决策表③（且证明自身的可追溯性先受②级通用门禁约束，C6）。
 *
 * 值语义；线程安全：纯值。
 */
struct DeterministicInfeasibilityProof {
    /// 类别（仅三类——见枚举注释；C5/C8）。
    ProofCategory category = ProofCategory::AnalyticBound;
    /// 域登记的具体论断 id（如 "kin.reach-beyond-link-sum"——词表归域，
    /// 本头不做词形裁决：§6.3 校验清单未含该项，不私加约束，O-13 保守）。
    std::string claimToken;
    /// 作用对象（任务点/区域/必经状态所属对象）。
    core::ObjectId subject;
    // ---- 按 category 的条件必填（validateProof 逐字段校验，缺一即 Invalid）----
    /// AnalyticBound 必填：界限表达（域登记，如"目标距离 > Σ连杆长"）。
    std::string boundExpression;
    /// ConstraintContradiction 必填：约束矛盾表达。
    std::string contradictionExpression;
    /// MandatoryStateCollision 必填：必经状态描述（stateKind/必经性依据/
    /// objectId 三要素齐备——EV-VER-6 反例：缺失→Invalid）。
    MandatoryStateDescriptor mandatoryState;
    /// MandatoryStateCollision 必填：碰撞对象对清单（非空；每对须合法——
    /// 见 CollisionPair 纪律）。
    std::vector<CollisionPair> collisionPairs;
    // ---- 通用必填 ----
    /// 适用前提（证明成立所假设的需求/配置——绑定输入身份）。
    std::string preconditions;
    /// 覆盖范围声明（作用域）：Analytic/Contradiction 类须为
    /// kCoverageClaimAllAlternatives、MandatoryState 类须为
    /// kCoverageClaimMandatoryState（§6.3 原文两规范值——validateProof
    /// 按类别核对，错配即句法非法）。
    std::string coverageClaim;
    /// 输入身份：证明针对哪个快照（须非零且与被汇总结果一致——§6.3 校验）。
    core::ContentIdentity snapshotId;
    /// 输入身份：证明针对哪个切片（须非零且与被汇总结果一致——§6.3 校验）。
    SliceId sliceId;
    /// 产生者评估键（isValidEvaluationKey 词形；须已注册——§6.3 校验；
    /// std::string 承载同 D-5 口径：类型化归 Evaluator.hpp/EV-T10）。
    std::string producer;
    /// 产生者契约版本（须与注册值相符——同 sliceId、不同契约版本＝不同
    /// 算法契约，CON-04）。
    std::uint32_t producerContractVersion = 0;

    bool operator==(const DeterministicInfeasibilityProof& o) const
    {
        return category == o.category && claimToken == o.claimToken && subject == o.subject
            && boundExpression == o.boundExpression
            && contradictionExpression == o.contradictionExpression
            && mandatoryState == o.mandatoryState && collisionPairs == o.collisionPairs
            && preconditions == o.preconditions && coverageClaim == o.coverageClaim
            && snapshotId == o.snapshotId && sliceId == o.sliceId
            && producer == o.producer
            && producerContractVersion == o.producerContractVersion;
    }
    bool operator!=(const DeterministicInfeasibilityProof& o) const { return !(*this == o); }
};

/**
 * @brief 产生者注册表只读投影（validateProof 的"producer 已注册且契约
 *        版本相符"检查面——§3.3 注入边界同款先例）。
 *
 * 实现口径（登记单元卡 v0.6）：§6.3 的 validateProof(proof, registry,
 * snapshot) 中 registry＝评估器注册表（§9.4，EV-T10 落地）；本端口是
 * 其最小只读投影（两个查询），使 validateProof 不依赖未落地头、EV-T10
 * 的 EvaluatorRegistry 直接适配（实现本接口或在注册表上包一层——适配
 * 归 EV-T10，本单元零编译依赖其他单元，R-1/R-2）。
 *
 * 生命周期：调用方持有并保证 validateProof 调用期间存活；本单元不接管。
 * 线程约束：实现方自行保证（validateProof 为纯函数，只做两次只读查询）。
 */
class IProducerRegistryView {
public:
    virtual ~IProducerRegistryView() = default;

    /// 评估键是否已注册（§9.4 注册表只读查询——未知键 false）。
    virtual bool isRegistered(std::string_view evaluationKey) const = 0;

    /// 已注册评估键的契约版本是否与给定版本相符（未注册键返回值无意义
    /// ——调用序恒为先 isRegistered 后本查询）。
    virtual bool contractVersionMatches(std::string_view evaluationKey,
                                        std::uint32_t contractVersion) const = 0;
};

/**
 * @brief 证明字段级校验问题码（§6.3 validateProof 校验清单——"缺一即
 *        Invalid"的逐字段定位面；EV-VER-6"证明字段级校验逐项观测"）。
 *
 * 枚举顺序＝校验器检查序（类别→类别条件字段→产生者→快照/切片绑定→
 * 覆盖声明）；值一经交付不得改动/插入，只能表尾追加并留痕。
 */
enum class ProofIssueCode : std::uint8_t {
    // ---- ① 类别合法（三类之一——§6.3 校验清单第 1 项）----
    CategoryInvalid,            ///< category 不在三类词表（EV-VER-2/3/4：三类之外→拒绝）
    // ---- ② 类别条件字段齐备（§6.3 校验清单第 2 项）----
    BoundExpressionMissing,     ///< AnalyticBound 缺界限表达
    ContradictionExpressionMissing, ///< ConstraintContradiction 缺矛盾表达
    MandatoryStateKindMissing,  ///< MandatoryStateCollision：stateKind 空/含 NUL
    MandatoryStateBasisMissing, ///< MandatoryStateCollision：必经性依据空（EV-VER-6 反例核心）
    MandatoryStateObjectInvalid,///< MandatoryStateCollision：objectId 保留值
    CollisionPairsMissing,      ///< MandatoryStateCollision：collisionPairs 空集（无证据来源）
    CollisionPairInvalid,       ///< 对象对非法（id 保留值/A==B/判定为否——见 CollisionPair 纪律）
    // ---- ③ 产生者（§6.3 校验清单第 3 项：已注册且契约版本相符）----
    ProducerKeyInvalid,         ///< producer 评估键词形非法（isValidEvaluationKey——查表前置）
    ProducerNotRegistered,      ///< producer 未注册（未注册产生者的"证明"不可追溯）
    ContractVersionMismatch,    ///< producer 契约版本与注册值不符（CON-04）
    // ---- ④ 快照/切片绑定（§6.3 校验清单第 4 项：非空且与被汇总结果一致）----
    SnapshotIdMissing,          ///< snapshotId 空身份（保留值）
    SnapshotIdMismatch,         ///< snapshotId 与被汇总快照不一致
    SliceIdMissing,             ///< sliceId 空身份（保留值）
    SliceIdMismatch,            ///< sliceId 与被汇总结果切片不一致（expectedSliceId）
    // ---- ⑤ 覆盖声明（§6.3 校验清单第 5 项：coverageClaim 句法合法）----
    CoverageClaimInvalid,       ///< coverageClaim 与类别规范声明不符（§6.3 两规范值）
};

/**
 * @brief 单条证明校验问题（逐字段定位：字段名＋碰撞对下标＋中文说明）。
 */
struct ProofIssue {
    /// 非碰撞对问题的 index 保留值。
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    ProofIssueCode code;      ///< 机器可判别问题码
    std::string field;        ///< 涉事契约字段名（如 "mandatoryState"/"coverageClaim"）
    std::size_t index = npos; ///< 涉事 collisionPairs 下标（非对问题＝npos）
    std::string message;      ///< 中文开发诊断（字段级校验的逐项观测面）
};

/**
 * @brief 确定性不可行证明的字段级校验（§6.3 validateProof 纯函数承载）。
 *
 * 校验清单（§6.3 原文五行，顺序＝检查序＝ProofIssueCode 声明序；
 * **全部问题一次列出**——字段级校验的逐项观测面，不做首错短路）：
 *   ①类别合法三元之一；
 *   ②category 条件字段齐备（AnalyticBound→boundExpression；
 *     ConstraintContradiction→contradictionExpression；
 *     MandatoryStateCollision→mandatoryState 三要素＋collisionPairs）；
 *   ③producer 已注册且契约版本相符（经 IProducerRegistryView 查询——
 *     producer 词形非法时先行拒绝，不再查表）；
 *   ④snapshotId/sliceId 非空且与被汇总结果一致（快照一致性对
 *     snapshot.snapshotId 核对；切片一致性对 expectedSliceId 核对）；
 *   ⑤coverageClaim 句法合法（按类别的规范声明——kCoverageClaim 两常量）。
 *
 * @param proof           [in] 待检证明（评估产出；调用方持有，不修改）
 * @param producerRegistry [in] 产生者注册表只读投影（适配 §9.4 注册表；
 *                        调用方持有，仅调用期使用）
 * @param snapshot        [in] 被汇总结果的来源快照（冻结值；snapshotId
 *                        绑定核对的事实面）
 * @param expectedSliceId [in] 被汇总结果的切片身份（sliceId 绑定核对的
 *                        事实面——实现口径：§6.3"与被汇总结果一致"的
 *                        检查载体，必填不给默认——校验不可静默跳过，
 *                        NFR-COR-03；EV-T06 汇总器持切片身份处调用）
 * @return 问题清单（空＝证明合格；顺序＝检查序，确定性——NFR-COR-02）
 *
 * 错误语义：非抛出（可恢复查询路径——§2.1 try* 分轨）；判定为"证明
 * 无效"（EvidenceItemStatus::Invalid 语义）归调用方采信，本函数不代抛
 * （汇总器把问题聚合为 invalidReason 诊断——§6.2）。
 *
 * 复杂度：O(P)（P＝collisionPairs 数）＋O(1) 查表（两次注册表查询）。
 *
 * 线程安全：可重入纯函数（注册表只读查询由实现方保证并发安全）。
 */
std::vector<ProofIssue> validateProof(const DeterministicInfeasibilityProof& proof,
                                      const IProducerRegistryView& producerRegistry,
                                      const AnalysisSnapshot& snapshot,
                                      const core::ContentIdentity& expectedSliceId);

// =====================================================================
// 搜索未果记录（§6.3 末——C5/C8"搜索未找到有效解"口径）
// =====================================================================

/**
 * @brief 解被硬条件过滤的原因（§6.3 filteredSolutions.filterReason 三值）。
 */
enum class SearchFilterReason : std::uint8_t {
    Residual,   ///< 残差超限（未达收敛判据）
    JointLimit, ///< 关节限位违约
    Collision,  ///< 碰撞（构型级——该解被硬过滤，C8：不上升为任务不可行）
};

/**
 * @brief 单条被过滤解的记录（§6.3 filteredSolutions 元素）。
 *
 * 值语义；线程安全：纯值。
 */
struct FilteredSolution {
    /// 解引用（域承载的解标识——评估器内部解集的稳定指称；词表归域，
    /// 编码安全下限＝非空＋无 NUL）。
    std::string solutionRef;
    /// 过滤原因（三值——Collision 对应 C8"全部因碰撞被过滤"反例面）。
    SearchFilterReason filterReason = SearchFilterReason::Residual;

    bool operator==(const FilteredSolution& o) const
    {
        return solutionRef == o.solutionRef && filterReason == o.filterReason;
    }
    bool operator!=(const FilteredSolution& o) const { return !(*this == o); }
};

/**
 * @brief 搜索未果记录（§6.3 SearchExhaustedRecord——"多初值未收敛或已
 *        找到的解全部被硬条件过滤"时由评估器产出）。
 *
 * 消费纪律（§6.3 原文）：汇总层判 **DataInsufficient**（附该记录），**
 * 不得**输出不可行结论；用户扩大初值/预算后按 §4.2.4 复评（新 sliceId、
 * 同 inputBaselineId——D-04）。本记录在决策表④场景作为数据不足凭据
 * 透传（VerdictResult.searchRecord，EV-T06）。
 *
 * 值语义；线程安全：纯值。
 */
struct SearchExhaustedRecord {
    /// 已用搜索预算（计数，无量纲——如迭代上限的消耗量）。
    std::uint64_t searchBudgetUsed = 0;
    /// 已试初值数（计数，无量纲——"扩大初值"复评的对照基线）。
    std::uint64_t initialGuessesTried = 0;
    /// 被过滤解清单（多初值全发散时可为空——EV-VER-2 第一场景）。
    std::vector<FilteredSolution> filteredSolutions;

    bool operator==(const SearchExhaustedRecord& o) const
    {
        return searchBudgetUsed == o.searchBudgetUsed
            && initialGuessesTried == o.initialGuessesTried
            && filteredSolutions == o.filteredSolutions;
    }
    bool operator!=(const SearchExhaustedRecord& o) const { return !(*this == o); }
};

// =====================================================================
// 多工况覆盖证据（§6.6 CaseCoverageMatrix——EVI-02 承载）
// =====================================================================

/**
 * @brief 工况执行状态五值（§6.6："已执行/未执行/无效/不适用/失败五态即
 *        CaseExecutionStatus"）。
 */
enum class CaseExecutionStatus : std::uint8_t {
    Executed,      ///< 已执行（覆盖的唯一凭据态——"包络结果的存在不替代任何工况的 Executed 条目"，DYN-07）
    NotExecuted,   ///< 未执行（漏验——enabled∧mandatory 工况落到本态即②级失败，EV-COV-1）
    Invalid,       ///< 无效（如错误工况引用——caseId ∉ 必验集时条目即本态语义，EV-COV-2）
    NotApplicable, ///< 不适用（必填原因——条目级原因承载见 CaseCoverageEntry）
    Failed,        ///< 失败（该工况运行 outcome=Failed——不构成覆盖，须重跑或整体降级）
};

/**
 * @brief 覆盖矩阵单条目（§6.6 entries 行：{caseId, status, runId?,
 *        resultSliceId?}）。
 *
 * 实现口径（登记单元卡 v0.6）：§6.6 约束行"NotApplicable 必填原因"要求
 * 原因载体——字段行未列，**约束行为权威**，本结构增补
 * notApplicableReason（presence 语义同 EvidenceItem）。
 *
 * 值语义；线程安全：纯值。
 */
struct CaseCoverageEntry {
    CaseId caseId;                 ///< 工况身份（须在快照冻结必验工况集内——否则条目非法）
    CaseExecutionStatus status = CaseExecutionStatus::NotExecuted;
    /// 执行运行身份（Executed 的追溯面——可选；有值须非零）。
    std::optional<core::RunId> runId;
    /// 结果切片身份（Executed 的结果追溯面——可选；有值须非零）。
    std::optional<core::ContentIdentity> resultSliceId;
    /// 不适用原因（status==NotApplicable 必填且非空——§6.6 约束行）。
    std::optional<std::string> notApplicableReason;

    bool operator==(const CaseCoverageEntry& o) const
    {
        return caseId == o.caseId && status == o.status && runId == o.runId
            && resultSliceId == o.resultSliceId
            && notApplicableReason == o.notApplicableReason;
    }
    bool operator!=(const CaseCoverageEntry& o) const { return !(*this == o); }
};

/**
 * @brief 必验工况覆盖矩阵（§6.6 CaseCoverageMatrix——EVI-02 的证据形态）。
 *
 * 校验规则（§6.6 表，validateCaseCoverageMatrix 逐条执行）：
 *   - requiredCaseSetId 必须等于快照 caseSet 的身份（与计划核对——不同
 *     即②级门禁失败面）；
 *   - caseId ⊆ 必验集（错误工况引用→条目 Invalid 语义——EV-COV-2）；
 *   - caseId 重复→矩阵非法（重复记录拒绝——EV-COV-2）；
 *   - 每个 enabled∧mandatory 工况必须有 Executed 条目（漏验→②级失败
 *     ——EV-COV-1；**包络结果的存在不替代**，DYN-07——本矩阵没有"包络
 *     凭据"字段，替代无从发生）；
 *   - NotApplicable 必填原因；
 *   - 覆盖完备 ⇔ 全部 enabled∧mandatory 工况 Executed。
 *
 * 分母语义（契约 acceptance 4，O-14 保守字面）：覆盖矩阵分母＝**快照
 * 冻结态必验工况集**（enabled∧mandatory 由需求对象在快照冻结时解析——
 * §4.1.3；schema 权威归 requirements 卡 WP-14-T01，evidence 只消费
 * 冻结标记，不解释标记）。
 *
 * 值语义；线程安全：纯值。
 */
struct CaseCoverageMatrix {
    /// 本矩阵对账的必验工况集身份（须等于 snapshot.caseSet.requiredCaseSetId
    /// ——与计划核对，§6.6 第 1 条）。
    core::ContentIdentity requiredCaseSetId;
    /// 逐工况覆盖条目（caseId 在合法矩阵内唯一）。
    std::vector<CaseCoverageEntry> entries;

    bool operator==(const CaseCoverageMatrix& o) const
    {
        return requiredCaseSetId == o.requiredCaseSetId && entries == o.entries;
    }
    bool operator!=(const CaseCoverageMatrix& o) const { return !(*this == o); }
};

/**
 * @brief 覆盖矩阵校验问题码（§6.6 校验规则行）。
 *
 * "矩阵非法"类（使 matrixLegal==false）：RequiredCaseSetIdMismatch/
 * EntryCaseIdInvalid/DuplicateCaseEntry/UnknownCaseReference/
 * NotApplicableReasonMissing/EntryRunRefInvalid/EntryResultSliceRefInvalid；
 * MissingMandatoryExecution 是**漏验**（覆盖不完备）而非矩阵非法——两
 * 面分开（EV-COV-2"非法"与 EV-COV-1"漏验"的判定面分离）。
 * 值一经交付不得改动/插入，只能表尾追加并留痕。
 */
enum class CoverageIssueCode : std::uint8_t {
    RequiredCaseSetIdMismatch,   ///< requiredCaseSetId ≠ 快照冻结必验集身份（与计划核对——§6.6 第 1 条）
    EntryCaseIdInvalid,          ///< 条目 caseId 为保留值（无法对账）
    DuplicateCaseEntry,          ///< caseId 重复（矩阵非法——EV-COV-2"重复记录拒绝"）
    UnknownCaseReference,        ///< caseId ∉ 快照必验集（错误工况引用——EV-COV-2"错误引用 Invalid"）
    NotApplicableReasonMissing,  ///< NotApplicable 条目缺非空原因（§6.6 约束行）
    EntryRunRefInvalid,          ///< runId 有值但为保留值（追溯面损坏）
    EntryResultSliceRefInvalid,  ///< resultSliceId 有值但为保留值（追溯面损坏）
    MissingMandatoryExecution,   ///< enabled∧mandatory 工况无 Executed 条目（漏验——EV-COV-1；非矩阵非法）
};

/**
 * @brief 单条覆盖矩阵问题（可定位：caseId 规范文本＋条目下标＋中文说明）。
 */
struct CoverageIssue {
    /// 集合级问题的 index 保留值（requiredCaseSetId 不符＝npos）。
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    CoverageIssueCode code;   ///< 机器可判别问题码
    std::string caseId;       ///< 涉事工况规范文本（"obj-<32hex>"；集合级为空串）
    std::size_t index = npos; ///< 涉事条目下标（集合级＝npos）
    std::string message;      ///< 中文开发诊断（"校验错误列出具体条目"——EV-COV-2 观测面）
};

/**
 * @brief 覆盖矩阵校验结果（§6.6 校验规则行的结构化承载）。
 */
struct CoverageCheckResult {
    /// 矩阵合法（无身份错配/重复/错误引用/条目缺陷类 issue）——非法矩阵
    /// 的覆盖完备性无意义（先修矩阵）。
    bool matrixLegal = true;
    /// 覆盖完备 ⇔ 全部 enabled∧mandatory 工况有 Executed 条目（§6.6 判据；
    /// 仅在 matrixLegal 时可采信）。
    bool coverageComplete = false;
    /// 全部问题（顺序＝检查序：集合级在前、逐条目按输入序——确定性）。
    std::vector<CoverageIssue> issues;
};

/**
 * @brief 必验工况覆盖矩阵校验（§6.6 校验规则表的纯函数承载）。
 *
 * 校验顺序（固定，NFR-COR-02）：①requiredCaseSetId 与快照冻结必验集
 * 身份核对（与计划核对）→②逐条目（caseId 保留值→重复→必验集外引用
 * →NotApplicable 原因→runId/resultSliceId 追溯面）→③漏验核对（快照
 * 必验集中每个 enabled∧mandatory 工况必须有 Executed 条目，逐工况列出）。
 * 分母（快照冻结态必验工况集）权威＝快照值——本函数不读任何其他工况
 * 来源（acceptance 4 保守字面）。
 *
 * @param matrix  [in] 待检矩阵（评估产出；调用方持有，不修改）
 * @param snapshot [in] 冻结快照（必验工况集事实来源——§4.1.3 冻结凭据）
 * @return 校验结果（issues 空＋matrixLegal＋coverageComplete 见结构注释；
 *         "空必验集/全不适用"的保守处置属汇总④级——EV-T06，本函数在
 *         空集下平凡返回 coverageComplete==true〔无必验工况即无漏验〕，
 *         P-EV-7 口径）
 *
 * 线程安全：可重入纯函数。
 */
CoverageCheckResult validateCaseCoverageMatrix(const CaseCoverageMatrix& matrix,
                                               const AnalysisSnapshot& snapshot);

// =====================================================================
// 区域采样证据（§6.6 RegionCoverageEvidence——KIN-04 R3/R8 承载）
// =====================================================================

/**
 * @brief 区域覆盖计数栏（§6.6："reachable/unreachable/dataInsufficient
 *        （位置与姿态两栏）——计数按类分列"）。
 *
 * 分母完整性约束（KIN-04 R8）：reachable＋unreachable＋dataInsufficient
 * == plannedTotal（不符→证据 Invalid，EV-COV-3）；**不可达保留分母**
 * （禁止按结果剔除样本）。
 *
 * 值语义；线程安全：纯值。
 */
struct RegionCoverageCounts {
    std::uint64_t reachable = 0;        ///< 可达样本数（计数，无量纲）
    std::uint64_t unreachable = 0;      ///< 不可达样本数（计数——保留在分母内，KIN-04 R8）
    std::uint64_t dataInsufficient = 0; ///< 数据不足样本数（计数——>0 ⇒ 证据须标记 downgraded）

    bool operator==(const RegionCoverageCounts& o) const noexcept
    {
        return reachable == o.reachable && unreachable == o.unreachable
            && dataInsufficient == o.dataInsufficient;
    }
    bool operator!=(const RegionCoverageCounts& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 区域采样证据（§6.6 RegionCoverageEvidence——覆盖率**算法**归
 *        kinematics，本契约只约束"能进正式判定的证据形状"）。
 *
 * §6.6 字段行承载：sampleSetIdentity/plannedPositionSamples/
 * plannedPoseSamples 必须与快照采样计划一致（分母来源）；两栏计数分列；
 * downgraded 标记（dataInsufficient>0 → true：覆盖率数值仍可作参考值
 * 报告，但结论整体降级 DataInsufficient、不得输出正式覆盖率通过结论；
 * 补全证据后按**同一冻结样本集**复评——inputBaselineId 凭据）。
 *
 * 值语义；线程安全：纯值。
 */
struct RegionCoverageEvidence {
    /// 样本集身份（须与快照冻结采样计划的 sampleSetIdentity 一致——分母
    /// 来源锚；KIN-04"同一冻结样本集复评"的凭据）。
    core::ContentIdentity sampleSetIdentity;
    /// 计划位置样本数（分母声明——须等于快照冻结计划值，plannedPositionSamples）。
    std::uint64_t plannedPositionSamples = 0;
    /// 计划位姿样本数（分母声明——须等于快照冻结计划值，plannedPoseSamples）。
    std::uint64_t plannedPoseSamples = 0;
    RegionCoverageCounts position; ///< 位置栏计数（分母＝plannedPositionSamples）
    RegionCoverageCounts pose;     ///< 姿态栏计数（分母＝plannedPoseSamples）
    /// 降级标记（任一栏 dataInsufficient>0 ⇒ 必须为 true——§6.6 downgraded 行）。
    bool downgraded = false;

    bool operator==(const RegionCoverageEvidence& o) const
    {
        return sampleSetIdentity == o.sampleSetIdentity
            && plannedPositionSamples == o.plannedPositionSamples
            && plannedPoseSamples == o.plannedPoseSamples && position == o.position
            && pose == o.pose && downgraded == o.downgraded;
    }
    bool operator!=(const RegionCoverageEvidence& o) const { return !(*this == o); }
};

/// 区域覆盖校验的作用栏（两栏独立核对——位置/姿态）。
enum class RegionCoverageColumn : std::uint8_t {
    Position, ///< 位置栏（分母＝plannedPositionSamples）
    Pose,     ///< 姿态栏（分母＝plannedPoseSamples）
};

/**
 * @brief 区域覆盖证据校验问题码（§6.6 RegionCoverageEvidence 约束行）。
 * 值一经交付不得改动/插入，只能表尾追加并留痕。
 */
enum class RegionCoverageIssueCode : std::uint8_t {
    SampleSetIdentityInvalid,    ///< sampleSetIdentity 为保留值（无分母锚）
    SampleSetNotFoundInSnapshot, ///< 样本集身份不在快照冻结采样计划中（分母来源错配——§6.6"与快照采样计划一致"）
    PlannedCountMismatch,        ///< 声明分母与快照冻结计划值不符（分母来源错配）
    DenominatorIncomplete,       ///< 三类计数之和 ≠ 计划分母（EV-COV-3：分母完整性不符→证据 Invalid）
    DowngradedFlagInconsistent,  ///< dataInsufficient>0 但 downgraded==false（§6.6 downgraded 行）
};

/**
 * @brief 单条区域覆盖问题（可定位：作用栏＋中文说明）。
 */
struct RegionCoverageIssue {
    RegionCoverageIssueCode code;                 ///< 机器可判别问题码
    RegionCoverageColumn column = RegionCoverageColumn::Position; ///< 作用栏（来源/分母类问题不受栏限定时取 Position——见消息）
    std::string message;                          ///< 中文开发诊断
};

/**
 * @brief 区域覆盖证据校验结果（§6.6 约束行的结构化承载）。
 */
struct RegionCoverageCheckResult {
    /// 形状合格（issues 空）——可进正式判定的证据形状（§6.6 表头语）。
    bool valid = true;
    /// 零样本（任一栏 plannedTotal==0 且该栏计数和为 0——覆盖率**不定义**：
    /// 判 DataInsufficient＋诊断、不得输出 0%/100%，EV-COV-3 反例面；
    /// 汇总裁定归 EV-T06，本结果只暴露事实）。
    bool zeroSample = false;
    /// 降级必要（任一栏 dataInsufficient>0——结论整体降级 DataInsufficient，
    /// 覆盖率数值仅作参考值；§6.6 downgraded 行）。
    bool downgradedRequired = false;
    /// 全部问题（顺序＝检查序：样本集锚在前、两栏按 Position→Pose，确定性）。
    std::vector<RegionCoverageIssue> issues;
};

/**
 * @brief 区域采样证据校验（§6.6 RegionCoverageEvidence 约束行＋KIN-04
 *        R3/R8 的纯函数承载）。
 *
 * 校验顺序（固定，NFR-COR-02）：①样本集锚——sampleSetIdentity 非零、
 * 在快照冻结采样计划（§4.1.4 samplingPlans）中可定位、声明分母与冻结
 * 计划值一致（分母来源核对）；②逐栏（Position→Pose）——分母完整性
 * （reachable＋unreachable＋dataInsufficient == plannedTotal；溢出安全
 * 求和）、零样本判定（plannedTotal==0 且计数和为 0）；③降级标记——
 * 任一栏 dataInsufficient>0 ⇒ downgraded 必须为 true。
 *
 * @param evidence [in] 待检证据（评估产出；调用方持有，不修改）
 * @param snapshot [in] 冻结快照（分母来源——冻结采样计划事实面）
 * @return 校验结果（valid/zeroSample/downgradedRequired 语义见结构注释；
 *         "零样本→判 DataInsufficient 不输出 0%/100%"与"downgraded→
 *         整体降级"的**汇总裁定**归 EV-T06 决策表——本函数暴露事实面）
 *
 * 线程安全：可重入纯函数。
 */
RegionCoverageCheckResult
validateRegionCoverageEvidence(const RegionCoverageEvidence& evidence,
                               const AnalysisSnapshot& snapshot);

}  // namespace sdurws::ird::evidence

#endif  // SDURWS_IRD_EVIDENCE_EVIDENCE_HPP
