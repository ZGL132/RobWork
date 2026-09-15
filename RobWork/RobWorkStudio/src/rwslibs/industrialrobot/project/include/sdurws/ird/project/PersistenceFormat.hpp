/**
 * @file   PersistenceFormat.hpp
 * @brief  磁盘格式读取类型（PersistenceFormat）——`.rwdesign` 存储自有格式的
 *         字段级数据契约（升级器/工具消费的稳定契约面）。
 *
 * 设计依据：
 *   - units/project.md §4.2（project.json 静态标识字段表）、§4.4（数据类型与
 *     格式契约——"以下类型在 PersistenceFormat.hpp 冻结"）、§4.8（canonical
 *     编码契约：ASCII、固定字段序、无浮点、parse(dump(x))==x；schemaVersion
 *     主版本破坏性/次版本兼容）、§8.11（格式升级：旧格式 format-legacy、
 *     未来版本 schema-future＋升级指引数据——PM-06/NFR-DEP-04）、§3.1
 *     （组成表：本头为"磁盘格式读取类型"公共契约头）；
 *   - 需求 CON-01（持久化对象统一身份/版本包络）、NFR-DEP-04（新格式自带
 *     schema 版本；拒绝重构前格式；已发布新格式必须提供前向升级器——本头
 *     承接其"版本判定与拒绝数据"数据侧，升级器本体归阶段 B §8.11）、
 *     PM-01/NFR-SEC-01/NFR-REL-04（externalRefs 记录：路径＋内容哈希＝io
 *     外部源缺失/变化检测的数据源）；
 *   - 任务契约 tasks/foundation/PRJ-T04.json acceptance 1～3。
 *
 * 背景说明（本头与 Codec 的分工）：
 *   本头只定义"磁盘上有什么字段、什么类型、什么约束"（§4.4 字段级契约）；
 *   canonical 编码/解码与严格解析归 src/Codec.{hpp,cpp}（§4.8——私有实现，
 *   不出 include/，R-2 纪律）。调用关系：事务引擎/打开协议/草稿服务/升级器
 *   （PRJ-T05+）持本头类型，经 Codec 完成 字节↔结构 转换。
 *
 * 类型与磁盘条目的对应（§4.1/§4.4）：
 *   ProjectStaticIdentity   ↔ project.json（项目静态标识，创建期一次写入）
 *   HeadRecord              ↔ HEAD（提交指针，唯一提交点）
 *   RevisionManifest        ↔ revisions/<rev-id>/manifest.json（修订清单）
 *   ProjectMetadataRecord   ↔ 元数据对象负载（objects/<oid>/<cv> 内的
 *                             ProjectMetadata——对象文件本体是域负载原样字节，
 *                             本结构是其 project 自有形态的读取类型）
 *   CommandRecord           ↔ revisions/<rev-id>/command.json（命令摘要/载荷/
 *                             逆命令/确认留痕）
 *   DraftDocument           ↔ drafts/<branch-id>/<module>.draft.json（草稿）
 *
 * 线程安全：全部为纯值类型（可任意拷贝，无共享状态）。
 * 确定性：编入类型的字段序即 canonical 序（§4.8 固定字段序），Codec 按此序
 * 输出——同值必同字节（NFR-COR-02），manifestDigest 等摘要字段的被摘要对象
 * 因此可复现。
 */

#ifndef SDURWS_IRD_PROJECT_PERSISTENCEFORMAT_HPP
#define SDURWS_IRD_PROJECT_PERSISTENCEFORMAT_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>    // ContentVersion（对象内容版本，cv- 规范文本）
#include <sdurws/ird/core/Identity.hpp>  // ProjectId/BranchId/RevisionId/ObjectId

