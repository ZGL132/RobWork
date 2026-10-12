/**
 * @file   ResourceRefs.hpp
 * @brief  外部资源引用与固化（ResourceRefs）——引用保护记录
 *         （ExternalResourceRecord）、固化请求/结果（SolidifyRequest/
 *         Result，§5.7 冻结签名）、消费窄接口（IExternalResourceSource
 *         ——io 注入面）、固化服务契约（SolidifyService）与"未固化阻断
 *         正式结论"判据数据面（PRJ-T17 落位）。
 *
 * 设计依据：
 *   - units/project.md §5.7（外部资源固化服务——SolidifyRequest/
 *     SolidifyResult 冻结签名与四段流程原文："io 安全读取→前后哈希比对
 *     →canonical 字节经对象库随命令提交发布→ExternalResourceRecord.
 *     state=materialized 引用保护"）、§3.1（组成表：ResourceRefs.hpp 行
 *     ——ExternalResourceRecord、SolidifyRequest/Result，详见 §5.7；
 *     本落位按 DTB §5.4 增量登记窄接口与服务契约）、§4.4.5
 *     （ExternalRefRecord 草稿期字段级契约——本头 ExternalResourceRecord
 *     的持久化子集与 state 词表来源）、§4.6（对象库与内容编址——固化
 *     副本入 objects/、不可变保证与引用保护语义）、§6.5（处理器注册
 *     协议——固化/重关联命令族经 HandlerRegistry 装配注册）、§10.4
 *     （与 io 的协作——2026-10-12 O-09 裁决后口径）、§2.1 O-13 行
 *     （外部资源固化的存储侧）、§2.2 CON-03/PM-09/NFR-REL-04 承接行；
 *   - 需求 CON-03（Verified/正式报告引用的外部资源必须有项目内不可变
 *     副本并受引用保护）、PM-09（外部源重关联经显式提交产生新修订）、
 *     PM-01（转正式固化三段边界）、NFR-REL-04（外部源缺失/变化可检测
 *     ——检测实现归 io，本头只定义 project 消费它的注入面）、EVI-01
 *     （未固化阻断正式结论——本头只提供判据数据面，判定归 evidence，
 *     N-9 边界）；
 *   - ARCHITECTURE §6.6（外部资源三段边界：一次性读取/外部引用记录/
 *     转正式固化）、§3.5（io↔project 注入式协作边说明——2026-10-12
 *     O-09 裁决补登，非编译边：任一侧引入对侧头文件或链接目标仍按
 *     表外同层边构建失败）；
 *   - 任务契约 tasks/foundation/PRJ-T17.json acceptance 1～3。
 *
 * 背景说明（为什么消费窄接口定义在 project 而实现在 io——O-09 裁决后
 *   的形态，本头零 io 依赖的机制依据）：ARCH §3.5 依赖表从未登记
 *   project→io 编译边（表外同层边＝构建失败），而 §5.7 固化流程需要
 *   "外部源安全读取（SafePath/预算）＋前后一致性检测"能力。按 O-09
 *   裁决（2026-10-12，P-PR-5/P-IO-1 消账），协作形态＝**注入式**：
 *   project 在自己的公共头定义消费窄接口（本头 IExternalResourceSource
 *   ——按 §5.7 流程所需形状裁剪：外部引用探测/安全读取/前后一致性），
 *   io 侧提供实现（io.md §9.8 IResourceSnapshotter 同语义），由 L5 应用
 *   壳装配期注入。双向零编译期依赖——本头不 include io 单元的任何
 *   公共头（路径前缀即 LinkageContractTest 扫描面，此处不以字面量书写），
 *   sdurws_ird_project 不链接 sdurws_ird_io（契约测试持续钉住）。
 *
 * 背景说明（为什么固化经命令进入事务——CON-03 的存储侧机制）：对象库
 *   的唯一写路径是命令提交（§4.6/§6.1——七步事务发布 objects/<oid>/<cv>
 *   并落修订引用集），不存在"绕过修订直接写对象库"的第二通道（自审项
 *   A-1）。因此固化的存储发布＝提交一条固化命令（处理器产出
 *   ObjectWrite），副本随该命令的修订进入对象库并被修订引用集引用——
 *   引用保护＝对象库不可变保证（§4.6：任何代码路径不以写模式打开已发布
 *   对象）＋修订引用可达（PA-2 不可变历史）。
 *
 * 线程模型：值类型全部可任意拷贝（无共享状态）；IExternalResourceSource
 *   实现的线程约束由实现方声明（io.md §9.8：probe 并发安全）；SolidifyService
 *   实现的线程约束见其方法注释（服务级操作互斥——实现口径）。
 */

