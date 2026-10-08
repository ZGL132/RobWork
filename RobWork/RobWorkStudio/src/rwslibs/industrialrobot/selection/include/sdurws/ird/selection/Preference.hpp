/**
 * @file   Preference.hpp
 * @brief  企业偏好过滤（selection 单元）——SEL-07"企业自定义优选品牌、
 *         供应状态和系列限制，但不改变硬能力判定"的分轨执行面：企业偏好
 *         配置、候选企业侧标注、偏好过滤结果与 PreferenceFilter（硬筛选
 *         之后的独立分轨处理，user-preference-filtered 独立 token）。
 *
 * 设计依据：
 *   - units/selection.md §10.2（空集语义表"用户过滤后为空＝偏好过滤结果
 *     ——与硬约束淘汰分开，非工程结论"）、§10.3（淘汰原因词表边界/偏好
 *     组——user-preference-filtered 与硬能力失败分离）、§10.4（分轨纪律
 *     ——偏好过滤不伪装成硬能力判定）、§14.0（通用约定——"用户优选过滤
 *     不伪装成硬能力（分轨 token）"；调用方错误 fail-fast vs 数据类返回
 *     素材）、§2.1 行 SEL-07（selection 承接偏好过滤逻辑；品牌数据的
 *     商业权威＝企业目录内容本身）、D-SEL-8（优选/供应状态/系列限制与
 *     硬能力分轨）
 *   - 需求 SEL-07（支持企业自定义优选品牌、供应状态和系列限制，但不改变
 *     硬能力判定）、SEL-06（偏好原因同样携带 ERR-01 比较型字段——文本
 *     比较以 actualText/requiredText 承载，T04 同款分轨）、NFR-COR-02
 *     （输出确定性）
 *   - 任务契约 tasks/foundation/WP-19-T07.json acceptance 1（"企业自定义
 *     优选品牌、供应状态和系列限制——不改变硬能力判定用例通过"）
 *
 * ★ 分轨纪律（本头存在的理由——为什么偏好过滤不写在 Screening.hpp）：
 *   1. 硬能力判定（SEL-03/04，T04 落位）的唯一输入是目录能力与工作点
 *      事实——企业偏好（品牌/供应状态/系列）不是器件的物理能力，任何
 *      偏好原因都不得进入 FeasibilityRecord.reasons（那会被下游组装器
 *      判为硬淘汰，伪装成工程结论）；
 *   2. 本头全部输出为独立的 PreferenceFilterOutcome——输入记录集按
 *      const 引用消费、零改写（契约测试钉住"apply 前后输入深等"）；
 *      呈现层（WP-19-T10 插件）据"硬可行∧偏好保留"呈现候选、据
 *      "硬可行∧偏好未过"呈现偏好过滤结果——§10.2 空集语义表的承载；
 *   3. 偏好原因构造唯一经 IRejectionReasonProvider::make 接口（§14.6
 *      词表唯一实现点——token=UserPreferenceFiltered、diagRef 经
 *      reasonTokenDiagCode 回填 SEL-USER-PREFERENCE-FILTERED），本头
 *      不第二处构造原因、不书写 token/码值字面量。
 *
 * 匹配语义（登记单元卡 §19.3 T07 落位细化）：
 *   - 三个偏好维度统一为"白名单式偏好条件"：白名单非空＝维度启用，
 *     候选值不在白名单（或标注缺失）→ 一条 user-preference-filtered
 *     原因；白名单为空＝维度不适用（不产生原因——与 ScreeningCriteria
 *     "条件缺失≠数据缺失"同款分界纪律）；
 *   - 品牌（vendor）的商业权威＝目录条目（卡 §2.1"企业目录内容本身"
 *     ——单一事实源，不从企业标注取品牌）；供应状态与产品系列在 v1
 *     目录 schema 中无字段（T03 落位细化 ③——v1 列契约冻结，不因本
 *     任务扩大 schema），经 CandidateEnterpriseFacts 由企业域值传递；
 *   - 文本匹配为逐字符精确匹配（区分大小写——与 JointMountRequirement
 *     "词表值逐字符精确匹配"同一纪律，无模糊/大小写折叠）。
 *
 * 线程安全：PreferenceFilter 无状态纯函数对象（可重入——卡 §14.10
 * "筛选/曲线/组合构造（纯函数）可重入"）；全部输入由调用方持有，输出
 * 按值返回（卡 §14.0 所有权约定）。
 */

