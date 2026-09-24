/**
 * @file   CommandHandlers.hpp
 * @brief  建模命令处理器族——IModelingCommandHandler 基类（§9.4.8，
 *         project ICommandHandler 实现）＋五命令处理器（§9.3 命令清单）
 *         ＋AssertionSuite（§9.4.4 指定的就绪校验/prepare 共用断言实现）
 *         ＋命令载荷模型与编解码（§9.3 prepare 管线 decode 段）。
 *
 * 设计依据：
 *   - units/modeling.md §9.3（建模命令处理器族——五命令清单表
 *     commandType/requiresDualCompile/断言/inverse、prepare 管线图
 *     decode→基线重建→断言分域→CommandPlan 组装、快照式逆命令、命令
 *     上下文与交互约束）、§9.4.4（AssertionSuite——"两处共用同一断言
 *     实现，不得出现两套判定"）、§9.4.8（IModelingCommandHandler 签名
 *     与线程/副作用约束）、§9.5（T08 行九码——本头 AssertionSuite 为其
 *     唯一产码点）、§8.2（级别语义）、§9.1（编译失败回传口径）
 *   - units/project.md §5.3（ICommandHandler/HandlerContext/CommandPlan/
 *     PrepareOutcome 契约——prepare 三态；"处理器在 prepare 中完成策略
 *     校验：经④端口读 EngineeringPolicySet 阈值，超限产出
 *     ConfirmableFinding——project 不持有 4π 数值"）、§6.5（L5 装配注册
 *     ——防反向链接）、§6.9（快照式逆命令——撤销＝提交 restore 变体
 *     产生新修订，历史不改写）、§6.7（确认绑定四元组——project S4/S6
 *     职责，处理器只产出待确认集）
 *   - units/policy.md §9.4（IJointLimitEvaluator——比较型事实供给，判定
 *     权归处理器）、§4.4（JointThresholds——阈值唯一来源，行程阈值仅适
 *     用有限限位旋转关节）、§7.4（T＞L 才超限——比较边界）
 *   - 需求 MDL-06（断言分域就地阻止＋双编译原子性）、MDL-12（continuous
 *     工程工作范围）、SA-15（可确认诊断放行）、ARC-01（命令原子产生
 *     修订）、ARC-05（阈值唯一权威）、D-MDL-9（inverse 载荷＝受影响对象
 *     前一 (oid,cv) canonical 字节集）、NFR-MNT-04（判定不重复）、
 *     NFR-DEP-04（未知载荷版本拒绝）、CON-03（资源固化激励）
 *   - 任务契约 tasks/foundation/WP-13-T08.json acceptance 1～5（O-35
 *     无点 token 裁决的执行点——本头五个 token 即最终形态）
 *
 * 背景说明（prepare 管线为什么长这样——第一读者须知）：project 命令
 * 服务拥有事务/串行/确认编排/修订（NFR-MNT-04——不是转发包装器），
 * "怎样变更才合法"归业务域。处理器在 S3 被 project 调用，只做四件事：
 *   ① decode——把不透明载荷字节解释为域内写入意图（框架破损/版本不
 *     受理→RejectedInvalidInput，project 转 invalid-payload 拒绝）；
 *   ② 基线重建——从 baseSnapshot 闭包重建建模工作集（防御性复核：载荷
 *     声明的期望修订与实际基线不一致＝调用方契约违约 fail-fast——S2 已
 *     拦截过期基线，此处是防线纵深而非业务拒绝轨）；
 *   ③ 断言分域——AssertionSuite 对候选状态执行物理合法性断言（硬断言
 *     失败→RejectedHardAssert＋逐项定位诊断；物性缺失→Warning 预告；
 *     策略行程超限→ConfirmableFinding 待确认——SA-15 确认编排归 S4）；
 *   ④ 计划最终化——requiresDualCompile＋confirmableFindings＋inverse
 *     快照逆载荷（受影响对象前一 (oid,cv) canonical 字节集——D-MDL-9）
 *     ＋中文命令摘要（对象/字段/确认留痕/资源状态）。
 * 真正的写入/提交/编译编排（S4～S7）全部归 project——处理器除
 * ctx.objectId() 取号外**零副作用**，一切拒绝/失败路径零修订（MDL-06
 * 原子性）；双编译失败（S5）由 project 判 Failed(compile-failed)，HEAD
 * 与修订计数不变（§6.6——原子性来自顺序而非撤销）。
 *
 * O-35 裁决执行（契约 knownPitfalls）：命令 token 采用无点形态，服从
 * project.md §4.4.4 冻结语法 ^[a-z0-9-]{3,64}（DTB §4 2026-09-22 裁决
 * "采用无点形态＝服从现行冻结语法"）；点分迁移属破坏性变更须同步
 * schemaVersion，本版不适用。
 *
 * T09 增量（WP-13-T09，§7.6——单元卡 §15 v0.10 登记）：权威切换为
 * apply-robot-design 的**权威切换变体**（独立领域命令：prepare 内执行
 * 转换判定＋等价验证，验证失败不产生修订）。落位＝prepare 公共段的
 * 权威切换门（③.5 段：正向载荷 ∧ 基线 Explicit ∧ 候选 StandardDH 触发；
 * C-6 先决断投影＝载荷恰一根槽＋候选除权威侧字段外与基线逐字段一致；
 * 转换判定与 FK 对照经 HandlerServices 新增的 dhConverter/dhCompileProbe
 * 注入——未装配时切换变体到达即装配缺陷 fail-fast）。子类钩子契约
 * （Planned|RejectedInvalidInput 两态）不变——门在基类，五命令行为
 * 零变化（触发条件对非切换载荷恒假）。
 *
 * T10 增量（WP-13-T10，§4.8"删除＝引用移除"＋V-04/V-05——单元卡 §14.6
 * v0.12 登记）：①命令载荷 v2——新增 removals 引用移除段（v1 拒收，
 * NFR-DEP-04 升级指引＋R-MDL-5 草稿短命数据口径）；②AssertionSuite 新增
 * assertDefaultTcp（defaultTcp 闭包级引用完整性——I-MDL-9/KIN-14/§8.2
 * L7，与就绪校验共用单一判定面）与 assertReferenceProtection（V-04 引用
 * 保护——MDL-REF-PROTECTED 比较型定位诊断）；③prepare 公共段接线：移除
 * 保护门先于其余断言（保护拒绝面精确定位），defaultTcp 校验随闭包引用
 * 断言执行；④apply-tool-definition/apply-scene-objects 移除面（引用移除
 * ＝仅根写入，对象字节与历史修订闭包保留——PA-2/CON-02；摘要附"可能
 * 存在外部引用"提示，悬空检测归 requirements 就绪校验——P-MDL-6）；
 * ⑤apply-named-poses 合并流（保留键保留——V-27 建模侧；关节序一一对应
 * ——§4.6）。钩子契约（Planned|RejectedInvalidInput 两态）不变：移除
 * 事实经 DecodeOutcome.removedRefs 陈述，保护判定统一归基类断言段。
 * 两枚 T10 行稳定码（MDL-REF-PROTECTED/MDL-READINESS-DEFAULT-TCP-
 * INCOMPLETE）随本任务在 §9.5 表尾登记（实现期增登先例——v0.6～v0.9）。
 *
 * 线程约束（§9.4.8 原文）：处理器由 ProjectCommandService 在命令执行
 * 线程串行调用，内部无需加锁；跨上下文共享实例时处理器状态视为不可变。
 * 合法调用：仅 project 命令服务（装配注册后由 registry 分发）；业务/UI
 * 直接调用 prepare＝契约违约。副作用边界：仅产出 CommandPlan＋diags
 * （§9.4.8"副作用：仅产出 CommandPlan"）＋ctx.objectId() 取号（PA-1
 * ——身份分配唯一归 project）。
 */

