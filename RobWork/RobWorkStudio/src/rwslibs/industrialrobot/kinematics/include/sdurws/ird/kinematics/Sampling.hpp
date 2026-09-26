/**
 * @file   Sampling.hpp
 * @brief  区域采样与覆盖率（KIN-04）的采样通道值模型——SamplingPlan/
 *         RegionSamplingBudget 投影值、SampleRecord/SampleSet 样本值、
 *         确定性样本生成（Grid 体心/Random 种子序列/斐波那契螺旋方向集
 *         ×roll 均分——D-KIN-6 黄金锁定）、sampleSetIdentity 实现
 *         （evidence §4.1.4 公式——O-38/P-KIN-3 的 kinematics 侧实现面）
 *         与 IWorkspaceSampler 接口（§9.2 七个必需接口之四）。
 *
 * 设计依据：
 *   - units/kinematics.md §3.3（公共头布局表 Sampling.hpp 行——
 *     "IWorkspaceSampler＋样本生成（计划→确定性样本集；sampleSetIdentity
 *     实现）"，任务 T06）、§7.2（覆盖率图——定义→样本→分母→覆盖率→
 *     降级的全链语义＋职责边界"kinematics 执行采样并计算覆盖率（含
 *     sampleSetIdentity 实现）"）、§9.2（IWorkspaceSampler 接口契约
 *     原文——generateSamples/computeCoverage/evaluate 三方法）、§4.4
 *     （AnalysisConfiguration.regionBudget——采样预算/种子，KIN-13；
 *     本头承载其 T10 前的直传投影值）、§3.4（随机性唯一来源＝seed 派生
 *     确定性序列；数值恒 SI）
 *   - requirements 卡 §5.2（区域几何 Box＋位置/姿态采样定义的权威字段
 *     ——本头的 SamplingPlan 为其宿主解析投影，R-1 禁 include 其头）、
 *     I-REQ-6（采样计数 ≥0 合法——零样本场景）、D-REQ-2（GridBySpacing
 *     构建期规范化为计数——投影只收计数）
 *   - evidence 卡 §4.1.4（SamplingPlanRef 五字段——sampleSetIdentity
 *     ＝SHA-256 over (planContentIdentity ‖ 预算/种子 canonical)；
 *     plannedXxxSamples 为分母来源）、§6.6（RegionCoverageEvidence 校验
 *     面——本单元只产素材不裁定）
 *   - 治理裁决：O-37（宿主注入形态——本头 SamplingPlan 为评估宿主解析
 *     的投影值，req 对象 schema 不进本单元）；O-38/P-KIN-3（预算/种子
 *     canonical 编码字节三方一致义务——格式冻结前 kinematics 以快照
 *     SamplingPlanRef 对账为准、不一致即 DataInsufficient，设计上安全；
 *     本实现的 canonical 字节布局见 sampleSetIdentity 函数注，登记随
 *     单元卡 §14.6 v0.6，evidence 冻结时以此为准对齐）
 *   - REQUIREMENTS KIN-04（R3/R8 口径原文——存在性/全局双口径、分母＝
 *     计划样本总数、数据不足样本保留分母不计分子整体降级、零样本不定义、
 *     复评不得增删更换样本）、KIN-05（需要碰撞证据时缺检测器必须返回
 *     数据不足）
 *   - 任务契约 tasks/foundation/WP-15-T06.json acceptance 1~5
 *
 * 背景说明（样本状态五值词表的映射口径——单元卡 §7.2 词表的实现落值，
 * 登记随卡 §14.6 v0.6）：逐样本求解结局到样本状态的映射是本任务的核心
 * 语义决策——
 *   - Reached ⇐ 结局 1/4（存在 ≥1 个通过全部硬过滤的解——存在性口径
 *     的唯一凭据态）；
 *   - Unreachable ⇐ 结局 5（解析工作半径上界的确定性证明素材——需求
 *     C2③"解析界限"是唯一允许的确定性不可达路径；搜索类失败绝不入此态，
 *     C5/C8"数值搜索未找到有效解不构成不可行证明"）；
 *   - DataInsufficient ⇐ 结局 2/3（搜索未果→DataInsufficient 素材）或
 *     碰撞要求在场但缺检测器（KIN-05：绝不视为无碰撞——本单元以
 *     "要求＋无会话"判定，稳定诊断码 KIN-COLLISION-UNAVAILABLE 的产码
 *     面归 T07，§9.6 任务列分工）；
 *   - NotRun ⇐ 协作取消后未派发（partial 如实标记——§7.2"取消→不产
 *     正式覆盖率"，重跑同一样本集）；
 *   - NotApplicable 为词表完备性保留值（采样计划无停用语义——停用条目
 *     不入投影，生成面不产此态；覆盖缺口登记随卡 §14.6 v0.6）。
 *
 * 线程安全：本头全部实体为纯值/纯接口/无状态服务（无共享可变状态）；
 * 确定性：同 (plan, budget) 同样本集同序（D-KIN-6——复评不得增删更换
 * 样本的结构保证），载荷编码定宽小端＋字段定序（NFR-COR-01/02）。
 */

#ifndef IRD_KINEMATICS_SAMPLING_HPP
#define IRD_KINEMATICS_SAMPLING_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>

