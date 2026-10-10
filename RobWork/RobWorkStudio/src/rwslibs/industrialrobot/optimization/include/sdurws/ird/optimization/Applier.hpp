/**
 * @file   Applier.hpp
 * @brief  候选应用组装面（IOptimizationCandidateApplier，O13）——"设为当前
 *         方案"的采用守卫：§10.3 六项前置校验＋两步命令组合组装＋差异预览
 *         （任务 WP-20-T07；OPT-08/AT-12）。
 *
 * 设计依据：
 *   - units/optimization.md §10.1（候选应用流程——校验 V1~V3→差异预览→
 *     组装→两步提交→新修订→失效→复算提示）、§10.2（命令组合与原子性：
 *     step1 project 建支〔baseRevisionId 记录、不复制对象〕＋step2 域设计
 *     命令〔payload＝候选设计 canonical 字节；requiresDualCompile=true；
 *     expectedRevision＝新分支 tip〕；两命令非单事务——step2 失败仅残留
 *     方案分支、基线不受影响；红线：不直接修改 RobotDesign、不绕过
 *     ProjectCommandService、不自行实现撤销/重做）、§10.3（候选应用前置
 *     校验清单六项——组装面逐项执行）、§10.4（候选差异预览＝Model Diff
 *     消费——optimization 不重复实现差异呈现；P-OPT-8：传动比差异当前
 *     依赖 modeling diff 范围扩展，条目缺失时给警告**不虚构差异**）
 *   - 需求 OPT-08（候选归属 OptimizationRunResult 不产生项目修订；预览
 *     候选不得修改基线；"设为当前方案"必须创建方案分支、产生新修订并
 *     完整复算）、AT-12（基线保护与差异呈现：分支＋新修订＋完整复算三
 *     件套；基线内容不变）、MDL-08（差异比较数据实体——比较画面归 UX-13）
 *   - 架构决策 DOPT-6（optimization R1 不注册新命令 token：候选应用＝
 *     project 建支＋modeling apply-robot-design 组合——P-PR-9 规避；O-35
 *     裁决 2026-09-22：命令 token 采用**无点形态**＝服从现行冻结语法
 *     ^[a-z0-9-]{3,64}）、N1（optimization 不写 RobotDesign 对象）、
 *     R-1/R-2（与 modeling/project 只经公共头与注入缝协作——本单元依赖
 *     白名单九边不含 modeling，差异预览/候选物化经本头的注入接口桥接，
 *     生产适配器归 L5 装配层）、§12.2（buildApplyPlan 契约：校验并组装
 *     两步命令与差异预览输入；**不执行提交**——提交经①端口归执行编排）
 *   - 已知风险 P-OPT-3（候选应用物化：补丁→候选设计 canonical 字节需要
 *     modeling 侧 IRobotDesignPatchApplier 支持，未裁决——处置：组装面
 *     就绪＋allowApply 通道化：物化缝未注入时组装成功返回但 allowApply=
 *     false，应用功能不启用〔不违反 OPT-08 的 R1 验收前状态〕）、P-OPT-8
 *     （Model Diff 传动比覆盖——按现状根对象 diff；ratio 条目缺失给警告）、
 *     P-PR-9（命令 token 语法——O-35 已裁决无点形态，本单元不注册面）
 *   - 任务契约 tasks/foundation/WP-20-T07.json（acceptance 2/3：两步命令
 *     组合经①端口、基线对象字节不变〔AT-12〕、过期基线 StaleRevisionRejected
 *     候选保留、撤销/重做由 project 接管零自建栈、差异预览 P-OPT-8 警告
 *     不虚构）
 *
 * ★ 落位范围口径（诚实登记，防扩大）：本头交付①两条注入缝（物化缝/
 *   预览缝——canonical 字节为跨单元通道载荷，§12.3"通道载荷为 canonical
 *   字节"）；②组装请求/计划值类型；③组装服务（六项校验＋组装）。**不落**
 *   提交执行编排（两步 submit 的编排消费 CandidateApplyPlan 归 ui/L5 装配
 *   ——本面组装不含①端口调用，与 §12.2"不执行提交"逐字一致；契约测试
 *   经测试替身处理器走 project CommandPlan 声明面验证两步语义，见
 *   contract_test/ApplierContractTest.cpp 文件头口径登记）；建支命令的信封
 *   构造（token/payload 版本）归执行编排——project 内置建支命令族实体
 *   落位随 project 侧增量（P-PR-9 裁决后义务），本面只声明建支**语义**
 *   （baseRevisionId＋分支名），零 token 声明（DOPT-6 不注册纪律的组装侧
 *   表达）。
 *
 * 背景说明（第一读者须知——三件事）：
 *   ① **组装与执行分离**：buildApplyPlan 是 const 纯函数（只读请求输入、
 *      零副作用）——"预览候选不得修改基线"（OPT-08）在类型面上成立：
 *      组装不触碰项目；触碰项目的只有执行编排经①端口的两次 submit。
 *      基线保护的实质担保在 project：预览/评估全程只读快照（§10.2"评估
 *      全程只读快照"），应用只发生在新方案分支（PA-2 修订只增）。
 *   ② **通道化（P-OPT-3）**：组装成功 ≠ 可应用。物化缝未注入（生产常态
 *      直至 P-OPT-3 裁决落位）或项目只读时，计划以 allowApply=false 返回
 *      ＋blockedReasons 逐项原因——调用方（ui）据此禁用"设为当前方案"
 *      入口；候选照常导出（OPT-12 面）。这与"前置校验失败抛异常"（调用
 *      方给了不能应用的候选）是两回事：前者是**能力未装配**，后者是
 *      **调用方契约违约**。
 *   ③ **撤销/重做零自建栈**：本头无任何 undo/redo 面。应用产生的新修订
 *      的撤销＝project UndoRedoService 逆命令（apply 的 inverse 由 modeling
 *      handler 声明，快照式逆载荷——§10.2 红线）；plan.step2.requiresDual-
 *      Compile=true 的双编译编排归 project 命令服务 S5。
 *
 * 线程约束：buildApplyPlan const 只读可并发（§12.3 表 buildApplyPlan 行）；
 *   注入缝实现须各自满足其线程契约（物化缝/预览缝均为纯函数面）。
 */