namespace sdurws::ird::project {

// ---- core 消费类型可见性（P-PR-1 消费基线：core.md v0.1 §4.1/§4.2）----
// 显式 using 声明（不用 using namespace——不引入整个 core 命名空间，避免
// 名称空间污染）：本头的字段类型全部来自 core 公共契约（Identity.hpp 的
// 六 Id128 强类型＋Digest.hpp 的 ContentVersion）——§4.4 约定"身份字段
// 一律 core 规范文本"，存储层不复定义第二套身份类型（CON-01 身份/版本
// 包络唯一）。
using core::BranchId;
using core::ContentVersion;
using core::ObjectId;
using core::ProjectId;
using core::RevisionId;

// =====================================================================
// 格式常量（§4.2/§4.8/§8.11）
// =====================================================================

/// 格式标识 token（project.json/HEAD 的 formatId 字段固定值；§8.11 行 1：
/// 魔数/标识不符即旧格式 format-legacy，稳定只读拒绝、原文件不动）。
inline constexpr const char* kFormatId = "rwdesign";

/// schemaVersion 编码比例（实现口径，DTB §5.4 登记）：磁盘 schemaVersion 为
/// 单一 int，按 主版本*10000＋次版本 编码（1.0 → 10000、1.1 → 10001、
/// 2.0 → 20000）。选 10000 而非 1000/10：为次版本留出充足单调空间，主版本
/// 进位清晰（§4.8"主版本变更＝破坏性"判定只看 /10000 的商）。
inline constexpr int kSchemaVersionScale = 10000;

/// 当前实现支持的主版本（阶段 A 首版＝1；升级器随阶段 B 落地后按步距推进）。
inline constexpr int kSchemaVersionMajorCurrent = 1;

/// 当前实现的次版本（追加可选字段时递增，§4.8 稳定性语义）。
inline constexpr int kSchemaVersionMinorCurrent = 0;

/// 当前实现支持的 schemaVersion 编码值（major*10000＋minor＝10000）。
/// 打开协议（PRJ-T08）用它写入新建项目；Codec 解析时用它做 §8.11 判定。
inline constexpr int kSchemaVersionCurrent =
    kSchemaVersionMajorCurrent * kSchemaVersionScale + kSchemaVersionMinorCurrent;

// =====================================================================
// 草稿来源枚举（§4.4.5 DraftDocument.origin，token 冻结三值）
// =====================================================================

/**
 * @brief 草稿落盘来源（§4.4.5 origin 字段；token 冻结：
 *        autosave/manual/apply-retained）。
 *
 * 背景说明：autosave＝ui 定时器自动保存（PM-04，60 s 周期；定时器归 ui）；
 * manual＝用户手动保存；apply-retained＝应用被拒后保留的草稿（§8.3——
 * 应用失败的草稿不能丢弃，保留现场供修复后重试）。恢复横幅（PM-15）按
 * origin 决定提示语。
 */
enum class DraftOrigin {
    Autosave,       ///< autosave——定时自动保存
    Manual,         ///< manual——用户手动保存
    ApplyRetained,  ///< apply-retained——应用被拒后保留（§8.3）
};

/**
 * @brief 枚举→冻结 token（磁盘编码用；Provenance.hpp toToken 同款先例）。
 *
 * @param origin [in] 草稿来源枚举
 * @return 静态串（"autosave"/"manual"/"apply-retained"）；未知值返回
 *         "unknown"（防御，不可达——switch 全覆盖）
 */
const char* toToken(DraftOrigin origin) noexcept;

/**
 * @brief token→枚举（严格匹配；解析边界 try 轨，不抛）。
 *
 * @param token [in] 磁盘上的 origin 字符串
 * @return 命中返回枚举；未命中返回 nullopt（Codec 层据此拒绝为数据损坏）
 */
std::optional<DraftOrigin> draftOriginFromToken(std::string_view token) noexcept;

// =====================================================================
// §4.2 ProjectStaticIdentity——project.json（项目静态标识）
// =====================================================================

/**
 * @brief project.json 静态标识（§4.2 左列字段表）。
 *
 * 背景说明（与 ProjectMetadataRecord 的分工——§4.2 表的核心结论）：
 *   本结构承载"创建后不变的项目本体事实"（谁=projectId、什么格式=
 *   schemaVersion、何时创建=createdAtUtc、用什么工具版本创建），创建期一次
 *   写入、**不参与事务**（ARCH §6.1 A2：无并发修改者，仅另存为/升级时整体
 *   重写）；随演进的状态（分支表/显示名）在 ProjectMetadataRecord，二者
 *   消费者不同（本结构归 openStore/另存为/升级器）。
 *
 * 线程安全：纯值类型。
 */
struct ProjectStaticIdentity {
    /// 格式标识；固定 "rwdesign"（kFormatId；不符＝旧格式 format-legacy，
    /// §8.11 行 1——不提供读取）。
    std::string formatId = kFormatId;
    /// schema 版本（编码见 kSchemaVersionCurrent 注释）；创建时写当前版本。
    int schemaVersion = kSchemaVersionCurrent;
    /// 项目身份（prj-<32hex>，core 规范文本）；projectId 永不改名换 ID（§4.2 表）。
    ProjectId projectId{};
    /// 创建时刻，ISO-8601 UTC 文本（§4.4 约定：时间一律 ISO-8601 UTC）。
    std::string createdAtUtc;
    /// 创建时工具版本（诊断/兼容排查用；透传字符串，无格式约束）。
    std::string createdWithToolVersion;