#include <sdurws/ird/core/Digest.hpp>           // ContentIdentity（计划/样本集身份）
#include <sdurws/ird/core/Identity.hpp>         // ObjectId/TaskIdentity（绑定面）
#include <sdurws/ird/core/Evaluation.hpp>       // EvaluationMode（模式词表）
#include <sdurws/ird/evidence/Evaluator.hpp>    // IEngineeringEvaluator/请求与产出（§9.3 调用契约）
#include <sdurws/ird/kinematics/Coverage.hpp>   // CoverageTotals/CoverageResult（覆盖率值面——T06）
#include <sdurws/ird/kinematics/Errors.hpp>     // KinematicsError（Expected 错误侧）
#include <sdurws/ird/kinematics/Evidence.hpp>   // BatchDemands/BatchCondition/IBatchCheckpointSink
                                                //   （碰撞要求值与批检查点端口的 T05 复用面）
#include <sdurws/ird/kinematics/KinTypes.hpp>   // IKinRuntimeView/IkOutcomeKind（O-37 注入面）
#include <sdurws/ird/runtime/Description.hpp>   // detail::identityTransform3D()——SampleRecord
                                                //   位姿恒等初值（Transform3D 默认构造走
                                                //   外联符号——冒烟模式不可链接，Ik.hpp 同款规避）
#include <sdurws/ird/runtime/Errors.hpp>        // runtime::Expected（非抛出查询轨载体）

namespace sdurws::ird::kinematics {

// =====================================================================
// 域常量（唯一书写点——评估器 descriptor/证据行/载荷编码/测试共用，禁
// 第二处字面量。落位说明：区域覆盖通道的键/契约版本常量唯一书写点在
// 本头（采样值面）而非 Evaluators.hpp，理由与 T05 批量键落 Evidence.hpp
// 相同——sampleSetIdentity 与样本生成（本头）先于评估器形态消费该身份，
// 放本头避免 include 环）
// =====================================================================

/// 评估键（evidence 词形闸门内 kebab 形态——卡面 §4.3 键名
/// "kin.region-coverage" 的实现形态；kebab 化偏差同 T03~T05 先例随卡
/// §14.6 登记。DependencyKey 才允许点）。
inline constexpr char kRegionCoverageEvaluationKey[] = "kin-region-coverage";

/// 评估器输入/输出契约版本（进 sliceId——CON-04；一经交付不回退）。
inline constexpr std::uint32_t kRegionCoverageContractVersion = 1U;

/// 域载荷登记 token（evidence §7.1 域词表——卡面点形保留；v1 随
/// canonical 布局 codec 版本演进）。
inline constexpr char kRegionCoveragePayloadToken[] = "kin.region-coverage.v1";

/// 区域覆盖证据行 id（evidence §6.2 EvidenceItem.itemId 词形 "<域>.<项>"
/// ——表 4 运动学行"区域覆盖率"的落位行；逐样本状态表在载荷内，证据行
/// 以 digest＋subject=regionObjectId 溯源绑定，登记随卡 §14.6 v0.6）。
inline constexpr char kKinRegionCoverageRowId[] = "kin.region-coverage";

/// 逐样本评估的进度/分批 phase token（reportProgress 的 phase 字段落值
/// ——§7.1/§8.3 批粒度上报的覆盖通道对应物）。
inline constexpr char kRegionCoveragePhase[] = "solve-sample";

// =====================================================================
// RegionSamplingBudget——采样预算/种子（KIN-13 regionBudget 的投影值）
// =====================================================================

/**
 * @brief 区域采样的预算/种子参数（AnalysisConfiguration.regionBudget 的
 *        T10 前直传投影——§4.4 schema 归 T10，本值只承载采样通道实际
 *        消费的字段；T10 落位后由 AnalysisConfiguration 投影填充）。
 *
 * 身份纪律（§8.4"线程配置与身份"行的覆盖通道落点）：seed 进入
 * sampleSetIdentity 的 canonical 字节（预算/种子参数——evidence §4.1.4
 * 公式第二元）；threadCount 是执行参数、**不入身份**（§8.4 总则"线程数
 * 是执行参数不进结果身份"——批量通道同款；AnalysisConfiguration 级的
 * canonical 含线程数属 KIN-13/T10 的 configDigest 面，与本身份分层——
 * D-04 双层身份：线程数变更改 sliceId、不改样本基准）。
 *
 * 值语义纯结构；线程安全（并发只读）。
 */
struct RegionSamplingBudget {
    /// 确定性随机种子（Random 位置采样的序列源；0 非法——I-KIN-4 拒绝，
    /// 不做 0→1 静默替换，NFR-COR-03；装配期 fail-fast 面）。无量纲。
    std::uint64_t seed = 0;
    /// 并行分片线程数（≥1；执行参数——逐样本求解的分片并行度，不入
    /// sampleSetIdentity，见结构体注）。无量纲计数。
    std::uint32_t threadCount = 1U;

    bool operator==(const RegionSamplingBudget& o) const
    {
        return seed == o.seed && threadCount == o.threadCount;
    }
    bool operator!=(const RegionSamplingBudget& o) const { return !(*this == o); }
};

// =====================================================================
// SamplingPlan——区域采样计划的宿主解析投影（requirements §5.2 定义面的
// kinematics 侧值；R-1 禁 include——O-37 宿主注入同款纪律）
// =====================================================================

/**
 * @brief 区域 Box 几何（requirements §5.2"R1 仅 Box（center＋size）"的
 *        投影——宿主已把 refFrame 系解析到基座系 {B}，§5.1 坐标纪律）。
 *
 * 约束（I-REQ-6 区域非退化）：size 三分量 >0 且有限（装配期 fail-fast
 * 面——非有限/非正的 Box 拒绝进入采样）。单位：m。
 * 值语义纯结构；线程安全（并发只读）。
 */
struct RegionBox {
    /// 盒中心（基座系 {B}，单位 m）。
    rw::math::Vector3D<double> center = rw::math::Vector3D<double>(0.0, 0.0, 0.0);
    /// 盒三边长（基座系 {B} 各轴对齐——轴对齐盒；单位 m；三分量 >0 有限）。
    rw::math::Vector3D<double> size = rw::math::Vector3D<double>(0.0, 0.0, 0.0);

