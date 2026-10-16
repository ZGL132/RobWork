/**
 * @file   Envelope.hpp
 * @brief  近似外包络估算（KIN-10，R2）——按配置方向数（默认 180，KIN-10
 *         原文）的斐波那契螺旋方向集（§7.2 黄金设施复用）逐方向做射线
 *         边界二分搜索，估算各方向的最大可达距离 Rmax（m，基座系 {B}）；
 *         可取消（协作探针——EX-T03 语义，管线桥接面归 WP-15-T14 采样
 *         执行管线）；结果类型化标注"近似、非精确"（AT-26）。
 *
 * 设计依据：
 *   - REQUIREMENTS KIN-10（P1/R2/D 原文："支持近似外包络估算：按配置
 *     方向数（默认 180）射线边界计算 Rmax，可取消，结果明确标注'近似、
 *     非精确'"）、AT-26（"外包络标注'近似非精确'"）
 *   - units/kinematics.md §7.3（与 KIN-09/10/11（R2）的边界——探索式
 *     估算，结果**不得**进入 Must 区域判定或必验覆盖；§7.2 冻结样本集
 *     是唯一正式口径；"复用 §7.2 方向集生成器与解集设施"）、§7.2（方向
 *     集黄金锁定——generateDirections 斐波那契螺旋；存在性口径"存在
 *     ≥1 有效解即达"）、§8.3（取消——协作式查询）、§8.4（确定性——
 *     同输入同输出；随机性唯一来源＝seed 派生序列）、§3.4（数值恒 SI；
 *     取消粒度）、§9.1（非异常出口 Expected<T, KinematicsError>；调用方
 *     错误 fail-fast）
 *   - 任务契约 tasks/foundation/WP-15-T15.json（acceptance 1~4；note：
 *     "射线边界计算纯域内实现，执行/取消经 T14 采样执行管线传递——
 *     EX-T03 语义，零新跨单元接线"；knownPitfalls P-KIN-7——本头为纯
 *     值面/纯函数面，零 ui/呈现耦合）
 *
 * 背景说明（"近似、非精确"的三个数学根源——为什么这是估算而不是精确
 * 可达边界，注释给 review 工程师）：
 *   1. **方向离散**：只沿 directionCount 条离散方向估算，方向之间的
 *      边界形状未知（KIN-10 原文"按配置方向数"即承认离散口径）；
 *   2. **射线内搜索也不完备**：二分把"数值搜索未找到解"（结局 2/3）
 *      当作射线上的"不可达侧"使用——而 C5/C8 明文"数值搜索未找到有效
 *      解不构成不可行证明"（初值/预算不足时真可达点也会被判 NotFound）；
 *      因此 Rmax 是"已证实可达的最远距离"（下估计），真实边界可能更远；
 *   3. **单调性假设未验证**：二分内锚点搜索＋对分只保证收敛到"锚点
 *      所在可达区间"的外端点；工作空间沿射线非单区间（多岛/空腔）时，
 *      更外侧的可达岛不被本算法发现。
 *   三者合成的结论：Rmax 恒为"近似、非精确"——本头以单值精度词表
 *   （EnvelopePrecision，只有 Approximate 一个成员）把该标注做成类型化
 *   结构事实（不存在"精确包络"形态可被误选），并用 kEnvelopeApproximation-
 *   Note 固定文案承载 §7.3 的两条禁令（不作 Must 区域判定依据、不能替代
 *   样本全覆盖）。
 *
 * 与解析界限（Bounds.hpp，T04）的分工：解析工作半径上界 totalRadius 是
 * 三角形不等式给出的**确定性上界**（覆盖全部关节角）。★ KIN-P5/审查
 * A04 措辞修正：上界点**并非**"数学上已证不可达"——三角形不等式取等
 * 时（共线串联构型）上界点恰是可达的（审查反例：六轴同轴链 TCP 距轴
 * 1 m，上界点 (R,0,0) 由零位构型精确到达）。因此本算法把 Rup 作为
 * 射线扫描的**首个探测站**（可达则 rMax=Rup 直接成立），而不是像旧
 * 实现那样把它当作免探测的"不可达侧"端点（旧实现注释称上界点"已证
 * 不可达"系错误表述——已随本迁移更正）；解析界限的"证明素材/不可行
 * 裁定"语义不进本头（KIN-10 是估算工具，不产证明素材）。
 *
 * 执行与取消的边界（契约 note 字面）：本头是**纯域内计算**（单线程
 * 顺序执行，无执行器派发）；取消经 EnvelopeQuery::cancellationProbe
 * 协作探针——管线侧（WP-15-T14 采样执行管线）把 execution 取消令牌
 * 桥接为该探针（EX-T03 语义），本单元零新跨单元接线。探针为空＝不可
 * 取消（合法——短方向数的预览场景）。
 *
 * 线程安全：全部实体为纯值/纯函数（无共享可变状态）——并发只读安全；
 * computeApproximateEnvelope 单线程顺序执行（§3.4 纯函数服务）。
 * 确定性：同 (view, tcp, query) → 逐字节同结果（探针种子＝query.seed
 * 派生、方向集黄金锁定、二分迭代定长定序——NFR-COR-01/02；禁止读
 * 时钟/环境熵）。
 */

