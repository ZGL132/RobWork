/**
 * @file   CollisionEvaluator.cpp
 * @brief  碰撞评估唯一实现——会话构建三步（§6.4）的产品实现：场景校验、
 *         作用域展开与冲突同码复核、检测器初始化与会话身份计算。
 *
 * 设计依据：
 *   - units/policy.md §6.1（场景校验四项——会话构建期）、§6.4（createSession
 *     三步：作用域解析/检测器构建/sessionIdentity；确定性）、§7.1（作用域
 *     矩阵——可执行规则）、§7.2（展开集冲突检查"排除∩必检=∅"＋评估顺序
 *     的状态语义）、§7.5（名称不可解析→会话构建失败）、§9.3（唯一实现
 *     入口与复现要素取值）、§12 POL-T06 行（构建期半区交付）
 *   - traceability/foundation-api-diff.md CR-04（sceneContentIdentity←
 *     workCellCompileIdentity 裁决；WorkCell 共享只读；零 runtime 编译依赖）
 *   - 需求 ARC-05（唯一实现）、ARC-04（不可解析不猜测）、MDL-04/15
 *     （Self 自碰撞/Environment 环境参与——作用域矩阵行）、KIN-05（无几何
 *     不伪装已检——构建期只登记事实）
 *
 * 实现纪律（本文件为 rw 非模板类（WorkCell/ProximitySetup/规则）的消费
 * TU——集成模式专属编译单元，冒烟模式不进构建，runtime RT-T07 同款
 * CMake 分工；RobWork 命名标识符（RobWorkCollisionEvaluator/make 工厂/
 * RW_VERSION）归同模式的姊妹 TU——src/RobWorkCollisionEvaluator.cpp，
 * 切分说明见该文件头与头文件 detail 声明注释）：
 *   - 名称消费仅经 IPolicyNameContext（完整名精确匹配，R-4 无拼接/剥离）；
 *   - ProximitySetup 不取自 WorkCell（R-POL-3——RobWork 默认碰撞设置不进
 *     产品策略，静态对排除默认过滤显式关闭）；
 *   - sessionIdentity 摘要唯一经 core::ContentDigester（SHA-256——D-05；
 *     无第二哈希实现。PolicyCodec::contentIdentity 保持"策略内容身份"的
 *     唯一计算点不变，会话身份是 §6.4/§4.1 既定的另一身份概念）；
 *   - 全部错误走 PolicyError fail-fast（§9.3 错误类型行"场景校验失败→
 *     PolicyError"）——无诊断收集轨（构建失败即无会话，无部分产物）。
 *
 * 线程安全：全部入口为 const 只读或构造期一次性逻辑（可重入）；会话构造
 * 后只读（§9.3"并发只读可重入"）。确定性：同输入同产物（固定校验序/展开
 * 序/规范排序——NFR-COR-02）。
 */

#include <sdurws/ird/policy/CollisionEvaluator.hpp>

#include <rw/models/WorkCell.hpp>
#include <rw/proximity/ProximitySetup.hpp>
#include <rw/proximity/ProximitySetupRule.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace sdurws::ird::policy {

// =====================================================================
// 词表转发表（SceneObjectRole↔token）——词表串事实源仍在
// PolicyParsing sceneObjectRoleTokens()（§6.1"场景对象角色词表归建模语义"
// 同源；本表为会话构建期的映射面，非第二词表）。
// =====================================================================

std::string_view sceneObjectRoleToken(SceneObjectRole role) noexcept
{
    // 编译期固定 switch 全枚举（无 default——遗漏即编译器告警）；串与
    // PolicyParsing kRoleTokens 逐字一致（POL-SCOPE 展开与解析期词表核对
    // 同词表——单一事实源约束的运行期保证）。
    switch (role) {
    case SceneObjectRole::RobotLink: return "RobotLink";
    case SceneObjectRole::Tool: return "Tool";
    case SceneObjectRole::Payload: return "Payload";
    case SceneObjectRole::EnvironmentObject: return "EnvironmentObject";
    case SceneObjectRole::Workpiece: return "Workpiece";
    }
    // 不可达路径：全枚举已覆盖（满足编译器出口要求；全表用例钉住）。
    return {};
}

std::optional<SceneObjectRole> trySceneObjectRole(std::string_view token) noexcept
{
    // 精确等值匹配（附录 D 第 12 项——无大小写折叠/无空白容忍；词表外
    // 一律 nullopt，由调用方按场景校验语义拒绝——ARC-04 不猜测）。
    for (const SceneObjectRole role :
         {SceneObjectRole::RobotLink, SceneObjectRole::Tool, SceneObjectRole::Payload,
          SceneObjectRole::EnvironmentObject, SceneObjectRole::Workpiece}) {
        if (sceneObjectRoleToken(role) == token) {
            return role;
        }
    }
    return std::nullopt;
}

// =====================================================================
// detail——会话构建的内部实现件（非公共契约；R-2 纪律同 PolicySet detail）。
// =====================================================================

