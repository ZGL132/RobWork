/**
 * @file   Evaluator.hpp
 * @brief  评估器接口与注册表（③评估器端口所有者）——评估键/descriptor/
 *         调用约定接口/两注册表/注册清单 manifest。
 *
 * 设计依据：
 *   - units/evidence.md §9（§9.1 端口定位与调度分离、§9.2 EvaluatorDescriptor
 *     与依赖/能力声明、§9.3 IEngineeringEvaluator 与调用约定、§9.4
 *     EvaluatorRegistry 注册表行为冻结、§9.5 EvidenceProfileRegistry）、
 *     §3.1 组成表（Evaluator.hpp｜EvaluationKey、EvaluatorDescriptor、
 *     IEvaluatorFactory、IEngineeringEvaluator、EvaluationRequest/Output、
 *     IEvaluationContext、EvaluatorRegistry、RegistrationManifest、
 *     EvidenceProfileRegistry）、§10.2（评估器注册与调用时序）
 *   - 需求 EVI-01（模式效力分层——supportedModes 承载）、OPT-03/OPT-05
 *     （跨域组合经③端口协作——注册制 L5 装配）、SEL-05（选型消费传动
 *     映射评估器——同端口）、CON-06/AT-19（manifest 摘要跨进程一致）、
 *     CON-04（contractVersion 进入切片身份）
 *   - 任务契约 tasks/foundation/EV-T10.json（≙WP-05-T10）acceptance 1～3：
 *     EV-REG-1/2 注册边界/并发/manifest 稳定用例；不建调度器（D-12）；
 *     descriptor 依赖/能力声明（§9.2）与 EvidenceProfileRegistry（§9.5）
 *     行为用例
 *
 * 背景说明（本头在证据链上的位置）：
 *   evidence 拥有③评估器端口（ARCH §7.2）：**按评估键发现已注册评估器并
 *   调用**。跨域评估组合（优化消费运动学/碰撞/动力学校核、选型消费传动
 *   映射）经本端口协作，不反向链接业务实现（注册制，L5 装配——R-1 红线的
 *   实施形态：注册表只持 IEvaluatorFactory 接口，evidence 不链接任何业务
 *   单元，也不创建逐域转发包装器——域直接实现 IEngineeringEvaluator，
 *   NFR-MNT-04）。
 *
 * ★ 调度分离边界（§9.1/D-12——acceptance 2 的设计依据）：
 *   评估器调用与任务执行分离。evidence 只定义**计算接口**（本头）；执行
 *   调度、取消信号传递、进程控制与派发全部归 execution（ARCH §4）。因此：
 *   两张注册表内**没有**任务调度器、工作队列、取消通道或进度设施；本头
 *   也不实现任何此类设施。IEvaluationContext 只是评估器调用约定（取消
 *   查询/进度上报/对象读取的**接口**），其实现由宿主（execution/主进程）
 *   注入——evidence 对其零实现、零假定。
 *
 * ★ P-EX-7 处置（knownPitfalls 登记，governance-log P-EX-7）：
 *   评估器**执行期**能力声明（暂停支持/检查点粒度/强制终止代价）的承载
 *   位置不在本头——EvaluatorDescriptor 不含任何执行能力字段；该面由
 *   execution 侧经其注册表扩展声明 EvaluatorRuntimeCapabilities 承载
 *   （units/execution.md §5.5），登记为 evidence 交接项。本头以此为准：
 *   descriptor 的"能力声明"仅指工程评估能力（supportedModes/stateless/
 *   threadSafety，§9.2），与执行调度能力互不混装；两侧行为约束经各自
 *   单元卡登记，任何一侧不私改对端口径。
 *
 * 消费的既有 evidence 契约（本单元内组合）：
 *   DependencyDeclaration＋声明闭包校验器（Dependency.hpp——EV-T02 预登记
 *   "注册拒绝归 EvaluatorRegistry 消费"的拒绝原语）；AnalysisSnapshot/
 *   CaseId（Snapshot.hpp）；InputSlice（Slice.hpp）；EvidenceItem/
 *   DeterministicInfeasibilityProof/SearchExhaustedRecord/
 *   RequiredEvidenceProfile/validateEvidenceProfile/computeProfileContent
 *   Identity（Evidence.hpp）；DomainVerdictInputs（Verdict.hpp）；
 *   DomainPayload/EvidenceProfileRef（Envelope.hpp）。
 *   衔接面兑现（EV-T05 I-6/EV-T06 I-1 预登记"EV-T10 注册表直接适配"）：
 *   EvaluatorRegistry 实现 IProducerRegistryView（validateProof 的产生者
 *   查询面）；EvidenceProfileRegistry 实现 IProfileRegistryView
 *   （aggregateVerdict 的 Profile 查询面）——两注册表即视图本体，无需另建
 *   适配器（最小实现面——NFR-MNT-04 同源）。
 *
 * 线程模型（§9.4/§9.5 线程安全行）：
 *   注册期单线程约定（L5 装配，主进程与 worker 进程各执行同一注册清单）；
 *   运行期 find/manifest/create/findProfile 并发只读安全（内部
 *   std::shared_mutex 保护——注册与查询互斥、查询之间并行）；注册表对象
 *   本身不可拷贝/移动（含互斥量成员）。注册期之外调用 registerEvaluator/
 *   registerProfile 属调用方契约违约——不构成数据竞争（有锁保护）但违反
 *   "运行期不增删"语义（§9.4），行为按注册边界规则处理。
 *
 * 确定性（NFR-COR-02）：注册校验问题清单顺序固定（检查序见
 * RegistrationIssueCode 声明序）；manifest 按 key 字典序排序、其摘要对
 * 规范编码计算（同注册集必得同摘要——主/worker 比对的前提，CON-06）。
 */

