/**
 * @file   Replay.hpp
 * @brief  DYN-08 数据面——各关节曲线联动投影、三维轨迹时刻回放数据与峰值
 *         定位查询（units/dynamics.md §9.5 dynamics.show-curves /
 *         dynamics.locate-peak / dynamics.replay-at 三命令的领域数据承载
 *         与只读投影面）。
 *
 * 设计依据：
 *   - units/dynamics.md §9.5（UI 协作表：show-curves＝"各关节曲线联动
 *     （q/q̇/q̈/τ/P 逐关节多曲线＋游标联动）——读归档 payload 投影／UI
 *     线程（零计算）"；locate-peak＝"峰值定位（曲线游标跳转峰值时刻＋
 *     三维姿态同步）"；replay-at＝"三维轨迹时刻回放：按 t 驱动会话姿态
 *     ……UI 线程（插值查表）"；投影行＝"回放数据（逐时刻关节状态）随
 *     payload（DYN-08 数据面——不入 dyn Profile，D-14 约束）"）
 *   - units/dynamics.md §4.3（轨迹消费契约——"dynamics 不重算轨迹运动
 *     学、不平滑、不插值外推"；轨迹段结构是"DYN-08 回放的定位键"）、
 *     §4.6（序列纪律——非 Ok 行不进统计／Partial 不阻断）、§4.5（量纲
 *     表——转动 N·m／移动 N 类型化）、§10.0（副作用行"零写盘、零修订、
 *     零项目目录写入"）
 *   - 需求 DYN-08（P1：提供各关节曲线联动、峰值定位和三维轨迹时刻回放）、
 *     DYN-03（输出序列——本面消费其样本行）、KIN-06/AT-04（会话姿态零
 *     修订语义——本面全部纯只读函数，结构性零修订）
 *   - 任务契约 tasks/foundation/WP-17-T08.json（"回放数据承载与峰值定位
 *     消费 T04 已落位的序列/峰值输出（computePeaks/RMS），在既有形态上
 *     扩展不推翻"）
 *
 * ★ 落地面口径（诚实登记，DTB §5.4 精神——单元卡 §1.2 同步登记）：
 *   1. §3.2 布局表无本头（该表十三头为 T01 时点设计基线）——T05 已有
 *      Evidence.hpp→EvidenceBuilder.hpp 先例；本头承载 DYN-08 三命令的
 *      数据面（曲线投影/回放数据/采样/峰值定位），命令适配器面另见
 *      Commands.hpp（§10.7——T08/T09 共担）。
 *   2. "UI 线程（零计算）"的口径（§9.5 原文与 §9.5 备注行"UI 线程不得
 *      执行动力学计算（RNEA/仿真/统计全在 worker）"）：本头的全部产出
 *      均为**数据重组**——逐字段取样本行原值或相邻两样本线性混合，无
 *      RNEA 递推、无积分、无统计聚合（峰值数值不重算——locate 只做
 *      结构对账后按行序取归档行），故 T09 面板可在 UI 线程安全调用；
 *      O(n) 重组与 10⁵ 样本级序列的相容性由"逐字段直拷"的常数因子保证。
 *   3. 峰值定位消费 T04 落位的 computePeaks 行集（Envelope.hpp token 表
 *      行序），不重复实现统计——"统计口径唯一实现点"纪律（§10.4）在
 *      本面的延伸；行集与序列的来源一致性由结构对账（行数＝Ok 关节数
 *      ×每关节行数）防御错位，数值正确性由产出侧（worker 统计＋归档
 *      内容身份寻址）保证。
 *
 * 背景说明（回放数据为什么是"逐时刻帧"而不是直接用 DynamicsSample 行）：
 *   DynamicsSample 一行＝某工况某时刻**某关节**（§4.4），三维回放需要
 *   某时刻**全关节**的完整状态（一帧驱动整机姿态）；buildReplayData 即
 *   行→帧的重组执行点（§9.5 投影行"逐时刻关节状态"），帧内关节序＝
 *   jointIndex 升序（链序——驱动顺序稳定）。回放数据属 payload 的
 *   DYN-08 数据面（不入 dyn Profile 证据项——D-14 约束：证据面与呈现
 *   数据面分离），由调用侧随 payload 组装/归档。
 *
 * 线程安全：三个类均无状态、方法纯函数（输入只读、零副作用——零写盘/
 *   零修订，§10.0）；多实例并行安全。
 * 确定性：同输入→同输出（遍历序＝序列行序、无环境依赖——NFR-COR-02）。
 * 错误语义（§10.0 两轨）：序列行序结构违约（时间倒退/同刻度关节行重复）
 *   与词表越界为**调用方错误**——DynamicsError fail-fast（token 前缀
 *   dynamics/...，不发 DYN-* 稳定码）；查询落空（空序列/越界时刻/无峰
 *   关节）为**合法空态**——std::nullopt 显式返回，绝不伪造数值（
 *   NFR-COR-03）。
 */