#ifndef IRD_MODELING_COMMANDHANDLERS_HPP
#define IRD_MODELING_COMMANDHANDLERS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>     // DiagnosticRecord/ConfirmableFinding/ComparativeFields
#include <sdurws/ird/core/Identity.hpp>     // ObjectId/RevisionId/ContentVersion
#include <sdurws/ird/modeling/DiagCodes.hpp>  // T08 行码常量（产码唯一书写点——禁字符串拼码）
#include <sdurws/ird/modeling/DhConvert.hpp>  // IDhExplicitConverter/CompileProbe（T09 权威切换门——§7.6）
#include <sdurws/ird/modeling/ObjectTypes.hpp>  // 五对象 token（命令对象路由）
#include <sdurws/ird/modeling/Parts.hpp>    // 四部件值模型（断言域与闭包视图）
#include <sdurws/ird/modeling/RobotDesign.hpp>  // 根对象值模型（断言域输入）
#include <sdurws/ird/modeling/Template.hpp>  // ModelingWorkingSet（基线/候选工作集——v0.9 闭包视图）
#include <sdurws/ird/policy/JointLimits.hpp>   // IJointLimitEvaluator/JointLimitQuery（行程比较）
#include <sdurws/ird/policy/PolicyPort.hpp>    // IPolicyProvider（④端口——策略解析）
#include <sdurws/ird/policy/PolicySet.hpp>     // EngineeringPolicySet（阈值唯一来源）
#include <sdurws/ird/project/CommandService.hpp>  // ICommandHandler/HandlerContext/CommandPlan/ObjectWrite
#include <sdurws/ird/project/QueryPort.hpp>    // RevisionView（基线闭包视图）

namespace sdurws::ird::modeling {

// =====================================================================
// 五命令 token（§9.3 命令清单——O-35 无点裁决的最终形态；唯一书写点）
// =====================================================================

/// @brief apply-robot-design——写入 robot-design 根对象（＋新增部件对象）；
///        requiresDualCompile=true（§9.3 表行 1——权威参数化对象入 WC/DWC）。
inline constexpr std::string_view kCmdApplyRobotDesign = "apply-robot-design";

/// @brief apply-tool-definition——写入 tool-definition（＋根引用表增量）；
///        requiresDualCompile=true（工具几何/物性入 WC/DWC——§9.3 表行 2）。
inline constexpr std::string_view kCmdApplyToolDefinition = "apply-tool-definition";

/// @brief apply-scene-objects——scene-object 批量（增/改；移除引用走根
///        对象编辑命令）；requiresDualCompile=true（碰撞几何变更——§9.3 表行 3）。
inline constexpr std::string_view kCmdApplySceneObjects = "apply-scene-objects";

/// @brief apply-named-poses——写入 named-pose-set；requiresDualCompile=
///        false（不进 Description——§9.3 表行 4；纯参考数据零编译影响）。
inline constexpr std::string_view kCmdApplyNamedPoses = "apply-named-poses";

/// @brief apply-drivetrain-design——写入 robot-drivetrain；requiresDual
///        Compile=true（ratio/摩擦/耦合入 Description——§9.3 表行 5）。
inline constexpr std::string_view kCmdApplyDrivetrainDesign = "apply-drivetrain-design";

// =====================================================================
// AssertionSuite——就绪校验与 prepare 共用的唯一断言实现（§9.4.4 指定）
// =====================================================================

/**
 * @brief 物理合法性断言套件（D-MDL-11/NFR-MNT-04 单一判定点——就绪校验
 *        L4/L5/L6/L10 与命令 prepare 断言的唯一共同实现）。
 *
 * 职责（§9.3 断言分域＋§8.2 分层表的判定面；本套件只做"值事实→定位
 * 诊断记录"的换轨，判定数学零重写）：
 *   - 物性硬断言①②③（已提供 m≤0/惯量非 SPD/三角不等式——连杆与工具
 *     共用 BodyData 单一实现，特征值数学唯一来自 src/InertiaMath.hpp，
 *     与 I-MDL-5 不变量同源）→ MDL-ASSERT-MASS-NONPOSITIVE/
 *     MDL-ASSERT-INERTIA-NOT-SPD/MDL-ASSERT-INERTIA-TRIANGLE（就地阻止
 *     ＋精确定位：subject=ObjectId＋localName＋比较型三要素——DTB 禁止
 *     项的对位实现）；缺失（NotProvided）不触发硬断言→
 *     MDL-READINESS-PHYSICS-MISSING Warning 预告（DataInsufficient 降级
 *     ——V15-01，§9.3"物性缺失→不阻断，转 Warning 诊断随计划留痕"）；
 *   - 限位硬断言④（qmin≥qmax；continuous 工作范围未确认/非有限）→
 *     MDL-ASSERT-LIMIT-INTERVAL/MDL-ASSERT-RANGE-NOT-FINITE；已确认
 *     有限范围的 continuous 豁免限位断言（无 bounds）与行程上限检查
 *     （MDL-12——V15 反例④的放行半边）；
 *   - 行程上限策略校验（MDL-06④）——经 IJointLimitEvaluator.evaluate
 *     （阈值唯一来源＝传入 EngineeringPolicySet 的 JointThresholds，
 *     默认 4π，本地无第二常量——ARC-05/NFR-MNT-07）→
 *     TravelLimitExceeded（T＞L 才超限——policy §7.4 冻结边界）→构造
 *     MDL-06-TRAVEL-LIMIT ConfirmableFinding（实际行程/阈值/单位 rad
 *     三要素；policy 不执行放行——SA-15 确认编排归 project S4）；
 *   - 闭包引用存在性（I-MDL-9 闭包半段——值模型层无闭包上下文，§4.10
 *     范围注记归本套件）→ MDL-READINESS-REF-MISSING；
 *   - 资源状态面事实（Recorded 未固化）→ MDL-READINESS-RESOURCE-STATE
 *     （Warning——不阻断应用，正式评估由证据门禁阻断）；
 *   - 对象 schema 主版本受支持 → 违者 MDL-READINESS-SCHEMA-UNSUPPORTED
 *     （解码门先行强制——本断言为就绪层的防御面）。
 *
 * 线程安全：无共享可变状态；全部方法 const——并发只读可重入（§3.4 总
 * 约定 1）。确定性：同输入同输出（诊断文案为固定中文模板＋确定性数值
 * 格式化——NFR-COR-02）。
 */
class AssertionSuite {
public:
    /**
     * @brief 注入端口集（L5/测试装配；全部非 owning——调用方保证存活期
     *        覆盖套件使用期）。
     *
     * R-4 边界：名称解析归 runtime——行程评估的 runtimeName 解析源
     * （IPolicyNameContext）由装配注入（runtime ⑥端口适配器），modeling
     * 不自建名称映射、不做前缀拼接。
     */
    struct Ports {
        /// 关节限位评估器（policy 唯一实现——经 makeJointLimitEvaluator 装配）。
        const policy::IJointLimitEvaluator* jointLimitEvaluator = nullptr;
        /// 名称上下文（行程评估的 runtimeName 解析源；评估调用期必需）。
        const policy::IPolicyNameContext* nameContext = nullptr;
    };