#ifndef SDURWS_IRD_EVIDENCE_EVALUATOR_HPP
#define SDURWS_IRD_EVIDENCE_EVALUATOR_HPP

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Dependency.hpp>
#include <sdurws/ird/evidence/Envelope.hpp>
#include <sdurws/ird/evidence/Errors.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/evidence/Slice.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/evidence/Verdict.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sdurws::ird::evidence {

// =====================================================================
// 评估器并发能力词表（§9.2 ThreadSafety 行）
// =====================================================================

/**
 * @brief 评估器的线程安全档位（§9.2 原文三值——descriptor 注册时一次性
 *        声明，运行期只读）。
 *
 * 语义（调用方据此决定并发策略；evidence 不执行并发控制——那是调用方的
 * 责任，注册表只承载声明）：
 *   - SingleThread：评估器只允许单线程调用（一次一个 evaluate 进入）；
 *   - ConcurrentReadOnly：多线程并发 evaluate 安全（只读共享输入）；
 *   - FullyThreadSafe：任意并发（含内部缓存写路径）安全。
 *
 * 可重入性契约（§9.3）：evaluate 的并发安全承诺完全由本声明表达——
 * 注册表与调用框架不做二次校验（声明即契约，虚报属域实现缺陷）。
 */
enum class ThreadSafety : std::uint8_t {
    SingleThread,       ///< 单线程专用（一次一个调用）
    ConcurrentReadOnly, ///< 并发只读安全
    FullyThreadSafe,    ///< 完全线程安全
};

/**
 * @brief ThreadSafety 值合法性（§9.4 注册期验证"threadSafety 合法"的
 *        纯函数承载）。
 *
 * @param threadSafety [in] 待检档位
 * @return 三值词表之一 true
 *
 * 边界说明：C++ 枚举经 static_cast 可携带词表外位型（如反序列化损坏或
 * 内存破坏的 descriptor）——诚构造路径不可能产生非法值，本检查是注册
 * 门禁对**位型完整性**的防御面（与"任一失败即拒绝并指明字段"配套）；
 * 词表封闭，新增档位＝设计变更（走单元卡增量修订）。
 */
inline bool isKnownThreadSafety(ThreadSafety threadSafety) noexcept
{
    switch (threadSafety) {
    case ThreadSafety::SingleThread:
    case ThreadSafety::ConcurrentReadOnly:
    case ThreadSafety::FullyThreadSafe:
        return true;
    }
    // 词表外位型（static_cast 注入）落入此处——注册期拒绝面。
    return false;
}

// =====================================================================
// 评估键与评估器描述符（§9.2）
// =====================================================================

/**
 * @brief 评估键（§9.2 原文类型名 EvaluationKey——如 "kin-batch-ik"）。
 *
 * 承载口径（D-5 收敛，登记单元卡 v1.1）：语义别名＝std::string，词形
 * 闸门＝isValidEvaluationKey（Slice.hpp——§8.2 原文语法
 * [a-z][a-z0-9-]{1,63}；注意评估键**不含点**，与依赖键词形不同）。
 * 刻意不做强包装类型：§9.4 明文把"key 语法"列为**注册期校验项**（"任一
 * 失败即拒绝并指明字段"）——若词形非法值在构造期即不可存在，该项校验
 * 将空转、设计行为的拒绝面被架空；词形违例的捕获点按设计固定在注册期
 * （EvaluatorRegistry）与编码期（SliceBuilder），与既有承载
 * （InputSlice.evaluationKey 等）同一口径。类型别名使新接口以设计名
 * EvaluationKey 书写（§3.1 组成表的概念名落地）。
 *
 * 语义约束：评估键在注册表内唯一（§9.4 重复注册拒绝）；同键不同
 * contractVersion 是**同一评估器算法契约的版本演进**（换版本＝装配清单
 * 变更——§9.4 版本冲突行，不存在运行期热替换）。
 */
using EvaluationKey = std::string;

/**
 * @brief 评估器描述符（§9.2——注册时一次性声明，运行期只读）。
 *
 * 生命周期：由域工厂（IEvaluatorFactory）持有并经 descriptor() 只读
 * 返回；注册表不拷贝存储整个 descriptor，只提取注册所需字段（键/契约
 * 版本/Profile 身份——manifest 载荷）。调用方不得在注册后修改 descriptor
 * （"运行期只读"契约——违反属域实现缺陷，无运行期防护）。
 *
 * 值语义；线程安全：注册后按不可变对待（并发只读安全）。
 *
 * ★ 字段面冻结（P-EX-7）：本结构恰含 §9.2 字段表七字段——**不含任何
 * 执行期能力字段**（暂停/检查点粒度/强制终止代价归 execution 侧注册表
 * 扩展声明，见文件头 P-EX-7 处置）；测试侧以结构化绑定钉死成员数与
 * 成员序（字段面漂移即编译失败）。
 */
