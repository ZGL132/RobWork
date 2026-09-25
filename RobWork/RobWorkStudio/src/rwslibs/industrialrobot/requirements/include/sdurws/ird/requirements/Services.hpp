/**
 * @file   Services.hpp
 * @brief  requirements 领域服务——任务点/区域/工况三服务（§9.4 原文契约
 *         的落位）：条目构造校验、顺序键拓扑校验、采样定义规范化、必验
 *         集合解析（P-EV-9 冻结规则唯一实现点）与需求档派生。
 *
 * 设计依据：
 *   - units/requirements.md §3.3（公共头表 Services.hpp 行——T03）、§9.4
 *     （ITaskPointService/IWorkRegionService/IOperatingConditionService
 *     行原文签名与 @错误 行）、§4.7（I-REQ 不变量——构造边界执行点）、
 *     §6.2（必验范围冻结 schema——resolveRequiredCases 权威条款）、
 *     §4.8（RequirementProfile 派生视图）、§5.2（normalizeSampling——
 *     D-REQ-2 规范化规则）、§3.4（纯函数服务：无共享可变状态、可重入、
 *     并发安全）
 *   - 需求 REQ-01~04（条目语义）、REQ-06（Must/Should 分级数据源）、
 *     P-EV-9（必验派生唯一——V-05 requirements 侧断言）、NFR-COR-01/02
 *   - 任务契约 tasks/foundation/WP-14-T03.json acceptance 3（采样确定性）、
 *     4（构造校验错误语义逐项＋resolveRequiredCases §6.2 冻结 schema）、
 *     1（I-REQ-7/I-REQ-9 的服务面执行）
 *
 * 背景说明（服务的角色——"构造边界"）：值模型（RequirementTypes.hpp）
 * 是字段的忠实载体、不做构造校验；非法实例在本头三个服务的 createXxx
 * 处被拒之门外（错误语义逐项：DuplicateName/IllegalTolerance/
 * ZeroVectorTarget/AllDofFree/DegenerateRegion——§9.4 @错误 行），编辑器
 * 与命令处理器只接受服务产出/服务校验通过的条目。这样字节面（Codec
 * decode 第④步）与编辑面（服务构造）复用同一批 validate* 函数——语义
 * 单源（NFR-MNT-04），两处拒绝口径永不漂移。
 *
 * 临时句柄语义（§9.4 createPoint 行"分配临时句柄"）：createXxx 产出的
 * ObjectId 是 core::ObjectId::generate() 的新随机值——**编辑期临时句柄**，
 * 正式分配权归 project（命令 prepare 阶段经 HandlerContext.objectId()
 * 重绑，O-36 口径：跨修订稳定的是正式句柄；本服务不承诺临时句柄跨会话
 * 稳定）。集合内唯一性由兄弟名清单参数核对（编辑器从工作集填充）。
 *
 * 线程安全：三服务与全部自由函数无共享可变状态、可重入——多线程并发
 * 调用安全（§3.4 总约定 1）。确定性：同输入→同输出（临时句柄除外——
 * 身份生成本质随机，NFR-COR-01 的"确定性"承诺不含 ObjectId 分配）。
 */

#ifndef IRD_REQUIREMENTS_SERVICES_HPP
#define IRD_REQUIREMENTS_SERVICES_HPP

#include <sdurws/ird/core/DiagData.hpp>                 // DiagnosticRecord——服务签名诊断输出参数类型
#include <sdurws/ird/requirements/Codec.hpp>            // Expected——查询轨两态载体（normalizeSampling 返回轨）
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // 值模型＋校验层＋词表

#include <string>
#include <vector>