    /// 构造（注入端口；物性/限位/引用/资源断言不消费端口——端口仅行程
    /// 校验使用，空端口的违约面见 evaluateTravelLimits 注）。
    explicit AssertionSuite(Ports ports) noexcept;

    // ---- 物性硬断言①②③（BodyData 单一实现——§4.4"断言①②③同连杆"）----

    /**
     * @brief 对一个物性组执行硬断言①②③与缺失预告。
     *
     * @param subject         [in] 所属对象身份（连杆/工具 objectId——诊断
     *                        subject 定位要素）
     * @param localName       [in] 局部名（诊断 localName 要素；空串＝对象
     *                        无 localName 口径——记录以 nullopt 承载）
     * @param body            [in] 物性组（只读）
     * @param blockers        [out] 硬断言失败追加（①②③逐项；比较型三要素：
     *                        ①actual=m、expected=0、kg；②actual=λmin、
     *                        expected=0、kg·m²；③actual=λmax、expected=
     *                        λmid＋λmin、kg·m²）。仅已提供值参与（缺失不
     *                        触发——NotProvided 走预告）。
     * @param missingWarnings [out] 缺失预告追加（mass/inertia 各缺一项一条
     *                        MDL-READINESS-PHYSICS-MISSING——DataInsufficient
     *                        降级预告，不阻断）
     *
     * 纯函数；线程安全；确定性。
     */
    void assertBodyPhysical(const core::ObjectId& subject,
                            const std::string& localName,
                            const BodyData& body,
                            std::vector<core::DiagnosticRecord>& blockers,
                            std::vector<core::DiagnosticRecord>& missingWarnings) const;

    // ---- 限位硬断言④（§8.2 L4：区间错/未确认两分支）----

    /**
     * @brief 对根对象关节表执行限位硬断言④。
     *
     * 检查面：Revolute/Prismatic 已提供 bounds 的 qmin<qmax（violation→
     * MDL-ASSERT-LIMIT-INTERVAL，单位随类型 rad/m）；Continuous 的工程
     * 工作范围——NotProvided→"未确认"阻断、Provided 但端点非有限或
     * min≥max→"非有限"阻断（均 MDL-ASSERT-RANGE-NOT-FINITE，rad）；已
     * 确认有限范围→通过（豁免本断言与行程检查——MDL-12）。Revolute/
     * Prismatic 缺失 bounds 不是本断言面（§8.2 L4 仅列"区间错/未确认"
     * 两 Blocking 分支；缺失限位的待确认面归导入域
     * MDL-IMPORT-PENDING-CONFIRM，Description 以 NotProvided 承载）。
     *
     * @param design   [in] 根对象（只读）
     * @param blockers [out] 硬断言失败追加（比较型三要素：actual=qmin/
     *                 range.first、expected=qmax/range.second）
     *
     * 纯函数；线程安全；确定性。
     */
    void assertJointLimitIntervals(const RobotDesign& design,
                                   std::vector<core::DiagnosticRecord>& blockers) const;

    // ---- 行程上限策略校验（MDL-06④——SA-15 待确认集的唯一产出点）----

    /**
     * @brief 行程上限校验的三态（调用方按态处置——不静默跳过）。
     */
    enum class TravelEvaluation {
        NoExceeded,       ///< 无可评估关节或全部行程合规（无产出）
        FindingsProduced, ///< 产出 ConfirmableFinding（超限关节——待确认集）
        EvaluationFailed, ///< 评估未终态化（名称不可解析等——评估诊断已追加
                          ///  至 evalDiagnostics；行程合法性未确认＝不得放行）
    };

    /**
     * @brief 执行行程上限策略校验（§9.3：④端口阈值→评估器→Confirmable）。
     *
     * 关节表装配口径：仅 Revolute（有限限位→qMin/qMax；缺失→无限位 spec，
     * 评估器按"未声明限位"口径全部不适用）与 Continuous（已确认范围→
     * engineeringRange）进入；Prismatic/Fixed 排除——行程阈值"仅适用有限
     * 限位旋转关节"（policy §4.4 原文）。构型表＝单行全零（行程是限位
     * 派生事实，与构型无关；评估器的裕量/违例面在本场景非消费目标）。
     *
     * @pre 前置调用 assertJointLimitIntervals 且无 RANGE-NOT-FINITE 阻断
     *     （continuous 未确认范围不可装配进查询——评估器对缺范围 continuous
     *      fail-fast；短路优先级由调用方保证，本函数对违 pre 的输入经
     *      EvaluationFailed 表达，不伪造事实）。
     *
     * @param design          [in] 根对象（只读——关节表来源）
     * @param policy          [in] 已解析策略（阈值唯一来源——不读本策略
     *                        以外语义）
     * @param confirmables    [out] 超限发现追加（MDL-06-TRAVEL-LIMIT；
     *                        比较型三要素 actual=|qmax−qmin|、expected=
     *                        阈值、单位 rad；cause 内嵌 policyContentId
     *                        规范文本——§6.7 确认绑定四元组的定位素材，
     *                        四元组组装归 project S6）
     * @param evalDiagnostics [out] 评估器诊断追加（EvaluationFailed 态的
     *                        POLICY-* 已注册码记录——调用方据此阻断）
     * @return 三态（见 TravelEvaluation）
     *
     * @throws PolicyError 评估器对装配违约的 fail-fast（查询契约由本套件
     *         装配保证——到达即实现缺陷，不吞不改）；端口为空＝装配违约
     *         同轨（调用方装配错误 fail-fast）
     *
     * 纯函数；线程安全；确定性。
     */
    TravelEvaluation evaluateTravelLimits(const RobotDesign& design,
                                          const policy::EngineeringPolicySet& policy,
                                          std::vector<core::ConfirmableFinding>& confirmables,
                                          std::vector<core::DiagnosticRecord>& evalDiagnostics) const;

