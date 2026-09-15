/**
 * @file   Codec.hpp
 * @brief  project 自有格式 canonical 编解码器（§4.8）——字节↔PersistenceFormat
 *         结构的唯一转换入口（私有实现头，不出 include/——R-2 纪律）。
 *
 * 设计依据：
 *   - units/project.md §3.4（源码目录布局：src/Codec.{hpp,cpp}＝"project
 *     自有格式 canonical 编码（§4.8）"）、§4.8（canonical 编码契约：
 *     ASCII、固定字段序、无浮点——全字符串身份与整数、parse(dump(x))==x）、
 *     §4.4（字段级契约——字段序即 §4.4 表列序）、§8.11（版本判定与拒绝）；
 *   - 需求 CON-01（身份/版本包络持久化）、NFR-DEP-04/PM-06（版本拒绝＋
 *     升级指引数据）、NFR-COR-02（确定性：同值必同字节）；
 *   - 任务契约 tasks/foundation/PRJ-T04.json acceptance 1～4。
 *
 * 背景说明（CR-02 处置——为什么本编解码器没有哈希路径）：
 *   foundation-contract-review CR-02（已关闭）：摘要算法只调用 core
 *   ContentDigester。本单元的对应纪律：①对象负载的内容版本摘要＝对收到的
 *   字节经 core::ContentDigester 计算（D-10：project 不解释域负载），唯一
 *   入口＝contentVersionOf()；②dump/parse 对 digest 类字段（manifestDigest/
 *   digest256/contentHash256 等）只透传、不重算、不复算校验——"自有格式
 *   编码器不私设第二哈希路径"。PRJ-T05 对象库与 PRJ-T06 修订索引的摘要
 *   需求一律复用 contentVersionOf()（或直接持 core::ContentDigester）。
 *
 * 错误语义（错误二分，AGENTS §3）：
 *   - 数据侧错误（磁盘内容损坏/越界/版本不符）→ StoreError：
 *       StoreCorrupt  ＝语法/结构/字段/值域非法（含截断、非 ASCII 输入、
 *                       缺必填、未知字段〔当前版本〕、整数越界、身份字段
 *                       违反 core 规范文本、摘要字段非 64 小写 hex 等）；
 *       FormatLegacy  ＝formatId 不符或 schemaVersion 主版本低于当前支持
 *                       （§8.11 行 1——稳定只读拒绝）；
 *       SchemaFuture  ＝schemaVersion 主版本高于当前支持（§8.11 行 2——
 *                       detail 携带升级指引数据 document/supported/upgrade）。
 *   - 调用方错误 → std::invalid_argument fail-fast：dump 输入违反 canonical
 *     契约（如 payload 含无效 UTF-8 字节序列——域负载契约归 core §6.3）。
 *
 * 次版本兼容语义（§4.8 稳定性；NFR-DEP-04）：
 *   schemaVersion 同主版本、次版本高于本实现（如 1.1 文档、本实现 1.0）＝
 *   上级版本"追加可选字段"——解析容忍并跳过未知字段（兼容读取，已知字段
 *   语义不变）。注意：跳过意味着 dump(parse(x)) 丢未知字段——降级读仅用于
 *   只读展示，写路径永远写当前版本（由调用方纪律保证，PRJ-T08/T12）。
 *
 * 线程安全：全部函数为纯函数（无共享可变状态），可并发调用。
 */

#ifndef SDURWS_IRD_PROJECT_SRC_CODEC_HPP
#define SDURWS_IRD_PROJECT_SRC_CODEC_HPP

#include <string>
#include <string_view>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/project/PersistenceFormat.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>  // StoreError/StoreErrorCode——parse 的
                                             // 错误契约类型（头自包含原则：消费本头
                                             // 的 TU 不必另行猜测补包）

