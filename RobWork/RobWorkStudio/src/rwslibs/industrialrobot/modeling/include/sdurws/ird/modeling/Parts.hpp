/**
 * @file   Parts.hpp
 * @brief  modeling 四部件对象值模型——ToolDefinition（tool-definition）/
 *         SceneObject（scene-object）/ PoseSet（named-pose-set）/
 *         DrivetrainDesign（robot-drivetrain）（§4.4～§4.7）＋部件级
 *         不变量核查（工具物性 I-MDL-5"断言①②③同连杆"；传动 I-MDL-11/
 *         I-MDL-12）＋耦合矩阵数值校验与编辑流（WP-13-T18——§4.7
 *         coupling 一等字段的 R2 编辑/持久化入口与 I-MDL-11 重算复核）。
 *
 * 设计依据：
 *   - units/modeling.md §4.4（ToolDefinition，MDL-13）、§4.5（SceneObject，
 *     MDL-15）、§4.6（PoseSet，MDL-17）、§4.7（DrivetrainDesign，MDL-16/21、
 *     SEL-10）、§4.2（对象分解——五对象表的后四行）、§4.8（身份/版本/
 *     引用约束）、§4.10（I-MDL-11/I-MDL-12）、§8.1（传动与耦合模型输入
 *     ——WP-13-T18 落位：非方阵/奇异/病态经比较型诊断阻止成模，M-12）、
 *     §14.2 D-MDL-1/D-MDL-2
 *   - units/policy.md §4.3（SceneObjectRole 词表"归建模语义，policy 消费"
 *     ——词表本体在 ObjectTypes.hpp，本头只消费）
 *   - 需求 MDL-13（工具定义引用不复制）、MDL-15（场景对象）、MDL-16
 *     （动力学参数层）、MDL-17（命名位姿）、MDL-21（传动 R2）、SEL-10
 *     （选型回填）、CON-03（资源三段边界）
 *   - 任务契约 tasks/foundation/WP-13-T03.json acceptance 1（五部件对象
 *     schema——Parts.hpp 行）、acceptance 3（R1 下 coupling 配置拒绝）；
 *     tasks/foundation/WP-13-T10.json acceptance 1/3/4（§3.3 Parts.hpp
 *     T10 行"编辑流"：工具 tcpList≥1 与 T_flange_tool、场景约束、命名
 *     位姿合并/保留键/关节序——文末"部件编辑流"节）；
 *     tasks/foundation/WP-13-T18.json acceptance 1/2（coupling 编辑与
 *     持久化——applyDrivetrainCouplingEdit；R1 阻断保留——StageLocked
 *     值面＋MDL-21-COUPLING-STAGE-LOCKED 码面）
 *
 * 背景说明（为什么这四个对象独立成对象而连杆不是——§4.2 表"独立成对象
 * 的理由"列）：工具被任务/负载跨域引用（不复制几何）、场景对象被 REQ-04
 * 等跨域引用且场景变更不失效纯运动学切片、位姿集是纯参考数据（变更不
 * 触发重算）、传动是 selection 回填与 OPT StageB 的独立写对象——四者的
 * **修订节奏与失效范围**都与根对象不同，独立成对象让修订闭包按对象累积
 * （"位姿集变更不改根字节"由对象分解保证，§4.2）。全部为纯值类型，
 * 所有权与线程约束同 RobotDesign.hpp 文件头（编辑态仅 UI 线程可变）。
 */

#ifndef IRD_MODELING_PARTS_HPP
#define IRD_MODELING_PARTS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rw/math/Transform3D.hpp>  // T_flange_tool／T_tool_tcp／世界系位姿（m/rad）

#include <sdurws/ird/core/Identity.hpp>     // ObjectId
#include <sdurws/ird/core/Provenance.hpp>   // SourcedValue
#include <sdurws/ird/modeling/ObjectTypes.hpp>  // SceneObjectRole（词表所有者＝modeling）
#include <sdurws/ird/modeling/RobotDesign.hpp>  // GeometryRef/BodyData/InvariantViolation/InvariantId

namespace sdurws::ird::modeling {

// =====================================================================
// ToolDefinition（§4.4——tool-definition 对象，每工具一个；MDL-13）
// =====================================================================

/**
 * @brief 工具 TCP 条目（§4.4 tcpList 行：{key, offset, displayName}）。
 *
 * defaultTcp 经 (toolOid, tcpKey) 引用其一（KIN-14）；TCP 变更须精确失效
 * 运动学及下游（AT-05①）——失效由各评估器声明的依赖键传播，本结构只承
 * 载值。offset 单位 m/rad，T_tool_tcp＝"tcp 系相对 tool 系"。
 */
struct TcpEntry {
    std::string key;         ///< 工具内唯一键（defaultTcp.tcpKey 的引用目标）
    /// TCP 相对工具法兰系安装接口的位姿（m/rad；core.md §4.6 T_ab 约定）。
    rw::math::Transform3D<double> offset{
        rw::math::Vector3D<double>(0.0, 0.0, 0.0),
        rw::math::Rotation3D<double>(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0)};
    std::string displayName;  ///< 仅呈现（UX-02 口径——同根对象 displayName）

    bool operator==(const TcpEntry& o) const
    {
        return key == o.key && transformEquals(offset, o.offset) && displayName == o.displayName;
    }
    bool operator!=(const TcpEntry& o) const { return !(*this == o); }

    /// 位姿逐元素相等（与 RobotDesign.cpp 的 transformEqual 同口径——此处
    /// 为头内联实现供值比较用；算法单一事实仍在 RobotDesign.cpp 注释）。
    static bool transformEquals(const rw::math::Transform3D<double>& a,
                                const rw::math::Transform3D<double>& b) noexcept
    {
        for (std::size_t r = 0; r < 3; ++r) {
            for (std::size_t c = 0; c < 3; ++c) {
                if (!(a.R()(r, c) == b.R()(r, c))) { return false; }
            }
        }
        for (std::size_t i = 0; i < 3; ++i) {
            if (!(a.P()[i] == b.P()[i])) { return false; }
        }
        return true;
    }
};

/**
 * @brief 负载工况登记位（§4.4 payloadAttributes 行"登记保留，语义归
 *        requirements 负载工况；不进 Description"）。
 *
 * 只承载卡面已具名的 ratedLoad（额定负载，kg）；"..."其余字段待
 * requirements 语义冻结后按表尾追加纪律扩充——不发明未定义形状的字段。
 */
struct PayloadAttributes {
    double ratedLoad = 0.0;  ///< 额定负载，单位 kg（>0；语义冻结前的登记占位）

