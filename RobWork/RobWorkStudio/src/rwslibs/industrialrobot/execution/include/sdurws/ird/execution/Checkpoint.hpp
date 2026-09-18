/**
 * @file   Checkpoint.hpp
 * @brief  检查点协调器（CheckpointCoordinator）——检查点的写出编排、
 *         完整性校验与恢复调度（§8.1 检查点契约＋§10.6 接口的公共契约面）。
 *
 * 设计依据：
 *   - units/execution.md §8.1（CheckpointRecord 字段表、职责与规则：
 *     写出位置/损坏检查点/不兼容检查点/恢复后新 AttemptId/"检查点可恢复
 *     ≠任务已成功"/清理失败不破坏已提交检查点）、§10.6（ICheckpointCoordinator
 *     接口原文）、§3.1（组成行：Checkpoint.hpp 承载本头全部类型）
 *   - 需求 CON-04（缓存/检查点按契约判断；部分/失败结果不作正式命中——
 *     无 manifest＝不作为命中）、OPT-06（检查点平台机制）、TASK-01
 *     （暂停/检查点粒度能力）
 *   - 任务契约 tasks/foundation/EX-T08.json acceptance 1~2（EX-CKP-1~3、
 *     P-EX-9 处置：旧版本检查点不迁移——Incompatible 拒绝，宁重算不错续）
 *
 * 背景说明（为什么 execution 只做"存储治理"，判定归 evidence）：
 *   检查点是"运行进度"而非工程结论——它的可恢复性五条件（身份对齐＋格式
 *   可读＋完整性校验）与结果复用的效力面完全不同（evidence Compatibility.hpp
 *   文件头"四类复用严格区分"）。本单元的职责边界（§2.2 CON-04 行"不可越
 *   界"列）：兼容判定规则归 evidence judgeCheckpointCompatibility（纯函数
 *   单点复用，本单元零复制判定逻辑）；磁盘编址与 manifest 发布归 project
 *   （写入口唯一归属，§10.8"无任何直接磁盘写"）；execution 只做生成
 *   （批次摘要计算）、验证（装载/恢复期摘要校验）与恢复调度（判定调用＋
 *   拒绝转译＋新 Attempt 派发规格产出）。
 *
 * 磁盘职责声明（EX-CKP-1/3 的"原文件保留"为什么是结构保证）：
 *   本协调器对磁盘的全部动作＝①经 project::IResultArchivePort 写入
 *   （begin→writeBatch→finalize）；②恢复期以只读方式装载批次文件核对
 *   摘要。不存在任何删除/覆盖/改写磁盘的代码路径——损坏/不兼容/不可续
 *   三类拒绝全部是"零磁盘副作用"的内存态结论（废弃标记登记在会话内存
 *   索引，不落盘；磁盘删除归 project 域，阶段 A 不做 GC，§10.6 discard 注）。
 *
 * P-EX-9 处置口径（acceptance 2）：旧版本检查点**不迁移**——格式版本
 *   落在支持区间外（evidence kCheckpointFormatVersionMin/Max 闭区间）或
 *   契约版本/切片身份任一失配一律 Incompatible 拒绝恢复。保守方向
 *   "宁可重算不可错续"：迁移工具如有需要走需求/设计变更，不在本单元
 *   私加（§8.1 不兼容检查点规则原文）。
 *
 * 线程约束：写检查点/恢复调度按 §10.8 属调度线程串行域（归档端口调用
 *   遵守 P-PR-4 单侧冻结）；listCompatible 为并发只读面（内部互斥保护
 *   的不可变快照查询）。实现内部以互斥量保护会话索引，保证只读查询
 *   不被状态写阻塞（§10.8"查询永不被状态写阻塞"）。
 */