#ifndef IRD_SELECTION_PREFERENCE_HPP
#define IRD_SELECTION_PREFERENCE_HPP

#include <string>
#include <vector>

#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/FeasibleSet.hpp>  // IRejectionReasonProvider——原因构造唯一接口
#include <sdurws/ird/selection/Screening.hpp>    // FeasibilityRecord/DeviceKind/VerdictKind

namespace sdurws::ird::selection {

// =====================================================================
// 偏好维度 thresholdSource 词表（每条偏好原因的阈值来源登记值——唯一
// 书写点；呈现层据此定位"来自企业偏好配置的哪个维度"，SEL-06/ERR-01
// "阈值来源"追溯面。命名沿用筛选条件条目引用形态 config.* 风格）。
// =====================================================================

/// 品牌维度的阈值来源（企业偏好配置 preferredVendors 白名单）。
inline constexpr const char* kPrefDimVendor = "config.sel-preference/preferred-vendors";
/// 供应状态维度的阈值来源（企业偏好配置 allowedAvailability 白名单）。
inline constexpr const char* kPrefDimAvailability = "config.sel-preference/allowed-availability";
/// 系列维度的阈值来源（企业偏好配置 allowedSeries 白名单）。
inline constexpr const char* kPrefDimSeries = "config.sel-preference/allowed-series";

/// 企业侧标注缺失的呈现哨兵（actualText 承载——"未标注"是事实陈述，
/// 不是空字符串占位；避免与"标注了空词表值"混淆，登记单元卡 §19.3 T07）。
inline constexpr const char* kPrefAnnotationAbsent = "(未标注)";

// =====================================================================
// 企业偏好配置（SEL-07"企业自定义"的承载——全部字段可序列化进
// config.sel-screening Configuration 条目侧的企业偏好域）
// =====================================================================

/**
 * @brief 企业偏好配置（SEL-07——企业自定义优选品牌/供应状态/系列限制）。
 *
 * 三维度语义（登记单元卡 §19.3 T07）：列表非空＝该维度启用（白名单式
 * 偏好条件——候选值不在白名单即 user-preference-filtered）；列表为空＝
 * 该维度不适用（零原因——不启用不惩罚）。企业想"只标注不过滤"某维度
 * 时留空即可——配置即语义，无第二套开关。
 *
 * 白名单值纪律：词表值逐字符精确匹配；条目不得为空串（空串是"未标注"
 * 哨兵语义，混入白名单会使未标注候选恒命中——语义歧义，校验边界拒绝，
 * 见 PreferenceFilter::apply 契约表）。
 */
struct EnterprisePreference {
    /// 优选品牌白名单（vendor 精确匹配；空＝品牌维度不适用）。
    /// 双重语义：①过滤面（非空时 vendor ∉ 白名单 → 偏好原因）；
    /// ②标注面（命中白名单的候选输出 preferredVendorHit=true——呈现层
    /// 排序建议信号，§10.4 排序键⑥"用户可选排序键"的呈现层素材）。
    std::vector<std::string> preferredVendors;
    /// 供应状态白名单（企业词表值，如 "in-production"；空＝维度不适用）。
    std::vector<std::string> allowedAvailability;
    /// 允许系列白名单（企业域系列标注值；空＝维度不适用）。
    std::vector<std::string> allowedSeries;

