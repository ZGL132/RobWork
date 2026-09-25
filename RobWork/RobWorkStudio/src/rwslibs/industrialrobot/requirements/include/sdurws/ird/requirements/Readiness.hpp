/**
 * @file   Readiness.hpp
 * @brief  IRequirementReadinessChecker——需求就绪校验器（§9.5 行原文契约
 *         的落位）：R0~R9 分层校验（§8.1 表，短路优先）、DomainReadinessItem
 *         呈现数据（P-REQ-6：门控拦截归 workflow/ui，本单元只供数）与
 *         ReadinessSummary 投影（evidence ①级输入门禁数据源，§6.4①）。
 *
 * 设计依据：
 *   - units/requirements.md §8.1（就绪校验分层表——R0~R9 逐层检查面与
 *     Blocking/Warning/NotApplicable 分级；跨聚合浅引用边界专项声明：
 *     仅查闭包 objectRefs 元数据、不解码 modeling 对象）、§8.3（状态
 *     正交关系表——就绪轴只判输入完备合法，"需求未就绪不产生任何工程
 *     判定" V-12）、§7.5（预览与正式分离——Preview 语义零修订零正式
 *     证据）、§9.5（IRequirementReadinessChecker 行原文签名：check/
 *     readinessSummary）、§9.6（T05 行六码——本头为唯一产码消费者）、
 *     §9.2（接口契约总则——纯函数服务：可重入、无副作用、值语义产出）、
 *     §3.4（线程与确定性总约定）、§12（ui 交接行——DomainReadinessItem
 *     数据；workflow/ui 门控行——"门控判定归 workflow"）
 *   - units/evidence.md §6.4①（ReadinessSummary 数据形状对端锚——
 *     {valid, invalidMustItems[]} 两字段；"任一启用 Must 条目非法——
 *     REQ-06 口径，由请求方/域提供"；evidence 是消费者不是生产者，
 *     PA-1 就绪判定语义归 requirements 侧）
 *   - 需求 REQ-06（Must/Should 分级与预览分离）、EVI-01（表 1——预览
 *     不产生正式证据与结果对象）、V-12/V-20（观测点）、NFR-MNT-04
 *     （无重复判定——与命令 prepare 同源断言套件，O-39 裁决）、
 *     NFR-COR-01/02（确定性）
 *   - 任务契约 tasks/foundation/WP-14-T05.json acceptance 1~5/7（R0~R9
 *     逐层正反例、ReadinessSummary 数据源、Must/Should 分级、预览分离、
 *     O-39 纯函数投影面、P-REQ-6 边界声明）
 *
 * 背景说明（为什么就绪校验是纯函数分层短路——第一读者须知）：
 *   就绪轴（§8.3）只回答一个问题："这份需求输入现在能否被正式评估
 *   消费？"它与编辑状态、执行状态、工程判定三轴正交（V-12：需求未
 *   就绪不产生任何工程判定结论）。因此本校验器：
 *   ① 输入只有工作集＋闭包元数据（CheckContext），输出只有报告值——
 *     零修订、零正式证据对象、零内容身份承诺（EVI-01 表 1 预览语义；
 *     evidence §4.1.1"未应用草稿预览输入"行）；编辑器即时预检与评估
 *     组装前复核调用的是同一 check（O-39 裁决：组装时现场重算，与命令
 *     prepare 同源断言套件，NFR-MNT-04 无重复判定）。
 *   ② R0→R9 短路优先（§8.1 表头原文）：某层出现 Blocking（阻止应用
 *     级）发现即停止其后各层——后层检查的前提（结构/引用/几何合法）
 *     已不成立，继续执行只会产出派生噪声。Warning（可应用，登记）与
 *     NotApplicable（显式标记，检查域缺席）不短路。
 *   ③ 条目级各层（R1~R8）只判**启用**条目——§4.3/§4.4/§4.5 enabled
 *     字段行原文"未启用条目不进入正式就绪判定"（ACC3"任一启用的
 *     Must 条目"口径）；未启用条目零发现、不入 Must 清单。集合级各层
 *     （R0 根引用表/R9 schema）恒适用（集合对象无启用语义）。
 *   ④ 分层是定位面：每条发现标注所属层（R0~R9）与级别，诊断记录携
 *     稳定码（§9.6 T05 行）＋subject＋localName——校验面板逐项呈现，
 *     workflow/ui 据此做各自的阶段门控（P-REQ-6：本单元不判"能否进入
 *     某阶段"，只供数）。
 *
 * 层-检查域-稳定码映射（§8.1 表逐行落位；§9.6 未逐层给码的层按语义
 * 就近映射，逐条登记如下——实现决策，单元卡 §14.6 同步）：
 *   R0 结构完整：根引用表四槽解析＋集合 token 一致（悬空/失配→
 *     REQ-READY-REF-MISSING）；条目 id/name 非空唯一与集合规范序
 *     （I-REQ-1/2/3）＝工作集不变量，属调用方前置——违约 fail-fast
 *     （std::invalid_argument），不产诊断：合法生产者（编辑器/解码链/
 *     命令基线重建）已保证不变量，到达即调用方契约违约（AGENTS §3
 *     错误二分：调用方错误 fail-fast）。
 *   R1 引用完整（浅校验）：refFrame 的 ModelFrame/SceneObject 目标、
 *     AlignFrame.targetFrame、AlignGeometryNormal.targetSceneObject
 *     的闭包存在性＋token 匹配（→REQ-READY-REF-MISSING）。
 *   R2 坐标系有效：refFrame 引用种类与场景槽合法性（forScene 结构面，
 *     World 缺省合法——结构违约→REQ-READY-REF-MISSING；构造边界已
 *     保证，此为防御面）。
 *   R3 位姿合法：任务点逐条 validatePoseConstraint＋validateTolerance
 *     ＋启用段距离（数值有限/容差>0/constrainedDof 非空/姿态规则参数
 *     ——I-REQ-5；违例→REQ-READY-POSE-ILLEGAL）。区域盒/覆盖率/采样
 *     的参数面归 R6（§9.6 PLAN-DEGENERATE 行语义"区域退化"）；工况
 *     值面无对应层（构造边界保证，不重复判定）。
 *   R4 工况绑定完整：appliesTo.stations 与 events[].stationRef 逐个
 *     指向任务点条目（子条目 ObjectId——O-36 模型内锚，不入闭包，
 *     在点集内定位）＋Stations 空清单非法组合（§4.7）；悬空→
 *     REQ-READY-REF-MISSING（"引用悬空"语义就近承载，层标注 R4）。
 *   R5 必验范围明确：必验集合（enabled∧Must 工况——§6.2 冻结规则）
 *     为空→REQ-READY-NO-REQUIRED-CASE（Warning，正式拦截归 evidence
 *     P-EV-7）；非空→本层无发现。
 *   R6 采样计划有效：区域盒非退化/覆盖率∈[0,1]/区域采样定义（I-REQ-6，
 *     →REQ-READY-PLAN-DEGENERATE Blocking）；计划条目 validateSampling
 *     Plan（规范化 Grid 形态/姿态采样≥1）与计划-区域一一对应（悬空/
 *     一区多计划→Blocking）；区域非空而计划集为空→REQ-READY-PLAN-
 *     MISSING（Warning 零计划预告——§8.1 R6"Warning（零计划）"分支，
 *     该分支 §9.6 原六码无 warning 级承载，按 modeling WP-13-T10 实现
 *     期增登先例在 §9.6 表尾增登本码）；无区域且无计划→NotApplicable
 *     （"无区域任务"显式标记——§8.1 级别行原文示例）。Grid 计数乘积
 *     =0 合法（零样本由评估判定——V-02），不产发现。
 *   R7 顺序无环：任务点经 TaskPointService::checkSequence（I-REQ-7
 *     单一实现——NFR-MNT-04）、区域以同构拓扑核对（区域顺序键 §4.4
 *     "同任务点语义；R7 校验面归 T05"）；重复键/悬空前驱/成环→
 *     REQ-READY-SEQ-CYCLE（悬空前驱非字面"成环/重复"，按层-码对齐
 *     原则以 R7 层码承载，cause 区分）；全部条目无顺序声明→
 *     NotApplicable。
 *   R8 工具/模型引用存在：tcpRef（Tool→tool-definition；DefaultTcp
 *     恒合法缺省）、工况 toolRefs/payload toolRef（Tool）、environment
 *     Refs（scene-object）的闭包存在性＋token 匹配（悬空→REQ-READY-
 *     REF-MISSING）；无任何工具/环境引用面→NotApplicable。§8.1 R8 的
 *     Warning 分支（PendingManualResolution 姿态规则）依赖 T07 派生流
 *     的状态标记（当前值模型无该承载字段），随 T07 落位增补——本层
 *     现不产出该警告（不私建状态语义——NFR-COR-03，登记单元卡）。
 *   R9 可生成 evidence 输入切片：五对象 schemaVersion 受支持（超版→
 *     REQ-SCHEMA-UNSUPPORTED，§9.6 T02/T03 行——NFR-DEP-04 稳定拒绝
 *     面）；五集合 canonical 可编码＋必验解析确定（§6.2）＝防御面，
 *     经 R3/R6 值校验与集合不变量前置后不可达，失败即实现缺陷
 *     fail-fast（std::logic_error——modeling 基线解码失败同轨）。
 *
 * O-39 处置（契约 acceptance 5——2026-09-22 已裁决，消解 P-REQ-3）：
 *   ReadinessSummary 在评估组装时**现场重算**（与命令 prepare 同源
 *   断言套件——命令处理器候选态重估调用的就是本 check，NFR-MNT-04），
 *   修订内不持久化就绪结论。本头实现纯函数投影面：check/readiness
 *   Summary 可重入无副作用，不写入任何持久化就绪记录（返回值即全部
 *   产出——无 out 参数、无静态状态、无环境读取）。
 *
 * 线程安全：本头全部实体为纯值/纯函数（校验器无共享可变状态），并发
 * 只读安全、可重入（§3.4 总约定 1）。
 * 确定性（NFR-COR-01/02）：分层执行序固定（R0→R9）；同层发现按条目
 * 规范文本序产出（集合遍历序＝条目已排序序，I-REQ-1）；诊断文案为
 * 固定中文模板＋确定性数值格式化；无环境/时钟/locale 依赖。
 */