#ifndef SDURWS_IRD_PROJECT_RESOURCEREFS_HPP
#define SDURWS_IRD_PROJECT_RESOURCEREFS_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/project/QueryPort.hpp>

namespace sdurws::ird::project {

// ProjectStore 完整类型定义于 ProjectStore.hpp（本头只前向声明——
// createSolidifyService 的引用参数与工厂实现都在消费侧解析，仅包含本头
// 的翻译单元不必为此拖入存储上下文契约）。
class ProjectStore;

// =====================================================================
// §5.7 冻结签名锚定类型
// =====================================================================

/**
 * @brief 内容哈希值类型（§5.7 SolidifyResult.sourceChangedDuringCopy 的
 *        元素类型名锚定）。
 *
 * 落位口径（DTB §5.4 增量登记）：§5.7 原文写作 core::ContentHash256，
 * 而 core v0.1（P-PR-1 消费基线）的 256 位摘要值类型只有 Digest256
 * （Digest.hpp"摘要原始字节"）——不存在名为 ContentHash256 的类型。
 * 按单元卡 §5 章约定"签名为实现建议（自洽契约片段，未编译验证）"，
 * 本别名以 core 唯一摘要值类型兑现冻结签名中的类型名，**不定义第二个
 * 摘要值类型**（CON-01 身份/版本包络唯一；CR-02 摘要算法唯一）。语义：
 * SHA-256 摘要原始字节（32 字节；全零＝保留值，与 core 纪律同源）。
 */
using ContentHash256 = core::Digest256;

// =====================================================================
// 外部资源引用保护记录（§4.4.5 state 词表的阶段 B 冻结落位）
// =====================================================================

/**
 * @brief 外部资源引用记录的生命周期状态（§5.7"ExternalResourceRecord.
 *        state"；io.md §8.5 记录形态 state=recorded|materialized 同源）。
 *
 * 背景说明（两态的状态机与判定责任边界）：recorded＝草稿期登记完成
 * （路径＋内容哈希已落盘——NFR-REL-04 检测数据源就绪，副本未入项目）；
 * materialized＝固化完成（不可变副本已随修订入 objects/ 并被引用集
 * 引用——引用保护生效，io.md §8.4 步骤 7"此后对象受不可变保证，
 * runtime S10 免复查"）。状态迁移只能经固化/重关联命令提交发生
 * （CON-03——状态事实随修订持久化，不随内存翻转）；"未固化阻断正式
 * 结论"的判定归 evidence（N-9），本状态只是判据数据。
 *
 * token 形态（§4.4.5 ExternalRefRecord.state 字段"词表归阶段 B io 契约
 * 冻结"的兑现）：小写 kebab token，磁盘编码用 toToken()/fromToken()
 * 互转（DraftOrigin 先例同款）。
 */
enum class ExternalResourceState {
    /// recorded——草稿期已登记（路径＋哈希），副本未入项目。
    Recorded,
    /// materialized——不可变副本已入对象库并被修订引用（引用保护生效）。
    Materialized,
};

/**
 * @brief 枚举→冻结 token（磁盘/命令载荷编码用；toToken(DraftOrigin)
 *        先例同款）。
 *
 * @param state [in] 生命周期状态枚举
 * @return 静态串（"recorded"/"materialized"）；未知值返回 "unknown"
 *         （防御，不可达——switch 全覆盖）
 */
const char* toToken(ExternalResourceState state) noexcept;

/**
 * @brief token→枚举（严格匹配；解析边界 try 轨，不抛）。
 *
 * @param token [in] 记录载体上的 state 字符串
 * @return 命中返回枚举；未命中返回 nullopt（调用方按载体损坏处置——
 *         草稿侧映射为记录不可信，命令载荷侧按非法输入拒绝）
 */
std::optional<ExternalResourceState> externalResourceStateFromToken(
    std::string_view token) noexcept;

/**
 * @brief 外部资源引用保护记录（§5.7 命名的 ExternalResourceRecord）——
 *        一条外部引用从草稿登记到固化保护的全程事实载体。
 *
 * 背景说明（与 §4.4.5 ExternalRefRecord 的关系——两份形态不重复定义
 *   字段语义）：PersistenceFormat::ExternalRefRecord 是**草稿文件内**
 *   的登记形态（§4.4.5 字段级冻结——载体为 <module>.draft.json，字段
 *   集合冻结且不含固化结果字段）；本结构是**引用保护全程**的统一形态
 *   （§5.7 命名；在草稿字段之上追加固化结果三字段）。草稿形态↔统一
 *   形态的互转见 toDraftExternalRef()/externalResourceFromDraft()——
 *   转换只投影 §4.4.5 冻结字段集合，不发明第二份字段语义。
 *
 * 固化结果字段的持久化位置（实现口径，DTB §5.4）：resourceObject/
 * materializedVersion 不进草稿文件（§4.4.5 字段冻结面不动），随固化/
 * 重关联命令的 payloadCanonical（域所有 canonical 字节——§6.4）持久化
 * 于修订命令留痕，供历史追溯；消费面（evidence 判据）经本结构在内存
 * 组装（工作流从命令历史或自身会话状态回填）。
 *
 * 线程安全：纯值类型。
 */
struct ExternalResourceRecord {
    /// 记录 id（草稿内唯一 token；SolidifyRequest.externalRefId 的关联
    /// 键，§5.7）。非空。
    std::string externalRefId;
    /// 外部资源绝对路径原文（存储侧登记；解析/SafePath 校验归 io——
    /// §5.7 责任划分"缺失/变化/读取失败/预算超限分别由 io 产出诊断"）。
    /// 非空。
    std::string absolutePath;
    /// 登记时内容 SHA-256，64 个小写十六进制字符（NFR-REL-04 检测基准
    /// ——io 比对现路径内容与本值判定 missing/changed，§8.3 io.md）。
    std::string contentHash256;
    /// 登记时内容字节数（单位＝字节；uint64——大文件不截断）。
    std::uint64_t sizeBytes = 0;
    /// 登记时刻，ISO-8601 UTC 文本（§4.4 时间约定）。
    std::string recordedAtUtc;
    /// 生命周期状态（见 ExternalResourceState——recorded/materialized）。
    ExternalResourceState state = ExternalResourceState::Recorded;
    /// 固化副本对象身份（obj- 规范文本；state==Materialized 时必为有效
    /// 身份，recorded 态为全零保留值——"副本未入项目"的表达，不伪造）。
    core::ObjectId resourceObject{};
    /// 固化副本的内容版本（cv- 规范文本；state==Materialized 时必有效，
    /// recorded 态为全零保留值）。与 resourceObject 共同构成引用保护的
    /// 编址键（objects/<oid>/<cv>——§4.6）。
    core::ContentVersion materializedVersion{};