    /// 精确等值（逐成员——身份面比较禁用数值容差；Vector3D 分量精确相等）。
    bool operator==(const RegionBox& o) const
    {
        return center == o.center && size == o.size;
    }
    bool operator!=(const RegionBox& o) const { return !(*this == o); }
};

/**
 * @brief 位置采样定义（requirements §5.2 PositionSampling 的投影——
 *        GridBySpacing 已被 requirements 在计划构建期规范化为 Grid 计数
 *        （counts[i]=floor(size[i]/spacing[i])+1，I-REQ-1/D-REQ-2），
 *        本投影只收规范化后的计数，不再实现间距语义）。
 *
 * 计数边界（I-REQ-6）：逐轴计数 ≥0 合法（任一轴为 0→计划位置样本数
 * ＝0——零样本场景，评估面判 DataInsufficient，本层不拒）；Random 的
 * count ≥0 同理。值语义纯结构；线程安全。
 */
struct PositionSamplingDefinition {
    /// 采样方法两值词表（requirements §5.2 原文方法集减去构建期已规范化
    /// 的 GridBySpacing）。
    enum class Method : std::uint8_t {
        /// 网格均分（体心规则——generateGridPositions 的黄金锁定语义）。
        Grid,
        /// 种子派生确定性伪随机（generateRandomPositions——§3.4 随机性
        /// 唯一来源）。
        Random,
    };

    /// 采样方法（缺省 Grid——黄金主径）。
    Method method = Method::Grid;
    /// Grid 逐轴计数（x/y/z 链序；单位无量纲；0 合法——零样本场景）。
    std::array<std::uint32_t, 3> gridCounts{1U, 1U, 1U};
    /// Random 样本数（无量纲计数；0 合法——零样本场景；仅 Random 消费）。
    std::uint32_t randomCount = 0U;

    bool operator==(const PositionSamplingDefinition& o) const
    {
        return method == o.method && gridCounts == o.gridCounts
            && randomCount == o.randomCount;
    }
    bool operator!=(const PositionSamplingDefinition& o) const
    {
        return !(*this == o);
    }
};

/**
 * @brief 姿态采样定义（requirements §5.2 OrientationSampling 的投影——
 *        方向集样本数×roll 样本数，供姿态覆盖率的全局口径采样）。
 *
 * 约束：directionSamples/rollSamples ≥1（requirements 字段表原文
 * "≥1"——装配期 fail-fast 面；因此位姿样本数＝P×D×R 随位置样本数同
 * 零——零样本判定只需看位置侧，见 generateSampleSet 注）。methodToken
 * 为 requirements 侧方法 token 的字面投影（本单元只承载不解释——词表
 * 权威归 requirements）。值语义纯结构；线程安全。
 */
struct OrientationSamplingDefinition {
    /// 方向集样本数（斐波那契螺旋方向数；≥1；无量纲计数）。
    std::uint32_t directionSamples = 1U;
    /// roll 均分样本数（≥1；无量纲计数）。
    std::uint32_t rollSamples = 1U;
    /// 方法 token（requirements 侧字面投影；允许空串＝宿主未登记——
    /// 本单元不消费其值，仅随投影透传供溯源）。
    std::string methodToken;

    bool operator==(const OrientationSamplingDefinition& o) const
    {
        return directionSamples == o.directionSamples
            && rollSamples == o.rollSamples && methodToken == o.methodToken;
    }
    bool operator!=(const OrientationSamplingDefinition& o) const
    {
        return !(*this == o);
    }
};

/**
 * @brief 区域采样计划投影（requirements SamplingPlan 条目的宿主解析值
 *        ——§7.2"requirements 定义区域与计划；kinematics 执行采样"。
 *
 * 投影纪律（O-37 裁决同款）：req-plan-set/req-region-set 的对象 schema
 * 不进本单元（R-1）；宿主（评估宿主）把切片内计划条目解析为基座系几何
 * ＋规范化计数后经工厂闭包注入。planContentIdentity 为 requirements 侧
 * 计算的计划 canonical 内容身份（区域定义＋采样参数——evidence
 * SamplingPlanRef.planContentIdentity 同源值），本单元原样入
 * sampleSetIdentity 与对账面，不重算、不解释。
 *
 * 镜像/分层（§7.2"镜像样本独立计数"的落位口径，登记随卡 §14.6 v0.6）：
 * 镜像阵列的派生条目由 requirements 层展开为**独立计划条目**（独立
 * planContentIdentity），宿主逐条注入；生成器对每个计划条目独立生成
 * 样本（定义→样本一对一确定，无隐藏分层），镜像派生计划的样本以独立
 * sampleIndex 入全集、独立计入分母——跨计划无去重。mirrorOf 仅承载
 * 派生溯源元数据（生成语义不消费）。
 *
 * demands 为区域级要求值投影（BatchDemands 复用——碰撞要求是覆盖通道
 * 消费的唯一要求值：collisionFreeRequired=true 且未注入碰撞会话→该计划
 * 全部样本 DataInsufficient（KIN-05 承接——缺检测器绝不视为无碰撞）；
 * minimumJointMargin 对覆盖率（存在性/全局口径）无语义，本通道不消费，
 * 载荷不携带）。值语义纯结构；线程安全（并发只读）。
 */
struct SamplingPlan {
    /// 工作区域对象身份（req.regions 闭包内——evidence SamplingPlanRef.
    /// regionObjectId 同源；对账与结果绑定的键）。
    core::ObjectId regionObjectId;
    /// 采样计划内容身份（requirements 侧 canonical——身份对账面原值）。
    core::ContentIdentity planContentIdentity;
    /// 区域 Box（基座系 {B}——宿主已解析 refFrame；单位 m）。
    RegionBox box;
    /// 位置采样定义（规范化计数——结构体注）。
    PositionSamplingDefinition position;
    /// 姿态采样定义（方向数×roll 数——结构体注）。
    OrientationSamplingDefinition orientation;
    /// 区域级要求值（碰撞要求——KIN-05 承接面；见结构体注）。
    BatchDemands demands;
    /// 镜像派生源区域身份（空＝原生条目——溯源元数据，生成不消费）。
    std::optional<core::ObjectId> mirrorOf;

