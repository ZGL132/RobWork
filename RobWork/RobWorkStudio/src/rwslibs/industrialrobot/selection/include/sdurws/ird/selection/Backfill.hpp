/**
 * @file   Backfill.hpp
 * @brief  器件回填（SEL-10）——回填命令组装＋DeviceBackfillCommandHandler
 *         （project ①命令端口的 selection 域处理器）＋物性合成内核（§12.4）
 *         ＋命令/记录载荷 canonical 编解码。
 *
 * 设计依据：
 *   - units/selection.md §12（器件回填、project 命令、事务和复算——§12.1
 *     时序图 S1～S6、§12.2 回填纪律九条、§12.3 命令形态与 token、§12.4
 *     物性合成规则）、§3.1（组成表 Backfill 行——"回填数据组装＋
 *     DeviceBackfillCommandHandler"）、§3.5（布局表 Backfill.hpp 行——
 *     "IDeviceBackfillCommandHandler/DeviceBackfillCommand"）、§14.8
 *     （IDeviceBackfillCommandHandler 接口原文——prepare 判定在处理器，
 *     P-PR-3 读法）、§14.0（通用约定：调用方错误 fail-fast／数据环境类
 *     返回诊断；零 Qt；确定性 NFR-COR-02）
 *   - units/project.md §5.3（ICommandHandler/HandlerContext/CommandPlan/
 *     PrepareOutcome/CommandEnvelope/ObjectWrite 契约——经公共头
 *     sdurws/ird/project/CommandService.hpp 消费；§4.4.4 commandType
 *     冻结语法 ^[a-z0-9-]{3,64}）、§6.2（expectedRevision 并发校验——
 *     StaleRevisionRejected 的 project 侧承载）、§6.3（invalid-payload/
 *     stale-revision 稳定码——命令服务语义，selection 不复述）
 *   - 需求 SEL-10（应用选型方案经领域命令更新各轴 DriveTrainDesign——
 *     记录目录版本与安装关系；按明确参考系合成质量/质心/惯量，区分壳体
 *     质量与转子等效惯性，禁止重复计入；应用产生新修订并提示实际依赖
 *     结果需复算，复核完成前不沿用原通过结论）、MDL-16（合成后的质量/
 *     质心/惯量与 MDL-05 几何估算区分并逐项标记来源；器件回填后的合成
 *     规则见 SEL-10）、MDL-05（平行轴规则同源）、MDL-06（合成结果断言
 *     ①～③同语义——m>0、SPD、三角不等式）、AT-30（应用选型→更新传动
 *     配置→壳体/转子惯量回填不重复计入→新修订提示复算；复核前不沿用
 *     通过结论）、PM-12（回填基线）、CON-02/05（历史不可变＋内容寻址）
 *   - 任务契约 tasks/foundation/WP-19-T09.json acceptance 1～3（命令端口
 *     回填＋合成不重复计入；新修订＋复算提示＋AT-30 用例；token 无点
 *     词形＋ird_gates 零命中）
 *
 * 背景说明（本头在命令链路中的位置——第一读者须知）：
 *   选型回填是"评估结果落回工程模型"的写路径：用户在选型结果中应用某
 *   候选组合后，本单元把逐轴的目录器件引用（目录版本＋电机/减速器型号
 *   ＋安装关系）与合成物性组装为领域命令，经 project ①命令端口提交；
 *   project 编排 S1～S6（形式校验/基线解析/处理器 prepare/确认/双编译/
 *   事务提交——units/project.md §6），selection 只提供 ICommandHandler
 *   实现并在 prepare 阶段完成全部域判定（P-PR-3：命令服务零业务数值）。
 *   一切拒绝/失败路径零修订（MDL-06 原子性）；多轴回填整体原子（§12.2
 *   纪律 3——一个命令＝恰一新修订）。
 *
 * ★ 依赖形态登记（DTB §5.4——T09 落位细化 ①，为什么 include project
 *   公共头却不链接 project 库）：
 *   卡 §3.2 将 project 列于"运行时注入/端口"列＝**零编译链接边**（产品
 *   库不链接 sdurws_ird_project——ird_gates SUB 面与本单元配置期守卫
 *   双面钉住）。§14.8 的 handler 契约要求实现 project::ICommandHandler
 *   接口——接口实现只需 include 对方公共头（CommandService.hpp，R-2
 *   许可的公共头消费形态）并遵守"零调用对方实现符号"纪律：
 *     ①本头/实现不调用 HandlerContext 的任何方法（objectId 取号改经
 *       ObjectWrite.objectId 空值语义＝project S6 装配点分配——公共头
 *       ObjectWrite 注释原文；基线对象定位改经 prepare 入参 baseSnapshot
 *       的 objectRefs 值快照）；
 *     ②HandlerContext 构造函数实现在 project 库内——测试面不构造该类
 *       （prepare 的可测内核＝planFromEnvelope，不依赖 HandlerContext，
 *       prepare 壳仅做直通转发）；HandlerContext::objectId() 与
 *       HandlerRegistry 构造/注册等实现符号在 selection 全域零引用——
 *       链接面自然闭合（双模式构建链接成功为机器证据）；
 *     ③产品面 include 单元白名单的扩展（{selection,core,evidence}→
 *       ＋project）随单元卡 §3.2 增量修订与两处红线测试同步（单元内
 *       同判据双防线——BuildRedLineTest/BuildGraphContractTest）。
 *
 * ★ 回填记录对象与 modeling robot-drivetrain 的关系（DTB §5.4——T09
 *   落位细化 ②，PA-1 权威唯一）：
 *   SEL-10 的目标是"更新各轴 DriveTrainDesign"（modeling robot-drivetrain
 *   对象，modeling.md §4.7 表 catalogBackfill 行明文"阶段 C 由 selection
 *   命令写入"）。但 robot-drivetrain 的 canonical 字节编码唯一权威＝
 *   modeling Codec（modeling.md §4.8；selection 零 modeling 编译边——
 *   §3.2 R-1/R-2），selection 自拼该对象字节＝第二实现（NFR-MNT-03）。
 *   因此本落位将回填记录落为 **selection 域独立对象**（objectTypeToken
 *   ＝kBackfillRecordObjectToken，"sel-device-backfill"——词形同对象
 *   token 约定）：完整承载 SEL-10 语义（逐轴目录版本/安装关系/合成物性
 *   ／转子独立登记/复算提示），不冒用 modeling 对象 token 防修订闭包
 *   路由歧义（modeling ObjectTypes.hpp 注释——token 是解析器路由键，
 *   拼写漂移＝路由失联）。权威 robot-drivetrain 对象的写入走 modeling
 *   既有 apply-drivetrain-design 命令链，由 L5 装配层把本命令的回填
 *   字段转译编排（optimization Applier 两步编排同款先例）。该跨命令
 *   编排登记为 P-SEL-7 待对齐项（裁决与落位归 L5 装配/集成面）——
 *   单元卡 §19.3 T09 落位细化同步登记。
 *
 * 线程与生命周期（§14.10）：处理器实例在命令服务串行槽内被调用（同
 *   上下文串行）——实现无共享可变状态，可重入；组装器/合成内核/编解码
 *   均纯函数。所有权：输入由调用方持有，输出按值返回。
 */