#ifndef SDURWS_IRD_OPTIMIZATION_APPLIER_HPP
#define SDURWS_IRD_OPTIMIZATION_APPLIER_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>       // core::BranchId/RevisionId——
                                              //  基线状态面与建支语义
#include <sdurws/ird/optimization/CandidatePatch.hpp>  // CandidatePatch/CandidateId
                                              //  ——候选描述（补丁物化/归属核对）
#include <sdurws/ird/optimization/Run.hpp>    // OptimizationRunResult——候选归属
                                              //  容器（OPT-08 数据面）
#include <sdurws/ird/optimization/Types.hpp>  // OptimizationError——域异常 fail-fast

namespace sdurws::ird::optimization {

// =====================================================================
// 既有命令 token 引用（DOPT-6：只引用不注册——token 权威在 modeling）
// =====================================================================

/**
 * @brief 候选设计域命令 token（"apply-robot-design"——modeling 单元 §9.3
 *        命令清单行 1 的**既有**注册 token，无点形态〔O-35 裁决：服从
 *        project.md §4.4.4 冻结语法 ^[a-z0-9-]{3,64}〕）。
 *
 * 为什么是常量引用而不是注册：DOPT-6/§10.2 红线——optimization R1 不注册
 * 新命令 token、不写 RobotDesign 对象（N1）。本常量只是把组合命令的第二步
 * 指向 modeling 既有处理器；token 的注册（HandlerRegistry）与信封的
 * payloadFormatVersion 由执行编排（L5 装配层，可达 modeling 公共头）完成
 * ——本单元无 modeling 编译边（R-1），不代持其契约常量。
 */
inline constexpr std::string_view kApplyRobotDesignCommandToken =
    "apply-robot-design";

// ---- 组装计划的过程 token（呈现/测试定位用；非诊断码——错误语义面 ----
// ---- 的稳定码仍唯一归 DiagCodes.hpp OPT-* 登记表） --------------------

/// 阻断原因：候选物化缝未注入（P-OPT-3 裁决前生产常态——应用功能不启用，
/// 候选只导出）。
inline constexpr std::string_view kApplyBlockMaterializationUnavailable =
    "candidate-materialization-unavailable";
/// 阻断原因：项目只读（PM-07——应用入口禁用＋诊断，§10.2 红线）。
inline constexpr std::string_view kApplyBlockProjectReadOnly = "project-read-only";
/// 预览警告：差异预览通道未装配或输入不足（如实警告，不虚构空差异）。
inline constexpr std::string_view kDiffWarningSourceUnavailable =
    "diff-preview-unavailable";
/// 预览警告：传动比差异超出当前 modeling diff 范围（P-OPT-8——ratioPerJoint
/// 随 robot-drivetrain 对象、根对象 diff 不覆盖；给警告**不虚构差异条目**）。
inline constexpr std::string_view kDiffWarningRatioOutOfScope =
    "ratio-diff-out-of-scope";

// =====================================================================
// 基线状态面（②端口投影值——组装面的项目侧输入；零 project 类型依赖）
// =====================================================================

/**
 * @brief 当前项目基线状态（组装请求的项目侧输入——调用方自②端口
 *        IProjectQueryPort 投影：branchTips()/writable）。
 *
 * 为什么是投影值而不是端口引用：组装是纯函数（可并发、可重放）——端口
 * 引用会引入时序依赖与并发约束；投影快照把"组装时点的基线事实"固化进
 * 请求（与 §10.3 第 4 项"当前分支 tip"的核对语义一致——快照后 tip 前进
 * 由提交期的 S2 并发校验兜底：expectedRevision 失配 ⇒ StaleRevisionRejected，
 * 组装面只做**预期标记**）。
 *
 * 线程安全：纯值。
 */
struct ProjectBaselineState {
    core::BranchId branch{};   ///< 当前活动分支（应用发生的基线分支；须 isValid）
    core::RevisionId tip{};    ///< 该分支当前 tip 修订（§10.3 第 4 项核对锚）
    bool writable = false;     ///< 项目可写（PM-07——false ⇒ 应用入口禁用）
};

// =====================================================================
// 差异预览数据面（§10.4——modeling ModelDiffReport 的跨单元通道投影）
// =====================================================================

/**
 * @brief 候选差异预览条目（modeling.md §9.4.9 ModelDiffEntry 的**值语义
 *        同构投影**——本单元无 modeling 编译边（R-1），经字符串化通道
 *        承载其数据实体：group/kind 用其稳定 token 文本（"structure"/
 *        "parameters"/"properties"；"added"/"removed"/"modified"），
 *        objectId 为 obj- 规范文本（根对象字段条目为空串）。
 *
 * 零语义增殖：字段集与 modeling 实体一一对应（定位四元组＋field＋变化
 * 双标记＋两侧确定性文本摘要）——optimization 不重解释、不重排序（排序
 * 契约归 modeling diff 实现），呈现分组消费归 UX-13 方案比较视图（卡
 * §10.4 分工）。
 *
 * 线程安全：纯值。
 */
struct CandidateDiffEntry {
    std::string group = {};        ///< 呈现分组 token（MDL-08 三组——见类注）
    std::string kind = {};         ///< 变化三态 token（added/removed/modified）
    std::string objectId = {};     ///< 对象定位锚（obj- 文本；空＝根对象字段条目）
    std::string subjectPath = {};  ///< 值模型内字段定位路径（如 "joints[2].axis"）
    std::string field = {};        ///< 叶字段名（如 "axis"/"mass"/"ratioPerJoint"）
    bool valueChanged = false;      ///< 值/状态面不同（含四态迁移与链序位变化）
    bool provenanceChanged = false; ///< 来源标记不同（仅 SourcedValue 字段）
    std::string baselineText = {}; ///< 基线侧确定性值摘要（空＝该侧不存在）
    std::string candidateText = {}; ///< 候选侧确定性值摘要（空＝该侧不存在）
};

/**
 * @brief 候选差异预览（§10.4 数据面：差异条目＋P-OPT-8 警告清单）。
 *
 * 警告语义（P-OPT-8 处置原文"按现状根对象 diff；传动比差异条目缺失时给
 * 警告（不虚构差异）"）：warnings 非空**不是错误**——是"呈现面请如实
 * 告知用户该组差异可能不全"的限定语（如传动比补丁在 diff 中无对应条目
 * 时追加 kDiffWarningRatioOutOfScope）；条目永远以 diff 实际产出为准，
 * 绝不因警告而补造条目。
 *
 * 线程安全：纯值。
 */
struct CandidateDiffPreview {
    std::vector<CandidateDiffEntry> entries = {};  ///< 差异条目（modeling diff
                                                    ///  实际产出——零虚构）
    std::vector<std::string> warnings = {};        ///< 限定语 token（kDiffWarning*
                                                    ///  常量集；可空）
};

// =====================================================================
// 注入缝一：差异预览源（P-OPT-8——生产适配器桥 modeling IModelDiffService）
// =====================================================================

/**
 * @brief 候选差异预览源（§10.4"差异预览消费 modeling IModelDiffService"的
 *        注入缝——本单元零 modeling 编译边（R-1），由 L5 装配层提供适配器：
 *        输入两侧设计 canonical 字节、内部经 modeling Codec 还原工作集并
 *        调 IModelDiffService::diff，产出逐条目文本化投影）。
 *
 * 通道载荷口径（§12.3）：canonical 字节是跨单元通道的合法载荷（接口对象
 * 本身不跨进程传递）；基线字节＝基线修订根对象字节（调用方自②端口读出），
 * 候选字节＝物化缝产出——两侧同构（同一 canonical 形态）方可比较。
 *
 * 实现契约：纯函数（const、不修改输入字节、无副作用——同 modeling diff
 * @post"不修改两输入工作集"）；同输入同输出（NFR-COR-02）。
 */
class ICandidateDiffSource {
public:
    virtual ~ICandidateDiffSource() = default;