struct EvaluatorDescriptor {
    /// 评估键（EvaluationKey 词形——isValidEvaluationKey；§9.4 注册期
    /// 语法校验，重复键拒绝）。
    EvaluationKey key;
    /// 输入/输出契约版本（无量纲版本号；>0——§9.4 注册期校验；进入
    /// 切片身份 sliceId——CON-04，同输入不同契约版本＝不同切片）。
    std::uint32_t contractVersion = 0;
    /// 依赖声明（§4.2.1/§9.2——注册期闭包校验〔§4.2.3①〕：声明语法/
    /// 必需性配对/条件闭包 referencedKeys ⊆ 已声明键 ∪ 快照事实键；
    /// D-10：条件输入未入切片的声明在注册期即被拒绝）。
    std::vector<DependencyDeclaration> inputs;
    /// 证据 Profile 声明引用（{profileId, version}——§9.2"注册时必须已
    /// 可解析"：EvaluatorRegistry 按 (profileId, version) 对
    /// EvidenceProfileRegistry 解析，未注册即拒绝）。承载说明（实现口径
    /// R-3，登记单元卡 v1.1）：复用 §7.1 的 EvidenceProfileRef 三元组
    /// 结构，但本字段只消费其声明面——contentIdentity 由 evidence 于
    /// Profile 注册时计算（域不可申报，§9.5），评估器申报非零值即注册
    /// 拒绝（防域侧伪造绑定身份；manifest 的 profileIdentity 恒取注册表
    /// 权威值）。域按"注册 Profile 在前、评估器注册在后"的接入序构造
    /// 本字段（§13）。
    EvidenceProfileRef profile;
    /// 支持的评估模式（core::EvaluationMode 的子集——Preview/Quick/
    /// Verified，≥1：§9.4 注册期"模式集非空"校验；"子集"语义＝无重复）。
    /// EVI-01 表 1：模式是证据效力属性（Preview 不产生 envelope、Quick
    /// 不得单独支撑正式通过）——评估器声明其支持哪些效力层级，汇总与
    /// 包络层按请求模式执行对应门禁。
    std::vector<core::EvaluationMode> supportedModes;
    /// 无跨调用状态标志（true＝评估器可在多次 evaluate 之间共享实例——
    /// §9.4"无状态评估器建议 stateless=true＋共享实例"；false＝每次经
    /// factory.create() 取独立实例）。
    bool stateless = false;
    /// 线程安全档位（isKnownThreadSafety 词表——§9.4 注册期合法性校验；
    /// 可重入性承诺，见枚举注释）。
    ThreadSafety threadSafety = ThreadSafety::SingleThread;

    /// 成员精确等值（测试/装配核对用；manifest 身份判定以摘要为准）。
    bool operator==(const EvaluatorDescriptor& o) const
    {
        return key == o.key && contractVersion == o.contractVersion
            && inputs == o.inputs && profile == o.profile
            && supportedModes == o.supportedModes && stateless == o.stateless
            && threadSafety == o.threadSafety;
    }
    bool operator!=(const EvaluatorDescriptor& o) const { return !(*this == o); }
};

// =====================================================================
// 评估调用约定（§9.3——数据面＋两接口＋宿主上下文）
// =====================================================================

/**
 * @brief 一次评估调用的请求（§9.3 EvaluationRequest——派发时由调用方
 *        〔execution/域〕组装）。
 *
 * 生命周期：调用方持有并保证 evaluate() 调用期间存活（const 引用传入
 * ——评估器不得修改请求；快照/切片为冻结不可变值，§4.1.5/§4.2.3）。
 * 值语义；线程安全：纯值（各线程持各自请求）。
 */
struct EvaluationRequest {
    /// 被评估任务身份（五元组——派发时绑定，execution 分配；进入结果
    /// 包络的任务绑定面，TASK-02/§7.1）。
    core::TaskIdentity task;
    /// 本次评估模式（core::EvaluationMode——Preview 请求合法：域做草稿
    /// 预览，但不产生 envelope〔表 1/EVI-01〕；效力门禁在包络构造边界
    /// 与汇总层执行，评估器按模式自选证据效力口径）。
    core::EvaluationMode mode = core::EvaluationMode::Verified;
    /// 来源快照（物化或引用形态——worker 场景为物化，§10.1⑤；评估器经
    /// context.tryObjectBytes 读取物化对象字节）。
    AnalysisSnapshot snapshot;
    /// 冻结输入切片（评估器按 slice.entries 索引导入——§10.2 步骤①；
    /// 切片已含评估键与契约版本〔进 sliceId，CON-04〕）。
    InputSlice slice;
    /// 分批评估的工况子集（覆盖矩阵跨批次汇总——EVI-02/KIN-04；空集
    /// ＝不做工况分批〔全量一次〕，语义由域评估器自解释）。
    std::vector<CaseId> caseSubset;
};

/**
 * @brief 评估器产出（§9.3 EvaluationOutput——域证据的承载）。
 *
 * ★ envelope 不在此处：结果包络由**调用侧**（execution/域）经
 * aggregateVerdict（Verdict.hpp）＋ResultEnvelope::make（Envelope.hpp）
 * 组装——评估器只产出素材（证据/证明/搜索未果/判定输入/载荷/诊断），
 * 判定与构造校验分离（§9.3 注释原文；D-08/D-09 的职责边界）。
 *
 * 值语义；线程安全：纯值。
 */
struct EvaluationOutput {
    /// 领域证据（§6.2 EvidenceItem——含证明/搜索未果记录的域内素材；
    /// presence 纪律由 validateEvidenceItems 在汇总/包络层核对）。
    std::vector<EvidenceItem> evidence;
    /// 确定性不可行证明（可选——§6.3；存在性≠有效性：汇总层必经
    /// validateProof 字段级校验〔D-09〕，评估器不自我宣告有效性）。
    std::optional<DeterministicInfeasibilityProof> proof;
    /// 搜索未果记录（可选——§6.3 末：C5/C8"搜索未找到有效解"口径；
    /// 汇总层判 DataInsufficient 附该记录，不得输出不可行结论）。
    std::optional<SearchExhaustedRecord> searchRecord;
    /// 域判定输入（Must/Should 违例——REQ-06 口径；evidence 只汇总
    /// 定级与透传，PA-1：违例语义归域）。
    DomainVerdictInputs verdictInputs;
    /// 域结果载荷（canonical 编码，域契约——evidence 视为不透明字节；
    /// 仅 Completed 组合允许携带，表 3 行 3 由包络构造边界强制）。
    std::optional<DomainPayload> payload;
    /// 诊断记录（域自选轨——§9.3 契约"抛 EvidenceError 或返回诊断，
    /// 域自选，约定登记于域任务卡"；码值权威归 diagnostics，PA-1）。
    std::vector<core::DiagnosticRecord> diagnostics;
};