namespace detail {

/// 会话身份组成的形态代（组成变化时升代——与策略 schema 同款版本纪律）。
inline constexpr std::uint8_t kSessionIdentityForm = 1;

/// 会话身份魔数（8 字节——"IRDPOL1"帧式 magic 的会话域对偶；防止把其他
/// 域的摘要字节误当会话身份比较）。
inline constexpr std::array<std::uint8_t, 8> kSessionIdentityMagic
    = {'I', 'R', 'D', 'S', 'E', 'S', 'S', '1'};

/**
 * @brief 规范化对象对（字节字典序 A<B——无序对语义的展开形态）。
 *
 * 依据：ScopedCollisionPair 规范化注释（NFR-COR-05 稳定排序基础）；排序
 * 键＝ObjectId::operator<（Identity.hpp 字节字典序——容器键既定序）。
 */
inline std::pair<core::ObjectId, core::ObjectId>
canonicalPair(const core::ObjectId& a, const core::ObjectId& b)
{
    if (a < b) {
        return {a, b};
    }
    return {b, a};
}

/**
 * @brief §7.1 矩阵行归属判定（角色对 → 碰撞域；矩阵未列 → nullopt）。
 *
 * 逐行对照 units/policy.md §7.1 矩阵（可执行规则——本函数是矩阵的机械
 * 转写，评审可逐行核对）：
 *   | 机器人×机器人（相邻/非相邻）          | Self        |
 *   | 机器人连杆×工具/负载                  | Tool        |
 *   | 工具/负载×环境对象                    | Environment |
 *   | 工具/负载×工件（Workpiece 归环境侧）  | Tool        |
 *   | 机器人连杆×环境对象/工件              | Environment |
 *   | 环境对象/工件×环境对象/工件           | Scene       |
 *   | 工具×工具/负载×负载（矩阵未列）        | nullopt     |
 * 无序对语义：入口先按角色枚举值序重排（规范化），分类只写一份——
 * 枚举声明序 RobotLink<Tool<Payload<EnvironmentObject<Workpiece 即规范化序。
 */
std::optional<CollisionDomain> classifyDomainPair(SceneObjectRole a, SceneObjectRole b)
{
    // 无序对规范化：按枚举值序重排（分类表的行数减半——与 canonicalPair
    // 同方向的确定性手段）。
    if (static_cast<int>(b) < static_cast<int>(a)) {
        std::swap(a, b);
    }
    // 重排后 (a,b) 升序——逐行判定（每行注释标注矩阵原文行）。
    switch (a) {
    case SceneObjectRole::RobotLink:
        switch (b) {
        case SceneObjectRole::RobotLink:         return CollisionDomain::Self;        // 机器人非相邻/相邻连杆
        case SceneObjectRole::Tool:              return CollisionDomain::Tool;        // 工具 vs 连杆（安装侧相邻/非相邻）
        case SceneObjectRole::Payload:           return CollisionDomain::Tool;        // 负载 vs 连杆
        case SceneObjectRole::EnvironmentObject: return CollisionDomain::Environment; // 连杆 vs 环境对象
        case SceneObjectRole::Workpiece:         return CollisionDomain::Environment; // 连杆 vs 工件（工件归环境侧）
        }
        break;
    case SceneObjectRole::Tool:
        switch (b) {
        case SceneObjectRole::Tool:              return std::nullopt;                 // 工具×工具——矩阵未列
        case SceneObjectRole::Payload:           return std::nullopt;                 // 工具×负载——矩阵未列
        case SceneObjectRole::EnvironmentObject: return CollisionDomain::Environment; // 机器人侧 vs 环境对象
        case SceneObjectRole::Workpiece:         return CollisionDomain::Tool;        // 工具 vs 工件（Tool 域显式行）
        }
        break;
    case SceneObjectRole::Payload:
        switch (b) {
        case SceneObjectRole::Payload:           return std::nullopt;                 // 负载×负载——矩阵未列
        case SceneObjectRole::EnvironmentObject: return CollisionDomain::Environment; // 机器人侧 vs 环境对象
        case SceneObjectRole::Workpiece:         return CollisionDomain::Tool;        // 负载 vs 工件
        }
        break;
    case SceneObjectRole::EnvironmentObject:
        switch (b) {
        case SceneObjectRole::EnvironmentObject: return CollisionDomain::Scene;      // 环境 vs 环境（默认不检）
        case SceneObjectRole::Workpiece:         return CollisionDomain::Scene;      // 环境 vs 工件（环境侧对象间）
        }
        break;
    case SceneObjectRole::Workpiece:
        break;   // (Workpiece, Workpiece)——环境侧对象间
    }
    // 工件×工件＝环境侧对象间（Scene 域——同"环境 vs 环境"行）。
    if (a == SceneObjectRole::Workpiece && b == SceneObjectRole::Workpiece) {
        return CollisionDomain::Scene;
    }
    return std::nullopt;
}

/**
 * @brief 计算会话身份（§6.4：sessionIdentity=f(policy, scene, names, backend)）。
 *
 * 组成（规范编码——大端、长度前缀、无填充，与 PolicyCodec §5.3 帧纪律同源）：
 *   magic(8B) + form(1B) + policyCid(32B) + sceneCid(32B) + nameMapCid(32B)
 *   + backendId/backendVersion/toleranceModel 各 u32 长度前缀＋原文 UTF-8 字节。
 *
 * 摘要唯一经 core::ContentDigester（SHA-256——D-05；CR-02 红线：本单元不
 * 复制摘要算法、不私设第二哈希路径；PolicyCodec::contentIdentity 的"策略
 * 内容身份唯一计算点"登记不适用于本函数——会话身份是 §4.1 既定的另一
 * 身份概念，其摘要算法来源仍唯一归 core）。
 *
 * @return 会话身份（SHA-256 输出非全零——isValid 恒 true）
 */
core::ContentIdentity computeSessionIdentity(const core::ContentIdentity& policyCid,
                                             const core::ContentIdentity& sceneCid,
                                             const core::ContentIdentity& nameMapCid,
                                             const CollisionBackendDescriptor& backend)
{
    // 增量摘要器栈上各持（非线程安全——本函数为纯函数局部使用，POL-T03
    // 同款"每线程各持实例"纪律）。
    core::ContentDigester digester;
    digester.update(kSessionIdentityMagic.data(), kSessionIdentityMagic.size());
    digester.update(&kSessionIdentityForm, sizeof(kSessionIdentityForm));
    // 三个内容身份按 §6.4 f() 参数序固定排列（顺序即语义——组成变化升
    // kSessionIdentityForm，旧身份与新身份不可比）。
    digester.update(policyCid.bytes.data(), policyCid.bytes.size());
    digester.update(sceneCid.bytes.data(), sceneCid.bytes.size());
    digester.update(nameMapCid.bytes.data(), nameMapCid.bytes.size());
    // 后端复现要素三串：u32 大端长度前缀＋原文字节（无终止符——长度即界，
    // 与 PolicyCodec lengthPrefixedText 同构；空串合法——长度 0）。
    const std::string* backendParts[] = {&backend.backendId, &backend.backendVersion,
                                         &backend.toleranceModel};
    for (const std::string* part : backendParts) {
        const std::array<std::uint8_t, 4> lenBigEndian{
            static_cast<std::uint8_t>((part->size() >> 24) & 0xFFu),
            static_cast<std::uint8_t>((part->size() >> 16) & 0xFFu),
            static_cast<std::uint8_t>((part->size() >> 8) & 0xFFu),
            static_cast<std::uint8_t>(part->size() & 0xFFu)};
        digester.update(lenBigEndian.data(), lenBigEndian.size());
        digester.update(part->data(), part->size());
    }
    // finalize 即结束（幂等禁止——core §5.2）；SHA-256 输出不可能全零。
    core::ContentIdentity identity;
    identity.bytes = digester.finalize();
    return identity;
}

}  // namespace detail