    bool operator==(const SamplingPlan& o) const
    {
        return regionObjectId == o.regionObjectId
            && planContentIdentity == o.planContentIdentity && box == o.box
            && position == o.position && orientation == o.orientation
            && demands == o.demands && mirrorOf == o.mirrorOf;
    }
    bool operator!=(const SamplingPlan& o) const { return !(*this == o); }
};

// =====================================================================
// SampleRecord/SampleSet——样本值模型（§7.2 样本生成产物）
// =====================================================================

/**
 * @brief 样本种类两值词表（§7.2"位置样本/姿态样本"——逐样本评估接线
 *        的分流依据：位置样本＝位置 IK（存在性口径）、姿态样本＝完整
 *        位姿 IK（全局口径））。
 */
enum class SampleKind : std::uint8_t {
    /// 位置样本（评估＝位置存在性 IK——orientation 无约束，落值见
    /// Evaluators.cpp 逐样本求解注）。
    Position,
    /// 位姿样本（评估＝完整位姿 IK——(位置×姿态) 组合的全局口径）。
    Pose,
};

/**
 * @brief 单个采样点（§7.2 样本生成产物；值语义；线程安全并发只读）。
 *
 * 全部物理量单位与坐标系：position 单位 m、基座系 {B}（宿主已解析区域
 * refFrame）；pose 为完整位姿（基座系 {B}，平移 m／旋转 rad——仅 Pose
 * 样本有语义，Position 样本该字段为恒等占位不消费）。
 *
 * sampleIndex 是**全集全局序键**（0 起连续——"样本按 sampleIndex 对齐
 * 计划（分母完整性核查键）"，acceptance 4；全局序＝计划注入序×计划内
 * 生成序，单计划面与计划内序一致，登记随卡 §14.6 v0.6）。
 */
struct SampleRecord {
    /// 全局样本序（0 起连续；分母对齐与结果表对齐的唯一键）。
    std::uint64_t sampleIndex = 0;
    /// 来源区域对象身份（逐样本携带——跨计划集合的归属溯源）。
    core::ObjectId regionObjectId;
    /// 来源计划内容身份（逐样本携带——身份对账与报告分组键）。
    core::ContentIdentity planContentIdentity;
    /// 样本种类（位置/位姿——评估接线的分流依据）。
    SampleKind kind = SampleKind::Position;
    /// 采样位置（基座系 {B}，单位 m）。
    rw::math::Vector3D<double> position = rw::math::Vector3D<double>(0.0, 0.0, 0.0);
    /// 采样完整位姿（基座系 {B}——仅 Pose 样本有语义；平移 m／旋转 rad）。
    rw::math::Transform3D<double> pose = runtime::detail::identityTransform3D();
};

/**
 * @brief 确定性样本集（§7.2"计划→确定性样本集"的产物值）。
 *
 * 不变式（generateSampleSet 保证）：samples 按全局 sampleIndex 升序
 * 连续（0..N-1 无洞——分母完整性核查的结构前提）；plannedPositionSamples
 * ＝Σ计划位置样本数（Grid counts 乘积/Random count）、
 * plannedPoseSamples＝Σ（位置样本数×方向数×roll 数）——两值即
 * SamplingPlanRef 分母字段的对账源（KIN-04 R8"分母＝计划样本总数"）。
 *
 * 复评不变式（D-KIN-6/acceptance 4）：同 (plans, budget) 生成同样本集
 * 同序——"复评不得增删更换样本"由确定性生成＋sampleSetIdentity 对账
 * 双重保证。值语义；冻结后只读；线程安全（并发只读）。
 */
struct SampleSet {
    /// 全部样本（全局序升序连续）。
    std::vector<SampleRecord> samples;
    /// 计划位置样本总数（分母——存在性口径；与快照 SamplingPlanRef.
    /// plannedPositionSamples 对账）。
    std::uint64_t plannedPositionSamples = 0;
    /// 计划位姿样本总数（分母——全局口径；与快照 SamplingPlanRef.
    /// plannedPoseSamples 对账）。
    std::uint64_t plannedPoseSamples = 0;
};

// =====================================================================
// SampleState/SampleResultRecord/SampleResultSet——逐样本结果值模型
// =====================================================================

/**
 * @brief 单样本评估状态五值词表（§7.2 原文词表；映射口径见文件头
 *        "样本状态五值词表的映射口径"注——Reached/Unreachable/
 *        DataInsufficient/NotApplicable/NotRun 的求解结局映射）。
 */
enum class SampleState : std::uint8_t {
    /// 存在 ≥1 个通过全部硬过滤的解（结局 1/4——存在性/全局口径的分子态）。
    Reached,
    /// 解析界限确定性证明（结局 5——唯一确定性不可达路径；素材随载荷）。
    Unreachable,
    /// 数据不足（结局 2/3 搜索未果；或碰撞要求在场而缺检测器——KIN-05；
    /// 保留分母不计分子＋整体降级）。
    DataInsufficient,
    /// 不适用（词表完备性保留——生成面不产此态，枚举注见文件头）。
    NotApplicable,
    /// 未运行（协作取消后未派发——partial 如实标记，不产正式覆盖率）。
    NotRun,
};

/**
 * @brief 单样本评估结果记录（逐样本状态表——"覆盖证据＝逐样本状态表
 *        （canonical）＋汇总计数"（§7.2）的状态表元素）。
 *
 * 不变式（评估器保证）：state∈{NotApplicable,NotRun} 时 reason 必填非空
 * （ERR-01 显式标记不伪造）；outcomeKind 仅在样本实际进入求解时非空
 * （预终结/缺检测器轨无结局）。等值面说明：本结构定义 operator==
 * （全字段逐成员——状态表是覆盖证据的核心面，逐位对齐断言需要它；
 * 成员全为标量/枚举/短文本，无量等值歧义）。值语义；线程安全。
 */
struct SampleResultRecord {
    /// 对齐键（＝SampleRecord::sampleIndex——分母完整性核查键）。
    std::uint64_t sampleIndex = 0;
    /// 评估状态（五值——枚举注）。
    SampleState state = SampleState::NotRun;
    /// 求解结局（可空——实际求解的样本携带，供素材溯源；预终结/缺
    /// 检测器轨为空）。
    std::optional<IkOutcomeKind> outcomeKind;
    /// 碰撞未评价标记（策略未启用碰撞且无碰撞要求——检查不在范围；
    /// 绝不解读为无碰撞，KIN-05 口径同源）。
    bool collisionNotEvaluated = false;
    /// 原因文本（NotApplicable/NotRun 必填非空——ERR-01；缺检测器的
    /// DataInsufficient 也携带原因，其余状态为空）。
    std::string reason;