/**
 * @brief 评估调用上下文（§9.3 IEvaluationContext——调用约定接口；实现
 *        由宿主〔execution/主进程〕注入）。
 *
 * ★ 本接口在 evidence 内**零实现**（D-12 调度分离）：取消、进度、对象
 * 读取的设施全部归宿主；evidence 只定义评估器与宿主之间的对话形状。
 * 测试以本地替身实现（§11 测试设施——替身只验证契约，EV-REG-3 边界）。
 *
 * 生命周期：宿主持有，evaluate() 调用期间存活（引用传入）。
 * 线程约束：evaluate 在单线程内使用其 context 引用；实现方自行保证
 * 内部状态安全（与评估器的并发档位正交）。
 */
class IEvaluationContext {
public:
    virtual ~IEvaluationContext() = default;

    /**
     * @brief 查询式取消（协作取消——§9.1/§9.3：非 evidence 调度设施，
     *        取消信号的传递与生效归 execution）。
     * @return 宿主已请求取消 true；评估器必须周期性查询本接口（§9.3
     *         契约"长评估必须周期性查询 cancellationRequested"），观测
     *         到取消后自主选择退出路径（抛错/返回部分产出＋诊断——域
     *         契约自选），evidence 不代为中断。
     */
    virtual bool cancellationRequested() const = 0;

    /**
     * @brief 进度上报（§9.1：进度**设施**归宿主——本方法只是上报通道；
     *        evidence 不存储、不聚合进度）。
     * @param percent [in] 进度百分比，0～100（无量纲；越界值由实现方
     *                自行取舍——evidence 不校验）
     * @param phase   [in] 当前阶段的人读短语（如 "solve-batch"；编码
     *                安全下限归实现方）
     */
    virtual void reportProgress(std::uint8_t percent, std::string_view phase) = 0;

    /**
     * @brief 物化快照对象读取（§9.3——与 IObjectBytesSource 同形：
     *        worker 场景的快照载荷由 execution 物化后经本接口供给，
     *        §3.3"worker 进程……同样零 project 依赖"）。
     *
     * @param objectId      [in] 目标对象身份（逻辑对象——ARC-04）
     * @param contentVersion [in] 期望内容版本（请求切片条目所锚定的
     *                版本——读取结果必须与切片条目一致，否则评估输入
     *                与冻结切片错配）
     * @return 对象字节（存在且版本相符）；nullopt＝对象不可得（评估器
     *         据此走证据缺失/诊断路径，evidence 不代答）
     */
    virtual std::optional<std::vector<std::uint8_t>>
    tryObjectBytes(core::ObjectId objectId, core::ContentVersion contentVersion) const = 0;
};

/**
 * @brief 工程评估器接口（§9.3 IEngineeringEvaluator——③端口的被调方；
 *        域直接实现，evidence 不建逐域转发包装器——NFR-MNT-04）。
 *
 * 实现契约（§9.3 注释原文的机械化）：
 *   - 确定性：同（request 切片，环境）→ 等价输出（NFR-COR-02）；
 *   - 纯计算：不派发任务、不写项目、不产生修订、不自我注册（L2 计算内
 *     核纪律；写路径权威归 project——PA-1）；
 *   - 错误轨：抛 EvidenceError（评估域错误码）或返回诊断——域自选，
 *     约定登记于域任务卡（§2.1 异常行）；
 *   - 可重入性＝descriptor.threadSafety 声明；
 *   - 长评估必须周期性查询 context.cancellationRequested()（§9.3）。
 */
class IEngineeringEvaluator {
public:
    virtual ~IEngineeringEvaluator() = default;

    /// 评估器描述符（注册期已验证；运行期只读——返回引用须指向稳定
    /// 存储，通常为实现对象的成员）。
    virtual const EvaluatorDescriptor& descriptor() const = 0;

    /**
     * @brief 执行一次评估（§9.3 调用约定的全部责任见类注释）。
     *
     * @param request [in] 评估请求（调用方持有；调用期间有效）
     * @param context [in] 宿主注入的调用上下文（取消/进度/对象读取）
     * @return 评估产出（envelope 由调用侧组装——结构体注释）
     *
     * @throws EvidenceError 域评估失败（错误码约定登记于域任务卡）；
     *         evidence 不捕获、不转译域异常（D-15：进程内抛传）
     */
    virtual EvaluationOutput evaluate(const EvaluationRequest& request,
                                      IEvaluationContext& context) = 0;
};

/**
 * @brief 评估器工厂（§9.3 IEvaluatorFactory——实例创建；注册表持工厂
 *        共享，实例由调用方经 create() 取得并独占）。
 *
 * worker 模型（§9.4 实例/生命周期行）：主进程与 worker 进程各执行同一
 * 注册清单（同工厂列表）；worker 内各建实例（隔离性），无状态评估器
 * （stateless=true）可共享实例。实现须保证 create() 线程安全（§9.4
 * "create() 线程安全"——注册表在锁外调用它）。
 */
class IEvaluatorFactory {
public:
    virtual ~IEvaluatorFactory() = default;

    /// 本工厂产出的评估器描述符（注册期验证对象；多次调用须返回同一
    /// 值——注册表按首次返回值登记）。
    virtual const EvaluatorDescriptor& descriptor() const = 0;