    bool operator==(const ProjectStaticIdentity& o) const noexcept
    {
        return formatId == o.formatId && schemaVersion == o.schemaVersion
            && projectId == o.projectId && createdAtUtc == o.createdAtUtc
            && createdWithToolVersion == o.createdWithToolVersion;
    }
    bool operator!=(const ProjectStaticIdentity& o) const noexcept { return !(*this == o); }
};

// =====================================================================
// §4.4.1 HeadRecord——HEAD 文件内容（提交指针，唯一提交点）
// =====================================================================

/**
 * @brief HEAD 文件内容（§4.4.1 字段表）——全项目最新一次提交的修订引用。
 *
 * 背景说明：HEAD 是唯一提交点（事务第 5 步原子切换的对象，§7.1）；其中
 * branchId/revisionSeq 是 D-06 决策超出 ARCH 最低要求的服务性字段（重开
 * 会话的默认分支＝HEAD.branchId、修订单调序号的断点恢复），不构成第二权威
 * （权威状态永远在 HEAD 引用的修订→元数据链上）。manifestDigest 用于切换
 * 后自校验（读回 manifest 字节重算比对——摘要计算唯一路径见 Codec 的
 * contentVersionOf 注释，CR-02），它不充当修订身份。
 *
 * 线程安全：纯值类型。
 */
struct HeadRecord {
    /// 格式标识；固定 "rwdesign"（与 project.json 一致；不符＝format-legacy）。
    std::string formatId = kFormatId;
    /// schema 版本；与 project.json 一致（不一致＝CorruptStoreDetected——
    /// 跨文件一致性检查归打开协议 PRJ-T08，本层只做单文件判定）。
    int schemaVersion = kSchemaVersionCurrent;
    /// 项目身份（prj- 规范文本）；与 project.json 一致（同上，跨文件检查）。
    ProjectId projectId{};
    /// 最新提交修订（rev- 规范文本）。
    RevisionId revisionId{};
    /// 修订单调序号（项目内递增；无单位——纯计数）。不进 RevisionId（core
    /// D-04：序号归 project 修订记录，core 只提供不透明身份）。
    std::uint64_t revisionSeq = 0;
    /// 该修订所属分支（brn- 规范文本）＝重开会话的默认分支（D-06）。
    /// 注意：HEAD 记录的是"最后一次提交所在分支"，不是"当前活动分支"
    /// （切换分支是纯会话选择，零写入——§4.5）。
    BranchId branchId{};
    /// 指向修订 manifest 的字节摘要，64 个小写十六进制字符（SHA-256 hex，
    /// 无 tag 前缀——与对象文件名 <cv> 同规则；§4.4.1"切换后自校验"）。
    /// Codec 对该字段只透传不重算（CR-02：编码器不私设第二哈希路径）。
    std::string manifestDigest;

    bool operator==(const HeadRecord& o) const noexcept
    {
        return formatId == o.formatId && schemaVersion == o.schemaVersion
            && projectId == o.projectId && revisionId == o.revisionId
            && revisionSeq == o.revisionSeq && branchId == o.branchId
            && manifestDigest == o.manifestDigest;
    }
    bool operator!=(const HeadRecord& o) const noexcept { return !(*this == o); }
};

// =====================================================================
// §4.4.2 RevisionManifest——revisions/<rev-id>/manifest.json（修订清单）
// =====================================================================

/**
 * @brief {objectId, contentVersion} 引用对——修订清单 metadataRef 与元数据
 *        supersedes 共用的最小引用形态（§4.4.2/§4.4.3 表内嵌对象）。
 *
 * 线程安全：纯值类型。
 */
struct ObjectRefPair {
    /// 被引用对象（obj- 规范文本）。
    ObjectId objectId{};
    /// 对象内容版本（cv-<64hex> core 规范文本）。注意与对象文件名的区别：
    /// 文件名 `<cv>` 为无 tag 的 64 hex（§4.1 命名规则），磁盘记录字段一律
    /// 带 core 规范 tag（§4.4 约定"身份字段一律 core 规范文本"）。
    ContentVersion contentVersion{};