    // ---- 闭包引用存在性（I-MDL-9 闭包半段——§8.2 L1）----

    /**
     * @brief 根对象引用表对工作集闭包视图的存在性与 token 匹配。
     *
     * 闭包视图＝工作集（v0.9 增量字段：rootObjectId＋toolObjects/
     * sceneObjects/poseSetObject/drivetrainObject）——编辑态预检即草稿
     * 世界，prepare 侧由基线重建装配同一形状（同源闭包）。toolRefs/
     * sceneRefs/poseSetRef/drivetrainRef 逐引用核对：目标存在且类型
     * token 匹配（violation→MDL-READINESS-REF-MISSING，subject=被引对象
     * 身份——精确定位到缺失引用）。defaultTcp∈toolRefs 与引用表无重复
     * 由 I-MDL-9 值模型半段承担（L0 层——不在本断言重复）。
     *
     * @param design       [in] 根对象（只读——引用表来源）
     * @param closureView  [in] 闭包视图工作集（只读）
     * @param blockers     [out] 引用缺失追加
     *
     * 纯函数；线程安全；确定性。
     */
    void assertClosureReferences(const RobotDesign& design,
                                 const ModelingWorkingSet& closureView,
                                 std::vector<core::DiagnosticRecord>& blockers) const;

    // ---- defaultTcp 引用校验（I-MDL-9/KIN-14——WP-13-T10；§8.2 L7 闭包半段）----

    /**
     * @brief defaultTcp 的闭包级引用完整性（KIN-14"defaultTcp 必填口径"）。
     *
     * 检查面（全部候选状态可判——闭包视图半段；toolOid∈toolRefs 的值
     * 模型半段由 I-MDL-9 解码门强制，此处为纵深复核）：
     *   - 有工具引用（toolRefs 非空）而 defaultTcp 未设置→阻断（§4.3
     *     defaultTcp 行"有 tools 时须已设置"——KIN-14）；
     *   - defaultTcp.toolOid 不在候选 toolRefs→阻断（I-MDL-9）；
     *   - defaultTcp.toolOid 不指向闭包视图内的工具对象，或 defaultTcp.
     *     tcpKey 不在该工具 tcpList 中→阻断（KIN-14/tcpKey 存在性——
     *     L7"工具与 TCP 完整"）。
     * 全部产出 MDL-READINESS-DEFAULT-TCP-INCOMPLETE（§9.5 T10 行——
     * WP-13-T10 实现期增登），subject=defaultTcp 引用的工具身份（未设
     * 置面 subject 为空、定位经 context）。
     *
     * 就绪校验（Readiness.cpp L7）与本断言共用同一实现（§9.4.4——不得
     * 出现两套判定，NFR-MNT-04）。
     *
     * @param design      [in] 候选根对象（只读——引用表与 defaultTcp 来源）
     * @param closureView [in] 候选闭包视图工作集（只读——工具对象来源）
     * @param blockers    [out] 违例追加
     *
     * 纯函数；线程安全；确定性。
     */
    void assertDefaultTcp(const RobotDesign& design,
                          const ModelingWorkingSet& closureView,
                          std::vector<core::DiagnosticRecord>& blockers) const;

    // ---- 引用保护（I-MDL-9/V-04——WP-13-T10；"删除"的移除前置门）----

    /**
     * @brief 移除请求的引用保护（V-04：被 defaultTcp 引用的工具不得移除）。
     *
     * 基线根的 defaultTcp 若指向本次移除集内的任一对象→逐对象产出
     * MDL-REF-PROTECTED 比较型定位诊断（subject=被移除对象 oid；比较三
     * 要素：actual=该对象被 defaultTcp 引用计数 1、expected=0、单位 1
     * 无量纲）——移除被拒（I-MDL-9 引用保护；先解除 defaultTcp 引用再
     * 移除）。对象字节与旧修订闭包不受影响（拒绝零写入——PA-2/CON-02）。
     *
     * 未被本域引用的对象（如普通场景对象——defaultTcp 只引用工具）通过
     * 本门：跨域引用（requirements→工具 ObjectId）无法由 modeling 直检
     * （R-1 禁互链），由移除命令摘要附"可能存在外部引用"提示＋⑤事件承
     * 接（悬空检测归 requirements 就绪校验，P-MDL-6——§4.8 原文）。
     *
     * @param baselineRoot [in] 基线根对象（只读——defaultTcp 的变更前状态）
     * @param removedOids  [in] 本次移除引用的对象身份集（载荷 removals 展平）
     * @param blockers     [out] 保护违例追加（非空＝移除被拒）
     *
     * 纯函数；线程安全；确定性。
     */
    void assertReferenceProtection(const RobotDesign& baselineRoot,
                                   const std::vector<core::ObjectId>& removedOids,
                                   std::vector<core::DiagnosticRecord>& blockers) const;

    // ---- 资源状态面（§8.2 L6——Warning 不阻断）----

    /**
     * @brief 资源清单状态面核查（Recorded 未固化→Warning 提示固化）。
     *
     * 范围注记（§8.2 L6 行"Recorded 且探测 Missing/Changed"）：缺失/变化
     * 探测是 I/O——归 io 护栏（装载时）与 runtime 编译复核
     * （ResourceChanged 整体失败），就绪层（纯函数、不读文件系统）只承
     * 载状态机事实：Recorded→MDL-READINESS-RESOURCE-STATE Warning（未
     * 固化提示，CON-03 固化激励——不阻断应用）；Solidified→通过。
     *
     * @param design   [in] 根对象（只读——resourceManifest 来源）
     * @param warnings [out] 警告追加
     *
     * 纯函数；线程安全；确定性。
     */
    void checkResourceStates(const RobotDesign& design,
                             std::vector<core::DiagnosticRecord>& warnings) const;