    bool operator==(const PayloadAttributes& o) const noexcept
    {
        return ratedLoad == o.ratedLoad;
    }
    bool operator!=(const PayloadAttributes& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 工具定义对象（§4.4 字段表；字段声明序＝表行序＝编解码字段序）。
 *
 * 每工具一个独立对象（§4.2 表理由列：任务/负载经引用使用、不复制工具
 * 几何——MDL-13）；"删除"＝根对象 toolRefs 移除引用（对象字节永久保留，
 * PA-2/§4.8）。
 */
struct ToolDefinition {
    /// 对象 schema 主版本（单一权威＝kToolDefinitionSchemaVersion，禁写字面量）。
    std::uint32_t schemaVersion = kToolDefinitionSchemaVersion;

    core::ObjectId objectId;  ///< 对象稳定身份（ARC-04——project 对象创建时分配）
    std::string localName;    ///< 同根对象 localName 口径（runtime §4.3.6）
    std::string displayName;  ///< 仅呈现（不进 Description 编译身份）

    /// 安装接口 T_flange_tool＝"tool 系相对 flange 系"（m/rad；§4.4 表行）。
    rw::math::Transform3D<double> mountInterface{
        rw::math::Vector3D<double>(0.0, 0.0, 0.0),
        rw::math::Rotation3D<double>(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0)};

    /// TCP 列表（≥1——表行约束；defaultTcp 经 tcpKey 引用其一）。
    std::vector<TcpEntry> tcpList;

    std::optional<GeometryRef> geometry;  ///< 工具几何（引用 resourceManifest，不复制）

    /// 物性组（断言①②③同连杆——§4.4 表行原文；BodyData 单一实现复用）。
    BodyData body;

    std::optional<PayloadAttributes> payloadAttributes;  ///< 登记保留（语义归 requirements）

    bool operator==(const ToolDefinition& o) const;
    bool operator!=(const ToolDefinition& o) const { return !(*this == o); }
};

// =====================================================================
// SceneObject（§4.5——scene-object 对象，每对象一个；MDL-15）
// =====================================================================

/**
 * @brief 场景对象（§4.5 字段表；字段声明序＝表行序＝编解码字段序）。
 *
 * worldPose 为**世界坐标系固连**位姿（m/rad；不得预乘安装旋转——runtime
 * §4.3.4 同口径，M-11 基座—世界单一不变量的场景侧形态）。角色词表
 * SceneObjectRole 所有者＝modeling（ObjectTypes.hpp——policy 消费 token）。
 */
struct SceneObject {
    /// 对象 schema 主版本（单一权威＝kSceneObjectSchemaVersion）。
    std::uint32_t schemaVersion = kSceneObjectSchemaVersion;

    core::ObjectId objectId;  ///< 对象稳定身份
    std::string localName;    ///< 同前口径

    /// 世界系固连位姿（m/rad；M-11——不预乘安装旋转）。
    rw::math::Transform3D<double> worldPose{
        rw::math::Vector3D<double>(0.0, 0.0, 0.0),
        rw::math::Rotation3D<double>(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0)};

    std::optional<GeometryRef> geometry;  ///< 固定帧/环境几何

    SceneObjectRole role = SceneObjectRole::EnvironmentObject;  ///< 角色（五值词表）

    /// 导入的自碰撞分组提示（仅报告用途——不改变策略，策略权威归 policy）。
    std::optional<std::string> collisionProfileHint;

    bool operator==(const SceneObject& o) const;
    bool operator!=(const SceneObject& o) const { return !(*this == o); }
};

// =====================================================================
// PoseSet（§4.6——named-pose-set 对象，每模型一份；MDL-17）
// =====================================================================

/**
 * @brief 命名位姿条目（§4.6：{key, jointConfiguration, note}）。
 *
 * jointConfiguration 与关节序一一对应（rad/m——随关节类型），长度一致
 * 性核查需要根对象关节表上下文，归消费方（ui 会话层/MDL-20 导出）与
 * 就绪校验；homeConfiguration/zeroConfiguration 为两个保留键（编辑器
 * "复位 Home/Zero"会话命令的目标参考——UX-13/KIN-06，复位只改 ui 会话
 * 姿态不产生修订）。
 */
struct PoseSetEntry {
    std::string key;                  ///< 位姿键（集合内唯一；保留键见类注）
    std::vector<double> jointConfiguration;  ///< 关节角序列，rad（移动关节 m），与关节序对应
    std::string note;                 ///< 备注（纯参考）

    bool operator==(const PoseSetEntry& o) const
    {
        return key == o.key && jointConfiguration == o.jointConfiguration && note == o.note;
    }
    bool operator!=(const PoseSetEntry& o) const { return !(*this == o); }
};

/**
 * @brief 命名位姿集对象（§4.6；字段声明序＝编解码字段序）。
 *
 * ★ 不进 Description、不进任何评估器依赖键（§4.6 原文）：位姿仅作会话
 * 与检查参考（MDL-17），其修订不触发重算——"不改变权威模型"由对象分解
 * 保证（独立对象、根对象只持引用）。消费方＝ui 会话层与 MDL-20 导出。
 * 卡面 §4.6 未登记 localName 字段——位姿集不经名称映射（不进 runtime），
 * 故无 localName（如实从卡，不补字段）。
 */
struct PoseSet {
    /// 对象 schema 主版本（单一权威＝kNamedPoseSetSchemaVersion）。
    std::uint32_t schemaVersion = kNamedPoseSetSchemaVersion;

    core::ObjectId objectId;  ///< 对象稳定身份

    /// 位姿条目（key 唯一；编解码按 key 字典序规范化——§4.8 集合定序在
    /// ObjectId 语义之外的字符串键集合按同一字典序纪律执行）。
    std::vector<PoseSetEntry> entries;

    bool operator==(const PoseSet& o) const;
    bool operator!=(const PoseSet& o) const { return !(*this == o); }
};

// =====================================================================
// DrivetrainDesign（§4.7——robot-drivetrain 对象，每模型一份；MDL-16/21）
// =====================================================================

/**
 * @brief 传动耦合设计（§4.7 coupling 行；MDL-21，R2）。
 *
 * C 为常矩阵（行×列＝适用关节窗口 jointRange [i,j]，闭区间——行主序
 * 扁平存储）；conditionNumber 为矩阵条件数（无量纲，正实数——≤1×10⁸
 * 上限见 P-RT-7 设计默认，值来源登记 P-MDL-7）。R1 下本结构**禁止配置**
 * （存在即阻断——I-MDL-12）。
 *
 * WP-13-T18 增注（R2 数值校验语义）：conditionNumber 字段是建模侧的
 * **申报/缓存参考**——合法性判定一律以重算值为准（申报值失真不构成绕过
 * 病态阻止的通道；重算复核见 checkCouplingMatrix——I-MDL-11"可逆、条件
 * 数 ≤1×10⁸"以 C 的奇异值分解实测为判据，runtime 编译校验同口径重算兜
 * 底，两处判定互为防线纵深）。
 */
struct CouplingDesign {
    std::uint32_t rows = 0;      ///< 矩阵行数（＝适用关节窗口高）
    std::uint32_t cols = 0;      ///< 矩阵列数（R2 合法态＝方阵——I-MDL-11）
    std::vector<double> c;       ///< C 矩阵元素（行主序，无量纲；rows*cols 个）
    std::uint32_t jointRangeFirst = 0;  ///< 适用关节窗口起点 i（闭区间，0 起关节序）
    std::uint32_t jointRangeLast = 0;   ///< 适用关节窗口终点 j（闭区间）
    double conditionNumber = 1.0;       ///< 条件数（无量纲；申报值——判定以重算为准，见类型注）

    bool operator==(const CouplingDesign& o) const
    {
        return rows == o.rows && cols == o.cols && c == o.c
            && jointRangeFirst == o.jointRangeFirst
            && jointRangeLast == o.jointRangeLast
            && conditionNumber == o.conditionNumber;
    }
    bool operator!=(const CouplingDesign& o) const { return !(*this == o); }
};

/**
 * @brief 逐关节摩擦参数（§4.7 frictionPerJoint 行；进入 Description
 *        friction——runtime §4.2）。
 *
 * 未填写→NotProvided→DataInsufficient 降级（DYN-06/M-8 闭环）。单位随
 * 关节类型：转动 N·m·s/rad（viscous）与 N·m（coulomb/bias）；移动
 * N·s/m（viscous）与 N（coulomb/bias）。
 */
struct FrictionEntry {
    core::SourcedValue<double> viscous;  ///< 黏滞系数 fv（N·m·s/rad 或 N·s/m）
    core::SourcedValue<double> coulomb;  ///< 库仑摩擦 fc（N·m 或 N）
    core::SourcedValue<double> bias;     ///< 偏置力矩（N·m 或 N）

    bool operator==(const FrictionEntry& o) const
    {
        return viscous == o.viscous && coulomb == o.coulomb && bias == o.bias;
    }
    bool operator!=(const FrictionEntry& o) const { return !(*this == o); }
};

/**
 * @brief 逐关节力矩限值（§4.7 torqueLimitsPerJoint 行；不进 CanonicalModel
 *        ——消费方 SEL/DYN，进入驱动工作点评估的输入）。
 *
 * 单位随关节类型：转动 N·m；移动 N。
 */
struct TorqueLimitEntry {
    core::SourcedValue<double> rated;  ///< 额定值（N·m 或 N）
    core::SourcedValue<double> peak;   ///< 峰值（N·m 或 N）

    bool operator==(const TorqueLimitEntry& o) const
    {
        return rated == o.rated && peak == o.peak;
    }
    bool operator!=(const TorqueLimitEntry& o) const { return !(*this == o); }
};

/**
 * @brief 选型目录回填登记（§4.7 catalogBackfill 行；SEL-10 回填字段——
 *        阶段 C 由 selection 命令写入，schema 本卡登记）。
 */
struct CatalogBackfill {
    std::string catalogVersion;  ///< 目录版本（词表归 selection——本单元只承载）
    std::string motorKey;        ///< 电机目录键
    std::string reducerKey;      ///< 减速器目录键
    std::string mounting;        ///< 安装形态（目录词）

    bool operator==(const CatalogBackfill& o) const
    {
        return catalogVersion == o.catalogVersion && motorKey == o.motorKey
            && reducerKey == o.reducerKey && mounting == o.mounting;
    }
    bool operator!=(const CatalogBackfill& o) const { return !(*this == o); }
};

/**
 * @brief 传动耦合阶段开关（I-MDL-11/I-MDL-12 的核查入参）。
 *
 * R1＝当前程序阶段：coupling 禁止配置（MDL-12/21 R1 口径）；R2/阶段 D
 * 经 MDL-21 启用线性耦合（§8.1——启用路径与矩阵复核随 T18/R2 落位）。
 */
enum class CouplingStage {
    R1Locked,    ///< R1：coupling 配置即违例（I-MDL-12）
    R2Enabled,   ///< R2：coupling 允许，须过 I-MDL-11 矩阵核查
};

/**
 * @brief 耦合阶段稳定 token（"R1-locked"/"R2-enabled"）。纯函数；确定性。
 */
std::string_view couplingStageToken(CouplingStage stage) noexcept;

/**
 * @brief 传动设计对象（§4.7 字段表；字段声明序＝表行序＝编解码字段序）。
 *
 * 每模型一份独立对象（§4.2 表理由列：selection 回填与 OPT StageB 传动比
 * 编辑的独立写对象；力矩/摩擦仅动力学消费——evidence.md §5.3 失效矩阵行）。
 * 卡面 §4.7 未登记 localName（传动对象不经名称映射）——如实从卡。
 */
struct DrivetrainDesign {
    /// 对象 schema 主版本（单一权威＝kRobotDrivetrainSchemaVersion）。
    std::uint32_t schemaVersion = kRobotDrivetrainSchemaVersion;

    core::ObjectId objectId;  ///< 对象稳定身份

    /// 逐可动关节传动比（无量纲，>0 且有限——I-MDL-11；与关节序一一
    /// 对应，**有序不排序**——下标即关节序）。R1 可编辑（OPT StageB 连续
    /// 变量，V12-02）；回填来源 CatalogBackfill（SEL-10）。
    std::vector<core::SourcedValue<double>> ratioPerJoint;

    std::optional<CouplingDesign> coupling;  ///< R1 禁止配置（I-MDL-12）/R2 受 I-MDL-11 约束

    std::vector<FrictionEntry> frictionPerJoint;        ///< 逐关节摩擦（与关节序对应）
    std::vector<TorqueLimitEntry> torqueLimitsPerJoint; ///< 逐关节力矩限值（与关节序对应）

    std::optional<CatalogBackfill> catalogBackfill;  ///< SEL-10 回填（阶段 C 由 selection 写入）

    bool operator==(const DrivetrainDesign& o) const;
    bool operator!=(const DrivetrainDesign& o) const { return !(*this == o); }
};

// =====================================================================
// 部件级不变量核查（§4.4/§4.7 指派给部件对象的不变量半段）
// =====================================================================

/**
 * @brief 工具物性不变量核查（§4.4"断言①②③同连杆"——与连杆共用
 *        BodyData 单一实现，编号同为 I-MDL-5）。
 *
 * @param tool [in] 待核查工具对象（只读）
 * @return 违例清单（空＝通过；subject 前缀固定 "tool"，确定序）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<InvariantViolation> checkInvariants(const ToolDefinition& tool);

/**
 * @brief 传动不变量核查（I-MDL-11/I-MDL-12——§4.10 两条目均落在传动
 *        对象上；根对象只有 drivetrainRef 引用位）。
 *
 * 核查面（按阶段）：
 *   - I-MDL-12：stage==R1Locked 且 coupling 已配置→违例（§4.7"存在即
 *     阻断"——稳定诊断码 MDL-21-COUPLING-STAGE-LOCKED 随 T18/R2 注册，
 *     §9.5 表尾追加纪律，本函数只产出值面违例）；
 *   - I-MDL-11：ratioPerJoint 逐项有限>0；R2 下 coupling 须方阵（rows==
 *     cols==c.size() 开方一致）且 conditionNumber 为正实数且 ≤1×10⁸
 *     （P-RT-7 设计默认——阈值单源引用，modeling 不私设第二常量，
 *     P-MDL-7；C 可逆性与条件数的**重算复核**需要 SVD，随 T18/R2 经
 *     runtime 单源实现，本函数只核查已登记字段的自洽性）。
 *
 * @param drivetrain [in] 待核查传动对象（只读）
 * @param stage      [in] 当前耦合阶段（R1 锁定/R2 启用）
 * @return 违例清单（空＝通过；确定序）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<InvariantViolation> checkInvariants(const DrivetrainDesign& drivetrain,
                                                CouplingStage stage);

// =====================================================================
// 耦合矩阵数值校验（I-MDL-11 重算复核——WP-13-T18；§8.1"病态/非常矩阵"
// 行：非方阵、奇异、条件数超限→编辑边界比较型诊断＋应用阻断，M-12 不降
// 级不静默。就绪校验 L9、命令 prepare 断言与编辑原语三处共用同一判定
// ——NFR-MNT-04 单一判定面）
// =====================================================================

/**
 * @brief 耦合矩阵数值校验结果（值事实面——比较型诊断的三要素素材由
 *        violationKind 与 recomputedConditionNumber 承载，诊断记录组装
 *        归产码点：AssertionSuite::assertDrivetrainCoupling）。
 *
 * 线程安全：纯值类型。
 */
struct CouplingMatrixCheck {
    /// 违例种类稳定 token（""＝通过；其余值见 checkCouplingMatrix 逐档注）。
    std::string violationKind;
    /// 重算条件数 κ=σmax/σmin（无量纲）。通过时为实测 κ（≥1）；数值
    /// 奇异（σmin≤σmax×1×10⁻¹²）时为实测 κ（≥1×10¹²，有限——判档比值
    /// 层面）；结构非法（非方阵/非有限元素/窗口失配）时无 κ 可言，承载
    /// 0（调用方按 violationKind 选择比较要素，不用本值伪造 κ）。
    double recomputedConditionNumber = 0.0;
    /// 奇异分档比值 σmin/σmax（无量纲，(0,1]；仅奇异档有效——比较型
    /// 诊断 actual 侧的有限承载值，ERR-01：κ 下溢为无穷时不伪造有限 κ）。
    double singularSigmaRatio = 0.0;
    /// 中文定位细节（元素下标/维度/窗口计数等——诊断 context/cause 素材）。
    std::string detail;

    bool ok() const noexcept { return violationKind.empty(); }  ///< 通过判定
};

/**
 * @brief 耦合矩阵数值校验（I-MDL-11 重算复核；§8.1；MDL-21/M-12）。
 *
 * 校验序（固定——确定性；先结构后数值，短路返回首个违例）：
 *   ① "not-square"——非方阵：rows≠cols，或扁平存储容量 rows×cols 与
 *      元素数不一致（行主序承载自洽面）；
 *   ② "element-not-finite"——任一元素 NaN/±Inf（I-MDL-3：非法值不静默
 *      置 0；比较要素 actual=非有限元素计数、expected=0）；
 *   ③ "window-mismatch"——适用关节窗口计数（j−i+1）≠ 方阵阶 n（MDL-21
 *      "方阵 n×n〔适用关节窗口与对应电机轴同序〕"的窗口一致性半段；
 *      比较要素 actual=窗口计数、expected=阶数）；
 *   ④ "window-out-of-range"——窗口超出根关节表（first+n > jointCount；
 *      关闭：jointCount 为根对象关节表长度，编辑流传入——窗口须指向
 *      存在的关节；比较要素同 ③ 形态）；
 *   ⑤ "singular"——σmin ≤ σmax×1×10⁻¹²（数值奇异分界——κ≥1×10¹² 档，
 *      det≈0 不可逆；比较要素 actual=σmin/σmax 比值、expected=1×10⁻¹²
 *      之上——κ 在该档可下溢为无穷，以有限比值承载，ERR-01 不伪造）；
 *   ⑥ "ill-conditioned"——重算 κ > 1×10⁸（P-RT-7 设计默认——阈值单点
 *      kCouplingConditionNumberLimit，P-MDL-7；比较要素 actual=重算 κ、
 *      expected=1×10⁸、单位 "1" 无量纲）；
 *   ⑦ 通过——ok()，recomputedConditionNumber＝实测 κ。
 *
 * 申报值（coupling.conditionNumber）不参与任何判定（见 CouplingDesign
 * 类型注——申报失真不构成绕过通道）。
 *
 * @param coupling   [in] 待校验耦合设计（只读）
 * @param jointCount [in] 根对象关节表长度（窗口越界判据——窗口起点＋阶数
 *                    不得超过该值；无根上下文传入 0 时任何非空窗口均判
 *                    越界——窗口必须指向存在的关节）
 * @return 校验结果（见结构注；kind 空串＝通过）
 *
 * 纯函数；线程安全；确定性（SVD 固定扫描序/收敛阈/轮数上限——同输入
 * 逐位同输出，NFR-COR-02）。
 */
CouplingMatrixCheck checkCouplingMatrix(const CouplingDesign& coupling,
                                        std::size_t jointCount);

// =====================================================================
// 部件编辑流（§3.3 Parts.hpp T10 行——WP-13-T10；构造/编辑边界的值面
// 校验与纯函数合并流。处置原则同 §4.10 尾段：编辑边界 fail-fast＝调用
// 方错误，经值面返回不抛异常——§9.4.1 EditOutcome 轨道同构）
// =====================================================================

// ---- 命名位姿保留键（§4.6：homeConfiguration/zeroConfiguration 两个
// 保留键作为编辑器"复位 Home/Zero"会话命令的目标参考——UX-13/KIN-06；
// 复位只改 ui 会话姿态不产生修订，故保留键的**写入**不属于用户命名位姿
// 管理面——REQUIREMENTS MDL-17"除 Home/Zero 外保存、命名与恢复姿态"）----

/// @brief Home 复位参考键（§4.6 字面；位姿集条目 key 作用域）。
inline constexpr std::string_view kHomeConfigurationPoseKey = "homeConfiguration";

/// @brief Zero 复位参考键（§4.6 字面；位姿集条目 key 作用域）。
inline constexpr std::string_view kZeroConfigurationPoseKey = "zeroConfiguration";

/**
 * @brief 保留键判定（§4.6 两保留键的字面集合成员测试）。
 *
 * @param key [in] 位姿条目键（只读）
 * @return true＝保留键（用户命名位姿编辑面不得携带——mergeNamedPoseEntries
 *         拒绝）；false＝普通键
 *
 * 纯函数；线程安全；确定性。
 */
bool isReservedPoseKey(std::string_view key) noexcept;

/**
 * @brief 命名位姿编辑流的错误码（编辑流局部错误轨道——EstimateErrorCode/
 *        DhErrorCode 同款先例：各接口局部载体不并入 ModelingErrorCode 域表）。
 */
enum class PoseEditErrorCode {
    Ok,                  ///< 成功（merged 有效）
    ReservedKeyInEdit,   ///< 编辑条目携带保留键（MDL-17"除 Home/Zero 外"——越出面拒绝）
    EmptyKey,            ///< 条目键为空串（引用锚不可为空——codec 键作用域）
    DuplicateKey,        ///< 编辑条目键重复（I-MDL-2 身份/键唯一性的条目面）
    JointOrderMismatch,  ///< 条目 jointConfiguration 长度≠根关节表长度（§4.6"与关节序一一对应"）
    /// "key-not-found"——剔除面键不在用户条目集（UI-T60 表尾追加值；
    /// 既有五值次序不动——词表表尾追加纪律）
    KeyNotFound,
};

/**
 * @brief 命名位姿合并结果（mergeNamedPoseEntries 的两态产出）。
 *
 * code==Ok 时 merged 有效＝基线保留键条目 ∪ 用户条目（键字典序——codec
 * canonical 序）；其余 code 时 merged 为 nullopt、subject＝出错条目键。
 * 纯值类型。
 */
struct PoseEditOutcome {
    PoseEditErrorCode code = PoseEditErrorCode::Ok;  ///< 结果码（见枚举注）
    std::string subject;                             ///< 定位（出错条目键；Ok 时为空串）
    std::optional<PoseSet> merged;                   ///< 合并产物（仅 Ok 时有值）
};

/**
 * @brief 位姿集编辑拒绝值（UI-T60——applyPoseSetEntryUpsertEdit/
 *        applyPoseSetEntryRemoveEdit 的返回错误面；与其余编辑流错误值
 *        同构：码＋定位细节。可默认构造供值语义容器使用）。
 */
struct PoseEditError {
    PoseEditErrorCode code = PoseEditErrorCode::EmptyKey;  ///< 稳定错误码
    std::string detail;  ///< 定位细节（UTF-8；subject 键/原因）

    bool operator==(const PoseEditError& o) const noexcept
    {
        return code == o.code && detail == o.detail;
    }
    bool operator!=(const PoseEditError& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 命名位姿合并编辑流（§4.6/D-MDL-3/MDL-17——apply-named-poses
 *        处理器与插件编辑器共用的唯一合并实现）。
 *
 * 语义（三段，全部确定性）：
 *   ① 编辑条目校验（任一失败即整体拒绝，不产出半成品——NFR-COR-03）：
 *      键非空（EmptyKey）；不含保留键（ReservedKeyInEdit——保留键的写入
 *      不属用户命名位姿管理面，MDL-17 字面；其**读取**归 ui 会话复位）；
 *      键在编辑集内唯一（DuplicateKey）；jointConfiguration 长度==根关节
 *      表长度（JointOrderMismatch——§4.6"与关节序一一对应"；对照 §4.7
 *      ratioPerJoint 行明写"逐可动关节"，本处无"可动"限定词＝关节表全序，
 *      含 Fixed 表序位——取卡面字面）；
 *   ② 保留键保留（V-27 建模侧）：基线位姿集中的保留键条目**原样带入**
 *      合并产物——用户位姿编辑永不破坏 Home/Zero 参考键（复位走会话命令
 *      零修订，KIN-06）；基线无位姿集＝尚无保留键，产物只含用户条目；
 *   ③ 规范化输出：合并条目按 key 字典序排列（codec canonical 序——
 *      putPoseSet 排序域），用户条目全集**替换**基线非保留条目（编辑提交
 *      ＝完整用户位姿清单，非增量 upsert——与载荷携带完整对象字节的
 *      replace 语义一致）。
 *
 * @param baseline       [in] 基线位姿集（nullopt＝尚无位姿集——首建）
 * @param userEntries    [in] 用户命名位姿全集（编辑提交面；按值接收——
 *                       合并时被移动进产物）
 * @param rootJointCount [in] 根对象关节表长度（关节序一一对应的比对基准）
 * @return 合并结果（code==Ok 时 merged 有效；错误时 subject＝首个出错
 *         条目键——确定性：按编辑序首个违例）
 *
 * 纯函数；线程安全；确定性（NFR-COR-02）。
 */
PoseEditOutcome mergeNamedPoseEntries(const std::optional<PoseSet>& baseline,
                                      std::vector<PoseSetEntry> userEntries,
                                      std::size_t rootJointCount);

// =====================================================================
// 部件位姿编辑流（UI-T55——F-497 兑现③：工具安装接口/场景世界位姿）
// =====================================================================

/**
 * @brief 工具/场景位姿编辑值（六标量载荷——旋转组合归域内核，与
 *        JointOriginEditValue 同一划界：UI 交标量、域出矩阵）。
 *
 * 字段语义（ZYX 约定 R＝Rz(yaw)·Ry(pitch)·Rx(roll)——与 src/RpyMath.hpp
 * 正解/呈现反解同一约定）：
 *   - 工具安装接口面：x/y/z m＋roll/pitch/yaw rad，**法兰坐标系**下表示
 *     （T_flange_tool——§4.4 表行）；
 *   - 场景世界位姿面：x/y/z m＋roll/pitch/yaw rad，**世界坐标系**固连
 *     （M-11——不预乘安装旋转）。
 */
struct PartPoseEditValue {
    double x = 0.0;      ///< 平移 X 分量，单位 m（参考系随消费面——见类型注）
    double y = 0.0;      ///< 平移 Y 分量，单位 m（参考系随消费面——见类型注）
    double z = 0.0;      ///< 平移 Z 分量，单位 m（参考系随消费面——见类型注）
    double roll = 0.0;   ///< 姿态 roll 角（绕 X），单位 rad（ZYX 约定）
    double pitch = 0.0;  ///< 姿态 pitch 角（绕 Y），单位 rad（ZYX 约定）
    double yaw = 0.0;    ///< 姿态 yaw 角（绕 Z），单位 rad（ZYX 约定）

    bool operator==(const PartPoseEditValue& o) const noexcept
    {
        return x == o.x && y == o.y && z == o.z && roll == o.roll
               && pitch == o.pitch && yaw == o.yaw;
    }
    bool operator!=(const PartPoseEditValue& o) const noexcept
    {
        return !(*this == o);
    }
};

/**
 * @brief 部件位姿编辑局部错误码（局部载体先例同款——JointEditErrorCode
 *        同款"接口局部错误枚举，不进域级错误轨道"）。
 */
enum class PartPoseEditErrorCode {
    /// "value-not-finite"——六分量含 NaN/Inf（I-MDL-3：非法值不静默置 0）
    ValueNotFinite,
};

/// @brief 局部错误码稳定 token（"value-not-finite"）。纯函数；确定性。
std::string_view partPoseEditErrorCodeToken(PartPoseEditErrorCode code) noexcept;

/**
 * @brief 部件位姿编辑拒绝值（局部错误面：码＋定位细节——呈现文案归
 *        UI 层按码映射，同 JointEditError 注）。
 */
struct PartPoseEditError {
    PartPoseEditErrorCode code = PartPoseEditErrorCode::ValueNotFinite;  ///< 稳定错误码
    std::string detail;  ///< 定位细节（UTF-8；subject/原因）

    bool operator==(const PartPoseEditError& o) const noexcept
    {
        return code == o.code && detail == o.detail;
    }
    bool operator!=(const PartPoseEditError& o) const noexcept { return !(*this == o); }
};

// ModelingWorkingSet 前置声明（本头被 Template.hpp 包含——R-2 禁反向
// include；仅引用语义，完整型在 .cpp 侧消费）。
struct ModelingWorkingSet;

/**
 * @brief 应用一次工具安装接口编辑（§4.4——UI-T55 接线面）。
 *
 * 规则：①toolIndex 越界＝调用方契约违约→抛 std::invalid_argument；
 * ②六分量有限性→ValueNotFinite（I-MDL-3）；③提交 mountInterface 直写
 * （Transform3D 值语义——非 SourcedValue，§4.4 表行）＋追加一条变更摘要
 * 记录（append-only）。拒绝时工作集字节不变（域内强保证）。
 *
 * @param ws        [in,out] 目标工作集
 * @param toolIndex [in] 工具下标（toolObjects 序，0 起；越界＝fail-fast）
 * @param value     [in] 编辑值（法兰系六标量——见 PartPoseEditValue 注）
 * @return nullopt＝接受；非空＝拒绝（工作集不变）
 * @throws std::invalid_argument 越界下标（调用方契约违约）
 *
 * 纯函数（除工作集写入）；确定性；仅 UI 线程（编辑态纪律）。
 */
std::optional<PartPoseEditError> applyToolMountEdit(ModelingWorkingSet& ws,
                                                    std::size_t toolIndex,
                                                    const PartPoseEditValue& value);

/**
 * @brief 应用一次场景世界位姿编辑（§4.5——UI-T55 接线面）。
 *
 * 规则同 applyToolMountEdit（sceneIndex 越界 fail-fast／六分量有限性／
 * worldPose 直写＋一条变更记录——拒绝时工作集不变）。参考系＝世界坐标系
 * 固连（M-11——不预乘安装旋转）。
 */
std::optional<PartPoseEditError> applyScenePoseEdit(ModelingWorkingSet& ws,
                                                    std::size_t sceneIndex,
                                                    const PartPoseEditValue& value);

// =====================================================================
// TCP 列表结构化编辑流（UI-T57——F-497 兑现④：MDL-13 不变量流）
// =====================================================================

/**
 * @brief TCP 列表编辑局部错误码（局部载体先例同款）。
 */
enum class TcpEditErrorCode {
    /// "key-empty"——新增/改名的键为空（I-MDL-13：键是 defaultTcp 引用锚）
    KeyEmpty,
    /// "key-duplicate"——新增键与既有键重复（I-MDL-13：集合内唯一）
    KeyDuplicate,
    /// "key-not-found"——删除/改偏移的目标键不在 tcpList 中
    KeyNotFound,
    /// "last-tcp-protected"——删除最后一条 TCP（I-MDL-13：tcpList ≥1）
    LastTcpProtected,
    /// "default-tcp-referenced"——目标键被根 defaultTcp 引用（I-MDL-9
    /// 引用保护——RemoveObjectRefEdit 同款语义）
    DefaultTcpReferenced,
    /// "value-not-finite"——offset 含 NaN/Inf（I-MDL-3）
    ValueNotFinite,
};

/// @brief 局部错误码稳定 token。纯函数；确定性。
std::string_view tcpEditErrorCodeToken(TcpEditErrorCode code) noexcept;

/**
 * @brief TCP 列表编辑拒绝值（局部错误面：码＋定位细节）。
 */
struct TcpEditError {
    TcpEditErrorCode code = TcpEditErrorCode::ValueNotFinite;  ///< 稳定错误码
    std::string detail;  ///< 定位细节（UTF-8；subject/键/原因）

    bool operator==(const TcpEditError& o) const noexcept
    {
        return code == o.code && detail == o.detail;
    }
    bool operator!=(const TcpEditError& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 追加一条 TCP 条目（§4.4 tcpList 行——UI-T57）。
 *
 * 规则：①toolIndex 越界 fail-fast；②key 非空（KeyEmpty）且不与既有键
 * 重复（KeyDuplicate）；③offset 六分量有限性（ValueNotFinite）；④提交
 * （displayName 原样；offset 经 ZYX 正解组合）＋一条变更记录。
 *
 * @throws std::invalid_argument 越界下标（调用方契约违约）
 */
std::optional<TcpEditError> applyTcpAddEdit(ModelingWorkingSet& ws,
                                            std::size_t toolIndex,
                                            const std::string& key,
                                            const std::string& displayName,
                                            const PartPoseEditValue& offset);

/**
 * @brief 删除一条 TCP 条目（§4.4 tcpList 行——UI-T57）。
 *
 * 规则（次序随代码——F-516④：引用保护先于最后一条保护，指引更明确）：
 * ①越界 fail-fast；②键须存在（KeyNotFound）；③根 defaultTcp 引用该键时
 * 拒绝（DefaultTcpReferenced——I-MDL-9 引用保护，同 RemoveObjectRefEdit
 * 语义）；④tcpList 仅剩一条时拒绝（LastTcpProtected——I-MDL-13 ≥1）；
 * ⑤提交＋一条变更记录。
 */
std::optional<TcpEditError> applyTcpRemoveEdit(ModelingWorkingSet& ws,
                                               std::size_t toolIndex,
                                               const std::string& tcpKey);

/**
 * @brief 编辑一条 TCP 的安装偏移位姿（§4.4 offset 行——UI-T57）。
 *
 * 规则：①越界 fail-fast；②键须存在（KeyNotFound）；③六分量有限性；
 * ④offset 直写＋一条变更记录。参考系＝"tcp 系相对 tool 系安装接口"
 * （core.md §4.6 T_ab 约定）。
 */
std::optional<TcpEditError> applyTcpOffsetEdit(ModelingWorkingSet& ws,
                                               std::size_t toolIndex,
                                               const std::string& tcpKey,
                                               const PartPoseEditValue& offset);

/**
 * @brief 将根 defaultTcp 切换为指定工具的指定 TCP（§4.3 defaultTcp 行——
 *        UI-T57；根对象写入——与 SetBasePlacementEdit 同为根字段编辑，
 *        域内核落位于本文件与其消费面同址）。
 *
 * 规则：①越界 fail-fast；②键须存在（KeyNotFound）；③根 defaultTcp 以
 * (tool.objectId, key) 整体写入（UserProvided 语义——值模型无来源标记的
 * 引用字段，直写）＋一条变更记录。
 */
std::optional<TcpEditError> applyDefaultTcpSwitchEdit(ModelingWorkingSet& ws,
                                                      std::size_t toolIndex,
                                                      const std::string& tcpKey);

/**
 * @brief 编辑一条 TCP 的显示名（§4.4 tcpList displayName 行——UI-T58；
 *        UI-T57 卡"诚实边界"的顺延项）。
 *
 * 规则：①toolIndex 越界 fail-fast；②键须存在（KeyNotFound——编辑目标
 * 锚定既有条目，不接受按下标定位）；③displayName 直写＋一条变更记录。
 *
 * displayName 是**仅呈现字段**（UX-02 口径——同根对象 displayName；
 * §4.8：displayName 改名产生新修订但 Description 不变，编译缓存可复用），
 * 故本原语不做内容校验：空串接受（呈现侧回落按 TCP 键呈现）、无长度/
 * 字符集约束——MDL-13 不变量只约束键与列表长度，不约束呈现名。
 *
 * @throws std::invalid_argument 越界下标（调用方契约违约）
 */
std::optional<TcpEditError> applyTcpDisplayNameEdit(ModelingWorkingSet& ws,
                                                    std::size_t toolIndex,
                                                    const std::string& tcpKey,
                                                    const std::string& displayName);

// =====================================================================
// 位姿集/传动编辑流（UI-T60——F-497 余项收尾：§4.6/§4.7 对象的编辑页
// 承载面；位姿集复用 mergeNamedPoseEntries 单一合并实现——NFR-MNT-04）
// =====================================================================

/**
 * @brief 位姿集编辑错误码稳定 token。纯函数；确定性。
 *
 * （UI-T60 表尾追加 KeyNotFound——剔除面需要"键不存在"的显式拒绝面；
 * 既有五值次序不动——词表表尾追加纪律。）
 */
std::string_view poseEditErrorCodeToken(PoseEditErrorCode code) noexcept;

/**
 * @brief 新增/覆盖一条用户命名位姿（§4.6——UI-T60 编辑页承载）。
 *
 * 规则：①条目键非空（EmptyKey）且非保留键（ReservedKeyInEdit——
 * homeConfiguration/zeroConfiguration 的写入不属用户命名位姿管理面，
 * MDL-17 字面）；②经 mergeNamedPoseEntries 单一合并实现校验＋合并
 * （条目键与用户集内既有键相同＝覆盖语义——编辑提交＝完整用户清单的
 * 单条目增量形态；jointConfiguration 长度≠根关节表长度→
 * JointOrderMismatch）；③位姿集对象缺席→以草稿确定性句柄创建
 * （deriveDraftObjectId——§5.2 临时句柄纪律，提交时回填正式身份）＋
 * 根 poseSetRef 写入；④合并产物写入工作集＋恰一条变更记录。
 *
 * @throws 无（全部经返回值错误面——调用方输入属可恢复错误轨）
 */
std::optional<PoseEditError> applyPoseSetEntryUpsertEdit(ModelingWorkingSet& ws,
                                                         const PoseSetEntry& entry);

/**
 * @brief 删除一条用户命名位姿（§4.6——UI-T60 编辑页承载）。
 *
 * 规则：①保留键拒绝（ReservedKeyInEdit——保留键条目原样保留，
 * V-27 建模侧）；②键须存在于用户条目集（KeyNotFound——UI-T60 表尾
 * 追加值）；③经 mergeNamedPoseEntries 合并（用户集剔除该键）＋写入；
 * ④恰一条变更记录。删空用户条目集＝位姿集仅剩保留键条目（合法态）。
 *
 * @throws 无（返回值错误面）
 */
std::optional<PoseEditError> applyPoseSetEntryRemoveEdit(ModelingWorkingSet& ws,
                                                         const std::string& key);

/**
 * @brief 传动编辑错误码（局部错误轨道——PoseEditErrorCode 同款先例）。
 */
enum class DrivetrainEditErrorCode {
    /// "value-not-finite"——分量含 NaN/Inf（I-MDL-3）
    ValueNotFinite,
    /// "ratio-not-positive"——传动比 ≤0（I-MDL-11：有限且 >0）
    RatioNotPositive,
    /// "key-not-found"——键/下标定位的对象缺失（UI-T60 剔除面预留）
    KeyNotFound,
};

/// @brief 传动编辑错误码稳定 token。纯函数；确定性。
std::string_view drivetrainEditErrorCodeToken(DrivetrainEditErrorCode code) noexcept;

/**
 * @brief 传动编辑拒绝值（局部错误面：码＋定位细节）。
 */
struct DrivetrainEditError {
    DrivetrainEditErrorCode code = DrivetrainEditErrorCode::ValueNotFinite;  ///< 稳定错误码
    std::string detail;  ///< 定位细节（UTF-8）

    bool operator==(const DrivetrainEditError& o) const noexcept
    {
        return code == o.code && detail == o.detail;
    }
    bool operator!=(const DrivetrainEditError& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 传动编辑会话的公共前置（UI-T60——三编辑原语共用；单元内实现
 *        细节：传动对象缺席→草稿确定性句柄创建＋根 drivetrainRef 写入
 *        ＋三向量按根关节表长度补齐 NotProvided〔§4.7"与关节序一一对应"
 *        ——值模型下标即关节序；结构编辑后由本前置对齐〕）。
 *
 * @throws std::invalid_argument jointIndex 越界（调用方契约违约）
 */
std::optional<DrivetrainEditError> ensureDrivetrainEditTarget(ModelingWorkingSet& ws,
                                                              std::size_t jointIndex);

/**
 * @brief 编辑一关节的传动比（§4.7 ratioPerJoint 行——UI-T60；R1 可编辑，
 *        OPT StageB 连续变量 V12-02）。
 *
 * 规则：①ensureDrivetrainEditTarget（缺席创建/向量对齐/越界 fail-fast）；
 * ②有限性（ValueNotFinite，I-MDL-3）＋正值（RatioNotPositive，I-MDL-11）；
 * ③SourcedValue UserProvided 写入（MDL-05 显式权威一等值）＋恰一条变更
 * 记录。无量纲（SI 系数 1）。
 */
std::optional<DrivetrainEditError> applyDrivetrainRatioEdit(ModelingWorkingSet& ws,
                                                            std::size_t jointIndex,
                                                            double ratio);

/**
 * @brief 编辑一关节的摩擦三元（§4.7 frictionPerJoint 行——UI-T60）。
 *
 * 三分量逐项有限性（ValueNotFinite）；写入单位随关节类型（转动
 * N·m·s/rad＋N·m；移动 N·s/m＋N——SI 真值直写，量纲词表缺席面〔fv〕
 * 的换算归呈现层不归本原语）；SourcedValue UserProvided＋恰一条记录。
 */
std::optional<DrivetrainEditError> applyDrivetrainFrictionEdit(ModelingWorkingSet& ws,
                                                               std::size_t jointIndex,
                                                               double viscous,
                                                               double coulomb,
                                                               double bias);

/**
 * @brief 编辑一关节的力矩限值对（§4.7 torqueLimitsPerJoint 行——UI-T60；
 *        不进 CanonicalModel——消费方 SEL/DYN）。
 *
 * 两分量逐项有限性；SourcedValue UserProvided＋恰一条变更记录。
 */
std::optional<DrivetrainEditError> applyDrivetrainTorqueLimitEdit(ModelingWorkingSet& ws,
                                                                  std::size_t jointIndex,
                                                                  double rated,
                                                                  double peak);

// =====================================================================
// 耦合矩阵编辑流（WP-13-T18——§4.7 coupling 行"一等字段"的编辑边界；
// MDL-21/R2：C 编辑与持久化；R1 能力位下配置即拒绝——I-MDL-12）
// =====================================================================

/**
 * @brief 耦合矩阵编辑错误码（局部错误轨道——DrivetrainEditErrorCode
 *        同款先例；与 CouplingMatrixCheck 的 violationKind 一一对应，
 *        另加阶段锁——编辑边界的值面）。
 */
enum class CouplingEditErrorCode {
    /// "stage-locked"——R1 能力位下配置 coupling（I-MDL-12 存在即阻断；
    /// 稳定诊断码 MDL-21-COUPLING-STAGE-LOCKED 在命令 prepare 断言面产
    /// 出——编辑边界为值面拒绝，诊断产码唯一经工厂，PA-1）
    StageLocked,
    /// "not-square"——非方阵（行数≠列数或存储容量不一致——I-MDL-11）
    NotSquare,
    /// "element-not-finite"——元素含 NaN/Inf（I-MDL-3）
    ElementNotFinite,
    /// "window-mismatch"——窗口计数≠方阵阶（MDL-21"方阵 n×n〔适用关节
    /// 窗口与对应电机轴同序〕"）
    WindowMismatch,
    /// "window-out-of-range"——窗口超出根关节表（first+n>jointCount）
    WindowOutOfRange,
    /// "singular"——数值奇异（σmin≤σmax×1×10⁻¹²——不可逆，I-MDL-11）
    Singular,
    /// "ill-conditioned"——病态（重算 κ>1×10⁸——P-RT-7/P-MDL-7 单点阈值）
    IllConditioned,
};

/// @brief 耦合编辑错误码稳定 token（枚举成员连字符串——UT 判别与变更
///        摘要承载）。纯函数；确定性。
std::string_view couplingEditErrorCodeToken(CouplingEditErrorCode code) noexcept;

/**
 * @brief 耦合矩阵编辑拒绝值（局部错误面：码＋定位细节——同 DrivetrainEditError 形态）。
 */
struct CouplingEditError {
    CouplingEditErrorCode code = CouplingEditErrorCode::StageLocked;  ///< 稳定错误码
    std::string detail;  ///< 定位细节（UTF-8；维度/窗口/κ 实测值）

    bool operator==(const CouplingEditError& o) const
    {
        return code == o.code && detail == o.detail;
    }
    bool operator!=(const CouplingEditError& o) const { return !(*this == o); }
};

/**
 * @brief 应用一次腕部关节窗口线性耦合矩阵配置（§4.7 coupling 行——
 *        WP-13-T18；MDL-21/R2 一等字段编辑＋持久化入口）。
 *
 * 规则（按检查序；拒绝时工作集**字节不变**——§9.4.1 拒绝语义的域内核）：
 *   ① 阶段锁（I-MDL-12）：stage==R1Locked 且提交面携带 coupling（非
 *      nullopt）→StageLocked 拒绝——R1 能力位下 coupling 配置即阻断，
 *      "不提前放开 R1 阻断"红线（MDL-12/21 R1 口径；本任务红线原文）。
 *      清除面（nullopt）两态均放行（移除配置不是"配置"——R1 下允许
 *      清除，阶段 D 启用前移除的修复动作即经此面）；
 *   ② 权威编辑守卫语义（C-1 单一判定）：coupling 是传动对象字段，不在
 *      AuthorityLockedField 受管字段轴内（RobotDesign.hpp 注："type/
 *      zeroOffset/bounds 两态均权威，不在管辖内"——coupling 同族：两种
 *      权威模式下均为权威可编辑字段）。本原语**不发明第二套权威拒绝
 *      语义**——不做 StandardDH 态拒绝（那会是 authorityEditGuard 词表
 *      之外私设的 C-1 变体）；测试面钉扎：StandardDH 权威态下合法 C 照
 *      常入修订（AuthorityMode 不阻断本编辑）；
 *   ③ 传动对象确保（ensureDrivetrainEditTarget 同款前置——对象缺席→
 *      草稿确定性句柄创建＋根 drivetrainRef 写入）；
 *   ④ 数值校验（checkCouplingMatrix——I-MDL-11 重算复核；jointCount＝
 *      根关节表长度）：not-square/element-not-finite/window-mismatch/
 *      window-out-of-range/singular/ill-conditioned 任一命中即拒绝
 *      （比较型三要素素材随错误 detail 承载——不降级不静默，M-12）；
 *   ⑤ 提交：coupling 整体写入传动对象＋恰一条变更记录（append-only；
 *      摘要含窗口与阶数定位）。
 *
 * @param ws        [in,out] 目标工作集（拒绝时保证不变——先校验后提交）
 * @param stage     [in] 当前耦合阶段（R1 锁定/R2 启用——能力位由装配/
 *                  面板侧注入，程序默认 R1Locked）
 * @param coupling  [in] 提交面（nullopt＝清除配置；有值＝整体替换）
 * @return nullopt＝接受（工作集已更新＋一条变更记录）；非空＝拒绝
 *         （局部错误面——工作集不变）
 *
 * @throws 无（全部经返回值错误面——调用方输入属可恢复错误轨；传动编辑
 *         前置的越界面不适用——本原语无下标入参）
 *
 * 非线程安全（编辑态仅 UI 线程——ModelingWorkingSet 注）；确定性。
 */
std::optional<CouplingEditError> applyDrivetrainCouplingEdit(ModelingWorkingSet& ws,
                                                             CouplingStage stage,
                                                             std::optional<CouplingDesign> coupling);

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_PARTS_HPP
