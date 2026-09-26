/**
 * @file   Commands.hpp
 * @brief  会话姿态写入口（KinSessionPose——KIN-06/AT-04）与设默认命令门面
 *         （IKinematicsCommandHandler——KIN-14/AT-27）。
 *
 * 设计依据：
 *   - units/kinematics.md §3.3（公共头布局表 Commands.hpp 行——本头为该行
 *     的 T08 落位面）、§4.5（身份外元素表"会话姿态"行——零修订/零失效/
 *     不入缓存身份的单元侧保证）、§9.2（IKinematicsCommandHandler 接口
 *     原文——两方法签名与 @pre/@post 权威）、§9.7（跨域命令归宿声明
 *     D-KIN-5——组装 modeling `apply-robot-design` 增量载荷经①端口提交，
 *     处理器/断言/inverse 归 modeling）、§9.8（L-K4 会话姿态/L-K5 TCP 回
 *     填/L-K9 设默认/L-K11 只读拒绝四条界面数据流的本单元数据面）
 *   - ARCHITECTURE.md §7.7（会话态承载归 ui——SelectionModel/
 *     DraftController 管辖；本单元只提供写入口与"身份外"保证，不承载
 *     会话状态的所有权）、§7.11（产生修订的命令一律经①命令端口）
 *   - 需求 KIN-06（双击只改会话姿态，不改模型、不触发失效）、KIN-14
 *     （当前 TCP/设备设为项目默认——经命令产生新修订，不破坏既有引用）、
 *     AT-04（会话预览不产生修订）、AT-27（设默认 TCP 经命令；V-18 会话
 *     姿态不入身份的本单元侧执行面）
 *   - 治理裁决：O-35（2026-09-22 已裁决无点命令形态——本门面提交的
 *     modeling 命令 token `apply-robot-design` 即无点形态；§9.8
 *     `kinematics.*` 七条是 ui CommandId 词表登记，非 project 命令语法，
 *     本头不定义任何自有 project 命令 token——D-KIN-5）、P-KIN-5（KIN-14
 *     命令归宿 modeling——知会登记义务随单元卡 §14.6 兑现）、P-KIN-7
 *     （对端契约未冻结——载荷框架字节与 token/版本常量以 modeling 卡
 *     v0.15 现行文本为基线，漂移即本头测试金标失败显性化，增量同步义务
 *     随卡登记）
 *
 * 背景说明（第一读者须知——为什么门面长这样）：
 *   1. 会话姿态（双击任务/候选、可视化点回写、复位 Home）在架构上属 ui
 *      会话态（ARCH §7.7），kinematics 不拥有其存储；但 L-K4 数据流的
 *      "写入口与身份外保证"由本单元供给：KinSessionPose 是一个不携带任何
 *      端口/事件/身份语义的纯值容器，ui 将其嵌入会话对象并调用其写入口。
 *      它的结构性保证＝**对修订/失效/缓存身份系统不可见**：没有任何评估
 *      请求身份（IkRequestIdentity——KinTypes.hpp，D-KIN-4"本单元不读
 *      任何会话状态"）的字段来自本类型，求解身份只由显式输入构成。
 *   2. 设默认 TCP/设备（KIN-14）的写入对象是 modeling 的
 *      robot-design.defaultTcp 字段（§9.7），命令处理器/断言/inverse 全
 *      归 modeling（D-KIN-5/P-KIN-5）——本单元的职责严格限于"组装载荷＋
 *      经①端口提交＋结果回显"。R-1 禁业务域互链（不 include modeling/
 *      project 头，卡 §3.2"切片字节＋各自卡 canonical 语义约定"纪律），
 *      因此①端口以本单元自有最小投影接口（IKinProjectCommandGateway）
 *      注入：宿主（L5 装配）以适配器转译到真 ProjectCommandService——
 *      O-37 宿主注入裁决的同款纪律（IKinRuntimeView/IKinCollisionScene-
 *      Source 先例）。
 *   3. 载荷字节面分两层：命令**框架**（magic/版本/槽布局——modeling 命令
 *      载荷 §9.3 文档化格式）由本单元按卡面语义组装（组装义务在门面，
 *      §9.7 原文）；根对象**内容字节**（RobotDesign canonical 编解码）保
 *      持 modeling 单一权威（NFR-MNT-03/04），经 RootDesignPatcher 注入
 *      缝由宿主以 modeling 公共 RobotDesignCodec 实现——本单元零第二套
 *      对象编解码器。
 *
 * 线程安全：KinSessionPose 非线程安全（仅 UI 线程访问——ARCH §7.7 会话
 *   态界面线程纪律）；KinematicsCommandHandler 自身无可变状态（并发调用
 *   安全性随注入面的线程契约：gateway 实现须并发安全——真①端口内部命
 *   令执行槽串行，project.md §6.1）；全部值类型并发只读安全。
 * 确定性：同输入→同信封字节（框架编码定宽小端＋字段定序——NFR-COR-02；
 *   提交结果随①端口确定性与存储状态，非本单元承诺面）。
 */