#ifndef IRD_SELECTION_BACKFILL_HPP
#define IRD_SELECTION_BACKFILL_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>       // DiagnosticRecord（拒绝定位诊断）
#include <sdurws/ird/core/Digest.hpp>         // ContentVersion（目录锁定版本强类型）
#include <sdurws/ird/core/Identity.hpp>       // ObjectId/RevisionId（身份强类型）
#include <sdurws/ird/selection/CatalogTypes.hpp>  // CatalogPackageSnapshot/CatalogVersion/ModelId
#include <sdurws/ird/project/CommandService.hpp>  // ICommandHandler/CommandEnvelope/CommandPlan/PrepareOutcome——§14.8 接口面（零实现符号引用，见文件头"依赖形态登记"）

namespace sdurws::ird::selection {

// =====================================================================
// 冻结常量（唯一书写点——实现/测试/留痕共用，禁字符串拼词）
// =====================================================================

/**
 * @brief 回填命令注册 token（§12.3 建议值——无点形态）。
 *
 * P-SEL-3 处置：project 命令 token 语法存在 P-PR-9 争议（§4.4.4 冻结
 * ^[a-z0-9-]{3,64} 不含点 vs §6.5 含点示例），裁决前以无点保守形态占位
 * （与 modeling 卡 D-MDL-6/O-35 同案——DTB §4 2026-09-22 裁决"采用无点
 * 形态＝服从现行冻结语法"）；P-PR-9 裁决含点后随增量任务迁移（迁移属
 * 破坏性变更，须同步 payload 版本语义——本项目 v1 不适用）。
 */
inline constexpr std::string_view kBackfillCommandToken = "apply-device-backfill";

/// 回填命令载荷格式版本（无单位——处理器自有演进版本戳；不受理的历史
/// 版本由 prepare 拒绝——NFR-DEP-04）。
inline constexpr std::uint32_t kBackfillPayloadFormatVersion = 1;

/// 回填记录对象类型 token（selection 域登记——见文件头"回填记录对象"
/// 段；词形遵守对象 token 小写连字符约定，修订闭包 objectRefs 路由键）。
inline constexpr std::string_view kBackfillRecordObjectToken = "sel-device-backfill";

/// 回填记录载荷 magic（8 字节协议头——域内 canonical 编码协议冻结面；
/// "IRDSBFV1"＝IRD selection backfill record v1）。
inline constexpr std::string_view kBackfillRecordMagic = "IRDSBFV1";

/// 命令载荷 magic（8 字节协议头——"IRDSBFP1"＝IRD selection backfill
/// payload v1；magic 尾字符与版本常量同步演进）。
inline constexpr std::string_view kBackfillPayloadMagic = "IRDSBFP1";

/// 命令载荷 codec 版本（字段面演进递增——CON-04 切片身份同源纪律；
/// v1 字节流由解码侧严格核对，不带静默兼容读）。
inline constexpr std::uint32_t kBackfillPayloadCodecVersion = 1;

// ---- 参考系声明词表（§12.4"按明确参考系合成（建议：连杆坐标系/质心系，
//      随命令留痕）"的冻结承载；回填命令必须显式携带，缺席即拒绝）----

/// 连杆坐标系参考系：全部几何量（质心位置/惯量张量姿态）在各轴连杆坐标
/// 系 {L} 下表示（{L} 定义随权威模型——modeling BodyData 同款语义：质心
/// 基准、连杆系参考姿态）。
inline constexpr std::string_view kBackfillFrameLink = "link-frame";

// =====================================================================
// 复算提示（AT-30——§12.1"提示动力学/传动/选型/优化结果需要复算"）
// =====================================================================

/**
 * @brief 复算提示域词表（AT-30 四域——SEL-10 需求原文"实际依赖结果
 *        （运动学/动力学/选型/优化指标）需复算"的封闭承载）。
 *
 * 回填变更权威模型的器件参数（传动比/合成物性）后，这些域的历史评估
 * 结果失去当前性——复核完成前**不得沿用原通过结论**（AT-30）。本单元
 * 只产出"哪些域需复算"的事实记录（值传递）；当前性判定与包络更新的
 * 所有权归 evidence（§11/§13.1 责任矩阵——selection 不越权改判）。
 */
enum class RecalcDomain : std::uint8_t {
    Kinematics = 0,   ///< 运动学（合成物性变更影响质量分布相关几何估算链）
    Dynamics = 1,     ///< 动力学（合成质量/质心/惯量与摩擦/限值是 DYN 输入）
    Selection = 2,    ///< 选型（本域工作点/可行集随权威模型变化失效）
    Optimization = 3, ///< 优化指标（StageB 传动变量/指标随基线变化失效）
};

/// 复算域 token（"kinematics"/"dynamics"/"selection"/"optimization"——
/// 记录载荷与报告机器判读用；词表外值返回空串，防御分支不可达）。
std::string_view recalcDomainToken(RecalcDomain domain) noexcept;

/// 复算域数量（4——词表封闭性的编译期事实；供断言与遍历）。
inline constexpr std::size_t kRecalcDomainCount = 4;

/**
 * @brief AT-30 复算提示集（回填记录对象的固定组成部分）。
 *
 * domains 恒为四域全量（SEL-10 原文枚举的"实际依赖结果"——回填必然
 * 同时影响运动学/动力学/选型/优化四域的输入链，本落位不做逐域裁剪，
 * 裁剪语义若有随域需求演进登记）；retainPriorConclusion 恒 false——
 * "复核完成前不沿用原通过结论"的记录面（false＝旧结论不复用；复核
 * 完成后的当前性推进归 evidence，不归本对象改写）。
 */
struct BackfillRecalcNotice {
    /// 需复算域全集（恒四域；kRecalcDomainCount 大小不变量由组装面保证）。
    bool domains[kRecalcDomainCount] = { true, true, true, true };
    /// 复核完成前不沿用原通过结论（AT-30——恒 false＝不沿用；编码为
    /// u8 标志位，解码侧严格核对取值）。
    bool retainPriorConclusion = false;