namespace sdurws::ird::requirements {

// =====================================================================
// 构造规格与产出（§9.4 行文的 Spec/Outcome 面——值语义，调用方所有）
// =====================================================================

/**
 * @brief 任务点构造规格（§9.4 TaskPointSpec——createPoint 的输入面）。
 *
 * 字段＝TaskPoint 的"用户可表达"子集（objectId 除外——临时句柄由服务
 * 分配；名称唯一性经 siblingNames 核对）；校验失败经 CreateOutcome 错误
 * 侧返回（值面，非异常——§9.2 总则）。
 */
struct TaskPointSpec {
    std::string name;                          ///< 语义名（集合内唯一——siblingNames 核对）
    ProcessTag processTag = ProcessTag::Generic;  ///< 工艺标签
    RequirementLevel level = RequirementLevel::Must;  ///< 需求等级
    bool enabled = true;                       ///< 启用
    core::ValueProvenance source{};            ///< 来源标记
    RequirementReference refFrame;             ///< 参考坐标系（默认 World）
    std::optional<RequirementReference> tcpRef;            ///< 工具/TCP 引用（可选）
    PoseConstraint pose{};                     ///< 受约束位姿
    ToleranceSpec tolerance{};                 ///< 容差
    TaskSegment approach{};                    ///< 接近段
    TaskSegment retract{};                     ///< 撤离段（work 段由服务恒定生成——即任务点本身）
    RequirementDemand demands{};               ///< 要求值
    std::optional<std::string> sequenceKey;    ///< 顺序键（前驱条目名——checkSequence 语义）
    std::string note;                          ///< 备注
    std::vector<std::string> siblingNames{};   ///< 目标集合既有条目名（编辑器填充——I-REQ-3 边界核对）
};

/**
 * @brief 区域构造规格（§9.4 WorkRegionSpec——createRegion 的输入面；
 *        位置采样允许 GridBySpacing 原始间距——区域侧定义是编辑态，
 *        规范化在 buildPlan，D-REQ-2）。
 */
struct WorkRegionSpec {
    std::string name;                          ///< 语义名（集合内唯一）
    RequirementLevel level = RequirementLevel::Must;  ///< 需求等级
    bool enabled = true;                       ///< 启用
    core::ValueProvenance source{};            ///< 来源标记
    RequirementReference refFrame;             ///< 参考坐标系
    std::optional<RequirementReference> tcpRef;            ///< 工具/TCP 引用（可选）
    BoundingBox box{};                         ///< 区域盒（m）
    PositionSampling positionSampling{};       ///< 位置采样定义（GridBySpacing 合法）
    OrientationSampling orientationSampling{}; ///< 姿态采样定义
    CoverageTargets coverageTargets{};         ///< 覆盖率目标
    RequirementDemand demands{};               ///< 要求值
    std::optional<std::string> sequenceKey;    ///< 顺序键
    std::string note;                          ///< 备注
    std::vector<std::string> siblingNames{};   ///< 集合既有条目名（I-REQ-3）
};

/**
 * @brief 工况构造规格（§9.4 OperatingConditionSpec——createCondition 的
 *        输入面；负载/事件/适用范围以整表给出，逐项经
 *        validateOperatingCondition 校验）。
 */
struct OperatingConditionSpec {
    std::string name;                          ///< 语义名（集合内唯一）
    RequirementLevel level = RequirementLevel::Must;  ///< 需求等级（必验派生源——I-REQ-9）
    bool enabled = true;                       ///< 启用
    std::vector<core::ObjectId> environmentRefs;           ///< 环境障碍引用
    std::vector<RequirementReference> toolRefs;            ///< 工具引用
    std::vector<ConditionPayload> payloads;    ///< 负载
    std::vector<ConditionEvent> events;        ///< 事件
    std::optional<double> targetCycleTimeS;    ///< 目标节拍（s）
    RequirementDemand demands{};               ///< 要求值
    std::optional<std::uint32_t> verificationOrderHint;    ///< 必验顺序建议
    AppliesTo appliesTo{};                     ///< 适用范围
    std::string note;                          ///< 备注
    std::vector<std::string> siblingNames{};   ///< 集合既有条目名（I-REQ-3）
};

/**
 * @brief 条目构造产出（§9.4 CreateOutcome——两态：合法条目或定位错误；
 *        诊断列表参数为 §9.4 原文签名形状的契约预留位——本单元服务无
 *        已登记诊断码，错误经值面返回，§9.2/Errors.hpp 阶段纪律）。
 */
struct CreateOutcome {
    bool ok = false;                ///< true＝entry 有效；false＝error 有效
    TaskPoint point{};              ///< ok 态：任务点（work 段恒启用——占位表达段序）
    WorkRegion region{};            ///< ok 态：区域（三服务共用载体——按 create 来源读对应成员）
    OperatingCondition condition{}; ///< ok 态：工况
    RequirementError error{};       ///< err 态：首个违例错误（@错误 行逐项语义）
};

/**
 * @brief 顺序键拓扑校验产出（§9.4 SequenceCheckResult——R7 的独立入口）。
 *
 * 语义（I-REQ-7＋§5.1"顺序集＝按 key 拓扑消费"）：sequenceKey＝前驱
 * 条目**名称**（顺序边 prev→this）；链式表达线性顺序。
 *   - duplicateKeys：两条目声明同一前驱（同一位置两个后继＝顺序歧义）；
 *   - danglingKeys：前驱名不存在（引用悬空——R1 面的顺序特例）；
 *   - cycle：前驱链回环（Kahn 拓扑消去后仍有剩余节点）。
 * 三者均 Blocking（§8.1 R7）；acyclic＝三者全无。
 */
struct SequenceCheckResult {
    bool acyclic = true;                       ///< 顺序关系可线性化（无环＋无重复＋无悬空）
    std::vector<std::string> duplicateKeys{};  ///< 被重复声明的前驱名（升序去重——确定性）
    std::vector<std::string> danglingKeys{};   ///< 悬空前驱名（升序去重）
    std::vector<std::string> cycleNodes{};     ///< 环上条目名（升序去重；无环＝空）

