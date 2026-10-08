/**
 * @file   SeriesBuilder.hpp
 * @brief  动力学序列构建器（units/dynamics.md §10.3 IDynamicsSeriesBuilder
 *         的具体实现载体）——从 RNEA 逐样本输出构建 DynamicsSeries：排序
 *         纪律、完整性计数、身份块冻结与 canonical 内容身份计算的唯一
 *         执行点（§4.4/§4.6）。
 *
 * 设计依据：
 *   - units/dynamics.md §10.3（IDynamicsSeriesBuilder 契约表：addSample/
 *     finalize 两方法、前置"样本时间单调非降——违例 DYN-SERIES-NON-
 *     MONOTONIC 不排序修复；finalize 后不可变"、职责行"排序校验/
 *     planned/actual 计数/缺口与非有限标记/身份块冻结/contentIdentity
 *     计算"）、§4.4（DynamicsSeries 数据模型）、§4.6（序列纪律——排序/
 *     缺口/非有限/Empty 语义）、§10.0（通用约定——缺身份拒绝）
 *   - 需求 DYN-03（每轴转角/位移、转速、加速度、类型化广义力、机械功率
 *     序列输出——本构建器即序列输出面的组装点）、CON-05（内容寻址——
 *     contentIdentity）、NFR-COR-02（稳定排序与确定性）、NFR-COR-03
 *     （非有限数不静默）
 *   - 决策 D-DYN-9 无关本头（RMS 口径归 Envelope）；本头执行的是"序列
 *     形态纪律"——六形态不可混用（§4.6 形态区分行）中的第二形态
 *     （动力学时间序列）的唯一合法入口
 *   - 任务契约 tasks/foundation/WP-17-T04.json（acceptance 1"序列输出"）
 *
 * ★ 契约形态微调（诚实登记，DTB §5.4 精神——单元卡 §1.2 同步登记）：
 *   1. §10.3 的接口以**具体类**形态落地（同 T03 先例——evidence 适配与
 *      注册面随 WP-17-T10 装配冻结），方法签名与卡面一致
 *      （addSample(finalize(SeriesIdentity))）。
 *   2. SeriesIdentity 为本头定义的身份块 DTO（§4.4 DynamicsSeries 身份块
 *      字段＋plannedSampleCount＋validitySeed 事实透传）——卡未给出
 *      SeriesIdentity 字段级定义，本落地面为最直接对应；planned/actual
 *      计数中 planned 来自调用方（构建器无法从行集反推上游计划采样数，
 *      缺口检测必须由计划数参与——§4.6"plannedSampleCount−
 *      actualSampleCount＝缺口"），validitySeed 透传评估器产出的来源
 *      事实（frictionMissing/估算计数/forwardCheck——构建器无此事实
 *      通道，覆写即丢事实）。
 *   3. 非法示例的处置分解（卡 §10.3"重复时间戳 addSample 不报错但
 *      finalize 标记"＋前置行"违例→DYN-SERIES-NON-MONOTONIC"两句的
 *      交界面）：时间**倒退**（t < 前行 t）＝前置行明文违例→addSample
 *      即抛 DynamicsError（fail-fast，调用方错误轨）；同刻度**重复
 *      (t, jointIndex) 行对**＝非法示例的"不报错但 finalize 标记"→
 *      finalize 检出后向 diagRefs 追加 DYN-INPUT-INVALID 语义素材并把
 *      完整性压为 Partial（正常评估器输出无此形态——防御性二次校验，
 *      §4.3 上游校验才是正式拒绝点）。
 *
 * 背景说明（为什么构建器不校验五分项恒等式）：τ_total=Σ分项是 RNEA 的
 *   被验证性质（§5.2——黄金算例 V-03 校验），不是构建器的纪律职责
 *   （§10.3 职责行只列排序/计数/标记/冻结/内容身份五项）；构建器对
 *   任意来源的样本行照收——统计器同样如此（消费任意 DynamicsSeries），
 *   "谁产出谁负责"的分层使黄金对照可以独立于组装链进行。
 *
 * 线程安全：实例单线程使用（每工况一构建器——§10.0）；finalize 后
 *   内部缓冲清空、实例可复用于下一工况（同线程）。
 * 确定性：同（样本行序＋身份块）→同 contentIdentity（SHA-256 纯字节
 *   变换，NFR-COR-02；编码规则见 DynamicsSeries 类注释与 SeriesBuilder
 *   .cpp 文件头）。
 */