#ifndef SDURWS_IRD_EXECUTION_CHECKPOINT_HPP
#define SDURWS_IRD_EXECUTION_CHECKPOINT_HPP

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>      // DiagnosticRecord（诊断承载）
#include <sdurws/ird/core/Digest.hpp>        // Digest256（批次/复合摘要）
#include <sdurws/ird/core/Identity.hpp>      // TaskIdentity/RunId（§8.1 身份字段）
#include <sdurws/ird/evidence/Compatibility.hpp>  // CheckpointSummary/judge 判定面（零复制消费）
#include <sdurws/ird/evidence/Slice.hpp>     // isValidEvaluationKey（C-2 词形闸门单点）
#include <sdurws/ird/execution/Errors.hpp>   // ExecutionError/ExecutionErrorCode
#include <sdurws/ird/execution/Ports.hpp>    // IExecutionDiagnosticsSink（诊断注入）
#include <sdurws/ird/execution/TaskTypes.hpp>  // CheckpointId（§4.1 已落位——EX-T02
                                            // 头注"EX-T08 include 本头不重定义"）
#include <sdurws/ird/project/ArchivePort.hpp>  // IResultArchivePort（检查点写入口——登记边）

namespace sdurws::ird::execution {

// =====================================================================
// CheckpointId——检查点主键（复用 TaskTypes.hpp §4.1 已落位定义）
// =====================================================================

// CheckpointId{run, sequence} 已随 EX-T02 落位于 TaskTypes.hpp（§4.1 概念
// 表末行；isValid/toCanonical/三序齐全，头注明文"EX-T08 落地时 include
// 本头，不重定义"）——本头零重定义，直接消费。

// =====================================================================
// CheckpointBatchFile / CheckpointFileRef——批次文件与内容寻址引用
// =====================================================================

/**
 * @brief 检查点批次文件（worker 通道 CheckpointBatch 回传片段的执行侧
 *        承载——§8.1 写出流程"CheckpointBatch{seq, 中间状态片段}"）。
 *
 * 载荷字节对 execution 不透明（§8.1 intermediateStateRef 行"对 execution
 * 不透明"）——本单元只计算摘要、透传给归档端口，不解析域中间状态。
 *
 * 值语义（可移动——批次字节按移动传递，避免大载荷拷贝）。
 */
struct CheckpointBatchFile {
    /// 运行目录内相对路径（'/' 分隔；保留名/穿越防护由归档端口校验——
    /// 本单元不重复实现白名单，直接消费端口裁决）。
    std::string relPath;
    /// 片段完整字节（域中间状态——不透明载荷）。
    std::vector<std::uint8_t> bytes;
};

/**
 * @brief 批次文件的内容寻址引用（§8.1 intermediateStateRef 行"批次文件
 *        清单＋摘要"的元素形态）——不可变：写出时计算，此后只读。
 */
struct CheckpointFileRef {
    /// 相对路径（与批次文件的 relPath 一致）。
    std::string relPath;
    /// 该文件字节的 SHA-256 摘要（core::ContentDigester 单点计算——CON-05
    /// 内容寻址；恢复期重算比对即 EX-CKP-1 的损坏判据）。
    core::Digest256 sha256{};
    /// 文件字节数（单位：字节；与 manifest sizeBytes 一致——核对面）。
    std::uint64_t sizeBytes = 0;
};

// =====================================================================
// CheckpointRecord——检查点记录（§8.1 字段表逐行）
// =====================================================================

/// 检查点信封格式版本（§8.1 checkpointFormatVersion 行：当前 1；失配→
/// 不兼容——支持区间权威归 evidence kCheckpointFormatVersionMin/Max 闭
/// 区间，本常量只是**写出**时打戳的当前值，不作判定依据）。
inline constexpr std::uint32_t kCheckpointRecordVersion = 1;

/// 登记表 schema 版本同源的记录 schema 版本（§8.1 recordVersion 行：当前 1）。
inline constexpr std::uint32_t kCheckpointSchemaVersion = 1;

/**
 * @brief 检查点记录（§8.1 字段表全字段——"清单/身份数据"，不含域载荷）。
 *
 * 生命周期：writeCheckpoint 组装并随会话索引持有（不可变——§8.1 各字段
 *   "不可变"列；批次只增语义由归档端口的只增发布承担）。恢复成功后记录
 *   作为"已完成统计基点"交给新 Attempt 派发（§8.1 恢复流程"统计累计，
 *   不重复"）。
 *
 * 值语义；线程安全：纯值（构造后只读）。
 */
struct CheckpointRecord {
    /// 主键（§8.1 checkpointId 行：seq≥1、run 为登记内运行）。
    CheckpointId checkpointId{};
    /// 产出该检查点的运行与尝试（五元组——§8.1 task 行；有效性同 core）。
    core::TaskIdentity task{};
    /// 输入绑定两身份（§8.1 snapshotId/sliceId 行——非零；与登记扩展字段
    /// 一致；sliceId 已含评估器契约版本与环境版本要素——恢复判定的身份面）。
    core::ContentIdentity snapshotId;
    core::ContentIdentity sliceId;
    /// 评估器键与契约版本（§8.1 evaluatorKey 行——键词形归
    /// isValidEvaluationKey 闸门；契约版本为恢复兼容判定第三条件）。
    std::string evaluatorKey;
    std::uint32_t evaluatorContractVersion = 0;
    /// 策略与名称映射绑定（§8.1 policyIdentity/nameMapIdentity 行——CON-06）。
    core::ContentIdentity policyIdentity;
    core::ContentIdentity nameMapIdentity;
    /// 检查点载荷格式版本（§8.1：≥1；失配→不兼容——P-EX-9 不迁移）。
    std::uint32_t checkpointFormatVersion = kCheckpointRecordVersion;
    /// 随机种子（§8.1 randomSeed 行：0＝无随机性；NFR-COR-02 复现要素）。
    std::uint64_t randomSeed = 0;
    /// 并行配置（§8.1 threadCount 行：≥1；复现要素）。
    std::uint32_t threadCount = 1;
    /// 已完成样本/分段（§8.1：completed≤total；续跑统计基点——继续不重复统计）。
    std::uint64_t completedUnits = 0;
    std::uint64_t totalUnits = 0;
    /// 中间状态载荷引用（§8.1 intermediateStateRef 行：批次文件清单＋摘要；
    /// 复合摘要按 relPath 字典序计算——见 computePayloadDigest 注）。
    std::vector<CheckpointFileRef> intermediateStateRef;
    /// 复合摘要（§8.1 payloadDigest 行：全部批次文件 SHA-256 复合；不符→
    /// Corrupt——EX-CKP-1 的判据字段）。
    core::Digest256 payloadDigest{};
    /// 创建时刻（UTC——§8.1 createdAtUtc 行；观测面不参与判定）。
    std::chrono::system_clock::time_point createdAtUtc{};
    /// 可恢复能力（§8.1 resumable 行：域声明中间状态完整性；false 的检查点
    /// 仅供诊断——恢复拒绝，EX-CKP-3 的构造面）。
    bool resumable = true;
    /// 本记录 schema 版本（§8.1 recordVersion 行：主版本失配→不兼容——
    /// 判定面同格式版本，经 evidence 判定条件消费）。
    std::uint32_t recordVersion = kCheckpointSchemaVersion;