#ifndef IRD_REQUIREMENTS_READINESS_HPP
#define IRD_REQUIREMENTS_READINESS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>              // DiagnosticRecord——发现定位载体
#include <sdurws/ird/evidence/Verdict.hpp>           // evidence::ReadinessSummary——①级投影对端（值类型消费）
#include <sdurws/ird/project/QueryPort.hpp>          // project::ObjectRef——闭包 objectRefs 元数据（浅校验数据面）
#include <sdurws/ird/requirements/DiagCodes.hpp>     // REQ-READY-* 码常量（产码唯一书写点）
#include <sdurws/ird/requirements/Editor.hpp>        // RequirementWorkingSet——校验输入工作集

namespace sdurws::ird::requirements {

// =====================================================================
// 分层与分级词表（§8.1 表——R0~R9 层序与三级分类；纯值枚举）
// =====================================================================

/**
 * @brief 就绪校验分层（§8.1 表 R0~R9 行序——枚举值即层号，短路执行序
 * 与报告标注共用）。
 *
 * 线程安全：纯值枚举；确定性：同层同 token（NFR-COR-02）。
 */
enum class ReadinessCheckLayer : std::uint8_t {
    R0,  ///< 结构完整（根引用表/集合 token/条目标识不变量前置）
    R1,  ///< 引用完整（浅校验——refFrame 目标/姿态规则目标闭包存在＋token）
    R2,  ///< 坐标系有效（refFrame 槽-种类合法；World 缺省合法）
    R3,  ///< 位姿合法（数值有限/容差>0/constrainedDof 非空/姿态规则参数）
    R4,  ///< 工况绑定完整（appliesTo/stationRef 指向存在的任务点条目）
    R5,  ///< 必验范围明确（空集 Warning——正式拦截归 evidence P-EV-7）
    R6,  ///< 采样计划有效（区域非退化/计划-区域一一对应；零计划 Warning）
    R7,  ///< 任务顺序无环（sequenceKey 拓扑——无环/无重复/无悬空）
    R8,  ///< 工具/模型引用存在（tcpRef/环境引用浅有效）
    R9,  ///< 可生成 evidence 输入切片（schema 受支持/canonical 可编码）
};

/// @brief 分层的稳定 token（"R0".."R9"——报告呈现与测试定位面；静态存储期）。
std::string_view readinessLayerToken(ReadinessCheckLayer layer) noexcept;

/**
 * @brief 发现级别（§8.1"级别"行三值；可确认级本单元暂无——§8.1 原文
 * "若未来出现走 SA-15 同款流，不私设"，故不预留枚举值）。
 *
 * 语义边界（P-REQ-6）：三级是**数据事实分级**（呈现面），不是门控动作
 * ——Blocking 的阶段拦截规则归 workflow/ui（本单元只供 DomainReadiness
 * Item 数据）。
 */
enum class ReadinessFindingLevel : std::uint8_t {
    Blocking,      ///< 阻止应用级（存在即输入未就绪——ReadinessSummary.valid=false）
    Warning,       ///< 可应用，登记（含"数据不足预告"——正式判定归下游）
    NotApplicable, ///< 显式标记（检查域在本工作集上缺席——如无区域任务）
};

/// @brief 级别的稳定 token（"Blocking"/"Warning"/"NotApplicable"；静态存储期）。
std::string_view readinessFindingLevelToken(ReadinessFindingLevel level) noexcept;

// =====================================================================
// 校验上下文与报告值（§9.5 CheckContext/RequirementReadinessReport）
// =====================================================================

/**
 * @brief 就绪校验上下文（§9.5 check 入参第二位——浅校验的数据面）。
 *
 * 字段＝修订闭包的 objectRefs 元数据快照（project 提供——§8.1 专项
 * 声明"仅查闭包 objectRefs 元数据，不解码 modeling 对象内容"）。语义：
 *   - 元素为目标修订闭包内全部存储对象的引用（oid＋cv＋objectTypeToken
 *     ＋digest256）；本单元只读 oid 与 objectTypeToken（R-1 红线：不解码
 *     modeling 对象字节；语义级有效性归评估时解析——§8.1 浅引用边界）；
 *   - 命令 prepare 侧的候选闭包＝基线 objectRefs 按计划写入增改后的
 *     后像（同源口径——候选态就绪重估与基线态同一数据形状）。
 *
 * 生命周期：纯值（深拷贝）；调用方持有的值语义对象。线程安全：纯值。
 */
struct CheckContext {
    /// 修订闭包对象引用集（全量——任一修订自足描述完整可见状态，§4.4.2）。
    std::vector<project::ObjectRef> closureRefs;