    /**
     * @brief 创建评估器实例（所有权随 unique_ptr 转移给调用方）。
     * @return 新实例（实现方保证非空——返回空指针属工厂契约违约，
     *         注册表创建路径视其为调用方错误）
     */
    virtual std::unique_ptr<IEngineeringEvaluator> create() const = 0;
};

// =====================================================================
// 注册清单 manifest（§9.4 主/worker 一致性行——CON-06/AT-19 装配侧保障）
// =====================================================================

/**
 * @brief 注册清单单条目（§9.4 manifest 行：{key, contractVersion,
 *        profileIdentity}）。
 *
 * 值语义；线程安全：纯值。
 */
struct RegistrationManifestEntry {
    /// 评估键（字典序排序键——§9.4"按 key 字典序排序"）。
    EvaluationKey key;
    /// 输入/输出契约版本（注册时 descriptor 申报值）。
    std::uint32_t contractVersion = 0;
    /// 绑定 Profile 的内容身份（EvidenceProfileRegistry 注册时计算的
    /// 权威值——非 descriptor 申报值，§9.5"域不可申报"）。
    core::ContentIdentity profileIdentity;

    bool operator==(const RegistrationManifestEntry& o) const
    {
        return key == o.key && contractVersion == o.contractVersion
            && profileIdentity == o.profileIdentity;
    }
    bool operator!=(const RegistrationManifestEntry& o) const { return !(*this == o); }
};

/**
 * @brief 注册清单及其摘要（§9.4 主/worker 一致性——execution 在派发/
 *        握手时比对两侧摘要，不一致即拒绝派发：同一快照跨入口/跨进程
 *        同一评估器同一判定的**装配侧**保障，CON-06/AT-19）。
 *
 * 确定性（NFR-COR-02）：entries 按 key 字典序升序；digest 为清单的
 * canonical 编码摘要（magic "IRDRGM1"＋codec 版本＋逐条目规范编码——
 * 实现见 src/Evaluator.cpp 头注释；非往返载体，唯一消费方式＝摘要）。
 * 同一注册集（任意注册顺序）必得同一 entries 序与同一 digest。
 *
 * 值语义；线程安全：纯值。
 */
struct RegistrationManifest {
    /// 清单条目（按 key 字典序升序——§9.4 排序稳定性即 EV-REG-2 观测点）。
    std::vector<RegistrationManifestEntry> entries;
    /// 清单摘要（SHA-256，经 core::ContentDigester——CR-02 摘要唯一；
    /// 空注册表也有确定摘要——两侧空装配一致）。
    core::ContentIdentity digest;

    bool operator==(const RegistrationManifest& o) const
    {
        return entries == o.entries && digest == o.digest;
    }
    bool operator!=(const RegistrationManifest& o) const { return !(*this == o); }
};

// =====================================================================
// EvidenceProfileRegistry（§9.5——域证据 Profile 注册表）
// =====================================================================

/**
 * @brief 证据 Profile 注册表（§9.5——域按需求 §8.1 表 4 实例化
 *        RequiredEvidenceProfile 后在此注册；评估器注册期验证依赖此表）。
 *
 * 行为冻结（§9.5 原文）：
 *   - 重复 (profileId, version) 注册 → 拒绝（EvidenceError〔ProfileDuplicate〕
 *     ——不覆盖、不静默；同域多版本可共存〔版本是键的一部分〕）；
 *   - contentIdentity 由 evidence 于注册时计算（canonical 编码摘要，
 *     computeProfileContentIdentity——**域不可申报**：调用方携带的
 *     contentIdentity 一律被注册值覆盖）；
 *   - 注册期校验（validateEvidenceProfile——item 语法/条件词形/替代
 *     标志与 itemClass 一致性/Common 禁止域登记）；条件闭包经注册顺序
 *     解耦：Profile 条件的 referencedKeys **语法校验**在本表注册时做，
 *     与评估器声明键的**交叉校验**在评估器注册时做（EvaluatorRegistry
 *     ——§9.5 原文）；
 *   - 替代标志仅限非 Common 类（validateEvidenceProfile 承载）。
 *
 * 线程安全（§9.5"线程安全同 9.4"）：注册期单线程约定（装配）；运行期
 * findProfile 并发只读安全；返回指针指向注册表内对象（std::map 节点
 * 稳定——后续注册不影响既有指针有效性），注册后 Profile 内容按不可变
 * 对待。不可拷贝/移动（互斥量成员）。
 *
 * 生命周期：由 L5 装配持有；EvaluatorRegistry 以 const 引用绑定本表
 * （构造注入——Profile 表必须先于评估器注册表构造、后于其析构）。
 */
class EvidenceProfileRegistry final : public IProfileRegistryView {
public:
    EvidenceProfileRegistry() = default;

    /// 值语义禁用（互斥量成员——运行期实例与装配期绑定引用一一对应）。
    EvidenceProfileRegistry(const EvidenceProfileRegistry&) = delete;
    EvidenceProfileRegistry& operator=(const EvidenceProfileRegistry&) = delete;

    /**
     * @brief 注册一个域 Profile（§9.5——重复拒绝→内容校验→计算内容
     *        身份→入库；检查序固定，同坏注册必报同一首错，NFR-COR-02）。
     *
     * @param profile [in] 待注册 Profile（按值传入——注册表存储盖写
     *                contentIdentity 后的副本）；注册后不可变更
     *
     * @throws EvidenceError（ProfileDuplicate）同 (profileId, version)
     *         已注册——注册边界拒绝（§9.5；detail 携带重复键）
     * @throws EvidenceError（ProfileInvalid）validateEvidenceProfile
     *         问题清单非空——detail 聚合全部问题（逐条含 itemId/下标）
     *
     * 复杂度：O(I log I)（校验＋身份计算的规范化排序，I＝项数）——
     * 注册期一次性，无热点。
     */
    void registerProfile(RequiredEvidenceProfile profile);

