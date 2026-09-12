/**
 * @file   Evaluator.cpp
 * @brief  评估器接口与注册表实现——两注册表（§9.4/§9.5）＋注册期校验
 *         拒绝原语＋注册清单 manifest 摘要。
 *
 * 设计依据：
 *   - units/evidence.md §9.4（EvaluatorRegistry 注册表行为冻结——重复
 *     注册/未知键/注册期验证/实例生命周期/线程安全/主 worker 一致性）、
 *     §9.5（EvidenceProfileRegistry——重复拒绝/contentIdentity 注册时
 *     计算/注册期校验/交叉校验经注册顺序解耦）、§9.2（descriptor 字段
 *     表与"任一失败即拒绝并指明字段"）、§10.2（注册与调用时序）
 *   - 需求 CON-06/AT-19（manifest 摘要跨进程一致）、CON-04（契约版本）、
 *     EVI-01/OPT-03/OPT-05/SEL-05（③端口承载）；任务契约
 *     tasks/foundation/EV-T10.json（≙WP-05-T10）acceptance 1～3
 *
 * 实现口径（登记单元卡 v1.1，实现细节精确化、语义与设计一致）：
 *   - R-1：create(未知键) 返回 nullptr 不抛——与 find 的"查询非抛"同轨
 *     （§9.4 未知键行：调用方给"评估器不可用"诊断；装配不一致由
 *     execution 的 manifest 摘要比对在派发前拦截，进程内不设双语义）；
 *   - R-2：注册检查序＝空 factory → 重复键 → 注册期校验 → 登记（重复
 *     边界先于内容校验——重复注册与 descriptor 品质无关，§9.4 两行
 *     相互独立；同坏注册必报同一首错，NFR-COR-02）；
 *   - R-3：descriptor.profile 复用 EvidenceProfileRef 三元组结构但只
 *     消费声明面 {profileId, version}——contentIdentity 由 evidence 于
 *     Profile 注册时计算（§9.5"域不可申报"），评估器申报非零值即注册
 *     拒绝（RegistrationIssueCode::ProfileContentIdentityDeclared）；
 *     manifest 的 profileIdentity 恒取 Profile 注册表权威值；
 *   - R-4（Profile 注册检查序）：重复 (profileId, version) → 语法校验
 *     （validateEvidenceProfile）→ contentIdentity 覆盖计算 → 入库
 *     （重复边界先于内容校验，与 R-2 同一纪律）。
 *
 * manifest 摘要编码（RegistrationManifest.digest——非往返身份投影，
 * IRDPRF1 同款纪律：无 parse，唯一消费方式＝摘要；摘要唯一经
 * core::ContentDigester——CR-02）。字节布局（整型一律大端，§5.2 同源）：
 *   magic "IRDRGM1"（8 字节）｜codec 版本 0x01（1 字节）
 *   ｜条目数 u32
 *   ｜逐条目（按 key 字典序——调用方传入已排序清单）：
 *     key（u32 长度前缀＋字节）｜contractVersion u32
 *     ｜profileIdentity（32 字节原始摘要）
 * 同一注册集（任意注册顺序）必得同一编码与摘要（entries 排序规范化
 * 由 EvaluatorRegistry::manifest 的 map 迭代序保证——NFR-COR-02）。
 *
 * 线程安全：两注册表共享std::shared_mutex 三段纪律（注册排他/查询共享/
 * 工厂调用锁外）；锁序唯一（评估器注册表→Profile 注册表单向），无死锁面。
 */

#include <sdurws/ird/evidence/Evaluator.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace sdurws::ird::evidence {