    bool operator==(const SequenceCheckResult& o) const
    {
        return acyclic == o.acyclic && duplicateKeys == o.duplicateKeys
            && danglingKeys == o.danglingKeys && cycleNodes == o.cycleNodes;
    }
    bool operator!=(const SequenceCheckResult& o) const { return !(*this == o); }
};

/**
 * @brief 派生可派生性预检产出（§9.4 DerivePrecheck——T07 镜像/模板前的
 *        源条目预检面；T03 落最小真实实现：按源 id 定位条目并核对其
 *        姿态规则是否含引用型规则〔AlignFrame/AlignGeometryNormal〕——
 *        引用目标在镜像侧不存在的"不可镜像"清单）。
 */
struct DerivePrecheck {
    std::vector<core::ObjectId> notFound{};    ///< 清单中不存在的源 id（调用方错误面）
    std::vector<core::ObjectId> referenceRules{};  ///< 姿态为引用型规则的源（镜像侧目标可能缺席——T07 处置 PendingManualResolution）

    bool operator==(const DerivePrecheck& o) const
    {
        return notFound == o.notFound && referenceRules == o.referenceRules;
    }
    bool operator!=(const DerivePrecheck& o) const { return !(*this == o); }
};

// =====================================================================
// 三领域服务（§9.4 原文契约；实现形态＝"接口＋final 无状态实现"——
// Codec/CanonicalModelInputBuilder 同款，可默认构造随处持有）
// =====================================================================

/**
 * @brief 任务点服务（§9.4 行原文契约——纯函数；对工作集中 TaskPoint
 *        集合做查询/校验/批量构造）。
 */
class ITaskPointService {
public:
    virtual ~ITaskPointService() = default;

    /**
     * @brief 由参数构造一条合法任务点（§9.4 行原文——含位姿/容差/三段/
     *        来源标记；分配临时句柄）。
     *
     * 校验链（错误语义逐项——§9.4 @错误 行；短路返回首个违例）：
     *   ①名称空/与 siblingNames 重复 → DuplicateName（I-REQ-3 构造边界
     *     拒绝，不静默加后缀——NFR-COR-03）；
     *   ②容差非法（≤0/非有限）→ IllegalTolerance（I-REQ-5）；
     *   ③PointAtTarget 零向量目标 → ZeroVectorTarget（§5.3）；
     *   ④constrainedDof 全 false → AllDofFree（I-REQ-5）；
     *   ⑤引用/姿态规则其余参数非法 → IllegalTolerance（validateXxx 全链）。
     *
     * @param spec  [in] 构造规格（siblingNames 由编辑器从工作集填充）
     * @param diags [out] 诊断输出（追加不清空；本服务无已登记诊断码——
     *              恒不写入，§9.4 原文签名形状的契约预留位）
     * @return ok＝合法任务点（objectId＝新临时句柄；work 段恒启用——
     *         §4.3"work 段即任务点本身"）；err＝首个违例错误
     *
     * 纯函数；线程安全；确定性（同输入同错误/同条目值——objectId 除外，
     * 身份生成本质随机）。
     */
    virtual CreateOutcome createPoint(const TaskPointSpec& spec,
                                      std::vector<core::DiagnosticRecord>& diags) const = 0;

    /**
     * @brief 顺序键拓扑校验（§9.4 行原文——无环/重复——R7 的独立入口，
     *        供编辑器增量调用）。
     *
     * @param points [in] 任务点集合（任意序；含未启用条目——顺序是全集
     *               语义，与启用无关）
     * @return 三类违例的定位清单（acyclic＝三类全无；清单升序去重——
     *         确定性，NFR-COR-02）
     *
     * 纯函数；线程安全；确定性。
     */
    virtual SequenceCheckResult checkSequence(const std::vector<TaskPoint>& points) const = 0;

