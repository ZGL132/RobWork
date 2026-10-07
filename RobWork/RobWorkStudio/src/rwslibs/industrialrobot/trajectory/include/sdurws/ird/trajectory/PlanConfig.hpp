/**
 * @file   PlanConfig.hpp
 * @brief  轨迹求解配置（§5.5 TrajectoryPlanConfiguration，schemaVersion=1）
 *         ——schema 值类型、合法域校验、canonical 编解码（magic IRDCFGTR1，
 *         进 config.trj 切片条目）、planConfigDigest 摘要出口与快照配置
 *         引用条目组装（WP-16-T04 批）。
 *
 * 设计依据：
 *   - units/trajectory.md §5.5（TrajectoryPlanConfiguration 设计基线——
 *     九字段表、"canonical 编码（实现 magic IRDCFGTR1，定长封闭布局、
 *     小端、f64 位模式）→configDigest＝SHA-256→切片 config.trj
 *     Configuration 条目→进 sliceId、不进 inputBaselineId（D-04 同款）"、
 *     "复检数值不在本配置内——TRJ-04 步长/预算默认属 P-06"）、§5.3
 *     （startStateRef 三选一——不从会话姿态读取，KIN-06/AT-04）
 *   - 需求 KIN-13 同源纪律（求解配置进入运行身份与缓存身份——TRJ 侧
 *     承接）、NFR-COR-02（确定性种子；0 非法——I-KIN-4 同款拒绝）、
 *     NFR-COR-03（非法输入拒绝不钳制）、AT-27（求解配置分层——配置进
 *     sliceId 断言）、AT-05（TCP 变更失效/求解配置改变不改变样本基准）
 *   - 先例：kinematics/AnalysisConfig.hpp（WP-15-T10 同款形态——schema＋
 *     validate＋codec＋digest＋ConfigEntry 组装五件套；"定宽小端＋字段
 *     定序＋集合字典序"canonical 纪律）
 *   - 任务契约 tasks/foundation/WP-16-T04.json（acceptance 2——身份绑定
 *     与 §7 一致；§5.5 身份路径的落地面）
 *
 * 背景说明（为什么轨迹求解配置必须进身份）：cartesianSampleStep、
 * ikContinuityThreshold、smoothTolerance、limitsScaleFactor、planningSeed
 * 等字段的任何改变都会改变**计算本身**（采样计划、构型选择序、缩放后
 * 的限值面）——同输入不同配置的结果不可比也不可缓存复用。本头通过
 * "canonical 字节全字段入码→SHA-256→configDigest→切片 config.trj 条目"
 * 把这条纪律做成结构保证：配置改变⇒configDigest 改变⇒sliceId 改变⇒
 * 缓存不命中⇒重算（V-28 ConfigurationChanged 逐条目失效的数据基础）。
 *
 * 与 kinematics 编码形态的一致性与差异：同为小端＋f64 位模式直写＋
 * "集合字典序"（本 schema 的 plannerSelection.params 是集合字段——按
 * 键字典序排布条目，布局为**变长**；kinematics v1 无集合故定长）。
 * 各域 canonical 独立成套，config.trj 字节只在本域与 evidence 不透明
 * 承载间流动，无跨域字节互解场景（ConfigEntry 不透明性的目的）。
 *
 * 线程安全：本头全部实体为纯值/纯函数/无状态服务（无共享可变状态），
 * 并发只读安全；Codec 实现无可变状态（可共享）。确定性：canonical 编码
 * 纯位组装＋集合字典序（同配置同字节——NFR-COR-01/02）；摘要唯一经
 * core::ContentDigester（CR-02——摘要算法全仓唯一实现点）。
 */

#ifndef IRD_TRAJECTORY_PLANCONFIG_HPP
#define IRD_TRAJECTORY_PLANCONFIG_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>        // core::ContentIdentity/ContentDigester（configDigest）
#include <sdurws/ird/core/Identity.hpp>      // core::ObjectId（startTaskPoint）
#include <sdurws/ird/evidence/Snapshot.hpp>  // evidence::ConfigEntry（配置引用条目承载）

