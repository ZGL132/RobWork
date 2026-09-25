/**
 * @file   CommandHandlers.hpp
 * @brief  需求命令处理器族——IRequirementCommandHandler 基类（§9.5，
 *         project ICommandHandler 实现）＋两命令处理器（§9.1 命令清单：
 *         apply-requirement-set/apply-requirement-import）＋命令载荷模型
 *         与编解码（§9.1 prepare 管线 decode 段）。
 *
 * 设计依据：
 *   - units/requirements.md §9.1（命令处理器族表——commandType 写入对象/
 *     断言/inverse 三列："就绪 R0~R9 现场重估（防基线漂移）＋快照式逆
 *     命令（受影响对象前版字节）"；prepare 管线图 decode→基线重建→就绪
 *     断言（Blocking 拒绝＋逐项定位诊断）→CommandPlan{objectWrites,
 *     inverse,summary}；"本单元无 ConfirmableFinding 产出——
 *     confirmableFindings 恒空"）、§9.5（IRequirementCommandHandler 行
 *     原文签名）、§9.7（五对象引用关系图——根引用表/集合对象路由）、
 *     §9.2（接口契约总则——处理器仅命令线程）、§4.8（I-REQ-8 导入溯源
 *     无路径）、§3.4（线程与确定性总约定）
 *   - units/project.md §5.3（ICommandHandler/HandlerContext/CommandPlan/
 *     PrepareOutcome 契约——prepare 三态）、§6.5（L5 装配注册——防反向
 *     链接）、§6.9（快照式逆命令——撤销＝提交 restore 变体产生新修订，
 *     历史不改写）、§4.4.4（token 冻结语法 ^[a-z0-9-]{3,64}）
 *   - units/modeling.md §9.3（prepare 管线模式——本卡 §9.1"prepare 管线
 *     同 modeling §9.3 模式"的对端锚；CommandHandlers.hpp 同构落位）
 *   - 需求 REQ-06（预览/正式分离——命令应用属正式面，就绪 Blocking 拒
 *     绝）、ARC-01（命令原子产生修订）、PA-2（逆命令不改历史）、
 *     D-MDL-9 同源口径（inverse 载荷＝受影响对象前一 (oid,cv) canonical
 *     字节集）、NFR-MNT-04（就绪判定与就绪校验器同源——O-39 裁决）、
 *     NFR-DEP-04（未知载荷版本拒绝）
 *   - 任务契约 tasks/foundation/WP-14-T05.json acceptance 6（O-35 处置：
 *     无点 token；prepare 管线；confirmableFindings 恒空；导入溯源完整
 *     性断言；就绪现场重估）与 knownPitfalls O-35/O-39
 *
 * 背景说明（prepare 管线为什么长这样——第一读者须知）：project 命令
 * 服务拥有事务/串行/确认编排/修订（NFR-MNT-04），"需求集怎样变更才
 * 合法"归本单元。处理器在 S3 被 project 调用，只做五件事：
 *   ① decode——把不透明载荷字节解释为域内写入意图（框架破损/版本不
 *     受理→RejectedInvalidInput，project 转 invalid-payload 拒绝）；
 *   ② 基线重建——从 baseSnapshot 闭包同源解码五对象（防御性复核：
 *     envelope.expectedRevision 与 baseSnapshot.id 不一致＝调用方契约
 *     违约 fail-fast——S2 已拦截过期基线，此处是防线纵深）；
 *   ③ 差值解码＋写入集表达（子类钩子）——槽形状校验/身份取号回填
 *     （ctx.objectId()——PA-1 取号点）/候选工作集装配；
 *   ④ 就绪现场重估（R0~R9，§9.1 断言列——防基线漂移：载荷描述的变更
 *     相对什么基线有效，以提交时刻的候选整体重估为准，O-39 同源语义）
 *     ——Blocking 拒绝（RejectedHardAssert＋逐项定位诊断）；
 *   ⑤ 计划最终化——requiresDualCompile=false（需求对象不进 WorkCell
 *     描述——编译影响面为零，§9.1 表无双编译列）/confirmableFindings
 *     恒空（SA-15 流不私设——本单元无策略校验类放行场景）/inverse 快照
 *     逆载荷/中文命令摘要。
 * 真正的写入/提交（S6/S7）全部归 project——处理器除 ctx.objectId()
 * 取号外零副作用，一切拒绝路径零修订（ARC-01 原子性）。
 *
 * O-35 裁决执行（契约 knownPitfalls/acceptance 6）：两命令 token 采用
 * 无点形态（apply-requirement-set/apply-requirement-import），服从
 * project.md §4.4.4 冻结语法（DTB §4 2026-09-22 裁决）；P-REQ-5 随裁决
 * 消解，点分迁移不适用。
 *
 * 线程约束（§9.2 总则原文）：处理器仅命令线程调用（project 串行槽）；
 * 跨上下文共享实例时处理器状态视为不可变（实例无状态——两个 final 处
 * 理器仅持基类不变成员）。副作用边界：仅产出 CommandPlan＋diags＋
 * ctx.objectId() 取号。
 */