    /**
     * @brief 按 (profileId, version) 精确查找（IProfileRegistryView
     *        适配——EV-T06 I-1 预登记的衔接面；aggregateVerdict 的
     *        Profile 查询经本视图）。
     *
     * @param profileId [in] 域 id（五域词表——未注册一律 nullptr）
     * @param version   [in] 域登记版本
     * @return 已注册 Profile 的只读指针（注册表内对象——调用方不接管
     *         所有权；指针在注册表存活期有效）；未注册返回 nullptr
     *         （查询非抛——§9.4 未知键语义同源）
     *
     * 线程安全：并发只读安全（共享锁）。
     */
    const RequiredEvidenceProfile* findProfile(std::string_view profileId,
                                               std::string_view version) const override;

private:
    /// 注册表本体：profileId → (version → Profile)（嵌套 map＝二元组
    /// 键的字典序承载；节点稳定保证 findProfile 指针跨注册有效；
    /// std::less<> 透明比较器——string_view 查询免拷贝）。
    std::map<std::string, std::map<std::string, RequiredEvidenceProfile, std::less<>>,
             std::less<>>
        m_profiles;
    /// 读写锁：注册排他、查询共享（§9.5"线程安全同 9.4"；mutable——
    /// const 查询路径加锁）。
    mutable std::shared_mutex m_mutex;
};

// =====================================================================
// 注册期校验（§9.4 注册期验证行的纯函数承载——拒绝原语供注册表消费）
// =====================================================================

/**
 * @brief 评估器注册期校验问题码（§9.4 注册期验证行＋§9.5 交叉校验的
 *        逐字段定位面）。枚举顺序＝检查序（确定性输出——NFR-COR-02）；
 *        值一经交付不得改动/插入，只能表尾追加并留痕。
 */
enum class RegistrationIssueCode : std::uint8_t {
    // ---- descriptor 完整性（§9.4"descriptor 完整性"括注五项）----
    KeySyntaxInvalid,              ///< key 词形非法（isValidEvaluationKey——评估键不含点）
    ContractVersionZero,           ///< 契约版本为 0（§9.4"契约版本>0"——0 保留值不入身份）
    SupportedModesEmpty,           ///< 支持模式集为空（§9.4"模式集非空"——≥1）
    SupportedModesDuplicate,       ///< 支持模式集含重复项（"子集"语义——§9.2）
    ThreadSafetyUnknown,           ///< threadSafety 词表外位型（isKnownThreadSafety）
    ProfileContentIdentityDeclared,///< descriptor.profile.contentIdentity 非零（域不可申报——§9.5；实现口径 R-3）
    // ---- Profile 解析与交叉校验（§9.4"Profile 已注册且版本可解析"＋
    //      §9.5"与评估器声明的交叉校验在评估器注册时做"）----
    ProfileUnresolvable,           ///< (profileId, version) 未注册（§9.2"注册时必须已可解析"）
    ProfileConditionKeyOutOfClosure, ///< Profile 条件 referencedKeys ⊄ 声明键 ∪ 快照事实键（§9.5 交叉校验）
    // ---- 依赖声明闭包（§9.4"依赖声明闭包校验（§4.2.3①）"——细节经
    //      DependencyIssueCode 承载，本码只标注类别；拒绝码面＝
    //      DeclarationInvalid〔EV-T02 预登记契约〕）----
    DeclarationIssue,              ///< 依赖声明校验问题（映射自 DependencyIssue 清单）
};

/**
 * @brief 单条注册期校验问题（可定位：字段名＋机器码＋中文说明——
 *        §9.4"任一失败即拒绝并指明字段"的承载）。
 */
struct RegistrationIssue {
    RegistrationIssueCode code; ///< 机器可判别问题码（检查序）
    std::string field;          ///< 涉事 descriptor 字段名（"key"/"contractVersion"/
                                ///  "supportedModes"/"threadSafety"/"profile"/
                                ///  "profile.contentIdentity"/"inputs"）
    std::string message;        ///< 中文开发诊断（定位取值与原因；供注册拒绝消息聚合）
};

/**
 * @brief 评估器注册期校验器（§9.4 注册期验证行的非抛出纯函数承载）。
 *
 * 检查序（固定——同坏 descriptor 必得同问题清单，NFR-COR-02；顺序对齐
 * §9.4 原文行文：descriptor 完整性字段 → Profile 解析 → Profile 交叉
 * 校验 → 依赖声明闭包）：
 *   1. key 词形（isValidEvaluationKey——评估键不含点）；
 *   2. 契约版本 >0（0＝保留值）；
 *   3. 支持模式集非空且无重复（"子集，≥1"语义）；
 *   4. threadSafety 词表合法（isKnownThreadSafety——位型防御）；
 *   5. profile.contentIdentity 保留值（域不可申报——§9.5，实现口径 R-3）；
 *   6. (profileId, version) 在 Profile 注册表可解析（§9.2——未注册即拒）；
 *   7. 已解析 Profile 各项条件 referencedKeys ⊆ 声明键 ∪ 快照事实键
 *      （§9.5 交叉校验——经注册顺序解耦的下半场；仅在第 6 步通过时执行）；
 *   8. 依赖声明闭包（validateDependencyDeclarations——§4.2.3①，问题
 *      逐条映射为 DeclarationIssue，细节在 message）。
 *
 * @param descriptor       [in] 待检描述符（注册候选；调用方持有）
 * @param profileRegistry  [in] Profile 注册表（解析与交叉校验的事实面；
 *                         调用方保证调用期间存活且不被并发修改——装配
 *                         期单线程约定）
 * @param snapshotFactKeys [in] 快照事实键（§4.2.3①：由注册方注入——
 *                         evidence 不拥有其词表；闭包核对用）
 *
 * @return 问题清单（空＝通过；顺序＝检查序）
 *
 * 线程安全：可重入纯函数（对两注册表只读——Profile 查询走其共享锁）。
 */