#ifndef IRD_KINEMATICS_COMMANDS_HPP
#define IRD_KINEMATICS_COMMANDS_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>    // DiagnosticRecord（诊断回显载体）
#include <sdurws/ird/core/Identity.hpp>    // ObjectId/BranchId/RevisionId（强身份）
#include <sdurws/ird/kinematics/Errors.hpp>  // KinematicsError（基线读取错误轨）
#include <sdurws/ird/kinematics/KinTypes.hpp>  // TcpRef（设默认 TCP 的入参值）
#include <sdurws/ird/runtime/Errors.hpp>   // runtime::Expected（非异常出口载体）

namespace sdurws::ird::kinematics {

// =====================================================================
// KinSessionPose——会话姿态写入口（L-K4/KIN-06/AT-04；§4.5 身份外元素）
// =====================================================================

/**
 * @brief 会话姿态的纯值容器（ui 会话态的本单元侧写入口——§9.8 L-K4 行）。
 *
 * 背景说明（为什么它必须"什么都不会"）：KIN-06 的语义是"双击只改变会话
 * 姿态，不修改设计模型、不触发结果失效"。本类型以结构方式兑现该保证：
 *   - 零端口：不持有命令/查询/事件/评估器任何注入面——写操作在类型层面
 *     **不存在产生修订或失效的通道**（V-18 观测点"修订计数"为零的结构
 *     根基；测试以计数网关证明零提交）；
 *   - 零身份耦合：本类型的值不是任何评估请求身份的字段（D-KIN-4——
 *     IkRequestIdentity 的 referenceQ 由调用方显式装配，装配层若想以会话
 *     姿态作初值，必须显式读取再显式传入；身份因此只随显式输入变化，
 *     V-18 观测点"requestIdentity 不变（referenceQ 未变）"）。
 *
 * 所有权与承载（ARCH §7.7）：会话状态所有权归 ui——ui 将本容器嵌入其会
 * 话对象（SelectionModel/DraftController 管辖域），本单元不登记、不持久
 * 化、不跨会话保留。复位 Home 的 Home 值由调用方供给（ui 从命名位姿集
 * 保留键 homeConfiguration 读取——MDL-17 保留键纪律，建模侧已登记"KIN-06
 * 复位走会话命令零修订"；本单元不消费 requirements/modeling 对象）。
 *
 * 单位与坐标系：关节向量逐自由度 SI（转动 rad／移动 m，§5.1 q 权威口径），
 * 链序与设备自由度一致——本类型不理解链结构，维度/有限性校验见方法注。
 *
 * 线程约束：**非线程安全——仅 UI 线程访问**（会话态界面线程纪律；
 * ARCH §7.7/§9.4 会话对象行同口径）。
 */
class KinSessionPose {
public:
    /**
     * @brief 写入会话姿态（L-K4 三入口的统一写点：双击任务/候选、可视化
     *        点回写）。
     *
     * @param q [in] 权威关节向量（rad／m；链序）。按值接收——容器持有
     *           自己的副本，调用方后续修改不影响会话态。
     *
     * @throws std::invalid_argument q 含非有限分量（NaN/±∞——NFR-COR-03
     *         "非有限拒绝入算"在会话面的同则执行：非法值不入会话态，不
     *         钳制不置零；空向量合法＝"清除并置空"以外的显式空姿态由
     *         clear() 表达，本方法接受空向量并存为已设置空集）
     *
     * 副作用：仅本容器状态；零修订、零失效、零事件（结构性保证见类注）。
     */
    void setJointConfiguration(std::vector<double> q);