    /**
     * @brief 生成基线→候选方向的差异预览。
     *
     * @param baselineDesignCanonical [in] 基线设计 canonical 字节（RobotDesign
     *                                根对象字节——基线修订的事实面）
     * @param candidateDesignCanonical [in] 候选设计 canonical 字节（物化缝产出）
     * @return 差异预览（条目＋警告；空差集＝空条目表——合法值对象）
     */
    virtual CandidateDiffPreview diff(
        const std::vector<std::uint8_t>& baselineDesignCanonical,
        const std::vector<std::uint8_t>& candidateDesignCanonical) const = 0;
};

// =====================================================================
// 注入缝二：候选设计物化（P-OPT-3——补丁→候选设计 canonical 字节）
// =====================================================================

/**
 * @brief 候选设计物化缝（§10.3 第 5 项"补丁→候选设计 canonical 物化可用"
 *        的注入面——P-OPT-3 登记的建模侧裁决项：生产适配器需要 modeling
 *        IRobotDesignPatchApplier 公共接口增量，裁决前由调用方/测试供给，
 *        与 T04 探针/T05 指标投影/T06 ICandidateProjector 同款接缝形态）。
 *
 * 为什么组装面需要它：step2 域命令的 payload＝候选设计 canonical **完整
 * 字节**（多变量复合候选单命令整体提交——MDL-06 原子性由命令边界保证）；
 * 补丁只是"基线之上的覆盖集"，字节级物化（覆盖视图→权威字段→重估算→
 * canonical 编码）归 modeling/runtime 权威——本单元零字节构造逻辑（N1/
 * 不复制物化公式）。
 *
 * 实现契约：纯函数（同补丁同绑定 ⇒ 同字节——NFR-COR-02；字节即 step2
 * payload，直接进修订与对象库）；抛出的异常按调用方错误透传（物化失败
 * ＝研究定义/补丁非法，组装即失败——fail-fast，不带病组装）。
 */
class ICandidateDesignMaterializer {
public:
    virtual ~ICandidateDesignMaterializer() = default;