    /**
     * @brief 结构有效性（§8.1 各行"合法/非法"列的机械化——写出前置校验）。
     *
     * 校验项：主键有效、五元组有效、四身份非零、评估键词形合法
     * （isValidEvaluationKey）、格式版本≥1、threadCount≥1、completed≤total。
     * **载荷面（intermediateStateRef/payloadDigest）不在本校验范围**——
     * 它是写出编排从实际批次派生的产物而非调用方申报事实（请求内预填值
     * 一律被覆盖）；补全后的完整记录在 writeCheckpoint 内另行强校验
     * （引用非空＋复合摘要非零）。recordVersion/checkpointFormatVersion
     * 的"当前值"语义不在此校验（历史版本记录是合法装载对象——判定拒绝
     * 归 evidence 判定式，不在结构校验私加）。
     */
    bool isValid() const noexcept;
};

// =====================================================================
// 复合摘要（payloadDigest 的唯一计算点——写出与恢复校验共用）
// =====================================================================

/**
 * @brief 计算批次文件集合的复合 SHA-256（§8.1 payloadDigest 行"全部批次
 *        文件 SHA-256 复合"的唯一实现——写/读两侧同一函数，杜绝两侧各写
 *        一套编码的漂移）。
 *
 * 编码（确定性，NFR-COR-02）：文件按 relPath 字典序升序排列，逐文件将
 * "relPath 字节 ‖ 0x00 分隔符 ‖ 该文件 SHA-256（32 字节原始摘要）"追加进
 * 同一个 core::ContentDigester，取终值。分隔符防止相邻字段拼接歧义；以
 * **每文件摘要**（而非原始字节）进复合，使恢复期可先逐文件定位损坏
 * （哪一文件不符）再做复合结论——观测点更精确且复杂度仍为 O(总字节)。
 *
 * @param files [in] 批次文件集合（可为空——空集合得"空复合摘要"，是
 *                合法确定性值；但 CheckpointRecord::isValid 要求批次非空）
 * @return 32 字节复合摘要
 *
 * 线程安全：可重入纯函数。
 */
core::Digest256 computePayloadDigest(const std::vector<CheckpointBatchFile>& files);

/**
 * @brief 对一份已装载的批次文件集合作逐文件完整性核对（恢复期判据面）。
 *
 * @param files   [in] 磁盘装载的批次文件（顺序无关——按 relPath 对齐）
 * @param record  [in] 检查点记录（逐条目比对 relPath→摘要/字节数，末尾
 *                比对复合摘要）
 * @return 空串＝全部相符；非空＝首个不符文件的 relPath（损坏定位——
 *         EX-CKP-1 诊断 cause 携带）
 *
 * 线程安全：可重入纯函数。
 */
std::string verifyCheckpointFiles(const std::vector<CheckpointBatchFile>& files,
                                  const CheckpointRecord& record);

// =====================================================================
// 写出（§10.6 writeCheckpoint 两结构）
// =====================================================================

/**
 * @brief 检查点写出请求（§10.6 writeCheckpoint 入参的承载）。
 *
 * 值语义（可移动——批次字节按移动消费）；线程约束：仅调度线程构造传递。
 */
struct CheckpointWriteRequest {
    /// 检查点记录描述（身份/绑定/复现要素/统计——载荷外全部字段；批次
    /// 摘要由协调器计算后补全 intermediateStateRef/payloadDigest 两字段）。
    /// 记录内预填的 intermediateStateRef/payloadDigest 被忽略（以批次实际
    /// 内容为准——防"声明与载荷不一致"的伪造面）。
    CheckpointRecord record{};
    /// 批次文件集合（≥1——空批次没有检查点意义；调用方契约，违约 fail-fast）。
    std::vector<CheckpointBatchFile> files;
    /// 归档位置 checkpoints/<run-id>/<seq>/ 的完整路径（登记事实——A8
    /// "归档位置不重新推导"同款：编排方从登记记录/存储上下文取得，本单元
    /// 原样交给归档端口校验白名单，不自行拼接存储根）。
    std::filesystem::path archiveDir;
};

/**
 * @brief 检查点写出结果（§10.6 CheckpointWriteResult 的承载）。
 *
 * 错误语义（AGENTS §3 二分）：请求结构非法（isValid 为假/批次空/
 * archiveDir 空）＝调用方契约违约→抛 ExecutionError fail-fast；归档端口
 * 的环境类失败（disk-full/冲突/上下文关闭）＝可预期失败→结构化
 * ArchiveFailed＋诊断，不抛。
 */
struct CheckpointWriteResult {
    /// 写出结论（§10.6 后置"正式检查点（或批次残留＋诊断）"的两态）。
    enum class Outcome {
        Published,    ///< manifest 已原子发布＝正式检查点（幂等可续）
        ArchiveFailed, ///< 归档端口环境类失败——批次残留为"未完成"（不破坏
                       ///< 已提交批次；§8.1"清理失败不破坏已提交检查点"）
    };