#ifndef IRD_DYNAMICS_REPLAY_HPP
#define IRD_DYNAMICS_REPLAY_HPP

#include <cstdint>
#include <optional>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>       // core::ContentIdentity（来源序列
                                            //   内容身份——CON-05 追溯键）
#include <sdurws/ird/core/Identity.hpp>     // core::ObjectId（对象级稳定身份）
#include <sdurws/ird/dynamics/DynTypes.hpp> // DynamicsSeries/DynJointType/
                                            //   PeakRecord/DynamicsValidity
#include <sdurws/ird/dynamics/Envelope.hpp> // kPeakTokenCount/kPeaksPerJoint
                                            //   （峰值 token 词表——行序解读
                                            //   唯一权威，单一事实源）
#include <sdurws/ird/dynamics/Errors.hpp>   // DynamicsError（调用方错误
                                            //   fail-fast 异常轨）

namespace sdurws::ird::dynamics {

// =====================================================================
// 曲线联动投影（§9.5 dynamics.show-curves 数据面——"q/q̇/q̈/τ/P 逐关节
// 多曲线＋游标联动"的领域承载；纯重组——全部值取自样本行原值）。
// =====================================================================

/**
 * @brief 逐关节曲线行（曲线联动数据面的单关节承载——五通道时间序列）。
 *
 * 五通道与量纲（§4.5 量纲表，按 jointType 类型化——转动/连续关节与移动
 * 关节的力/力矩量纲不同、逐关节独立携带不混算，D-DYN-4）：
 *   - t                 时间轴，单位 s（该关节 Ok 行时刻升序——非 Ok 行
 *                       剔除后各关节时间轴可能不同，逐关节自带）
 *   - q                 通道 1 位置：rad（转动/连续）或 m（移动）
 *   - qd                通道 2 速度：rad/s 或 m/s
 *   - qdd               通道 3 加速度：rad/s² 或 m/s²
 *   - generalizedForce  通道 4 总广义力 τ_total：N·m 或 N（按 jointType）
 *   - mechanicalPower   通道 5 机械功率：W（P=τ·q̇，样本点恒等式成立）
 *
 * 通道数组与 t 逐下标对位（同长度）；值全部为对应样本行原字段直拷——
 * 零重算、零平滑、零插值（§4.3 激励映射纪律在呈现面的同口径延伸）。
 * 值语义纯结构；线程安全。
 */
struct JointCurves {
    std::uint32_t jointIndex;       ///< 关节序号（0 基，链序；行序按本字段升序）
    core::ObjectId jointObjectId;   ///< 关节稳定对象 ID（ARC-04——取该关节
                                    ///<   首个 Ok 样本行的值；同关节恒同值）
    DynJointType jointType;         ///< 关节类型——决定 generalizedForce 量纲
    std::vector<double> t;          ///< 时间轴，单位 s（Ok 行时刻升序）
    std::vector<double> q;          ///< 位置通道：rad 或 m
    std::vector<double> qd;         ///< 速度通道：rad/s 或 m/s
    std::vector<double> qdd;        ///< 加速度通道：rad/s² 或 m/s²
    std::vector<double> generalizedForce; ///< 总广义力通道 τ_total：N·m 或 N
    std::vector<double> mechanicalPower;  ///< 机械功率通道：W
    std::size_t nonOkCount;         ///< 该关节被剔除的非 Ok 行数（numericState
                                    ///<   ≠Ok——NFR-COR-03 不进曲线；计数
                                    ///<   如实携带供 UI 标注数据缺失）
};

/**
 * @brief 单工况曲线投影（dynamics.show-curves 的数据面产物——读归档
 *        payload 后经 DynamicsCurveProjector::projectCurves 组装）。
 *
 * 空序列/无 Ok 行→joints 空（Empty 显式语义——不做 0 值伪装，NFR-COR-03）；
 * Partial 序列可投影（非 Ok 行剔除＋nonOkCount 计数——§4.6"Partial 不
 * 阻断"精神在呈现面：UI 侧凭 completeness/nonOkCount 标注数据缺失，
 * 投影器不拒绝）。
 * 值语义纯结构；线程安全。
 */
struct CurveProjection {
    core::ObjectId conditionId;               ///< 工况对象 ID（＝series.
                                              ///<   conditionId 透传）
    core::ContentIdentity sourceSeriesId;     ///< 来源序列 canonical 内容身份
                                              ///<   （＝series.contentIdentity
                                              ///<   透传——投影数据追溯键，
                                              ///<   CON-05）
    std::vector<JointCurves> joints;          ///< 逐关节曲线行（jointIndex
                                              ///<   升序——稳定序，NFR-COR-02）
    DynamicsValidity::Completeness completeness; ///< 来源序列完整性透传
                                              ///<   （UI 侧标注数据缺失的
                                              ///<   呈现依据——不阻断投影）
};

// =====================================================================
// 三维轨迹时刻回放数据（§9.5 dynamics.replay-at 数据面——"逐时刻关节
// 状态"；DYN-08 数据面随 payload、不入 dyn Profile〔D-14〕）。
// =====================================================================

/**
 * @brief 回放帧内单关节状态（某时刻某关节的运动/力完整五量——姿态驱动
 *        只消费 q，其余三量供曲线游标同步与峰值姿态同步呈现）。
 *
 * 量纲同 JointCurves（q: rad|m；qd: rad/s|m/s；qdd: rad/s²|m/s²；
 * generalizedForce: N·m|N；mechanicalPower: W——按 jointType 类型化）。
 * 值语义纯结构；线程安全。
 */
struct ReplayJointState {
    std::uint32_t jointIndex;       ///< 关节序号（0 基，链序；帧内升序）
    core::ObjectId jointObjectId;   ///< 关节稳定对象 ID（ARC-04）
    DynJointType jointType;         ///< 关节类型——量纲标签（驱动方解读 q 单位）
    double q;                       ///< 关节位置：rad（转动/连续）或 m（移动）
                                    ///<   ——三维姿态驱动输入（FK 归 runtime，
                                    ///<   本域零姿态计算）
    double qd;                      ///< 关节速度：rad/s 或 m/s
    double qdd;                     ///< 关节加速度：rad/s² 或 m/s²
    double generalizedForce;        ///< 总广义力 τ_total：N·m 或 N
    double mechanicalPower;         ///< 机械功率：W
};

/**
 * @brief 单时刻回放帧（某时刻全关节状态——三维回放的驱动单位；§9.5
 *        "逐时刻关节状态"的承载）。
 *
 * completeAllJoints 语义：该帧关节行数是否等于数据集全 Ok 关节总数
 * （jointCount）——不等＝该时刻存在非 Ok 行/缺行（该帧状态不完整，
 * UI 侧应标注；帧仍交付——已到手的关节状态不因缺行丢弃，§4.6"不截断
 * 伪造"精神）。
 * 值语义纯结构；线程安全。
 */
struct ReplayFrame {
    double t;                       ///< 帧时刻，单位 s（序列样本时刻——严格
                                    ///<   递增；数据集内帧序即本字段升序）
    std::uint32_t segmentIndex;     ///< 所在轨迹段序号（0 基——该时刻样本行
                                    ///<   携带的段结构；DYN-08 回放定位键）
    std::vector<ReplayJointState> joints; ///< 帧内关节状态（jointIndex 升序）
    bool completeAllJoints;         ///< 帧关节是否齐全（分母＝数据集 jointCount）
};

/**
 * @brief 三维轨迹时刻回放数据集（dynamics.replay-at 的数据面产物——
 *        经 DynamicsReplayProjector::buildReplayData 从序列组装，随
 *        payload 携带；不入 dyn Profile 证据项——D-14 证据面与呈现面
 *        分离）。
 *
 * 值语义纯结构；并发只读安全（构建后不修改）。
 */
struct ReplayData {
    core::ObjectId conditionId;               ///< 工况对象 ID（透传）
    core::ContentIdentity sourceSeriesId;     ///< 来源序列 canonical 内容身份
                                              ///<   （追溯键——CON-05）
    std::vector<ReplayFrame> frames;          ///< 回放帧（t 严格递增；空＝
                                              ///<   Empty 显式语义——无 Ok 行
                                              ///<   不伪造帧，NFR-COR-03）
    std::size_t jointCount;                   ///< 全 Ok 关节总数（帧完整性
                                              ///<   分母 completeAllJoints）
    DynamicsValidity::Completeness completeness; ///< 来源序列完整性透传
};

/**
 * @brief 插值查表的单次查询结果（dynamics.replay-at 按 t 驱动会话姿态的
 *        返回形态——§9.5"UI 线程（插值查表）"）。
 *
 * exact＝true：t 恰命中回放帧（样本时刻）——joints 为该帧原值直拷；
 * exact＝false：t 落在相邻两帧之间——joints 为两帧同关节线性混合
 *   （显示投影语义：插值点仅是相邻样本的线性过渡呈现；τ·q̇ 恒等式与
 *   能量积分只在样本点成立，插值点数值不得用于统计/评估——回放驱动
 *   属呈现面）。
 * 值语义纯结构；线程安全。
 */
struct ReplaySample {
    bool exact;                     ///< 是否精确命中回放帧（false＝区间插值）
    double t;                       ///< 查询时刻，单位 s（＝入参 t）
    std::uint32_t segmentIndex;     ///< 所在轨迹段（取区间左帧的段——区间
                                    ///<   [tL,tR) 归左段，保守边界口径同
                                    ///<   §5.4"样本时刻 t≥tEvent 生效"的
                                    ///<   左闭精神；exact 时即该帧段号）
    std::vector<ReplayJointState> joints; ///< 该时刻关节状态（jointIndex 升序）
    bool completeAllJoints;         ///< 关节齐全性（插值时＝两帧共有该关节才
                                    ///<   可插值——任一侧缺该关节则该关节
                                    ///<   单侧取值且本位 false；exact 时
                                    ///<   ＝帧值）
};

// =====================================================================
// 曲线联动投影器（show-curves 数据面的组装执行点）。
// =====================================================================

/**
 * @brief 曲线联动投影器（§9.5 dynamics.show-curves 数据面——纯数据重组，
 *        零动力学计算）。
 */
class DynamicsCurveProjector {
public:
    DynamicsCurveProjector() = default;