    /**
     * @brief 复位 Home（L-K4 第三入口——UX-13"复位关节至 Home"会话命令
     *        的数据面）。
     *
     * 语义＝以调用方供给的 Home 构型覆写会话姿态（零修订——MDL-17/V-27
     * 建模侧登记的"KIN-06 复位走会话命令零修订"同则）。与
     * setJointConfiguration 分立命名是为了留痕与追溯（L-K4 行三入口在
     * 调用点可分辨），行为上等价于一次写入。
     *
     * @param homeQ [in] Home 构型（rad／m；链序——调用方从命名位姿集
     *              保留键读取，本单元不解析位姿集对象）
     *
     * @throws std::invalid_argument homeQ 含非有限分量（同上——NFR-COR-03）
     */
    void resetToHome(const std::vector<double>& homeQ);

    /// 清除会话姿态（回"未设置"态；isSet()=false、jointConfiguration()
    /// 为空向量——ui 据此回退到默认显示来源）。
    void clear();

    /// 是否已设置（false＝会话无姿态——ui 的"以当前 TCP 为目标"回填
    /// （L-K5）在此态下不得读值，改走编辑器缺省值）。
    bool isSet() const noexcept { return m_set; }

    /**
     * @brief 读会话姿态（L-K5"以当前 TCP 为目标"显示默认值回填的唯一
     *        读取点——读出的值仅作 ui 呈现默认值；求解身份不含会话，
     *        D-KIN-4/L-K5 原文）。
     *
     * @return 关节向量常引用（rad／m；未设置时为空向量——引用所指对象
     *         随本容器存活，调用方不接管所有权）
     */
    const std::vector<double>& jointConfiguration() const noexcept { return m_q; }

private:
    /// 会话姿态值（rad／m；未设置时恒空——与 m_set 双字段互斥表达）。
    std::vector<double> m_q;
    /// 已设置标记（bool 与空向量的区分面——"显式空姿态"与"未设置"语义
    /// 分离，ui 判定回填来源的依据）。
    bool m_set = false;
};

// =====================================================================
// ①端口投影类型——宿主注入面的值载体（R-1 不 include project 头）
// =====================================================================

/**
 * @brief 门面提交的 modeling 命令 token（§9.7——`apply-robot-design`，
 *        modeling 卡 §9.3 命令清单行 1 同串）。
 *
 * 本单元唯一书写的命令 token：值面权威归 modeling（其公共头常量本单元
 * 不可 include——R-1），此处以字面＋测试金标钉住（CommandFacadeContract-
 * Test 断言同串与无点语法）；modeling 侧 token 演进时本常量随 P-KIN-7
 * 增量同步义务更新。形态依据：O-35 裁决（2026-09-22，无点形态＝服从
 * project.md §4.4.4 冻结语法 ^[a-z0-9-]{3,64}）。
 */
inline constexpr std::string_view kFacadedModelingCommand = "apply-robot-design";

/**
 * @brief 门面组装的 modeling 命令载荷格式版本（modeling 卡 §9.3
 *        kCommandPayloadVersion＝2 同值——WP-13-T10 载荷 v2：新增
 *        removals 段；v1 拒收＝NFR-DEP-04）。
 *
 * 值面权威归 modeling（同上——R-1 不 include，测试金标钉住）。框架编码
 * （encodeKinApplyRobotDesignPayload）随该版本产出 v2 布局（含 removals
 * 空段计数）；modeling 版本演进时本常量与框架编码同步升版。
 */
inline constexpr std::uint32_t kModelingCommandPayloadVersion = 2;

/**
 * @brief 根对象槽的对象类型 token（modeling 卡 §4.2 `robot-design` 同串
 *        ——kRobotDesignObjectType 同值面；R-1 不 include，测试金标钉住）。
 */
inline constexpr std::string_view kRobotDesignObjectTypeToken = "robot-design";

/**
 * @brief 设默认提交的基线事实（②端口在门面处的投影值——宿主适配器从
 *        查询端口与权威元数据取数组装）。
 *
 * 背景说明：门面组装增量载荷需要三个事实——往哪条分支提交（branch）、
 * 以哪个修订为基线（tipRevision——显式绑定当前 tip，满足 §9.2 @pre"快照
 * 基线=当前 tip"）、写哪个对象的什么字节（rootObjectId＋rootObjectBytes
 * ——根对象 canonical 字节**透传**，本单元不解码：RobotDesign 编解码
 * 权威在 modeling，见文件头注 3）。字节的当前性由宿主适配器保证（取数
 * 与提交之间不引入二次读取——与①端口 S2 过期基线校验互为防线）。
 *
 * 线程安全：纯值类型。
 */
struct KinProjectBaseline {
    /// 目标分支（brn- 规范文本——①端口 S1 校验其存在于权威元数据）。
    core::BranchId branch{};
    /// 当前 tip 修订（rev-；作为提交信封 expectedRevision——并发校验由
    /// ①端口 S2 承担）。nullopt＝适配器未取得 tip（空分支态——提交按
    /// ①端口"缺省=tip"语义解析）。
    std::optional<core::RevisionId> tipRevision;
    /// robot-design 根对象身份（obj-）。nullopt＝闭包无根对象（无设备
    /// ——NoDevice 轨，门面产 KIN-NO-DEVICE 诊断，不提交）。
    std::optional<core::ObjectId> rootObjectId;
    /// 根对象 canonical 字节（modeling Codec 格式——**不透明透传**；
    /// 本单元零解码零改写，设默认 TCP 时仅作为补丁缝的输入）。
    std::vector<std::uint8_t> rootObjectBytes;
};

/**
 * @brief 命令信封（①端口投影——与 project CommandEnvelope 字段同构；
 *        R-1 下由宿主适配器逐字段转译为真信封提交）。
 *
 * 背景说明（§6.4 载荷三元组——project 侧权威语义的投影复述）：载荷身份
 * ＝commandType（处理器注册 token）＋payloadFormatVersion（处理器自有
 * 格式版本戳）＋payloadCanonical（域 canonical 字节，project 透传存储
 * 不解释——D-10）。本单元对三元组的取值：token＝kFacadedModelingCommand、
 * 版本＝kModelingCommandPayloadVersion、字节＝encodeKinApplyRobotDesign-
 * Payload 产出。
 *
 * 线程安全：纯值类型。
 */
struct KinCommandEnvelope {
    /// 目标分支（brn-——来自基线事实，门面不改写）。
    core::BranchId branch{};
    /// 期望基线修订（rev-；来自基线 tip——①端口 S2 过期校验面）。缺省
    /// ＝提交期解析 tip（project §6.2 语义）。
    std::optional<core::RevisionId> expectedRevision;
    /// 处理器注册 token（恒 kFacadedModelingCommand——D-KIN-5：门面只提
    /// 交 modeling 命令，无自有 token）。
    std::string commandType;
    /// 载荷格式版本（恒 kModelingCommandPayloadVersion）。
    std::uint32_t payloadFormatVersion = 0;
    /// 域 canonical 载荷字节（框架编码产出——不透明）。
    std::vector<std::uint8_t> payloadCanonical;
};

/**
 * @brief 提交结果回显（§9.2 门面 @post 的值承载——"成功=新修订；失败=
 *        零修订＋诊断"）。
 *
 * 背景说明（为什么只分两态）：①端口真结果的四态细分（Rejected/
 * Aborted/Failed 及其封闭理由词表）是 project 的权威语义（PA-1），真
 * 界面呈现应直接消费 CommandResult（宿主装配层持有）；门面的契约只需
 * 区分"产生了恰好一个新修订"与"零修订"，失败原因经 diagnostics 透传
 * （携带①端口/处理器产出的稳定码诊断记录——PRJ- 与 MDL- 前缀的门卫与
 * 断言码，本单元不自造不改写）。避免在 kinematics 复制①端口理由词表
 * （NFR-MNT-03 单一权威——第二份词表即漂移面）。
 *
 * 线程安全：纯值类型。
 */
struct CommandSubmission {
    /// 终态两分（语义见类注）。
    enum class Kind { Committed, NotCommitted };