    Outcome outcome = Outcome::ArchiveFailed;
    /// 已发布检查点的主键（Published 时与请求一致；ArchiveFailed 仍回带
    /// ——诊断关联面）。
    CheckpointId checkpointId{};
    /// 失败明细诊断（ArchiveFailed 时非空； Published 时为空——成功不发码）。
    std::vector<core::DiagnosticRecord> diagnostics;
};

// =====================================================================
// 恢复（§10.6 restore/listCompatible 三结构）
// =====================================================================

/**
 * @brief 恢复候选查询（§10.6 ResumeQuery 的承载）。
 *
 * 判定面字段与 evidence::CacheHitQuery 的 requestSliceId/requestContract
 * Version 同名同义（C-4：模式与 Profile 不参与检查点判定——本查询因此
 * 不携带二者）。
 *
 * 值语义；线程约束：仅调度线程构造传递。
 */
struct ResumeQuery {
    /// 运行过滤（nullopt＝不限运行——跨运行按任务链找最近候选的编排面）。
    std::optional<core::RunId> run;
    /// 请求切片身份（与检查点 sliceId 的相等是恢复第二条件——evidence 判定式）。
    core::ContentIdentity requestSliceId;
    /// 请求契约版本（恢复第三条件——EV-CPA-2）。
    std::uint32_t requestContractVersion = 0;
};

/**
 * @brief 恢复请求（§10.6 restore 入参的承载）——目标检查点＋请求判定面。
 *
 * 值语义；线程约束：仅调度线程。
 */
struct CheckpointRestoreRequest {
    /// 目标检查点（索引内须存在——未知主键＝调用方契约违约 fail-fast）。
    CheckpointId checkpointId{};
    /// 请求切片身份（判定第二条件的请求侧——evidence 判定式）。
    core::ContentIdentity requestSliceId;
    /// 请求契约版本（判定第三条件的请求侧）。
    std::uint32_t requestContractVersion = 0;
};

/**
 * @brief 恢复结果（§10.6 CheckpointRestoreResult 的承载——拒绝面全谱）。
 *
 * 拒绝不删盘（§8.1"恢复失败……保留原检查点（不删除、不降级）"——本结构
 *   全部非 Restored 结论都是零磁盘副作用的内存态结论）。
 */
struct CheckpointRestoreResult {
    /// 恢复结论（§8.1 恢复流程"Incompatible/Corrupt→拒绝＋诊断（原检查点
    /// 保留）"＋resumable 行"false 的检查点仅供诊断"的结论词表）。
    enum class Outcome {
        Restored,          ///< 判定通过＋完整性核对通过——返回恢复规格
        Corrupt,           ///< 摘要校验失败（EX-CHECKPOINT-CORRUPT＋损坏定位；
                           ///< 该检查点会话内登记废弃——§8.1 损坏检查点规则）
        Incompatible,      ///< 兼容判定拒绝（EX-CHECKPOINT-INCOMPATIBLE＋
                           ///< evidence reasons——P-EX-9 不迁移）
        NotResumable,      ///< 域申报 resumable=false（仅供诊断——拒绝恢复）
        Discarded,         ///< 目标已被显式废弃/先前损坏废弃（discard 或 Corrupt）
        UnknownCheckpoint, ///< 目标不在会话索引（跨会话恢复须先经恢复扫描
                           ///< 重建——project §7.4/PM-15，非本单元职责）
    };