    bool operator==(const ExternalResourceRecord& o) const noexcept
    {
        return externalRefId == o.externalRefId && absolutePath == o.absolutePath
            && contentHash256 == o.contentHash256 && sizeBytes == o.sizeBytes
            && recordedAtUtc == o.recordedAtUtc && state == o.state
            && resourceObject == o.resourceObject
            && materializedVersion == o.materializedVersion;
    }
    bool operator!=(const ExternalResourceRecord& o) const noexcept
    {
        return !(*this == o);
    }
};

/**
 * @brief 统一形态→草稿形态投影（§4.4.5 ExternalRefRecord）。
 *
 * 背景说明（投影只保留草稿冻结字段）：草稿形态没有 resourceObject/
 * materializedVersion 字段（§4.4.5 冻结面），固化结果不进草稿文件——
 * 本投影按 §4.4.5 字段集合逐一拷贝（state 经 toToken 编码），调用方
 * 以 DraftService 保存（草稿写路径的唯一入口，§5.4）。materialized
 * 记录投影后 state 字段仍为 "materialized"（token 词表两态齐备——
 * 草稿文件可承载已固化标记，检测面可据此免探测）。
 *
 * @param record [in] 统一形态记录（字段约束同结构体注释）
 * @return 草稿形态记录（深拷贝——与源记录后续变化隔离）
 *
 * 线程安全：纯函数。
 */
ExternalRefRecord toDraftExternalRef(const ExternalResourceRecord& record);

/**
 * @brief 草稿形态→统一形态装配（§4.4.5 → 本头；固化结果三字段置
 *        recorded 态缺省值——草稿文件不携带固化结果，见结构体注释）。
 *
 * @param draft [in] 草稿形态记录（字段约束见 §4.4.5——调用方自草稿
 *              文档加载；字段合法性校验归加载侧）
 * @return 统一形态记录（state 经 fromToken 严格解析）；state token
 *         未命中＝nullopt（调用方按草稿损坏处置——不猜测不伪造）
 *
 * 线程安全：纯函数。
 */
std::optional<ExternalResourceRecord> externalResourceFromDraft(
    const ExternalRefRecord& draft);

// =====================================================================
// §5.7 固化请求/结果（冻结签名——零偏差锚定）
// =====================================================================

/**
 * @brief 固化请求（§5.7 冻结签名原文：{externalRefId, targetResource-
 *        Object, budgetBytes}——字段名/类型/顺序零偏差）。
 *
 * 背景说明（逐字段语义）：
 *   - externalRefId：草稿外部引用记录 id（ExternalResourceRecord.
 *     externalRefId 的关联键——服务据此校验请求与记录的一致性，失配＝
 *     调用方错误 fail-fast）；
 *   - targetResourceObject：固化副本对象（新分配或替换版本——无效/
 *     全零身份＝请服务新分配（core generate，"project 分配"语义 §4.6）；
 *     有效身份＝替换版本（同 oid 新 cv——对象库内容编址天然支持，
 *     §4.6），重关联场景传既有 oid 即得"同对象换版本"谱系）；
 *   - budgetBytes：读取预算（单位＝字节；io BudgetGuard 管辖区——
 *     §5.7 字段注释"io BudgetGuard 执行"；实现口径：>0 必填，0＝无
 *     意义的"零预算"，服务侧 fail-fast，不代默认值——io.md §8.4 步 1
 *     "budgetBytes 与产品默认取小"的取小输入由调用方供给）。
 *
 * 线程安全：纯值类型。
 */
struct SolidifyRequest {
    /// 草稿外部引用记录 id（非空；与目标记录 externalRefId 一致）。
    std::string externalRefId;
    /// 固化副本对象（obj-；全零＝新分配，有效＝替换版本）。
    core::ObjectId targetResourceObject{};
    /// 读取预算（单位＝字节；必须 >0——io BudgetGuard 执行，§5.7）。
    std::uint64_t budgetBytes = 0;
};

/**
 * @brief 固化结果（§5.7 冻结签名原文：{ok, materializedVersion,
 *        sourceChangedDuringCopy, diagnostics}——字段名/类型/顺序零
 *        偏差）。
 *
 * 背景说明（逐字段语义与"不伪造数值"纪律）：
 *   - ok==true 当且仅当副本已随命令提交入对象库（新修订产生）；
 *     ok==false 时 materializedVersion 无意义（保留全零——不伪造）；
 *   - sourceChangedDuringCopy：复制期间源变化检测（§5.7；NFR-REL-04
 *     的复制中途半区——io.md V17）。nullopt＝复制前后快照一致；有值＝
 *     检出变化，值为复制后快照的内容哈希（**新内容**的哈希——供用户
 *     对照登记值判断外部改写幅度；诊断清单同时携带 Changed 比较型
 *     诊断，io.md §8.3 口径）；
 *   - diagnostics：io 产出的缺失/变化/读取失败/预算超限诊断（§5.7
 *     "project 透传并中止固化"——码值归 io 生产者语义，project 不解释
 *     不改写），以及命令端口对拒绝/失败面的用户级诊断（如只读上下文
 *     的 PRJ-LOCK-HELD）。
 *
 * 线程安全：纯值类型。
 */
struct SolidifyResult {
    /// true＝副本已随命令提交入对象库（materializedVersion 有效）。
    bool ok = false;
    /// 固化副本的内容版本（cv-；ok==false 时为全零保留值）。
    core::ContentVersion materializedVersion{};
    /// 复制期间源变化检测（nullopt＝未检出；有值＝复制后快照哈希）。
    std::optional<ContentHash256> sourceChangedDuringCopy;
    /// 透传诊断（io 产出＋命令端口用户级诊断——见结构体注释）。
    std::vector<core::DiagnosticRecord> diagnostics;
};

// =====================================================================
// 消费窄接口（注入面——io 实现由 L5 装配注入，O-09 裁决形态）
// =====================================================================

/**
 * @brief 外部引用探测结论（io.md §9.8 ExternalRefProbe 同构三值；
 *        §8.3 检测语义——digest 为唯一权威判据）。
 *
 * 背景说明：Missing＝登记路径不可达（NFR-REL-04 缺失半区）；Changed＝
 * 现路径内容哈希 ≠ 登记哈希（size/mtime 不同仅提示不判变——防 touch
 * 误报，io.md §8.3"重算 digest 比对"）；Ok＝未变。探测是 PM-09 重关联
 * 用户流程的触发数据（检测能力归 io；流程与命令归 project/工作流）。
 *
 * 线程安全：纯值词表。
 */
enum class ExternalRefProbe {
    /// 未变（digest 相同——登记哈希仍描述现内容）。
    Ok,
    /// 缺失（登记路径不可达）。
    Missing,
    /// 已变化（现内容哈希 ≠ 登记哈希——digest 权威判据）。
    Changed,
};

/**
 * @brief 外部源快照（io.md §8.2 ResourceSnapshot 的窄面投影——§5.7
 *        流程所需形状：只承载大小与内容摘要，路径/mtime 不出窄接口
 *        ——路径不作身份、不作引用键，io.md §8.5 身份规则）。
 *
 * 线程安全：纯值类型。
 */
struct ExternalSourceSnapshot {
    /// 快照内容字节数（单位＝字节）。
    std::uint64_t sizeBytes = 0;
    /// 快照内容 SHA-256（权威判据；实现经 core ContentDigester——
    /// io 侧摘要算法唯一纪律 SA-12，本面透传字节值）。
    ContentHash256 contentDigest{};
};

/**
 * @brief 外部源安全读取结果（§5.7 流程第 1～2 段的结果形状：io 安全
 *        读取＋复制＋前后一致性检测——io.md §8.4 步骤 2～6 的窄面承载）。
 *
 * 背景说明（成功与失败的字段纪律）：ok==true 当且仅当副本完整且前后
 * 快照一致（sourceChangedDuringCopy==false）——此时 canonicalBytes
 * 才有效（交 project 入对象库的副本字节）；ok==false 时 canonicalBytes
 * 为空（**不产生部分副本**——io.md §8.4 失败责任表"无半成品"），诊断
 * 清单必非空（缺失/变化/预算超限/读取失败至少其一——io 产出，project
 * 透传）。前后快照无论成败均尽力回填（变化检测判据的数据面）。
 *
 * 线程安全：纯值类型。
 */
struct ExternalSourceReadResult {
    /// true＝副本完整且前后快照一致（canonicalBytes 可入对象库）。
    bool ok = false;
    /// 固化副本字节（ok==true 时非空语义由实现保证长度与快照一致；
    /// ok==false 时为空——无部分副本）。
    std::vector<std::uint8_t> canonicalBytes;
    /// 复制前快照 A（§8.4 步骤 2）。
    ExternalSourceSnapshot before;
    /// 复制后快照 B（§8.4 步骤 4——重读源文件）。
    ExternalSourceSnapshot after;
    /// true＝前后快照不一致（digest 或 size 任一不同——§8.4 步骤 5；
    /// NFR-REL-04 复制中途变化检测，io.md V17 语义）。
    bool sourceChangedDuringCopy = false;
    /// 诊断（io 产出：IO-RES-MISSING/IO-RES-CHANGED/IO-SEC-BUDGET-*/
    /// IO-RES-* 四分类——io.md §8.3/§9.8 收编 token；project 透传不
    /// 解释——§5.7"缺失/变化/读取失败/预算超限分别由 io 产出诊断"）。
    std::vector<core::DiagnosticRecord> diagnostics;
};

/**
 * @brief 外部资源源（消费窄接口）——project 定义、io 实现（io.md
 *        §9.8 IResourceSnapshotter 同语义：SafePath/BudgetGuard 防护
 *        与检测实现）、L5 应用壳装配期注入（O-09 裁决形态；ARCH §3.5
 *        "io↔project 注入式协作边"的 project 侧契约面）。
 *
 * 背景说明（接口形状为什么只有两个方法）：§5.7 固化流程对 io 的消费
 *   恰好两点——段②检测（probe：Missing/Changed 判定，PM-09 重关联
 *   触发数据）与段③固化执行（readForSolidify：安全读取＋中转复制＋
 *   前后一致性）。包编解码等其余 io 能力不属固化流程（WP-04-T18
 *   另行落位），不预建进本接口（NFR-MNT-04 不预建无消费者接口）。
 *
 * 所有权与生命周期：实现对象由装配方（L5/测试）持有；注入 SolidifyService
 *   的指针非 owning，其生存期必须覆盖服务对象（服务在其任意方法内
 *   随时调用）。
 *
 * 线程约束：实现方按 io.md §9.8 声明（probe 并发安全；solidify 会话级
 *   ——中转区互斥由实现内部承担，本接口不暴露会话句柄）。
 */
class IExternalResourceSource {
public:
    /// 虚析构：经接口指针删除实现对象是多态所有权的常规路径。
    virtual ~IExternalResourceSource() = default;