    /// 终态（缺省 NotCommitted——"未成功"是安全缺省，防调用方漏检时误
    /// 当成功消费）。
    Kind kind = Kind::NotCommitted;
    /// 新修订身份（rev-；仅 Committed 态非空——恰好一个新修订的后置）。
    std::optional<core::RevisionId> newRevision;
    /// 诊断回显（零修订时的失败定位：门面自产的 KIN-* 码记录＋①端口/
    /// 处理器透传记录；Committed 态通常为空——①端口旁路诊断不属本契约）。
    std::vector<core::DiagnosticRecord> diagnostics;

    /// 便捷判定（调用方可读性——避免裸比对枚举）。
    [[nodiscard]] bool committed() const noexcept { return kind == Kind::Committed; }
};

// =====================================================================
// IKinProjectCommandGateway——①/②端口投影注入面（O-37 宿主注入纪律）
// =====================================================================

/**
 * @brief 设默认门面的注入端口（真①端口 ProjectCommandService 与②查询
 *        取数在 R-1 约束下的本单元投影——宿主（L5 装配）以适配器实现，
 *        O-37 裁决同款纪律：宿主构建适配器、门面只依赖自有最小接口）。
 *
 * 语义边界（不新增对端语义）：
 *   - fetchBaseline()：基线事实取数（②端口投影——分支/tip/根对象身份
 *     与字节；见 KinProjectBaseline 注）。错误轨＝KinematicsError{NoDevice}
 *     （闭包无 robot-design 根——"无可用设备"业务出口，§9.6 KIN-NO-DEVICE
 *     同语义）；适配器自身契约违约（内部状态缺失等编程错误）走异常
 *     fail-fast，不入错误轨（Collision.hpp assembleCollisionScene 先例）。
 *     约束：并发只读安全；取数不产生任何修订。
 *   - submit(envelope)：信封提交（①端口投影——S1~S7 编排语义全部在对
 *     端：writable/未知命令/过期基线/载荷校验/确认/双编译/事务）。返回
 *     两态回显（CommandSubmission——细分理由在对端语义内，诊断透传）。
 *     约束：实现须并发安全（真①端口内部命令执行槽串行，project §6.1）。
 *
 * 生命周期与所有权：宿主持有（非 owning——门面以裸指针引用，构造注入，
 *     存活期须覆盖门面使用期）。
 */
class IKinProjectCommandGateway {
public:
    virtual ~IKinProjectCommandGateway() = default;