    bool operator==(const ObjectRefPair& o) const noexcept
    {
        return objectId == o.objectId && contentVersion == o.contentVersion;
    }
    bool operator!=(const ObjectRefPair& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 修订对象引用（§4.4.2 objectRefs[] 元素）——该修订完整状态闭包中的
 *        一个对象（ARCH §6.3"对象集引用"）。
 *
 * 背景说明：objectRefs 是全量引用集（≥1，含元数据对象本身），不是增量——
 * 任一修订都自足地描述其完整可见状态，这是"修订视图不可变"（§4.6）的
 * 数据基础。objectTypeToken 随引用登记（§4.6），对象文件内不自述类型
 * （D-10：避免同一字节因存放形态不同产生第二身份）。
 *
 * 线程安全：纯值类型。
 */
struct ObjectRef {
    /// 对象身份（obj- 规范文本；跨内容修改稳定）。
    ObjectId objectId{};
    /// 对象内容版本（cv- 规范文本；内容字节摘要——内容寻址编址键）。
    ContentVersion contentVersion{};
    /// 对象类型 token（域登记的稳定 token，如 "RobotDesign"；透传不解释——
    /// 词表归各域任务卡，project 只存储与回放）。非空。
    std::string objectTypeToken;
    /// 对象负载字节摘要，64 个小写十六进制字符（§4.3 引用图 digest256 列；
    /// 读校验用，不充当对象身份——§4.3 四概念区分）。透传不重算（CR-02）。
    std::string digest256;

    bool operator==(const ObjectRef& o) const noexcept
    {
        return objectId == o.objectId && contentVersion == o.contentVersion
            && objectTypeToken == o.objectTypeToken && digest256 == o.digest256;
    }
    bool operator!=(const ObjectRef& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 修订清单（§4.4.2 字段表）——一次命令提交的持久化封面。
 *
 * 背景说明：revisionSeq 的分配归命令服务（O-15）；parentRevisionId 是
 * **分支 tip 维度**的父修订（§4.5——分支 A 上的提交以 A.tip 为父，与
 * "哪条分支活跃"无关）；元数据经 metadataRef 指向（权威分支表永远取
 * HEAD 引用的元数据版本——INV-M3，禁止经父修订元数据重建）。本结构无
 * schemaVersion 字段（§4.4.2 表无此字段）：manifest 随修订演进，其版本
 * 语义由容器（打开协议读 project.json/HEAD）判定——Codec 按当前支持版本
 * 的结构严格解析。INV-M1/M2（branchId 唯一、supersedes 链无环）属发布期
 * 校验（PRJ-T06/T07 载体），本头不做跨字段/跨文件校验。
 *
 * 线程安全：纯值类型。
 */
struct RevisionManifest {
    /// 修订身份（rev- 规范文本）；与所在目录名一致（§4.4.2 表约束——
    /// 目录名一致性检查归读取方，本层只做单文件解析）。
    RevisionId revisionId{};
    /// 修订单调序号（项目内单调；无单位——纯计数）。
    std::uint64_t revisionSeq = 0;
    /// 分支 tip 维度的父修订（§4.5）；首修订无（nullopt）。
    std::optional<RevisionId> parentRevisionId;
    /// 提交时活动分支（brn- 规范文本）。
    BranchId branchId{};
    /// 提交时间，ISO-8601 UTC 文本。
    std::string committedAtUtc;
    /// 本修订引用的 ProjectMetadata（对象引用对）。
    ObjectRefPair metadataRef;
    /// 完整对象引用集（≥1，必含 metadataRef 指向的元数据对象——§4.4.2 表）。
    /// 顺序：写入序即 canonical 序（Codec 按数组原序输出；"固定字段序"约束
    /// 结构字段，数组元素序是数据本身）。
    std::vector<ObjectRef> objectRefs;
    /// 本次新就位对象的内容版本列表（§4.4.2"残留识别/浏览辅助"；可选——
    /// canonical 省略规则：空列表＝缺省字段，dump 时不输出）。元素为
    /// cv- 规范文本。
    std::vector<ContentVersion> introducedObjects;

    bool operator==(const RevisionManifest& o) const noexcept
    {
        return revisionId == o.revisionId && revisionSeq == o.revisionSeq
            && parentRevisionId == o.parentRevisionId && branchId == o.branchId
            && committedAtUtc == o.committedAtUtc && metadataRef == o.metadataRef
            && objectRefs == o.objectRefs && introducedObjects == o.introducedObjects;
    }
    bool operator!=(const RevisionManifest& o) const noexcept { return !(*this == o); }
};

// =====================================================================
// §4.4.3 ProjectMetadataRecord——元数据对象负载（project 自有形态读取类型）
// =====================================================================

/**
 * @brief 分支表条目（§4.4.3 branches[] 元素）。
 *
 * 背景说明：label 创建时一次写入、不可改名（P-PR-8 登记的待裁决口径——
 * 维持"创建时 label"；PM-11 若需可编辑方案名走需求变更）；baseRevision
 * 创建时记录后不变，tipRevisionId 随每次该分支提交经新元数据版本更新
 * （§4.5 三种"修订指针"表）。
 *
 * 线程安全：纯值类型。
 */
struct BranchRecord {
    /// 分支身份（brn- 规范文本）；branches[] 内唯一（INV-M1，发布期校验）。
    BranchId branchId{};
    /// 分支显示名（创建时一次写入，P-PR-8；透传字符串）。
    std::string label;
    /// 分支创建时的起点修订（rev- 规范文本；创建后不变）。
    RevisionId baseRevisionId{};
    /// 该分支当前 tip 修订（rev- 规范文本；每次提交经新元数据更新）。
    RevisionId tipRevisionId{};
    /// 分支创建时间，ISO-8601 UTC 文本。
    std::string createdAtUtc;

    bool operator==(const BranchRecord& o) const noexcept
    {
        return branchId == o.branchId && label == o.label
            && baseRevisionId == o.baseRevisionId && tipRevisionId == o.tipRevisionId
            && createdAtUtc == o.createdAtUtc;
    }
    bool operator!=(const BranchRecord& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 元数据对象负载（§4.4.3 字段表）——随修订引用、随 HEAD 原子切换
 *        提交的"权威演进状态"（分支表/方案清单/项目显示名）。
 *
 * 背景说明：本对象以对象文件形态存于 objects/（canonical 负载字节），同时
 * 是 project 自有格式——它是五类型中唯一"既是对象负载又是自有格式"的
 * 类型（§4.2 表右列）。supersedes 指向上一权威元数据（谱系链，首版无），
 * 发布时校验谱系无环（INV-M2——发布期校验，归 PRJ-T06/T07）。schemaVersion
 * 是元数据负载自身版本，随整体 schemaVersion 联动（§4.4.3 表）。
 *
 * 线程安全：纯值类型。
 */
struct ProjectMetadataRecord {
    /// 元数据负载自身版本（随整体 schemaVersion 联动；§8.11 判定同源）。
    int schemaVersion = kSchemaVersionCurrent;
    /// 提交本元数据版本的修订 id（rev- 规范文本；闭包规则第二通道的
    /// 入口——§4.5 visit_meta 经 committedBy 回连修订）。
    RevisionId committedBy{};
    /// 上一权威元数据引用（谱系链；首版无）。**发布时校验谱系无环**
    /// （INV-M2），本层不校验（单文件解析无谱系上下文）。
    std::optional<ObjectRefPair> supersedes;
    /// 项目显示名（PM-11 标题栏；UTF-8——磁盘编码时经 \uXXXX 转义保持
    /// 纯 ASCII，§4.8）。改名经领域命令产生新版本＋新修订（R2 PM-11-S2）。
    std::string projectDisplayName;
    /// 主分支（初始分支，brn- 规范文本）。
    BranchId primaryBranchId{};
    /// 分支表（≥1；INV-M1 branchId 唯一＋tip 指向已存在修订——发布期校验）。
    std::vector<BranchRecord> branches;
    /// 方案清单附加标签（§4.4.3"阶段 B 随 workflow 需要启用，字段先冻结"；
    /// 可选——canonical 省略规则：空 map＝缺省字段）。键序＝字典序输出
    /// （std::map 有序，天然满足 canonical 确定性——NFR-COR-02）。
    std::map<std::string, std::string> schemeLabels;

    bool operator==(const ProjectMetadataRecord& o) const noexcept
    {
        return schemaVersion == o.schemaVersion && committedBy == o.committedBy
            && supersedes == o.supersedes && projectDisplayName == o.projectDisplayName
            && primaryBranchId == o.primaryBranchId && branches == o.branches
            && schemeLabels == o.schemeLabels;
    }
    bool operator!=(const ProjectMetadataRecord& o) const noexcept { return !(*this == o); }
};

// =====================================================================
// §4.4.4 CommandRecord——revisions/<rev-id>/command.json（命令留痕）
// =====================================================================

/**
 * @brief 逆命令表达（§4.4.4 inverse 字段；§6.9 撤销语义）。
 *
 * 背景说明：撤销＝逆命令提交（D-11——产生新修订，不改写历史）；不可逆
 * 命令无 inverse 字段（如项目创建）。形态与正向命令同构（type＋版本＋
 * 载荷），载荷同样是域所有 canonical 字节（D-10 透传）。
 *
 * 线程安全：纯值类型。
 */
struct InverseCommand {
    /// 逆命令处理器注册 token（同 commandType 语法 ^[a-z0-9-]{3,64}）。
    std::string commandType;
    /// 逆命令负载的处理器自有版本（无单位——格式版本号；§6.4 版本三元组）。
    std::uint32_t payloadFormatVersion = 0;
    /// 逆命令 canonical 负载（域所有；透传不解释）。
    std::string payloadCanonical;

    bool operator==(const InverseCommand& o) const noexcept
    {
        return commandType == o.commandType
            && payloadFormatVersion == o.payloadFormatVersion
            && payloadCanonical == o.payloadCanonical;
    }
    bool operator!=(const InverseCommand& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 确认凭据（§4.4.4 confirmations[].credential；§6.7 绑定）。
 *
 * 背景说明：对应 core::ConfirmationCredential（DiagData.hpp——principal＋
 * time_point）的**磁盘形态**：confirmedAtUtc 按 §4.4 约定存 ISO-8601 UTC
 * 文本（time_point↔文本的互转归确认流编排，PRJ-T11 载体——本头是磁盘
 * 契约，不引 core 时间类型）。
 *
 * 线程安全：纯值类型。
 */
struct ConfirmationCredentialRecord {
    /// 确认主体（用户/角色标识；采集归 ui/project，透传）。
    std::string principal;
    /// UTC 确认时刻，ISO-8601 UTC 文本。
    std::string confirmedAtUtc;

    bool operator==(const ConfirmationCredentialRecord& o) const noexcept
    {
        return principal == o.principal && confirmedAtUtc == o.confirmedAtUtc;
    }
    bool operator!=(const ConfirmationCredentialRecord& o) const noexcept
    {
        return !(*this == o);
    }
};

/**
 * @brief 确认留痕记录（§4.4.4 confirmations[] 元素；§6.7 确认绑定四元组
 *        ＋凭据）。
 *
 * 背景说明（绑定失效判定，§6.7/PRJ-TX-2）：四元组 {findingDigest,
 * policyContentId, commandDigest, baseRevisionId} 任一与复核时重算值不符
 * ＝确认凭据失效（编译前复核归 PRJ-T10/T11——本头只持久化留痕）。
 * 字段校验深度（透传不解释原则，CR-02/D-10 同精神）：findingDigest/
 * commandDigest 为摘要文本（64 小写 hex），policyContentId 为内容身份
 * （cid- core 规范文本，CON-06），baseRevisionId 为修订身份（rev-）——
 * 身份/摘要字段按 §4.4 约定"身份字段一律 core 规范文本"做格式校验；
 * 其语义复核（比对重算）不归存储层。
 *
 * 线程安全：纯值类型。
 */
struct ConfirmationRecord {
    /// finding 内容摘要，64 个小写十六进制字符（§6.7"finding 内容摘要"）。
    std::string findingDigest;
    /// 产生该 finding 的已解析策略内容身份（cid- 规范文本；CON-06）。
    std::string policyContentId;
    /// 本次命令载荷摘要，64 个小写十六进制字符。
    std::string commandDigest;
    /// 确认所针对的输入版本（rev- 规范文本；PM-04 草稿基线同概念）。
    std::string baseRevisionId;
    /// 确认凭据（主体＋时刻）。
    ConfirmationCredentialRecord credential;

    bool operator==(const ConfirmationRecord& o) const noexcept
    {
        return findingDigest == o.findingDigest && policyContentId == o.policyContentId
            && commandDigest == o.commandDigest && baseRevisionId == o.baseRevisionId
            && credential == o.credential;
    }
    bool operator!=(const ConfirmationRecord& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 命令留痕（§4.4.4 字段表）——一次命令提交的摘要/载荷/逆命令/确认
 *        留痕（随修订永久留痕，PM-12-S1 历史浏览）。
 *
 * 背景说明：payloadCanonical 是处理器域内 canonical 字节，project **不解释**
 * （透传存储——§6.4；版本三元组＝commandType＋payloadFormatVersion＋负载
 * 字节，处理器按三元组声明受理版本集合）。本结构无 schemaVersion 字段
 * （§4.4.4 表无此字段）：命令随修订演进，版本语义由容器判定（同
 * RevisionManifest 的处理，见其注释）。
 *
 * 线程安全：纯值类型。
 */
struct CommandRecord {
    /// 处理器注册 token，语法 ^[a-z0-9-]{3,64}（§4.4.4 表冻结；如
    /// "project.create-branch"——§6.5 project 内置元数据命令族）。
    std::string commandType;
    /// 处理器自有负载版本（无单位——格式版本号；§6.4：处理器演进用）。
    std::uint32_t payloadFormatVersion = 0;
    /// canonical 编码负载（域所有；透传不解释——D-10）。
    std::string payloadCanonical;
    /// 逆命令表达（不可逆命令无——nullopt；§6.9 撤销语义）。
    std::optional<InverseCommand> inverse;
    /// 确认留痕（可选——canonical 省略规则：空列表＝缺省字段）。
    std::vector<ConfirmationRecord> confirmations;
    /// 人读命令摘要（处理器生成，project 原样持久化；PM-12-S1 历史浏览
    /// 展示；UTF-8——磁盘经 \uXXXX 转义，§4.8）。
    std::string summary;

    bool operator==(const CommandRecord& o) const noexcept
    {
        return commandType == o.commandType
            && payloadFormatVersion == o.payloadFormatVersion
            && payloadCanonical == o.payloadCanonical && inverse == o.inverse
            && confirmations == o.confirmations && summary == o.summary;
    }
    bool operator!=(const CommandRecord& o) const noexcept { return !(*this == o); }
};

// =====================================================================
// §4.4.5 DraftDocument——drafts/<branch-id>/<module>.draft.json（草稿）
// =====================================================================

/**
 * @brief 草稿期外部引用记录（§4.4.5 externalRefs[] 元素）。
 *
 * 背景说明（PM-01 存储侧数据契约——为什么记录"路径＋内容哈希"）：
 *   NFR-SEC-01 要求外部资源不以裸路径充当项目资源引用——裸路径指向的
 *   文件可被外部静默修改，正式结论（证据链）将引用到不可复现的内容。
 *   本记录把"引用了哪个外部文件（absolutePath）＋引用时它是什么内容
 *   （contentHash256/sizeBytes）＋何时登记（recordedAtUtc）＋当前生命周期
 *   状态（state）"落盘，成为 NFR-REL-04（外部源缺失/变化可检测）的检测
 *   数据源：io 侧比对现路径内容哈希与本记录即可判定 missing/changed
 *   （检测实现归 io——§5.7 责任划分；本结构是数据契约）。
 *
 * 字段校验深度：absolutePath 为登记路径原文（透传；Windows 安全读取/
 * SafePath 校验归 io——§5.7 责任划分，存储层不重复实现）；state 为生命
 * 周期 token（透传；取值词表随阶段 B io 契约冻结，已知值如 materialized
 * ——§5.7 固化完成后状态）。contentHash256 为 64 小写 hex（SHA-256 hex，
 * 与对象文件名 <cv> 同规则）。
 *
 * 线程安全：纯值类型。
 */
struct ExternalRefRecord {
    /// 记录 id（草稿内唯一 token；SolidifyRequest.externalRefId 的关联键，
    /// §5.7）。非空。
    std::string externalRefId;
    /// 外部资源绝对路径原文（存储侧登记；解析/安全校验归 io——§5.7）。
    /// 非空。
    std::string absolutePath;
    /// 登记时内容 SHA-256，64 个小写十六进制字符（NFR-REL-04 检测基准）。
    std::string contentHash256;
    /// 登记时内容字节数（单位＝字节；uint64——大文件不截断）。
    std::uint64_t sizeBytes = 0;
    /// 登记时刻，ISO-8601 UTC 文本。
    std::string recordedAtUtc;
    /// 生命周期状态 token（透传；词表归阶段 B io 契约冻结——§5.7
    /// ExternalResourceRecord.state，如 recorded/materialized）。
    std::string state;

    bool operator==(const ExternalRefRecord& o) const noexcept
    {
        return externalRefId == o.externalRefId && absolutePath == o.absolutePath
            && contentHash256 == o.contentHash256 && sizeBytes == o.sizeBytes
            && recordedAtUtc == o.recordedAtUtc && state == o.state;
    }
    bool operator!=(const ExternalRefRecord& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 草稿文档（§4.4.5 字段表）——未应用草稿的磁盘形态（PM-04"保存与
 *        应用分离"：草稿落盘不产生任何修订）。
 *
 * 背景说明（§3.1 组成表的落位说明）：§3.1 将 DraftDocument 列于
 * DraftService.hpp 行，但 §4.4 开头约定"以下类型在 PersistenceFormat.hpp
 * 冻结"且任务契约 PRJ-T04.json acceptance 1 明确 round-trip 全类型含
 * DraftDocument——本头即其冻结落位；PRJ-T12 草稿服务落位 DraftService.hpp
 * 时直接复用本类型（服务层不再重复定义，避免两处契约漂移）。
 *
 * 归属三元组（projectId/branchId/moduleId）在加载时与路径和 project.json
 * 校验（不一致＝DraftCorruptDetected——§4.1 drafts 行；路径一致性校验归
 * DraftService 载体 PRJ-T12，本层做单文件内字段校验）。payload 是模块草稿
 * 负载（域所有 canonical 字节——含 SourcedValue 来源标记在内的域序列化
 * 全部原样透传，project 不解释；CR-02/D-10）。
 *
 * 线程安全：纯值类型。
 */
struct DraftDocument {
    /// 草稿格式版本（§4.4.5"草稿格式版本"；随整体 schemaVersion 联动，
    /// §8.11 判定同源）。
    int schemaVersion = kSchemaVersionCurrent;
    /// 归属项目（prj- 规范文本）。
    ProjectId projectId{};
    /// 归属分支（brn- 规范文本；与所在目录名一致——路径校验归 PRJ-T12）。
    BranchId branchId{};
    /// 注册的模块 token（§4.1 drafts 行命名规则；注册表校验归 DraftService
    /// ——PRJ-T12；本层做非空 ASCII token 校验）。非空。
    std::string moduleId;
    /// 草稿基线修订（rev- 规范文本；≠分支 tip 时应用被拒——PM-04）。
    RevisionId baseRevisionId{};
    /// 模块草稿负载（域所有 canonical 字节，UTF-8——含 SourcedValue 来源
    /// 标记在内的域序列化原样透传，project 不解释；CR-02/D-10）。Codec 对
    /// 其只做 UTF-8 编码健全性检查（ASCII 磁盘格式的承载前提），不做任何
    /// 语义解析。
    std::string payload;
    /// 草稿期外部引用记录（可选——canonical 省略规则：空列表＝缺省字段；
    /// PM-01 登记口径/NFR-SEC-01，见 ExternalRefRecord 注释）。数组元素序
    /// ＝登记序（数据本身，Codec 原序输出）。
    std::vector<ExternalRefRecord> externalRefs;
    /// 落盘时间，ISO-8601 UTC 文本。
    std::string savedAtUtc;
    /// 草稿来源（冻结三值，见 DraftOrigin）。
    DraftOrigin origin = DraftOrigin::Autosave;

    bool operator==(const DraftDocument& o) const noexcept
    {
        return schemaVersion == o.schemaVersion && projectId == o.projectId
            && branchId == o.branchId && moduleId == o.moduleId
            && baseRevisionId == o.baseRevisionId && payload == o.payload
            && externalRefs == o.externalRefs && savedAtUtc == o.savedAtUtc
            && origin == o.origin;
    }
    bool operator!=(const DraftDocument& o) const noexcept { return !(*this == o); }
};

}  // namespace sdurws::ird::project

#endif  // SDURWS_IRD_PROJECT_PERSISTENCEFORMAT_HPP