    /**
     * @brief 外部引用探测（段②检测——io.md §8.3/§9.8 probe 同语义）。
     *
     * @param record [in] 外部引用记录（登记路径＋哈希——检测基准；
     *               state 不参与判定：recorded/materialized 都可探测，
     *               探测对象是"登记的外部源"而非项目内副本）
     *
     * @return 三值结论（Ok/Missing/Changed——见 ExternalRefProbe）
     *
     * @throws 实现方契约内异常（如 record 字段非法——SafePath 校验
     *         归 io，project 不预校验路径形态：§5.7 责任划分）
     *
     * 线程安全：并发安全（io.md §9.8"probe 并发安全"）。
     */
    [[nodiscard]] virtual ExternalRefProbe probe(
        const ExternalResourceRecord& record) = 0;

    /**
     * @brief 安全读取＋复制＋前后一致性（段③固化执行——io.md §8.4
     *        步骤 1～6：预算开户→快照 A→复制→快照 B→比对→副本摘要
     *        复核；失败清理中转、不产生部分副本）。
     *
     * @param record      [in] 外部引用记录（absolutePath＝P-1 用户源
     *                    路径——SafePath 管辖区，解析归 io）
     * @param budgetBytes [in] 读取预算（单位＝字节；>0——BudgetGuard
     *                    管辖区，超限即失败＋比较型诊断）
     *
     * @return 读取结果（成败字段纪律见 ExternalSourceReadResult 注释；
     *         变化检出＝ok==false＋sourceChangedDuringCopy==true＋
     *         Changed 诊断——固化在 project 侧中止，零修订）
     *
     * @throws 实现方契约内异常（record 字段非法等——透传不吞）
     *
     * 线程约束：会话级互斥由实现内部承担（io.md §9.8"solidify 会话级
     * ——中转区互斥"）；并发调用同一实现的行为由实现方定义。
     */
    [[nodiscard]] virtual ExternalSourceReadResult readForSolidify(
        const ExternalResourceRecord& record, std::uint64_t budgetBytes) = 0;
};

// =====================================================================
// "未固化阻断正式结论"判据数据面（EVI-01 消费——判定归 evidence，N-9）
// =====================================================================

/**
 * @brief 外部资源固化判据数据（"未固化阻断正式结论"（EVI-01 证据不足
 *        口径）在 project 侧的全部可得数据——**数据面，非判定面**）。
 *
 * 背景说明（N-9 边界——为什么只有数据没有结论动词）：需求把"是否允许
 *   正式结论"的判定权交给 evidence（§2.1 N-9"证据对象的业务判断——
 *   project 只保存与提供证据对象"；io.md §8.4 失败责任表同口径"是否
 *   允许正式报告：evidence——io 不判定"）。本结构聚合两个机械事实：
 *   记录生命周期状态与副本引用可达性；gateSatisfied 是两事实的合取
 *   （materialized 且副本被目标修订引用），供 evidence 消费组装其
 *   EVI-01 证据充分性判定——本单元不产生"阻断/放行"结论。
 *
 * 线程安全：纯值类型。
 */
struct ExternalResourceGateData {
    /// 记录生命周期状态（recorded＝未固化；materialized＝已固化）。
    ExternalResourceState state = ExternalResourceState::Recorded;
    /// 副本引用可达性：true＝(resourceObject, materializedVersion) 出现
    /// 于目标修订的引用集（引用保护在该修订上成立——PA-2 意义下该修订
    /// 永远能读到该副本）。
    bool copyReferencedByRevision = false;
    /// 判据合取：state==Materialized 且 copyReferencedByRevision。
    ///（数据事实合取，非业务判定——见结构体注释）
    bool gateSatisfied = false;