// =====================================================================
// detail——内置后端复现要素单点（声明见 CollisionEvaluator.hpp detail 块；
// 字面量单点与 RobWork 命名标识符分 TU 的切分说明见该声明注释与本头
// 文件头"翻译单元切分说明"）。
// =====================================================================

namespace detail {

CollisionBackendDescriptor makeBuiltinBackendDescriptor(std::string backendVersion)
{
    // 复现要素冻结值（§6.5/§8.1）：backendId＝§6.5 原文取值；backendVersion
    // ＝调用方注入的基线冻结值（kFrozenBuiltinBackendVersion——P-POL-5，
    // WP-24-T01 基线 ird/share/baseline.md §3 登记值；原"暂取 RW_VERSION
    // 构建版本"临时口径已消账）；toleranceModel＝后端固有数值分辨率登记串
    // （复现要素非工程阈值——R-7/§7.4 三分；登记事实不发明数值）。
    CollisionBackendDescriptor descriptor;
    descriptor.backendId = "rw.proximity.builtin-rw";
    descriptor.backendVersion = std::move(backendVersion);
    descriptor.toleranceModel =
        "builtin-rw/bvtree;binary-collision+distance;numeric-resolution=backend-internal"
        "(not-an-engineering-threshold;P-POL-5 frozen@WP-24-T01:ird/share/baseline.md)";
    return descriptor;
}

}  // namespace detail

// =====================================================================
// ResolvedCollisionScope 计数访问器（声明见 CollisionEvaluator.hpp）。
// =====================================================================

std::size_t ResolvedCollisionScope::inScopePairCount() const noexcept
{
    // 线性遍历计数（状态谓词——与展开序无关的确定性结果）。
    std::size_t count = 0;
    for (const ScopedCollisionPair& p : pairs) {
        if (p.status == ScopePairStatus::Mandatory
            || p.status == ScopePairStatus::InScopeDefault) {
            ++count;
        }
    }
    return count;
}

std::size_t ResolvedCollisionScope::excludedPairCount() const noexcept
{
    std::size_t count = 0;
    for (const ScopedCollisionPair& p : pairs) {
        if (p.status == ScopePairStatus::ExcludedByRule
            || p.status == ScopePairStatus::ExcludedByAdjacencyDefault) {
            ++count;
        }
    }
    return count;
}

std::size_t ResolvedCollisionScope::inScopePairCount(CollisionDomain domain) const noexcept
{
    std::size_t count = 0;
    for (const ScopedCollisionPair& p : pairs) {
        if (p.domain == domain
            && (p.status == ScopePairStatus::Mandatory
                || p.status == ScopePairStatus::InScopeDefault)) {
            ++count;
        }
    }
    return count;
}