    /// 可拷贝可移动（无状态——实例仅是调用边界）。
    DynamicsCurveProjector(const DynamicsCurveProjector&) = default;
    DynamicsCurveProjector& operator=(const DynamicsCurveProjector&) = default;

    /**
     * @brief 从序列投影各关节五通道曲线（show-curves 数据面；[in] 只读）。
     *
     * 组装规则：
     *   - 行序防御校验（构建器产物不可能的形态出现＝上游违约，fail-fast
     *     暴露）：t 非降（倒退→token "series-non-monotonic"，同
     *     SeriesBuilder 口径）；同刻度 (t, jointIndex) 行重复→token
     *     "series-row-duplicate"。★ 与 SeriesBuilder 对重复行"finalize
     *     标记不拒收"的差异（诚实登记）：投影是曲线显示的直接数据源，
     *     重复行取值歧义会在显示面画出错误曲线而非标注问题，且投影器
     *     零计算零诊断、无 diagRefs 产出通道（§9.5"UI 线程零计算"），
     *     结构违约只能 fail-fast 暴露（调用方错误轨——§10.0）；
     *   - 非 Ok 行剔除（numericState≠Ok——NFR-COR-03 不进曲线），逐关节
     *     计入 nonOkCount；
     *   - 逐关节 Ok 行按时间序组装五通道（值＝样本行原字段直拷——零重算
     *     零平滑，§4.3 同口径）；
     *   - completeness/conditionId/sourceSeriesId 透传。
     *
     * @param series [in] 单工况序列（SeriesBuilder 产物——身份/行序已冻结）
     * @return 曲线投影（joints 按 jointIndex 升序；空序列/无 Ok 行→
     *         joints 空——Empty 显式语义）
     *
     * @throws DynamicsError 序列行序结构违约（见上——调用方错误，不发
     *         稳定码）
     *
     * 复杂度：O(n)，n 为样本行数（单遍扫描＋逐关节分组）。
     */
    CurveProjection projectCurves(const DynamicsSeries& series) const;
};

// =====================================================================
// 回放数据构建与插值查表（replay-at 数据面的组装与查询执行点）。
// =====================================================================

/**
 * @brief 回放投影器（§9.5 dynamics.replay-at 数据面——行→帧重组＋按 t
 *        插值查表；纯数据重组，零动力学计算）。
 */
class DynamicsReplayProjector {
public:
    DynamicsReplayProjector() = default;