    /// 值相等（引用集逐元素全等——ObjectRef 自带 operator==；纯值惯例）。
    bool operator==(const CheckContext& o) const { return closureRefs == o.closureRefs; }
    bool operator!=(const CheckContext& o) const { return !(*this == o); }
};

/**
 * @brief 闭包浅核对（§8.1 跨闭包半区的单点实现——WP-14-T06 起由文件内
 *        私有辅助提升为公共自由函数，供就绪层与姿态规则解析面共用同一
 *        份核对语义，NFR-MNT-04 无第二套判定）。
 *
 * 核对规则（§8.1 专项声明字面）：目标 ObjectId 存在于 ctx.closureRefs
 * 且其登记 objectTypeToken 与 expectedToken 一致——**仅读元数据两字段**
 * （objectId/objectTypeToken），不解码 modeling 对象字节（R-1 红线）。
 *
 * @param ctx           [in] 闭包 objectRefs 元数据（只读）
 * @param oid           [in] 待核对目标对象
 * @param expectedToken [in] 期望对象类型 token（expectedTargetToken 的
 *                      产出——RequirementTypes.hpp 匹配表单点）
 * @return 通过＝nullopt；违例＝人读中文原因（悬空/token 失配——供诊断
 *         cause 复用；机器判别以稳定码 REQ-READY-REF-MISSING 为准）
 *
 * 纯函数；线程安全；确定性（线性扫描＝输入序，保序不依赖哈希——与
 * Readiness 内部查找同款确定性取舍）。
 */
std::optional<std::string> closureRefViolation(const CheckContext& ctx,
                                               const core::ObjectId& oid,
                                               std::string_view expectedToken);

/**
 * @brief 单条就绪发现（§12 ui 交接行 DomainReadinessItem 数据的承载）。
 *
 * ★ P-REQ-6 边界（契约 acceptance 7）：本结构只有三个字段——层（哪项
 * 检查）、级别（数据事实分级）、诊断记录（稳定码＋定位＋中文原因与
 * 建议动作）。**没有任何门控动作语义**：不含"允许/禁止进入某阶段"的
 * 判定字段与方法——Blocking 的阶段门控拦截规则归 workflow/ui，校验
 * 面板对三级仅做呈现（D-REQ-11 同源：供数不做主）。
 *
 * 线程安全：纯值。
 */
struct DomainReadinessItem {
    ReadinessCheckLayer layer = ReadinessCheckLayer::R0;  ///< 所属检查层（§8.1 行）
    ReadinessFindingLevel level = ReadinessFindingLevel::Blocking;  ///< 事实分级（呈现面——非门控动作）
    core::DiagnosticRecord diag{};  ///< 定位诊断（稳定码 §9.6；subject=条目/对象 oid，localName=条目名）