#ifndef IRD_REQUIREMENTS_COMMANDHANDLERS_HPP
#define IRD_REQUIREMENTS_COMMANDHANDLERS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>     // DiagnosticRecord——逐项定位诊断
#include <sdurws/ird/core/Identity.hpp>     // ObjectId——对象/条目身份
#include <sdurws/ird/requirements/DiagCodes.hpp>  // REQ-READY-* 码常量（产码唯一书写点）
#include <sdurws/ird/requirements/ObjectTypes.hpp>  // 五对象 token（命令对象路由）
#include <sdurws/ird/requirements/Readiness.hpp>  // 就绪现场重估（O-39 同源断言套件）
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // 五对象值模型＋编辑器工作集前置
#include <sdurws/ird/project/CommandService.hpp>  // ICommandHandler/HandlerContext/CommandPlan
#include <sdurws/ird/project/QueryPort.hpp>       // RevisionView（基线闭包视图）

namespace sdurws::ird::requirements {

// =====================================================================
// 两命令 token（§9.1 命令清单——O-35 无点裁决的最终形态；唯一书写点）
// =====================================================================

/// @brief apply-requirement-set——写入 req-set 根＋四个集合对象（全量或
///        增量；§9.1 表行 1）；requiresDualCompile=false（需求对象不进
///        WorkCell 描述——纯评估输入数据，零编译影响面）。
inline constexpr std::string_view kCmdApplyRequirementSet = "apply-requirement-set";

/// @brief apply-requirement-import——同上（导入批次专用；§9.1 表行 2），
///        附加导入溯源完整性断言（I-REQ-8：摘要＋行号必登记）。
inline constexpr std::string_view kCmdApplyRequirementImport = "apply-requirement-import";

/// @brief 命令载荷格式版本（project §6.4 版本三元组的处理器自有版本戳；
///        schema 变更→+1，旧版本 payload 拒绝并给升级指引——NFR-DEP-04）。
inline constexpr std::uint32_t kRequirementCommandPayloadVersion = 1;

// =====================================================================
// 命令载荷模型与编解码（§9.1 decode 段——处理器域内契约，project 不解释）
// =====================================================================

/**
 * @brief 载荷对象槽（§9.1"全量或增量"写入的载体单元——一个待写入对象）。
 *
 * 身份语义（与编辑期临时句柄纪律衔接——Services.hpp 头注/O-36）：
 *   - allocateNew=true：新对象——objectId 须为全零保留值（未分配标记），
 *     prepare 经 ctx.objectId() 取号回填（PA-1：身份分配唯一归 project；
 *     编辑期临时句柄在命令提交时不跨命令使用）；
 *   - allocateNew=false：既有对象替换——objectId 须有效且存在于基线闭包
 *     （写不存在的身份＝无效输入）。
 *
 * objectBytes＝对象 canonical 字节（RequirementCodec.encode 产出，
 * kCurrentRequirementFormatVersion——解码门在校验链内强制 I-REQ 不变量，
 * 非法条目不可能经载荷进入就绪断言域）。
 *
 * 线程安全：纯值类型。
 */
struct RequirementPayloadSlot {
    bool allocateNew = false;               ///< true＝新对象（取号回填）
    core::ObjectId objectId;                ///< 槽身份（语义见结构注）
    std::string objectTypeToken;            ///< 五对象 token 之一（路由与校验）
    std::vector<std::uint8_t> objectBytes;  ///< 对象 canonical 字节（解码门输入）