namespace sdurws::ird::trajectory {

// =====================================================================
// 域常量（唯一书写点——编解码/摘要/快照接线/测试共用，禁第二处字面量）
// =====================================================================

/// TrajectoryPlanConfiguration schema 版本（§5.5 表头"字段
/// （schemaVersion=1）"——字段增删/语义变化＝新版本号＋新 canonical
/// 布局；v1 编码不被更高版本解码接受，向前不兼容——身份面不允许歧义
/// 解码）。
inline constexpr std::uint32_t kTrajectoryPlanConfigSchemaVersion = 1U;

/// canonical 编码 magic（ASCII "IRDCFGTR1"——§5.5 原文指定，**9 字符**；
/// 与 kinematics IRDCFG01 的 8 字符同款"magic＋版本"编码纪律但长度不同
/// ——F-395 教训：magic 长度口径以实写字节为准，本常量含尾零共 10 字节
/// 数组、编码写入恒取前 9 字节，无尾位歧义）。
inline constexpr char kTrajectoryPlanConfigMagic[] = "IRDCFGTR1";

/// canonical magic 的实写字节数（"IRDCFGTR1" 字符数——encode/decode 共用
/// 同一长度常量，禁第二处字面量）。
inline constexpr std::size_t kTrajectoryPlanConfigMagicSize = 9U;

/// 配置种类 token（§5.5"切片 config.trj Configuration 条目"与 §14.2.1
/// descriptor 依赖行 "config.trj(Configuration,Required)" 的落值——点形
/// token 归 evidence 依赖键词表域，非评估键）。
inline constexpr char kConfigTrjKindToken[] = "config.trj";

/// 运动律词表（§5.5 timeParamMethod"封闭词表，当前唯一值＝唯一实现"——
/// quintic-spline-c2；§12 运动律选择。新增值＝本卡增量修订＋实现＋黄金
/// 算例四者同批落地，§6.6 词表纪律）。
inline constexpr char kTimeParamMethodQuinticSplineC2[] = "quintic-spline-c2";

// =====================================================================
// 枚举词表（§5.5 三处 enum 字段的域内封闭词表）
// =====================================================================

/**
 * @brief 起始状态选择（§5.5 startStateRef"enum{Home, Zero, TaskPoint}＋
 *        optional<ObjectId> 三选一"）。
 *
 * 语义（§5.3）：起始状态不从会话姿态读取（KIN-06/AT-04——会话姿态不进
 * 计算身份）；Home/Zero 为权威模型字段（CanonicalModel 经编译链），由
 * 宿主解析为起始构型后注入展开器（Sequence.hpp——本单元不消费 runtime
 * 模型视图，投影注入纪律同 O-37）。TaskPoint 形态必须给出任务点对象
 * 身份（startTaskPoint）且不得指向未启用任务点（§5.5 行原文——启用性
 * 校验归展开器的拓扑面）。
 */
enum class StartStateKind : std::uint8_t {
    /// 权威 Home 构型（模型字段）。
    Home,
    /// 权威 Zero 构型（模型字段——全零关节角）。
    Zero,
    /// 指定任务点构型（startTaskPoint 必填）。
    TaskPoint,
};

/**
 * @brief 驻留事件消费开关（§5.5 dwellPolicy——预览快速评估可忽略驻留；
 *        Verified 必须 HonorEvents）。
 *
 * 组合校验（§12.3）：dwellPolicy=IgnoreDwell 仅限 Preview/Quick（筛选
 * 语义）；模式×配置组合校验在评估器入口执行（组合非法→TRJ-INPUT-INVALID
 * 素材——评估器归 WP-16-T10，本头只承载词表）。
 */
enum class DwellPolicy : std::uint8_t {
    /// 消费工况 Dwell 事件（驻留进时间轴——§12.3；Verified 必选）。
    HonorEvents,
    /// 忽略驻留（仅 Preview/Quick 合法）。
    IgnoreDwell,
};

// =====================================================================
// TrajectoryPlanConfiguration——求解配置 schema（§5.5 逐字段）
// =====================================================================

/**
 * @brief 轨迹求解配置（§5.5 设计基线九字段；schemaVersion=1）。
 *
 * 业务语义：轨迹规划域的全部可调参数集——进入运行身份（planConfigDigest）
 * 与缓存身份（config.trj 切片条目→sliceId），修改即失效重算（§5.5 身份
 * 路径）。**负向边界（不是遗漏，是设计）**：
 *   - 复检数值不在本配置内（§5.5 末行原文"TRJ-04 步长/预算默认属 P-06，
 *     调整通道＝工程策略"——本结构无任何复检步长/细分预算/间距阈值字段，
 *     杜绝"配置覆盖策略"红线破坏）；
 *   - 无显示单位/会话姿态字段（身份外元素——KIN-12/§4.5 同款纪律，
 *     canonical 布局里根本没有这些字段）。
 *
 * 字段与 §5.5 表的对应（两处实现化微调——DTB §5.4 登记）：
 *   - "smoothTolerance（关节 rad／TCP m，双域各一）"落两字段
 *     smoothToleranceJoint/smoothToleranceTcp（双域各一的字面承接）；
 *   - "plannerSelection{family token, params（键值规范表）}"落
 *     PlannerSelection{familyToken, std::map}（map 键序＝字典序——
 *     "键值规范表"的规范序承载，入码即按该序）。
 *
 * 值语义纯结构；线程安全（并发只读）。
 */
struct TrajectoryPlanConfiguration {
    /// 起始状态选择（§5.3；TaskPoint 时 startTaskPoint 必填）。
    StartStateKind startStateKind = StartStateKind::Home;
    /// 起始状态指向的任务点对象（仅 TaskPoint 有语义；其余形态必须为
    /// 保留值全零——§5.5"不得指向未启用任务点"的启用性校验归展开器）。
    core::ObjectId startTaskPoint;