    /**
     * @brief 镜像/阵列/模板前的源条目可派生性预检（§9.4 行原文——不可
     *        镜像规则清单）。
     *
     * @param sources [in] 待派生源条目 id（工作集点集内定位）
     * @return notFound＝不存在的源 id；referenceRules＝姿态为引用型规则
     *         （AlignFrame/AlignGeometryNormal）的源——其引用目标在镜像
     *         侧可能缺席（T07 按卡处置 PendingManualResolution，本服务
     *         只列清单不代处置）
     *
     * 纯函数；线程安全；确定性（扫描序＝输入序）。
     */
    virtual DerivePrecheck precheckDerivation(const std::vector<core::ObjectId>& sources) const = 0;
};

/// 规范化采样定义产出（§9.4 NormalizedSampling——规范化后计数即
/// planContentIdentity 的输入，D-REQ-2）。
struct NormalizedSampling {
    PositionSampling normalized{};   ///< 规范化形态（恒 Grid；counts 即权威）
    bool changed = false;            ///< 输入是否为 GridBySpacing（true＝发生了规范化改写——编辑器提示面）

    bool operator==(const NormalizedSampling& o) const
    {
        return normalized == o.normalized && changed == o.changed;
    }
    bool operator!=(const NormalizedSampling& o) const { return !(*this == o); }
};

/// 区域构造产出（复用 CreateOutcome 载体——region 成员有效）。
using RegionCreateOutcome = CreateOutcome;

/// 工况构造产出（复用 CreateOutcome 载体——condition 成员有效）。
using ConditionCreateOutcome = CreateOutcome;

/**
 * @brief 工作区域服务（§9.4 行原文契约——区域构造/采样定义规范化
 *        （GridBySpacing→counts，D-REQ-2）/覆盖率目标校验）。
 */
class IWorkRegionService {
public:
    virtual ~IWorkRegionService() = default;

    /**
     * @brief 由参数构造一条合法区域（错误语义：DuplicateName〔I-REQ-3〕|
     *        DegenerateRegion〔I-REQ-6 盒退化/覆盖率越界〕|IllegalTolerance
     *        〔间距非法〕——§9.4 @错误 行族）。
     *
     * @param spec  [in] 构造规格
     * @param diags [out] 诊断输出（恒不写入——同 createPoint 契约预留位）
     * @return ok＝合法区域（objectId＝新临时句柄）；err＝首个违例错误
     *
     * 纯函数；线程安全；确定性。
     */
    virtual RegionCreateOutcome createRegion(const WorkRegionSpec& spec,
                                             std::vector<core::DiagnosticRecord>& diags) const = 0;

    /**
     * @brief 规范化采样定义（§9.4 行原文——确定性；规范化后计数即
     *        planContentIdentity 的输入，D-REQ-2；boxSize 为区域盒尺寸
     *        （m）——floor 规则的被除数，§5.2）。
     *
     * 规则（§5.2 原文）：GridBySpacing → Grid，counts[i] =
     * floor(size[i]/spacing[i]) + 1。Grid/Random 原样通过（changed=false
     * ——已是权威形态）。
     *
     * 确定性来源（acceptance 3——KIN-04 冻结前提）：除法/floor 均为
     * IEEE754 确定运算——同（size,spacing）输入必得同 counts，跨进程
     * 逐字节一致（NFR-COR-01/02）；同义计划（不同 spacing 规范化到同
     * counts）同身份——D-REQ-2。
     *
     * @param raw     [in] 原始采样定义（GridBySpacing 的 spacing 须三分量
     *                正有限——违约经 err 返回 IllegalTolerance）
     * @param boxSize [in] 区域盒尺寸（m；三分量正——违约经 err 返回
     *                DegenerateRegion）
     * @param diags   [out] 诊断输出（恒不写入——契约预留位同上）
     * @return ok＝规范化结果（normalized＋changed）；err＝输入非法
     *
     * 纯函数；线程安全；确定性。
     */
    virtual Expected<NormalizedSampling> normalizeSampling(
        const PositionSampling& raw, const rw::math::Vector3D<double>& boxSize,
        std::vector<core::DiagnosticRecord>& diags) const = 0;
};

/**
 * @brief 必验集合解析产出（§9.4 RequiredCaseResolution 载体复用——
 *        §6.2 冻结 schema 的解析面）。
 */
using RequiredCaseParse = RequiredCaseResolution;

/**
 * @brief 工况服务（§9.4 行原文契约——工况构造/必验派生解析（I-REQ-9）/
 *        适用范围校验）。
 */
class IOperatingConditionService {
public:
    virtual ~IOperatingConditionService() = default;

