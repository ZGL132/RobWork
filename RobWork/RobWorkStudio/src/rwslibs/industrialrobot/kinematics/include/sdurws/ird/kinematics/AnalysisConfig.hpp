/**
 * @file   AnalysisConfig.hpp
 * @brief  求解配置与显示单位（KIN-12/13，T10 批次）——AnalysisConfiguration
 *         schema（§4.4/schemaVersion=1）、canonical 编解码
 *         （IAnalysisConfigurationCodec——入快照 configurationRefs）、
 *         configDigest 摘要出口、配置变更依赖提示（L-K7/AT-27——只提示
 *         不自动重算）与显示单位纯投影（KIN-12——换算唯一经 core Units）。
 *
 * 设计依据：
 *   - units/kinematics.md §3.3（公共头布局表 AnalysisConfig.hpp 行——
 *     "AnalysisConfiguration schema＋IAnalysisConfigurationCodec（canonical
 *     编码，入 configurationRefs）"，任务 T10）、§4.4（AnalysisConfiguration
 *     schema 逐字段＋canonical 编码纪律"定宽小端＋字段定序＋集合字典序"
 *     ＋configDigest=SHA-256 入 configurationRefs/sliceId、不入
 *     inputBaselineId）、§4.5（身份外元素清单——显示单位纯投影，切换不改
 *     SI 真值/不产生修订/不触发重算）、§9.5（求解配置失效面速查——
 *     config.ik 列全四行命中；样本基准仅随 plan＋budget/seed 变）、§9.6
 *     行 12（KIN-CONFIG-ILLEGAL——求解配置非法的产码消费任务＝T10）、
 *     §9.8 L-K7/L-K8（配置修改提示/单位切换重投影）、§3.4 条 4（随机性
 *     唯一来源＝AnalysisConfiguration.seed 派生确定性序列）
 *   - REQUIREMENTS KIN-12（显示单位纯投影；扩展单位 inch/grad/turn 为
 *     KIN-12-S1/R2——不提前实现）、KIN-13（求解配置：初值策略/数量/迭代
 *     上限/容差/去重阈值/区域采样预算（及线程数）；独立于用户级设置
 *     （PM-14）持久化，进入运行身份与缓存身份；修改后按实际输入依赖提示
 *     受影响结果需重算；碰撞启用状态不由本条拥有、判定阈值归工程策略）、
 *     AT-27（单位切换不改 SI 真值不触发重算；求解配置修改按依赖失效并
 *     提示）、PM-14（用户设置持久化不入 .rwdesign）、I-KIN-4（seed=0
 *     非法拒绝、不做 0→1 静默替换）、NFR-COR-03（非法输入拒绝不钳制）、
 *     V-17/V-20/V-21（显示单位无失效/种子入身份/线程数入身份）、V13-01
 *     （纯显示开关不改变计算结果或缓存身份）
 *   - evidence 卡 §4.1.2（ConfigEntry——配置引用的不透明 canonical 承载）、
 *     §4.2/§5.1（D-04 双层身份：Configuration 条目进 sliceId、被
 *     baseline-projection 排除——求解配置改变不改样本基准/比较基准）
 *   - 任务契约 tasks/foundation/WP-15-T10.json acceptance 1~5
 *
 * 背景说明（为什么"求解配置"与"显示单位"是两类东西——本头把它们并置
 * 恰是为了结构性分开）：求解配置（初值/迭代/容差/去重阈值/预算/种子/
 * 线程数）改变**计算本身**，因此必须进入运行身份（configDigest→快照
 * configurationRefs）与缓存身份（config.ik 依赖条目→sliceId）——改配置
 * 就必须重算，绝不允许静默复用旧结果（V-20/V-21）；显示单位（m/cm/mm、
 * deg/rad）只改变**数值的呈现比例**，SI 真值不动，因此必须留在一切身份
 * 之外（§4.5）——切换单位零重算/零修订（V-17）。本头通过"canonical
 * 布局里根本没有显示单位字段"（kAnalysisConfigCanonicalSize 定长封闭）
 * 把这条边界做成结构保证，而不是靠调用方自觉。
 *
 * 持久化边界（P-KIN-4 处置锚点，契约 knownPitfalls）：AnalysisConfiguration
 * 的存储载体/键归 PM-14 用户级设置通道（ui 侧，ui 卡冻结后对接）——
 * 本单元只交付 schema＋canonical 编解码与装载出口（encode/decode），**不**
 * 触碰 .rwdesign 修订（分析配置非项目对象，§9.7）、不实现任何磁盘读写
 * （io 面 T10 无）。decode 对损坏字节的拒绝为 fail-fast 异常——用户级
 * 存储损坏属数据侧错误的最终防线，不允许静默吞掉（AGENTS 错误语义）；
 * ui 侧接线时的降级/提示策略按其卡面语义在其边界处置。
 *
 * 线程安全：本头全部实体为纯值/纯函数/无状态服务（无共享可变状态），
 * 并发只读安全；IAnalysisConfigurationCodec 实现无可变状态（可共享）。
 * 确定性：canonical 编码纯位组装（同配置同字节——NFR-COR-01/02）；摘要
 * 唯一经 core::ContentDigester（CR-02——摘要算法全仓唯一实现点）。
 */