    // ---- schema 主版本受支持（§8.2 L10 半段——防御面）----

    /**
     * @brief 工作集全部对象的 schema 主版本受支持核查。
     *
     * 解码门（Codec decode 校验链④）已对入存字节强制同一谓词——本断言
     * 服务于携带**内存构造**对象的工作集（模板/编辑器路径）的就绪预检。
     * 主版本≠当前支持值（大于＝未来版本拒绝猜测、小于＝无升级器不可读
     * ——NFR-DEP-04）→ MDL-READINESS-SCHEMA-UNSUPPORTED。
     *
     * @param ws       [in] 工作集（只读——design＋部件对象全集）
     * @param blockers [out] 超版追加（subject＝对象身份——根对象无身份时
     *                 以 nullopt 承载）
     *
     * 纯函数；线程安全；确定性。
     */
    void assertSchemaVersions(const ModelingWorkingSet& ws,
                              std::vector<core::DiagnosticRecord>& blockers) const;

private:
    Ports m_ports;  ///< 注入端口（构造后只读——行程校验消费面）
};

// =====================================================================
// 命令载荷模型与编解码（§9.3 decode 段——处理器域内契约，project 不解释）
// =====================================================================

/// @brief 命令载荷格式版本（§6.4 版本三元组的处理器自有版本戳；schema
///        变更→+1，旧版本 payload 拒绝并给升级指引——NFR-DEP-04）。
///        v2（WP-13-T10）：新增 removals 引用移除段（§9.3 表行 3"移除
///        引用"与 V-04/V-05 删除面——卡 §4.8"删除＝引用移除"的命令载体）；
///        v1 载荷拒收（重新编辑指引——卡 R-MDL-5：草稿属会话短命数据，
///        版本不兼容损失可接受并如实提示）。
inline constexpr std::uint32_t kCommandPayloadVersion = 2;

/**
 * @brief 载荷对象槽（§9.3"应用编辑差值"的载体单元——一个待写入对象）。
 *
 * 身份语义（与 ModelingWorkingSet 的临时句柄纪律衔接——Template.hpp 注）：
 *   - allocateNew=true：新对象——objectId 须为全零保留值（未分配标记），
 *     prepare 经 ctx.objectId() 取号回填（PA-1：身份分配唯一归 project）；
 *   - allocateNew=false：既有对象替换——objectId 须有效且存在于基线闭包
 *     （写不存在的身份＝无效输入）。
 *
 * objectBytes＝对象 canonical 字节（RobotDesignCodec，kCurrentFormatVersion
 * ——§4.8 确定性序列化；解码门在校验链内强制 I-MDL 不变量——非法模型
 * 不可能经载荷进入断言域，断言域拦截的是解码门之外的跨对象/策略事实）。
 *
 * 线程安全：纯值类型。
 */
struct PayloadObjectSlot {
    bool allocateNew = false;               ///< true＝新对象（取号回填）
    core::ObjectId objectId;                ///< 槽身份（语义见结构注）
    std::string objectTypeToken;            ///< 五对象 token 之一（路由与校验）
    std::vector<std::uint8_t> objectBytes;  ///< 对象 canonical 字节（解码门输入）

    bool operator==(const PayloadObjectSlot& o) const
    {
        return allocateNew == o.allocateNew && objectId == o.objectId
            && objectTypeToken == o.objectTypeToken && objectBytes == o.objectBytes;
    }
    bool operator!=(const PayloadObjectSlot& o) const { return !(*this == o); }
};

/**
 * @brief 引用移除槽（WP-13-T10 增量——§9.3 表行 apply-scene-objects"移除
 *        引用"与 §4.8"删除＝引用移除"的载荷载体）。
 *
 * 语义：从根对象引用表移除该对象引用（toolRefs/sceneRefs），被移除对象
 * 本身**零写入**——对象库不可变只增不改（PA-2/CON-02），对象字节与历史
 * 修订闭包完整保留；"删除"永不物理删除。
 *
 * 移除被 defaultTcp 引用的工具→拒绝＋MDL-REF-PROTECTED 比较型定位诊断
 * （I-MDL-9/V-04）；移除命令摘要附"可能存在外部引用"提示＋⑤事件（修订
 * 提交即触发 DependencyInvalidated——project TxEngine 已验契约；悬空检测
 * 归 requirements 就绪校验，P-MDL-6）。
 *
 * 线程安全：纯值类型。
 */
struct PayloadRemovalSlot {
    core::ObjectId objectId;      ///< 待移除引用的对象身份（须有效且被根引用）
    std::string objectTypeToken;  ///< 五对象 token 之一（路由与校验）

    bool operator==(const PayloadRemovalSlot& o) const
    {
        return objectId == o.objectId && objectTypeToken == o.objectTypeToken;
    }
    bool operator!=(const PayloadRemovalSlot& o) const { return !(*this == o); }
};

/**
 * @brief 建模命令载荷（处理器域内 canonical 形态——CommandEnvelope.
 *        payloadCanonical 的域解释，D-10：project 透传存储不解释）。
 *
 * 模式语义（§6.9 撤销/重做——"以 inverse 载荷提交 restore 型命令（同一
 * commandType，payload=restore 变体）"）：
 *   - Apply：正向应用——槽语义见各处理器注（每命令的槽形状校验不同）；
 *     removals 段（v2）承载引用移除（Apply 专用——见 PayloadRemovalSlot 注）；
 *   - Restore：快照逆放——全部槽 allocateNew=false 且 objectId 须存在于
 *     基线（恢复历史字节）；槽字节为受影响对象**前一版本的 canonical
 *     字节集**（D-MDL-9——由 prepare 的 inverse 组装产出，往返一致）；
 *     removals 须为空（逆放不表达移除——移除的逆＝根对象字节还原，
 *     引用随根字节恢复）。
 *
 * 线程安全：纯值类型。
 */
struct CommandPayload {
    /// 载荷模式（见结构注）。
    enum class Mode { Apply, Restore };

    Mode mode = Mode::Apply;                  ///< 模式（Apply|Restore）
    std::vector<PayloadObjectSlot> objects;   ///< 对象槽（槽序＝确定性处理序）
    std::vector<PayloadRemovalSlot> removals; ///< 引用移除槽（v2；Apply 专用——槽序确定性）

