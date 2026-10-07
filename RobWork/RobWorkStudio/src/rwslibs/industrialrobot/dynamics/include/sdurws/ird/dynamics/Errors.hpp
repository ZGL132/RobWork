/**
 * @file   Errors.hpp
 * @brief  dynamics 域错误契约——DynamicsError（调用方错误 fail-fast 异常轨；
 *         环境错误走素材/诊断轨，不经本类型）。
 *
 * 设计依据：
 *   - units/dynamics.md §10.0 通用约定"错误类型"行（原文：域错误
 *     DynamicsError : std::runtime_error，token 前缀 dynamics/...；
 *     调用方错误〔前置违约/非法参数/身份缺失〕fail-fast 异常、不发稳定码；
 *     环境错误〔数值失败/上游不兼容/资源问题〕返回素材/诊断〔ERR-01〕，
 *     稳定码归属 diagnostics StableCodeRegistry——DYN-*，§9.4）
 *   - units/dynamics.md §5.6（数值稳定性、失败语义与耦合链防御——输入
 *     非法/工况级失败/局部异常的三层处置表）
 *   - 需求 ERR-01（稳定诊断码）、NFR-COR-03（非有限/非法不静默）
 *   - 任务契约 tasks/foundation/WP-17-T03.json（RNEA 评估器错误轨）
 *
 * 背景说明（两轨分工——为什么异常不带稳定码字段）：稳定码的注册与发布
 *   权威＝diagnostics StableCodeRegistry（PA-1）；异常是"调用方契约违约"
 *   的进程内 fail-fast 通道，调用方应修复调用而不是重试，故异常 message
 *   以 dynamics/ 前缀 token 开头（人类定位用），但不承载 DYN-* 稳定码——
 *   需要进入正式诊断/报告的环境错误（如 RNEA 数值失败 DYN-RNEA-FAILED、
 *   摩擦缺失 DYN-FRICTION-MISSING）由评估器以 core::DiagnosticRecord 素材
 *   随产出返回（InverseDynOutcome.diagnostics），不抛异常。
 *
 * 线程安全：纯异常类型（值语义）；确定性：message 由固定格式拼装（无
 *   locale/环境依赖——NFR-COR-02）。
 */

#ifndef IRD_DYNAMICS_ERRORS_HPP
#define IRD_DYNAMICS_ERRORS_HPP

#include <stdexcept>
#include <string>
#include <string_view>

namespace sdurws::ird::dynamics {

/**
 * @brief dynamics 域错误（§10.0 错误类型行——调用方错误 fail-fast 轨）。
 *
 * 触发面（本任务落地的评估入口前置，§10.1 前置行的机械化——任一违例即
 * 抛本异常，评估终止、不产出半成品序列）：
 *   - 模型指针为空 / 链无可动关节 / 链携带 Fixed 等词表外关节类型；
 *   - 工况对象 ID 为空（工况集空——§8.1"空工况集合"调用侧形态）；
 *   - 样本激励维度与模型可动关节数不匹配（DYN-DIMENSION-MISMATCH 语义——
 *     §5.6"输入维度不匹配：评估终止"；异常 message 携带实际/期望维度的
 *     比较数据）；
 *   - 轨迹时间重复/倒退/零间隔（DYN-SERIES-NON-MONOTONIC 语义——评估器
 *     不排序修复、不插值抹平，§4.6）；
 *   - 物性防御性复检失败（非有限/惯量张量非对称超容差——§5.3 附录 D
 *     第 6 项；建模侧 MDL-06 断言前置，此处为跨版本快照的防御面）。
 *
 * token 约定：what() 以 "dynamics/<短横线 token>: " 开头（§10.0 token
 * 前缀），后接中文定位（含实测值——比较型素材语义）。异常不进入正式
 * 诊断记录（不发 DYN-* 稳定码——文件头"两轨分工"）。
 */
class DynamicsError : public std::runtime_error {
public:
    /**
     * @brief 构造域错误（token＋中文定位自动拼装）。
     *
     * @param token [in] dynamics/ 前缀下的短横线 token（如 "input-invalid"
     *              ——调用方保证不带前缀与冒号；本构造单点补全）
     * @param detail [in] 中文定位文本（含字段名与实测值——错误可定位语义）
     */
    DynamicsError(std::string_view token, const std::string& detail)
        : std::runtime_error("dynamics/" + std::string(token) + ": " + detail)
        , m_token(token)
    {
    }

    /// 取 token（不带前缀——调用方按需拼装展示形态；纯函数、不抛）。
    const std::string& token() const noexcept { return m_token; }

private:
    std::string m_token;  ///< dynamics/ 前缀下的 token（去掉前缀存储——展示层自由拼装）
};

}  // namespace sdurws::ird::dynamics

#endif  // IRD_DYNAMICS_ERRORS_HPP