#ifndef IRD_KINEMATICS_ENVELOPE_HPP
#define IRD_KINEMATICS_ENVELOPE_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <rw/math/Vector3D.hpp>

#include <sdurws/ird/kinematics/Errors.hpp>     // KinematicsError（Expected 错误侧）
#include <sdurws/ird/kinematics/Fk.hpp>         // Expected（非抛出查询轨别名）
#include <sdurws/ird/kinematics/Ik.hpp>         // IIkSolver/InitialValueStrategy/
                                                //   IkFacilityFailure（非终局形态复用）
#include <sdurws/ird/kinematics/KinTypes.hpp>   // IKinRuntimeView/TcpRef/IkRequestIdentity

namespace sdurws::ird::kinematics {

// =====================================================================
// 域常量（唯一书写点——计算实现/测试共用，禁第二处字面量）
// =====================================================================

/// 默认方向数（KIN-10 原文"按配置方向数（默认 180）"的 180 落值；
/// 无量纲计数。黄金锁定随 T13 同族口径——修改走设计变更）。
inline constexpr std::uint32_t kEnvelopeDirectionCountDefault = 180U;

/// 本计算通道的算法契约版本（§8.4"求解器版本入身份"同族——Rmax 边界
/// 算法（径向站点扫描＋二分）的版本锚；升级即换算法语义，消费方据此
/// 判结果可比性。一经交付不回退）。
/// ★ v2（KIN-P2/A02 验收驳回补迁移）：位置存在性探针由"姿态容差 π 落
/// 值"迁移为 IkRequest.taskSpace=PositionOnly（3 维任务子空间）——探针
/// 判定语义翻转（同方向同 t 的探针可由搜索未果翻为可达），版本不升会
/// 让跨迁移的 Rmax 结果假借同一身份比较（与 kIkSolverContractVersion
/// 同批 1→2 的同构理由）。
/// ★ v3（KIN-P5/审查 A04＋A06 双驱动）：①探测算法由"内锚点几何序列
/// （Rup·2⁻ᵏ）＋hi=Rup 免探测"重构为"上界点先行＋等差径向站点由外向
/// 内扫描＋外端区间二分"——同方向同模型可从"rMax=0 近似空"翻为
/// "rMax>0"（上界点/外环带可由新站点命中——A04 反例），且新增
/// NoSampleFound 射线态使"未找到可达样本"不再以 Rmax=0 的确定结论
/// 呈现；②探针初值种子派生随 64 位完整混合换算法（kIkSolverContract-
/// Version 2→3 同批——A06）。两处均为结果语义变化，版本不升级会让
/// 新旧两代 Rmax 假借同一身份比较。
inline constexpr std::uint32_t kEnvelopeContractVersion = 3U;

// ★ KIN-P2/A02 验收驳回补迁移：旧"位置存在性探针姿态无约束落值 π"
// （kEnvelopePoseFreeTolerance）移除——探针现以 IkRequest.taskSpace=
// PositionOnly 承载姿态自由（姿态项不进目标函数；π 容差方案下姿态仍
// 参与 6 维迭代，位置可达探针点被姿态项牵制出局——审查 A02 同源）。
// Evaluators.cpp/Workspace.cpp/Sampling.cpp 的同机制常量已于 KIN-P2 主
// 提交（2c163349）同批移除。

/**
 * @brief "近似、非精确"固定标注文案（KIN-10 原文词＋§7.3 两条禁令——
 *         结果呈现/导出面应原样携带，不得改写省略；AT-26 验收锚）。
 *
 * 确定性：编译期固定串（NFR-COR-02——同常量同字节）。
 */
inline constexpr char kEnvelopeApproximationNote[] =
    "近似外包络（非精确）：方向离散＋数值搜索的射线边界 Rmax，不构成"
    "精确可达边界；不得替代样本全覆盖（KIN-04 冻结样本集），不得作为"
    "Must 区域判定依据（kinematics 卡 §7.3/AT-26）。";

// =====================================================================
// EnvelopePrecision——精度词表（单值；类型化承载"近似、非精确"）
// =====================================================================

/**
 * @brief 外包络结果的精度词表（**故意单值**——KIN-10/AT-26 的结构化
 *         标注载体）。
 *
 * 为什么只有一个枚举成员：近似外包络只有一种精度形态；不存在"精确
 * 包络"值——把"这是估算"做成类型的结构事实，调用方无法把结果误当
 * 精确口径消费（§7.3 边界的编译期承载）。若未来出现精确算法，属新
 * 需求语义（走需求变更），不在本词表内表尾追加"精确"值混淆当前语义。
 */
enum class EnvelopePrecision : std::uint8_t {
    /// 恒值——"近似、非精确"（KIN-10 原文词；AT-26 标注锚）。
    Approximate,
};

// =====================================================================
// EnvelopeRayState/EnvelopeRayRecord——逐方向射线记录
// =====================================================================

/**
 * @brief 单方向射线边界状态三值词表（§7.2 样本状态词表的估算通道
 *         收缩版——估算通道无"数据不足降级"语义：单探针搜索未果并入
 *         边界判定〔近似根源 2，见文件头注〕，只有"算了/没算/没找到"
 *         三态）。
 */
enum class EnvelopeRayState : std::uint8_t {
    /// 边界已计算（rMax 有效——该方向找到了 ≥1 个可达探测站，rMax＝
    /// 已证实可达的最远距离下估计）。
    Computed,
    /// 未运行（取消/设施失败后未开始——partial 如实标记；reason
    /// 必填非空，ERR-01 显式标记不伪造）。
    NotRun,
    /// 未找到可达样本（KIN-P5/审查 A04 新增——探测预算〔径向站点全数
    /// 扫描〕内没有找到任何可达样本的**低置信观测**：既可能是该方向
    /// 真不可达，也可能是可达样本落在站点分辨率之外或数值搜索未果
    /// 〔近似根源 2〕。与旧实现的本质区别：旧行为把此形态并入 Computed
    /// 并给出 rMax=0——"找到半径为零"的确定结论掩盖了"没有找到"的
    /// 事实（审查 A04：半径 1 m 圆形工作空间 32 探针后输出 Rmax=0）。
    /// 本态下 rMax **无语义**（保持 0 占位——呈现侧/消费方必须按状态
    /// 词表判读，不得把该态当 rMax=0 的可达性结论消费）；reason 必填
    /// 非空（低置信语义随文案显式携带——ERR-01 同款纪律）。
    NoSampleFound,
};

/**
 * @brief 单方向的射线边界记录（结果列元素——方向集生成序排列）。
 *
 * 单位与坐标系（AGENTS §2.5）：direction 为单位方向向量（无量纲，
 * ‖d‖=1，基座系 {B}——区域投影系即基座系，与 §7.2 generateDirections
 * 同坐标系约定）；rMax 单位 m，基座系 {B} 下沿 direction 自基座原点
 * 起算的距离。
 *
 * 值语义纯结构；线程安全（并发只读）。
 */
struct EnvelopeRayRecord {
    /// 方向序号（0 起，方向集生成序 j——与 generateDirections(count)
    /// 的输出序一一对应；全序键）。
    std::uint32_t directionIndex = 0;
    /// 单位方向（‖d‖=1，基座系 {B}；generateDirections 产出原值）。
    rw::math::Vector3D<double> direction = rw::math::Vector3D<double>(0.0, 0.0, 0.0);
    /// 该方向最大可达距离估算值（m；基座系 {B} 自原点沿 direction——
    /// "已证实可达的最远距离"下估计，近似语义见文件头注；
    /// state=NotRun/NoSampleFound 时无语义——NoSampleFound 态保持 0
    /// 占位，**不构成 rMax=0 的可达性结论**，见枚举注）。
    double rMax = 0.0;
    /// 本方向消耗的 IK 探测调用数（计数，无量纲——搜索预算素材；
    /// state=NotRun 时为已消耗部分，如实保留）。
    std::uint32_t probeCount = 0;
    /// 射线状态（三值——枚举注）。
    EnvelopeRayState state = EnvelopeRayState::NotRun;
    /// 原因文本（state=NotRun/NoSampleFound 必填非空——ERR-01；
    /// Computed 态为空串。内部诊断链文本，不得未经 diagnostics 脱敏
    /// 直接呈现）。
    std::string reason;