    bool operator==(const DomainReadinessItem& o) const
    {
        return layer == o.layer && level == o.level && diag == o.diag;
    }
    bool operator!=(const DomainReadinessItem& o) const { return !(*this == o); }
};

/**
 * @brief 需求就绪校验报告（§9.5 RequirementReadinessReport——check 的
 * 值语义产出；一切产出都在返回值里，零持久化副作用）。
 *
 * 字段两块（均为呈现/投影数据，无门控动作——P-REQ-6）：
 *   - items：按 R0→R9 短路执行序排列的发现清单（含 NotApplicable 显式
 *     标记项——报告自描述"哪些层检查了什么"）；
 *   - invalidMustItems：启用∧Must 且带 Blocking 发现的条目 id 规范文本
 *     全量清单（REQ-06 口径——升序去重，不抽样）。该清单在 check 内
 *     计算（需要工作集的 level/enabled 事实，报告自足后 readiness
 *     Summary 才能按 §9.5 原文签名只吃报告投影）。
 *
 * 线程安全：纯值。
 */
struct RequirementReadinessReport {
    std::vector<DomainReadinessItem> items;     ///< 发现清单（R0→R9 短路序；确定性序）
    std::vector<std::string> invalidMustItems;  ///< 非法启用 Must 条目 id 规范文本（升序去重——REQ-06 全量清单）