    bool operator==(const ExternalResourceGateData& o) const noexcept
    {
        return state == o.state && copyReferencedByRevision == o.copyReferencedByRevision
            && gateSatisfied == o.gateSatisfied;
    }
    bool operator!=(const ExternalResourceGateData& o) const noexcept
    {
        return !(*this == o);
    }
};

/**
 * @brief 计算外部资源对目标修订的固化判据数据（纯函数——记录事实与
 *        修订引用集的机械比对）。
 *
 * @param record   [in] 外部引用记录（state/materialized 字段为判据
 *                 输入；recorded 态的 oid/cv 为全零——不可能命中引用集，
 *                 合取自然为 false）
 * @param revision [in] 目标修订视图（通常＝被评估修订——evidence 的
 *                 输入快照；引用集为全量引用形态，§4.4.2）
 *
 * @return 判据数据（见 ExternalResourceGateData）
 *
 * 线程安全：纯函数（参数均为值快照——与存储后续变化隔离，PA-3）。
 */
[[nodiscard]] ExternalResourceGateData externalResourceGateData(
    const ExternalResourceRecord& record, const RevisionView& revision);

// =====================================================================
// 固化服务契约（§5.7 服务实体——PRJ-T17 落位；实现载体 SolidifyServiceImpl）
// =====================================================================

/// 固化命令注册 token（§6.5 命令族命名——§4.4.4 冻结语法
/// ^[a-z0-9-]{3,64}$ 无点 kebab，O-35 裁决口径；历史浏览 PM-12-S1 展示面）。
inline constexpr const char* kSolidifyCommandType = "solidify-external-resource";

/// 重关联命令注册 token（PM-09 语义标记——AT-21"外部源重关联经显式
/// 提交"；同冻结语法）。
inline constexpr const char* kRelinkCommandType = "relink-external-resource";

/**
 * @brief 外部资源固化服务（§5.7 服务实体）——把 io 安全读取的副本
 *        字节经命令端口发布入对象库的编排者。
 *
 * 背景说明（编排分解——为什么读取在命令外、发布在命令内）：§5.7 流程
 *   原文"canonical 字节经对象库随命令提交发布"与 io.md §8.4 步骤 7
 *   "随下一次命令提交发布"共同表明：读取/比对（io 能力）与发布（project
 *   事务）是两段。服务先调源读取（§5.7 第 1～2 段）——失败/变化即中止
 *   返回（**命令未提交，零修订**——比"提交后拒绝"更早止损且不借用
 *   §6.3 拒绝词表表达环境事实）；成功则以固化/重关联命令提交发布
 *   （CON-03——命令处理器把交接的副本字节装配为 ObjectWrite，随七步
 *   事务入对象库并落修订）。该分解为实现口径登记（DTB §5.4——单元卡
 *   §5.7 流程四段的时序落位）。
 *
 * 获取方式：createSolidifyService() 工厂（实现类型不出公共头——R-2）。
 *
 * 生命周期与所有权：实例由装配方 unique_ptr 持有；其对存储上下文与
 *   源实现均为非 owning 引用/指针——**服务实例的生存期须覆盖存储上下
 *   文**（构造期向 HandlerRegistry 注册的命令处理器持服务回指针，注册
 *   表随存储上下文存活）。重复创建（同上下文二次注册同 token）＝注册
 *   表边界拒绝（std::invalid_argument——§5.3.5）。
 *
 * 线程模型：solidify/relink 服务级互斥（内部互斥——pending 副本交接面
 *   为服务单份状态；命令槽串行性不受影响）；probe 直通源（并发安全由
 *   源实现保证，io.md §9.8）。
 */
class SolidifyService {
public:
    /// 虚析构：工厂返回 unique_ptr 多态销毁的常规保障。
    virtual ~SolidifyService() = default;