#ifndef IRD_DYNAMICS_SERIESBUILDER_HPP
#define IRD_DYNAMICS_SERIESBUILDER_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>     // core::ObjectId/TaskIdentity（身份块分量）
#include <sdurws/ird/core/Digest.hpp>       // core::ContentIdentity（内容身份值类型）
#include <sdurws/ird/dynamics/DynTypes.hpp> // DynamicsSample/DynamicsSeries/DynamicsValidity
#include <sdurws/ird/dynamics/Errors.hpp>   // DynamicsError（fail-fast 异常轨——契约面）
#include <sdurws/ird/dynamics/InverseDynamics.hpp> // kInverseDynContractVersion/
                                                   //   kRneaAlgorithmVersion（契约/算法
                                                   //   版本常量唯一书写点）

namespace sdurws::ird::dynamics {

// =====================================================================
// 序列身份块（finalize 的入参——§10.3 SeriesIdentity 的域内落地形态，
// 见文件头微调登记 2）。
// =====================================================================

/**
 * @brief 序列身份块（§4.4 DynamicsSeries 身份块＋计划数＋来源事实的
 *        冻结入参；全部值语义）。
 *
 * 完整性校验（finalize，§10.0"身份要求"行——缺身份拒绝）：snapshotId/
 * sliceId/trajectoryPayloadId 三内容身份必须非全零（isValid）；conditionId
 * 必须有效；task 五元组必须 isValid；algorithmVersion/dynConfigDigest
 * 必须非空（空串＝配置身份缺失——结果不可追溯）。toolObjectId 允许空
 * id（无工具模型是合法形态——评估器同口径）。
 *
 * validitySeed 语义：调用方把评估器产出的 DynamicsValidity（来源事实：
 * frictionMissing/estimatedLinkCount/estimatedPayloadCount/
 * externalValidationPending/forwardCheck）原样传入；构建器**只覆写**其
 * 派生字段（completeness/plannedSampleCount/actualSampleCount/
 * nonFiniteCount——由行集与 plannedSampleCount 重新计算），事实字段
 * 逐位透传。默认构造（全 Empty/0/false/NotRun）即"无来源事实"。
 */
struct SeriesIdentity {
    // —— 身份块（§4.4 DynamicsSeries 身份块逐字段同序）——
    core::ContentIdentity snapshotId;          ///< 绑定快照（全零＝调用方错误）
    core::ContentIdentity sliceId;             ///< 绑定切片（当前性判据；全零＝调用方错误）
    core::ContentIdentity trajectoryPayloadId; ///< 上游轨迹 payload 内容身份
                                               ///<   （全零＝调用方错误）
    core::ObjectId conditionId;                ///< 工况对象 ID（空 id＝调用方错误）
    core::ObjectId toolObjectId;               ///< 工具对象 ID（无工具模型＝空 id——合法）
    std::uint32_t evaluatorContractVersion = static_cast<std::uint32_t>(
        kInverseDynContractVersion);           ///< 本域契约版本（默认当前版——
                                               ///<   消费方显式传入历史版本值亦可）
    std::string   algorithmVersion{std::string{kRneaAlgorithmVersion}}; ///<
                                               ///<   RNEA/统计实现版本 token（默认当前）
    std::string   dynConfigDigest;             ///< config.dyn 摘要（空串＝调用方错误）
    core::TaskIdentity task;                   ///< 运行身份五元组（isValid 必须成立）