    /// 规划器选型（TRJ-03"规划器与参数可配置"的承载——§5.5；family 词表
    /// 当前仅登记实现选型（§10.2，WP-16-T06 消费面），扩展走本卡增量
    /// 修订。本批不校验 family 具体值（选型消费在 T06 规划器适配），仅
    /// 校验"非空"——空 token 无身份意义）。
    std::string plannerFamilyToken;
    /// 规划器参数键值规范表（键→值均为 UTF-8 串；map 字典序＝规范序，
    /// 入码即按该序——集合字典序纪律）。本批无白名单（参数语义归 T06），
    /// 仅校验键非空且键值不含 NUL（canonical 编码安全下限）。
    std::map<std::string, std::string> plannerParams;

    /// 确定性随机种子（随机化规划器/采样的确定性种子——§5.5；0 非法，
    /// I-KIN-4 同款拒绝、不做 0→1 静默替换。无量纲）。
    std::uint64_t planningSeed = 0;
    /// 笛卡尔段 IK 连续性检查采样步长上限（m；>0 有限——§5.5；进身份）。
    double cartesianSampleStep = 0.05;
    /// 相邻采样点关节分支连续判定阈值（逐轴上界；转动 rad／移动 m——
    /// §5.5"附录 D 第 3 项同源量纲；取值登记黄金算例"；>0 有限）。
    double ikContinuityThreshold = 1e-6;
    /// 简化/平滑几何保持容差——关节域（rad；>0 有限——§5.5 双域各一）。
    double smoothToleranceJoint = 1e-3;
    /// 简化/平滑几何保持容差——TCP 域（m；>0 有限——同上）。
    double smoothToleranceTcp = 1e-3;
    /// 运动律 token（§5.5 封闭词表——当前唯一合法值
    /// kTimeParamMethodQuinticSplineC2；TRJ-08 扩展点经新 token＋新实现
    /// 承载，本结构不预留空槽）。
    std::string timeParamMethod{kTimeParamMethodQuinticSplineC2};
    /// 关节速度/加速度限值使用比例（无量纲，(0,1]；保守缩放，1.0＝全
    /// 限值——§5.5；真值来源＝CanonicalJoint，本配置只存比例不存限值，
    /// ARC-05）。
    double limitsScaleFactor = 1.0;
    /// 驻留事件消费开关（§5.5 dwellPolicy——组合校验归评估器入口）。
    DwellPolicy dwellPolicy = DwellPolicy::HonorEvents;