#ifndef IRD_KINEMATICS_ANALYSISCONFIG_HPP
#define IRD_KINEMATICS_ANALYSISCONFIG_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>       // core::DiagnosticRecord（KIN-CONFIG-ILLEGAL 产码面）
#include <sdurws/ird/core/Digest.hpp>         // core::ContentIdentity/ContentDigester（configDigest）
#include <sdurws/ird/core/Units.hpp>          // core::UnitToken（显示单位投影——SA-12 唯一换算入口）
#include <sdurws/ird/evidence/Snapshot.hpp>   // evidence::ConfigEntry（configurationRefs 承载）
#include <sdurws/ird/kinematics/Ik.hpp>       // InitialValueStrategy（§4.4 词表同源——T04 已落位）
#include <sdurws/ird/kinematics/Sampling.hpp> // RegionSamplingBudget（§4.4 regionBudget 字段类型——T06 已落位）

namespace sdurws::ird::kinematics {

// =====================================================================
// 域常量（唯一书写点——编解码/摘要/快照接线/测试共用，禁第二处字面量）
// =====================================================================

/// AnalysisConfiguration schema 版本（§4.4 结构体注原文"schemaVersion = 1
/// （演进即新版本）"——字段增删/语义变化＝新版本号＋新 canonical 布局；
/// v1 编码不被更高版本解码接受（向前不兼容——身份面不允许歧义解码））。
inline constexpr std::uint32_t kAnalysisConfigurationSchemaVersion = 1U;

/// canonical 编码 magic（8 字节 ASCII——IRDCFG01；与 evidence IRDSNAP1/
/// IRDSLCE1、本单元 IRDSIG01 同款"magic＋版本"编码纪律。长度恒按 8 字节
/// 计入字节流——F-395 教训：magic 长度口径以实写字节为准，本常量恰 8 字
/// 符，无尾位歧义）。
inline constexpr char kAnalysisConfigMagic[] = "IRDCFG01";

/// 配置种类 token（§4.4 原文——快照 configurationRefs 与切片 config.ik
/// 依赖条目的 configKindToken 落值；点形 token 归 evidence 词表域
/// （isValidDependencyKey 允许点），非评估键（评估键无点——kebab 形））。
inline constexpr char kConfigIkKindToken[] = "config.ik";

/// canonical v1 布局总字节数（封闭定长——v1 全部字段为定宽标量、无集合
/// 字段；该常量同时是"schema 无隐藏字段"的负向断言锚（acceptance 4）：
/// 若未来有人在结构体加字段而不升版本，编码长度与本常量失配即刻暴露）。
inline constexpr std::size_t kAnalysisConfigCanonicalSize = 65U;

// =====================================================================
// AnalysisConfiguration——求解配置 schema（§4.4 逐字段；KIN-13）
// =====================================================================

/**
 * @brief 求解配置（§4.4 schema 原文逐字段；schemaVersion=1）。
 *
 * 业务语义（KIN-13）：求解域的全部可调参数集——进入运行身份（configDigest）
 * 与缓存身份（config.ik 切片条目），修改即按依赖失效（§9.5：四个评估通道
 * 全部声明 config.ik）。**负向 schema（acceptance 4 的结构承载）**：
 *   - 无碰撞开关（V13-01：碰撞启用只读引用快照已解析 EngineeringPolicySet
 *     ——分析配置不得覆盖策略；预览缺碰撞证据按评估模式/证据规则（EVI-01）
 *     表达，不是开关）；
 *   - 无判定阈值字段（近限位比/条件数警告等归 policy JointThresholds——
 *     §6.3 阈值分离；本结构只含"求解收敛/去重"类参数，不含任何"判定"
 *     类阈值）。
 * 两项排除不是遗漏，是设计边界——任何"顺手加上"的开关/阈值字段都构成
 * 配置覆盖策略的红线破坏（DTB 禁止项"配置不得覆盖策略"）。
 *
 * 合法域（构造/解码后必须满足——validateAnalysisConfiguration 为唯一
 * 校验点，encode/decode 前置调用）：
 *   - initialValuesCount ≥ 1、iterationLimit ≥ 1（无符号计数域中"负数"
 *     形态不可表达，0 即非法下界——卡面"计数负"的 schema 落值口径）；
 *   - 三容差有限且 >0（默认值＝附录 D 第 1/2/3 项 1e-6，"分析配置默认"
 *     类别——黄金数据集锁定归 T13，D-KIN-6）；
 *   - seed ≠ 0 且 regionBudget.seed ≠ 0（I-KIN-4：0 非法，拒绝、不做
 *     0→1 静默替换——NFR-COR-03。默认构造值恒为 0＝**故意非法**：强制
 *     调用方显式选择种子，"忘了设种子"不可能静默通过校验）；
 *   - regionBudget.threadCount ≥ 1（执行参数但入 configDigest——KIN-13
 *     明文"线程数等求解域参数"入身份面 V-21；不入 sampleSetIdentity，
 *     D-04 分层见 RegionSamplingBudget 注）。
 *
 * 默认值口径：容差三值＝附录 D 明文默认；初值策略/数量/迭代＝查询面
 * （TaskPointIkQuery）既有落值同源（JointGrid/1/1——黄金主径）；种子两值
 * 默认 0＝非法占位（见上）。值语义纯结构；线程安全（并发只读）。
 */
struct AnalysisConfiguration {
    /// 初值策略（§5.3 三值词表——ReferenceQ/SeededRandom/JointGrid；
    /// 枚举与 Ik.hpp 同一类型，无第二词表）。
    InitialValueStrategy initialStrategy = InitialValueStrategy::JointGrid;
    /// 多初值数量（≥1；SeededRandom/JointGrid 消费，ReferenceQ 忽略其值
    /// 但仍入身份——身份面不随消费路径裁剪，防"同配置不同解读"）。无量纲计数。
    std::uint32_t initialValuesCount = 1U;
    /// 单初值迭代上限（≥1）。无量纲计数。
    std::uint32_t iterationLimit = 1U;
    /// 位置残差收敛容差（单位 m；>0、有限；默认 1e-6——附录 D 第 1 项）。
    double positionResidualTolerance = 1e-6;
    /// 姿态残差收敛容差（单位 rad（不是度！）；>0、有限；默认 1e-6——
    /// 附录 D 第 2 项）。
    double orientationResidualTolerance = 1e-6;
    /// IK 解去重阈值（关节空间逐轴；单位 rad（转动关节）／m（移动关节，
    /// 随 MDL-12-S1）；>0、有限；默认逐轴 1e-6——附录 D 第 3 项/C1）。
    double ikDedupThresholdPerAxis = 1e-6;
    /// 区域采样预算/线程数（求解域参数——regionBudget.seed 入
    /// sampleSetIdentity 与 configDigest 双面；threadCount 仅入
    /// configDigest。类型与分层语义见 Sampling.hpp RegionSamplingBudget 注）。
    RegionSamplingBudget regionBudget{};
    /// 确定性随机种子（初值策略 SeededRandom 的序列源；§3.4"随机性唯一
    /// 来源"；0 非法——I-KIN-4 拒绝，不做 0→1 静默替换。无量纲）。
    std::uint64_t seed = 0;