    // —— 计划数与来源事实（见类型注释）——
    std::size_t plannedSampleCount = 0;        ///< 计划样本时刻数（轨迹采样×抽取；
                                               ///<   缺口检测的分母——§4.6）
    DynamicsValidity validitySeed;             ///< 来源事实透传（评估器 validity 的
                                               ///<   事实字段；派生字段被构建器覆写）
};

// =====================================================================
// 序列构建器（§10.3 IDynamicsSeriesBuilder 的具体实现载体——域内具体
// 类；每工况一实例）。
// =====================================================================

/**
 * @brief 动力学序列构建器（§10.3——DynamicsSeries 的唯一合法组装入口）。
 *
 * 职责（一次 finalize 的执行序——§10.3 职责行逐步对应）：
 *   1. addSample 逐行收集（时间单调非降防御校验——倒退即抛）；
 *   2. finalize：身份块完整性校验（缺一即 DynamicsError）→行序稳定
 *      排序 (t, jointIndex)→重复 (t, jointIndex) 行对检出（diagRefs
 *      标记＋压 Partial）→完整性计数派生（Empty/Complete/Partial——
 *      §4.6）→validitySeed 事实字段透传→canonical 序列化计算
 *      contentIdentity（SHA-256）→冻结返回；
 *   3. finalize 后内部缓冲清空（实例可复用；已产出的 DynamicsSeries
 *      为独立值——构造后不可变）。
 */
class DynamicsSeriesBuilder {
public:
    DynamicsSeriesBuilder() = default;

    /// 可拷贝可移动（纯值缓冲——实例仅是组装边界）。
    DynamicsSeriesBuilder(const DynamicsSeriesBuilder&) = default;
    DynamicsSeriesBuilder& operator=(const DynamicsSeriesBuilder&) = default;

    /**
     * @brief 追加一个样本行（§10.3 addSample——[in] 值拷贝，本类不持有
     *        调用方内存）。
     *
     * @param sample [in] 样本行（行内字段语义见 DynamicsSample；行序应
     *               为 (t 非降, 同刻度 jointIndex 升序)——评估器输出序，
     *               同刻度内乱序由 finalize 排序修复）
     *
     * @throws DynamicsError 时间倒退（t < 前一行 t——DYN-SERIES-NON-
     *         MONOTONIC 语义，token "series-non-monotonic"）：评估器不
     *         排序修复、不插值抹平（§4.6），构建器同口径 fail-fast。
     *         同刻度（t == 前一行 t）不在此拒收——正常逐关节行即同刻度
     *         多行；完全重复的 (t, jointIndex) 行对由 finalize 标记。
     */
    void addSample(const DynamicsSample& sample);

    /**
     * @brief 冻结并返回序列（§10.3 finalize——[in] 身份块，[out] 冻结
     *        序列；调用后内部缓冲清空）。
     *
     * @param identity [in] 序列身份块（完整性校验见 SeriesIdentity 注释）
     * @return 冻结的 DynamicsSeries（身份块从 identity 逐字段拷入；行序
     *         稳定排序；contentIdentity 已计算）
     *
     * @throws DynamicsError 身份块不完整（snapshotId/sliceId/
     *         trajectoryPayloadId 全零、conditionId 空 id、task 不
     *         isValid、algorithmVersion/dynConfigDigest 空串——调用方
     *         错误轨，§10.0"缺身份拒绝"）
     */
    DynamicsSeries finalize(SeriesIdentity identity);

    /// 已收集的样本行数（测试/进度观测用——纯函数、不抛）。
    std::size_t sampleCount() const noexcept { return mSamples.size(); }

private:
    std::vector<DynamicsSample> mSamples; ///< 待冻结样本行（addSample 追加序——
                                          ///<   finalize 时稳定排序）
    bool mHaveLastT = false;              ///< 是否已有前行时间（首行免比较）
    double mLastT = 0.0;                  ///< 前一行样本时间，单位 s（单调非降校验基准）
};

}  // namespace sdurws::ird::dynamics

#endif  // IRD_DYNAMICS_SERIESBUILDER_HPP