    bool operator==(const SampleResultRecord& o) const
    {
        return sampleIndex == o.sampleIndex && state == o.state
            && outcomeKind == o.outcomeKind
            && collisionNotEvaluated == o.collisionNotEvaluated
            && reason == o.reason;
    }
    bool operator!=(const SampleResultRecord& o) const { return !(*this == o); }
};

/**
 * @brief 逐样本结果集（computeCoverage 第二参数——与 SampleSet 按
 *        sampleIndex 双射对齐； completeness 核查在 computeCoverage 内
 *        执行，违例 logic_error 不静默）。值语义；线程安全。
 */
struct SampleResultSet {
    /// 逐样本结果（覆盖全部样本各恰一条——双射核查键 sampleIndex）。
    std::vector<SampleResultRecord> results;
};

// =====================================================================
// sampleSetIdentity——样本集身份（evidence §4.1.4 公式的实现落点；
// O-38/P-KIN-3 三方一致义务的 kinematics 侧）
// =====================================================================

/**
 * @brief 计算单个采样计划的样本集身份（evidence §4.1.4 公式：
 *        sampleSetIdentity ＝ SHA-256 over (planContentIdentity ‖
 *        预算/种子参数 canonical)）。
 *
 * canonical 字节布局（codec 版本 1——**格式冻结前**的 kinematics 实现
 * 落值，登记随单元卡 §14.6 v0.6；evidence 卡冻结 canonical 格式时以此
 * 为准对齐——P-KIN-3 三方一致义务；冻结前对账面＝快照 SamplingPlanRef
 * 逐字节比对，不一致即 DataInsufficient，本布局的任何漂移都会被对账
 * 捕获，设计上安全）：
 * @code
 *   "IRDSSID1"（8 字节 ASCII magic）
 *   ‖ u32 codecVersion = 1（小端）
 *   ‖ u32 planIdLength（小端）‖ planContentIdentity.toCanonical()（ASCII，
 *     形如 "cid-<64hex>"——规范文本字节，与 core 规范文本面同源）
 *   ‖ u64 seed（小端——预算/种子参数 canonical 的当前全集；Grid 采样
 *     不消费 seed，但 seed 仍入身份：预算/种子是采样计划的决定输入之一，
 *     KIN-04 R8/KIN-13 明文；线程数不入身份——执行参数，见
 *     RegionSamplingBudget 注）
 * @endcode
 *
 * "身份对计划参数计算而非对枚举列表"（evidence §4.1.4 原文）——本函数
 * 只消费计划身份与预算/种子，不枚举样本点（大样本集不枚举入快照）。
 *
 * @param planContentIdentity [in] 计划内容身份（requirements 侧 canonical
 *                             原值；允许零值——零值照实入摘要，对账面
 *                             由快照侧决定语义）
 * @param budget              [in] 采样预算/种子（seed 入摘要；threadCount
 *                             不入——执行参数）
 * @return 样本集身份（32 字节摘要——SamplingPlanRef.sampleSetIdentity
 *         的计算源）
 *
 * 纯函数；线程安全；确定性（同输入同字节同摘要——NFR-COR-01/02）。
 */
core::ContentIdentity sampleSetIdentity(const core::ContentIdentity& planContentIdentity,
                                        const RegionSamplingBudget& budget);

// =====================================================================
// 确定性样本生成（D-KIN-6 黄金锁定面——生成规则逐函数登记，修改走
// 设计变更并同步卡面 §7.2）
// =====================================================================

/**
 * @brief Grid 体心规则位置采样（§7.2"Grid counts → 均分坐标（体心规则
 *        黄金锁定）"的唯一实现点）。
 *
 * 生成规则（D-KIN-6；黄金锁定 T13）：盒 [min,max]（min=center−size/2，
 * max=center＋size/2——基座系 {B}，m）逐轴均分为 counts[i] 格，样本取
 * 格心：coord_i(k_i) ＝ min_i ＋ (k_i ＋ 0.5)·(size_i / counts_i)。枚举序
 * ＝x 最慢、z 最快（index ＝ (ix·counts[1]＋iy)·counts[2]＋iz——黄金
 * 锁定序）；任一轴 counts=0 → 0 个样本（零样本场景，I-REQ-6）。
 *
 * @param box    [in] 区域盒（基座系 {B}；size 三分量 >0 有限——调用方
 *               契约，装配校验面保证）
 * @param counts [in] 逐轴计数（无量纲；0 合法——零样本）
 * @return 采样位置列（生成序＝枚举序；单位 m，基座系 {B}）
 *
 * 纯函数；线程安全；确定性（与 seed 无关——Grid 不消费种子）。
 */
std::vector<rw::math::Vector3D<double>>
generateGridPositions(const RegionBox& box, const std::array<std::uint32_t, 3>& counts);

/**
 * @brief 种子派生伪随机位置采样（§7.2"Random→seed 派生序列"的唯一实现
 *        点；§3.4"随机性唯一来源＝seed 派生确定性序列"）。
 *
 * 生成规则（D-KIN-6；黄金锁定 T13）：第 k 个样本（0 起）的序列状态
 * state ＝ seed·2³² ＋ k（混入样本序——与 Ik.cpp 初值生成同构的确定性
 * 纪律），逐轴 u∈[0,1) 由 splitmix64(state) 连续派生（x→y→z 各演进一步
 * ——同初值生成"逐轴连续派生"的同款形态），坐标＝min ＋ u·size。
 *
 * @param box  [in] 区域盒（基座系 {B}；单位 m）
 * @param count [in] 样本数（无量纲；0 → 空集）
 * @param seed [in] 确定性种子（0 非法——I-KIN-4；装配校验面保证非 0）
 * @return 采样位置列（生成序＝k 序；单位 m，基座系 {B}）
 *
 * 纯函数；线程安全；确定性（同 (count,seed) 同序列——跨进程逐位一致）。
 */
std::vector<rw::math::Vector3D<double>>
generateRandomPositions(const RegionBox& box, std::uint32_t count, std::uint64_t seed);

/**
 * @brief 斐波那契螺旋方向集（§7.2"方向集（斐波那契螺旋，黄金锁定）"
 *        的唯一实现点）。
 *
 * 生成规则（D-KIN-6；黄金锁定 T13）：第 j 个方向（0 起）——
 * z_j ＝ 1 − (2j＋1)/N；r_j ＝ √(1−z_j²)；φ_j ＝ j·goldenAngle（黄金角
 * ＝π(3−√5) rad，常量字面锁定）；方向＝(r_j·cosφ_j, r_j·sinφ_j, z_j)
 * （单位向量，基座系 {B}——区域投影系即基座系，方向不做额外坐标变换）。
 *
 * @param count [in] 方向数（≥1——装配校验面保证；N=1 时退化为 +Z 单方向）
 * @return 单位方向列（生成序＝j 序；无量纲，‖d‖=1）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<rw::math::Vector3D<double>> generateDirections(std::uint32_t count);

/**
 * @brief roll 均分角序列（§7.2"roll 均分"的唯一实现点）。
 *
 * 生成规则（D-KIN-6；黄金锁定 T13）：体心规则与 Grid 同源——第 r 个
 * roll（0 起）＝ −π ＋ (r ＋ 0.5)·(2π/M)（rad；[−π,π) 全周均分的格心
 * ——与位置 Grid 的体心口径一致）。
 *
 * @param count [in] roll 数（≥1——装配校验面保证）
 * @return roll 角列（生成序＝r 序；单位 rad）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<double> generateRolls(std::uint32_t count);

/**
 * @brief 由方向与 roll 组装完整姿态（姿态样本的位姿语义落值——登记随
 *        卡 §14.6 v0.6，黄金锁定 T13）。
 *
 * 组装规则（D-KIN-6）：R ＝ R_dir(d)·Rz(roll)——R_dir 为把工具 z 轴
 * （+Z）旋到方向 d 的最小旋转（Rodrigues 轴角：轴＝ẑ×d、角＝两者夹角；
 * d＝±ẑ 时退化为恒等/绕 z 半周——数值稳定分支）；再绕**局部 z 轴**转
 * roll（工具滚转的物理语义）。平移＝样本位置（TCP 目标点）。
 *
 * @param direction [in] 单位方向（‖d‖=1——调用方契约；基座系 {B}）
 * @param roll      [in] 滚转角（rad；[−π,π) 值域由 generateRolls 保证，
 *                  本函数不归一化）
 * @param position  [in] 样本位置（基座系 {B}，单位 m）
 * @return 完整目标位姿（基座系 {B}——T_base_tcp 目标值）
 *
 * 纯函数；线程安全；确定性。
 */
rw::math::Transform3D<double> poseFromDirectionRoll(const rw::math::Vector3D<double>& direction,
                                                    double roll,
                                                    const rw::math::Vector3D<double>& position);

/**
 * @brief 计划位置样本数（Grid counts 乘积/Random count——分母的第一
 *        计算点，生成与对账同源防两套真值漂移）。
 *
 * @param plan [in] 采样计划投影
 * @return 计划位置样本总数（无量纲；Grid 逐轴乘积含 0 因子→0——零样本；
 *         Random→randomCount）
 *
 * 纯函数；线程安全；确定性。
 */
std::uint64_t plannedPositionSampleCount(const SamplingPlan& plan);

/**
 * @brief 计划位姿样本数（位置样本数×方向数×roll 数——全局口径分母；
 *        (位置×姿态) 组合的计数语义，KIN-04 R3）。
 *
 * @param plan [in] 采样计划投影
 * @return 计划位姿样本总数（无量纲；位置侧为 0 → 0）
 *
 * 纯函数；线程安全；确定性。
 */
std::uint64_t plannedPoseSampleCount(const SamplingPlan& plan);

/**
 * @brief 生成多计划的确定性样本集（§7.2"计划→样本"全链的唯一实现点
 *        ——IWorkspaceSampler::generateSamples（单计划入口）与评估器
 *        多计划编排的共同计算核）。
 *
 * 生成序（黄金锁定，登记随卡 §14.6 v0.6）：逐计划按注入序；计划内先
 * 全部位置样本（Grid 枚举序/Random k 序）、后全部位姿样本（位置序×
 * 方向 j 序×roll r 序——index ＝ (pi·D＋j)·R＋r）；全局 sampleIndex 在
 * 跨计划间连续累加（0 起无洞——SampleSet 不变式）。
 *
 * @param plans  [in] 计划投影集（注入序＝生成序；空集→空样本集合法
 *               ——评估面以 NotApplicable 行显式标记，不伪造区域）
 * @param budget [in] 采样预算/种子（seed 消费面＝Random 位置采样；
 *               Grid 不消费——同 (plans,budget) 同输出由 seed 一致性
 *               保证）
 * @return 确定性样本集（分母两值同算——对账源）
 *
 * @throws std::logic_error 内部不变量破坏（样本数≠分母计数——实现缺陷，
 *         不静默）
 *
 * 纯函数；线程安全；确定性（D-KIN-6——复评同集同序）。
 */
SampleSet generateSampleSet(const std::vector<SamplingPlan>& plans,
                            const RegionSamplingBudget& budget);

/**
 * @brief 覆盖率计算（§7.2 覆盖率图的唯一实现点——IWorkspaceSampler::
 *        computeCoverage 与评估器编排共用的自由函数面，NFR-MNT-04
 *        "比较/统计唯一实现点"纪律的 T04 sortSolutions/deduplicate-
 * Solutions 同款先例）。
 *
 * 计算口径（KIN-04 R3/R8；逐条）：
 *   - 分母＝set 的计划样本总数（position.planned＝plannedPositionSamples、
 *     orientation.planned＝plannedPoseSamples——禁止按评估结果剔除样本）；
 *   - 逐样本状态按五值归轴累计：Reached 入分子；Unreachable/
 *     DataInsufficient/NotRun/NotApplicable 单独计数（数据不足保留分母
 *     不计分子；NotRun>0 ⇒ incomplete）；
 *   - 双射核查：results 的 sampleIndex 集合必须与 set.samples 完全一致
 *     （分母完整性核查键——缺项/重复/越界即 std::logic_error）；
 *   - defined 标记＝分母>0；零样本轴比率不定义（无比率字段——Coverage.hpp
 *     文件头注）；
 *   - downgraded＝任一轴 dataInsufficient>0 或任一轴分母为 0。
 *
 * @param set     [in] 确定性样本集（分母来源）
 * @param results [in] 逐样本结果集（双射对齐——违例 std::logic_error）
 * @return 覆盖率结果（整数计数＋标记；同输入同值——确定性）
 *
 * @throws std::logic_error 双射核查失败（调用方组装缺陷——不静默）
 *
 * 纯函数；线程安全；确定性。
 */
CoverageResult computeCoverage(const SampleSet& set, const SampleResultSet& results);

// =====================================================================
// PlanIdentityCheck/RegionCoverageComputation——对账面与覆盖计算结果值
// （评估器编排产出→载荷编码输入的中间值；T05 BatchComputation 同位）
// =====================================================================

/**
 * @brief 单计划的身份对账结果（acceptance 3——评估器重算 sampleSetIdentity
 *        与快照 SamplingPlanRef 比对的逐计划记录）。
 *
 * 语义：matched=true ⇔ 快照内存在该区域的 SamplingPlanRef 且身份逐字节
 * 一致且分母两值一致（身份/分母任一不符即对账失败——分母字段同属冻结
 * 凭据，evidence §4.1.4"plannedXxxSamples 为分母来源"）；对账失败的
 * 计划绝不参与评估（未冻结/不一致样本集绝不沿用——评估器在任一
 * matched=false 时终止于 KIN-SAMPLE-IDENTITY-MISMATCH 素材轨）。值语义；
 * 线程安全（并发只读）。
 */
struct PlanIdentityCheck {
    /// 区域对象身份（对账键——SamplingPlanRef.regionObjectId 同源）。
    core::ObjectId regionObjectId;
    /// 计划内容身份（原值透传——重算摘要的第二输入）。
    core::ContentIdentity planContentIdentity;
    /// 评估器重算的样本集身份（sampleSetIdentity 函数产出）。
    core::ContentIdentity computedIdentity;
    /// 对账结论（true＝快照 ref 存在且身份/分母全一致）。
    bool matched = false;
    /// 重算的计划位置样本总数（分母对账源——与 ref 同值时 matched 才
    /// 可能成立）。
    std::uint64_t plannedPositionSamples = 0;
    /// 重算的计划位姿样本总数（分母对账源——同上）。
    std::uint64_t plannedPoseSamples = 0;