    /// 基线事实取数（②端口投影——见类注；确定性：同存储态同返回值）。
    [[nodiscard]] virtual runtime::Expected<KinProjectBaseline, KinematicsError>
        fetchBaseline() const = 0;

    /// 信封提交（①端口投影——见类注；非 Committed 态恒零修订）。
    virtual CommandSubmission submit(const KinCommandEnvelope& envelope) = 0;
};

// =====================================================================
// RootDesignPatcher——根对象字节补丁注入缝（modeling 编解码权威保持）
// =====================================================================

/**
 * @brief 根对象字节补丁函数类型（设默认 TCP 的内容字节变更面——宿主以
 *        modeling 公共 RobotDesignCodec 实现：decode 基线根字节→写
 *        defaultTcp=(tcp.toolObject, tcp.tcpKey)→encode 新根字节）。
 *
 * 背景说明（为什么是注入缝而不是本单元实现）：RobotDesign canonical 编
 * 解码是 modeling 的单一权威（NFR-MNT-03/04；1383 行级编解码器不在本单
 * 元复制第二份——§3.2"不 include 其头"纪律下的对端字节消费走宿主承载）。
 * 本单元只负责把补丁产物放入命令载荷框架的正确槽位（§9.7"组装增量载荷"
 * 的框架半区）。
 *
 * 契约：
 *   - 入参 baselineRootBytes：基线根对象 canonical 字节（透传自基线事
 *     实——补丁实现不得假设其可变性，按只读消费）；
 *   - 入参 defaultTcp：目标默认 TCP（kin::TcpRef——toolObject＝tool-
 *     definition 对象身份、tcpKey 空串＝canonical TCP；闭包存在性校验
 *     归 modeling 命令断言 assertDefaultTcp，本缝不预校验——D-KIN-5
 *     "断言归 modeling"）；
 *   - 返回：补丁后的新根对象 canonical 字节（同字节确定性不要求跨实现
 *     ——编码权威在 modeling Codec）；nullopt＝补丁失败（基线字节不可
 *     解码/编码失败——数据侧错误，门面转 KIN-ROOT-BYTES-ILLEGAL 诊断，
 *     零修订零提交）。
 *
 * 线程安全：实现须可重入（纯函数建议——门面不串行化补丁调用）。
 */
using RootDesignPatcher = std::function<std::optional<std::vector<std::uint8_t>>(
    const std::vector<std::uint8_t>& baselineRootBytes, const TcpRef& defaultTcp)>;

// =====================================================================
// 载荷框架编码（§9.7"组装增量载荷"的框架半区——modeling 载荷 v2 布局）
// =====================================================================

/**
 * @brief 组装 apply-robot-design 单根槽载荷的 canonical 字节（§9.7 门面
 *        组装义务的字节面；modeling 命令载荷 v2 框架布局的兼容编码）。
 *
 * 编码布局（modeling 卡 §9.3 文档化格式——小端、长度前缀、无填充；与
 * modeling encodeCommandPayload 的 Apply 单根槽形态逐字节兼容）：
 * @code
 *   magic "IRDMCP2"（7 字节 ASCII——v2 魔数与版本尾数同步）
 *   ‖ u32 payloadFormatVersion = 2（小端）
 *   ‖ u32 mode = 0（Apply）
 *   ‖ u32 objectCount = 1
 *   ‖ 对象槽×1：
 *       u8  allocateNew = 0（既有对象替换——根对象身份稳定）
 *       ‖ u32 oidLength ‖ 根对象 ObjectId 规范文本（"obj-<32hex>" ASCII）
 *       ‖ u32 tokenLength ‖ "robot-design"（ASCII）
 *       ‖ u32 bytesLength ‖ 根对象 canonical 字节（透传，本函数不改写）
 *   ‖ u32 removalsCount = 0（v2 removals 空段——设默认零引用移除）
 * @endcode
 *
 * 漂移防线（P-KIN-7）：本函数的输出被契约测试以**测试内独立解码器**回
 * 解核对（槽字段/计数/尾段），并以金标字节向量逐位钉住——modeling 框架
 * 版本演进（magic/版本/布局）时金标失败显性化，走卡面增量同步。
 *
 * @param rootObjectId    [in] 根对象身份（obj-；槽 oid 规范文本来源）
 * @param rootObjectBytes [in] 根对象 canonical 字节（透传入槽；本函数
 *                        不解码不改写——内容权威在 modeling）
 * @return canonical 载荷字节（同输入逐字节相同——NFR-COR-02；纯位组装
 *         无格式化）
 *
 * 纯函数；线程安全；不抛（字节组装无失败出口——长度上限 2^32 内由调用
 * 方契约保证：根对象字节超出 u32 长度域属调用方数据违约，静默截断不存在
 * ——长度域以 u32 承载与 modeling 框架一致）。
 */
std::vector<std::uint8_t> encodeKinApplyRobotDesignPayload(
    const core::ObjectId& rootObjectId, const std::vector<std::uint8_t>& rootObjectBytes);

// =====================================================================
// IKinematicsCommandHandler——设默认命令门面（§9.2 接口原文）
// =====================================================================

/**
 * @brief 设默认 TCP/设备命令门面（KIN-14——§9.2 接口契约的 T08 落位）。
 *
 * 契约（§9.2 原文口径，两方法共用）：
 *   - @pre writable（只读模式的拒绝由①端口 S1 产生——NotWritable＋门卫
 *     稳定码诊断经回显透传，本单元不自判 writable：PA-1 可写性权威归
 *     project）；快照基线=当前 tip（基线事实由注入端口供给，门面绑定
 *     fetchBaseline 返回的 tip——过期提交由①端口 S2 拒绝＋诊断）。
 *   - @post 成功＝新修订（不破坏既有引用——modeling 命令断言保证：
 *     assertReferenceProtection/assertDefaultTcp 等在处理器 prepare 内
 *     执行，D-KIN-5）；失败＝零修订＋诊断（门面自产 KIN-* 码或透传对端
 *     诊断）。
 *   - @线程 UI 线程调用（§9.2 原文——异步等待由宿主装配层承载，门面
 *     同步返回回显值）。
 *   - @所有权 gateway/patcher 均非 owning（构造注入，存活期覆盖使用期）。
 *
 * 合法与非法调用对照（§9.2 行文）：合法＝writable 会话＋闭包内 TCP 引用
 * ＋本项目设备根身份；非法＝只读会话提交（①端口拒绝＋诊断回显）、把非
 * 本项目设备根的对象传给 setProjectDefaultDevice（调用方契约违约——
 * fail-fast）、补丁缝未装配即调 TCP 路径（装配违约——fail-fast）。
 */
class IKinematicsCommandHandler {
public:
    /// 虚析构：实现随宿主装配多态销毁的常规保障。
    virtual ~IKinematicsCommandHandler() = default;