    /// 逐字段精确等值（值语义面比较；身份面比较一律走 configDigest——
    /// 字节等值身份，禁数值容差）。
    bool operator==(const AnalysisConfiguration& o) const
    {
        return initialStrategy == o.initialStrategy
            && initialValuesCount == o.initialValuesCount
            && iterationLimit == o.iterationLimit
            && positionResidualTolerance == o.positionResidualTolerance
            && orientationResidualTolerance == o.orientationResidualTolerance
            && ikDedupThresholdPerAxis == o.ikDedupThresholdPerAxis
            && regionBudget == o.regionBudget && seed == o.seed;
    }
    bool operator!=(const AnalysisConfiguration& o) const { return !(*this == o); }
};

// =====================================================================
// 校验与产码（fail-fast 调用方错误轨＋KIN-CONFIG-ILLEGAL 诊断内容面）
// =====================================================================

/**
 * @brief 校验求解配置合法域（§4.4 合法域全表；非法即抛——fail-fast）。
 *
 * 校验序固定（确定性——同一坏配置必报同一首错，NFR-COR-02）：
 *   1. initialStrategy 枚举值域（解码面防御——位型超大值拒绝）；
 *   2. initialValuesCount ≥ 1；3. iterationLimit ≥ 1；
 *   4. positionResidualTolerance 有限且 >0（m）；
 *   5. orientationResidualTolerance 有限且 >0（rad）；
 *   6. ikDedupThresholdPerAxis 有限且 >0（rad|m）；
 *   7. regionBudget.seed ≠ 0；8. regionBudget.threadCount ≥ 1；
 *   9. seed ≠ 0。
 *
 * 错误语义（AGENTS 调用方错误轨）：非法配置＝调用方契约违约，抛
 * std::invalid_argument 并在文案给出首错字段/实际值/合法域——拒绝计算、
 * 不钳制不置零更不静默替换（NFR-COR-03；seed=0 绝不替成 1，I-KIN-4）。
 * 需要用户可见诊断的调用面（ui 编辑器/装配面板）另行调用
 * configurationIllegalDiagnostic 取 KIN-CONFIG-ILLEGAL 记录。
 *
 * @throws std::invalid_argument 任一合法域违约（文案含首错定位——确定性序）
 *
 * 纯函数；线程安全；确定性。
 */
void validateAnalysisConfiguration(const AnalysisConfiguration& config);

/**
 * @brief 组装"求解配置非法"的稳定诊断记录（§9.6 行 12
 *        KIN-CONFIG-ILLEGAL 的产码消费面——T10）。
 *
 * 与 Commands.cpp 门面自产诊断同款形态（码值经 DiagCodes.hpp 在册常量
 * kKinConfigIllegal——禁字符串拼码）；cause 直接承接
 * validateAnalysisConfiguration 的异常文案（首错定位），context/recommended
 * action 按 UX-03"对象/上下文/原因/建议动作"四要素给足。subject 缺席：
 * 配置无项目对象身份（用户级设置——非 .rwdesign 对象），不伪造 ObjectId。
 *
 * @param cause [in] 违约原因文案（惯例＝捕获 validateAnalysisConfiguration
 *              异常的 what()；空串会被 DiagnosticRecord::make 拒绝——C-3）
 * @return 可入诊断通道/报告的记录（码＝KIN-CONFIG-ILLEGAL，error 级）
 *
 * 纯函数；线程安全。
 */
core::DiagnosticRecord configurationIllegalDiagnostic(const std::string& cause);

// =====================================================================
// IAnalysisConfigurationCodec——canonical 编解码契约（§3.3 布局表 T10 行）
// =====================================================================

/**
 * @brief 求解配置 canonical 编解码契约（§3.3 原文名）。
 *
 * 为什么是接口（而非仅自由函数）：schemaVersion 演进的**多版本共存点**
 * ——v1（AnalysisConfigurationCodec）之后若配置增字段（如 KIN-12-S1 扩展
 * 单位以外的求解域参数），新版本实现同契约以 v2 布局编解码；持久化侧
 * （P-KIN-4，ui 侧）依赖本接口即可在版本间切换，不绑死 v1 类型。接口
 * 方法为纯函数语义（实现不得携带可变状态——可共享）。
 *
 * 编码面向两处消费（§4.4）：
 *   - 快照 configurationRefs：ConfigEntry.canonicalBytes（不透明承载——
 *     evidence 对字节摘要得 contentIdentity，语义见 makeConfigurationRefEntry）；
 *   - 用户级设置存储：字节串原样存取（PM-14 通道，载体归 ui 侧）。
 */
class IAnalysisConfigurationCodec {
public:
    virtual ~IAnalysisConfigurationCodec() = default;