    bool operator==(const RequirementPayloadSlot& o) const
    {
        return allocateNew == o.allocateNew && objectId == o.objectId
            && objectTypeToken == o.objectTypeToken && objectBytes == o.objectBytes;
    }
    bool operator!=(const RequirementPayloadSlot& o) const { return !(*this == o); }
};

/**
 * @brief 需求命令载荷（处理器域内 canonical 形态——CommandEnvelope.
 *        payloadCanonical 的域解释，D-10：project 透传存储不解释）。
 *
 * 模式语义（project §6.9 撤销/重做——"以 inverse 载荷提交 restore 型
 * 命令（同一 commandType，payload=restore 变体）"）：
 *   - Apply：正向应用——槽语义见 RequirementPayloadSlot 注（本处理器族
 *     无引用移除面：需求"删除"＝集合条目减少后整体重写集合对象字节，
 *     五对象本身不删除——§9.7"删除＝引用/条目移除；字节永久保留"）；
 *   - Restore：快照逆放——全部槽 allocateNew=false 且 objectId 须存在
 *     于基线（恢复历史字节）；槽字节为受影响对象**前一版本的 canonical
 *     字节集**（由 prepare 的 inverse 组装产出，往返一致）。
 *
 * 线程安全：纯值类型。
 */
struct RequirementCommandPayload {
    /// 载荷模式（见结构注）。
    enum class Mode { Apply, Restore };

    Mode mode = Mode::Apply;                 ///< 模式（Apply|Restore）
    std::vector<RequirementPayloadSlot> objects;  ///< 对象槽（槽序＝确定性处理序）