// =====================================================================
// 会话构建三步（§6.4）——校验序固定（确定性；任一步失败即无会话）。
// =====================================================================

namespace {

/// 场景对象索引（ObjectId→条目只读指针——装配自 scene.objects；键唯一性
/// 已由第一步校验保证，重复在先失败）。
using SceneObjectIndex = std::map<core::ObjectId, const SceneObjectEntry*>;

/// 规范对象对键（展开集/相邻集的容器键——字节字典序）。
using PairKey = std::pair<core::ObjectId, core::ObjectId>;

/**
 * @brief 第一步：场景校验（§6.1 四项＋policy/装配防御复检——见类注释①）。
 *
 * 校验序固定（①身份复检→②场景结构→③主链设备解析→④规则引用核对）——
 * 每步失败即抛，cause 定位对象（规范文本＋局部名——可追溯，ERR-01）。
 *
 * @throws PolicyError(PolicyObjectInvalid) policy 非 Valid/身份无效/后端
 *         实例空（调用方契约违约）
 * @throws PolicyError(SceneInvalid) 场景结构四项任一失败
 * @throws PolicyError(NameUnresolved) primaryDevice 名称解析断链
 */
void validateSceneAndPolicy(const EngineeringPolicySet& policy, const CollisionScene& scene,
                            const IPolicyNameContext& names,
                            const std::shared_ptr<rw::proximity::CollisionStrategy>& strategy,
                            SceneObjectIndex& objectIndex)
{
    // ① policy 发布态复检（§9.3 前置行"policy 为已发布（Valid）对象"——
    // 把未发布对象当策略传入＝调用方契约违约 fail-fast；与解析管线首段
    // "契约违约复检"同款纪律）。
    if (!policy.policyObject.isValid() || !policy.contentIdentity.isValid()
        || policy.validationState != PolicyValidationState::Valid) {
        throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                          "会话构建：policy 非已发布对象（Valid 态＋双身份有效——§9.3 前置行）");
    }
    // 后端实例非空（§6.4"内置后端实例"——空指针＝装配违约，fail-fast）。
    if (!strategy) {
        throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                          "会话构建：检测器后端实例为空（装配违约——§6.4 检测器两半）");
    }
    // ② sceneContentIdentity 非空（§6.1 场景校验第 4 项——编译产物必有
    // 内容身份；CR-04：值来自 runtime workCellCompileIdentity，policy 只
    // 核对有效性不重算）。
    if (!scene.sceneContentIdentity.isValid()) {
        throw PolicyError(PolicyErrorCode::SceneInvalid,
                          "会话构建：sceneContentIdentity 非空核对失败（§6.1——"
                          "编译产物内容身份缺失）");
    }
    // workcell 编译产物在场（shared_ptr<const WorkCell>——空＝调用方未装配
    // 编译产物，违反 §6.1"输入是运行时快照（编译产物）"的前提）。
    if (!scene.workcell) {
        throw PolicyError(PolicyErrorCode::SceneInvalid,
                          "会话构建：workcell 编译产物缺失（空指针——§6.1 输入模型前提）");
    }
    // objects 的 objectId 唯一（§6.1 场景校验第 1 项；全零保留值＝未分配
    // 身份，同为装配违约——一并拒绝）。subject 绑定违规对象（ERR-01）。
    objectIndex.clear();
    for (const SceneObjectEntry& entry : scene.objects) {
        if (!entry.objectId.isValid()) {
            throw PolicyError(PolicyErrorCode::SceneInvalid,
                              "会话构建：场景对象身份无效（全零保留值）: localName='"
                                  + entry.localName + "'");
        }
        const auto inserted = objectIndex.emplace(entry.objectId, &entry);
        if (!inserted.second) {
            throw PolicyError(
                PolicyErrorCode::SceneInvalid,
                "会话构建：场景对象身份重复（§6.1 objectId 唯一）: localName='"
                    + entry.localName + "' 与 '" + inserted.first->second->localName
                    + "'（obj=" + entry.objectId.toCanonical() + "）");
        }
    }
    // ③ primaryDevice 可经名称上下文解析到 workcell 内设备（§6.1 场景校验
    // 第 3 项）——两级：名称上下文应答（CR-04：runtime Expected 错误→
    // nullopt 原样呈现，不猜测）＋解析名在 workcell 内确有对应设备（场景
    // Frame↔对象 ID 断链＝§7.5"名称不可解析"行，会话构建失败同码）。
    if (!scene.primaryDevice.isValid()) {
        throw PolicyError(PolicyErrorCode::SceneInvalid,
                          "会话构建：primaryDevice 身份无效（全零保留值）");
    }
    const std::optional<std::string> deviceName = names.tryRuntimeName(scene.primaryDevice);
    if (!deviceName.has_value()) {
        throw PolicyError(PolicyErrorCode::NameUnresolved,
                          "会话构建：primaryDevice 经名称上下文不可解析（§6.1——"
                          "不猜测，ARC-04; obj=" + scene.primaryDevice.toCanonical() + "）");
    }
    // 完整名精确匹配设备（findDevice——只读查询；空＝名称在映射内但
    // workcell 无此设备＝编译产物与名称映射断链，同码拒绝）。
    if (scene.workcell->findDevice(*deviceName).isNull()) {
        throw PolicyError(PolicyErrorCode::NameUnresolved,
                          "会话构建：primaryDevice 解析名在 workcell 内无对应设备（§7.5 "
                          "名称不可解析——场景 Frame↔对象 ID 断链）: name='" + *deviceName
                              + "'");
    }
    // ④ 策略规则引用的对象 ⊆ objects（§6.1 场景校验第 2 项）——逐规则
    // 逐目标核对（mandatory 在前、excluded 在后，各按承载序——诊断定位
    // 可复现）。Object 目标核对清单成员；Role 目标做词表防御复检（解析期⑤
    // 已拒词表外——构建期复检为纵深防御）；Group 目标保守拒绝（组定义
    // 数据不在会话输入内——见枚举码注释）。
    const auto checkTarget = [&](const ScopeTarget& target, bool isMandatory,
                                 std::size_t ruleIndex) {
        switch (target.kind) {
        case ScopeTargetKind::Object: {
            // 对象目标必须在场景清单内（§6.1 第 2 项——缺失即会话构建失败；
            // 定位信息＝规则清单/下标/端侧＋对象规范文本——ERR-01 可追溯，
            // 调用方凭此对齐场景装配与规则对象编址）。
            if (objectIndex.find(target.object) == objectIndex.end()) {
                throw PolicyError(
                    PolicyErrorCode::SceneInvalid,
                    "会话构建：策略规则引用对象不在场景清单（§6.1 规则引用 ⊆ objects）: "
                        + std::string{isMandatory ? "mandatoryPairs" : "excludedPairs"} + "["
                        + std::to_string(ruleIndex) + "]（obj=" + target.object.toCanonical()
                        + "）");
            }
            break;
        }
        case ScopeTargetKind::Role: {
            // 角色目标词表防御复检（五值词表——解析期⑤同表；词表外不可达
            // 除非场景装配与解析上下文不一致）。
            if (!trySceneObjectRole(target.roleToken).has_value()) {
                throw PolicyError(PolicyErrorCode::SceneInvalid,
                                  "会话构建：规则角色目标不在场景对象角色词表（防御复检）: '"
                                      + target.roleToken + "'");
            }
            break;
        }
        case ScopeTargetKind::Group: {
            // Group 目标保守拒绝：组成员数据不在 (policy, scene, names) 任一
            // 会话输入内（RawPolicyInput v1 无组定义字段——policy.md v0.5 ⑥
            // 登记"组定义数据由装配侧持有"）。不可核对即拒绝——静默跳过会
            // 违反"过滤不得隐藏必检"（§7.2），猜测违反 ARC-04。数据通道
            // （schema v1.1 组定义字段或装配侧预展开）留待所有者裁决。
            std::string cause = "会话构建：Group 规则目标在会话内不可展开（组成员数据不在";
            cause += "policy/scene/names 任一输入内——v1 schema 无组定义字段，";
            cause += "policy.md v0.5 ⑥ 登记）：groupName='" + target.groupName + "'（";
            cause += isMandatory ? "mandatoryPairs" : "excludedPairs";
            cause += "[" + std::to_string(ruleIndex) + "]）——保守拒绝，不猜测（ARC-04）";
            throw PolicyError(PolicyErrorCode::SceneInvalid, cause);
        }
        }
    };
    for (std::size_t i = 0; i < policy.collision.mandatoryPairs.size(); ++i) {
        checkTarget(policy.collision.mandatoryPairs[i].first, true, i);
        checkTarget(policy.collision.mandatoryPairs[i].second, true, i);
    }
    for (std::size_t i = 0; i < policy.collision.excludedPairs.size(); ++i) {
        checkTarget(policy.collision.excludedPairs[i].first, false, i);
        checkTarget(policy.collision.excludedPairs[i].second, false, i);
    }
}