    Outcome outcome = Outcome::UnknownCheckpoint;
    /// 恢复规格（Restored 时有效）：完整记录（含 completedUnits/totalUnits
    /// 统计基点与批次引用——新 Attempt 派发"续跑统计累计，不重复"的输入；
    /// §8.1"检查点可恢复≠任务已成功"：恢复产物仍走完整接纳，不在本面包装）。
    CheckpointRecord record{};
    /// 不兼容原因 token 清单（Incompatible 时非空——evidence 判定式的
    /// reasons 原样透传〔词表归 evidence，本单元零复制零改写〕）。
    std::vector<std::string> reasons;
    /// 诊断明细（Corrupt/Incompatible 时含稳定码诊断——EX-CHECKPOINT-\*；
    /// 其余结论为空或开发级说明）。
    std::vector<core::DiagnosticRecord> diagnostics;
};

/**
 * @brief 恢复候选（listCompatible 的元素——主键＋evidence 摘要的配对）。
 *
 * 归置说明（登记单元卡 §15.4）：§10.6 listCompatible 返回类型原文写
 * CheckpointSummary——其字段面与 evidence::CheckpointSummary 五字段完全
 * 重合（判定摘要"从检查点存储条目提取的登记头五字段"，evidence §8.2
 * 原文），为杜绝第二套摘要结构本单元直接复用 evidence 类型，另以配对
 * 结构补上主键关联（调用方需要知道"恢复哪一个"）。
 */
struct CheckpointCandidate {
    /// 候选检查点主键。
    CheckpointId id{};
    /// 判定摘要（evidence 五字段——integrityVerified 反映会话索引的当前
    /// 校验状态：写出时核对了摘要为 true；被损坏废弃后不再出现在候选中）。
    evidence::CheckpointSummary summary;
};

// =====================================================================
// ICheckpointCoordinator（§10.6 接口原文）
// =====================================================================

/**
 * @brief 检查点协调器接口（§10.6 原文四方法——写出/恢复/候选查询/废弃）。
 *
 * 生命周期：实现随调度器装配创建（L5 持有）；接口引用不拥有归档端口与
 * 诊断 sink（构造注入、调用方保证存活期覆盖）。
 *
 * 线程约束：writeCheckpoint/restore/discard 仅调度线程（P-PR-4 域）；
 * listCompatible 并发只读。
 */
class ICheckpointCoordinator {
public:
    virtual ~ICheckpointCoordinator() = default;