    /**
     * @brief 固化外部引用（§5.7 流程编排——recorded→materialized 的
     *        状态迁移载体）。
     *
     * 执行序（各步失败即返回；除"命令拒绝/失败"外**不产生任何修订**）：
     *   1. 前置校验（调用方错误 fail-fast——见 @throws）；
     *   2. 源安全读取（IExternalResourceSource::readForSolidify——预算
     *      ＋SafePath＋复制＋前后一致性）：失败/变化 → ok=false＋透传
     *      诊断返回（零修订——中止固化，§5.7"透传并中止（不伪造数值）"）；
     *   3. 提交固化命令（kSolidifyCommandType，payload v1＝记录字段＋
     *      目标对象＋预算＋副本摘要——域 canonical，§6.4）：处理器把
     *      交接的副本字节装配为 ObjectWrite（objectTypeToken=
     *      "ExternalResource"），随七步事务入对象库；
     *   4. 映射结果：committed → ok=true＋materializedVersion（自新
     *      修订引用集提取目标 oid 的内容版本）；否则 ok=false＋命令
     *      端口用户级诊断透传（如只读上下文 PRJ-LOCK-HELD）。
     *
     * @param request [in] 固化请求（externalRefId 须与 record 一致；
     *                budgetBytes>0；targetResourceObject 全零＝新分配）
     * @param branch  [in] 目标分支（brn- 规范文本；须存在于权威元数据
     *                ——命令端口 S1 校验）
     * @param record  [in] 待固化记录（state 必须为 Recorded——已固化
     *                记录重复固化＝调用方错误；重关联场景构造新记录后
     *                走 relink()）
     *
     * @return 固化结果（成败字段纪律见 SolidifyResult 注释）
     *
     * @throws std::invalid_argument 前置违约（externalRefId 空/失配、
     *         state≠Recorded、budgetBytes==0）与装配违约（源未注入）
     *         ——全部 fail-fast
     *
     * 线程安全：服务级互斥（并发固化串行——内部互斥）。
     */
    [[nodiscard]] virtual SolidifyResult solidify(const SolidifyRequest& request,
                                                  core::BranchId branch,
                                                  const ExternalResourceRecord& record) = 0;