    bool operator==(const PlanIdentityCheck& o) const
    {
        return regionObjectId == o.regionObjectId
            && planContentIdentity == o.planContentIdentity
            && computedIdentity == o.computedIdentity && matched == o.matched
            && plannedPositionSamples == o.plannedPositionSamples
            && plannedPoseSamples == o.plannedPoseSamples;
    }
    bool operator!=(const PlanIdentityCheck& o) const { return !(*this == o); }
};

/**
 * @brief 区域覆盖计算结果（评估器编排产出→载荷编码输入的中间值——
 *        绑定块/逐计划对账/样本与结果/覆盖率的全量承载）。
 *
 * 不变式（评估器保证）：对账全通过时 identityChecks 全部 matched=true
 * （任一 false 即终止于素材轨、本结构不产出载荷）；samples/results 按
 * sampleIndex 双射对齐；coverage 由 computeCoverage 从 (samples,
 * results) 计算——三者的守恒式在载荷编码前复核（违例 logic_error）。
 * 值语义；冻结后只读。确定性：同输入同值（NFR-COR-01）。
 */
struct RegionCoverageComputation {
    // ---- 结果绑定（§5.6 六要素——由评估请求填充）----
    core::ContentIdentity snapshotId;   ///< 来源快照内容身份
    core::ContentIdentity sliceId;      ///< 冻结输入切片身份（CON-04）
    core::ContentIdentity configDigest; ///< 求解配置摘要（T10 落位前可零值）
    core::EvaluationMode mode = core::EvaluationMode::Verified; ///< 评估模式
    std::uint64_t seed = 0;             ///< 确定性种子（采样预算——身份面）
    std::vector<double> referenceQ;     ///< 排序参考构型（rad|m——D-KIN-4）
    core::TaskIdentity task;            ///< 任务五元组（载荷绑定面）