    /// 可拷贝可移动（无状态——实例仅是调用边界）。
    DynamicsReplayProjector(const DynamicsReplayProjector&) = default;
    DynamicsReplayProjector& operator=(const DynamicsReplayProjector&) = default;

    /**
     * @brief 从序列构建逐时刻回放数据集（replay-at 数据面；[in] 只读）。
     *
     * 组装规则：行序防御校验同 projectCurves（倒退/同刻度关节行重复
     * fail-fast）→按 t 分组 Ok 行成帧（同刻度全部关节进同帧；帧内
     * jointIndex 升序——行序已校验，顺序收集即升序）→帧完整性标记
     * （帧行数≠jointCount→completeAllJoints=false）→身份透传。
     *
     * @param series [in] 单工况序列（只读）
     * @return 回放数据集（frames 按 t 严格递增；无 Ok 行→frames 空——
     *         Empty 显式语义，绝不伪造帧）
     *
     * @throws DynamicsError 序列行序结构违约（同 projectCurves——调用方
     *         错误轨）
     *
     * 复杂度：O(n)，n 为样本行数。
     */
    ReplayData buildReplayData(const DynamicsSeries& series) const;

    /**
     * @brief 按 t 查询单时刻关节状态（replay-at 的"插值查表"执行点；
     *        [in] 只读）。
     *
     * 查询规则（逐步）：
     *   1. t 非有限（NaN/±Inf）→DynamicsError fail-fast（token
     *      "replay-time-invalid"——调用方错误；NFR-COR-03 不静默）；
     *   2. 数据集无帧（Empty）→nullopt（合法空态——不伪造）；
     *   3. t 越出 [首帧 t, 末帧 t]→nullopt（**不外推**——§4.3"不平滑、
     *      不插值外推"纪律在回放面同口径；越界时刻的数据不存在，呈现
     *      侧应停在端点）；
     *   4. t 命中某帧时刻→exact=true，该帧原值直拷；
     *   5. t 落在相邻帧 [tL,tR) 区间→线性插值：α=(t−tL)/(tR−tL)（tR>tL
     *      由帧 t 严格递增保证），逐关节双指针对齐两帧——共有关节五量
     *      线性混合，单侧独有关节取单侧值（无对侧数据源，不跨关节虚构
     *      ——任一侧缺失即 completeAllJoints=false）。
     *
     * @param data [in] 回放数据集（buildReplayData 产物；只读）
     * @param t    [in] 查询时刻，单位 s（必须有限——越界返回空态而非异常）
     * @return 单时刻状态（见 ReplaySample 语义）；空态→nullopt
     *
     * @throws DynamicsError t 非有限（调用方错误轨）
     *
     * 复杂度：O(log n ＋ J)——帧二分＋逐关节对齐（帧内升序双指针）。
     */
    std::optional<ReplaySample> sampleAt(const ReplayData& data, double t) const;
};

// =====================================================================
// 峰值定位（locate-peak 数据面——消费 T04 computePeaks 行集，零重算）。
// =====================================================================

/**
 * @brief 峰值定位器（§9.5 dynamics.locate-peak 数据面——曲线游标跳转
 *        峰值时刻＋三维姿态同步的查询执行点）。
 *
 * 消费口径：峰值行集来自 T04 落位的
 * DynamicsEnvelopeCalculator::computePeaks（归档于 payload/统计结果——
 * UI 线程零计算，本类不重算统计；行序解读唯一权威＝Envelope.hpp token
 * 表：(jointIndex 升序, 每关节 6 行 token 序)）。
 */
class DynamicsPeakLocator {
public:
    DynamicsPeakLocator() = default;