    /**
     * @brief 把候选补丁物化为候选设计 canonical 字节（step2 payload）。
     *
     * @param patch    [in] 候选补丁（须为已通过构造校验的合法补丁）
     * @param bindings [in] 研究绑定集（补丁语义来源——权威字段定位）
     * @return 候选设计 canonical 字节（RobotDesign 形态——基线全量＋补丁
     *         覆盖；空补丁＝基线字节本身）
     */
    virtual std::vector<std::uint8_t> materialize(
        const CandidatePatch& patch,
        const std::vector<VariableBinding>& bindings) const = 0;
};

// =====================================================================
// 组装请求（§10.1 输入面——运行结果＋目标候选＋基线状态＋基线字节）
// =====================================================================

/**
 * @brief 候选应用组装请求（buildApplyPlan 的冻结输入——值语义拷贝入组装）。
 *
 * 基线字节字段为什么可空：差异预览需要基线设计 canonical 字节（②端口
 * 读出）；调用方未提供时预览降级为"通道不可用"警告（不阻断组装——预览
 * 是应用的前置呈现，不是应用的前提条件；§10.1 流程 DIFF 在 ASM 前，
 * 但预览能力缺失时用户仍可依据候选表采用）。
 *
 * 线程安全：纯值。
 */
struct ApplyCandidateRequest {
    OptimizationRunResult run{};   ///< 源运行结果（候选归属容器——§10.3 第
                                    ///  1~3 项校验的输入；运行绑定原修订）
    CandidateId candidateId{};     ///< 目标候选身份（须 isValid——§10.3 第
                                    ///  2/3 项的检索与核对锚）
    ProjectBaselineState baseline{}; ///< 当前项目基线状态（②端口投影——
                                     ///  §10.3 第 4/6 项核对输入）
    /// 基线设计 canonical 字节（差异预览输入——可空＝预览降级警告）。
    std::vector<std::uint8_t> baselineDesignCanonical{};
    /// 方案分支显示名（空＝默认命名规则"方案 <候选 id 前 12 位>"——实现
    /// 口径：分支名一次写入〔P-PR-8 无改名入口〕，默认名保证可辨识；
    /// 显示名不参与任何身份）。
    std::string branchLabel{};
};

// =====================================================================
// 组装计划（§10.1 ASM 产出——两步命令语义＋通道化状态＋预览）
// =====================================================================

/**
 * @brief 第一步：方案分支创建语义（§10.2 step1——project 内置元数据命令）。
 *
 * 刻意**不含** commandType/payload 字段：建支命令属 project 内置元数据
 * 命令族（project.md §6.5——"project 即处理器"的唯一命令族），其 token
 * 形态（O-35 裁决后无点）与处理器实体落位归 project 侧增量义务（PRJ-T10
 * 机制面已就绪：CommandPlan.metadataChange.createBranchWithBase 声明面）。
 * optimization 组装的是**语义**（以哪个修订为基、分支叫什么）；信封构造
 * 归执行编排（L5 装配层，可达 project 公共头）——DOPT-6"不注册新 token"
 * 与"不越权代持他单元契约常量"（R-1 精神）的双重表达。
 *
 * 原子性语义提示（§10.2"原子性边界"）：本步独立提交产生恰好一个元数据
 * 修订（分支表随 ProjectMetadata）；step2 失败时本步产物**保留**（方案
 * 分支残留处置＝保留，用户可重试应用或忽略；分支删除不在产品范围）。
 *
 * 线程安全：纯值。
 */
struct ApplyPlanStepCreateBranch {
    core::RevisionId baseRevisionId{}; ///< 建支基修订＝运行输入修订（分支记录
                                        ///  baseRevisionId 一次写入；**不复制
                                        ///  对象**——新分支 tip 指向同一修订，
                                        ///  对象库零新增，OPT-VER-145 观测点）
    std::string label{};               ///< 新分支显示名（非空——project 建支
                                        ///  声明面拒绝空 label〔P-PR-8 一次
                                        ///  写入〕；组装面同规则前置校验）
};

/**
 * @brief 第二步：候选设计应用语义（§10.2 step2——modeling 域设计命令）。
 *
 * 信封分工（诚实登记，DTB §5.4）：本结构承载 token/payload/expectedRevision/
 * 双编译声明四要素；**payloadFormatVersion 不在本面**——它是 modeling
 * 处理器的受理版本（域 canonical 载荷演进戳），本单元无 modeling 编译边
 * 不可代持（R-1），由执行编排按 modeling 公共头的处理器契约回填（构造
 * CommandEnvelope.payloadFormatVersion）。缺该字段不影响 plan 的完整性：
 * 版本语义归载荷解释方（modeling），不归组装方。
 *
 * expectedRevision 语义：＝step1 新分支的 tip＝baseRevisionId（建支不复制
 * 对象、新分支 tip 一次写入源 tip——project §4.5.1 走查步骤 1/3）——即
 * **运行输入修订**。step2 信封的 branch 字段（目标分支）为 step1 提交后
 * 由执行编排回填（BranchId 由 project 在 S6 装配点分配，组装期不可知——
 * branchTips() 中 baseRevisionId==本面 baseRevisionId 的新增条目即目标）。
 *
 * 线程安全：纯值。
 */
struct ApplyPlanStepApplyDesign {
    std::string commandType{};   ///< 域命令 token（＝kApplyRobotDesignCommandToken
                                  ///  ——modeling 既有注册面，只引用不注册）
    std::vector<std::uint8_t> payloadCanonical{}; ///< 候选设计 canonical 字节
                                  ///  （物化缝产出——完整设计字节，多变量
                                  ///  复合候选单命令整体提交，MDL-06）
    core::RevisionId expectedRevision{}; ///< 期望基线＝新分支 tip＝运行输入
                                  ///  修订（提交期 S2 并发校验锚——过期即
                                  ///  PRJ-STALE-REVISION-REJECTED，候选保留）
    bool requiresDualCompile = true; ///< 双编译声明（MDL-06——WorkCell 与
                                  ///  DynamicWorkCell 任一失败不提交；§10.1
                                  ///  REV 节点"任一失败不提交"的命令面承载）
};

/**
 * @brief 候选应用组装计划（buildApplyPlan 的产出——执行编排的完整输入）。
 *
 * 消费序（§10.1 流程的执行侧展开）：
 *   1. allowApply==false ⇒ 呈现 blockedReasons（入口禁用——不执行任何
 *      提交；候选仍可导出）；
 *   2. expectedStaleBaseline==true ⇒ 呈现"基线已前进，提交预计被拒"
 *      （§10.3 第 4 项——**不阻塞**执行：用户可强行尝试，提交期由
 *      project S2 兜底拒绝〔PRJ-STALE-REVISION-REJECTED〕，候选保留）；
 *   3. 执行编排经①端口提交 step1（建支）→ 取得新分支 id（branchTips
 *      增量）→ 提交 step2（apply，branch 回填）；
 *   4. step2 成功 ⇒ 新修订产生 → project 事件链 RevisionCommitted→
 *      DependencyInvalidated 自动触发依赖失效 → **完整复算提示**呈现
 *      （本计划 recalcRequired==true 恒定——复核完成前不沿用原通过结论，
 *      §10.1 RECALC 节点/§10.5）；
 *   5. step2 失败 ⇒ 方案分支残留（保留＋定位诊断呈现；不回滚 step1——
 *      §10.2 原子性边界）。
 *
 * 线程安全：纯值。
 */
struct CandidateApplyPlan {
    // ---- 通道化状态（P-OPT-3）----
    bool allowApply = false; ///< 应用能力位：六项前置的 1~3/5~6 全部满足且
                              ///  物化缝可用且项目可写时为 true。false 时
                              ///  step1/step2 语义字段可能不完整（物化缺失
                              ///  ⇒ step2 为空）——调用方**不得**消费命令
                              ///  字段，只呈现 blockedReasons。
    std::vector<std::string> blockedReasons = {}; ///< 阻断原因 token（kApplyBlock*
                              ///  常量集；按校验序追加——确定性）