    bool operator==(const EnterprisePreference& o) const
    {
        return preferredVendors == o.preferredVendors
            && allowedAvailability == o.allowedAvailability
            && allowedSeries == o.allowedSeries;
    }
    bool operator!=(const EnterprisePreference& o) const { return !(*this == o); }
};

/**
 * @brief 候选企业侧标注（企业域数据——供应状态/产品系列的值传递承载）。
 *
 * 为什么不含 vendor（登记单元卡 §19.3 T07）：品牌数据的商业权威＝目录
 * 条目本身（卡 §2.1 行 SEL-07"不承接"列）——PreferenceFilter 从快照
 * 主表读 vendor（单一事实源），企业标注若另携品牌会制造两个权威；本
 * 结构只承载 v1 目录 schema 未登记的企业域标注。
 *
 * 标注缺失语义：availability/series 为空串＝企业未标注该候选（事实）——
 * 对应维度启用时按"未命中白名单"产生偏好原因（actualText＝
 * kPrefAnnotationAbsent），保守不默认通过（企业漏标不该静默放行——呈现
 * 层可见原因后由企业补标）。
 */
struct CandidateEnterpriseFacts {
    ModelId modelId;            ///< 标注所属候选型号稳定 ID（目录域身份——跨主表通用）
    std::string availability;   ///< 供应状态（企业词表值；空串＝未标注）
    std::string series;         ///< 产品系列（企业域标注；空串＝未标注）

    bool operator==(const CandidateEnterpriseFacts& o) const
    {
        return modelId == o.modelId && availability == o.availability
            && series == o.series;
    }
    bool operator!=(const CandidateEnterpriseFacts& o) const { return !(*this == o); }
};

/**
 * @brief 单候选偏好过滤结果（分轨承载——硬判定事实之外的企业偏好事实）。
 *
 * 消费契约（登记单元卡 §19.3 T07）：本结构不改写、不替代
 * FeasibilityRecord——可行集组装（FeasibleSetBuilder，T06）仍以硬筛选
 * 记录为唯一事实源；本结果供呈现层（WP-19-T10）合并呈现"硬可行∧偏好
 * 保留/偏好未过"两态与排序建议，供 reporting 建议证据项
 * sel.preferred-vendor-availability（kSelProfileItemVendorAvailability）
 * 消费。
 */
struct PreferenceFilterOutcome {
    ModelId modelId;             ///< 候选型号稳定 ID（候选级——跨轴聚合键）
    DeviceKind deviceKind = DeviceKind::Motor; ///< 候选类别（Motor/Gearbox——Combination 拒收）
    /// 优选品牌命中（vendor ∈ preferredVendors；白名单空＝false）——
    /// 排序建议信号（非判定事实；§10.4 排序键⑥呈现层素材）。
    bool preferredVendorHit = false;
    /// 偏好维度全部通过（启用的维度零未命中；硬不可行候选恒 true——
    /// 偏好维度不适用，硬判定已定）。
    bool passesPreference = true;
    /// 未命中维度的偏好原因（每维度至多一条；维度执行序＝品牌→供应状态
    /// →系列——固定登记序；token 恒 UserPreferenceFiltered）。
    std::vector<RejectionReason> preferenceReasons;