std::vector<RegistrationIssue> validateEvaluatorRegistration(
    const EvaluatorDescriptor& descriptor,
    const EvidenceProfileRegistry& profileRegistry,
    const std::vector<std::string>& snapshotFactKeys);

/**
 * @brief 注册期校验的 fail-fast 轨（§9.4"任一失败即拒绝并指明字段"的
 *        拒绝原语——EvaluatorRegistry::registerEvaluator 消费）。
 *
 * 拒绝码面（EV-T02 预登记契约的兑现——依赖声明闭包问题独立成码）：
 *   - descriptor 完整性/Profile 解析/交叉校验问题（RegistrationIssueCode
 *     前 8 值）→ EvidenceError（EvaluatorDescriptorInvalid），detail 聚合
 *     全部问题（"[字段] 说明; …"——一次注册可看全）；
 *   - 仅依赖声明闭包问题（DeclarationIssue）→ EvidenceError
 *     （DeclarationInvalid）——§4.2.3① 注册期拒绝的既有码面
 *     （Dependency.hpp requireValidDependencyDeclarations 同源）；
 *   - 两类并存时优先报 descriptor 侧（检查序首类——确定性首错，
 *     NFR-COR-02 builder 惯例）；通过则静默返回。
 *
 * @param descriptor       [in] 同 validateEvaluatorRegistration
 * @param profileRegistry  [in] 同 validateEvaluatorRegistration
 * @param snapshotFactKeys [in] 同 validateEvaluatorRegistration
 *
 * @throws EvidenceError（EvaluatorDescriptorInvalid 或 DeclarationInvalid，
 *         按上述码面）任一问题存在——调用方注册契约违约，fail-fast
 *
 * 线程安全：可重入纯函数（抛错路径同）。
 */
void requireValidEvaluatorRegistration(
    const EvaluatorDescriptor& descriptor,
    const EvidenceProfileRegistry& profileRegistry,
    const std::vector<std::string>& snapshotFactKeys);

// =====================================================================
// EvaluatorRegistry（§9.4——③端口注册表；行为冻结表逐行承载）
// =====================================================================

/**
 * @brief 评估器注册表（§9.4——持工厂接口、装配期注册、manifest 摘要供
 *        主/worker 比对；D-12：不建调度设施，与执行分界）。
 *
 * 行为冻结（§9.4 表逐行——acceptance 1 的规则来源）：
 *   - 注册时机：L5 装配期（主进程与 worker 进程各执行同一注册清单）；
 *     运行期不增删（运行期调用注册属调用方契约违约——本类不加运行期
 *     状态位拦截〔§9.4 未定义"运行期"判据〕，语义约束登记于此）；
 *   - 重复注册：同 evaluationKey 再次注册 → 注册边界拒绝
 *     （EvidenceError〔EvaluatorDuplicate〕）——不覆盖、不静默；同键
 *     不同 contractVersion 的注册＝同键重复，同样拒绝；**换版本＝装配
 *     清单变更**（新版本新清单，跨进程一致），不存在运行期热替换；
 *   - 未知键：find/create 返回 nullptr（查询非抛）；调用方给"评估器
 *     不可用"诊断（EV-REG-1 观测点——诊断装配在调用方）；
 *   - 注册期验证：requireValidEvaluatorRegistration（字段完整性＋
 *     Profile 解析＋交叉校验＋声明闭包——任一失败即拒绝并指明字段）；
 *   - 实例/生命周期：注册表持 factory（共享、独占所有权）；实例由
 *     调用方经 find/create 取得并独占；无状态评估器建议 stateless=
 *     true＋共享实例（声明面——共享策略由调用方按 descriptor 执行）；
 *   - 线程安全：注册期单线程约定（装配）；运行期 find/manifest/create
 *     并发只读安全（std::shared_mutex——注册排他、查询共享）；create
 *     的工厂调用在锁外执行（工厂运行期存活由"运行期不增删"保证）；
 *   - 主/worker 一致性：manifest() 返回按 key 字典序排序的清单及其
 *     摘要（RegistrationManifest——两侧装配同一清单必得同摘要）；
 *   - 反向链接禁止：只持 IEvaluatorFactory 接口；不链接任何业务单元
 *     （R-1/R-2）；不创建逐域转发包装器（NFR-MNT-04）。
 *
 * 生命周期：由 L5 装配持有；构造时绑定 EvidenceProfileRegistry const
 * 引用（注册期验证的解析面——其必须先于本表构造、后于其析构）。
 * 不可拷贝/移动（引用成员＋互斥量）。
 */
class EvaluatorRegistry final : public IProducerRegistryView {
public:
    /**
     * @brief 构造（绑定 Profile 注册表——§10.2 注册期验证"闭包/Profile"
     *        需要 Profile 解析面）。
     *
     * @param profileRegistry [in] Profile 注册表引用（长期存活——见类
     *                        注释生命周期；本表不接管所有权）
     */
    explicit EvaluatorRegistry(const EvidenceProfileRegistry& profileRegistry);

    /// 值语义禁用（引用成员＋互斥量——装配期一对一绑定）。
    EvaluatorRegistry(const EvaluatorRegistry&) = delete;
    EvaluatorRegistry& operator=(const EvaluatorRegistry&) = delete;