    // ---- 逐计划身份对账（acceptance 3——冻结凭据核对记录）----
    /// 逐计划对账结果（计划注入序）。
    std::vector<PlanIdentityCheck> identityChecks;

    // ---- 样本与结果（§7.2 覆盖证据＝逐样本状态表＋汇总计数）----
    /// 确定性样本集（分母来源——D-KIN-6 复评同集同序）。
    SampleSet samples;
    /// 逐样本结果（与 samples 双射对齐）。
    SampleResultSet results;
    /// 覆盖率计算结果（双口径计数＋标记——Coverage.hpp）。
    CoverageResult coverage;
};

// =====================================================================
// IWorkspaceSampler——区域采样与覆盖率接口（§9.2 七个必需接口之四原文）
// =====================================================================

/**
 * @brief 区域采样与覆盖率接口（KIN-04；key="kin.region-coverage"——
 *        §9.2 接口契约原文的三方法形态）。
 *
 * 契约（§9.2 行内联原文）：
 *   - generateSamples：样本集生成（独立入口，供契约测试与检查点续跑
 *     核对 sampleSetIdentity）；@post sampleSetIdentity 与快照
 *     SamplingPlanRef 一致（不一致→调用方证据缺失路径——对账在评估器
 *     evaluate 面执行，本入口只保证"同 (plan,budget) 同样本集同序"的
 *     确定性前提）；@确定性 同 (plan, budget, seed) → 同样本集（同序）。
 *   - computeCoverage：覆盖率计算（§7.2 图；分母＝计划样本总数；零样本
 *     →DataInsufficient 素材——defined 标记面，绝不输出比率）。
 *   - evaluate：evidence 评估器契约（实现类的装配注入形态见 Evaluators.hpp
 *     WorkspaceSampler——本头只定接口形状）。
 *
 * generateSamples 的错误轨：Expected 的 E 侧承载装配类调用方错误
 * （plan 恒等校验失败等——KinematicsError 值；§9.1"非异常出口"）。
 * 本单元无状态服务实现下，合法输入不产生错误侧（校验前置在装配面）。
 */
class IWorkspaceSampler : public evidence::IEngineeringEvaluator {
public:
    virtual ~IWorkspaceSampler() = default;