    /**
     * @brief 重关联外部源（PM-09——用户重新选择外部文件后以新记录
     *        重新固化；**显式命令提交，产生新修订**，AT-21）。
     *
     * 与 solidify() 同一编排管线，差异仅在命令 token（kRelinkCommandType
     * ——历史留痕区分"首次固化"与"重关联"两个用户动作，PM-12-S1 浏览
     * 展示面）。重关联的副本对象身份：request.targetResourceObject 传
     * 既有固化 oid＝替换版本（同对象新内容——对象库谱系连续）；全零＝
     * 新分配（首次重关联/此前从未固化）。
     *
     * @param request   [in] 重关联请求（externalRefId 须与 newRecord
     *                  一致；budgetBytes>0）
     * @param branch    [in] 目标分支（brn-）
     * @param newRecord [in] 重关联后的新记录（用户重新选择源文件、io
     *                  重新登记路径＋哈希后的形态；state 必须为
     *                  Recorded——重关联是"以新源重新固化"，不是状态翻转）
     *
     * @return 固化结果（同 solidify()）
     *
     * @throws std::invalid_argument 前置违约/装配违约（同 solidify()）
     *
     * 线程安全：服务级互斥（同 solidify()）。
     */
    [[nodiscard]] virtual SolidifyResult relink(const SolidifyRequest& request,
                                                core::BranchId branch,
                                                const ExternalResourceRecord& newRecord) = 0;