    bool operator==(const RequirementCommandPayload& o) const
    {
        return mode == o.mode && objects == o.objects;
    }
    bool operator!=(const RequirementCommandPayload& o) const { return !(*this == o); }
};

/**
 * @brief 载荷确定性编码（§4.8 序列化纪律同源：字段定序、小端长度前缀、
 *        UTF-8、无填充；magic "IRDRCM1"——requirements command payload，
 *        尾数随 kRequirementCommandPayloadVersion 演进）。
 *
 * @param payload [in] 载荷（只读）
 * @return canonical 字节（同载荷重复编码逐字节相等——NFR-COR-02；
 *         tryDecode(encode(p))==p 往返）
 *
 * 纯函数；线程安全；确定性。
 */
std::vector<std::uint8_t> encodeRequirementCommandPayload(
    const RequirementCommandPayload& payload);

/**
 * @brief 载荷解码（try 轨——严格校验：magic/版本/长度前缀/截断/越界/
 *        token 词表外/身份文本不成形/尾随字节，任一失败＝nullopt 不猜
 *        测；版本≠kRequirementCommandPayloadVersion 同样拒绝——
 *        NFR-DEP-04）。
 *
 * @param bytes [in] encodeRequirementCommandPayload 产出（或任意来源字节）
 * @return 解码结果；破损/版本不受理＝nullopt（prepare 转
 *         RejectedInvalidInput——§9.1 decode 失败分支）
 *
 * 纯函数；线程安全；确定性（不产出诊断——载荷破损的机器判别面是
 * PrepareOutcome::RejectedInvalidInput，§9.6 无对应登记码，不私定）。
 */
std::optional<RequirementCommandPayload> tryDecodeRequirementCommandPayload(
    const std::vector<std::uint8_t>& bytes);

// =====================================================================
// IRequirementCommandHandler——基类（§9.5）与两命令处理器
// =====================================================================

/**
 * @brief 子类钩子的产出（差值解码与写入集表达的结果）。
 *
 * 契约：outcome==Planned 时 candidate/affectedOids 有效——candidate＝基
 * 线＋编辑差值（新对象身份已经 ctx.objectId() 回填——PA-1 取号点在钩子
 * 内）；写入集已由钩子表达进 CommandPlan.objectWrites；affectedOids＝
 * 受影响对象身份全集（inverse 快照素材）。outcome==RejectedInvalidInput
 * 时其余字段无意义（域结构校验失败——槽形状/token/身份存在性/导入溯
 * 源完整性；基类清空拒绝态计划）。
 *
 * 线程安全：纯值类型。
 */
struct RequirementDecodeOutcome {
    /// 钩子结果（Planned｜RejectedInvalidInput——钩子不产出 RejectedHard
    /// Assert：硬断言（就绪 Blocking）判定统一归基类断言段——单一判定面，
    /// NFR-MNT-04）。
    project::PrepareOutcome outcome = project::PrepareOutcome::Planned;
    /// 候选工作集（Planned 时有效——就绪重估输入）。
    RequirementWorkingSet candidate;
    /// 受影响对象身份（inverse 素材——槽序确定性）。
    std::vector<core::ObjectId> affectedOids;
};

/**
 * @brief 需求命令处理器基类（§9.5 行原文契约）：封装 §9.1 prepare 管线
 *        公共段（载荷解码/基线重建与防御性复核/子类差值解码/就绪 R0~R9
 *        现场重估/计划最终化），子类只声明 commandType（无点 token——
 *        O-35）与导入断言差异（decodeAndPlan 钩子）。
 *
 * 生命周期：L5 装配期构造并注册进 HandlerRegistry（一次性——
 * registerRequirementCommandHandlers），运行期只读。
 *
 * prepare 执行序（S3 阶段；全部同步、命令线程——§9.1 管线图逐步落位）：
 *   ① 载荷框架解码：tryDecodeRequirementCommandPayload 失败或信封版本
 *     不受理→RejectedInvalidInput（invalid-payload；§9.1 decode 失败分支）；
 *   ② 基线重建＋防御性复核（rebuildRequirementBaseline——闭包五对象
 *     token 路由同源解码；expectedRevision≠baseSnapshot.id＝调用方契约
 *     违约 fail-fast）；
 *   ③ 子类钩子 decodeAndPlan：差值解码＋命令形状校验＋身份取号回填＋
 *     写入集表达（RejectedInvalidInput 透传；apply-requirement-import
 *     的导入溯源完整性断言在钩子内执行——I-REQ-8）；
 *   ④ 就绪现场重估（R0~R9——ReadinessChecker.check 与编辑器预检/评估
 *     组装同一套件，NFR-MNT-04/O-39；候选闭包后像＝基线引用＋写入增量
 *     ——R1/R8 浅校验能看到本次新挂载的集合对象）：Blocking 非空→
 *     RejectedHardAssert＋逐项定位诊断（含 REQ-READY-INPUT-INCOMPLETE
 *     汇总诊断一条——invalid-count/invalid-items 键面）；仅 Warning→
 *     随 diags 留痕、计划照常产出（可应用级——§8.1 级别语义）；
 *   ⑤ 计划最终化：requiresDualCompile=false／confirmableFindings 恒空
 *     （§9.1 原文——SA-15 流不私设）／inverse（受影响对象前一 (oid,cv)
 *     canonical 字节集；首次应用无前版＝不可逆声明 nullopt——project
 *     §6.9 空历史语义）／中文摘要→Planned。
 *
 * @错误 prepare 不抛业务异常（拒绝走值面）；防御性复核失败（基线不一致/
 * 基线字节损坏）＝调用方契约违约或数据侧异常，fail-fast 透传（不吞不改
 * ——AGENTS 错误纪律）。
 *
 * 线程约束：仅命令执行线程（project 串行槽）；写权限：经①端口
 * （CommandPlan 声明面），无其他写路径。
 */
class IRequirementCommandHandler : public project::ICommandHandler {
public:
    /// 处理器注册 token（§4.4.4 冻结语法——构造定值 final 落位；无点
    /// 形态＝O-35 裁决）。
    [[nodiscard]] std::string commandType() const final;

    /// 载荷版本演进点（currentPayloadVersion 的 final 落位；受理集合＝
    /// {kRequirementCommandPayloadVersion}——PRJ-T10 受理口径）。
    [[nodiscard]] std::uint32_t currentPayloadVersion() const final;

    // ---- prepare（§5.3.2 签名——执行序见类注；final：公共段不随子类变化）----
    project::PrepareOutcome prepare(project::HandlerContext& ctx,
                                    const project::CommandEnvelope& envelope,
                                    const project::RevisionView& baseSnapshot,
                                    project::CommandPlan& out,
                                    std::vector<core::DiagnosticRecord>& diags) final;

protected:
    /**
     * @brief 子类钩子：差值解码与写入集表达（断言/确认/inverse/summary
     *        由基类统一执行；两命令的槽形状语义见处理器类注）。
     *
     * @param ctx      [in] 执行上下文（objectId() 取号——新对象身份分配）
     * @param payload  [in] 已框架解码的载荷（模式＋槽集）
     * @param baseline [in] 基线工作集（基线重建产出——闭包视图同源）
     * @param out      [out] 计划（钩子只填 objectWrites——其余字段基类负责）
     * @return 钩子产出（RequirementDecodeOutcome——见结构注）
     */
    virtual RequirementDecodeOutcome decodeAndPlan(
        project::HandlerContext& ctx,
        const RequirementCommandPayload& payload,
        const RequirementWorkingSet& baseline,
        project::CommandPlan& out) = 0;