    /**
     * @brief 是否存在 Blocking 级发现（输入未就绪的机器判别面）。
     * @return true＝任一层有 Blocking（⇒ ReadinessSummary.valid==false）
     *
     * ★ 这是**事实查询**不是门控动作（P-REQ-6）：调用方（workflow/ui/
     * evidence 组装）据各自规则消费该事实；本单元不判定"能否进入某
     * 阶段"。纯函数；确定性。
     */
    bool hasBlocking() const noexcept;

    bool operator==(const RequirementReadinessReport& o) const
    {
        return items == o.items && invalidMustItems == o.invalidMustItems;
    }
    bool operator!=(const RequirementReadinessReport& o) const { return !(*this == o); }
};

// =====================================================================
// IRequirementReadinessChecker——接口（§9.5 行原文签名）
// =====================================================================

/**
 * @brief 需求就绪校验器接口（§9.5 行原文：R0~R9（§8.1）；产出报告＋
 *        ReadinessSummary 投影（evidence ①级数据源））。
 *
 * 消费方（三者同源一套判定——NFR-MNT-04/O-39）：
 *   - 编辑器即时预检（UI 线程——预览语义，REQ-06/EVI-01 表 1：零修订
 *     零正式证据，§7.5）；
 *   - 命令 prepare 候选态重估（命令线程——§9.1"就绪 R0~R9 现场重估
 *     （防基线漂移）"，CommandHandlers.hpp）；
 *   - 评估组装前复核（evidence ①级——组装时现场重算，O-39 裁决）。
 */
class IRequirementReadinessChecker {
public:
    virtual ~IRequirementReadinessChecker() = default;

