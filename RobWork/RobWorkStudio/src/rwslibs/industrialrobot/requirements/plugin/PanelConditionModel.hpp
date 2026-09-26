/**
 * @file   PanelConditionModel.hpp
 * @brief  工况面板呈现模型（零 Qt）——工况表＋负载/事件/节拍/适用范围
 *         投影＋必验清单预览（RequirementProfile 投影）（卡 §9.8 面板表
 *         第 4 行）。
 *
 * 设计依据：
 *   - units/requirements.md §9.8（面板组成表第 4 行——"工况表＋负载/事件/
 *     节拍/适用范围编辑＋必验清单预览（RequirementProfile 投影）"；消费
 *     契约＝§6.1/§9.4）、§4.5（OperatingCondition 字段表）、§6.2（必验
 *     冻结 schema——resolveRequiredCases P-EV-9 唯一实现点）、§4.8
 *     （RequirementProfile 派生档——deriveRequirementProfile 派生入口）
 *   - 需求 REQ-04（工况要求值）、P-EV-9（必验派生唯一——I-REQ-9）；任务
 *     契约 tasks/foundation/WP-14-T08.json acceptance 1（工况面板行）
 *
 * 背景说明（零计算逻辑——acceptance 3）：必验清单是**派生档**（P-EV-9
 * 冻结规则 mandatory≡(level==Must)）——本面板不复制派生规则，必验预览经
 * deriveRequirementProfile/IOperatingConditionService::resolveRequiredCases
 * 域函数现算（P-EV-9 单点，NFR-MNT-04）；负载四态字段（SourcedValue）
 * 呈现"未提供/不适用"占位（缺失≠零，ERR-01 纪律）；适用范围/事件绑定
 * 全部直投（悬空引用的判定归就绪层 R4，呈现层不代判）。
 *
 * 线程约束：全部函数纯函数，可重入；入参仅 UI 线程可变（§3.4）。
 * 确定性：行序固定；同输入同输出（域解析按 ObjectId 规范序——确定性）。
 */

#ifndef IRD_REQUIREMENTS_PLUGIN_PANELCONDITIONMODEL_HPP
#define IRD_REQUIREMENTS_PLUGIN_PANELCONDITIONMODEL_HPP

#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>  // core::ObjectId（条目锚）
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // OperatingCondition/RequiredCaseEntry/ConditionPayload 等
#include <sdurws/ird/requirements/Services.hpp>  // IOperatingConditionService/deriveRequirementProfile（P-EV-9 单点）
#include "PanelStationModel.hpp"  // StationFieldRow/StationFieldEnablement（检查器行模型复用——同目录插件私有头）

namespace sdurws::ird::requirements {

// =====================================================================
// 工况表行与检查器行（卡 §9.8 第 4 行的行载体——行模型复用 StationFieldRow）
// =====================================================================

/**
 * @brief 工况面板的一个工况行（纯值——工况表的行数据源；选择联动锚
 *        ＝条目 ObjectId，L-R1）。
 */
struct ConditionRow {
    core::ObjectId objectId;   ///< 工况锚（L-R1 选中联动键）
    std::string name;          ///< 语义名（工程用语——UX-02）
    std::string level;         ///< 等级（"Must"/"Should"——必验派生事实随行）
    bool enabled = false;      ///< 启用事实（必验集合＝enabled∧Must——§6.2）
    std::string cycleText;     ///< 目标节拍摘要（"≥x s"或"未设"——可选字段）
    std::string appliesToText; ///< 适用范围摘要（"全部工位"/"N 个工位"/"不适用"）

    bool operator==(const ConditionRow& o) const
    {
        return objectId == o.objectId && name == o.name && level == o.level
            && enabled == o.enabled && cycleText == o.cycleText
            && appliesToText == o.appliesToText;
    }
    bool operator!=(const ConditionRow& o) const { return !(*this == o); }
};

/**
 * @brief 投影工况表行（§9.8 第 4 行——一工况一行；行序＝工作集序）。
 * @param conditions [in] 工况集合（工作集权威——只读）
 * @return 行序列（纯投影——不缓存；确定性）
 *
 * 纯函数；确定性；不抛。
 */
std::vector<ConditionRow> conditionRows(const std::vector<OperatingCondition>& conditions);

/**
 * @brief 投影工况检查器字段行（负载/事件/节拍/适用范围/要求值——§4.5
 *        字段表的行投影；行模型复用 StationFieldRow——同一行形态跨面板
 *        复用，呈现契约一致）。
 *
 * 字段面（行序＝登记序）：
 *   - 条目级行：名称/等级/启用/必验（必验行＝enabled∧Must 派生事实——
 *     本行文案由 resolveRequiredCases 产出的 RequiredCaseEntry 直投，
 *     不本地复判 I-REQ-9）；
 *   - 负载逐条：工具引用＋质量 kg/质心 m/惯量 kg·m²（四态——未提供
 *     占位，缺失≠零）；
 *   - 事件逐条：类型＋绑定工位锚＋时长 s（Dwell 可选）；
 *   - 节拍/要求值/适用范围/备注。
 *
 * @param condition [in] 工况条目（工作集权威——只读）
 * @param service   [in] 工况服务（resolveRequiredCases——必验派生域函数）
 * @param writable  [in] 会话可写性（false→可编辑行灰显——L-R12 行半区）
 * @return 检查器行序列（确定性）
 *
 * 纯函数；确定性；不抛。
 */
std::vector<StationFieldRow> conditionFieldsFor(const OperatingCondition& condition,
                                                const IOperatingConditionService& service,
                                                bool writable);

// =====================================================================
// 必验清单预览（卡 §9.8 第 4 行"必验清单预览（RequirementProfile 投影）"）
// =====================================================================

/**
 * @brief 必验清单预览行（RequiredCaseEntry 的呈现承载——§6.2 冻结 schema
 *        的直投；mandatory 是派生事实（I-REQ-9），呈现层零复判）。
 */
struct MustListEntryRow {
    core::ObjectId caseId;   ///< 工况条目 ObjectId（§6.2 caseId——定位跳转锚）
    std::string label;       ///< 人读标签（§6.2 label＝工况 name——D-REQ-6）
    bool enabled = false;    ///< 用户启用开关原值
    bool mandatory = false;  ///< 派生必验标记（≡level==Must——域解析产出直投）

    bool operator==(const MustListEntryRow& o) const
    {
        return caseId == o.caseId && label == o.label && enabled == o.enabled
            && mandatory == o.mandatory;
    }
    bool operator!=(const MustListEntryRow& o) const { return !(*this == o); }
};

/**
 * @brief 必验清单预览投影（RequirementProfile 的呈现半区——全量投影行
 *        ＋Must/Should 计数＋覆盖目标汇总；派生档经
 *        deriveRequirementProfile 域函数现算——P-EV-9 单点零复制）。
 *
 * @param points     [in] 任务点集合（Must/Should 计数输入——工作集权威）
 * @param regions    [in] 区域集合（计数＋覆盖汇总输入）
 * @param conditions [in] 工况集合（必验清单输入）
 * @param service    [in] 工况服务（resolveRequiredCases 复用入口）
 * @return 预览行序列（序＝域解析规范序——确定性）
 *
 * 纯函数；确定性；不抛。
 */
std::vector<MustListEntryRow> mustListPreview(const std::vector<TaskPoint>& points,
                                              const std::vector<WorkRegion>& regions,
                                              const std::vector<OperatingCondition>& conditions,
                                              const IOperatingConditionService& service);

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_PLUGIN_PANELCONDITIONMODEL_HPP
