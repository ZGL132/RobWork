/**
 * @file   MappingTypes.hpp
 * @brief  传动映射输入侧归一化值类型（units/drivetrain.md §5）——关节轴/
 *         电机轴/带符号传动比（c 口径）/耦合矩阵（R2 类型面）/效率模型/
 *         转子惯量/传动配置身份与归一化传动模型 DriveTrainModel。
 *
 * 设计依据：
 *   - units/drivetrain.md §5.2（核心类型设计——签名＝设计基线；实现任务
 *     允许按 DTB §5.4 微调并登记偏差）、§5.3（方向、符号与轴序——★全文
 *     唯一约定：c＝Δq_joint/Δθ_motor；Δq_joint＝C·Δθ_motor；
 *     τ_motor＝Cᵀ·τ_joint）、§5.4（物理量单位表）、§5.5（身份链）、
 *     §6.2/§6.3（R1 映射定义与阻断面）、§9.2（转子惯量输入要求）
 *   - 需求 DYN-04（M-12 映射动力学口径）、MDL-16（力矩限值消费）、
 *     SEL-10（转子字段"未计入 DWC 合成"前提）、NFR-COR-03（非有限拒绝）
 *   - 任务契约 tasks/foundation/WP-18-T03.json（acceptance 1——唯一映射
 *     实现的输入面）
 *
 * ★ 传动比口径（D-DT-4/P-DT-10，全文唯一）：本单元全部公共面对传动比
 *   统一采用 c＝Δq_joint/Δθ_motor（关节位移增量／电机位移增量，无量纲
 *   rad/rad）；减速器 n:1 对应 c＝1/n；对角情形 C＝diag(c_1..c_n)；虚功
 *   对偶 τ_motor＝Cᵀ·τ_joint；"J·i²" 记法按 i＝1/c 换算（对角反射惯量
 *   J_reflected＝J_rotor/c²）。禁用第二种未声明约定（如 n＝θ/q）。
 *
 * ★ 矩阵承载的实现微调登记（DTB §5.4——卡 §5.2 实现注记的落位选择）：
 *   卡面把矩阵类型记作 `Matrix` 占位并建议"按 L1 实测登记替换"。本实现
 *   按实测选择**自持行主序轻量矩阵 RowMatrix（std::vector<double> 承载）**：
 *   R1 阶段全部矩阵运算可解析退化——对角映射只涉及逐轴标量运算（除以
 *   c_j、乘以 c_j），对角矩阵条件数＝max|c|/min|c|（解析式，谱条件数在对
 *   角阵上恰为该值），无通用矩阵库需求；自持承载使公共头保持纯 std 值
 *   语义（零 L1 类型向消费单元传播——dynamics/selection 经③端口值传递
 *   消费，卡 §3.2/§12）。R2 通用非对角矩阵（求逆/交叉惯量矩阵）归
 *   WP-18-T05 落位时按 L1 实测再对齐（届时允许换承载——契约演进走单元
 *   卡增量修订）。
 *
 * 错误语义（卡 §13.0）：结构非法（维度/对角元为 0/非有限/轴序/轴类型/
 *   空输入/转子值非法）＝调用方契约违约，构造入口 fail-fast 抛
 *   std::invalid_argument（消息携带 DT-* 码语义）——不返回半结果；
 *   "缺失"类（效率条目缺失、转子条目缺失、负载折算惯量缺失）不是非法，
 *   以 optional/列表缺席承载，由映射核心走 DataInsufficient 降级素材。
 *
 * 线程安全：全部纯值类型（构造后按不可变对待——卡 §13.9 归一化模型
 *   "构造后不可变，跨线程只读共享"）。
 * 确定性：无时钟/随机源；同构造入参必得等价对象（NFR-COR-02）。
 */