    /**
     * @brief 写出检查点（§10.6 原文：批次校验→归档端口 writeBatch
     *        （checkpoints/<run>/<seq>/）→finalize（manifest 发布））。
     *
     * 前置：五元组核对通过（§10.6 原文——请求记录五元组须有效；"登记内
     * 运行"的登记表核对归派发编排，本接口只做结构校验）；后置：正式检查
     * 点（或批次残留＋诊断）；失败不破坏已提交检查点。
     *
     * @param request [in] 写出请求（记录描述＋批次＋归档位置）
     * @return 写出结果（Published/ArchiveFailed＋诊断）
     *
     * @throws ExecutionError(InvalidState) 调用方契约违约：记录结构无效、
     *         批次为空、archiveDir 为空、同 (run,seq) 已有正式检查点
     *         （重发布＝协议错误——临时/在途重写归归档端口只增幂等，正式
     *         后重写不被受理）
     */
    virtual CheckpointWriteResult writeCheckpoint(const CheckpointWriteRequest& request) = 0;

    /**
     * @brief 恢复调度（§10.6 原文：候选查找→evidence 兼容判定→通过则
     *        返回恢复规格（供新 Attempt 派发））。
     *
     * 前置：任务 Paused/终态（续跑）——状态机核对归派发编排；失败→原
     * 检查点保留＋诊断（全部拒绝路径零磁盘副作用——见文件头声明）。
     *
     * @param request [in] 恢复请求（目标主键＋请求判定面）
     * @return 恢复结果（Restored 规格或分类拒绝＋诊断）
     *
     * @throws ExecutionError(InvalidState) 调用方契约违约：checkpointId
     *         结构无效
     */
    virtual CheckpointRestoreResult restore(const CheckpointRestoreRequest& request) = 0;

    /**
     * @brief 兼容候选清单（§10.6 原文 listCompatible——判定通过的候选，
     *        按主键字典序确定性排列〔同查询同清单，NFR-COR-02〕）。
     *
     * 摘要完整性面说明：清单不做磁盘装载核对（那是 restore 的重校验义务
     * ——EX-CKP-1 的磁盘级判据），integrityVerified 反映会话索引内的当前
     * 校验状态。
     */
    virtual std::vector<CheckpointCandidate> listCompatible(const ResumeQuery& query) const = 0;