/**
 * @brief 单条规则的目标展开（§7.2"Role/Group 展开为对象集"——场景面）。
 *
 * 前置：目标已经 validateSceneAndPolicy 核对（Object 在清单内/Role 在
 * 词表内/Group 已拒绝——展开面不可达 Group，防御分支抛不可达违约）。
 *
 * @return 展开出的具体对象集（场景清单承载序——最终对集另做规范排序，
 *         承载序不进入结果序）
 */
std::vector<core::ObjectId>
expandRuleTarget(const ScopeTarget& target, const SceneObjectIndex& objectIndex)
{
    std::vector<core::ObjectId> expanded;
    switch (target.kind) {
    case ScopeTargetKind::Object:
        // 对象目标→对象本身（已在清单——查表取回）。
        expanded.push_back(target.object);
        break;
    case ScopeTargetKind::Role: {
        // 角色目标→场景内该角色全部对象（§7.2"展开为对象集后比对"——
        // 场景装配事实为准，非解析上下文应答：两层防御的第二层差异面）。
        const SceneObjectRole role = *trySceneObjectRole(target.roleToken);
        for (const auto& [id, entry] : objectIndex) {
            if (entry->role == role) {
                expanded.push_back(id);
            }
        }
        break;
    }
    case ScopeTargetKind::Group:
        // 不可达：Group 目标在场景校验期已保守拒绝（validateSceneAndPolicy
        // 第④步）。到达此处＝内部不变式破坏——fail-fast 暴露而非静默空集
        // （空集会让规则"消失"，违反过滤可追溯性）。
        throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                          "会话构建内部不变式破坏：Group 目标应已在场景校验期拒绝");
    }
    return expanded;
}

