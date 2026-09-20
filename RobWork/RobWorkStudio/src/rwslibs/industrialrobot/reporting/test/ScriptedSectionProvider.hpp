/**
 * @file   ScriptedSectionProvider.hpp
 * @brief  ScriptedSectionProvider——章节投影提供方（IReportSectionProvider）
 *         的具名脚本化替身（§10 可控替身四具名之一；RPT-T11 落位——本头
 *         为本任务从 RPT-T04/T05 各测试文件局部夹具收敛的唯一正本，局部
 *         副本随 RPT-T11 拆除）。
 *
 * 设计依据：
 *   - units/reporting.md §10（可控替身清单原文："ScriptedSectionProvider
 *     （按脚本返回章节内容/缺项/不适用）"）、§9.2（IReportSectionProvider
 *     契约——"替身与真实提供方同一契约"〔§9.2 后置行〕：纯投影/同请求同
 *     输出/值拷贝请求）、§5.1 词表规则（"阶段 A 前不存在——替身仅供测试"）、
 *     §11 RPT-T11 行（产物列原文——四具名替身；涉及文件 reporting/test/*）
 *   - units/testkit.md §2.4（T-1 允许依赖形态——替身头仅被测试目标包含）
 *   - 任务契约 tasks/foundation/RPT-T11.json acceptance 2/3/5
 *
 * 替身边界声明（§10.1 RP-STATE-4／任务约束§八——四具名替身共用，全文亦
 *   登记于 test/README.md，此处为具名正本之一）：
 *   本替身按脚本返回的章节内容/缺项/不适用**仅验证 reporting 侧契约**
 *   （注册边界/纯投影/绑定校验/状态呈现/缺项表达），**不构成任何运动学/
 *   轨迹/动力学/选型结果的业务正确性证明**（EV-REG-3 同源）；真实域章节
 *   内容的正确性归各域评估器与 RPT-T14/T15 真实注册联调（§12.3：真实章节
 *   内容不得以替身数据冒充验收）。脚本数值（entryKey/字段键/条目字段值）
 *   为确定性固定值——禁随机。
 *
 * 与真实提供方的同一契约（§9.2 后置行）：project() 纯投影（同请求同输出
 *   ——调用本身不改输出）、请求以值拷贝留存（调用方事后修改请求不得影响
 *   已记录副本——提供方不持有请求的结构性体现）。
 *
 * 线程约束：单线程使用（注册期装配/运行期投影均在测试属主线程；并发用例
 *   仅经 SectionRegistry::find() 消费，不并发调用本替身观测面）。
 */

#ifndef SDURWS_IRD_REPORTING_TEST_SCRIPTEDSECTIONPROVIDER_HPP
#define SDURWS_IRD_REPORTING_TEST_SCRIPTEDSECTIONPROVIDER_HPP

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sdurws/ird/reporting/ReportModel.hpp>
#include <sdurws/ird/reporting/SectionProvider.hpp>

namespace sdurws::ird::reporting::test_fakes {

/**
 * @brief 脚本化章节提供方（§9.2 IReportSectionProvider 三方法逐字实现）。
 *
 * 脚本面（构造时注入，构造后只读——"同请求同输出"后置行的替身承载）：
 *   - sectionId/minimumLevel/requiredEvaluationKeys：注册三要素；
 *   - scripted：project() 的固定返回值——由测试用§5.3 四态（Populated/
 *     NoFormalResult/DataInsufficient/NotApplicable）任一形态组装
 *     （"按脚本返回章节内容/缺项/不适用"——§10 替身清单原文）。
 *
 * 观测面：project() 调用计数与最近请求副本（纯投影零副作用/值拷贝请求/
 * 取消检查点位置等断言的事实面；mutable——测试桩观测面，产品代码无此
 * 形态）。
 */
class ScriptedSectionProvider final : public IReportSectionProvider {
public:
    /**
     * @brief 注册三要素＋脚本内容注入。
     *
     * @param id          [in] 章节 ID（§5.1 词表内——注册边界核对归注册表，
     *                    本替身不复制词表裁决）
     * @param minLevel    [in] 最低报告级别（须与词表级别一致——同上）
     * @param evalKeys    [in] 消费评估键声明（§9.2 契约面——请求过滤的
     *                    声明位；过滤本体归构建器）
     * @param scripted    [in] project() 固定返回的脚本内容（§5.3 四态任一）
     */
    ScriptedSectionProvider(std::string id, ReportLevel minLevel,
                            std::vector<std::string> evalKeys, SectionContent scripted)
        : m_id(std::move(id))
        , m_minLevel(minLevel)
        , m_evalKeys(std::move(evalKeys))
        , m_scripted(std::move(scripted))
    {
    }

    /// 兼容形态：评估键声明缺省＝空（§9.2 允许——"本章不消费评估键"）；
    /// RPT-T04 局部夹具（SectionProviderTest）的构造签名在此收敛。
    ScriptedSectionProvider(std::string id, ReportLevel minLevel, SectionContent scripted)
        : ScriptedSectionProvider(std::move(id), minLevel, {}, std::move(scripted))
    {
    }

    std::string sectionId() const override { return m_id; }
    ReportLevel minimumLevel() const override { return m_minLevel; }
    std::vector<std::string> requiredEvaluationKeys() const override { return m_evalKeys; }

    SectionContent project(const SectionRequest& request) override
    {
        ++m_projectCalls;              // 调用计数（副作用仅观测面——不影响输出）
        m_lastRequest = request;       // 请求值拷贝（结构性留存——非引用/指针）
        return m_scripted;             // 固定脚本输出（同请求同输出的契约承载）
    }

    // ---- 观测面（测试断言辅助） ----

    /// project() 累计调用次数（纯投影/取消检查点/逐章节粒度的断言面）。
    long projectCalls() const { return m_projectCalls; }

    /// 最近一次投影请求的值拷贝（§9.2"值拷贝请求"的观测面）。
    const std::optional<SectionRequest>& lastRequest() const { return m_lastRequest; }

private:
    std::string m_id;                       ///< 章节 ID（§5.1 词表 token）
    ReportLevel m_minLevel;                 ///< 最低级别（词表一致）
    std::vector<std::string> m_evalKeys;    ///< 消费评估键声明
    SectionContent m_scripted;              ///< 固定脚本输出（构造后只读）
    long m_projectCalls = 0;                ///< 投影调用计数（观测面）
    std::optional<SectionRequest> m_lastRequest;  ///< 最近请求副本（观测面）
};

}  // namespace sdurws::ird::reporting::test_fakes

#endif  // SDURWS_IRD_REPORTING_TEST_SCRIPTEDSECTIONPROVIDER_HPP