    bool operator==(const CommandPayload& o) const
    {
        return mode == o.mode && objects == o.objects && removals == o.removals;
    }
    bool operator!=(const CommandPayload& o) const { return !(*this == o); }
};

/**
 * @brief 载荷确定性编码（§4.8 序列化纪律同源：字段定序、小端长度前缀、
 *        UTF-8、无填充）。
 *
 * @param payload [in] 载荷（只读）
 * @return canonical 字节（同载荷重复编码逐字节相等——NFR-COR-02；tryDecode
 *         (encode(p))==p 往返）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<std::uint8_t> encodeCommandPayload(const CommandPayload& payload);

/**
 * @brief 载荷解码（try 轨——严格校验：magic/版本/长度前缀/截断/越界/
 *        尾随字节/token 词表外/槽字段一致性，任一失败＝nullopt 不猜测；
 *        版本≠kCommandPayloadVersion 同样拒绝——NFR-DEP-04）。
 *
 * @param bytes [in] encodeCommandPayload 产出（或任意来源字节）
 * @return 解码结果；破损/版本不受理＝nullopt（prepare 转
 *         RejectedInvalidInput——§9.3 decode 失败分支）
 *
 * 纯函数；线程安全；确定性（不产出诊断——载荷破损的机器判别面是
 * PrepareOutcome::RejectedInvalidInput，§9.5 无对应登记码，不私定）。
 */
std::optional<CommandPayload> tryDecodeCommandPayload(const std::vector<std::uint8_t>& bytes);

// =====================================================================
// IModelingCommandHandler——基类（§9.4.8）与五命令处理器
// =====================================================================

/**
 * @brief 处理器装配服务集（L5 装配注入的"处理器自持句柄"——project.md
 *        §5.3.2"处理器经 HandlerContext 获得……编译与策略端口句柄"中
 *        策略侧的落位：HandlerContext（PRJ-T10 冻结面）不暴露④端口，
 *        由装配期随处理器注入；编译端口仍以 HandlerContext::compilePort
 *        为准——处理器不直接调用编译，S5 编排归 project）。
 *
 * 线程安全：全部指针非 owning、构造后只读——装配纪律（存活期覆盖处理
 * 器使用期）。
 */
struct HandlerServices {
    /// 断言套件端口（评估器＋名称上下文——R-4：名称映射装配注入）。
    AssertionSuite::Ports assertionPorts{};
    /// ④策略端口（resolvePolicy——行程校验阈值唯一来源通道）。
    const policy::IPolicyProvider* policyProvider = nullptr;
    /// 策略对象身份（装配绑定——适用范围解析归 policy/装配层，本处理器
    /// 不推断"哪个策略适用于本项目"）。
    core::ObjectId policyObject{};
    /// 期望策略内容版本（nullopt＝不可编址取数——resolvePolicy 按错误
    /// 矩阵返回空 policy＋POLICY-OBJECT-MISSING 诊断轨；由装配侧锚定）。
    std::optional<core::ContentVersion> policyVersion;

    // ---- T09 增量（WP-13-T09，§7.6 权威切换门——表尾追加，向后兼容）----
    /// DH 转换器（§9.4.7——apply-robot-design 权威切换变体在 prepare 内
    /// 执行转换判定的唯一实现注入点；nullopt＝未装配：切换变体到达即
    /// 装配缺陷 fail-fast——非切换载荷不受影响。单元卡 §15 v0.10 登记）。
    const IDhExplicitConverter* dhConverter = nullptr;
    /// 编译分段探针（§7.6 等价验证注入面——verifyEquivalent 经此走
    /// runtime buildCanonicalModel S1～S5 只读分段；装配语义同上）。
    const CompileProbe* dhCompileProbe = nullptr;
};

/**
 * @brief 子类钩子的产出（§9.4.8 DecodeOutcome 落位）——差值解码与写入集
 *        表达的结果。
 *
 * 契约：outcome==Planned 时 candidate/affectedOids 有效——candidate＝基线
 * ＋编辑差值（新对象身份已经 ctx.objectId() 回填——PA-1 取号点在钩子内）；
 * 写入集已由钩子按 §9.4.8 语义表达进 CommandPlan.objectWrites（钩子的
 * out 参数）；affectedOids＝受影响对象身份全集（inverse 快照素材——
 * D-MDL-9）。outcome==RejectedInvalidInput 时其余字段无意义（域结构校验
 * 失败——槽形状/token/身份存在性/引用一致性；基类清空拒绝态计划）。
 *
 * 线程安全：纯值类型。
 */
struct DecodeOutcome {
    /// 钩子结果（Planned｜RejectedInvalidInput——钩子不产出 RejectedHard
    /// Assert：硬断言判定统一归基类断言段——单一判定面）。
    project::PrepareOutcome outcome = project::PrepareOutcome::Planned;
    /// 候选工作集（Planned 时有效——断言段输入）。
    ModelingWorkingSet candidate;
    /// 受影响对象身份（inverse 素材——槽序确定性）。
    std::vector<core::ObjectId> affectedOids;
    /// 行程校验相关性声明（§9.3 断言列的命令域归属：仅 apply-robot-design
    /// 置 true——行程 Confirmable 属根命令断言域，其余命令不改变关节行程
    /// 事实、不消费④端口）。
    bool travelRelevant = false;
    /// 本次移除引用的对象身份集（WP-13-T10 增量——引用保护断言的输入；
    /// 钩子只陈述移除事实，保护判定统一归基类断言段执行（MDL-REF-
    /// PROTECTED——单一判定面），非空时基类先于其余断言执行保护门）。
    std::vector<core::ObjectId> removedRefs;
};

/**
 * @brief 建模命令处理器基类（§9.4.8 原文契约）：封装 §9.3 prepare 管线
 *        公共段（载荷解码/基线重建与防御性复核/断言套件/计划最终化），
 *        子类只声明 commandType/payloadVersion（基类定值）/对象写入差异
 *        （decodeAndPlan 钩子）。
 *
 * 生命周期：L5 装配期构造并注册进 HandlerRegistry（一次性——
 * registerModelingCommandHandlers），运行期只读。
 *
 * prepare 执行序（S3 阶段；全部同步、命令线程——§9.3 管线图逐步落位）：
 *   ① 载荷框架解码：tryDecodeCommandPayload 失败→RejectedInvalidInput
 *     （invalid-payload；§9.3 decode 失败分支）；
 *   ② 基线重建：baseSnapshot 闭包 → reader 同源解码（五对象 token 路由
 *     → ModelingWorkingSet）；防御性复核——envelope.expectedRevision 与
 *     baseSnapshot.id 不一致＝调用方契约违约（S2 已拦截，fail-fast）；
 *   ③ 子类钩子 decodeAndPlan：差值解码＋命令形状校验＋身份取号回填＋
 *     写入集表达（RejectedInvalidInput 透传）；
 *   ④ 断言分域（AssertionSuite——与就绪校验共用，NFR-MNT-04）：硬断言
 *     失败→RejectedHardAssert＋逐项 DiagnosticRecord（code=MDL-ASSERT-*，
 *     subject＋localName＋比较型三要素——就地阻止＋精确定位）；物性缺失
 *     →Warning 预告随 diags 留痕；行程上限→ConfirmableFinding（仅当
 *     候选含有限限位/连续旋转关节时解析策略——策略不可解析→
 *     RejectedHardAssert＋④端口诊断传导，不静默跳过行程校验）；
 *   ⑤ 计划最终化：objectWrites/requiresDualCompile/confirmableFindings/
 *     inverse（受影响对象前一 (oid,cv) canonical 字节集；首次应用无前版
 *     ＝不可逆声明 nullopt——UndoRedoService 不提供越过首修订的撤销）/
 *     summary（中文：对象/字段/确认留痕/资源状态）→ Planned。
 *
 * @错误 prepare 不抛业务异常（拒绝走值面）；防御性复核失败（基线不一致/
 * 基线字节损坏）＝调用方契约违约或数据侧异常，fail-fast 透传（不吞不改
 * ——AGENTS 错误纪律）。
 *
 * 线程约束：仅命令执行线程（project 串行槽）；写权限：经①端口
 * （CommandPlan 声明面），无其他写路径。
 */
class IModelingCommandHandler : public project::ICommandHandler {
public:
    /// 构造（注入装配服务集＋本命令的 requiresDualCompile 声明——§9.3 表
    /// 列的子类定值；引用/指针非 owning）。
    IModelingCommandHandler(HandlerServices services, bool requiresDualCompile) noexcept;