/**
 * @brief 第二步前半：规则展开＋冲突同码复核（§7.2 两层防御的第二层）。
 *
 * 展开：每条规则的无序目标对 → 全部具体对象对（笛卡尔积去同对象——
 * 自对无碰撞语义）。规范序键入表；同对重复规则以首条为准（承载序——
 * 解析期重复检查的补充面；跨 kind 重复如 Object×Object 与 Role 展开命中
 * 同对，解析期不可判定，此处确定性吸收）。
 *
 * 冲突复核：excludedPairs 展开集 ∩ mandatoryPairs 展开集 = ∅ 必须成立
 * （§7.2 冲突框）——解析期仅可检"同 kind 同载荷＋Object×Role 经上下文"
 * 子集（policy.md v0.5 ④登记），本层以**场景装配事实**复核（与解析期
 * 上下文应答可能不一致——不一致即在此拦截，同码 POLICY-RULE-CONFLICT）。
 *
 * @throws PolicyError(RuleConflict) 展开集相交（cause 定位双方规则下标、
 *         理由与冲突对象对）
 */
void expandRulesAndCheckConflicts(const EngineeringPolicySet& policy,
                                  const SceneObjectIndex& objectIndex,
                                  std::map<PairKey, std::size_t>& mandatoryExpanded,
                                  std::map<PairKey, std::pair<std::size_t, std::string>>&
                                      excludedExpanded)
{
    // 必检规则展开（承载序遍历；map 首条准入——重复对确定性吸收）。
    for (std::size_t i = 0; i < policy.collision.mandatoryPairs.size(); ++i) {
        const PairRule& rule = policy.collision.mandatoryPairs[i];
        const std::vector<core::ObjectId> first = expandRuleTarget(rule.first, objectIndex);
        const std::vector<core::ObjectId> second = expandRuleTarget(rule.second, objectIndex);
        for (const core::ObjectId& a : first) {
            for (const core::ObjectId& b : second) {
                if (a == b) {
                    continue;   // 自对无碰撞语义（同一对象不与自己检测）
                }
                mandatoryExpanded.emplace(detail::canonicalPair(a, b), i);
            }
        }
    }
    // 排除规则展开（同上；携带下标＋理由——AppliedFilterRecord 素材）。
    for (std::size_t i = 0; i < policy.collision.excludedPairs.size(); ++i) {
        const PairRule& rule = policy.collision.excludedPairs[i];
        const std::vector<core::ObjectId> first = expandRuleTarget(rule.first, objectIndex);
        const std::vector<core::ObjectId> second = expandRuleTarget(rule.second, objectIndex);
        for (const core::ObjectId& a : first) {
            for (const core::ObjectId& b : second) {
                if (a == b) {
                    continue;
                }
                excludedExpanded.emplace(detail::canonicalPair(a, b),
                                         std::make_pair(i, rule.reason));
            }
        }
    }
    // 冲突同码复核（§7.2："排除∩必检=∅ 必须成立……违反→POLICY-RULE-
    // CONFLICT"——按必检集承载序定位首个冲突对，确定性诊断；定位信息＝
    // 双方规则清单/下标/理由＋冲突对象对规范文本——ERR-01 可追溯）。
    for (const auto& [pairKey, mandatoryIndex] : mandatoryExpanded) {
        const auto excludedIt = excludedExpanded.find(pairKey);
        if (excludedIt != excludedExpanded.end()) {
            throw PolicyError(
                PolicyErrorCode::RuleConflict,
                "会话构建冲突复核（§7.2 两层防御第二层）: excludedPairs["
                    + std::to_string(excludedIt->second.first) + "]（理由'"
                    + excludedIt->second.second + "'）展开后覆盖 mandatoryPairs["
                    + std::to_string(mandatoryIndex) + "]（理由'"
                    + policy.collision.mandatoryPairs[mandatoryIndex].reason
                    + "'）的对象对（objA=" + pairKey.first.toCanonical()
                    + ", objB=" + pairKey.second.toCanonical() + "）——过滤不得隐藏必检");
        }
    }
}

}  // namespace

// =====================================================================
// 会话构造——三步编排（第一步在自由函数；第二/三步在本构造函数体内）。
// =====================================================================

