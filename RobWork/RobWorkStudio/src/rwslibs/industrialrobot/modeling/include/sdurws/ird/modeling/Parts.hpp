/**
 * @file   Parts.hpp
 * @brief  modeling 四部件对象值模型——ToolDefinition（tool-definition）/
 *         SceneObject（scene-object）/ PoseSet（named-pose-set）/
 *         DrivetrainDesign（robot-drivetrain）（§4.4～§4.7）＋部件级
 *         不变量核查（工具物性 I-MDL-5"断言①②③同连杆"；传动 I-MDL-11/
 *         I-MDL-12）。
 *
 * 设计依据：
 *   - units/modeling.md §4.4（ToolDefinition，MDL-13）、§4.5（SceneObject，
 *     MDL-15）、§4.6（PoseSet，MDL-17）、§4.7（DrivetrainDesign，MDL-16/21、
 *     SEL-10）、§4.2（对象分解——五对象表的后四行）、§4.8（身份/版本/
 *     引用约束）、§4.10（I-MDL-11/I-MDL-12）、§14.2 D-MDL-1/D-MDL-2
 *   - units/policy.md §4.3（SceneObjectRole 词表"归建模语义，policy 消费"
 *     ——词表本体在 ObjectTypes.hpp，本头只消费）
 *   - 需求 MDL-13（工具定义引用不复制）、MDL-15（场景对象）、MDL-16
 *     （动力学参数层）、MDL-17（命名位姿）、MDL-21（传动 R2）、SEL-10
 *     （选型回填）、CON-03（资源三段边界）
 *   - 任务契约 tasks/foundation/WP-13-T03.json acceptance 1（五部件对象
 *     schema——Parts.hpp 行）、acceptance 3（R1 下 coupling 配置拒绝）
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
 */
struct CouplingDesign {
    std::uint32_t rows = 0;      ///< 矩阵行数（＝适用关节窗口高）
    std::uint32_t cols = 0;      ///< 矩阵列数（R2 合法态＝方阵——I-MDL-11）
    std::vector<double> c;       ///< C 矩阵元素（行主序，无量纲；rows*cols 个）
    std::uint32_t jointRangeFirst = 0;  ///< 适用关节窗口起点 i（闭区间，0 起关节序）
    std::uint32_t jointRangeLast = 0;   ///< 适用关节窗口终点 j（闭区间）
    double conditionNumber = 1.0;       ///< 条件数（无量纲；≤1×10⁸——I-MDL-11）

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

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_PARTS_HPP