    /// 载荷版本演进点（§9.4.8——currentPayloadVersion 的 final 落位；
    /// 当前受理集合＝{kCommandPayloadVersion}——PRJ-T10 受理口径）。
    [[nodiscard]] std::uint32_t currentPayloadVersion() const final;

    // ---- prepare（§5.3.2 签名——执行序见类注；final：公共段不随子类变化）----
    project::PrepareOutcome prepare(project::HandlerContext& ctx,
                                    const project::CommandEnvelope& envelope,
                                    const project::RevisionView& baseSnapshot,
                                    project::CommandPlan& out,
                                    std::vector<core::DiagnosticRecord>& diags) final;

protected:
    /**
     * @brief 子类钩子：差值解码与写入集表达（§9.4.8 原文签名——断言/
     * 确认/inverse/summary 由基类统一执行；各命令的槽形状语义见处理器
     * 类注）。
     *
     * @param ctx      [in] 执行上下文（objectId() 取号——新对象身份分配）
     * @param payload  [in] 已框架解码的载荷（模式＋槽集）
     * @param baseline [in] 基线工作集（基线重建产出——闭包视图同源）
     * @param out      [out] 计划（钩子只填 objectWrites——其余字段基类负责）
     * @return 钩子产出（DecodeOutcome——见结构注）
     */
    virtual DecodeOutcome decodeAndPlan(project::HandlerContext& ctx,
                                        const CommandPayload& payload,
                                        const ModelingWorkingSet& baseline,
                                        project::CommandPlan& out) = 0;

    /// 装配服务集（构造后只读——④端口/断言端口的处理器自持句柄）。
    HandlerServices m_services;
    /// 共用断言套件（由 m_services.assertionPorts 构造——与就绪校验同一
    /// 判定面，NFR-MNT-04；声明序在 m_services 之后——初始化依赖）。
    AssertionSuite m_suite;
    /// 本命令的 requiresDualCompile 声明（§9.3 表列——apply-named-poses
    /// 为 false，其余 true）。
    bool m_requiresDualCompile;
};

// ---------------------------------------------------------------------
// 五命令处理器（§9.3 命令清单表逐行——无状态子类，差异只在钩子）
// ---------------------------------------------------------------------

/**
 * @brief apply-robot-design 处理器（§9.3 表行 1；requiresDualCompile=
 *        true；断言：物性①②③＋限位④硬断言、行程上限 Confirmable、
 *        闭包引用——链型判定归导入/模板域，本命令不重复）。
 *
 * 槽形状语义：Apply 模式恰一个 robot-design 槽（根）＋0..n 部件槽：
 *   - 根槽 allocateNew：基线闭包不得已有根（恰一根——runtime S2 路由
 *     前提）→取号；显式 oid：须等于基线根身份（替换）；
 *   - 部件槽 allocateNew：取号并按 token 挂入新根引用表（toolRefs/
 *     sceneRefs 追加；poseSetRef/drivetrainRef 置入——根内该引用须原为
 *     未设置，"至多一份"）；显式 oid：须存在于基线同 token（字节替换，
 *     引用表不动）。
 * Restore 模式：全部槽显式 oid 且存在于基线——快照逆放（§6.9）。
 */
class ApplyRobotDesignHandler final : public IModelingCommandHandler {
public:
    explicit ApplyRobotDesignHandler(HandlerServices services) noexcept
        : IModelingCommandHandler(std::move(services), true) {}

    [[nodiscard]] std::string commandType() const override
    {
        return std::string(kCmdApplyRobotDesign);
    }

protected:
    DecodeOutcome decodeAndPlan(project::HandlerContext& ctx,
                                const CommandPayload& payload,
                                const ModelingWorkingSet& baseline,
                                project::CommandPlan& out) override;
};

/**
 * @brief apply-tool-definition 处理器（§9.3 表行 2；requiresDualCompile=
 *        true——工具几何/物性入 WC/DWC；断言：工具物性①②③＋defaultTcp
 *        引用校验（WP-13-T10——assertDefaultTcp/assertReferenceProtection
 *        对候选/基线执行））。
 *
 * 槽形状语义（Apply 模式，写入面与移除面二选一——混载＝无效载荷）：
 *   - 写入面：恰一个 tool-definition 槽；基线须已有根（根引用表增量挂
 *     载点）。allocateNew→取号＋根 toolRefs 追加（根对象一并写入——"＋
 *     根引用表增量"）；显式 oid→基线既有工具字节替换（引用稳定，不写
 *     根）。空 tcpList 工具在解码门被 I-MDL-13 拒绝（§4.4 ≥1）。
 *   - 移除面（WP-13-T10/V-04/V-05）：恰一个 tool 槽位移除（removals），
 *     objects 须为空。被移除对象须存在于基线闭包且被根 toolRefs 引用
 *     （否则＝无效载荷——无可移除引用）；被 defaultTcp 引用→保护门拒绝
 *     ＋MDL-REF-PROTECTED（subject=oid，V-04）；移除成功＝仅根对象写入
 *     （引用表移除——对象字节与历史修订闭包完整保留，PA-2/CON-02），
 *     摘要附"可能存在外部引用"提示（跨域引用直检归 requirements，R-1/
 *     P-MDL-6）。
 * Restore 模式：全部槽显式 oid 且存在于基线——快照逆放（§6.9）；removals
 * 须为空（移除的逆＝根字节还原，引用随根恢复）。
 */
class ApplyToolDefinitionHandler final : public IModelingCommandHandler {
public:
    explicit ApplyToolDefinitionHandler(HandlerServices services) noexcept
        : IModelingCommandHandler(std::move(services), true) {}