CollisionEvaluationSession::CollisionEvaluationSession(
    const EngineeringPolicySet& policy, const CollisionScene& scene,
    const IPolicyNameContext& names, CollisionBackendDescriptor backend,
    std::shared_ptr<rw::proximity::CollisionStrategy> strategy,
    std::shared_ptr<std::mutex> backendQueryMutex)
    : m_policy(policy)
    , m_scene(scene)
    , m_nameMapIdentity(names.nameMapContentIdentity())
    , m_backend(std::move(backend))
    , m_strategy(std::move(strategy))
    , m_collisionEnabled(policy.collision.enabled)
    , m_device()                  // 评估半区（POL-T07——buildEvaluationHalf 填充）
    , m_distanceCapable(false)    // 默认无距离能力（探测后更新）
    , m_backendQueryMutex(std::move(backendQueryMutex))
{
    // ---- 第一步：场景校验（§6.1——失败即无会话；对象索引同时装配） ----
    SceneObjectIndex objectIndex;
    validateSceneAndPolicy(m_policy, m_scene, names, m_strategy, objectIndex);

    // ---- 第二步前半：规则展开＋冲突同码复核（§7.2 两层防御第二层） ----
    // 无条件执行（结构性 policy×场景一致性核对——与碰撞总开关无关：
    // "enabled=false 时其余碰撞字段仍须完整合法"§4.3）。
    std::map<PairKey, std::size_t> mandatoryExpanded;
    std::map<PairKey, std::pair<std::size_t, std::string>> excludedExpanded;
    expandRulesAndCheckConflicts(m_policy, objectIndex, mandatoryExpanded, excludedExpanded);

    // ---- 第二步后半：作用域矩阵展开（§7.1 可执行规则） ----
    // 相邻对集合（模型事实——默认相邻过滤的消费源；规范化键）。
    std::set<PairKey> adjacentSet;
    for (const auto& adj : m_scene.adjacentLinkPairs) {
        adjacentSet.insert(detail::canonicalPair(adj.first, adj.second));
    }
    // 启用域查集（§4.3 enabledDomains——解析③不预填默认，值即装配侧预填
    // 结果；域禁用 voids 该域全部对——§7.1 不适用条件列，含必检对）。
    const std::set<CollisionDomain> enabledDomains(m_policy.collision.enabledDomains.begin(),
                                                   m_policy.collision.enabledDomains.end());

    // 逐对象对枚举（i<j 下标组合——对集与对象两两组合一一对应），归类后
    // 统一规范排序（结果序与场景清单承载序无关——NFR-COR-02）。
    std::vector<ScopedCollisionPair> expanded;
    for (std::size_t i = 0; i < m_scene.objects.size(); ++i) {
        for (std::size_t j = i + 1; j < m_scene.objects.size(); ++j) {
            const SceneObjectEntry& entryA = m_scene.objects[i];
            const SceneObjectEntry& entryB = m_scene.objects[j];
            // 矩阵行归属（§7.1——矩阵未列的组合不入作用域，classifyDomainPair
            // 返回 nullopt）。
            const std::optional<CollisionDomain> domain =
                detail::classifyDomainPair(entryA.role, entryB.role);
            if (!domain.has_value()) {
                continue;
            }
            // 域门控（§7.1 不适用条件"域禁用"——该域全部对不适用，必检对
            // 亦然；碰撞总开关关闭＝整体停用，作用域为空——评估期应答
            // DisabledByPolicy（§6.2），本层只如实产出空作用域）。
            if (!m_collisionEnabled || enabledDomains.find(*domain) == enabledDomains.end()) {
                continue;
            }
            // 状态判定序（§7.2 评估顺序框的展开语义——必检优先于一切过滤；
            // 相邻默认过滤仅 Self/Tool 域适用——Environment 行只允许逐对
            // 显式排除，Scene 行默认不检）：
            const PairKey key = detail::canonicalPair(entryA.objectId, entryB.objectId);
            ScopedCollisionPair pair;
            pair.objectA = key.first;
            pair.objectB = key.second;
            // 角色随规范序归位（A<B 的展开形态——coverage 分域计数素材）。
            if (key.first == entryA.objectId) {
                pair.roleA = entryA.role;
                pair.roleB = entryB.role;
            }
            else {
                pair.roleA = entryB.role;
                pair.roleB = entryA.role;
            }
            pair.domain = *domain;
            const auto mandatoryIt = mandatoryExpanded.find(key);
            const auto excludedIt = excludedExpanded.find(key);
            if (mandatoryIt != mandatoryExpanded.end()) {
                // ① 必检对集（mandatoryPairs 展开——不可被任何过滤覆盖，
                // §7.2"结构上不存在可隐藏必检的合法策略"）。
                pair.status = ScopePairStatus::Mandatory;
                pair.ruleIndex = mandatoryIt->second;
            }
            else if (excludedIt != excludedExpanded.end()) {
                // ② 显式排除对跳过（记录 ruleOrigin＝"policy.excludedPairs[i]
                // .reason"——评估期随 appliedFilters 全量输出）。
                pair.status = ScopePairStatus::ExcludedByRule;
                pair.ruleIndex = excludedIt->second.first;
                pair.filterReason = excludedIt->second.second;
            }
            else if (adjacentSet.find(key) != adjacentSet.end()
                     && (*domain == CollisionDomain::Self
                         || *domain == CollisionDomain::Tool)
                     && m_policy.collision.excludeAdjacentLinksByDefault) {
                // ③ 默认相邻过滤（消费模型相邻事实——excludeAdjacentLinks-
                // ByDefault 开启且域为 Self/Tool；ruleOrigin=
                // "default.adjacent-links"；必检对不受此滤（已在①分支））。
                pair.status = ScopePairStatus::ExcludedByAdjacencyDefault;
                pair.ruleIndex = 0;
                pair.filterReason = "default.adjacent-links";
            }
            else if (*domain == CollisionDomain::Scene) {
                // ④ Scene 域默认不检（静态场景无相对运动——§7.1 必须检测列
                // "显式 mandatoryPairs 可启用"；未命中必检/排除的 Scene 对
                // 不入作用域）。
                continue;
            }
            else {
                // ⑤ 域默认必检对（域启用即默认必检且未被合法排除——
                // §7.1 必须检测列；无规则来源，ruleIndex 恒 0）。
                pair.status = ScopePairStatus::InScopeDefault;
                pair.ruleIndex = 0;
            }
            expanded.push_back(std::move(pair));
        }
    }
    // 规范排序（(objectA, objectB) 字节字典序——同语义场景展开结果逐字节
    // 一致；establishes findings/appliedFilters 的稳定排序基础，§6.4）。
    std::sort(expanded.begin(), expanded.end(),
              [](const ScopedCollisionPair& l, const ScopedCollisionPair& r) {
                  if (l.objectA != r.objectA) {
                      return l.objectA < r.objectA;
                  }
                  return l.objectB < r.objectB;
              });
    m_scope.pairs = std::move(expanded);

    // ---- 第三步：检测器初始化（§6.4——ProximitySetup 由策略规则生成） ----
    // R-POL-3：不取 RobWork 默认设置（不调用 ProximitySetup::get(workcell)
    // ——文件内嵌 CollisionSetup/默认过滤不进产品策略）；默认构造后显式
    // 关闭"静态对排除"（RobWork 默认过滤开关——产品过滤面唯一来源是策略
    // 规则）；useIncludeAll 保持开启（候选集＝全部几何帧对，再由规则裁剪
    // ——与"默认必检、排除例外"的策略语义同向；显式赋值以固定其值）。
    m_proximitySetup = std::make_shared<const rw::proximity::ProximitySetup>(
        buildPolicyProximitySetup(names));

    // ---- 第三步：会话身份（§6.4——f(policy, scene, names, backend)） ----
    m_sessionIdentity = detail::computeSessionIdentity(m_policy.contentIdentity,
                                                       m_scene.sceneContentIdentity,
                                                       m_nameMapIdentity, m_backend);

    // ---- 评估半区装配（POL-T07——§6.4 createSession 的评估半区延伸：
    //      设备/Frame/名称/几何固化与距离能力探测；实现见 CollisionQuery.cpp。
    //      置于三步之后：评估半区消费三步产物（作用域/策略/互斥缺省自建）。 ----
    buildEvaluationHalf(names);
}