    bool operator==(const EnvelopeRayRecord& o) const
    {
        return directionIndex == o.directionIndex && direction == o.direction
            && rMax == o.rMax && probeCount == o.probeCount && state == o.state
            && reason == o.reason;
    }
    bool operator!=(const EnvelopeRayRecord& o) const { return !(*this == o); }
};

// =====================================================================
// EnvelopeQuery——计算查询（求解配置＋搜索参数＋注入/协作面）
// =====================================================================

/**
 * @brief 近似外包络计算查询（值语义，调用方组装；computeApproximate-
 *         Envelope 的唯一入参束）。
 *
 * 所有权与生命周期：solver 为**非 owning** 裸指针——调用方持有并保证
 * 计算期间存活（空＝内置 IkSolver 生产路径；测试可注入可控替身——
 * §10.1"可控求解器替身"口径）；cancellationProbe 在工作线程内被调用
 * ——须线程安全或干脆不可变（IkRequest.cancellationProbe 同款约束）。
 *
 * 求解配置为 config.ik（AnalysisConfiguration）的 T15 直传投影（T04/
 * T05/T06 查询同构形态）——包络通道实际消费的字段子集：无姿态容差
 * 字段（位置存在性探针以 taskSpace=PositionOnly 承载姿态自由——
 * KIN-P2/A02，该任务不消费姿态容差，IkRequest 默认合法值作请求合法域
 * 载体；"不预建无消费者条目"纪律）、
 * 无碰撞会话（KIN-10 原文无碰撞要求——Rmax 是纯运动学可达边界，碰撞
 * 感知包络属 KIN-09 点云/后续扩展，本通道不注入碰撞会话）。
 *
 * 确定性来源（§3.4/§8.4）：seed 是全部随机初值的唯一来源（探针级
 * 派生——同 (seed, 探测序号) 同初值集）；方向集与 seed 无关（黄金
 * 锁定）；二分迭代数由 (anchorProbesMax, bisectIterationsMax,
 * positionTolerance) 确定。
 *
 * 值语义纯结构；线程安全（并发只读）。单位：referenceQ 逐自由度
 * rad|m；positionTolerance m；dedupThresholdPerAxis rad|m 逐轴；
 * 其余计数无量纲。
 */
struct EnvelopeQuery {
    // ---- 求解配置（config.ik 直传投影——T04/T05/T06 查询同构）----
    /// 排序参考构型（rad|m——D-KIN-4 显式输入；维度须＝设备自由度，
    /// 装配校验面拒绝违例）。
    std::vector<double> referenceQ;
    /// 初值策略（§5.3 三值——逐探针 IK 的初值来源；缺省 SeededRandom：
    /// 边界附近雅可比趋奇异，多种子随机初值比单网格点更稳）。
    InitialValueStrategy initialStrategy = InitialValueStrategy::SeededRandom;
    /// 逐探针初值数量（≥1——存在性口径的搜索宽度）。
    std::uint32_t initialValuesCount = 4U;
    /// 单初值迭代上限（≥1）。
    std::uint32_t iterationLimit = 64U;
    /// 位置残差容差（m；>0 有限——附录 D 第 1 项；兼作二分早停的
    /// 物理分辨率：区间宽 ≤ 容差即停）。
    double positionTolerance = 1e-6;
    /// 去重阈值（rad|m 逐轴；>0 有限——附录 D 第 3 项；透传给逐探针
    /// IK 请求——存在性判据不消费去重，但请求字段完整性要求合法值）。
    double dedupThresholdPerAxis = 1e-6;