    /**
     * @brief 显式废弃（§10.6 原文：标记；磁盘删除归 project 域，阶段 A
     *        不做 GC——废弃后不再出现在 listCompatible、restore 拒绝）。
     *
     * @throws ExecutionError(InvalidState) checkpointId 结构无效
     */
    virtual void discard(CheckpointId id) = 0;
};

// =====================================================================
// CheckpointCoordinator——实现本体（§8.1/§10.6）
// =====================================================================

/**
 * @brief 检查点协调器实现（ICheckpointCoordinator 本体）。
 *
 * 依赖注入（构造期固定，非所有权引用——调用方保证存活期覆盖）：
 *   - project::IResultArchivePort：检查点写入口（begin→writeBatch→
 *     finalize——manifest 发布归 project，§8.1 写出位置规则）；
 *   - IExecutionDiagnosticsSink：稳定码诊断出口（EX-CHECKPOINT-\*）。
 *
 * 会话索引：已写出检查点的内存登记（主键→记录＋废弃标记）——恢复调度
 * 的查找/校验基准。跨会话（进程重启后）的恢复不归本面：磁盘残留的重建
 * 归 project 恢复扫描（§7.4/PM-15——"无第二账本"纪律，§5.4）。
 *
 * 确定性来源：候选排序＝主键字典序；复合摘要＝relPath 字典序编码
 * （computePayloadDigest）；诊断 cause 携带损坏文件定位——同输入同输出。
 */
class CheckpointCoordinator final : public ICheckpointCoordinator {
public:
    /// UTC 墙钟注入形态（createdAtUtc；空＝system_clock::now——测试注入
    /// 固定时钟保证留痕确定性，RunRegistry 同款纪律）。
    using UtcClockFn = std::function<std::chrono::system_clock::time_point()>;

    /**
     * @brief 构造（归档端口与诊断 sink 必备——非空引用）。
     *
     * @param archivePort [in] 归档端口（检查点写入口；非所有权）
     * @param diagnostics [in] 诊断 sink（非所有权；稳定码/开发双通道）
     * @param clock       [in] UTC 时钟（空＝system_clock::now）
     */
    CheckpointCoordinator(project::IResultArchivePort& archivePort,
                          IExecutionDiagnosticsSink& diagnostics,
                          UtcClockFn clock = nullptr);

    // ---- ICheckpointCoordinator（§10.6 四方法）----
    CheckpointWriteResult writeCheckpoint(const CheckpointWriteRequest& request) override;
    CheckpointRestoreResult restore(const CheckpointRestoreRequest& request) override;
    std::vector<CheckpointCandidate> listCompatible(const ResumeQuery& query) const override;
    void discard(CheckpointId id) override;

    /// 会话索引内的检查点总数（观测面——测试与诊断用；并发只读）。
    std::size_t indexedCount() const;

private:
    /// 索引条目（记录＋归档位置＋废弃标记——废弃含显式 discard 与损坏
    /// 自动废弃；archiveDir 随记录留存，恢复装载按 A8 取登记原值，
    /// 不由调用方重报——防"装载目录与归档目录漂移"）。
    struct IndexedCheckpoint {
        CheckpointRecord record;
        std::filesystem::path archiveDir;
        bool discarded = false;
    };

    /// 损坏/显式废弃的统一落点（§8.1"只读登记废弃标记"——会话内存标记，
    /// 零磁盘副作用；内部自持互斥）。
    void markDiscarded(const CheckpointId& id);

    project::IResultArchivePort& m_archivePort;   ///< 归档端口（非所有权）
    IExecutionDiagnosticsSink& m_diagnostics;     ///< 诊断 sink（非所有权）
    UtcClockFn m_clock;                           ///< UTC 时钟（空＝now）
    /// 会话索引（主键字典序容器——listCompatible 的确定性排列直接得到）。
    std::map<CheckpointId, IndexedCheckpoint> m_index;
    /// 索引互斥（writeCheckpoint/restore/discard 按约定仅调度线程，但
    /// listCompatible/indexedCount 是并发只读面——§10.8"查询不被写阻塞"
    /// 以互斥保证；临界区只有内存操作，无 I/O 无回调，无死锁面）。
    mutable std::mutex m_indexMutex;
};

}  // namespace sdurws::ird::execution

#endif  // SDURWS_IRD_EXECUTION_CHECKPOINT_HPP