    /**
     * @brief 编码为 canonical 字节（v1 布局——kAnalysisConfigCanonicalSize
     *        定长；非法配置先经 validateAnalysisConfiguration 拒绝）。
     * @throws std::invalid_argument 配置非法（fail-fast——先于任何字节产出）
     */
    virtual std::vector<std::uint8_t> encode(const AnalysisConfiguration& config) const = 0;

    /**
     * @brief 从 canonical 字节解码（结构校验＋合法域校验双段）。
     * @throws std::invalid_argument 字节非 v1 canonical 编码（长度/magic/
     *         版本/枚举值域/非有限浮点/非法值域任一违约——含就地定位文案）
     */
    virtual AnalysisConfiguration decode(const std::vector<std::uint8_t>& bytes) const = 0;
};

// =====================================================================
// AnalysisConfigurationCodec——canonical v1 实现（布局登记随卡 §14.6 v0.10）
// =====================================================================

/**
 * @brief canonical v1 编解码器（无状态——可共享/可重入）。
 *
 * 布局 v1（全部小端、字段定序如下、无填充；总长 65 字节＝
 * kAnalysisConfigCanonicalSize——封闭定长，负向 schema 的结构承载）：
 * @code
 *   偏移  长度  字段
 *    0     8   magic "IRDCFG01"（ASCII）
 *    8     4   u32 schemaVersion = 1
 *   12     1   u8  initialStrategy（枚举底层值——ReferenceQ=0/SeededRandom=1/JointGrid=2）
 *   13     4   u32 initialValuesCount
 *   17     4   u32 iterationLimit
 *   21     8   f64 positionResidualTolerance（m——IEEE754 位模式直写）
 *   29     8   f64 orientationResidualTolerance（rad——位模式直写）
 *   37     8   f64 ikDedupThresholdPerAxis（rad|m——位模式直写）
 *   45     8   u64 regionBudget.seed
 *   53     4   u32 regionBudget.threadCount
 *   57     8   u64 seed
 * @endcode
 * "集合字典序"项：v1 无集合字段（全定宽标量）——该纪律在本布局无落点，
 * 登记于此备查（未来版本引入集合字段时按字典序排布）。字节序为小端：
 * kinematics 单元 canonical 纪律（§4.4"定宽小端"）与 evidence 编码器的
 * 大端不同——各域 canonical 独立成套，config 字节只在本域与 evidence
 * 不透明承载间流动，无跨域字节互解场景（ConfigEntry 不透明性的目的）。
 */
class AnalysisConfigurationCodec final : public IAnalysisConfigurationCodec {
public:
    AnalysisConfigurationCodec() = default;