    // ---- 前置标记（§10.3 第 4 项——不阻塞组装）----
    bool expectedStaleBaseline = false; ///< 当前 tip ≠ 运行输入修订（组装后
                              ///  至执行前基线可能仍前进——最终裁决在提交
                              ///  期 S2；本标记只作**预期拒绝**提示）

    // ---- step1：建支语义（前置 1~3 通过即组装——与 allowApply 无关的
    //      语义面；allowApply==false 时调用方不消费）----
    ApplyPlanStepCreateBranch createBranch{};

    // ---- step2：应用语义（物化缝可用时才有值；nullopt＝step2 未组装）----
    std::optional<ApplyPlanStepApplyDesign> applyDesign{};

    // ---- 完整复算提示（§10.1 RECALC——采用三件套之三）----
    bool recalcRequired = true; ///< 恒 true（采用必然引入新设计——依赖失效
                              ///  级联后须完整复算，复核完成前不沿用原通过
                              ///  结论；字段保留显式位以便呈现面直接消费）

    // ---- 差异预览（§10.4——MDL-08 消费投影＋P-OPT-8 警告）----
    CandidateDiffPreview diffPreview{}; ///< 条目以预览缝实际产出为准（缝未
                              ///  装配或输入不足＝空条目＋kDiffWarning* 警告，
                              ///  不虚构差异）
};

// =====================================================================
// 组装服务（§12.2 接口签名——候选应用组装 O13）
// =====================================================================

/**
 * @brief 候选应用组装接口（卡 §12.2 原文契约：校验并组装两步命令（§10.2）
 *        与差异预览输入；**不执行提交**）。
 */
class IOptimizationCandidateApplier {
public:
    virtual ~IOptimizationCandidateApplier() = default;