    /// 可拷贝可移动（无状态——实例仅是调用边界）。
    DynamicsPeakLocator(const DynamicsPeakLocator&) = default;
    DynamicsPeakLocator& operator=(const DynamicsPeakLocator&) = default;

    /**
     * @brief 定位某关节某量纲通道的峰值记录（locate-peak 数据面；[in]
     *        全参只读）。
     *
     * 定位规则（逐步）：
     *   1. tokenIndex 越出峰值 token 词表（[0, kPeakTokenCount)——
     *      Envelope.hpp 表：0 力矩正/1 力矩反/2 速度/3 加速度/4 功率正/
     *      5 功率反）→DynamicsError fail-fast（token
     *      "peak-token-out-of-range"——调用方错误，词表越界不可静默）；
     *   2. 从序列推导含 Ok 行关节的升序表（行序解读锚——computePeaks
     *      行集自 Ok 关节并集生成，关节 0 全非 Ok 时行集自关节 1 起，
     *      仅凭行下标无法反推关节——必须以序列对账，这也是防"行集与
     *      序列不同源"错位的结构校验面）；
     *   3. jointIndex 不在 Ok 关节表→nullopt（该关节无 Ok 行无峰值——
     *      合法空态，不伪造）；
     *   4. 行集结构对账：peaks.size() 必须等于 Ok 关节数×kPeaksPerJoint
     *      （不等＝行集与序列不同源/被截断——DynamicsError fail-fast，
     *      token "peaks-series-mismatch"；定位错位比定位失败更危险，
     *      fail-fast 暴露）；
     *   5. 返回行下标＝(jointIndex 在 Ok 关节表中的位次)×kPeaksPerJoint
     *      ＋tokenIndex 的行（含 value/tPeakS/segmentIndex/持续窗/来源
     *      工况——游标跳转时刻取 tPeakS，三维姿态同步以 tPeakS 经
     *      sampleAt 查询关节状态——链路组合见测试端到端用例）。
     *
     * @param series     [in] 与峰值行集同源的序列（只读——行序解读锚）
     * @param peaks      [in] computePeaks(series) 的行集（归档 payload；
     *                   只读——本方法不重算其数值）
     * @param jointIndex [in] 目标关节序号（0 基链序）
     * @param tokenIndex [in] 峰值 token 序号（0..kPeakTokenCount-1——
     *                   Envelope.hpp token 表）
     * @return 峰值记录（tPeakS＝游标跳转时刻，s）；空态→nullopt
     *
     * @throws DynamicsError tokenIndex 越界／peaks 与 series 结构不同源
     *         （调用方错误轨——不发稳定码）
     *
     * 复杂度：O(n)（Ok 关节表推导单遍扫描；定位 O(1)）。
     */
    std::optional<PeakRecord> locate(const DynamicsSeries& series,
                                     const std::vector<PeakRecord>& peaks,
                                     std::uint32_t jointIndex,
                                     int tokenIndex) const;
};

}  // namespace sdurws::ird::dynamics

#endif  // IRD_DYNAMICS_REPLAY_HPP