    std::vector<std::uint8_t> encode(const AnalysisConfiguration& config) const override;
    AnalysisConfiguration decode(const std::vector<std::uint8_t>& bytes) const override;
};

// =====================================================================
// configDigest 摘要出口与快照接线（§4.4——入 configurationRefs/sliceId）
// =====================================================================

/**
 * @brief 计算求解配置摘要（configDigest＝SHA-256 over canonical 字节——
 *        §4.4 原文；摘要算法唯一经 core::ContentDigester，CR-02）。
 *
 * 消费面（三处，同一值）：
 *   - 快照 configurationRefs 的 ConfigEntry.contentIdentity（快照身份面）；
 *   - config.ik 依赖条目的 ConfigurationDependencyPayload.contentIdentity
 *     （切片身份面——进 sliceId，CON-05/KIN-13）；
 *   - 结果身份 IkRequestIdentity.configDigest／RegionCoverageComputation.
 *     configDigest（结果溯源面）。
 * **不入 inputBaselineId**（D-04 双层身份：baseline-projection 排除
 * Configuration 条目——求解配置改变不改样本基准/比较基准，改的只是
 * sliceId＝缓存不命中→重算）。
 *
 * @return 配置摘要（32 字节；同配置恒同摘要——确定性 NFR-COR-01/02）
 *
 * @throws std::invalid_argument 配置非法（同 encode——先校验后编码后摘要）
 *
 * 纯函数；线程安全；确定性。
 */
core::ContentIdentity analysisConfigurationDigest(const AnalysisConfiguration& config);

/**
 * @brief 组装快照配置引用条目（§4.4"进入快照 configurationRefs"的出口——
 *        configKindToken 恒为 kConfigIkKindToken（"config.ik"））。
 *
 * contentIdentity＝analysisConfigurationDigest 同源值（对 canonicalBytes
 * 摘要）——evidence SnapshotBuilder 冻结期对 ConfigEntry 的一致性校验
 * （contentIdentity 必须等于对字节的 SHA-256）因此天然通过；快照组装方
 * （L5/请求方）把本条目经 SnapshotBuilder::addConfiguration 录入。
 *
 * @return 配置引用条目（canonicalBytes 为 v1 编码；非法配置先被拒绝）
 *
 * @throws std::invalid_argument 配置非法（同 encode）
 *
 * 纯函数；线程安全；确定性。
 */
evidence::ConfigEntry makeConfigurationRefEntry(const AnalysisConfiguration& config);

// =====================================================================
// 配置变更依赖提示（L-K7/AT-27——只产提示数据，不触发任何重算）
// =====================================================================

/// 声明 config.ik 依赖的评估键全集（§4.3 依赖声明表四行的实现键——kebab
/// 形常量的唯一引用点：kPoseMetricsEvaluationKey（Evaluators.hpp）/
/// kTaskPointIkEvaluationKey（Ik.hpp）/kTaskPointsBatchEvaluationKey
/// （Evidence.hpp）/kRegionCoverageEvaluationKey（Sampling.hpp）。顺序＝
/// §4.3 表行序（确定性）。
std::array<std::string_view, 4> analysisConfigConsumerKeys();

/**
 * @brief 配置变更的依赖提示值（L-K7："config.ik 编辑→保存（用户级）→
 *        按依赖提示受影响结果需重算（不自动重算）"；AT-27"高级参数折叠
 *        并提示新旧结果不可直接比较"）。
 *
 * **提示数据，不是动作**：本结构只回答"哪些结果受影响、为什么"，重算
 * 的触发权在用户/调用侧（不自动重算——L-K7 原文；本单元无任何调度/
 * 提交通道，结构上不可能自动重算）。
 *
 * 语义（§9.5 失效面速查表的行×config.ik 列）：
 *   - configChanged（configDigest 不同）⇒ 四通道切片全部失效（config.ik
 *     列四行全命中）——affectedEvaluationKeys 给出全四键；requiresRecompute
 *     ＝configChanged（新旧结果不可直接比较的提示依据——AT-27）；
 *   - sampleBaselineChanged（regionBudget.seed 不同）⇒ 样本集身份随变
 *     （D-04 分层的例外面：seed 是样本基准的构成输入——新一轮研究基准，
 *     比较基准检查拦截）；仅线程数/其余字段变化时 sliceId 变而样本基准
 *     不变（§9.5 注※——V-21 身份面与样本基准的分层）。
 *
 * 比较面＝configDigest（canonical 字节等值——身份面），非 operator==
 * 逐字段面：两者在本 schema 下等价（定长布局全字段入码），取身份面是
 * 语义声明——提示回答的是"身份变没变"。
 *
 * 值语义纯结构；线程安全（并发只读）。确定性：同输入同提示。
 */
struct AnalysisConfigChangeHint {
    /// 配置身份变化（configDigest 不同——config.ik 依赖条目随之变化）。
    bool configChanged = false;
    /// 样本基准变化（regionBudget.seed 不同——sampleSetIdentity 随变；
    /// 见结构体注的 D-04 分层语义）。
    bool sampleBaselineChanged = false;
    /// 受影响评估键（§9.5 全四通道——configChanged 时填充
    /// analysisConfigConsumerKeys()，未变化时为空数组）。
    std::array<std::string_view, 4> affectedEvaluationKeys{};
};

/**
 * @brief 分析配置变更（before→after）并产出依赖提示（纯数据——不触发
 *        重算，L-K7）。
 *
 * @param before [in] 变更前配置（当前已保存的用户级配置）
 * @param after  [in] 变更后配置（编辑待保存值）
 * @return 依赖提示（相同配置→全空提示；变化→按结构体注语义填充）
 *
 * @throws std::invalid_argument 任一侧配置非法（先校验后比较——非法值
 *         无身份可言，fail-fast）
 *
 * 纯函数；线程安全；确定性。
 */
AnalysisConfigChangeHint analyzeConfigurationChange(const AnalysisConfiguration& before,
                                                    const AnalysisConfiguration& after);

// =====================================================================
// DisplayUnitProjection——显示单位纯投影（KIN-12/§4.5 身份外元素）
// =====================================================================

/**
 * @brief 显示单位投影句柄（KIN-12：长度 m/cm/mm、角度 deg/rad 的 R1 冻结
 *        子集——R2 扩展 inch/grad/turn 不提前实现，KIN-12-S1 归 WP-15-T17）。
 *
 * 语义边界（三重，缺一即 KIN-12 破坏）：
 *   1. **纯投影**：projectLength/projectAngle 只做"SI 真值→显示比例"的
 *      读侧换算，不携带、不缓存、不改写任何计算状态；换算唯一经
 *      core::UnitToken/convert（SA-12 单位换算唯一入口——本类是委托句柄
 *      不是第二实现点，零换算算术）；
 *   2. **身份外**：显示单位不是 AnalysisConfiguration 字段、不入 canonical
 *      布局、不入 configDigest/sliceId/sampleSetIdentity（§4.5 身份外
 *      元素清单第一行）——切换显示单位零重算/零修订/结果不动（V-17），
 *      该性质由"canonical 定长布局无此字段"结构保证（负向 schema）；
 *   3. **词表封闭**：只接受 R1 冻结子集 token（经 core::UnitToken::find
 *      解析＋本类白名单复核）——inch/grad/turn 等 R2 token 一律拒绝
 *      （nullopt），不提前实现（KIN-12-S1 面向 WP-15-T17）。
 *
 * 词表的存储语义归 core Units＋ui 投影（卡 §2.3 不拥有表）——本类不持久
 * 化、不注册词表，只按已注册 token 投影；ui 侧选择什么显示单位是其会话
 * 偏好（不入任何身份）。
 *
 * 值语义（可拷贝——两枚 UnitToken 句柄）；线程安全（core 注册表编译期
 * 冻结只读，句柄并发只读安全）。
 */
class DisplayUnitProjection {
public:
    DisplayUnitProjection() = delete;  ///< 只能经 tryFind 构造（保证 token 已注册且属 R1 子集）