    /**
     * @brief 执行 R0~R9 分层就绪校验（§9.5 行原文签名）。
     *
     * @param ws  [in] 需求工作集（只读；须满足集合不变量 I-REQ-1/2/3——
     *            违约为调用方契约违约 fail-fast，见文件头 R0 映射说明）
     * @param ctx [in] 校验上下文（闭包元数据——浅校验数据面；只读）
     * @return 报告（items 短路序＋invalidMustItems 投影；值语义）
     *
     * @throws std::invalid_argument 工作集不变量违约（I-REQ-1 规范序/
     *         I-REQ-2 id 唯一/I-REQ-3 name 唯一——合法生产者保证，到达
     *         即调用方契约违约，fail-fast 不产诊断）
     * @throws std::logic_error R9 防御面不可达失败（canonical 编码失败/
     *         必验解析不一致——经 R3/R6 与不变量前置后不应出现，到达即
     *         实现缺陷）
     *
     * 纯函数；线程安全（可重入）；确定性（NFR-COR-01/02）。
     */
    virtual RequirementReadinessReport check(const RequirementWorkingSet& ws,
                                             const CheckContext& ctx) const = 0;

    /**
     * @brief 就绪→evidence ①级输入门禁数据（§9.5 行原文——任一启用
     *        Must 条目非法→valid=false＋全量清单）。
     *
     * 投影规则（evidence §6.4① 数据形状逐字段一致——对端值类型消费）：
     *   - valid = !report.hasBlocking()（任一 Blocking 发现即输入未就绪
     *     ——①级"不运行正式评估"；保守门禁：Should/集合级 Blocking 同样
     *     使输入不可消费，其明细在 report.items，但不进 Must 清单）；
     *   - invalidMustItems = report.invalidMustItems（check 已按 enabled
     *     ∧Must 过滤——本函数零重算，NFR-MNT-04）。
     *
     * @param report [in] check 产出（只读；同一校验器的 check 结果——
     *               跨校验器实例混用不改变语义：判定无状态）
     * @return evidence::ReadinessSummary（两字段逐字段投影——PA-1 对端
     *         值类型直接消费，不再包装）
     *
     * 纯函数；线程安全（可重入）；确定性。
     */
    virtual evidence::ReadinessSummary readinessSummary(
        const RequirementReadinessReport& report) const = 0;
};

/// IRequirementReadinessChecker 的唯一产品实现（无状态——可默认构造，
/// 拷贝/移动平凡；§3.4 总约定 1）。
class RequirementReadinessChecker final : public IRequirementReadinessChecker {
public:
    RequirementReadinessReport check(const RequirementWorkingSet& ws,
                                     const CheckContext& ctx) const override;
    evidence::ReadinessSummary readinessSummary(
        const RequirementReadinessReport& report) const override;
};

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_READINESS_HPP