    /**
     * @brief 校验并组装候选应用计划（§10.3 六项前置逐项校验＋§10.1 ASM）。
     *
     * @param request [in] 组装请求（值语义拷贝——本函数不保留引用）
     * @return 组装计划（前置失败可能以异常表达〔调用方违约〕或以通道化
     *         状态表达〔能力未装配〕——区分见类下实现注）
     *
     * @throws OptimizationError(kOptApplyPlanInvalid) §10.3 第 1 项（运行
     *         非 Completed）/第 2 项（候选不可用：不存在/Quick 记录/非
     *         Feasible 系状态/正式资格不满足）/第 3 项（候选身份与运行
     *         归属不对应：内容寻址重算失配）——调用方给了不能应用的候选，
     *         fail-fast（§12.2 契约原文）；std::invalid_argument 请求结构
     *         违约（runId/candidateId/branch/tip 保留值——分支显示名为空
     *         合法，走默认命名规则）
     *
     * 线程安全：const 只读可并发（§12.3 buildApplyPlan 行）。
     * 确定性：同（请求, 注入缝行为）⇒ 同计划（NFR-COR-02）。
     */
    virtual CandidateApplyPlan buildApplyPlan(
        const ApplyCandidateRequest& request) const = 0;
};

/**
 * @brief 候选应用组装服务（唯一产品实现——注入缝构造定型；§12.3"服务
 *        实例由 L5 装配注入，运行期只读"）。
 *
 * @par 六项前置的处置分轨（§10.3 逐项——失败语义三分，AGENTS §2.5）：
 * | 项 | 校验 | 失败处置 | 归类 |
 * | 1 | run.runPhase==Completed（取消/失败/中断结果不可应用——TASK-02） | 抛 kOptApplyPlanInvalid | 调用方错误 |
 * | 2 | 候选存在、Verified 记录（screeningOnly==false）、状态 Feasible 系、formalPassEligible | 抛 kOptApplyPlanInvalid | 调用方错误 |
 * | 3 | CandidateId 归属核对：candidateIdOf(baselineRoot,baselineCv,patch)==candidateId | 抛 kOptApplyPlanInvalid | 调用方错误 |
 * | 4 | baseline.tip==run.revision | expectedStaleBaseline 标记（不阻塞——§10.3 原文） | 提示面 |
 * | 5 | 物化缝可用 | allowApply=false＋阻断原因（P-OPT-3 通道化） | 能力未装配 |
 * | 6 | baseline.writable | allowApply=false＋阻断原因（PM-07） | 能力未装配 |
 *
 * 第 2 项状态词表说明：Feasible 与 ParetoNondominated 都可行（后者是前者的
 * 非支配子集标记——§7.5；T06 编排产出 Verified-Feasible 记录后经 Pareto
 * 阶段可能被标记）；Infeasible/DataInsufficient/EvaluationFailed/ScreenedOut/
 * Pending 一概不可应用。
 *
 * 第 2 项检索语义（F-630，实现口径）：聚合的 candidates 为 Quick＋Verified
 * 合并序（Quick 批在前、Verified 批在后——Run.hpp 投影纪律），且 Verified
 * 复核批复用 Quick 批的 candidateId（同补丁同基线 ⇒ 同身份，§4.2）。检索
 * 两段式：先找 candidateId 匹配**且 screeningOnly==false**（Verified 记录）
 * 的首条记录，命中即用；无 Verified 记录时回退同身份任意记录——仅有 Quick
 * 记录走 screeningOnly 拒绝（守卫语义），全无同身份记录保持「候选不在源
 * 运行结果中」错误。即：**Verified 记录优先于 Quick 记录**，合并形态下
 * （同 candidateId 的 Quick＋Verified 并存）计划必然携带 Verified 记录的
 * 效力数据，Quick-only 形态仍被拒。
 */
class OptimizationCandidateApplier final : public IOptimizationCandidateApplier {
public:
    /**
     * @brief 注入缝集合（全部可空——空缝＝对应能力未装配，通道化降级；
     *        非 owning 只读指针，调用方保证 buildApplyPlan 期间存活）。
     */
    struct Deps {
        const ICandidateDesignMaterializer* materializer = nullptr; ///< 物化缝
                                     ///  （P-OPT-3——可空；空 ⇒ allowApply 恒 false）
        const ICandidateDiffSource* diffSource = nullptr; ///< 预览缝（P-OPT-8
                                     ///  ——可空；空 ⇒ 预览降级警告，不阻断）
    };

    /**
     * @brief 构造（缝装配期定型——L5 装配形态；P-OPT-3 裁决落位后由
     *        装配层注入生产物化适配器，应用功能随之启用）。
     */
    explicit OptimizationCandidateApplier(Deps deps) noexcept;

    /// @copydoc IOptimizationCandidateApplier::buildApplyPlan
    CandidateApplyPlan buildApplyPlan(
        const ApplyCandidateRequest& request) const override;

private:
    Deps m_deps;  ///< 注入缝（构造后只读——非 owning，生存期由装配方保证）
};

}  // namespace sdurws::ird::optimization

#endif  // SDURWS_IRD_OPTIMIZATION_APPLIER_HPP