    /**
     * @brief 按冻结 token 原文构造投影句柄（R1 子集校验——白名单外拒绝）。
     *
     * @param lengthSymbol [in] 长度显示单位 token（仅 "m"/"cm"/"mm"——
     *                     区分大小写；"inch" 等 R2 token 返回 nullopt）
     * @param angleSymbol  [in] 角度显示单位 token（仅 "rad"/"deg"；"grad"
     *                     等 R2 token 返回 nullopt）
     * @return 投影句柄；任一 token 不在 R1 子集或未在 core 注册表＝nullopt
     *         （不抛——调用方以用户设置原词解析失败处置，ui 回退默认投影）
     *
     * 纯函数；线程安全；确定性。
     */
    static std::optional<DisplayUnitProjection>
    tryFind(std::string_view lengthSymbol, std::string_view angleSymbol);

    /**
     * @brief 长度显示投影（m 真值 → 显示单位数值；读侧换算——不改任何状态）。
     * @param siMeters [in] 长度 SI 真值（单位 m；须有限——非有限经
     *                 core::convert 拒绝抛 CoreError，NFR-COR-03）
     * @return 显示数值（单位＝lengthUnit()；cm/mm 换算各一次乘除——不承诺
     *         位往返精确，呈现层按相对容差取整归 ui 职责）
     *
     * @throws core::CoreError siMeters 非有限（core/units/convert: 口径）
     *
     * 纯函数；线程安全；确定性（同 token 同值）。
     */
    double projectLength(double siMeters) const;