rw::proximity::ProximitySetup
CollisionEvaluationSession::buildPolicyProximitySetup(const IPolicyNameContext& names) const
{
    // 规则集＝作用域展开产物中带规则来源的对（必检/排除/相邻默认过滤）——
    // 域默认必检对不产生规则（RobWork 层语义：include-all 候选集默认含之）。
    // 规则插入序＝展开产物规范序（确定性——同会话输入同 setup 规则序）。
    rw::proximity::ProximitySetup setup;
    setup.setUseExcludeStaticPairs(false);
    setup.setUseIncludeAll(true);
    for (const ScopedCollisionPair& pair : m_scope.pairs) {
        const bool isExclude = pair.status == ScopePairStatus::ExcludedByRule
                            || pair.status == ScopePairStatus::ExcludedByAdjacencyDefault;
        const bool isInclude = pair.status == ScopePairStatus::Mandatory;
        if (!isExclude && !isInclude) {
            continue;   // 域默认必检对——无 RobWork 层规则（语义默认已含）
        }
        // 完整名精确匹配（§6.4"规则经名称上下文以完整名精确匹配转为
        // RobWork 规则，不做模式拼接"——R-4：只转发解析结果，无任何前缀
        // 拼接/剥离）。解析失败＝场景 Frame↔对象 ID 断链（§7.5）→ 会话
        // 构建失败（POLICY-CLL-NAME-UNRESOLVED）——不猜测。
        const std::optional<std::string> nameA = names.tryRuntimeName(pair.objectA);
        if (!nameA.has_value()) {
            throw PolicyError(PolicyErrorCode::NameUnresolved,
                              "会话构建：作用域对端 A 经名称上下文不可解析（§7.5——"
                              "不猜测，ARC-04; obj=" + pair.objectA.toCanonical() + "）");
        }
        const std::optional<std::string> nameB = names.tryRuntimeName(pair.objectB);
        if (!nameB.has_value()) {
            throw PolicyError(PolicyErrorCode::NameUnresolved,
                              "会话构建：作用域对端 B 经名称上下文不可解析（§7.5——"
                              "不猜测，ARC-04; obj=" + pair.objectB.toCanonical() + "）");
        }
        // 精确名规则（非通配/正则模式——词面即完整名，regex 精确匹配语义
        // 下等价于全等；ProximitySetupRule 的 pattern 即完整名字面量）。
        if (isExclude) {
            setup.addProximitySetupRule(
                rw::proximity::ProximitySetupRule::makeExclude(*nameA, *nameB));
        }
        else {
            // 必检对显式 INCLUDE（RobWork 层 include 规则优先于排除——必检
            // "不可被过滤覆盖"在基线过滤层同样成立，纵深防御）。
            setup.addProximitySetupRule(
                rw::proximity::ProximitySetupRule::makeInclude(*nameA, *nameB));
        }
    }
    return setup;
}

}  // namespace sdurws::ird::policy