    // ---- 包络搜索面（KIN-10 参数）----
    /// 方向数（≥1；缺省 180——kEnvelopeDirectionCountDefault，KIN-10
    /// 原文默认）。
    std::uint32_t directionCount = kEnvelopeDirectionCountDefault;
    /// 确定性种子（0 非法——I-KIN-4 拒绝，不做 0→1 静默替换，
    /// NFR-COR-03；探针初值序列的唯一来源，装配校验面拒绝 0）。
    std::uint64_t seed = 0;
    /// 径向探测站数上限（≥1；KIN-P5/审查 A04 起语义＝每方向等差站点
    /// 扫描的站点数 M——站点 t_j＝Rup·j/M，j=M..1（含上界点 t_M=Rup
    /// ——先行试探；站点分辨率＝Rup/M），由外向内扫描至首个可达站即
    /// 止（其外的站点已探明不可达、其内的站点不影响外边界）。旧语义
    /// （几何序列 t_k＝Rup·2⁻ᵏ 的"内锚点"）已废——它在原点邻域过密、
    /// 在 (Rup/2, Rup] 半径段只有一个探测点，窄环带/上界可达形态系统性
    /// 漏检（审查 A04 反例）。站点全灭→该方向 NoSampleFound（低置信，
    /// 不再是 rMax=0 的 Computed 确定结论）。
    std::uint32_t anchorProbesMax = 32U;
    /// 二分迭代数上限（≥1；区间宽 ≤ positionTolerance 即早停——本上限
    /// 是精度兜底：Rup·2⁻⁴⁰ 量级的相对分辨率）。
    std::uint32_t bisectIterationsMax = 40U;