namespace {

// =====================================================================
// manifest 编码原语（Evidence.cpp 同族规范字节流拼装——本 .cpp 局部）
// =====================================================================

/// 追加大端 u32（§5.2 定宽整型大端——codec 家族统一）。
void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

/// 追加长度前缀字符串（u32 长度＋原始字节——UTF-8 透明承载）。
void appendLenStr(std::vector<std::uint8_t>& out, const std::string& text)
{
    appendU32(out, static_cast<std::uint32_t>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
}

/**
 * @brief 计算注册清单摘要（实现见文件头字节布局；entries 必须已按 key
 *        字典序排序——由 EvaluatorRegistry::manifest 保证，本函数不复查
 *        以保持纯编码职责；摘要唯一经 core::ContentDigester——CR-02）。
 *
 * @param entries [in] 已排序清单条目
 * @return 清单摘要（同注册集恒同值——主/worker 比对的前提，CON-06）
 *
 * 线程安全：可重入纯函数（不修改输入；ContentDigester 为栈上局部实例
 * ——每线程各持，§4.2 core 契约）。
 */
core::ContentIdentity computeRegistrationManifestDigest(
    const std::vector<RegistrationManifestEntry>& entries)
{
    std::vector<std::uint8_t> encoding;
    // 预留：magic 8＋版本 1＋条目数 4＋每条目（4＋key 长度＋4＋32）——
    // 预估值，避免多次扩容（性能微优化，不影响语义）。
    std::size_t reserve = 13;
    for (const RegistrationManifestEntry& entry : entries) {
        reserve += 40 + entry.key.size();
    }
    encoding.reserve(reserve);

    // magic "IRDRGM1"＋codec 版本 0x01（编码演进锚——布局变更必须升版
    // 并在单元卡留痕；读取方按摘要整体比较，不存在旧码解码面）。
    const std::string_view magic{"IRDRGM1"};
    encoding.insert(encoding.end(), magic.begin(), magic.end());
    encoding.push_back(0x01u);
    appendU32(encoding, static_cast<std::uint32_t>(entries.size()));

    // 逐条目规范编码（顺序由调用方保证——key 字典序）。
    for (const RegistrationManifestEntry& entry : entries) {
        appendLenStr(encoding, entry.key);
        appendU32(encoding, entry.contractVersion);
        encoding.insert(encoding.end(), entry.profileIdentity.bytes.begin(),
                        entry.profileIdentity.bytes.end());
    }

    // 摘要收尾：SHA-256（core 唯一摘要实现——CR-02）→ ContentIdentity。
    core::ContentDigester digester;
    digester.update(encoding.data(), encoding.size());
    core::ContentIdentity identity;
    identity.bytes = digester.finalize();
    return identity;
}

/// 声明键集合中是否含给定键（线性扫描——注册期规模小，保持输入 const
/// 与确定性；与 Dependency.hpp 声明校验器的同款口径）。
bool containsKey(const std::vector<std::string>& keys, std::string_view key)
{
    for (const std::string& candidate : keys) {
        if (candidate == key) { return true; }
    }
    return false;
}

/// descriptor 依赖声明的键清单（Profile 条件交叉校验的"声明键"半边）。
std::vector<std::string> declaredKeysOf(const EvaluatorDescriptor& descriptor)
{
    std::vector<std::string> keys;
    keys.reserve(descriptor.inputs.size());
    for (const DependencyDeclaration& declaration : descriptor.inputs) {
        keys.push_back(declaration.key);
    }
    return keys;
}

/// 快照事实键查找（§4.2.3①注入词表——evidence 不拥有其语义）。
bool isFactKey(const std::vector<std::string>& snapshotFactKeys, std::string_view key)
{
    return containsKey(snapshotFactKeys, key);
}

}  // namespace

// =====================================================================
// EvidenceProfileRegistry（§9.5）
// =====================================================================

void EvidenceProfileRegistry::registerProfile(RequiredEvidenceProfile profile)
{
    // 全程排他锁：注册期操作（装配期单线程约定；锁保证并发调用不产生
    // 数据竞争——"运行期不增删"语义约束见契约头，锁不替代约定）。
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    // 第一步：注册边界——重复 (profileId, version) 拒绝（§9.5；先于内容
    // 校验——注册边界与 Profile 内容品质无关，实现口径 R-4）。
    const auto idIt = m_profiles.find(profile.profileId);
    if (idIt != m_profiles.end()
        && idIt->second.find(profile.version) != idIt->second.end()) {
        throw EvidenceError(
            EvidenceErrorCode::ProfileDuplicate,
            "(profileId=«" + profile.profileId + "», version=«" + profile.version
                + "») 已注册——重复注册拒绝，不覆盖、不静默（§9.5；同域多版本"
                  "共存须升版本号，版本是注册键的一部分）");
    }

    // 第二步：注册期语法校验（§9.5——item 语法/条件词形/替代标志与
    // itemClass 一致性/Common 禁止域登记；问题全部一次列出，detail 逐条
    // 含 itemId/下标——"指明字段"纪律）。条件闭包**不在此处**：经注册
    // 顺序解耦，与评估器声明的交叉校验在评估器注册时做（§9.5 原文）。
    const std::vector<ProfileIssue> issues = validateEvidenceProfile(profile);
    if (!issues.empty()) {
        std::string detail;
        for (const ProfileIssue& issue : issues) {
            if (!detail.empty()) { detail += "; "; }
            detail += "[" + issue.itemId + "#" + std::to_string(issue.index) + "] "
                + issue.message;
        }
        throw EvidenceError(EvidenceErrorCode::ProfileInvalid, std::move(detail));
    }

    // 第三步：contentIdentity 由 evidence 计算（§9.5"域不可申报"——
    // 调用方携带值一律被注册权威值覆盖；同内容恒同身份，NFR-COR-02）。
    profile.contentIdentity = computeProfileContentIdentity(profile);

    // 第四步：入库（嵌套 map 节点稳定——findProfile 返回指针跨后续注册
    // 有效；注册后 Profile 按不可变对待）。
    m_profiles[profile.profileId].emplace(profile.version, std::move(profile));
}

const RequiredEvidenceProfile* EvidenceProfileRegistry::findProfile(
    std::string_view profileId, std::string_view version) const
{
    // 共享锁（§9.5"线程安全同 9.4"——运行期并发只读安全）。
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    const auto idIt = m_profiles.find(profileId);
    if (idIt == m_profiles.end()) {
        return nullptr;
    }
    const auto versionIt = idIt->second.find(version);
    if (versionIt == idIt->second.end()) {
        return nullptr;
    }
    return &versionIt->second;
}

// =====================================================================
// 注册期校验（§9.4 注册期验证行——检查序见契约头 validateEvaluatorRegistration）
// =====================================================================

std::vector<RegistrationIssue> validateEvaluatorRegistration(
    const EvaluatorDescriptor& descriptor,
    const EvidenceProfileRegistry& profileRegistry,
    const std::vector<std::string>& snapshotFactKeys)
{
    std::vector<RegistrationIssue> issues;

    // ---- 1. key 词形（§9.4"key 语法"——isValidEvaluationKey＝§8.2 原文
    // 语法；评估键不含点，与依赖键词形的差异点在 Slice.hpp 注释）。
    if (!isValidEvaluationKey(descriptor.key)) {
        issues.push_back({RegistrationIssueCode::KeySyntaxInvalid, "key",
                          "评估键词形非法（应为 [a-z][a-z0-9-]{1,63}、不含点；实得 «"
                              + descriptor.key + "»）——§9.4 注册期验证"});
    }

    // ---- 2. 契约版本（§9.4"契约版本>0"——0 为保留值；CON-04：契约版本
    // 进入切片身份，0 版会使"同输入不同契约版本＝不同切片"退化）。
    if (descriptor.contractVersion == 0) {
        issues.push_back({RegistrationIssueCode::ContractVersionZero, "contractVersion",
                          "契约版本必须 >0（0 为保留值——CON-04 身份面退化）——"
                          "§9.4 注册期验证"});
    }

    // ---- 3. 支持模式集（§9.4"模式集非空"＋§9.2"子集"语义——子集无重复）。
    if (descriptor.supportedModes.empty()) {
        issues.push_back({RegistrationIssueCode::SupportedModesEmpty, "supportedModes",
                          "支持模式集为空（§9.2 要求 Preview/Quick/Verified 子集，≥1）"
                          "——§9.4 注册期验证"});
    } else {
        // 重复检测：规模 ≤3（三值词表的子集），平方扫描足够；每对重复只
        // 报首处（确定性——避免三连同值产生噪声清单）。
        for (std::size_t i = 1; i < descriptor.supportedModes.size(); ++i) {
            for (std::size_t j = 0; j < i; ++j) {
                if (descriptor.supportedModes[i] == descriptor.supportedModes[j]) {
                    issues.push_back(
                        {RegistrationIssueCode::SupportedModesDuplicate, "supportedModes",
                         std::string{"支持模式集含重复项 «"}
                             + core::toToken(descriptor.supportedModes[i])
                             + "»（\"子集\"语义不允许重复）——§9.2/§9.4 注册期验证"});
                    break;
                }
            }
        }
    }

    // ---- 4. threadSafety 词表合法性（§9.4"threadSafety 合法"——诚构造
    // 路径不可能非法；本检查是位型完整性的防御面，见 isKnownThreadSafety）。
    if (!isKnownThreadSafety(descriptor.threadSafety)) {
        issues.push_back({RegistrationIssueCode::ThreadSafetyUnknown, "threadSafety",
                          "threadSafety 为词表外位型（损坏/伪造 descriptor——"
                          "isKnownThreadSafety 拒绝）——§9.4 注册期验证"});
    }

    // ---- 5. Profile 内容身份保留值（实现口径 R-3——§9.5"域不可申报"：
    // 申报非零值＝伪造绑定身份面，manifest 恒取注册表权威值）。
    if (descriptor.profile.contentIdentity.isValid()) {
        issues.push_back({RegistrationIssueCode::ProfileContentIdentityDeclared,
                          "profile.contentIdentity",
                          "评估器不得申报 Profile 内容身份（由 evidence 于 Profile "
                          "注册时计算——§9.5；contentIdentity 应置保留值）"});
    }

    // ---- 6. Profile 解析（§9.2"注册时必须已可解析"——未注册即拒）。
    const RequiredEvidenceProfile* profile = profileRegistry.findProfile(
        descriptor.profile.profileId, descriptor.profile.version);
    if (profile == nullptr) {
        issues.push_back(
            {RegistrationIssueCode::ProfileUnresolvable, "profile",
             "(profileId=«" + descriptor.profile.profileId + "», version=«"
                 + descriptor.profile.version + "») 未注册——§9.2 注册时必须已可解析；"
                 "域按『Profile 注册在前、评估器注册在后』接入序（§13）"});
    } else {
        // ---- 7. Profile 条件交叉校验（§9.5"与评估器声明的交叉校验在评估
        // 器注册时做"——referencedKeys ⊆ 声明键 ∪ 快照事实键；Profile 注册
        // 时已保证其语法面，此处只做闭包半场）。合并序＝required 先、
        // suggested 后（ProfileIssue 同源约定——确定性）。
        const std::vector<std::string> declaredKeys = declaredKeysOf(descriptor);
        std::vector<const EvidenceProfileItem*> items;
        items.reserve(profile->required.size() + profile->suggested.size());
        for (const EvidenceProfileItem& item : profile->required) {
            items.push_back(&item);
        }
        for (const EvidenceProfileItem& item : profile->suggested) {
            items.push_back(&item);
        }
        for (const EvidenceProfileItem* item : items) {
            // 无适用条件的项不参与交叉校验（Always——无 referencedKeys 面）。
            if (!item->applicability.has_value()) {
                continue;
            }
            for (const std::string& referencedKey : item->applicability->referencedKeys) {
                // 闭包规则：条件决定键必须是评估器已声明的依赖键或快照
                // 事实键——否则该条件在评估器侧不可解析（条件翻转将无法
                // 触发重解析，D-10 同源防线）。
                if (containsKey(declaredKeys, referencedKey)
                    || isFactKey(snapshotFactKeys, referencedKey)) {
                    continue;
                }
                issues.push_back(
                    {RegistrationIssueCode::ProfileConditionKeyOutOfClosure, "profile",
                     "Profile 条件 referencedKey «" + referencedKey + "»（item «"
                         + item->itemId + "»）不在评估器声明键 ∪ 快照事实键内——"
                         "§9.5 交叉校验（注册顺序解耦的评估器注册侧半场）"});
            }
        }
    }

    // ---- 8. 依赖声明闭包（§9.4"依赖声明闭包校验（§4.2.3①）"——EV-T02
    // 预登记"注册拒绝归 EvaluatorRegistry 消费"的校验器复用；问题逐条
    // 映射为 DeclarationIssue，细节透传在 message——码面分工见
    // requireValidEvaluatorRegistration）。
    const std::vector<DependencyIssue> declarationIssues
        = validateDependencyDeclarations(descriptor.inputs, snapshotFactKeys);
    for (const DependencyIssue& issue : declarationIssues) {
        issues.push_back({RegistrationIssueCode::DeclarationIssue, "inputs",
                          "[" + issue.key + "#" + std::to_string(issue.index) + "] "
                              + issue.message});
    }

    return issues;
}

void requireValidEvaluatorRegistration(
    const EvaluatorDescriptor& descriptor,
    const EvidenceProfileRegistry& profileRegistry,
    const std::vector<std::string>& snapshotFactKeys)
{
    const std::vector<RegistrationIssue> issues
        = validateEvaluatorRegistration(descriptor, profileRegistry, snapshotFactKeys);
    if (issues.empty()) {
        return;
    }

    // 聚合全部问题为一条消息（"[字段] 说明; …"——"任一失败即拒绝并指明
    // 字段"（§9.4）的承载；一次注册可看全，顺序＝检查序确定）。
    std::string detail;
    for (const RegistrationIssue& issue : issues) {
        if (!detail.empty()) { detail += "; "; }
        detail += "[" + issue.field + "] " + issue.message;
    }

    // 码面分工（EV-T02 预登记契约的兑现）：DeclarationIssue 在检查序中
    // 恒位于清单尾部（第 8 步生成）——首条问题即 DeclarationIssue 当且
    // 仅当 descriptor 完整性/Profile 面全部通过、只剩声明闭包问题，此时
    // 按 §4.2.3① 既有码面抛 DeclarationInvalid；否则抛注册表侧码
    // EvaluatorDescriptorInvalid（两类并存时优先报 descriptor 侧——
    // 确定性首错，NFR-COR-02 builder 惯例）。
    if (issues.front().code == RegistrationIssueCode::DeclarationIssue) {
        throw EvidenceError(EvidenceErrorCode::DeclarationInvalid, std::move(detail));
    }
    throw EvidenceError(EvidenceErrorCode::EvaluatorDescriptorInvalid, std::move(detail));
}

// =====================================================================
// EvaluatorRegistry（§9.4）
// =====================================================================

EvaluatorRegistry::EvaluatorRegistry(const EvidenceProfileRegistry& profileRegistry)
    : m_profileRegistry(profileRegistry)
{
}

void EvaluatorRegistry::registerEvaluator(std::unique_ptr<IEvaluatorFactory> factory,
                                          const std::vector<std::string>& snapshotFactKeys)
{
    // ---- ①factory 非空（调用方注册契约违约——fail-fast；AGENTS 错误
    // 语义：调用方错误不允许静默忽略）。
    if (!factory) {
        throw EvidenceError(EvidenceErrorCode::EvaluatorDescriptorInvalid,
                            "factory 不得为空（注册候选缺工厂——调用方注册契约违约）");
    }

    // descriptor 引用指向 factory 对象自有成员（非 unique_ptr 控制块）——
    // 后续 move(unique_ptr) 不影响其有效性（工厂对象本体不动）。
    const EvaluatorDescriptor& descriptor = factory->descriptor();

    // ---- ②～④全程排他锁（注册期操作；锁序唯一：本表→Profile 表单向）。
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    // ---- ②重复键检查（§9.4 重复注册/版本冲突两行：同键再次注册＝注册
    // 边界拒绝，无论契约版本是否相同——"换版本＝装配清单变更"不在运行
    // 期做；先于内容校验——注册边界与 descriptor 品质无关，实现口径 R-2）。
    const auto existing = m_evaluators.find(descriptor.key);
    if (existing != m_evaluators.end()) {
        throw EvidenceError(
            EvidenceErrorCode::EvaluatorDuplicate,
            "evaluationKey=«" + descriptor.key + "» 已注册（契约版本 "
                + std::to_string(existing->second.contractVersion)
                + "）——同键再次注册＝注册边界拒绝，不覆盖、不静默（§9.4）；"
                  "换版本＝装配清单变更，须以新装配清单整体重装（跨进程一致，"
                  "CON-06），不存在运行期热替换");
    }

    // ---- ③注册期验证（§9.4 注册期验证行——字段完整性/Profile 解析/
    // 交叉校验/声明闭包；"任一失败即拒绝并指明字段"由 issue 聚合保证，
    // 码面分工见 requireValidEvaluatorRegistration）。在排他锁内调用：
    // Profile 查询走其共享锁——锁序单向，无死锁面。
    requireValidEvaluatorRegistration(descriptor, m_profileRegistry, snapshotFactKeys);

    // ---- ④登记：键 → {工厂, 契约版本, Profile 权威内容身份}（manifest
    // 载荷在此固化——运行期只读；Profile 指针必非空：第③步已验证可解析）。
    Registered entry;
    entry.factory = std::move(factory);
    entry.contractVersion = descriptor.contractVersion;
    entry.profileIdentity
        = m_profileRegistry
              .findProfile(descriptor.profile.profileId, descriptor.profile.version)
              ->contentIdentity;
    m_evaluators.emplace(descriptor.key, std::move(entry));
}

const IEvaluatorFactory* EvaluatorRegistry::find(std::string_view evaluationKey) const
{
    // 共享锁（§9.4"运行期 find/manifest() 并发只读安全"）。
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    const auto it = m_evaluators.find(evaluationKey);
    // 未知键返回 nullptr（查询非抛——§9.4；调用方给"评估器不可用"诊断）。
    return it == m_evaluators.end() ? nullptr : it->second.factory.get();
}

std::unique_ptr<IEngineeringEvaluator> EvaluatorRegistry::create(
    std::string_view evaluationKey) const
{
    // 查表在共享锁内（与 find 同轨——未知键 nullptr，实现口径 R-1）。
    const IEvaluatorFactory* factory = find(evaluationKey);
    if (factory == nullptr) {
        return nullptr;
    }
    // 工厂调用在锁外：工厂由注册表独占持有且运行期不增删（§9.4）——
    // 指针在调用期必然存活；锁外创建避免域代码在锁内执行（create()
    // 线程安全由工厂实现方保证——§9.4/IEvaluatorFactory 契约）。
    return factory->create();
}

RegistrationManifest EvaluatorRegistry::manifest() const
{
    // 第一段（共享锁内）：按 map 迭代序（key 字典序升序——std::map 单值
    // 键的默认严格弱序即字节字典序）快照条目清单——§9.4"按 key 字典序
    // 排序"的排序稳定性即 EV-REG-2 观测点。
    std::vector<RegistrationManifestEntry> entries;
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        entries.reserve(m_evaluators.size());
        for (const auto& keyValue : m_evaluators) {
            RegistrationManifestEntry entry;
            entry.key = keyValue.first;
            entry.contractVersion = keyValue.second.contractVersion;
            entry.profileIdentity = keyValue.second.profileIdentity;
            entries.push_back(std::move(entry));
        }
    }

    // 第二段（锁外）：摘要计算（纯编码——无需持锁；同注册集恒同值）。
    RegistrationManifest manifest;
    manifest.entries = std::move(entries);
    manifest.digest = computeRegistrationManifestDigest(manifest.entries);
    return manifest;
}

bool EvaluatorRegistry::isRegistered(std::string_view evaluationKey) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_evaluators.find(evaluationKey) != m_evaluators.end();
}

bool EvaluatorRegistry::contractVersionMatches(std::string_view evaluationKey,
                                               std::uint32_t contractVersion) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    const auto it = m_evaluators.find(evaluationKey);
    // 未注册键返回 false（IProducerRegistryView 契约：调用序恒为先
    // isRegistered 后本查询——false 是安全默认，不另行报错）。
    if (it == m_evaluators.end()) {
        return false;
    }
    return it->second.contractVersion == contractVersion;
}

}  // namespace sdurws::ird::evidence