    bool operator==(const PreferenceFilterOutcome& o) const
    {
        return modelId == o.modelId && deviceKind == o.deviceKind
            && preferredVendorHit == o.preferredVendorHit
            && passesPreference == o.passesPreference
            && preferenceReasons == o.preferenceReasons;
    }
    bool operator!=(const PreferenceFilterOutcome& o) const { return !(*this == o); }
};

// =====================================================================
// 偏好过滤器（SEL-07 分轨执行面——硬筛选之后的独立处理）
// =====================================================================

/**
 * @brief 企业偏好过滤器（SEL-07——对硬筛选记录集做候选级偏好分轨）。
 *
 * 处理语义（五步，顺序固定，登记单元卡 §19.3 T07）：
 *  1. 契约校验（调用方错误 fail-fast，见 @throws 表）；
 *  2. 候选聚合：records 按 (deviceKind, candidateModelId) 去重（跨轴
 *     合并——品牌/供应状态/系列是候选属性，与轴无关；保留首现序）；
 *  3. 硬可行判定：候选存在任一 verdict==Feasible 的记录即"硬可行候选"
 *     （§10.2 分层纪律——偏好过滤只对硬可行的候选有意义；其余候选的
 *     verdict 已由硬筛选/组合校核定论，偏好维度不适用）；
 *  4. 偏好维度判定（仅硬可行候选；依序全量执行不短路——§10.2"候选
 *     能力筛选不短路"同款纪律；维度序＝品牌→供应状态→系列）：
 *       品牌：preferredVendors 非空且快照条目 vendor ∉ 白名单 → 原因
 *             （actualText=vendor、requiredText=白名单逗号连接）；
 *       供应状态：allowedAvailability 非空且标注（缺失＝空串）∉ 白名单
 *             → 原因（actualText=标注或 kPrefAnnotationAbsent）；
 *       系列：allowedSeries 非空且标注（缺失＝空串）∉ 白名单 → 原因
 *             （同上）；
 *     每条原因经 reasonProvider.make(ReasonToken::UserPreferenceFiltered,
 *     ctx) 接口构造（数值侧 actual/required/atTime=0——文本比较，T04
 *     同款分轨；unit 空串；catalog=快照身份；thresholdSource=维度词表）；
 *  5. 输出装配：每候选一条 PreferenceFilterOutcome（preferredVendorHit
 *     恒计算——硬不可行候选也给标注供呈现；passesPreference=原因空），
 *     按 (deviceKind, modelId) 升序输出（NFR-COR-02 确定性）。
 *
 * @param snapshot       [in] 候选来源目录快照（vendor 读取面与追溯身份；
 *                       调用方持有；不可变共享）
 * @param records        [in] 硬筛选候选级记录（T04 screenMotors/
 *                       screenGearboxes 产出形态；本函数只读——零改写，
 *                       分轨纪律的执行面；调用方持有）
 * @param enterpriseFacts [in] 候选企业侧标注（供应状态/系列；可为空集
 *                       ——全部候选按"未标注"处理；调用方持有）
 * @param preference     [in] 企业偏好配置（维度启用开关＝白名单非空）
 * @param reasonProvider [in] 淘汰原因构造接口（§14.6 词表唯一实现点——
 *                       偏好原因经接口构造，本类不自建原因）
 * @return 逐候选偏好过滤结果（候选级去重后一一对应；(deviceKind,
 *         modelId) 升序——确定性，NFR-COR-02）
 *
 * @throws std::invalid_argument 调用方契约违约（fail-fast——卡 §14.0）：
 *         ①records 含 deviceKind==Combination 的组合级记录（偏好过滤是
 *         候选级，组合级经 T05/T06 链路，不在本函数域）；②records 引用
 *         的 candidateModelId 不在快照对应主表（引用完整性破坏——硬筛选
 *         产物必然来自快照，脱节＝装配链违约）；③enterpriseFacts 内
 *         modelId 重复（同一候选两条矛盾标注无法消解）；④任一白名单含
 *         空串条目（空串＝未标注哨兵，见 EnterprisePreference 注）。
 *
 * @note 纯函数：不改写任何输入（契约测试钉住 records 深等）；同输入
 *       恒同输出（NFR-COR-01/02）；可重入。企业标注引用了不在快照的
 *       modelId 不是错误（企业标注库可为多代目录的超集——该条目忽略，
 *       不产 outcome；登记单元卡 §19.3 T07）。
 */
class PreferenceFilter final {
public:
    std::vector<PreferenceFilterOutcome> apply(
        const CatalogPackageSnapshot& snapshot,
        const std::vector<FeasibilityRecord>& records,
        const std::vector<CandidateEnterpriseFacts>& enterpriseFacts,
        const EnterprisePreference& preference,
        const IRejectionReasonProvider& reasonProvider) const;
};

}  // namespace sdurws::ird::selection

#endif  // IRD_SELECTION_PREFERENCE_HPP