    /// 逐字段精确等值（值语义面比较；身份面比较一律走 planConfigDigest
    /// ——字节等值身份，禁数值容差；ObjectId 为字节精确等值）。
    bool operator==(const TrajectoryPlanConfiguration& o) const
    {
        return startStateKind == o.startStateKind
            && startTaskPoint == o.startTaskPoint
            && plannerFamilyToken == o.plannerFamilyToken
            && plannerParams == o.plannerParams
            && planningSeed == o.planningSeed
            && cartesianSampleStep == o.cartesianSampleStep
            && ikContinuityThreshold == o.ikContinuityThreshold
            && smoothToleranceJoint == o.smoothToleranceJoint
            && smoothToleranceTcp == o.smoothToleranceTcp
            && timeParamMethod == o.timeParamMethod
            && limitsScaleFactor == o.limitsScaleFactor
            && dwellPolicy == o.dwellPolicy;
    }
    bool operator!=(const TrajectoryPlanConfiguration& o) const { return !(*this == o); }
};

// =====================================================================
// 合法域校验（fail-fast 调用方错误轨）
// =====================================================================

/**
 * @brief 校验轨迹求解配置合法域（§5.5 各字段约束全表；非法即抛——
 *        fail-fast）。
 *
 * 校验序固定（确定性——同一坏配置必报同一首错，NFR-COR-02）：
 *   1. startStateKind 枚举值域（解码面防御——位型超大值拒绝）；
 *   2. startStateKind==TaskPoint ⇒ startTaskPoint 非全零（isValid），
 *      非 TaskPoint ⇒ startTaskPoint 必须为保留值全零（不携带悬空
 *      附件——身份面"同语义同字节"）；
 *   3. plannerFamilyToken 非空；
 *   4. plannerParams 每条键非空、键与值均不含 NUL（canonical 串安全）；
 *   5. planningSeed ≠ 0（I-KIN-4 同款——0 非法拒绝，不静默替换）；
 *   6. cartesianSampleStep 有限且 >0（m）；
 *   7. ikContinuityThreshold 有限且 >0（rad|m 逐轴）；
 *   8. smoothToleranceJoint 有限且 >0（rad）；
 *   9. smoothToleranceTcp 有限且 >0（m）；
 *  10. timeParamMethod ∈ {quintic-spline-c2}（封闭词表 v1）；
 *  11. limitsScaleFactor 有限且 ∈ (0,1]；
 *  12. dwellPolicy 枚举值域。
 *
 * 错误语义（AGENTS 调用方错误轨）：非法配置＝调用方契约违约，抛
 * TrajectoryError（token "trajectory/plan-config/..."）并在文案给出
 * 首错字段/实际值/合法域——拒绝计算、不钳制不置零更不静默替换
 * （NFR-COR-03）。用户可见诊断（TRJ-INPUT-INVALID）归评估器入口组装
 * （WP-16-T10）。
 *
 * @throws TrajectoryError 任一合法域违约（文案含首错定位——确定性序）
 *
 * 纯函数；线程安全；确定性。
 */
void validateTrajectoryPlanConfiguration(const TrajectoryPlanConfiguration& config);

// =====================================================================
// canonical 编解码（v1 布局——config.trj 切片条目与身份摘要的数据基础）
// =====================================================================

/**
 * @brief canonical v1 编解码（无状态——可共享/可重入；先校验后编码）。
 *
 * 布局 v1（全部小端、字段定序如下、无填充；变长段仅 plannerSelection
 * 集合——集合条目按键**字典序**排布，同集合任意插入序必得同字节，
 * NFR-COR-01/02）：
 * @code
 *   偏移        长度   字段
 *    0           9    magic "IRDCFGTR1"（ASCII 9 字节——kTrajectoryPlanConfigMagicSize）
 *    9           4    u32 schemaVersion = 1
 *   13           1    u8  startStateKind（Home=0/Zero=1/TaskPoint=2）
 *   14          16    ObjectId startTaskPoint（16 字节原样；非 TaskPoint 恒全零）
 *   30           8    u64 planningSeed
 *   38           8    f64 cartesianSampleStep（m——IEEE754 位模式直写）
 *   46           8    f64 ikContinuityThreshold（rad|m）
 *   54           8    f64 smoothToleranceJoint（rad）
 *   62           8    f64 smoothToleranceTcp（m）
 *   70           1    u8  dwellPolicy（HonorEvents=0/IgnoreDwell=1）
 *   71           8    f64 limitsScaleFactor
 *   79           4    u32 timeParamMethod 字节长 Nt
 *   83          Nt    timeParamMethod（UTF-8 字节）
 *   83+Nt        4    u32 plannerFamilyToken 字节长 Nf
 *   87+Nt       Nf    plannerFamilyToken（UTF-8 字节）
 *   87+Nt+Nf     4    u32 params 条目数 M
 *   91+Nt+Nf    ...   M×{ u32 键长＋键字节＋u32 值长＋值字节 }（键字典序）
 * @endcode
 * 空间类型说明：纯标量段 79 字节＋三个变长字符串段——总长随内容变化
 * （kinematics v1 定长是因无集合/串字段；本 schema 词表 token 与参数表
 * 必然变长）。解码端按偏移顺序解析并对"尾部剩余字节≠0"拒绝（长度
 * 封闭——多一字节即结构违约）。
 */
class TrajectoryPlanConfigCodec {
public:
    TrajectoryPlanConfigCodec() = default;