    bool operator==(const BackfillRecalcNotice& o) const noexcept
    {
        for (std::size_t i = 0; i < kRecalcDomainCount; ++i) {
            if (domains[i] != o.domains[i]) { return false; }
        }
        return retainPriorConclusion == o.retainPriorConclusion;
    }
    bool operator!=(const BackfillRecalcNotice& o) const noexcept
    {
        return !(*this == o);
    }
};

// =====================================================================
// 回填几何值模型（§12.4 合成输入/输出——全部 SI 单位＋显式参考系）
// =====================================================================

/**
 * @brief 三维位置矢量（单位 m；参考系随所属结构体的注释声明）。
 *
 * 独立小结构而非第三方向量类型：selection 计算库零 Qt／零框架依赖
 * （§14.0），回填面不引入 modeling 值类型（R-2——零 modeling include）。
 */
struct BackfillVec3 {
    double x = 0.0;  ///< X 分量，单位 m
    double y = 0.0;  ///< Y 分量，单位 m
    double z = 0.0;  ///< Z 分量，单位 m

    bool operator==(const BackfillVec3& o) const noexcept
    {
        return x == o.x && y == o.y && z == o.z;
    }
    bool operator!=(const BackfillVec3& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 惯量张量的六分量对称承载（单位 kg·m²；绕指定参考点、坐标轴
 *        姿态取所在坐标系轴向——modeling BodyData 同款语义）。
 *
 * 对称性说明（MDL-06 断言②的落位口径）：真实惯量张量必对称，本类型
 * 以六分量存储从结构上排除不对称输入通道（无 9 分量形态）——"对称
 * 性相对 1×10⁻¹²"的检查语义由存储形态结构性满足（诚实登记：不存在
 * 可携带不对称输入的字段面；合成运算〔加法＋秩一修正〕保持对称）。
 */
struct BackfillInertiaTensor {
    double ixx = 0.0;  ///< ∫(y²+z²)dm，单位 kg·m²
    double iyy = 0.0;  ///< ∫(x²+z²)dm，单位 kg·m²
    double izz = 0.0;  ///< ∫(x²+y²)dm，单位 kg·m²
    double ixy = 0.0;  ///< −∫xy·dm（惯性积，符号约定＝负积分式），单位 kg·m²
    double ixz = 0.0;  ///< −∫xz·dm，单位 kg·m²
    double iyz = 0.0;  ///< −∫yz·dm，单位 kg·m²

    bool operator==(const BackfillInertiaTensor& o) const noexcept
    {
        return ixx == o.ixx && iyy == o.iyy && izz == o.izz
            && ixy == o.ixy && ixz == o.ixz && iyz == o.iyz;
    }
    bool operator!=(const BackfillInertiaTensor& o) const noexcept
    {
        return !(*this == o);
    }
};

/**
 * @brief 单轴回填条目（命令载荷的逐轴域模型——§12.1"各轴 DriveTrain-
 *        Design 回填数据＋目录版本＋安装关系＋合成物性"的值承载）。
 *
 * 输入纪律：全部几何量在**该轴连杆坐标系**下表示（参考系由命令顶层
 * referenceFrameToken 声明——v1 冻结 kBackfillFrameLink）；全部数值
 * SI（质量 kg／长度 m／惯量 kg·m²）；传动比无量纲。
 *
 * 壳体/转子分轨（§12.2 纪律 5——SEL-10/MDL-16 防重复计入）：
 *   - motorHousingMassKg/motorHousingInertia 与 gearboxHousing* 进合成
 *     （§12.4 合成项：连杆原值＋电机壳体＋减速器壳体）；
 *   - rotorInertiaKgM2 为**独立登记字段**——不参与合成（drivetrain 映射
 *     显式计入的唯一位置——映射按 J·i² 反射进电机侧方程，若再进合成
 *     惯量即双重计入同一物理量）。
 */
struct AxisBackfillEntry {
    // ---- 身份与目录引用（记录面）----
    core::ObjectId jointId;        ///< 轴（关节）对象 ID——ARC-04 稳定身份，
                                   ///  与权威模型对齐键（不经名称匹配）
    std::string catalogId;         ///< 目录稳定 ID（CatalogIdentity.catalogId 同值——非空）
    std::string catalogVersion;    ///< 目录版本号（CatalogIdentity.version 同值——非空）
    std::string motorModelId;      ///< 电机型号稳定 ID（目录包内唯一键——非空）
    std::string gearboxModelId;    ///< 减速器型号稳定 ID（同上——非空）
    std::string mountKind;         ///< 安装关系词表值（CompatibilityRecord.mountKind
                                   ///  同值——安装关系记录，§12.2 纪律 4）
    core::ObjectId catalogLockObject;   ///< 目录锁定对象 ID（项目 catalog/<id>/<ver>
                                        ///  锁定对象——ValueProvenance.sourceObject 语义）
    core::ContentVersion catalogLockVersion; ///< 锁定对象内容版本（P-1：有
                                   ///  lockObject 必有 lockVersion——引用完整性）

    // ---- 传动参数（记录面——I-MDL-11 同口径校验）----
    double appliedRatio = 0.0;     ///< 回填传动比（无量纲；>0 且有限——与
                                   ///  modeling ratioPerJoint 消费口径一致）

    // ---- 合成输入：连杆原值（权威模型值传递——§12.4"连杆原值（权威模型）"）----
    double linkMassKg = 0.0;       ///< 连杆质量，单位 kg（>0——权威模型已提供物性）
    BackfillVec3 linkComM;         ///< 连杆质心，单位 m（该轴连杆坐标系下）
    BackfillInertiaTensor linkInertia; ///< 连杆惯量（绕连杆质心、连杆系姿态），
                                   ///  单位 kg·m²

    // ---- 合成输入：电机壳体（按安装位置——§12.4 合成项）----
    BackfillVec3 motorComAnchorM;  ///< 电机壳体质心位置，单位 m（连杆坐标系
                                   ///  下——由安装关系与安装尺寸确定，组装方输入）
    double motorHousingMassKg = 0.0; ///< 电机壳体质量，单位 kg（目录
                                   ///  MotorCatalogEntry.mass 同值；>0）
    BackfillInertiaTensor motorHousingInertia; ///< 电机壳体惯量（绕壳体质心、
                                   ///  连杆系姿态），单位 kg·m²——目录 v1 无
                                   ///  此字段（MotorCatalogEntry 仅有质量），
                                   ///  由调用方按器件数据补充输入（P-SEL-7
                                   ///  登记的数据面缺口，ValueProvenance 归
                                   ///  UserProvided 语义）

    // ---- 合成输入：减速器壳体（同上）----
    BackfillVec3 gearboxComAnchorM; ///< 减速器壳体质心位置，单位 m（连杆坐标系下）
    double gearboxHousingMassKg = 0.0; ///< 减速器壳体质量，单位 kg（目录
                                   ///  GearboxCatalogEntry.mass 同值；>0）
    BackfillInertiaTensor gearboxHousingInertia; ///< 减速器壳体惯量（绕壳体
                                   ///  质心、连杆系姿态），单位 kg·m²——目录
                                   ///  GearboxCatalogEntry.housingInertia 可缺失
                                   ///  （optional），缺失时整体失败（P-SEL-6
                                   ///  保守口径，§12.4 缺失处理）

    // ---- 转子等效惯量（独立字段——不进合成，§12.2 纪律 5）----
    double rotorInertiaKgM2 = 0.0; ///< 电机转子等效惯量，单位 kg·m²（电机轴
                                   ///  系；目录 MotorCatalogEntry.rotorInertia
                                   ///  同值；>0）——独立登记，映射侧显式计入
                                   ///  的唯一位置（防与壳体合成重复计入）

    bool operator==(const AxisBackfillEntry& o) const noexcept
    {
        return jointId == o.jointId && catalogId == o.catalogId
            && catalogVersion == o.catalogVersion && motorModelId == o.motorModelId
            && gearboxModelId == o.gearboxModelId && mountKind == o.mountKind
            && catalogLockObject == o.catalogLockObject
            && catalogLockVersion == o.catalogLockVersion
            && appliedRatio == o.appliedRatio && linkMassKg == o.linkMassKg
            && linkComM == o.linkComM && linkInertia == o.linkInertia
            && motorComAnchorM == o.motorComAnchorM
            && motorHousingMassKg == o.motorHousingMassKg
            && motorHousingInertia == o.motorHousingInertia
            && gearboxComAnchorM == o.gearboxComAnchorM
            && gearboxHousingMassKg == o.gearboxHousingMassKg
            && gearboxHousingInertia == o.gearboxHousingInertia
            && rotorInertiaKgM2 == o.rotorInertiaKgM2;
    }
    bool operator!=(const AxisBackfillEntry& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 回填命令域请求（命令载荷的解码形态——encodeBackfillPayload 的
 *        输入／decodeBackfillPayload 的输出）。
 *
 * axes 至少一条（零轴回填＝无意义命令，组装面拒绝）；轴身份 jointId 在
 * 命令内唯一（同轴重复＝调用方错误——多轴整体原子语义下的身份二义）。
 */
struct DeviceBackfillRequest {
    /// 参考系声明（§12.4"按明确参考系合成；记录参考系"——v1 词表冻结
    /// kBackfillFrameLink；词表外值组装/解码即拒绝）。
    std::string referenceFrameToken{std::string(kBackfillFrameLink)};
    /// 逐轴回填条目（≥1；组装序＝提交序，编码按声明序不重排）。
    std::vector<AxisBackfillEntry> axes;

    bool operator==(const DeviceBackfillRequest& o) const
    {
        return referenceFrameToken == o.referenceFrameToken && axes == o.axes;
    }
    bool operator!=(const DeviceBackfillRequest& o) const { return !(*this == o); }
};

// =====================================================================
// 物性合成内核（§12.4——SEL-10/MDL-05/16 承接；P-SEL-6 裁决前自持实现
// 与 MDL-06 断言同语义，黄金用例钉住）
// =====================================================================

/**
 * @brief 单轴合成结果（§12.4 合成目标——写回填记录的物性合成项）。
 *
 * 合成结果语义＝modeling BodyData 同款：质量（kg）、质心（m，连杆系）、
 * 惯量张量（绕合成质心、连杆系姿态——kg·m²）。来源标记：合成项逐轴
 * ValueProvenance.kind＝CatalogBackfill（core ProvenanceKind 词表——
 * 器件目录回填），随记录载荷以 methodTag 文本承载（"sel-backfill-
 * synthesis"；SourcedValue 的完整来源对象引用＝轴条目的目录锁定引用）。
 */
struct AxisSynthesis {
    double massKg = 0.0;              ///< 合成质量，单位 kg（连杆＋两壳体之和）
    BackfillVec3 comM;                ///< 合成质心，单位 m（连杆坐标系下——质量加权）
    BackfillInertiaTensor inertia;    ///< 合成惯量（绕合成质心、连杆系姿态），
                                      ///  单位 kg·m²——壳体计入、**转子不计入**
    double rotorInertiaKgM2 = 0.0;    ///< 转子等效惯量独立登记（kg·m²，电机轴
                                      ///  系）——与 inertia 分轨（不重复计入的
                                      ///  记录面证明）

    bool operator==(const AxisSynthesis& o) const noexcept
    {
        return massKg == o.massKg && comM == o.comM && inertia == o.inertia
            && rotorInertiaKgM2 == o.rotorInertiaKgM2;
    }
    bool operator!=(const AxisSynthesis& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 合成失败分类（错误二分——AGENTS §3：全部为数据/调用方侧）。
 */
enum class SynthesisFailure {
    NonFiniteInput,     ///< 输入含非有限数（NaN/±Inf——NFR-COR-03 拒绝）
    RangeInvalid,       ///< 数值范围非法（质量/转子惯量 ≤0 等）
    MassNonPositive,    ///< 合成质量非正（MDL-06 断言①——m>0）
    NotPositiveDefinite,///< 合成惯量非正定（MDL-06 断言②——SPD 特征值严格>0）
    TriangleInequality, ///< 三角不等式违约（MDL-06 断言③——主惯量 λ1+λ2≥λ3）
};

/**
 * @brief 单轴物性合成（§12.4 规则唯一实现点——壳体进合成、转子分轨）。
 *
 * 算法（逐步——AGENTS §2.4）：
 *   第 1 步 输入卫生检查：全部数值有限（NFR-COR-03）；质量/转子惯量
 *           范围检查（>0）——失败即拒绝，不静默置零；
 *   第 2 步 各部件惯量自其质心向连杆坐标系原点平行轴迁移：
 *           I_i^O ＝ I_i ＋ m_i·(|c_i|²E₃ − c_i·c_iᵀ)（c_i 在连杆系下；
 *           MDL-05 平行轴规则同源）；
 *   第 3 步 求和：M＝Σm_i；c＝Σ(m_i·c_i)/M（质量加权质心）；I^O＝ΣI_i^O；
 *   第 4 步 向合成质心回迁：I_syn ＝ I^O − M·(|c|²E₃ − c·cᵀ)——结果即
 *           "绕合成质心、连杆系姿态"的惯量张量（BodyData 消费语义）；
 *   第 5 步 MDL-06 断言①～③同语义检查（附录 D 第 6/7 项容差）：
 *           ① M>0；② SPD（3×3 对称阵三特征值严格>0）；③ 三角不等式
 *           （λ_sorted(1)+λ_sorted(2) ≥ λ_sorted(3)——特征值非负前提
 *           下即 λ1+λ2−λ3 ≥ 0；相对容差 1×10⁻¹²）——任一失败即整体
 *           拒绝（回填失败零修订，§12.4 断言行）。
 *
 * @param axis [in] 单轴回填条目（参考系＝该轴连杆坐标系；SI 单位）
 * @return 合成结果；失败时 ok=false＋failure 分类＋detail 中文定位
 *         （不抛异常——数据侧拒绝走返回值轨，§14.0 错误二分）
 *
 * @note 纯函数；可重入；确定性（同输入恒同输出——NFR-COR-02）。
 */
struct SynthesisOutcome {
    bool ok = false;              ///< true＝合成与断言全部通过
    SynthesisFailure failure = SynthesisFailure::NonFiniteInput; ///< 失败分类（ok=false 有效）
    AxisSynthesis synthesis{};    ///< 合成结果（ok=true 有效）
    std::string detail;           ///< 中文定位（失败原因——诊断/留痕素材）
};

SynthesisOutcome synthesizeAxisBodyProperties(const AxisBackfillEntry& axis);

// =====================================================================
// 命令载荷 canonical 编解码（组装面→①端口→handler 解码的协议面）
// =====================================================================

/**
 * @brief 编码回填命令载荷（DeviceBackfillRequest → canonical 字节）。
 *
 * 协议（IRDSBFP1）：magic(8)＋u32 codec 版本＋str(参考系 token)＋u32 轴
 * 数＋逐轴（身份/目录引用/安装关系/传动比/连杆原值/两壳体物性与锚点/
 * 转子惯量——字段定序见实现；小端定宽、UTF-8、无填充、无环境量——
 * project.md §4.8 canonical 规则同语义）。
 *
 * @param request [in] 回填请求（axes ≥1；数值须有限——编码入口校验）
 * @return canonical 字节（确定性——同输入恒同字节，NFR-COR-02）
 *
 * @throws std::invalid_argument axes 为空、jointId 非法（全零保留值）、
 *         同轴重复、参考系词表外、任一数值非有限（调用方错误 fail-fast
 *         ——§14.0 错误二分）
 */
std::vector<std::uint8_t> encodeBackfillPayload(const DeviceBackfillRequest& request);

/**
 * @brief 命令载荷解码结果（查询轨两态——handler 解码外部字节不抛）。
 */
struct DecodedBackfillPayload {
    enum class Status {
        Ok,                 ///< 解码成功（request 有效）
        Malformed,          ///< 结构非法（magic/版本/截断/残余/非法标志）
        UnsupportedVersion, ///< codec 版本不受理（NFR-DEP-04——升级指引面）
    };
    Status status = Status::Malformed;
    DeviceBackfillRequest request{}; ///< 解码结果（status==Ok 有效）
    std::string detail;              ///< 失败定位（中文——诊断素材）
};

/**
 * @brief 解码回填命令载荷（严格校验：越界/残余/非有限/词表外/非法身份
 *        一律 Malformed；版本不符 UnsupportedVersion；不产出半成品）。
 *
 * @param bytes [in] 载荷字节（任意来源——可能截断/篡改/异版）
 * @return 解码结果（不抛异常——查询轨）
 *
 * @note 纯函数；确定性。
 */
DecodedBackfillPayload decodeBackfillPayload(const std::vector<std::uint8_t>& bytes);

// =====================================================================
// 回填记录对象 canonical 编解码（CommandPlan.objectWrites 的载荷——
// SEL-10 语义的落盘记录面）
// =====================================================================

/**
 * @brief 回填记录对象（revision 闭包中 sel-device-backfill 对象的字节
 *        解码形态——恰一对象承载一次回填命令的全部轴，多轴整体原子的
 *        记录面载体）。
 */
struct BackfillRecordObject {
    std::string referenceFrameToken;   ///< 参考系声明（随命令留痕——§12.4）
    BackfillRecalcNotice recalc;       ///< AT-30 复算提示（四域＋不沿用标志）
    std::vector<AxisSynthesis> synthesis; ///< 逐轴合成结果（与命令 axes 同序
                                           ///  同长——合成记录面）
    std::vector<AxisBackfillEntry> axes;  ///< 逐轴回填条目（目录引用/安装
                                           ///  关系/传动比/输入原值——追溯面）

    bool operator==(const BackfillRecordObject& o) const
    {
        return referenceFrameToken == o.referenceFrameToken && recalc == o.recalc
            && synthesis == o.synthesis && axes == o.axes;
    }
    bool operator!=(const BackfillRecordObject& o) const { return !(*this == o); }
};

/**
 * @brief 编码回填记录对象（BackfillRecordObject → canonical 字节；协议
 *        IRDSBFV1——magic＋版本＋参考系＋复算提示＋逐轴〔合成结果＋条目
 *        全字段〕）。
 *
 * @param record [in] 记录对象（synthesis 与 axes 等长且非空——组装面保证）
 * @return canonical 字节（确定性）
 *
 * @throws std::invalid_argument synthesis/axes 尺寸不符或为空、身份非法、
 *         数值非有限（调用方错误 fail-fast）
 */
std::vector<std::uint8_t> encodeBackfillRecordObject(const BackfillRecordObject& record);

/**
 * @brief 解码回填记录对象（查询轨两态——同 decodeBackfillPayload 纪律）。
 */
struct DecodedBackfillRecord {
    enum class Status { Ok, Malformed, UnsupportedVersion };
    Status status = Status::Malformed;
    BackfillRecordObject record{}; ///< 解码结果（status==Ok 有效）
    std::string detail;            ///< 失败定位
};

DecodedBackfillRecord decodeBackfillRecordObject(const std::vector<std::uint8_t>& bytes);

// =====================================================================
// 回填数据组装（§3.1 组成表 Backfill 行"回填数据组装"——目录快照＋
// 调用方物理输入 → DeviceBackfillRequest＋命令信封）
// =====================================================================

/**
 * @brief 单轴组装源（调用方输入——目录之外的物理量与锚点值传递）。
 *
 * 背景：合成需要的"连杆原值＋两壳体质心锚点"来自权威模型与安装布置
 * （selection 不拥有也不重算——§13.1 责任矩阵），由组装方从模型读取
 * 后值传递；目录侧字段（壳体质量/转子惯量/减速器壳体惯量）由组装器
 * 自目录快照取值——**不经本结构重复输入**（单一来源，防两处漂移）。
 * 电机壳体惯量目录 v1 无字段，是唯一经本结构补充的目录缺口（P-SEL-7
 * 登记项——补充值的 ValueProvenance 归 UserProvided 语义）。
 */
struct AxisBackfillSource {
    core::ObjectId jointId;        ///< 轴对象 ID（须与目录兼容对所属模型一致——
                                   ///  组装器不核验模型归属〔零 modeling 边〕，
                                   ///  由调用方保证）
    ModelId motorModelId;          ///< 电机型号（须存在于快照电机主表）
    ModelId gearboxModelId;        ///< 减速器型号（须存在于快照减速器主表）
    std::string mountKind;         ///< 期望安装关系（须与快照 compatibility 表
                                   ///  该型号对的记录一致——不一致组装拒绝）

    // ---- 连杆原值（权威模型；连杆坐标系，SI）----
    double linkMassKg = 0.0;
    BackfillVec3 linkComM;
    BackfillInertiaTensor linkInertia;

    // ---- 壳体质心锚点（安装布置；连杆坐标系，m）----
    BackfillVec3 motorComAnchorM;
    BackfillVec3 gearboxComAnchorM;

    // ---- 电机壳体惯量补充（目录 v1 缺口——P-SEL-7；nullopt＝未补充，
    //      组装即数据不足拒绝——§12.4 缺失处理整体失败口径）----
    std::optional<BackfillInertiaTensor> motorHousingInertiaSupplement;

    // ---- 回填传动比（候选传动参数——取值口径＝组合校核段的候选传动
    //      参数构造〔DeviceCombination 侧 c=1/n 换算的唯一实现点〕，本处
    //      只透传不重算——PA-1；无量纲，>0 且有限——I-MDL-11 同口径）----
    double appliedRatio = 0.0;

    bool operator==(const AxisBackfillSource& o) const noexcept
    {
        return jointId == o.jointId && motorModelId == o.motorModelId
            && gearboxModelId == o.gearboxModelId && mountKind == o.mountKind
            && linkMassKg == o.linkMassKg && linkComM == o.linkComM
            && linkInertia == o.linkInertia
            && motorComAnchorM == o.motorComAnchorM
            && gearboxComAnchorM == o.gearboxComAnchorM
            && motorHousingInertiaSupplement == o.motorHousingInertiaSupplement
            && appliedRatio == o.appliedRatio;
    }
    bool operator!=(const AxisBackfillSource& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 组装结果（两态——组装失败即拒绝提交，不产出半成品请求）。
 */
struct BackfillAssemblyOutcome {
    enum class Kind {
        Assembled,          ///< 组装成功（request/envelope 模板有效）
        UnknownDevice,      ///< 型号不在快照主表（SEL-BACKFILL-UNKNOWN-DEVICE 语义）
        MountIncompatible,  ///< 安装关系与兼容表不一致（SEL-BACKFILL-MOUNT-MISMATCH）
        DataInsufficient,   ///< 壳体物性缺失且无补充（P-SEL-6 整体失败口径）
        InvalidInput,       ///< 身份/数值非法（非有限/空轴表/重复轴等）
    };
    Kind kind = Kind::InvalidInput;
    DeviceBackfillRequest request{};   ///< 组装产物（kind==Assembled 有效）
    std::vector<AxisSynthesis> synthesis; ///< 逐轴合成预演（同序同长——组装期
                                           ///  已完成 §12.4 合成与断言）
    std::string detail;                ///< 中文定位（失败原因/成功摘要）
};

/**
 * @brief 组装回填请求（§12.1"组装 DeviceBackfillCommand"的唯一实现点）。
 *
 * 组装步骤（逐步）：
 *   1. 输入卫生：axes 非空、无重复 jointId、数值有限——违约 InvalidInput；
 *   2. 逐轴目录取值：电机/减速器条目查找（缺失 UnknownDevice）；减速器
 *      壳体惯量取目录 optional 值（缺失且无源＝DataInsufficient——§12.4
 *      整体失败）；电机壳体惯量取补充值（缺席即 DataInsufficient）；
 *   3. 安装关系核对：快照 compatibility 表存在 (motor, gearbox, mountKind)
 *      记录（缺失/不一致 MountIncompatible——§12.2 纪律 4 的组装侧证明）；
 *   4. 合成预演：synthesizeAxisBodyProperties（§12.4）——失败透传
 *      InvalidInput（断言失败细节随 detail）；
 *   5. 产出 DeviceBackfillRequest（目录版本/锁定引用来自 lock 参数）。
 *
 * @param snapshot [in] 目录快照（锁定版本的不可变业务模型——调用方经
 *                 ICatalogProvider 取得并持有）
 * @param lock     [in] 锁定版本引用（lockObjectId＋identity——记录进逐轴
 *                 目录引用；P-1 校验：identity 内容摘要须与快照一致——
 *                 失配即 InvalidInput，引用完整性破坏属调用方错误）
 * @param axes     [in] 逐轴组装源（≥1）
 * @return 组装结果（不抛异常——数据/调用方侧拒绝走返回值轨）
 *
 * @note 纯函数；可重入；确定性。
 */
BackfillAssemblyOutcome assembleDeviceBackfill(const CatalogPackageSnapshot& snapshot,
                                               const CatalogVersion& lock,
                                               const std::vector<AxisBackfillSource>& axes);

/**
 * @brief 构造回填命令信封（组装产物 → ①端口 submit 输入）。
 *
 * branch/expectedRevision 由调用方给出（分支基线是提交期事实——组装器
 * 不感知项目状态；expectedRevision 缺省＝分支 tip，显式失配由 project
 * S2 以 stale-revision 拒绝——§12.2 纪律 2 的 project 侧承载）。
 *
 * @param branch           [in] 目标分支（brn- 规范文本）
 * @param expectedRevision [in] 期望基线修订（nullopt＝分支 tip）
 * @param request          [in] 组装产物（assembleDeviceBackfill 成功面）
 * @return 命令信封（commandType/payloadFormatVersion/payloadCanonical 按
 *         冻结常量填充——token 无点词形 P-SEL-3）
 *
 * @throws std::invalid_argument request 为空轴表（组装成功面不可能——
 *         防御性快失败）
 */
project::CommandEnvelope makeBackfillEnvelope(core::BranchId branch,
                                              std::optional<core::RevisionId> expectedRevision,
                                              const DeviceBackfillRequest& request);

// =====================================================================
// 命令处理器（§14.8——IDeviceBackfillCommandHandler＋具体实现）
// =====================================================================

/**
 * @brief 计划产出结果（prepare 三态的值面——不依赖 HandlerContext 的
 *        可测内核；kind 与 project::PrepareOutcome 一一对应）。
 */
struct BackfillPlanOutcome {
    enum class Kind {
        Planned,            ///< 计划已产出（plan 有效——进入 S4/S5/S6）
        RejectedInvalidInput, ///< 载荷结构/域校验不过（→ invalid-payload）
        RejectedHardAssert,   ///< 合成物性断言失败（MDL-06 同语义——
                              ///  → hard-assert-failed 就地阻止）
    };
    Kind kind = Kind::RejectedInvalidInput;
    std::string detail;     ///< 中文定位（拒绝原因/计划摘要）
};

/**
 * @brief SEL-10 回填命令处理器接口（§14.8 卡面命名形态——project::
 *        ICommandHandler 的 selection 域特化，追加不依赖 HandlerContext
 *        的计划内核方法；L5 装配期以 DeviceBackfillCommandHandler 实例
 *        注册进 HandlerRegistry）。
 *
 * prepare 契约（§14.8 注释原文）：回填数据合法性＋合成物性断言（§12.4）
 * ——判定在处理器（P-PR-3 读法）；命令提交/事务/双编译/修订由 project
 * 命令服务编排（selection 不自建事务）。@throws 无（拒绝经 prepare 三态
 * ＋diags 返回——命令服务语义）。
 */
class IDeviceBackfillCommandHandler : public project::ICommandHandler {
public:
    ~IDeviceBackfillCommandHandler() override = default;

    /**
     * @brief 计划内核：解码载荷→域校验→合成断言→CommandPlan 组装。
     *
     * 判定序（§12.1 S3 展开——逐步）：
     *   1. 载荷版本受理核对（≠currentPayloadVersion → InvalidInput，
     *      NFR-DEP-04）＋解码（Malformed/UnsupportedVersion → InvalidInput，
     *      逐项 SEL-BACKFILL-* 定位诊断）；
     *   2. 域校验：参考系词表、轴身份/重复、目录锁定引用与基线闭包一致
     *      （baseSnapshot.objectRefs 存在 (lockObject, lockVersion) 且
     *      一致——§12.1 S3"候选/目录版本存在"的基线侧判定；失配 →
     *      InvalidInput）；"候选存在"的目录侧核对归组装期（快照查找，
     *      本处不重复持有快照——单一来源）；
     *   3. 合成断言：逐轴 synthesizeAxisBodyProperties（组装期已预演，
     *      此处复核——载荷可能经不可信通道改写，断言以解码值为准）——
     *      任一轴断言失败（MDL-06①～③）→ HardAssert（多轴整体原子：
     *      任一轴失败＝整体失败，§12.2 纪律 3）；
     *   4. 计划组装：恰一 ObjectWrite（基线闭包已存在 sel-device-backfill
     *      对象→继承其 oid 改版；否则 objectId 空＝project S6 取号新建），
     *      载荷＝encodeBackfillRecordObject（合成结果＋条目＋复算提示）；
     *      摘要＝中文命令摘要（轴数/目录版本/复算提示——随修订持久化）。
     *
     * @param envelope     [in] 命令信封（载荷字节域所有）
     * @param baseSnapshot [in] 基线修订视图（值快照——命令槽保证 prepare
     *                     期间不受其他提交影响）
     * @param out          [out] 计划产出（Planned 时有效）
     * @param diags        [out] 逐项诊断（拒绝定位——SEL-BACKFILL-* 码）
     * @return 计划三态（值面）
     *
     * @note 纯计算（零副作用、零外部服务调用）；可重入；确定性。
     */
    virtual BackfillPlanOutcome planFromEnvelope(
        const project::CommandEnvelope& envelope,
        const project::RevisionView& baseSnapshot,
        project::CommandPlan& out,
        std::vector<core::DiagnosticRecord>& diags) const = 0;
};

/**
 * @brief 回填命令处理器实现（无状态——L5 装配注册、进程期复用）。
 *
 * prepare 直通转发 planFromEnvelope（HandlerContext 零调用——本处理器
 * 无需取号〔对象 oid 经空值语义由 project 分配〕、无需补充查询〔基线
 * 判定全部基于 baseSnapshot 值快照〕；文件头"依赖形态登记"①）。
 */
class DeviceBackfillCommandHandler final : public IDeviceBackfillCommandHandler {
public:
    DeviceBackfillCommandHandler() = default;

    /// 注册 token（kBackfillCommandToken——无点，P-SEL-3 保守形态）。
    [[nodiscard]] std::string commandType() const override;

    /// 受理载荷版本（kBackfillPayloadFormatVersion）。
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override;

    /// 计划内核（判定序见接口注释）。
    BackfillPlanOutcome planFromEnvelope(
        const project::CommandEnvelope& envelope,
        const project::RevisionView& baseSnapshot,
        project::CommandPlan& out,
        std::vector<core::DiagnosticRecord>& diags) const override;

    /// project 侧 prepare 入口（S3 调用——直通转发内核；ctx 零调用）。
    project::PrepareOutcome prepare(project::HandlerContext& ctx,
                                    const project::CommandEnvelope& envelope,
                                    const project::RevisionView& baseSnapshot,
                                    project::CommandPlan& out,
                                    std::vector<core::DiagnosticRecord>& diags) override;
};

}  // namespace sdurws::ird::selection

#endif  // IRD_SELECTION_BACKFILL_HPP