    /// 构造（本处理器注册 token 由子类定值——O-35 词表常量）。
    explicit IRequirementCommandHandler(std::string commandType) noexcept;

private:
    std::string m_commandType;  ///< 本处理器注册 token（构造定值——O-35 词表）
};

/**
 * @brief apply-requirement-set 处理器（§9.1 表行 1；写入 req-set＋四个
 *        集合对象，全量或增量；就绪 R0~R9 现场重估；快照式逆命令）。
 *
 * 槽形状语义（Apply 模式——基类通用校验见 prepare 类注）：
 *   - 恰含一个 req-set 根槽（恒含——根是修订闭包锚，§9.1"写入对象"列
 *     首）＋0..4 个集合槽，每对象 token 至多一槽（"每需求集至多一份"
 *     ——§4.1）；
 *   - req-set 根槽：基线无根→allocateNew（取号；基类校验闭包无根——
 *     恰一根）；基线有根→显式 oid（基类校验存在且 token 一致）。根对
 *     象字节**恒由候选根重编码写入**（挂载增量随根持久化——见 Command
 *     Handlers.cpp"实现决策登记"）；
 *   - 集合槽（req-point-set 等）：allocateNew→基线根引用表该槽须为未
 *     挂载（首挂载，取号后候选根引用槽置新 oid——根重编码写入）；
 *     显式 oid→须为基线根引用表该槽已挂载对象（字节替换，引用稳定）。
 * Restore 模式：全部槽显式 oid 且存在于基线——快照逆放（§6.9 同源；
 * 写入＝前版字节原样直写）。
 */
class ApplyRequirementSetHandler final : public IRequirementCommandHandler {
public:
    ApplyRequirementSetHandler() noexcept
        : IRequirementCommandHandler(std::string(kCmdApplyRequirementSet))
    {}

protected:
    RequirementDecodeOutcome decodeAndPlan(project::HandlerContext& ctx,
                                           const RequirementCommandPayload& payload,
                                           const RequirementWorkingSet& baseline,
                                           project::CommandPlan& out) override;
};

/**
 * @brief apply-requirement-import 处理器（§9.1 表行 2——导入批次专用；
 *        槽形状语义与 apply-requirement-set 相同）＋附加导入溯源完整性
 *        断言：写入的任务点/区域条目（§4.3/§4.4——溯源字段的载荷面）
 *        每条须携带完整导入溯源（importProvenance 在场＋sourceDigest 非
 *        全零＋recordNumber≥1——I-REQ-8"摘要＋行号"字面；工况/计划条目
 *        无溯源字段，§4.5/§5.2，不适用）。违约＝无效载荷
 *        （RejectedInvalidInput——域结构校验值面，modeling 解码门同款
 *        纪律：机器判别面是拒绝态，不私定诊断码）。
 */
class ApplyRequirementImportHandler final : public IRequirementCommandHandler {
public:
    ApplyRequirementImportHandler() noexcept
        : IRequirementCommandHandler(std::string(kCmdApplyRequirementImport))
    {}

protected:
    RequirementDecodeOutcome decodeAndPlan(project::HandlerContext& ctx,
                                           const RequirementCommandPayload& payload,
                                           const RequirementWorkingSet& baseline,
                                           project::CommandPlan& out) override;
};

/**
 * @brief 两命令处理器族装配注册（project §6.5——L5 应用壳装配期一次性；
 *        注册表所有权接收 unique_ptr，重复 token 注册边界拒绝）。
 *
 * @param registry [in,out] 目标注册表（调用方持有——本函数不接管）
 * @throws std::invalid_argument 注册表拒绝（token 重复——同族二次装配即
 *         装配错误，fail-fast）
 */
void registerRequirementCommandHandlers(project::HandlerRegistry& registry);

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_COMMANDHANDLERS_HPP