    // ---- 注入与协作面 ----
    /// IK 求解器句柄（非 owning；空＝内置 IkSolver——生产路径；测试
    /// 注入可控替身）。
    const IIkSolver* solver = nullptr;
    /// 结果身份（§5.6 绑定载体——snapshotId/sliceId/configDigest/mode
    /// 由调用方填充并原样回填结果；seed/referenceQ 两字段由实现从本
    /// 查询覆写回填〔防两处漂移〕，调用方无需预填）。
    IkRequestIdentity identity;
    /// 取消探针（可空＝不可取消；返回 true 即协作取消——§8.3/D-KIN-9；
    /// WP-15-T14 采样执行管线把 execution 取消令牌桥接为该探针
    /// 〔EX-T03 语义〕——本单元零跨单元接线，契约 note 字面）。
    std::function<bool()> cancellationProbe;

    bool operator==(const EnvelopeQuery& o) const
    {
        return referenceQ == o.referenceQ && initialStrategy == o.initialStrategy
            && initialValuesCount == o.initialValuesCount
            && iterationLimit == o.iterationLimit
            && positionTolerance == o.positionTolerance
            && dedupThresholdPerAxis == o.dedupThresholdPerAxis
            && directionCount == o.directionCount && seed == o.seed
            && anchorProbesMax == o.anchorProbesMax
            && bisectIterationsMax == o.bisectIterationsMax
            && solver == o.solver && identity == o.identity;
    }
    bool operator!=(const EnvelopeQuery& o) const { return !(*this == o); }
};

// =====================================================================
// EnvelopeResult——计算结果（取消/设施失败为非终局形态；标注恒近似）
// =====================================================================

/**
 * @brief 近似外包络计算结果（值语义；冻结后只读）。
 *
 * 解读规则（**按序判读**——IkOutcome 同款纪律）：
 *   - cancelled=true：取消不是结局（D-KIN-9）——rays 中已完成方向保留
 *     Computed 记录、未开始方向 NotRun（reason 非空）；rMax 全体不作
 *     任何结论使用（零正式证据）；
 *   - facilityFailure 非空：求解设施失败（注入视图面在计算期间失效
 *     ——F-746③ 同族非终局形态）——同取消轨读法：失败方向及后续
 *     NotRun，不作任何工程结论；
 *   - 其余（正常返回）：rays 全部终态（Computed＝该方向找到可达样本、
 *     rMax 为下估计；NoSampleFound＝未找到可达样本的低置信观测——
 *     rMax 无语义不作结论消费）；但**恒为近似**（precision 恒
 *     Approximate；kEnvelopeApproximationNote 两条禁令随行）。
 *
 * 值语义纯结构；线程安全（并发只读）。确定性：同输入逐字节同结果
 * （NFR-COR-01/02——浮点成员同位型）。
 */
struct EnvelopeResult {
    /// 取消标记（true＝协作取消返回——非终局，见结构体注判读规则）。
    bool cancelled = false;
    /// 求解设施失败形态（非终局——nullopt＝无；复用 Ik.hpp 的
    /// IkFacilityFailure 值〔stage/detail〕，不另设同构类型）。
    std::optional<IkFacilityFailure> facilityFailure;
    /// 精度标注（恒 Approximate——单值词表，见 EnvelopePrecision 注；
    /// "结果明确标注近似、非精确"的类型化落点，KIN-10/AT-26）。
    EnvelopePrecision precision = EnvelopePrecision::Approximate;
    /// 本通道算法契约版本（kEnvelopeContractVersion——结果可比性锚）。
    std::uint32_t contractVersion = kEnvelopeContractVersion;
    /// 实际方向数（＝query.directionCount；rays 尺寸同值）。
    std::uint32_t directionCount = 0;
    /// 解析工作半径上界（m——computeAnalyticReachBound().totalRadius，
    /// 射线搜索外括号；确定性上界，非本估算产物）。
    double reachUpperBound = 0.0;
    /// 全部方向的 IK 探测调用总数（计数，无量纲——搜索预算素材；
    /// 与逐方向 probeCount 之和一致）。
    std::uint64_t probeCount = 0;
    /// 逐方向射线记录（方向集生成序 j 升序＝directionIndex 0..N-1
    /// 连续——与 generateDirections 输出序一一对应）。
    std::vector<EnvelopeRayRecord> rays;
    /// 结果身份（§5.6 绑定载体——query.identity 原样回填，其中
    /// seed/referenceQ 两字段由实现从 query 覆写）。
    IkRequestIdentity identity;