    /**
     * @brief 角度显示投影（rad 真值 → 显示单位数值；读侧换算）。
     * @param siRadians [in] 角度 SI 真值（单位 rad（不是度！）；须有限）
     * @return 显示数值（单位＝angleUnit()；deg 投影＝×(180/π)）
     *
     * @throws core::CoreError siRadians 非有限（同上）
     *
     * 纯函数；线程安全；确定性。
     */
    double projectAngle(double siRadians) const;

    /// 长度显示单位 token（已注册句柄——symbol()/siFactor() 可用）。
    core::UnitToken lengthUnit() const noexcept { return m_length; }
    /// 角度显示单位 token（同上）。
    core::UnitToken angleUnit() const noexcept { return m_angle; }

    bool operator==(const DisplayUnitProjection& o) const noexcept
    {
        return m_length == o.m_length && m_angle == o.m_angle;
    }
    bool operator!=(const DisplayUnitProjection& o) const noexcept { return !(*this == o); }

private:
    DisplayUnitProjection(core::UnitToken length, core::UnitToken angle)
        : m_length(length), m_angle(angle)
    {
    }

    core::UnitToken m_length;  ///< 长度显示单位（tryFind 白名单内已注册句柄）
    core::UnitToken m_angle;   ///< 角度显示单位（同上）
};

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_ANALYSISCONFIG_HPP