    /**
     * @brief 注册一个评估器工厂（§9.4 注册时机/重复注册/注册期验证
     *        三行的落地——L5 装配期调用）。
     *
     * 检查序（固定——同坏注册必报同一首错，NFR-COR-02）：
     *   ①factory 非空（调用方契约违约 fail-fast）；
     *   ②重复键检查（注册边界先于内容校验——重复注册与 descriptor
     *     品质无关，§9.4 重复注册行；detail 携带已注册键与其契约版本）；
     *   ③requireValidEvaluatorRegistration（完整性→Profile→声明闭包；
     *     码面分工见该函数注释——"指明字段"由 issue 聚合保证）；
     *   ④登记：键 → {factory, 契约版本, Profile 权威内容身份}。
     *
     * @param factory          [in] 工厂（注册表取得独占所有权；运行期
     *                         存活——运行期不增删）
     * @param snapshotFactKeys [in] 快照事实键（声明闭包与 Profile 交叉
     *                         校验用——§4.2.3①；由注册方按其快照事实
     *                         给出，evidence 不拥有词表）
     *
     * @throws EvidenceError（EvaluatorDuplicate）同键重复注册
     * @throws EvidenceError（EvaluatorDescriptorInvalid）descriptor 完整
     *         性/Profile 解析/交叉校验任一问题（detail 指明字段）
     * @throws EvidenceError（DeclarationInvalid）依赖声明闭包拒绝
     * @throws EvidenceError（EvaluatorDescriptorInvalid）factory 为空
     *
     * 线程约束：注册期单线程约定（装配期）——排他锁保证并发调用的
     * 数据安全，但"运行期不增删"语义不受锁保护（§9.4）。
     */
    void registerEvaluator(std::unique_ptr<IEvaluatorFactory> factory,
                           const std::vector<std::string>& snapshotFactKeys);

    /**
     * @brief 按评估键查找已注册工厂（§9.4 未知键行——查询非抛）。
     *
     * @param evaluationKey [in] 评估键（词形非法视同未注册——查表必不命中）
     * @return 已注册工厂的只读指针（注册表持有、调用方不接管；在注册
     *         表存活期有效）；未知键返回 nullptr（调用方据此给"评估器
     *         不可用"诊断——EV-REG-1）
     *
     * 线程安全：并发只读安全（共享锁）。
     */
    const IEvaluatorFactory* find(std::string_view evaluationKey) const;

    /**
     * @brief 按评估键创建评估器实例（§10.2 时序"create() → 注册表"的
     *        便捷面＝find＋factory.create()）。
     *
     * @param evaluationKey [in] 评估键
     * @return 新实例（所有权归调用方）；未知键返回 nullptr（查询轨——
     *         与 find 同语义，实现口径 R-1 登记单元卡 v1.1：不抛——
     *         未知键是装配不一致的环境面〔execution 据摘要比对拦截〕
     *         而非进程内编程错误，诊断装配在调用方）
     *
     * 线程安全：并发安全（§9.4"create() 线程安全"——查表在共享锁内，
     * 工厂调用在锁外：工厂运行期存活由"运行期不增删"保证，锁外创建
     * 避免用户代码在锁内执行引发长尾阻塞）。
     */
    std::unique_ptr<IEngineeringEvaluator> create(std::string_view evaluationKey) const;

    /**
     * @brief 注册清单及其摘要（§9.4 主/worker 一致性——execution 派发/
     *        握手时比对两侧摘要，不一致即拒绝派发，CON-06/AT-19）。
     *
     * @return 清单（entries 按 key 字典序升序——map 迭代序即字典序；
     *         digest 对清单规范编码计算，同注册集恒同值）
     *
     * 线程安全：并发只读安全（共享锁）。
     */
    RegistrationManifest manifest() const;

    /**
     * @brief 评估键是否已注册（IProducerRegistryView 适配——EV-T05
     *        I-6 预登记的衔接面；validateProof 的产生者查询经本视图）。
     *
     * @param evaluationKey [in] 待查评估键
     * @return 已注册 true；未知键/词形非法 false
     *
     * 线程安全：并发只读安全（共享锁）。
     */
    bool isRegistered(std::string_view evaluationKey) const override;

    /**
     * @brief 已注册评估键的契约版本是否与给定版本相符（IProducerRegistry
     *        View 适配——validateProof 的"producer 契约版本相符"查询面；
     *        未注册键返回值无意义——调用序恒为先 isRegistered 后本查询，
     *        本实现按 false 返回不另行报错）。
     *
     * @param evaluationKey  [in] 已注册评估键
     * @param contractVersion [in] 待比对的契约版本
     * @return 注册值与给定值相等 true
     *
     * 线程安全：并发只读安全（共享锁）。
     */
    bool contractVersionMatches(std::string_view evaluationKey,
                                std::uint32_t contractVersion) const override;

private:
    /// 注册表条目（登记期从 descriptor 提取的运行期只读投影＋工厂本体）。
    struct Registered {
        std::unique_ptr<IEvaluatorFactory> factory; ///< 工厂（注册表独占；运行期存活）
        std::uint32_t contractVersion = 0;          ///< 契约版本（descriptor 申报值）
        core::ContentIdentity profileIdentity;      ///< Profile 权威内容身份（注册表计算面）
    };

    const EvidenceProfileRegistry& m_profileRegistry; ///< Profile 解析面（构造绑定，长期存活）
    mutable std::shared_mutex m_mutex;                ///< 读写锁（注册排他/查询共享——§9.4）
    /// 键 → 条目（map 序＝key 字典序——manifest 排序来源；std::less<>
    /// 透明比较器——string_view 查询免拷贝）。
    std::map<std::string, Registered, std::less<>> m_evaluators;
};

}  // namespace sdurws::ird::evidence

#endif  // SDURWS_IRD_EVIDENCE_EVALUATOR_HPP