    /**
     * @brief 把 TCP 设为项目默认（§9.2 原文签名——组装 modeling
     *        `apply-robot-design` 增量载荷：单根槽，根字节＝补丁缝对基
     *        线根写入 defaultTcp 后的产物）。
     *
     * 执行序（各步失败即返回；除提交步外全部零提交零修订）：
     *   1. 补丁缝未装配（空 std::function）＝装配违约 →
     *      std::invalid_argument（fail-fast——DhConverter 先例：缺依赖
     *      到达即编程错误，不产半提交）；
     *   2. fetchBaseline 错误（闭包无设备根）→ NotCommitted＋
     *      KIN-NO-DEVICE 诊断（零提交——"无可用设备"无设默认意义）；
     *   3. 基线根身份缺失（防御面，与 2 同语义轨）→ NotCommitted＋
     *      KIN-NO-DEVICE；
     *   4. 补丁缝返回 nullopt（基线根字节不可解码/编码失败——数据侧）
     *      → NotCommitted＋KIN-ROOT-BYTES-ILLEGAL 诊断（零提交）；
     *   5. 组装信封（token/版本/框架编码见对应常量与函数注；expected-
     *      Revision＝基线 tip）并经网关提交 → 回显透传（Committed 携带
     *      新修订；NotCommitted 透传对端诊断——只读/过期/断言拒绝等）。
     *
     * @param tcp [in] 目标默认 TCP（toolObject＝tool-definition 对象身份、
     *             tcpKey 空串＝canonical TCP；闭包存在性由 modeling 命令
     *             断言在 prepare 内校验——引用悬空时提交被拒（零修订）并
     *             透传 MDL-* 断言诊断，本方法不预校验闭包）
     * @return 提交回显（两态——见 CommandSubmission 注）
     *
     * @throws std::invalid_argument 补丁缝未装配（步骤 1——装配违约）
     *
     * 确定性：同（基线，tcp，补丁实现）→同信封字节（框架编码确定性；
     * 内容字节确定性随 modeling Codec）。
     */
    [[nodiscard]] virtual CommandSubmission setProjectDefaultTcp(const TcpRef& tcp) = 0;