    [[nodiscard]] std::string commandType() const override
    {
        return std::string(kCmdApplyToolDefinition);
    }

protected:
    DecodeOutcome decodeAndPlan(project::HandlerContext& ctx,
                                const CommandPayload& payload,
                                const ModelingWorkingSet& baseline,
                                project::CommandPlan& out) override;
};

/**
 * @brief apply-scene-objects 处理器（§9.3 表行 3；requiresDualCompile=
 *        true——碰撞几何变更；断言：闭包引用＋场景引用校验）。
 *
 * 槽形状语义（Apply 模式——批量增/改/移除引用可混载）：
 *   - 对象槽（0..n 个 scene-object 槽）：allocateNew→逐槽取号＋根
 *     sceneRefs 追加（批量一次根写入）；显式 oid→基线既有场景对象字节
 *     替换。场景引用校验（§9.3 断言列）＝闭包引用断言的 sceneRefs 面。
 *   - 移除槽（0..n 个，removals——WP-13-T10/V-05）：从根 sceneRefs 移除
 *     引用；被移除对象须存在于基线闭包且被引用（否则＝无效载荷）。场景
 *     对象无 defaultTcp 引用面（defaultTcp 只引用工具）——移除常规放行；
 *     摘要附"可能存在外部引用"提示（跨域引用悬空检测归 requirements
 *     就绪校验——R-1/P-MDL-6）。objects 与 removals 全空＝无效载荷。
 * Restore 模式：全部槽显式 oid 且存在于基线；removals 须为空（§6.9）。
 */
class ApplySceneObjectsHandler final : public IModelingCommandHandler {
public:
    explicit ApplySceneObjectsHandler(HandlerServices services) noexcept
        : IModelingCommandHandler(std::move(services), true) {}

    [[nodiscard]] std::string commandType() const override
    {
        return std::string(kCmdApplySceneObjects);
    }

protected:
    DecodeOutcome decodeAndPlan(project::HandlerContext& ctx,
                                const CommandPayload& payload,
                                const ModelingWorkingSet& baseline,
                                project::CommandPlan& out) override;
};

/**
 * @brief apply-named-poses 处理器（§9.3 表行 4；requiresDualCompile=
 *        false——不进 Description；无物性断言（纯参考数据））。
 *
 * 槽形状语义：Apply 模式恰一个 named-pose-set 槽；基线须已有根。槽内
 * entries＝用户命名位姿全集——经 mergeNamedPoseEntries 合并流（§4.6/
 * D-MDL-3/MDL-17，WP-13-T10）：保留键拒绝写入（"除 Home/Zero 外"）、
 * 键唯一、jointConfiguration 与根关节序一一对应；合并产物＝基线保留键
 * 条目（homeConfiguration/zeroConfiguration 原样保留——V-27 建模侧，
 * KIN-06 复位走会话命令零修订）∪ 用户条目（键字典序）。合并失败＝无效
 * 载荷（域结构校验——值面违例明细见 PoseEditErrorCode）。allocateNew→
 * 取号＋根 poseSetRef 置入（根内该引用须原为未设置——"至多一份"）；
 * 显式 oid→基线既有位姿集字节替换。位姿集不经名称映射、不进任何评估器
 * 依赖键（§4.6/D-MDL-3）——本命令零编译影响，S5 跳过双编译（位姿集修
 * 订不触发重算）。
 */
class ApplyNamedPosesHandler final : public IModelingCommandHandler {
public:
    explicit ApplyNamedPosesHandler(HandlerServices services) noexcept
        : IModelingCommandHandler(std::move(services), false) {}

    [[nodiscard]] std::string commandType() const override
    {
        return std::string(kCmdApplyNamedPoses);
    }

protected:
    DecodeOutcome decodeAndPlan(project::HandlerContext& ctx,
                                const CommandPayload& payload,
                                const ModelingWorkingSet& baseline,
                                project::CommandPlan& out) override;
};

/**
 * @brief apply-drivetrain-design 处理器（§9.3 表行 5；requiresDualCompile=
 *        true——ratio/摩擦/耦合入 Description；断言：传动合法性
 *        （I-MDL-11/12——解码门强制）＋R1 耦合阻断（I-MDL-12——解码门
 *        强制；R1 耦合的阻断码 MDL-21-COUPLING-STAGE-LOCKED 随 T18/R2
 *        注册，R1 拒绝面经解码门不变量承载——契约 note ④）。
 *
 * 槽形状语义：Apply 模式恰一个 robot-drivetrain 槽。allocateNew→取号＋
 * 根 drivetrainRef 置入（原须未设置）；显式 oid→基线既有传动字节替换
 * （SEL-10 回填/OPT StageB 编辑路径）。ratioPerJoint 与关节序的对应性
 * 核查归编译链（Description 映射）——本命令不重复判定。
 */
class ApplyDrivetrainDesignHandler final : public IModelingCommandHandler {
public:
    explicit ApplyDrivetrainDesignHandler(HandlerServices services) noexcept
        : IModelingCommandHandler(std::move(services), true) {}

    [[nodiscard]] std::string commandType() const override
    {
        return std::string(kCmdApplyDrivetrainDesign);
    }

protected:
    DecodeOutcome decodeAndPlan(project::HandlerContext& ctx,
                                const CommandPayload& payload,
                                const ModelingWorkingSet& baseline,
                                project::CommandPlan& out) override;
};

/**
 * @brief 五命令处理器族装配注册（project.md §6.5——L5 应用壳装配期一
 *        次性；注册表所有权接收 unique_ptr，重复 token 注册边界拒绝）。
 *
 * @param registry       [in,out] 目标注册表（调用方持有——本函数不接管）
 * @param services       [in] 全族共享的装配服务集（AssertionSuite 端口＋
 *                       ④端口锚定——五处理器持同一服务集；处理器无状态
 *                       ——共享安全）
 * @throws std::invalid_argument 注册表拒绝（token 重复——同族二次装配即
 *         装配错误，fail-fast）
 */
void registerModelingCommandHandlers(project::HandlerRegistry& registry,
                                     const HandlerServices& services);

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_COMMANDHANDLERS_HPP