    /**
     * @brief 样本集生成（独立入口——§9.2 原文；单计划面）。
     *
     * @param plan   [in] 采样计划投影（调用方持有）
     * @param budget [in] 采样预算/种子
     * @return 成功＝该计划的样本集（sampleIndex 为计划内局部序 0 起；
     *         多计划全局序由编排层偏移——generateSampleSet 注）；失败＝
     *         KinematicsError（调用方错误值面——不抛）
     *
     * 纯函数；线程安全；确定性（D-KIN-6）。
     */
    virtual runtime::Expected<SampleSet, KinematicsError>
    generateSamples(const SamplingPlan& plan, const RegionSamplingBudget& budget) const = 0;

    /**
     * @brief 覆盖率计算（§7.2 图——分母＝计划样本总数；整数计数无浮点
     *        容差；零样本→defined=false＋降级素材）。
     *
     * @param set     [in] 确定性样本集（分母来源——调用方持有）
     * @param results [in] 逐样本结果集（与 set 按 sampleIndex 双射对齐
     *                ——违例 std::logic_error）
     * @return 覆盖率结果（整数计数＋defined/降级标记——不含比率字段，
     *         "绝不输出 0% 或 100%"的结构保证）
     *
     * 纯函数；线程安全；确定性。
     */
    virtual CoverageResult computeCoverage(const SampleSet& set,
                                           const SampleResultSet& results) const = 0;

    /// evidence 评估器契约（§9.3 调用约定；实现见 Evaluators.hpp）。
    evidence::EvaluationOutput evaluate(const evidence::EvaluationRequest& request,
                                        evidence::IEvaluationContext& context) override = 0;
};

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_SAMPLING_HPP