    /**
     * @brief 由参数构造一条合法工况（错误语义：DuplicateName〔I-REQ-3〕|
     *        IllegalTolerance〔负载/事件/节拍/适用范围参数非法〕——
     *        §9.4 @错误 行族）。
     *
     * @param spec  [in] 构造规格
     * @param diags [out] 诊断输出（恒不写入——契约预留位同上）
     * @return ok＝合法工况（objectId＝新临时句柄）；err＝首个违例错误
     *
     * 纯函数；线程安全；确定性。
     */
    virtual ConditionCreateOutcome createCondition(
        const OperatingConditionSpec& spec,
        std::vector<core::DiagnosticRecord>& diags) const = 0;

    /**
     * @brief 必验集合解析（§9.4 行原文——enabled∧Must；P-EV-9 冻结规则
     *        的唯一实现点）。
     *
     * 冻结 schema（§6.2 权威条款，逐条实现）：
     *   mandatory := (level == Must)
     *   entries[i] := { caseId: objectId, label: name,
     *                   enabled: enabled, mandatory: (level==Must) }
     *   必验集合 := { c | c.enabled ∧ (c.level==Must) }
     *
     * 确定性（§6.1"本单元保证 entries 解析确定性"——evidence
     * requiredCaseSetId 的对账前提）：entries 序＝输入集合的规范序
     * （ObjectId 字典序——同集合必得同序，同序必得同投影）；无环境/
     * locale/时钟依赖（NFR-COR-01/02）。I-REQ-9：mandatory 仅由 level
     * 派生——不存在独立第三"必验"开关（V-05 requirements 侧断言面）。
     *
     * @param conditions [in] 工况条目（任意序——内部按规范序投影）
     * @return 全量投影 entries＋必验计数 requiredCount（P-EV-9 唯一实现
     *         点——evidence 侧为字面消费对端，联合观测不写为通过）
     *
     * 纯函数；线程安全；确定性。
     */
    virtual RequiredCaseResolution resolveRequiredCases(
        const std::vector<OperatingCondition>& conditions) const = 0;
};

/// 三服务的产品实现（无状态——可默认构造，拷贝/移动平凡；§3.4 总约定 1）。
class TaskPointService final : public ITaskPointService {
public:
    CreateOutcome createPoint(const TaskPointSpec& spec,
                              std::vector<core::DiagnosticRecord>& diags) const override;
    SequenceCheckResult checkSequence(const std::vector<TaskPoint>& points) const override;
    DerivePrecheck precheckDerivation(const std::vector<core::ObjectId>& sources) const override;
};

class WorkRegionService final : public IWorkRegionService {
public:
    RegionCreateOutcome createRegion(const WorkRegionSpec& spec,
                                     std::vector<core::DiagnosticRecord>& diags) const override;
    Expected<NormalizedSampling> normalizeSampling(const PositionSampling& raw,
                                    const rw::math::Vector3D<double>& boxSize,
                                    std::vector<core::DiagnosticRecord>& diags) const override;
};

class OperatingConditionService final : public IOperatingConditionService {
public:
    ConditionCreateOutcome createCondition(const OperatingConditionSpec& spec,
                                           std::vector<core::DiagnosticRecord>& diags) const override;
    RequiredCaseResolution resolveRequiredCases(
        const std::vector<OperatingCondition>& conditions) const override;
};

// =====================================================================
// 需求档派生（§4.8——RequirementProfile 的派生入口；DerivedReadOnly，
// 重算即得、不入 canonical 编码）
// =====================================================================

/**
 * @brief 派生集合级需求档（§4.8——必验工况清单/Must-Should 计数/覆盖
 *        目标汇总/contentIdentity）。
 *
 * 必验清单经 service.resolveRequiredCases 复用（P-EV-9 单点——本函数
 * 不复制冻结规则，只做视图投影）。contentIdentity＝对派生档输入的
 * canonical 投影文本（必验清单 caseId/label/enabled/mandatory＋计数＋
 * 覆盖汇总，"\x1F" 分隔——确定性串）再经 core::ContentDigester 摘要；
 * 是派生档自身的指纹，不是任何持久化对象的 ContentVersion（§4.8）。
 *
 * @param points     [in] 任务点集合（Must/Should 计数输入）
 * @param regions    [in] 区域集合（计数＋覆盖汇总输入）
 * @param conditions [in] 工况集合（必验清单＋计数输入）
 * @param service    [in] 工况服务（resolveRequiredCases 复用入口）
 * @return 派生档（确定性：同输入集合同档——含 contentIdentity 逐字节
 *         一致，NFR-COR-01/02）
 *
 * 纯函数；线程安全；确定性。
 */
RequirementProfile deriveRequirementProfile(const std::vector<TaskPoint>& points,
                                            const std::vector<WorkRegion>& regions,
                                            const std::vector<OperatingCondition>& conditions,
                                            const IOperatingConditionService& service);

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_SERVICES_HPP