    /**
     * @brief 外部引用探测（段②检测直通——PM-09 重关联触发数据面；
     *        缺失/变化判定归 io，本服务只转发不解释）。
     *
     * @param record [in] 外部引用记录（检测基准——登记路径＋哈希）
     *
     * @return 三值结论（Ok/Missing/Changed——ExternalRefProbe）
     *
     * @throws std::invalid_argument 装配违约（源未注入——构造即注入的
     *         正常装配下不可达）；实现方契约内异常透传
     *
     * 线程安全：并发安全（直通源——io.md §9.8 probe 并发安全）。
     */
    [[nodiscard]] virtual ExternalRefProbe probe(const ExternalResourceRecord& record) = 0;
};

/**
 * @brief 固化服务工厂（L5/测试装配入口）——构造服务实体并把固化/
 *        重关联命令处理器注册进存储上下文的 HandlerRegistry（§6.5
 *        装配协议——注册经公共访问器 ProjectStore::handlerRegistry()）。
 *
 * @param store  [in] 存储上下文（非 owning——命令端口与注册表的提供方；
 *               生存期覆盖返回的服务实例）
 * @param source [in] 外部资源源实现（非 owning——io 实现经 L5 装配
 *               注入；测试注入 fake。生存期覆盖服务实例）
 *
 * @return 服务实例（独占持有）
 *
 * @throws std::invalid_argument token 重复注册（同上下文重复创建服务
 *         ——装配错误尽早暴露，§5.3.5 边界拒绝）
 * @throws std::logic_error 存储上下文实现未开放装配通道（基类默认
 *         实现——ProjectStore::handlerRegistry() 契约）
 *
 * 线程安全：装配期调用（运行期创建属装配纪律违约——§5.3.5"运行期
 *         只读"的同族口径）。
 */
[[nodiscard]] std::unique_ptr<SolidifyService> createSolidifyService(
    ProjectStore& store, IExternalResourceSource* source);

}  // namespace sdurws::ird::project

#endif  // SDURWS_IRD_PROJECT_RESOURCEREFS_HPP