#ifndef IRD_DRIVETRAIN_MAPPINGTYPES_HPP
#define IRD_DRIVETRAIN_MAPPINGTYPES_HPP

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sdurws::ird::drivetrain {

// =====================================================================
// 关节类型与来源标记（卡 §5.2）
// =====================================================================

/**
 * @brief 关节轴类型（R1 承诺面＝旋转；prismatic 在 R1 触发范围外诊断）。
 *
 * 卡 §5.2 JointDriveAxis 行原文："Revolute / Continuous(已确认工作范围) /
 * Prismatic——Prismatic 在 R1 触发 DT-AXIS-TYPE-OUT-OF-SCOPE"。
 * mimic/闭环/planar/floating 链在 modeling 侧已被 R1 阻断成模（MDL-12），
 * 不进入本词表——本枚举只承载"能到达映射输入的类型"；映射入口的类型
 * 检查（Prismatic→DT-AXIS-TYPE-OUT-OF-SCOPE）是 SEL-09"范围外"诊断的
 * 第二道防线（卡 §6.3 行 3）。
 */
enum class JointKind : std::uint8_t {
    Revolute,   ///< 旋转关节（有限行程；位置单位 rad）
    Continuous, ///< 连续旋转关节（已确认工作范围——modeling 侧成模约束；rad）
    Prismatic,  ///< 移动关节（位移单位 m）——R1 映射范围外（SEL-09/MDL-12）
};

/**
 * @brief 值来源标记（卡 §5.2 SourcedValueTag——进入结果限定语的面）。
 *
 * 卡面来源词表：ModelingField（建模字段）/CatalogBackfill（选型回填——
 * SEL-10）/ConfigEntry（配置条目值传递）/UserProvided（用户提供）/
 * Estimated（估算）。Estimated 来源贯穿映射结果（卡 §10.7——"估算来源
 * 标记贯穿映射结果→证据/报告保留限定语 RPT-05"），本枚举即该标记的
 * 承载；词表封闭，新增来源＝设计变更（单元卡增量修订）。
 */
enum class SourcedValueTag : std::uint8_t {
    ModelingField,  ///< 建模字段（robot-drivetrain 对象权威值）
    CatalogBackfill,///< 选型回填（SEL-10——转子字段按回填值独立计入）
    ConfigEntry,    ///< 配置条目值传递（组合候选的目录行派生值）
    UserProvided,   ///< 用户显式提供（组装方值对象）
    Estimated,      ///< 估算值（结果携带估算限定语——RPT-05 素材）
};

// =====================================================================
// 轻量行主序矩阵承载（文件头注"矩阵承载的实现微调登记"）
// =====================================================================

/**
 * @brief 行主序动态尺寸矩阵（R1 归一化矩阵 Ĉ 的承载——文件头注）。
 *
 * 表示约定：data 按行主序连续存放 rows×cols 个元素，元素 (r,c) 位于
 * data[r*cols+c]（r∈[0,rows)、c∈[0,cols)，无量纲——rad/rad 同轴类型
 * 才可映射，卡 §5.4）。零 Qt、零框架类型：R1 全部消费面（对角检查/
 * 解析条件数/维度检查）只读本结构，无通用矩阵库调用。
 *
 * 线程安全：纯值类型。
 */
struct RowMatrix {
    std::size_t rows = 0;          ///< 行数（＝适用关节数，卡 §5.3 维度规则）
    std::size_t cols = 0;          ///< 列数（＝对应电机轴数）
    std::vector<double> data{};    ///< 行主序元素（size 恒＝rows*cols——结构校验点）

    /// 元素只读访问（无越界检查的快速路径——调用方保证索引合法；
    /// 校验路径请用 at()）。
    double operator()(std::size_t r, std::size_t c) const noexcept
    {
        return data[r * cols + c];
    }

    /// 带边界检查的元素访问：越界属调用方契约违约，fail-fast 抛
    /// std::out_of_range（卡 §13.0 错误语义——不返回半结果）。
    double at(std::size_t r, std::size_t c) const;

    /// 维度与元素数自洽（rows*cols==data.size()——结构有效性判据之一）。
    bool wellFormed() const noexcept
    {
        return data.size() == rows * cols;
    }

    bool operator==(const RowMatrix& o) const
    {
        return rows == o.rows && cols == o.cols && data == o.data;
    }
    bool operator!=(const RowMatrix& o) const { return !(*this == o); }
};

// =====================================================================
// 轴与传动配置值类型（卡 §5.2 签名逐字段承载）
// =====================================================================

/**
 * @brief 关节轴（身份与类型——权威来自建模链，本单元只消费）。
 *
 * 卡 §5.2：jointId 为关节对象 ID（ARC-04 稳定 ID，不经名称匹配）；
 * localName 为运行时局部名，仅诊断呈现用（⑥名称端口反解由接纳层保证，
 * 本单元不做任何名称前缀操作——R-4）。
 */
struct JointDriveAxis {
    core::ObjectId jointId; ///< 关节对象 ID（稳定身份；全零＝非法——构造校验）
    JointKind kind = JointKind::Revolute; ///< 轴类型（Prismatic 在 R1 范围外）
    std::string localName;  ///< 运行时局部名（诊断呈现；不进入任何身份/计算）

    bool operator==(const JointDriveAxis& o) const
    {
        return jointId == o.jointId && kind == o.kind && localName == o.localName;
    }
    bool operator!=(const JointDriveAxis& o) const { return !(*this == o); }
};

/**
 * @brief 电机轴（R1：与旋转关节一一对应；排列规则＝对应关节串联序）。
 *
 * 卡 §5.2：motorId 为电机轴对象 ID（建模侧分配；R1 可与 jointId 同源
 * 派生）；jointIndex 为对应关节轴下标——轴序纪律（§5.3）要求
 * motorAxes[k].jointIndex == k（电机轴按"对应关节串联序"排列；不一致
 * →DT-INPUT-AXIS-ORDER-MISMATCH，不允许静默重排——重排等价于改输入，
 * 必须由组装方显式完成）。
 */
struct MotorDriveAxis {
    core::ObjectId motorId;   ///< 电机轴对象 ID（稳定身份；全零＝非法）
    std::size_t jointIndex = 0; ///< 对应关节轴下标（轴序纪律见类注释）

    bool operator==(const MotorDriveAxis& o) const
    {
        return motorId == o.motorId && jointIndex == o.jointIndex;
    }
    bool operator!=(const MotorDriveAxis& o) const { return !(*this == o); }
};

/**
 * @brief 带符号传动比（★c 口径——文件头注"传动比口径"）。
 *
 * c＝Δq_joint/Δθ_motor（无量纲 rad/rad；0 非法——DT-RATIO-ZERO，除法
 * 无意义）。c＞0 电机与关节同向；c＜0 反向。映射核心契约支持带符号 c
 * （值传递通道可达——D-DT-5）；经项目对象通道（R1）受 modeling
 * I-MDL-11"ratio 有限＞0"约束，负值在 R1 项目数据不可达（P-DT-5 登记）。
 */
struct TransmissionRatio {
    double c = 0.0; ///< 传动比 c＝Δq_joint/Δθ_motor（无量纲；0 非法——构造校验）
    SourcedValueTag source = SourcedValueTag::ModelingField; ///< 来源标记

    bool operator==(const TransmissionRatio& o) const
    {
        return c == o.c && source == o.source;
    }
    bool operator!=(const TransmissionRatio& o) const { return !(*this == o); }
};

/**
 * @brief R2 耦合矩阵值类型（MDL-21——本任务仅类型面落位；R1 收到即阻断）。
 *
 * 卡 §5.2：C 的行＝适用关节（窗口内串联序）、列＝对应电机轴；R1 能力下
 * 出现本类型（模型 window 有值）→映射入口 DT-COUPLING-STAGE-LOCKED
 * （能力门控第一检查，卡 §6.3 行 1——显示/配置标签不改变计算能力）。
 * conditionNumber 为编译期/映射期检查用实测条件数（无量纲；阈值来源
 * P-RT-7 对齐——卡 §7.3；通用矩阵条件数算法随 T05 落位，本类型先承载
 * 组装方申报值的传递面）。
 */
struct CouplingMatrix {
    RowMatrix C{};                              ///< 耦合矩阵（行＝窗口关节，列＝电机轴）
    std::vector<core::ObjectId> jointRange{};   ///< 适用关节窗口（有序；行序一致）
    double conditionNumber = 0.0;               ///< 实测条件数（无量纲；组装方申报传递面）

    bool operator==(const CouplingMatrix& o) const
    {
        return C == o.C && jointRange == o.jointRange && conditionNumber == o.conditionNumber;
    }
    bool operator!=(const CouplingMatrix& o) const { return !(*this == o); }
};

/// R2 耦合窗口（卡 §5.2 DriveTrainModel.window 字段——R1 下存在即阻断）。
using CouplingWindow = CouplingMatrix;

/**
 * @brief 方向相关效率模型（值对象；权威值归 modeling/目录——卡 §5.2）。
 *
 * η⁺（etaForward）＝驱动方向（P_joint＞0，功率自电机流向负载）效率；
 * η⁻（etaBackward）＝再生方向（P_joint＜0，负载经传动流回电机侧）效率。
 * 合法域 (0,1]——η≤0 或 η＞1 为非法值（DT-EFFICIENCY-INVALID，比较型
 * 诊断；卡 §10.1）；**缺失**（模型 efficiency 列表缺该轴条目）不是非法，
 * 走 DataInsufficient 降级（卡 §10.7——不以 η＝1 静默替代）。
 * source==Estimated 时结果携带估算限定语素材（§10.7）。
 */
struct EfficiencyModel {
    double etaForward = 0.0;  ///< η⁺ 驱动方向效率（无量纲；合法 (0,1]）
    double etaBackward = 0.0; ///< η⁻ 再生方向效率（无量纲；合法 (0,1]）
    SourcedValueTag source = SourcedValueTag::ModelingField; ///< 来源标记（Estimated→限定语）

    bool operator==(const EfficiencyModel& o) const
    {
        return etaForward == o.etaForward && etaBackward == o.etaBackward
            && source == o.source;
    }
    bool operator!=(const EfficiencyModel& o) const { return !(*this == o); }
};

/**
 * @brief 电机转子等效惯量（**未计入** DWC 合成惯量——卡 §9.4 单一计入
 *        纪律的输入前提）。
 *
 * 卡 §5.2/§9.2：rotorInertia 为电机轴系转子值（单位 kg·m²），必须＞0 且
 * 有限——出现即构造校验（负值/零/非有限→DT-INERTIA-INVALID，
 * fail-fast）；**缺失**（列表缺该轴条目）→映射含转子项力矩不可得，按
 * 理想口径输出＋DT-ROTOR-MISSING（数据类降级，卡 §10.7）。
 * SEL-10 交接：组装方按"转子等效惯性"独立字段取值（未计入 DWC 合成），
 * 防与壳体质量/连杆合成惯量重复计入（D-DT-7）。
 */
struct RotorInertiaModel {
    double rotorInertia = 0.0; ///< 转子等效惯量（kg·m²，电机轴系；必须＞0 且有限）
    SourcedValueTag source = SourcedValueTag::CatalogBackfill; ///< 来源标记（回填→限定素材）

    bool operator==(const RotorInertiaModel& o) const
    {
        return rotorInertia == o.rotorInertia && source == o.source;
    }
    bool operator!=(const RotorInertiaModel& o) const { return !(*this == o); }
};

/**
 * @brief 传动配置内容身份（进入映射结果身份链——卡 §5.5）。
 *
 * 字节等值比较（evidence"三种等价"纪律；数值容差严禁作为身份等价关系）：
 * 任一字段变更→映射结果身份变更→旧结果按切片机制失效（CON-04/05）。
 */
struct DriveTrainIdentity {
    core::ContentVersion drivetrainObjectCv{}; ///< robot-drivetrain 对象内容版本（任一字段变更必然新版本）
    std::uint32_t algorithmVersion = 0;        ///< 映射算法版本（演进登记于卡 §18.4；0＝未登记非法）
    std::uint32_t contractVersion = 0;         ///< 输入/输出契约版本（进 sliceId——CON-04）

    bool operator==(const DriveTrainIdentity& o) const
    {
        return drivetrainObjectCv == o.drivetrainObjectCv
            && algorithmVersion == o.algorithmVersion
            && contractVersion == o.contractVersion;
    }
    bool operator!=(const DriveTrainIdentity& o) const { return !(*this == o); }
};

// =====================================================================
// 归一化传动模型（映射唯一消费形态——卡 §5.2）
// =====================================================================

/**
 * @brief 归一化传动模型（权威参数的规范化计算视图——映射唯一消费形态）。
 *
 * 背景说明（卡 §3.1"拥有"行）：权威传动参数在 modeling 的 robot-drivetrain
 * 对象（经 runtime 编译链）；本结构是组装方按编译视图构造的**中立值对象**
 * （值传递——O-07/D-DT-13 推荐形态），drivetrain 不消费 modeling/runtime
 * 任何头（依赖白名单红线）。构造一律经 makeDiagonalDriveTrainModel（R1）
 * ——构造入口完成结构校验（非法即 fail-fast，卡 §13.0），此后不可变。
 *
 * 字段与卡 §5.2 签名的对应（偏差已按 DTB §5.4 登记于单元卡 §18.3）：
 *   - chat：归一化满矩阵 Ĉ（n_joints×n_motors）——R1＝对角（本工厂只产
 *     对角形；非对角/窗口输入被阻断面拒绝）；
 *   - ratios：对角口径逐轴视图（chat(j,j)＝ratios[j].c，含符号）；
 *   - window：R2 耦合窗口——R1 恒无值（有值即能力门控阻断）；
 *   - efficiency/rotor：逐电机轴条目——**按下标配对**（条目 j 对应电机轴
 *     j；缺失条目＝该轴数据缺失，列表可以短于轴数或整体为空——降级语义，
 *     不是非法）；条目内的**值**非法（η∉(0,1]、J_rotor≤0/非有限）才是
 *     调用方错误（构造校验 fail-fast）；
 *   - zeroOffsetMotor：电机零位偏置 θ_off（rad；q_joint=0 对应的电机角），
 *     长度＝电机轴数（逐轴必有——无偏置填 0）；
 *   - ratedTorque：★卡面签名外增量字段（DTB §5.4 微调已登记）——逐电机
 *     轴额定/参考力矩（N·m，电机轴系；MDL-16 传动配置力矩限值字段的工作
 *     点消费面，卡 §2.1/§10.4 负载率参考值分母）；缺失（nullopt）→负载
 *     率＝不适用（ERR-01 显式标记，不伪造）。**仅作参考值承载，本单元不
 *     做任何限位判定**（P-DT-2——校验归消费域）。
 */
struct DriveTrainModel {
    std::vector<JointDriveAxis> jointAxes{};   ///< 关节轴表（按关节串联序——卡 §5.3 轴序表）
    std::vector<MotorDriveAxis> motorAxes{};   ///< 电机轴表（排列规则同轴序表）
    RowMatrix chat{};                          ///< 归一化矩阵 Ĉ（无量纲；R1＝对角方阵）
    std::vector<TransmissionRatio> ratios{};   ///< 对角口径逐轴视图（c_j＝chat(j,j)）
    std::optional<CouplingWindow> window{};    ///< R2 耦合窗口（R1 禁止出现——存在即 DT-COUPLING-STAGE-LOCKED）
    std::vector<EfficiencyModel> efficiency{}; ///< 逐电机轴效率（**下标配对**；缺失条目＝降级语义）
    std::vector<RotorInertiaModel> rotor{};    ///< 逐电机轴转子惯量（**下标配对**；缺失＝DT-ROTOR-MISSING 降级）
    std::vector<double> zeroOffsetMotor{};     ///< 电机零位偏置 θ_off（rad；长度＝电机轴数）
    /// 逐电机轴额定/参考力矩（N·m，电机轴系；MDL-16 消费面——nullopt＝
    /// 缺失→负载率不适用；★卡面签名外增量，登记于单元卡 §18.3）。
    std::vector<std::optional<double>> ratedTorque{};
    DriveTrainIdentity identity{};             ///< 内容身份（进映射结果身份链——§5.5）

    bool operator==(const DriveTrainModel& o) const
    {
        return jointAxes == o.jointAxes && motorAxes == o.motorAxes && chat == o.chat
            && ratios == o.ratios && window == o.window && efficiency == o.efficiency
            && rotor == o.rotor && zeroOffsetMotor == o.zeroOffsetMotor
            && ratedTorque == o.ratedTorque && identity == o.identity;
    }
    bool operator!=(const DriveTrainModel& o) const { return !(*this == o); }
};

/**
 * @brief 负载折算惯量输入（卡 §9.1/§9.5——惯量比的分子来源）。
 *
 * J_load@joint（关节轴系，kg·m²）由组装方提供并携带来源标记；权威产生者
 * 待 dynamics 卡对齐（P-DT-4——未对齐前按"组装方提供＋来源标记"保守
 * 消费）。per-axis **下标配对**：条目 j 对应电机轴 j；缺失条目→该轴惯量
 * 比＝不适用（ERR-01 显式标记；反射惯量〔转子侧〕照常输出——卡 §10.7）。
 * 值必须＞0 且有限（负载惯量物理上为正；非正/非有限＝调用方契约违约，
 * 构造校验 fail-fast）。
 */
struct LoadInertiaEntry {
    double loadInertiaJointSide = 0.0; ///< 负载折算惯量（kg·m²，关节轴系；必须＞0 且有限）
    SourcedValueTag source = SourcedValueTag::UserProvided; ///< 来源标记（P-DT-4 保守消费）

    bool operator==(const LoadInertiaEntry& o) const
    {
        return loadInertiaJointSide == o.loadInertiaJointSide && source == o.source;
    }
    bool operator!=(const LoadInertiaEntry& o) const { return !(*this == o); }
};

// =====================================================================
// 对角归一化模型工厂（R1 唯一合法构造入口——构造即校验）
// =====================================================================

/**
 * @brief 构造 R1 对角归一化传动模型（构造入口完成全部结构校验——卡
 *        §13.0"构造校验 fail-fast"；校验顺序＝卡 §6.3 阻断面表序中可在
 *        构造期执行的部分）。
 *
 * 校验集（每项注明触发码——消息文本以 DT-* 码开头，供调用方定位）：
 *   1. jointAxes 非空、motorAxes 非空且尺寸相等——否则 DT-INPUT-EMPTY/
 *      DT-INPUT-DIMENSION-MISMATCH；
 *   2. 逐轴 JointKind::Prismatic → DT-AXIS-TYPE-OUT-OF-SCOPE（SEL-09
 *      范围外——不静默套用旋转传动）；
 *   3. 轴序纪律 motorAxes[k].jointIndex==k——否则
 *      DT-INPUT-AXIS-ORDER-MISMATCH（不静默重排）；
 *   4. 逐轴 c 有限且≠0、chat 为方阵且对角元与 c 一致、非对角元全零——
 *      DT-MATRIX-NONFINITE/DT-RATIO-ZERO/DT-INPUT-DIMENSION-MISMATCH/
 *      DT-MATRIX-NONDIAGONAL-LOCKED；
 *   5. zeroOffsetMotor 长度＝轴数且逐元素有限——
 *      DT-INPUT-DIMENSION-MISMATCH/DT-MATRIX-NONFINITE；
 *   6. efficiency 条目 η∈(0,1] 且有限——DT-EFFICIENCY-INVALID；
 *   7. rotor 条目 J_rotor＞0 且有限——DT-INERTIA-INVALID（卡 §9.2）；
 *   8. ratedTorque 条目（若有值）有限且＞0——DT-INPUT-DIMENSION-MISMATCH
 *      族（力矩限值参考值物理为正；NFR-COR-03 非有限拒绝）；
 *   9. identity.algorithmVersion/contractVersion＞0（0＝保留值非法）。
 *   另：window 有值在构造期同样拒绝（DT-COUPLING-STAGE-LOCKED）——R1
 *   工厂不产耦合模型；耦合输入的阻断测试面在映射入口校验器（RCoupling
 *   校验器接受完整模型＋能力位，见 MappingCore.hpp）。
 *
 * @param jointAxes       [in] 关节轴表（串联序；非空）
 * @param motorAxes       [in] 电机轴表（对应关节串联序；尺寸＝关节轴数）
 * @param ratios          [in] 逐轴带符号传动比（尺寸＝轴数；c 有限≠0）
 * @param zeroOffsetMotor [in] 电机零位偏置 θ_off（rad；尺寸＝轴数）
 * @param identity        [in] 传动配置内容身份（版本字段须＞0）
 * @param efficiency      [in] 逐电机轴效率条目（可空/短于轴数＝缺失降级；
 *                             出现的值必须合法）
 * @param rotor           [in] 逐电机轴转子惯量条目（可空/短＝DT-ROTOR-
 *                             MISSING 降级；出现的值必须＞0 有限）
 * @param ratedTorque     [in] 逐电机轴额定/参考力矩（N·m；可空；nullopt
 *                             条目＝负载率不适用）
 *
 * @return 结构合法的归一化模型（不可变共享——卡 §13.9）
 *
 * @throws std::invalid_argument 上述任一校验失败（消息以 DT-* 码开头——
 *         调用方契约违约 fail-fast，不返回半结果）
 *
 * 线程安全：纯函数（可重入）。
 */
DriveTrainModel makeDiagonalDriveTrainModel(
    std::vector<JointDriveAxis> jointAxes,
    std::vector<MotorDriveAxis> motorAxes,
    std::vector<TransmissionRatio> ratios,
    std::vector<double> zeroOffsetMotor,
    DriveTrainIdentity identity,
    std::vector<EfficiencyModel> efficiency = {},
    std::vector<RotorInertiaModel> rotor = {},
    std::vector<std::optional<double>> ratedTorque = {});

}  // namespace sdurws::ird::drivetrain

#endif  // IRD_DRIVETRAIN_MAPPINGTYPES_HPP