    /// 等值面：identity 逐成员（IkRequestIdentity 的 == ——T15 表尾
    /// 追加面）＋其余标量/枚举/记录列全字段精确比较（浮点逐位——
    /// 确定性断言面禁用容差）。facilityFailure 手工展开比较（其类型
    /// 无 == 面——Ik.hpp 值族零触碰纪律）。
    bool operator==(const EnvelopeResult& o) const
    {
        const bool facilitySame =
            facilityFailure.has_value() == o.facilityFailure.has_value()
            && (!facilityFailure.has_value()
                || (facilityFailure->stage == o.facilityFailure->stage
                    && facilityFailure->detail == o.facilityFailure->detail));
        return cancelled == o.cancelled && facilitySame
            && precision == o.precision && contractVersion == o.contractVersion
            && directionCount == o.directionCount
            && reachUpperBound == o.reachUpperBound && probeCount == o.probeCount
            && rays == o.rays && identity == o.identity;
    }
    bool operator!=(const EnvelopeResult& o) const { return !(*this == o); }
};

// =====================================================================
// computeApproximateEnvelope——近似外包络计算（KIN-10 唯一实现入口）
// =====================================================================

/**
 * @brief 按配置方向数计算近似外包络（KIN-10；每方向射线边界 Rmax 的
 *         二分估算——纯域内计算，单线程顺序执行）。
 *
 * 算法（每方向 d_j，逐步；KIN-P5/审查 A04 重构版——实现细节见
 * src/Envelope.cpp 逐步注释）：
 *   1. 解析外括号：Rup＝computeAnalyticReachBound(view, tcp).totalRadius
 *      （三角形不等式确定性上界——同时是首个探测站，**不免探测**：
 *      上界点可可达，见文件头"与解析界限的分工"注）；
 *   2. 等差径向站点扫描（多锚点）：站点 t_j＝Rup·j/M（j=M..1，M＝
 *      anchorProbesMax；分辨率 Rup/M——窄环带检测能力由预算显式承载）
 *      逐点做位置存在性 IK（taskSpace=PositionOnly——KIN-P2/A02 任务
 *      维度语义），**由外向内**扫描至首个可达站即止——该站即外边界
 *      所在区间的内端 lo，其外一站点（已探明 NotFound）即外端 hi；
 *      上界站可达⇒rMax=Rup 直接成立（零二分）；
 *   3. 外端区间二分收敛：在 (lo, hi] 上对分——mid 可达则 lo=mid、否则
 *      hi=mid（区间宽 ≤ positionTolerance 或迭代数耗尽即停），rMax＝lo
 *      （已证实可达的最远距离——下估计，近似根源见文件头注）；
 *   4. 站点全灭（无任何可达站）→ 该方向 NoSampleFound 态（**低置信
 *      观测**——"未找到"如实标记，不产出 rMax=0 的确定结论；审查
 *      A04 的诚实性修复落点）。
 *
 * 取消响应粒度（§8.3 黄金口径的本通道落值）：每探针调用前查询一次
 * （探针级）＋逐探针 IK 内部周期查询（kIkCancellationProbeInterval
 * 迭代一次——求解器既有粒度透传）；任一观测为真即停止全部后续探测
 * ——已完成方向如实保留、未开始方向 NotRun。
 *
 * @param view [in] 只读模型视图（调用方持有，计算期间存活；与 tcp
 *                   同快照——§5.1 输入义务）
 * @param tcp  [in] TCP 引用（快照内解析；解析失败走 Expected 错误侧）
 * @param query [in] 计算查询（值语义读入——不修改、不存储引用）
 *
 * @return 成功＝外包络结果（结构体注判读规则）；失败＝KinematicsError：
 *           - NoDevice：视图模型无可用设备链（§9.6 KIN-NO-DEVICE——
 *             解析界限推导的前置缺失）；
 *           - NoTcp：快照无工具或 tcpRef 悬空（§9.6 KIN-NO-TCP 两分）；
 *           - FrameUnresolved：tcpKey 不命中 canonical TCP（KinTypes
 *             解析规则）。
 *
 * @throws std::invalid_argument 调用方契约违约（fail-fast 轨——§9.1/
 *         NFR-COR-03，不进入结果对象）：directionCount/anchorProbesMax/
 *         bisectIterationsMax/initialValuesCount/iterationLimit＝0、
 *         seed＝0（I-KIN-4）、positionTolerance/dedupThresholdPerAxis
 *         非有限或 ≤0、referenceQ 维度≠设备自由度或含非有限分量。
 *
 * 纯函数；线程安全；确定性（同输入逐字节同结果——NFR-COR-01/02）。
 * 长运算：默认 180 方向×（锚点＋二分）×多初值 IK——管线侧应在工作
 * 线程调用并桥接取消探针。
 */
Expected<EnvelopeResult> computeApproximateEnvelope(const IKinRuntimeView& view,
                                                    const TcpRef& tcp,
                                                    const EnvelopeQuery& query);

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_ENVELOPE_HPP
