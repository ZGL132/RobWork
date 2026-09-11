/**
 * @file   Codec.hpp
 * @brief  RT-Codec——CanonicalModel 的 canonical 二进制编码（§4.5 原文契约），
 *         三形态：encode（全字段编码）/ parse（解码往返）/ 内容身份（身份域
 *         编码＋SHA-256 摘要）。
 *
 * 设计依据：
 *   - units/runtime.md §4.5（RT-Codec 规则总表——编码形态/可选值/数值/旋转
 *     矩阵/身份域/往返/线程与确定性逐行）、§4.3.5（contentIdentity 行——
 *     "SHA-256 over RT-Codec(本模型除 diagnostics/capabilities/contentIdentity
 *     外全部字段)"）、§4.3.6（字节相同＝唯一等价关系）、§7.6（RT-Codec 家族
 *     子形态命名——IRDNAME 随 NameMap/RT-T05 落位）
 *   - core.md §4.2 责任边界（CR-02 承接）：core 提供"字节→摘要"（SHA-256
 *     唯一实现＝core::ContentDigester）；"对什么字节做摘要"归本单元——本头
 *     即 runtime 侧的**排除字段声明点**
 *   - 需求 ARC-03（纯函数可重入）、CON-05（内容寻址）、NFR-COR-02（跨进程
 *     一致：main/worker 同身份）、NFR-COR-03（缺失不伪造——presence 字节）
 *   - 任务契约 tasks/foundation/RT-T04.json（产物 2：Codec.hpp/.cpp——
 *     RT-Codec 三形态；knownPitfalls CR-02 处置落点）
 *
 * 背景说明（为什么自有二进制编码——决策 D-08）：无共享 JSON 库（实测）；
 * 跨进程身份一致要求编码不含任何平台/locale/第三方序列化器差异；身份不
 * 依赖第三方。因此用确定性二进制：固定 magic＋结构版本＋声明序字段＋长度
 * 前缀＋大端＋无填充。
 *
 * ★★★ CR-02 排除字段声明（跨单元红线，验收对照点）★★★
 *   身份域＝§4.3 字段全集**排除**以下五项（出处：§4.5"身份域"行＋§4.3.5
 *   contentIdentity 行＋§4.3.6"预设 token 不入身份"）：
 *     1. header.revisionSeq        ——仅展示排序；内容身份不依赖修订序号
 *                                    （CON-05 精神，§4.3.1"不入"列）；
 *     2. world.installPreset       ——预设 token＋来源标记是编辑表示/来源
 *                                    记录，T_world_base 已承载结果（§4.3.2
 *                                    "不入身份"、§4.3.6"预设 token 不入
 *                                    身份"——改名/换标记不产生新身份）；
 *     3. diagnostics               ——编译过程记录，同输入同诊断由确定性
 *                                    间接保证（§4.3.5"不入"、D-12）；
 *     4. capabilities              ——内容的派生投影，由身份域字段确定性
 *                                    推导（§4.3.5"不入"、D-12）；
 *     5. contentIdentity 自身      ——身份不自我引用（§4.3.5"即身份本身"）。
 *   上述清单由本头的 encodeIdentityDomain 实现，并由单元测试逐项钉住
 *   （CodecTest 身份域排除用例——契约 acceptance 3）。
 *
 * 三形态与失败语义：
 *   - encode/encodeIdentityDomain：纯函数、可重入、无 I/O、无隐藏状态
 *     （§4.5"线程/确定性"行）；唯一失败面＝遇到非有限 double（§4.5"数值"
 *     行"编码入口拒绝"）→ 抛 RuntimeError(InputInvalid)——合法 builder 产物
 *     全字段有限，不会触发；
 *   - parse：查询轨——失败返回 Expected err（不抛 RuntimeError；编解码的
 *     输入是外部字节，属可恢复数据错误）。仅接受全字段编码（身份域编码
 *     不是往返载体）；解码后经 CanonicalModelBuilder 全量不变量复核＋身份
 *     复核（防篡改/半传输——NFR-COR-03 不吞错）。
 *
 * NameMap 子形态（IRDNAME，§7.6）已随 RT-T05 在本头增量落位（见文件尾
 * 声明区）：encodeNameMap/parseNameMap/computeNameMapContentIdentity 三形态，
 * 与 CanonicalModel 三形态同规则（大端/长度前缀/确定性/CR-02 摘要边界）；
 * ★ 差异：IRDNAME 的内容身份＝SHA-256 over **该编码自身**（§7.6 原文），
 * 故编码内不嵌摘要字段（自引用不可行）——防篡改核对以重算身份比对实现
 * （worker 物化核对 D-13 的映射层形态）。
 *
 * 线程安全：全部纯函数（无共享可变状态），并发调用安全。
 * 确定性：同模型重复编码逐字节相等；编码不含时间/环境/地址量（ARC-03）。
 */