    /**
     * @brief 编码为 canonical 字节（v1 布局；非法配置先经校验拒绝）。
     * @throws TrajectoryError 配置非法（fail-fast——先于任何字节产出）
     */
    std::vector<std::uint8_t> encode(const TrajectoryPlanConfiguration& config) const;

    /**
     * @brief 从 canonical 字节解码（结构校验＋合法域校验双段）。
     * @throws TrajectoryError 字节非 v1 canonical 编码（长度/magic/版本/
     *         枚举值域/非有限浮点/非法值域/尾部剩余字节/变长段截断任一
     *         违约——含就地定位文案）
     */
    TrajectoryPlanConfiguration decode(const std::vector<std::uint8_t>& bytes) const;

private:
    /// 解码主体（decode 的实现细节——变长段读取经 vector::at，截断以
    /// std::out_of_range 冒出，由 decode 壳转译为域错误；私有＝非契约面，
    /// R-2 纪律）。
    TrajectoryPlanConfiguration decodeImpl(const std::vector<std::uint8_t>& bytes) const;
};

// =====================================================================
// configDigest 摘要出口与快照接线（§5.5——进 sliceId、不进 inputBaselineId）
// =====================================================================

/**
 * @brief 计算轨迹求解配置摘要（planConfigDigest＝SHA-256 over canonical
 *        字节——§5.5"canonical 编码→configDigest＝SHA-256"；摘要算法
 *        唯一经 core::ContentDigester，CR-02）。
 *
 * 消费面：切片 config.trj 依赖条目的内容身份（进 sliceId——缓存身份；
 * D-04：求解配置改变不改变样本基准，trajectory 无采样基准概念，仅
 * sliceId）＋结果身份组 TrjSequenceIdentity.planConfigDigest（溯源面）。
 *
 * @return 配置摘要（32 字节；同配置恒同摘要——确定性 NFR-COR-01/02）
 *
 * @throws TrajectoryError 配置非法（同 encode——先校验后编码后摘要）
 *
 * 纯函数；线程安全；确定性。
 */
core::ContentIdentity trajectoryPlanConfigurationDigest(const TrajectoryPlanConfiguration& config);

/**
 * @brief 组装快照配置引用条目（§5.5 身份路径的出口——"切片 config.trj
 *        Configuration 条目"的承载形态；configKindToken 恒
 *        kConfigTrjKindToken（"config.trj"））。
 *
 * contentIdentity＝trajectoryPlanConfigurationDigest 同源值（对
 * canonicalBytes 摘要）——evidence SnapshotBuilder 冻结期的一致性校验
 * （contentIdentity 必须等于对字节的 SHA-256）因此天然通过；快照组装方
 * （L5/评估宿主）把本条目经快照构建面录入。与 kinematics
 * makeConfigurationRefEntry（WP-15-T10）同款先例。
 *
 * @return 配置引用条目（canonicalBytes 为 v1 编码；非法配置先被拒绝）
 *
 * @throws TrajectoryError 配置非法（同 encode）
 *
 * 纯函数；线程安全；确定性。
 */
evidence::ConfigEntry makeTrajectoryPlanConfigRefEntry(const TrajectoryPlanConfiguration& config);

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_PLANCONFIG_HPP
