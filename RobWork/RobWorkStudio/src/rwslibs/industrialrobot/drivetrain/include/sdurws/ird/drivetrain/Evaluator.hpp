/**
 * @file   Evaluator.hpp
 * @brief  传动映射评估器（③端口注册面，units/drivetrain.md §13.10）——
 *         DriveTrainMappingEvaluator final : evidence::IEngineeringEvaluator
 *         （评估键 dt-mapping：校验→归一化解码→映射→一致性自检→工作点→
 *         Facts）＋DriveTrainMappingEvaluatorFactory（IEvaluatorFactory）
 *         ＋descriptor 工厂函数＋DriveTrainFactsProvider
 *         （IDriveTrainEvidenceFactsProvider 实现）。
 *
 * 设计依据：
 *   - units/drivetrain.md §13.10（③端口注册面——descriptor 字段落值/
 *     evaluate() 分解/输出形态）、§12.4（任务能力声明——支持取消与分批
 *     流式〔逐工况批次〕，不支持暂停）、§12.1（UpstreamResult 依赖——
 *     P-DT-6 提议键）、§13.0/§13.9（线程生命周期）、D-DT-14（评估键/
 *     模式/无状态/实例单线程）
 *   - evidence 冻结契约（Evaluator.hpp——IEngineeringEvaluator 调用约定/
 *     EvaluatorDescriptor 字段面/EvaluationOutput 形态；Slice.hpp——评估
 *     键词形与切片条目；Evidence.hpp——Profile 绑定）
 *   - 需求 DYN-04（唯一映射实现——dynamics/selection 不各自实现）、
 *     SEL-05（③端口消费同一口径工作点）、NFR-MNT-07（无重复算法）
 *   - 任务契约 tasks/foundation/WP-18-T03.json（acceptance 1——"唯一映射
 *     实现（dt.mapping 评估器经③端口注册）"）
 *
 * ★ 实现口径偏差（DTB §5.4——随单元卡增量修订登记；详见 Codec.hpp 尾注）：
 *   1. 评估键 "dt-mapping"（kebab）——卡面 "dt.mapping" 记法与 evidence
 *      isValidEvaluationKey 词形闸门（不含点）冲突，按③端口所有者词形
 *      权威执行；
 *   2. descriptor.profile 绑定消费域 sel Profile（profileId 五域词表封闭：
 *      isDomainProfileId 只认 kin/trj/dyn/sel/opt，卡面建议值 "dt" 无法
 *      注册——按需求 §8.1 表 4 选型域必需项"每组合电机侧工作点——
 *      DriveTrainMappingEvaluator 同一口径"绑定 sel 域；Profile 内容权威
 *      归 selection 卡，R1 阶段以最小必需项登记）；
 *   3. config.dt-mapping 不入 R1 依赖声明（evidence 依赖必需性词表无
 *      "Optional"位——Required/Conditional 二值；效率经 DriveTrainModel
 *      值传递，配置条目消费随组合场景任务落位）。
 *
 * 错误轨约定（evidence §9.3"抛 EvidenceError 或返回诊断，域自选，约定
 * 登记于域任务卡"——本域约定）：调用方契约违约（切片评估键/契约版本不
 * 符、必需条目缺失）＝fail-fast 抛 std::invalid_argument；数据类（切片
 * 对象字节不可得/解码违约/映射降级）＝返回 EvaluationOutput＋DT-* 诊断，
 * 不抛——与映射核心（MappingCore.hpp 文件头）同一错误语义两分法。
 *
 * 线程模型（§13.9）：评估器实例单线程（每 worker 每任务一实例）；
 * stateless=true（descriptor 声明——实例可共享）；核心可重入。
 */

#ifndef IRD_DRIVETRAIN_EVALUATOR_HPP
#define IRD_DRIVETRAIN_EVALUATOR_HPP

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/drivetrain/Codec.hpp>
#include <sdurws/ird/drivetrain/Facts.hpp>
#include <sdurws/ird/drivetrain/MappingCore.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>

#include <memory>
#include <string>
#include <vector>