#ifndef SDURWS_IRD_RUNTIME_CODEC_HPP
#define SDURWS_IRD_RUNTIME_CODEC_HPP

#include <array>
#include <cstdint>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>        // ContentIdentity（内容身份产物）
#include <sdurws/ird/runtime/CanonicalModel.hpp>  // 被编码的规范模型
#include <sdurws/ird/runtime/Errors.hpp>     // Expected（parse 查询轨）
#include <sdurws/ird/runtime/NameMap.hpp>    // IRDNAME 编码对象（RT-T05 增量）

namespace sdurws::ird::runtime::rtcodec {

// =====================================================================
// 编码头常量（§4.5"编码形态"行：magic＋结构版本；结构版本号变更＝编码
// 破坏性变更，走设计变更评审——§4.3 结构级约定）。
// =====================================================================

/// 魔数 "IRDCANO"（§4.5 原文）——CanonicalModel canonical 编码家族标识。
inline constexpr std::array<std::uint8_t, 7> kMagic{'I', 'R', 'D', 'C', 'A', 'N', 'O'};

/// 结构版本 major（结构版本号 canonical-model/1.0——§4.3 结构级约定）。
inline constexpr std::uint16_t kVersionMajor = 1;
/// 结构版本 minor（minor 升版＝向后兼容追加；major 升版＝破坏性变更）。
inline constexpr std::uint16_t kVersionMinor = 0;

/// 编码头域标志：全字段编码（parse 的唯一输入形态）。
inline constexpr std::uint16_t kDomainFull = 0;
/// 编码头域标志：身份域编码（排除 CR-02 清单——不作为 parse 输入）。
inline constexpr std::uint16_t kDomainIdentity = 1;

// =====================================================================
// 三形态 1/3：encode——全字段确定性编码（往返与 worker 物化的载体）。
// =====================================================================

/**
 * @brief 全字段确定性编码（§4.5"往返"行——parse(encode(x))==x 的 encode 侧；
 *        §4.1"可经 RT-Codec 序列化供 worker 物化"的字节载体）。
 *
 * 布局（字段按 CanonicalModel.hpp 声明序；全部整数/长度/计数大端、无填充；
 * double 为 IEEE754 位模式大端；optional 用 presence 字节显式编码——nullopt
 * ≠ 零值；SourcedValue＝状态字节＋载荷＋Provided 时来源记录）：
 *   magic(7) | major(2) | minor(2) | domain(2)=0
 *   header{project(16) branch(16) revision(16) revisionSeq(8)
 *           objectRefs{count(4) 逐条{objectId(16) contentVersion(32)
 *                     objectTypeToken(str) digest(32)}}〔按 ObjectId 规范
 *                     文本字典序——builder 已规范化〕
 *           descriptionContractVersion(4) compilerContractVersion(4)
 *           builtFrom(32)}
 *   world{T_world_base{R(9×8 行主序) t(3×8)} installPreset{SourcedValue<枚举>}
 *         gravityWorld(3×8)}
 *   chain{robotObjectId(16) robotLocalName(str) deviceName(str)
 *         joints{count(4) 逐条{…链序}} links{count(4) 逐条{…链序}}}
 *   tools{count(4) 逐条{…}} defaultTcpIndex{presence(1) [+index(4)]}
 *   scene{count(4) 逐条{…}}
 *   drivetrain{ratio{count(4) 逐条 Sv<double>} coupling{presence(1) [+…]}}
 *   resourceManifest{count(4) 逐条 ResourceRef}
 *   diagnostics{count(4) 逐条 DiagnosticRecord}
 *   capabilities{9×bool(1) jointTypes{count(4)+逐条(1)} hasBidirectionalNameMap(1)}
 *   contentIdentity(32)
 *
 * @param model [in] 规范模型（应为 CanonicalModelBuilder 产物——字段已规范化，
 *              本函数不重排；只读）
 * @return 编码字节（确定性——同模型重复调用逐字节相等；调用方持有）
 *
 * @throws RuntimeError InputInvalid 编码中遇到非有限 double（§4.5"数值"行
 *         "NaN/±Inf 编码入口拒绝"；合法 builder 产物全字段有限，不会触发）
 *
 * 线程/确定性：纯函数、可重入、无 I/O、无隐藏状态（§4.5 末行）。
 */
std::vector<std::uint8_t> encode(const CanonicalModel& model);

// =====================================================================
// 三形态 2/3：parse——解码（往返与 worker 物化的重建侧）。
// =====================================================================

/**
 * @brief 解码全字段编码并重建模型（§4.5"往返"行——parse(encode(x))==x 的
 *        parse 侧）。
 *
 * 校验链（任一失败返回 err，不抛、不产出半成品——NFR-COR-03）：
 *   ①magic/domain/版本匹配（仅接受 kDomainFull；版本不符＝拒绝而非尽力
 *     猜测——编码升版是破坏性变更）；
 *   ②长度前缀逐字段解码，任何越界/截断/尾随字节/非法枚举值/非法 presence
 *     值→InputInvalid（detail 携字节偏移定位）；
 *   ③解码结果经 CanonicalModelBuilder::build() 全量不变量复核（值级违约
 *     →对应 RuntimeError 转入 err——防篡改字节绕过构造约束）；
 *   ④身份复核：重算 contentIdentity 与编码携带值比对，不等→InputInvalid
 *     （身份域字节被篡改/半传输的确定性检测——worker 物化身份核对 D-13
 *     的模型层前置）。
 *
 * @param bytes [in] encode() 产出的全字段编码（只读；可为任意来源——本函数
 *              对外部字节做防御性校验）
 * @return ok＝重建的模型（与编码前的 builder 产物 operator== 相等且身份
 *         相等）；err＝校验失败（RuntimeError 携 InputInvalid/StructureInvalid
 *         与字节偏移定位）
 *
 * 线程/确定性：纯函数、可重入；同字节→同结果（NFR-COR-02）。
 */
Expected<CanonicalModel, RuntimeError> parse(const std::vector<std::uint8_t>& bytes);

// =====================================================================
// 三形态 3/3：内容身份——身份域编码＋SHA-256（CR-02 的摘要边界）。
// =====================================================================

/**
 * @brief 身份域编码（CR-02 排除字段声明的实现——文件头"排除字段声明"五项
 *        不出现在本编码中；其余布局与 encode 同规则，编码头 domain=1）。
 *
 * @param model [in] 规范模型（只读）
 * @return 身份域编码字节（确定性；调用方持有）
 *
 * @throws RuntimeError InputInvalid 同 encode（非有限拒绝）
 */
std::vector<std::uint8_t> encodeIdentityDomain(const CanonicalModel& model);

/**
 * @brief 计算模型内容身份（§4.3.5 contentIdentity＝SHA-256 over 身份域编码）。
 *
 * CR-02 摘要边界：本函数（及本单元一切摘要路径）**只调用**
 * core::ContentDigester——runtime 不实现第二套 SHA-256、不经第三方哈希；
 * 对什么字节做摘要由 encodeIdentityDomain 声明。
 *
 * @param model [in] 规范模型（只读）
 * @return 内容身份（非零——合法模型身份域编码非空，SHA-256 无全零碰撞面）
 *
 * @throws RuntimeError InputInvalid 同 encode（非有限拒绝）
 *
 * 确定性：同模型→同身份（跨进程一致——NFR-COR-02；RT-ID-1 钉住）。
 */
core::ContentIdentity computeContentIdentity(const CanonicalModel& model);

// =====================================================================
// RT-Codec 家族子形态：IRDNAME——RuntimeNameMap 的 canonical 编码（§7.6，
// 随 RT-T05 落位）。与 IRDCANO 同规则：确定性二进制、magic＋结构版本、
// 字段按声明序（条目按映射存储序＝(scope, localName, ObjectId 规范文本)
// 字典序——§7.2/§7.6 同键）、长度前缀、大端、无填充、UTF-8 无 NUL。
// =====================================================================

/// 魔数 "IRDNAME"（§7.6 子形态家族标识——与 kMagic 同为 7 字节）。
inline constexpr std::array<std::uint8_t, 7> kNameMapMagic{'I', 'R', 'D', 'N', 'A', 'M', 'E'};

/// IRDNAME 结构版本 major（编码升版＝破坏性变更，走设计变更评审——同上）。
inline constexpr std::uint16_t kNameMapVersionMajor = 1;
/// IRDNAME 结构版本 minor。
inline constexpr std::uint16_t kNameMapVersionMinor = 0;

/**
 * @brief 全字段确定性编码 RuntimeNameMap（§7.6；RT-NM-1 编码往返的 encode 侧）。
 *
 * 布局（大端、无填充）：
 *   magic(7) | major(2) | minor(2) | ruleVersion(4)
 *   count(4) | 逐条目（映射存储序）：
 *     scope(1) | objectId(16) | scopeToken{len(4)+UTF-8}
 *     | localName{len(4)+UTF-8} | fullName{len(4)+UTF-8}
 *     | authoritativeLocalName{len(4)+UTF-8}
 *
 * @param map [in] 名称映射（应为 buildRuntimeNameMap/parseNameMap 产物——
 *              条目已规范化，本函数不重排；只读）
 * @return 编码字节（确定性——同映射重复调用逐字节相等；调用方持有）
 *
 * @throws RuntimeError InputInvalid 空映射（无构建来源——worker 通道不应
 *         序列化占位值；NFR-COR-03 不吞错）
 *
 * 线程/确定性：纯函数、可重入、无 I/O、无隐藏状态。
 */
std::vector<std::uint8_t> encodeNameMap(const RuntimeNameMap& map);

/**
 * @brief 解码 IRDNAME 编码并重建映射（RT-NM-1 编码往返的 parse 侧；§9.3
 *        worker 物化的映射层载体）。
 *
 * 校验链（任一失败返回 err，不抛、不产出半成品——NFR-COR-03）：
 *   ①magic/版本匹配（版本不符＝拒绝而非尽力猜测）；②长度前缀逐字段解码，
 *   越界/截断/尾随字节/非法 scope 值→InputInvalid（detail 携字节偏移）；
 *   ③结构不变量复核：fullName 形态（Device 作用域 fullName==localName，
 *   其余 fullName==scopeToken+"."+localName）、fullName 全局唯一、
 *   (objectId, scope) 唯一、条目序＝(scope, localName, ObjectId) 字典序；
 *   ④内容身份重算（编码不含摘要字段——身份＝SHA-256 over 编码自身，见
 *   文件头差异说明），供调用方与请求预期值核对（worker 按身份核对，
 *   不等即拒绝执行——§9.3）。
 *
 * @param bytes [in] encodeNameMap() 产出的编码（只读；可为任意来源）
 * @return ok＝重建的映射（与编码前 operator== 相等且身份相等）；err＝校验
 *         失败（RuntimeError 携 InputInvalid 与字节偏移定位）
 *
 * 线程/确定性：纯函数、可重入；同字节→同结果（NFR-COR-02）。
 */
Expected<RuntimeNameMap, RuntimeError> parseNameMap(const std::vector<std::uint8_t>& bytes);

/**
 * @brief 计算映射内容身份（§7.6：nameMapContentIdentity＝SHA-256 over
 *        IRDNAME 编码——含规则版本编码头）。
 *
 * CR-02 摘要边界：只调用 core::ContentDigester（runtime 不实现第二套
 * SHA-256）；"对什么字节做摘要"由 encodeNameMap 声明。
 *
 * @param map [in] 名称映射（只读；空映射抛 InputInvalid——同 encodeNameMap）
 * @return 内容身份（非零；buildRuntimeNameMap 产物与 parseNameMap 重建产物
 *         同身份——跨进程一致，NFR-COR-02）
 *
 * @throws RuntimeError InputInvalid 空映射（同 encodeNameMap）
 */
core::ContentIdentity computeNameMapContentIdentity(const RuntimeNameMap& map);

}  // namespace sdurws::ird::runtime::rtcodec

#endif  // SDURWS_IRD_RUNTIME_CODEC_HPP