namespace sdurws::ird::project::codec {

// =====================================================================
// canonical 编码（dump）——§4.8：ASCII、固定字段序、无浮点、紧凑无空白
// =====================================================================

/**
 * @brief ProjectStaticIdentity → canonical JSON 字节（§4.2 字段序）。
 *
 * @param value [in] 静态标识（projectId 须为合法 core 规范文本——由类型
 *              不变量保证；违反不可达）
 * @return 纯 ASCII、紧凑、固定字段序的 JSON 文本（同值必同字节，NFR-COR-02）
 *
 * @throws std::invalid_argument 不可达路径（本类型无自由 UTF-8 字段——
 *         防御性契约声明，与含 UTF-8 字段的 dump 对齐）
 */
std::string dump(const ProjectStaticIdentity& value);

/**
 * @brief HeadRecord → canonical JSON 字节（§4.4.1 表列序字段序）。
 *
 * @param value [in] HEAD 记录
 * @return 纯 ASCII、紧凑、固定字段序的 JSON 文本
 *
 * @throws std::invalid_argument 不可达路径（无自由 UTF-8 字段）
 */
std::string dump(const HeadRecord& value);

/**
 * @brief RevisionManifest → canonical JSON 字节（§4.4.2 表列序字段序；
 *        introducedObjects 空列表＝缺省省略）。
 *
 * @param value [in] 修订清单
 * @return 纯 ASCII、紧凑、固定字段序的 JSON 文本
 *
 * @throws std::invalid_argument 不可达路径（无自由 UTF-8 字段）
 */
std::string dump(const RevisionManifest& value);

/**
 * @brief ProjectMetadataRecord → canonical JSON 字节（§4.4.3 表列序；
 *        supersedes/schemeLabels 空＝缺省省略；schemeLabels 键按字典序输出
 *        ——std::map 有序，canonical 确定性）。
 *
 * @param value [in] 元数据记录（projectDisplayName 为 UTF-8）
 * @return 纯 ASCII、紧凑、固定字段序的 JSON 文本（非 ASCII 字符经
 *         \uXXXX/代理对转义）
 *
 * @throws std::invalid_argument projectDisplayName 含无效 UTF-8 字节序列
 *         （域 canonical 契约违约——core §6.3：负载为 UTF-8；调用方错误
 *         fail-fast，不做静默替换）
 */
std::string dump(const ProjectMetadataRecord& value);

/**
 * @brief CommandRecord → canonical JSON 字节（§4.4.4 表列序；inverse 空＝
 *        缺省省略、confirmations 空列表＝缺省省略）。
 *
 * @param value [in] 命令留痕（payloadCanonical/summary/payload 等为 UTF-8）
 * @return 纯 ASCII、紧凑、固定字段序的 JSON 文本
 *
 * @throws std::invalid_argument payloadCanonical/summary/inverse 载荷含
 *         无效 UTF-8 字节序列（域 canonical 契约违约——fail-fast）
 */
std::string dump(const CommandRecord& value);

/**
 * @brief DraftDocument → canonical JSON 字节（§4.4.5 表列序；externalRefs
 *        空列表＝缺省省略）。
 *
 * @param value [in] 草稿文档（payload 为域所有 canonical 字节——UTF-8）
 * @return 纯 ASCII、紧凑、固定字段序的 JSON 文本
 *
 * @throws std::invalid_argument payload/absolutePath 等含无效 UTF-8 字节
 *         序列（域 canonical 契约违约——fail-fast）
 */
std::string dump(const DraftDocument& value);

// =====================================================================
// 严格解析（parse）——拒绝一切非法输入（acceptance 2）；版本判定 §8.11
// =====================================================================

/**
 * @brief canonical JSON → ProjectStaticIdentity（project.json 读取）。
 *
 * 版本判定：formatId ≠ "rwdesign" 或主版本低于当前 → FormatLegacy；
 * 主版本高于当前 → SchemaFuture（detail 含 document/supported/upgrade
 * 升级指引键值）；同主版本次版本更高 → 容忍未知字段（次版本兼容）。
 *
 * @param text [in] 磁盘字节（须为纯 ASCII canonical JSON；非 ASCII 输入
 *             拒绝——§4.8 磁盘格式 ASCII，防编码错乱静默入库）
 * @return 解析结果
 *
 * @throws StoreError StoreCorrupt（语法/截断/缺字段/类型/越界/身份格式）、
 *         FormatLegacy、SchemaFuture（同上判定）
 */
ProjectStaticIdentity parseStaticIdentity(std::string_view text);

/**
 * @brief canonical JSON → HeadRecord（HEAD 文件读取）。版本判定同
 *        parseStaticIdentity。
 *
 * @param text [in] 磁盘字节（纯 ASCII canonical JSON）
 * @return 解析结果
 *
 * @throws StoreError 同 parseStaticIdentity 的错误语义
 */
HeadRecord parseHeadRecord(std::string_view text);

/**
 * @brief canonical JSON → RevisionManifest（manifest.json 读取）。
 *
 * 本类型无 schemaVersion/formatId 字段（§4.4.2 表）——版本语义由容器
 * （project.json/HEAD）判定（见 PersistenceFormat.hpp RevisionManifest
 * 注释），本函数按当前支持版本的结构严格解析（未知字段拒绝）。
 *
 * @param text [in] 磁盘字节（纯 ASCII canonical JSON）
 * @return 解析结果
 *
 * @throws StoreError StoreCorrupt（含 objectRefs 为空数组——§4.4.2 ≥1 约束）
 */
RevisionManifest parseRevisionManifest(std::string_view text);

/**
 * @brief canonical JSON → ProjectMetadataRecord（元数据对象负载读取）。
 *        版本判定同 parseStaticIdentity。
 *
 * @param text [in] 磁盘字节（纯 ASCII canonical JSON）
 * @return 解析结果
 *
 * @throws StoreError StoreCorrupt（含 branches 空数组——§4.4.3 ≥1 约束）、
 *         FormatLegacy、SchemaFuture
 */
ProjectMetadataRecord parseMetadataRecord(std::string_view text);

/**
 * @brief canonical JSON → CommandRecord（command.json 读取）。
 *
 * 本类型无 schemaVersion/formatId 字段（§4.4.4 表）——同
 * parseRevisionManifest 的容器判定说明，按当前支持版本严格解析。
 *
 * @param text [in] 磁盘字节（纯 ASCII canonical JSON）
 * @return 解析结果
 *
 * @throws StoreError StoreCorrupt（含 commandType 违反 ^[a-z0-9-]{3,64}）
 */
CommandRecord parseCommandRecord(std::string_view text);

/**
 * @brief canonical JSON → DraftDocument（*.draft.json 读取）。版本判定同
 *        parseStaticIdentity（schemaVersion＝草稿格式版本，随整体联动）。
 *
 * @param text [in] 磁盘字节（纯 ASCII canonical JSON）
 * @return 解析结果
 *
 * @throws StoreError StoreCorrupt（含 origin 非冻结三值）、FormatLegacy、
 *         SchemaFuture
 */
DraftDocument parseDraftDocument(std::string_view text);

// =====================================================================
// CR-02 唯一摘要入口（D-10：对象负载摘要＝收到的字节经 core ContentDigester）
// =====================================================================

/**
 * @brief 计算对象负载的 ContentVersion（内容版本——内容寻址编址键）。
 *
 * 设计依据（CR-02/D-10 处置，本单元摘要路径的**唯一**入口）：
 *   - units/project.md §4.6（"project 对收到的字节计算 SHA-256 作为
 *     ContentVersion 并原样存储——D-10"）、§4.8（canonical 编码契约——
 *     对什么做摘要归各所有者，算法只经 core）；
 *   - foundation-contract-review CR-02（已关闭）：摘要算法只调用 core
 *     ContentDigester；每个编码器必须声明排除字段——本编码器（dump/parse）
 *     对全部 digest 类字段零计算（只透传），即"排除字段＝全部"的声明形态；
 *   - core.md v0.1 §4.2/§5.2（ContentDigester 增量式接口——P-PR-1 消费
 *     基线：不私改 core，消费其 v0.1 冻结签名）。
 *
 * 实现口径：对输入字节序列（分块方式无关——ContentDigester 纯函数性）
 * 一次性 update 后 finalize。**不解释负载内容**（任意字节均可，无格式
 * 前提——对象文件本体是域负载原样字节，§4.4.6）。
 *
 * @param payloadBytes [in] 对象负载原样字节（长度可为 0——空字节序列的
 *                     摘要合法，如空负载对象；无单位——按字节计）
 * @return 内容版本（cv-<64hex> 规范文本形态见 ContentVersion::toCanonical；
 *         全零不可达——SHA-256 无全零输出映射到任意已知输入的构造场景）
 *
 * 线程安全：纯函数（内部自持 ContentDigester 实例——core 纪律：每线程
 * 各持实例，本函数实例为栈局部，天然满足）。
 */
core::ContentVersion contentVersionOf(std::string_view payloadBytes);

}  // namespace sdurws::ird::project::codec

#endif  // SDURWS_IRD_PROJECT_SRC_CODEC_HPP