namespace sdurws::ird::drivetrain {

// =====================================================================
// descriptor 工厂（注册期验证对象——L5 装配经 Factory 消费）
// =====================================================================

/**
 * @brief 构造传动映射评估器描述符（字段落值＝卡 §13.10 行的机械化）。
 *
 * 字段落值（逐字段出处）：
 *   - key＝kMappingEvaluationKey（"dt-mapping"——偏差登记见文件头）；
 *   - contractVersion＝kMappingContractVersion（进 sliceId——CON-04）；
 *   - inputs＝{ {model.drivetrain, Object, Required}, {dyn.joint-series,
 *     UpstreamResult, Required} }（P-DT-6 提议键；config.dt-mapping 不入
 *     R1 声明——文件头偏差 3）；
 *   - profile＝{profileId:"sel", version:"1", contentIdentity:保留值}
 *     （域不可申报——§9.5 实现口径 R-3；偏差登记见文件头 2）；
 *   - supportedModes＝{Quick, Verified}（D-DT-14——Preview 暂不含，保守）；
 *   - stateless＝true；threadSafety＝SingleThread（实例级；D-DT-14）。
 *
 * @return 描述符（值语义——调用方经 Factory 持有稳定存储形态）
 */
evidence::EvaluatorDescriptor makeMappingDescriptor();

/// sel 域绑定 Profile 的登记版本（与 makeMappingDescriptor 的 profile.version
/// 同源——契约测试按此注册先行 Profile，"Profile 注册在前、评估器注册在后"
/// 接入序，evidence §13）。
inline constexpr std::string_view kMappingProfileId = "sel";
inline constexpr std::string_view kMappingProfileVersion = "1";

// =====================================================================
// 评估器实例（唯一映射实现的③端口适配层）
// =====================================================================

/**
 * @brief 传动映射评估器（卡 §13.10——evaluate()＝校验→归一化解码→映射→
 *        一致性自检→工作点→Facts；envelope 由接纳层组装，本卡不自建）。
 *
 * 实例内部仅持映射核心与 Facts 提供器的无状态实现——实例可共享
 * （stateless=true 的实例级语义由 descriptor 声明，调用方按声明决策）。
 *
 * ★ 能力位注入（WP-18-T05）：构造参数 stage 声明本实例的映射能力位
 * （默认 R1——既有构造面与 L5 装配行为零变化）。能力由装配清单与算法
 * 版本决定（卡 §6.3——UI/配置标签不改变能力），宿主装配按清单选择：
 * R1 装配下窗口输入仍被 DT-COUPLING-STAGE-LOCKED 阻断（R1 阻断反例
 * 保留——AT-38）；R2 装配下窗口输入经 §7.2 矩阵检查后进入块对角映射。
 */
class DriveTrainMappingEvaluator final : public evidence::IEngineeringEvaluator {
public:
    /// @param stage [in] 映射能力位（默认 R1——对角路径；R2 装配显式传入）。
    explicit DriveTrainMappingEvaluator(
        StageCapability stage = StageCapability::R1Capability);

    /// 描述符（注册期已验证；运行期只读——返回引用指向实例成员稳定存储）。
    const evidence::EvaluatorDescriptor& descriptor() const override;

    /**
     * @brief 执行一次评估（evidence §9.3 调用约定）。
     *
     * 流程：
     *   ①切片核对：evaluationKey/evaluatorContractVersion 与本评估器登记
     *     值不符＝派发违约（fail-fast——execution 按键派发的契约前提）；
     *   ②必需条目提取：model.drivetrain（Object）与 dyn.joint-series
     *     （UpstreamResult）——缺失＝派发前依赖未满足的运行期形态（调用方
     *     违约 fail-fast；派发前校验归 execution 注册闭包，此处为第二道）；
     *   ③字节读取：context.tryObjectBytes 按 Object 条目（id+version）读
     *     取模型对象字节——不可得＝数据类，返回诊断＋空 payload（不伪造）；
     *   ④解码＋映射＋统计：经 Codec 解码输入，调 DriveTrainMappingCore::
     *     evaluate（核心内含阻断面/一致性自检/统计——同一算法路径）；
     *   ⑤输出装配：EvidenceItem（sel 域工作点项：有工作点数据即 Satisfied
     *     ＋payload 摘要）＋payload（DomainPayload：kMappingPayloadToken＋
     *     encodeMappingOutput 字节，digest 由 DomainPayload::make 计算）＋
     *     诊断列表透传。
     *
     * @param request [in] 评估请求（调用方持有，调用期间有效）
     * @param context [in] 宿主上下文（取消/进度/对象读取——实例不持久持有）
     * @return 评估产出（envelope 由调用侧组装——§9.3）
     *
     * @throws std::invalid_argument 切片派发违约/必需条目缺失（调用方错误）
     *
     * 线程约束：实例单线程（descriptor.threadSafety 声明）。
     */
    evidence::EvaluationOutput evaluate(const evidence::EvaluationRequest& request,
                                        evidence::IEvaluationContext& context) override;

private:
    evidence::EvaluatorDescriptor m_descriptor; ///< 稳定存储（descriptor() 引用所指）
    StageCapability m_stage;                    ///< 本实例映射能力位（构造注入——卡 §6.3）
};

/**
 * @brief 评估器工厂（§9.3 IEvaluatorFactory——L5 装配注册进 EvaluatorRegistry；
 *        create() 线程安全〔仅构造无状态对象〕）。
 */
class DriveTrainMappingEvaluatorFactory final : public evidence::IEvaluatorFactory {
public:
    DriveTrainMappingEvaluatorFactory();

    /// 工厂描述符（多次调用返回同值——注册表按首次返回值登记）。
    const evidence::EvaluatorDescriptor& descriptor() const override;

    /// 创建评估器实例（所有权随 unique_ptr 转移；保证非空）。
    std::unique_ptr<evidence::IEngineeringEvaluator> create() const override;

private:
    evidence::EvaluatorDescriptor m_descriptor; ///< 稳定存储（descriptor() 引用所指）
};

// =====================================================================
// 事实 DTO 提供器（§13.6——Facts 提取的实现面）
// =====================================================================

/**
 * @brief 事实提供器实现（纯函数——从映射输出提取 canonical 事实面；
 *        评估器输出装配与消费域均可复用同一实现，防第二套提取逻辑）。
 */
class DriveTrainFactsProvider final : public IDriveTrainEvidenceFactsProvider {
public:
    DriveTrainFactsProvider() = default;

    /// 从映射输出提取事实 DTO（接口契约见 Facts.hpp）。
    DriveTrainEvidenceFacts facts(const DriveTrainMappingOutput& output) const override;
};

}  // namespace sdurws::ird::drivetrain

#endif  // IRD_DRIVETRAIN_EVALUATOR_HPP