    /**
     * @brief 把设备设为项目默认（§9.2 原文签名——组装 modeling
     *        `apply-robot-design` 增量载荷：单根槽，根字节＝基线根字节
     *        原样（设备指定语义））。
     *
     * 语义登记（R1 单设备数据模型的落地面，随单元卡 §14.6 v0.8）：R1
     * 每方案分支恰一个 robot-design 根对象＝项目设备本体（runtime 编译
     * 唯一 SerialDevice；多设备目标选择属 R2/MDL-18 对端）。因此"把
     * robotOid 指名的设备设为项目默认"在本数据模型下＝**经命令重写根
     * 对象产生设备指定修订**（KIN-14"经命令产生新修订"字面兑现；内容
     * 字节不变、既有引用零触碰——"不破坏既有引用"由数据面平凡成立＋
     * modeling 断言双重保证）。KIN-14"后续任务、计算与报告使用新默认"
     * 在 R1 下由"唯一设备即默认"平凡满足。
     *
     * 执行序：fetchBaseline（无根→NotCommitted＋KIN-NO-DEVICE）→
     * robotOid 与基线根身份核对（不属本项目设备根＝调用方契约违约→
     * fail-fast）→ 组装信封（槽字节＝基线字节原样）→ 提交回显透传。
     *
     * @param robotOid [in] 待设为默认的设备（robot-design 根）对象身份
     * @return 提交回显（两态——见 CommandSubmission 注）
     *
     * @throws std::invalid_argument robotOid 不是本项目 robot-design 根
     *         身份（跨项目/跨分支/部件对象误传——编程错误，无业务出口）
     *
     * 确定性：同（基线，robotOid）→同信封字节。
     */
    [[nodiscard]] virtual CommandSubmission
        setProjectDefaultDevice(const core::ObjectId& robotOid) = 0;
};

// =====================================================================
// KinematicsCommandHandler——门面产品实现（接口＋无状态实现的同头先例：
// FkEvaluator/Collision.hpp 形态）
// =====================================================================

/**
 * @brief 门面产品实现（构造注入网关与补丁缝；两方法执行序见接口注）。
 *
 * 生命周期与所有权：宿主（L5 装配/ui 会话装配）构造并持有；gateway 与
 * patcher 均非 owning——存活期须覆盖本对象使用期。
 *
 * 线程安全：本类无可变状态（两方法只读成员）——并发安全性随注入面
 * （gateway 并发安全即可；真①端口内部命令执行槽串行）。
 */
class KinematicsCommandHandler final : public IKinematicsCommandHandler {
public:
    /**
     * @brief 构造（注入两个对端缝——装配期一次性完成）。
     *
     * @param gateway [in] ①/②端口投影网关（非 owning；空指针＝装配违约
     *                ——构造即 fail-fast，不留"半装配"对象）
     * @param patcher [in] 根对象字节补丁缝（可空——仅 setProjectDefaultTcp
     *                需要；空函数对象时 TCP 路径到达即
     *                std::invalid_argument（装配违约），设备路径不受影响
     *                ——设备路径零补丁消费）
     *
     * @throws std::invalid_argument gateway 为空
     */
    KinematicsCommandHandler(IKinProjectCommandGateway* gateway,
                             RootDesignPatcher patcher);

    [[nodiscard]] CommandSubmission setProjectDefaultTcp(const TcpRef& tcp) override;
    [[nodiscard]] CommandSubmission
        setProjectDefaultDevice(const core::ObjectId& robotOid) override;

private:
    /// 共同尾部：以给定根槽字节组装信封并提交（token/版本/分支/tip 绑定
    /// 的唯一书写点——两方法共享，防装配漂移）。
    CommandSubmission assembleAndSubmit(const KinProjectBaseline& baseline,
                                        std::vector<std::uint8_t> rootObjectBytes);

    /// ①/②端口投影网关（构造期非空——非 owning，宿主保证存活；非 const
    /// 指针＝submit 是端口的非 const 操作面，fetchBaseline 经其 const 调用）。
    IKinProjectCommandGateway* m_gateway;
    /// 根对象字节补丁缝（可空——TCP 路径装配违约面，见构造注）。
    RootDesignPatcher m_patcher;
};

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_COMMANDS_HPP
