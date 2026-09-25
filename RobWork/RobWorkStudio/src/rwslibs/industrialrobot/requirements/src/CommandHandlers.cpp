/**
 * @file   CommandHandlers.cpp
 * @brief  需求命令处理器族实现——载荷编解码、基线重建、通用槽校验、
 *         两命令钩子与 §9.1 prepare 管线公共段（就绪现场重估＋快照式
 *         逆命令）。
 *
 * 设计依据：
 *   - units/requirements.md §9.1（命令清单表＋prepare 管线图：decode→
 *     基线重建→就绪断言（Blocking 拒绝＋逐项定位诊断）→CommandPlan；
 *     "无 ConfirmableFinding 产出"；§9.5 IRequirementCommandHandler 行、
 *     §4.8 I-REQ-8 导入溯源完整性、§9.7 对象引用关系图）
 *   - modeling/src/CommandHandlers.cpp（同款管线先例——本卡 §9.1
 *     "prepare 管线同 modeling §9.3 模式"的实现面镜像；帧格式/防御性
 *     复核/inverse 组装/摘要模板同构，域值与对象路由为 REQ 侧）
 *   - 需求 ARC-01/PA-2/PA-1、NFR-MNT-04（就绪判定单一套件——O-39）、
 *     NFR-COR-01/02（确定性）、NFR-DEP-04（版本拒绝）
 *   - 任务契约 tasks/foundation/WP-14-T05.json acceptance 6
 *
 * 实现决策登记（§9.5 钩子签名只传 RequirementWorkingSet——该值模型无
 * 五对象的存储 oid 成员，故职责切分为）：
 *   - 基类（持 BaselineSnapshot 局部量——prepare 栈上，无实例状态，线
 *     程安全）：通用槽校验（Apply 非空/每 token 至多一槽/显式槽身份存
 *     在且 token 一致/根槽 allocateNew 时闭包须无根——恰一根不变量的
 *     机器面）；就绪现场重估；inverse 快照组装（需要条目原始字节）；
 *   - 钩子（工作集＋ctx 足够）：候选装配＋写入集表达＋取号。两命令的
 *     差异仅一处——import 钩子在装配后追加导入溯源完整性断言（I-REQ-8）。
 *   - Apply 恒含根槽（恰一）：根是修订闭包锚（§9.1"写入对象"列首）
 *     ——挂载增量随根字节持久化；根对象字节**恒由候选根重编码**（载
 *     荷根字节经解码进候选后，挂载增量可能覆写引用槽——载荷根字节中
 *     未分配身份的引用槽位由此获得正式 oid；显式集合替换的挂载稳定性
 *     以基线根引用表核对）。
 *
 * 线程约束：处理器仅命令线程调用（project 串行槽）——实例无成员状态，
 * 共享安全（§5.3.5"运行期只读"）。
 * 确定性：候选装配/写入序/逆载荷槽序＝载荷槽序（调用方确定）；摘要为
 * 固定中文模板＋确定性计数；无环境/时钟依赖（NFR-COR-01/02）。
 */

#include <sdurws/ird/requirements/CommandHandlers.hpp>

#include <sdurws/ird/requirements/Codec.hpp>  // RequirementCodec——同源解码/编码

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace sdurws::ird::requirements {

// =====================================================================
// 对象 token 词表辅助（实现内私有——五对象路由与校验）
// =====================================================================

namespace {

/// 五对象 token 核对（词表外＝无效载荷/非本域对象——路由失联面）。
bool isRequirementObjectTypeToken(std::string_view token) noexcept
{
    return token == kReqSetObjectType || token == kReqPointSetObjectType
        || token == kReqRegionSetObjectType || token == kReqConditionSetObjectType
        || token == kReqPlanSetObjectType;
}

// =====================================================================
// 载荷编解码（确定性——字段定序、小端长度前缀、无填充；modeling 同构）
// =====================================================================

/// 载荷 magic（"IRDRCM1"——requirements command payload v1；版本演进随
/// kRequirementCommandPayloadVersion 与 magic 尾数同步——NFR-DEP-04）。
constexpr std::uint8_t kPayloadMagic[8] = {'I', 'R', 'D', 'R', 'C', 'M', '1', '\0'};

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

void putBytes(std::vector<std::uint8_t>& out, const std::uint8_t* data, std::size_t n)
{
    putU32(out, static_cast<std::uint32_t>(n));
    out.insert(out.end(), data, data + n);
}

void putString(std::vector<std::uint8_t>& out, std::string_view s)
{
    putBytes(out, reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

/// 严格读取的游标视图（越界即失败——不产出半成品）。
struct PayloadReader {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t pos = 0;

    bool readU32(std::uint32_t* out)
    {
        if (pos + 4 > size) { return false; }
        *out = static_cast<std::uint32_t>(data[pos])
             | (static_cast<std::uint32_t>(data[pos + 1]) << 8)
             | (static_cast<std::uint32_t>(data[pos + 2]) << 16)
             | (static_cast<std::uint32_t>(data[pos + 3]) << 24);
        pos += 4;
        return true;
    }
    bool readBytes(std::vector<std::uint8_t>* out)
    {
        std::uint32_t n = 0;
        if (!readU32(&n)) { return false; }
        if (pos + n > size) { return false; }  // 截断/越界——整体失败
        out->assign(data + pos, data + pos + n);
        pos += n;
        return true;
    }
    bool readString(std::string* out)
    {
        std::vector<std::uint8_t> raw;
        if (!readBytes(&raw)) { return false; }
        out->assign(raw.begin(), raw.end());
        return true;
    }
    bool atEnd() const { return pos == size; }
};

// =====================================================================
// 基线快照与通用校验（基类 prepare 栈上局部量——实例零状态）
// =====================================================================

/// 基线闭包的一个需求对象条目（oid/token/原始字节——inverse 快照素材：
/// 逆载荷＝受影响对象前一 (oid,cv) canonical 字节集，D-MDL-9 同源口径）。
struct BaselineEntry {
    core::ObjectId oid;
    std::string token;
    std::vector<std::uint8_t> bytes;
};

/// 基线重建的完整产物（工作集视图＋原始字节表）。
struct BaselineSnapshot {
    RequirementWorkingSet ws;
    std::vector<BaselineEntry> entries;  // 闭包内全部 requirements 对象（含根）
};

/// 基线中查找对象（oid→条目；不存在＝nullptr——调用方决定语义）。
const BaselineEntry* findBaselineEntry(const BaselineSnapshot& snap,
                                       const core::ObjectId& oid)
{
    for (const BaselineEntry& e : snap.entries) {
        if (e.oid == oid) { return &e; }
    }
    return nullptr;
}

/// 基线条目字节（逆放/校验的事实面）。
const std::vector<std::uint8_t>* baselineBytes(const BaselineSnapshot& snap,
                                               const core::ObjectId& oid)
{
    const BaselineEntry* e = findBaselineEntry(snap, oid);
    return e != nullptr ? &e->bytes : nullptr;
}

}  // namespace

// =====================================================================
// 载荷编解码（公开契约——见头文件注）
// =====================================================================

std::vector<std::uint8_t> encodeRequirementCommandPayload(
    const RequirementCommandPayload& payload)
{
    std::vector<std::uint8_t> out;
    out.reserve(64 + payload.objects.size() * 32);
    out.insert(out.end(), kPayloadMagic, kPayloadMagic + sizeof(kPayloadMagic));
    putU32(out, kRequirementCommandPayloadVersion);
    putU32(out, payload.mode == RequirementCommandPayload::Mode::Restore ? 1u : 0u);
    putU32(out, static_cast<std::uint32_t>(payload.objects.size()));
    for (const RequirementPayloadSlot& slot : payload.objects) {
        out.push_back(slot.allocateNew ? 1u : 0u);
        const std::string oid = slot.objectId.toCanonical();  // 全零→保留文本
        putString(out, oid);
        putString(out, slot.objectTypeToken);
        putBytes(out, slot.objectBytes.data(), slot.objectBytes.size());
    }
    return out;
}

std::optional<RequirementCommandPayload> tryDecodeRequirementCommandPayload(
    const std::vector<std::uint8_t>& bytes)
{
    // 框架完整性：magic/长度逐字节核对（任一失败＝nullopt，不猜测）。
    if (bytes.size() < sizeof(kPayloadMagic) + 12) { return std::nullopt; }
    for (std::size_t i = 0; i < sizeof(kPayloadMagic); ++i) {
        if (bytes[i] != kPayloadMagic[i]) { return std::nullopt; }
    }
    PayloadReader reader{bytes.data(), bytes.size(), sizeof(kPayloadMagic)};
    RequirementCommandPayload payload;
    std::uint32_t version = 0;
    std::uint32_t mode = 0;
    std::uint32_t count = 0;
    if (!reader.readU32(&version) || !reader.readU32(&mode)
        || !reader.readU32(&count)) {
        return std::nullopt;
    }
    // 版本不受理（NFR-DEP-04——旧版本拒绝；受理集合＝{当前版本}）。
    if (version != kRequirementCommandPayloadVersion) { return std::nullopt; }
    if (mode > 1u) { return std::nullopt; }
    payload.mode =
        (mode == 1u) ? RequirementCommandPayload::Mode::Restore
                     : RequirementCommandPayload::Mode::Apply;
    payload.objects.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        RequirementPayloadSlot slot;
        if (reader.pos >= reader.size) { return std::nullopt; }
        const std::uint32_t allocate = reader.data[reader.pos++];
        if (allocate > 1u) { return std::nullopt; }
        slot.allocateNew = (allocate == 1u);
        std::string oidText;
        if (!reader.readString(&oidText)) { return std::nullopt; }
        // 身份文本严格解析（tag/长度/字符集——含全零保留文本）。
        auto oid = core::ObjectId::tryFromCanonical(oidText);
        if (!oid.has_value()) { return std::nullopt; }
        slot.objectId = *oid;
        if (!reader.readString(&slot.objectTypeToken)) { return std::nullopt; }
        // token 词表核对（五对象之外＝无效载荷——路由失联面）。
        if (!isRequirementObjectTypeToken(slot.objectTypeToken)) { return std::nullopt; }
        if (!reader.readBytes(&slot.objectBytes)) { return std::nullopt; }
        payload.objects.push_back(std::move(slot));
    }
    if (!reader.atEnd()) { return std::nullopt; }  // 尾随字节＝破损
    return payload;
}

// =====================================================================
// prepare 公共管线内部段（实现内私有——file-local）
// =====================================================================

namespace {

/**
 * @brief 基线工作集重建（§9.1"基线重建"段：expectedRevision 闭包→同源
 *        解码）＋防御性复核。
 *
 * 逐闭包对象：五对象 token 路由到 RequirementCodec 同源解码（与编辑器
 * loadBaseline/就绪校验同一解码门——语义单源）；非本域 token（元数据/
 * 策略/modeling 对象等）跳过。字节取数走 ctx.query().object() 强语义。
 *
 * @throws std::invalid_argument 防御性复核失败：envelope.expectedRevision
 *         与 baseSnapshot.id 不一致（S2 已拦截过期基线——到达即调用方
 *         契约违约，fail-fast）
 * @throws std::logic_error 基线对象字节解码失败/闭包含重复根或重复集合
 *         （存储字节不可解码或闭包违约＝数据损坏/实现缺陷——防线纵深
 *         断言，不产出半成品基线）
 */
BaselineSnapshot rebuildRequirementBaseline(project::HandlerContext& ctx,
                                            const project::CommandEnvelope& envelope,
                                            const project::RevisionView& baseSnapshot)
{
    // ---- 防御性复核（expectedRevision==基线——S2 已拦截，此处为纵深）----
    if (envelope.expectedRevision.has_value()
        && !(*envelope.expectedRevision == baseSnapshot.id)) {
        throw std::invalid_argument(
            "requirements: prepare 基线防御性复核失败——expectedRevision 与 "
            "baseSnapshot 不一致（stale-revision 应由 project S2 拦截；直接"
            "调用 prepare 须遵守 §5.3.2 契约）");
    }

    BaselineSnapshot snap;
    RequirementCodec codec;
    bool rootSeen = false;  // 恰一根不变量的重建面（重复根＝存储违约）
    for (const project::ObjectRef& ref : baseSnapshot.objectRefs) {
        if (!isRequirementObjectTypeToken(ref.objectTypeToken)) {
            continue;  // 元数据/策略/modeling 对象——非本域，跳过
        }

        // 强语义取数＋同源解码；解码失败＝数据损坏面，fail-fast。
        std::vector<std::uint8_t> bytes =
            ctx.query().object(ref.objectId, ref.contentVersion);
        auto decoded = codec.decode(bytes, kCurrentRequirementFormatVersion);
        if (!decoded.ok()) {
            throw std::logic_error("requirements: 基线对象解码失败（数据损坏面）: "
                                   + ref.objectId.toCanonical() + " — "
                                   + decoded.error().detail);
        }
        RequirementObjectVariant value = decoded.get();  // 值拷贝（fresh 产物）

        BaselineEntry entry;
        entry.oid = ref.objectId;
        entry.token = ref.objectTypeToken;
        entry.bytes = std::move(bytes);
        snap.entries.push_back(std::move(entry));

        // token 路由进工作集视图（根/每集合至多一——重复＝存储违约）。
        std::visit(
            [&](auto& typed) {
                using T = std::decay_t<decltype(typed)>;
                if constexpr (std::is_same_v<T, RequirementSet>) {
                    if (rootSeen) {
                        throw std::logic_error(
                            "requirements: 基线闭包含多个根对象（存储违约）");
                    }
                    rootSeen = true;
                    snap.ws.root = typed;
                } else if constexpr (std::is_same_v<T, PointSet>) {
                    if (!snap.ws.points.entries.empty()) {
                        throw std::logic_error("requirements: 基线闭包含多个点集（存储违约）");
                    }
                    snap.ws.points = typed;
                } else if constexpr (std::is_same_v<T, RegionSet>) {
                    if (!snap.ws.regions.entries.empty()) {
                        throw std::logic_error("requirements: 基线闭包含多个区域集（存储违约）");
                    }
                    snap.ws.regions = typed;
                } else if constexpr (std::is_same_v<T, ConditionSet>) {
                    if (!snap.ws.conditions.entries.empty()) {
                        throw std::logic_error("requirements: 基线闭包含多个工况集（存储违约）");
                    }
                    snap.ws.conditions = typed;
                } else {
                    static_assert(std::is_same_v<T, PlanSet>, "五变体全覆盖");
                    if (!snap.ws.plans.entries.empty()) {
                        throw std::logic_error("requirements: 基线闭包含多个计划集（存储违约）");
                    }
                    snap.ws.plans = typed;
                }
            },
            value);
    }
    return snap;
}

/**
 * @brief Apply 载荷的通用槽校验（基类职责——需要基线条目面，钩子签名
 *        只传工作集；实现决策见文件头"实现决策登记"）。
 *
 * 校验面（任一违约＝无效载荷）：
 *   ① objects 非空（纯元数据命令本单元无）；
 *   ② 恰一个根槽（req-set——修订闭包锚，§9.1"写入对象"列首）＋每
 *      token 至多一槽；
 *   ③ 显式槽：身份有效且存在于基线闭包、token 一致；
 *   ④ 根槽 allocateNew：基线闭包须无 req-set 对象（恰一根——重复建根
 *      =身份冲突违约）。
 */
bool applySlotsValid(const RequirementCommandPayload& payload,
                     const BaselineSnapshot& baseline)
{
    if (payload.objects.empty()) {
        return false;  // ①
    }
    std::size_t rootSlots = 0;
    for (std::size_t i = 0; i < payload.objects.size(); ++i) {
        const RequirementPayloadSlot& slot = payload.objects[i];
        if (slot.objectTypeToken == kReqSetObjectType) {
            ++rootSlots;
            if (slot.allocateNew) {
                // ④：闭包已有根则不得再申请建根（恰一根——§9.7）。
                for (const BaselineEntry& e : baseline.entries) {
                    if (e.token == kReqSetObjectType) { return false; }
                }
            }
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (payload.objects[j].objectTypeToken == slot.objectTypeToken) {
                return false;  // ② 每 token 至多一槽
            }
        }
        // ③ 显式槽身份存在性＋token 一致（基线条目面核对）。
        if (!slot.allocateNew) {
            if (!slot.objectId.isValid()) { return false; }
            const BaselineEntry* entry = findBaselineEntry(baseline, slot.objectId);
            if (entry == nullptr || entry->token != slot.objectTypeToken) {
                return false;
            }
        }
    }
    return rootSlots == 1;  // ② 恰一根槽
}

/**
 * @brief 候选闭包后像装配（就绪 R1/R8 浅校验的 CheckContext 数据面）：
 *        基线 objectRefs 按本次计划写入增改（同 oid 替换、新 oid 追加）。
 *
 * 注：ContentVersion/digest256 由 project 在 S6 计算登记——prepare 期的
 * 后像只保证 oid＋objectTypeToken 两字段（就绪浅校验 §8.1 只消费这两
 * 字段），cv/digest 置保留零值不参与判定。
 */
CheckContext buildPostImageClosure(const project::RevisionView& baseSnapshot,
                                   const project::CommandPlan& plan)
{
    CheckContext ctx;
    ctx.closureRefs.reserve(baseSnapshot.objectRefs.size() + plan.objectWrites.size());
    for (const project::ObjectRef& ref : baseSnapshot.objectRefs) {
        ctx.closureRefs.push_back(ref);
    }
    for (const project::ObjectWrite& write : plan.objectWrites) {
        bool replaced = false;
        if (write.objectId.has_value()) {
            for (project::ObjectRef& ref : ctx.closureRefs) {
                if (ref.objectId == *write.objectId) {
                    ref.objectTypeToken = write.objectTypeToken;  // 同 oid 替换
                    replaced = true;
                }
            }
        }
        if (!replaced) {
            project::ObjectRef added;
            if (write.objectId.has_value()) {
                added.objectId = *write.objectId;
            }
            added.objectTypeToken = write.objectTypeToken;
            ctx.closureRefs.push_back(std::move(added));
        }
    }
    return ctx;
}

/// 受影响对象的前一版本逆槽集（快照式逆命令——§9.1 inverse 列；基线中
/// 不存在的对象＝本次新增——无前版，跳过）。
std::vector<RequirementPayloadSlot> buildInverseSlots(
    const BaselineSnapshot& baseline, const std::vector<core::ObjectId>& affected)
{
    std::vector<RequirementPayloadSlot> slots;
    slots.reserve(affected.size());
    for (const core::ObjectId& oid : affected) {
        const std::vector<std::uint8_t>* bytes = baselineBytes(baseline, oid);
        const BaselineEntry* entry = findBaselineEntry(baseline, oid);
        if (bytes == nullptr || entry == nullptr) {
            continue;  // 首次新增对象——无前版可逆
        }
        RequirementPayloadSlot slot;
        slot.allocateNew = false;
        slot.objectId = entry->oid;
        slot.objectTypeToken = entry->token;
        slot.objectBytes = *bytes;  // 前一版本 canonical 字节（原样快照）
        slots.push_back(std::move(slot));
    }
    return slots;
}

/// 命令 token→中文摘要标题（§9.1——人读面的固定词表）。
std::string_view commandTitle(std::string_view token)
{
    if (token == kCmdApplyRequirementSet) { return "应用需求集"; }
    if (token == kCmdApplyRequirementImport) { return "应用需求导入批次"; }
    return token;  // 词表外 token（防御——原样承载不猜测）
}

/// 中文命令摘要（对象/就绪留痕/可逆性——确定性模板；随修订持久化）。
std::string buildSummary(std::string_view commandToken,
                         std::size_t writeCount,
                         std::size_t addedCount,
                         std::size_t replacedCount,
                         std::size_t warningCount)
{
    // 摘要三段（确定性序）：对象（写入计数＋新增/替换拆分）→就绪警告
    // （Warning 随计划留痕计数——Blocking 已拒绝不会到达）→可逆性。
    std::string summary(commandTitle(commandToken));
    summary += "：写入需求对象 " + std::to_string(writeCount) + " 项（新增 "
             + std::to_string(addedCount) + "、替换 " + std::to_string(replacedCount) + "）";
    if (warningCount > 0) {
        summary += "；就绪警告 " + std::to_string(warningCount) + " 项（不阻断应用）";
    }
    summary += "；撤销＝逆命令（受影响对象前版字节）";
    return summary;
}

/// 导入溯源完整性核对（apply-requirement-import 附加断言——I-REQ-8
/// 字面：importProvenance 在场＋sourceDigest 非全零＋recordNumber≥1；
/// 载荷面＝任务点/区域条目（§4.3/§4.4 字段表——工况条目无溯源字段，
/// §4.5，不适用）。
bool importProvenanceComplete(const RequirementWorkingSet& candidate)
{
    const auto digestNonZero = [](const core::Digest256& d) {
        for (const std::uint8_t b : d) {
            if (b != 0) { return true; }
        }
        return false;
    };
    for (const TaskPoint& p : candidate.points.entries) {
        if (!p.importProvenance.has_value()
            || !digestNonZero(p.importProvenance->sourceDigest)
            || p.importProvenance->recordNumber < 1) {
            return false;
        }
    }
    for (const WorkRegion& r : candidate.regions.entries) {
        if (!r.importProvenance.has_value()
            || !digestNonZero(r.importProvenance->sourceDigest)
            || r.importProvenance->recordNumber < 1) {
            return false;
        }
    }
    return true;
}

/// 载荷槽解码为对象值（RequirementCodec 同源解码——解码门强制 I-REQ 不
/// 变量；失败＝nullopt，调用方转无效载荷）。
std::optional<RequirementObjectVariant> decodeSlotObject(
    const RequirementCodec& codec, const RequirementPayloadSlot& slot)
{
    auto decoded = codec.decode(slot.objectBytes, kCurrentRequirementFormatVersion);
    if (!decoded.ok()) {
        return std::nullopt;
    }
    return decoded.get();
}

/// 集合槽的路由面（slot token → 根引用表槽与候选工作集成员的联动写）。
/// 返回 false＝token/值类型不一致（无效载荷）。
bool routeSetObject(RequirementWorkingSet& ws, const std::string& token,
                    const RequirementObjectVariant& value)
{
    if (token == kReqPointSetObjectType && std::holds_alternative<PointSet>(value)) {
        ws.points = std::get<PointSet>(value);
        return true;
    }
    if (token == kReqRegionSetObjectType && std::holds_alternative<RegionSet>(value)) {
        ws.regions = std::get<RegionSet>(value);
        return true;
    }
    if (token == kReqConditionSetObjectType
        && std::holds_alternative<ConditionSet>(value)) {
        ws.conditions = std::get<ConditionSet>(value);
        return true;
    }
    if (token == kReqPlanSetObjectType && std::holds_alternative<PlanSet>(value)) {
        ws.plans = std::get<PlanSet>(value);
        return true;
    }
    return false;  // token/值类型不一致（根对象字节冒充集合槽等）
}

/// 集合 token → 基线根引用表槽（挂载稳定性核对面；非集合 token＝nullptr）。
const std::optional<core::ObjectId>* baselineMountOf(const RequirementWorkingSet& ws,
                                                     const std::string& token)
{
    if (token == kReqPointSetObjectType) { return &ws.root.pointSetRef; }
    if (token == kReqRegionSetObjectType) { return &ws.root.regionSetRef; }
    if (token == kReqConditionSetObjectType) { return &ws.root.conditionSetRef; }
    if (token == kReqPlanSetObjectType) { return &ws.root.planSetRef; }
    return nullptr;
}

/// 集合 token → 候选根引用表槽（挂载增量面；同上）。
std::optional<core::ObjectId>* candidateMountOf(RequirementWorkingSet& ws,
                                                const std::string& token)
{
    if (token == kReqPointSetObjectType) { return &ws.root.pointSetRef; }
    if (token == kReqRegionSetObjectType) { return &ws.root.regionSetRef; }
    if (token == kReqConditionSetObjectType) { return &ws.root.conditionSetRef; }
    if (token == kReqPlanSetObjectType) { return &ws.root.planSetRef; }
    return nullptr;
}

}  // namespace

// =====================================================================
// 基类：prepare 公共段（§9.1 管线图逐步落位——执行序见头文件类注）
// =====================================================================

IRequirementCommandHandler::IRequirementCommandHandler(std::string commandType) noexcept
    : m_commandType(std::move(commandType))
{}

std::string IRequirementCommandHandler::commandType() const
{
    return m_commandType;  // 构造定值（O-35 词表——无点形态）
}

std::uint32_t IRequirementCommandHandler::currentPayloadVersion() const
{
    return kRequirementCommandPayloadVersion;  // 受理集合＝{当前版本}
}

project::PrepareOutcome IRequirementCommandHandler::prepare(
    project::HandlerContext& ctx,
    const project::CommandEnvelope& envelope,
    const project::RevisionView& baseSnapshot,
    project::CommandPlan& out,
    std::vector<core::DiagnosticRecord>& diags)
{
    // ---- ① 载荷框架解码（§9.1 decode 失败→RejectedInvalidInput）----
    std::optional<RequirementCommandPayload> payload =
        tryDecodeRequirementCommandPayload(envelope.payloadCanonical);
    if (!payload.has_value()) {
        return project::PrepareOutcome::RejectedInvalidInput;  // invalid-payload
    }
    // 版本受理双检（S1 已拦截——防线纵深；受理集合＝{当前版本}）。
    if (envelope.payloadFormatVersion != kRequirementCommandPayloadVersion) {
        return project::PrepareOutcome::RejectedInvalidInput;
    }

    // ---- ② 基线重建＋防御性复核（失败 fail-fast——见 rebuild 注）----
    const RequirementCodec codec;
    BaselineSnapshot baseline = rebuildRequirementBaseline(ctx, envelope, baseSnapshot);

    // ---- ②.5 Apply 通用槽校验（需要基线条目面——基类职责，见文件头
    //      实现决策登记；Restore 槽校验在钩子内同面执行）----
    if (payload->mode == RequirementCommandPayload::Mode::Apply
        && !applySlotsValid(*payload, baseline)) {
        return project::PrepareOutcome::RejectedInvalidInput;
    }

    // ---- ③ 子类钩子：差值解码＋候选装配＋写入集表达（§9.5 原文签名）----
    RequirementDecodeOutcome dec = decodeAndPlan(ctx, *payload, baseline.ws, out);
    if (dec.outcome != project::PrepareOutcome::Planned) {
        out.objectWrites.clear();
        return dec.outcome;  // RejectedInvalidInput（域口径——槽形状/溯源）
    }

    // ---- ④ 就绪 R0~R9 现场重估（§9.1 断言列——防基线漂移；与编辑器
    //      预检/评估组装同一断言套件，NFR-MNT-04/O-39）----
    const CheckContext postImage = buildPostImageClosure(baseSnapshot, out);
    const RequirementReadinessChecker readinessChecker;
    const RequirementReadinessReport readiness =
        readinessChecker.check(dec.candidate, postImage);
    std::size_t warningCount = 0;
    for (const DomainReadinessItem& item : readiness.items) {
        if (item.level == ReadinessFindingLevel::Warning) { ++warningCount; }
    }
    if (readiness.hasBlocking()) {
        // Blocking 拒绝＋逐项定位诊断（§9.1 原文）＋一条汇总诊断
        // （REQ-READY-INPUT-INCOMPLETE——invalid-count/invalid-items 键面，
        // §9.6 T05 行"ReadinessSummary.valid=false 投影面"的人读承载）。
        // 拒绝态计划不可消费（project 不消费）——清空（ARC-01 零修订）。
        out.objectWrites.clear();
        for (const DomainReadinessItem& item : readiness.items) {
            diags.push_back(item.diag);
        }
        std::string itemList;
        for (const std::string& id : readiness.invalidMustItems) {
            if (!itemList.empty()) { itemList += ";"; }
            itemList += id;
        }
        diags.push_back(core::DiagnosticRecord::make(
            std::string(kReqReadyInputIncomplete), std::nullopt, std::nullopt,
            std::nullopt,
            "invalid-count=" + std::to_string(readiness.invalidMustItems.size())
                + "; invalid-items=" + (itemList.empty() ? "-" : itemList),
            "候选需求集就绪重估未通过（R0~R9 存在 Blocking——逐项定位见前序"
            "诊断；基线漂移防护：以提交时刻候选整体重估）",
            "修正所列非法条目后重新提交"));
        return project::PrepareOutcome::RejectedHardAssert;
    }

    // ---- ⑤ 计划最终化（S6 事务归 project）----
    out.requiresDualCompile = false;  // 需求对象不进 WorkCell 描述（§9.1 表无双编译列）
    out.confirmableFindings.clear();  // 恒空——本单元无策略校验放行场景（SA-15 不私设）

    // inverse：受影响对象前一 (oid,cv) canonical 字节集（快照式逆命令）；
    // 全部受影响对象均无前版（首次应用）＝不可逆声明 nullopt（project
    // §6.9 空历史语义）。
    std::vector<RequirementPayloadSlot> inverseSlots =
        buildInverseSlots(baseline, dec.affectedOids);
    if (!inverseSlots.empty()) {
        RequirementCommandPayload inverse;
        inverse.mode = RequirementCommandPayload::Mode::Restore;
        inverse.objects = std::move(inverseSlots);
        out.inverseCommandType = m_commandType;
        out.inversePayloadCanonical = encodeRequirementCommandPayload(inverse);
    }

    // 新增/替换拆分（摘要对象面——affectedOids 与基线的交集为替换）。
    std::size_t replaced = 0;
    for (const core::ObjectId& oid : dec.affectedOids) {
        if (findBaselineEntry(baseline, oid) != nullptr) { ++replaced; }
    }
    const std::size_t added = dec.affectedOids.size() - replaced;

    // 中文命令摘要（对象/就绪警告留痕/可逆性——随修订持久化）。
    out.summary = buildSummary(m_commandType, out.objectWrites.size(), added,
                               replaced, warningCount);

    // 仅 Warning（可应用级）：随 diags 留痕、计划照常产出（§8.1 级别
    // 语义；modeling 物性缺失预告同款先例）。
    for (const DomainReadinessItem& item : readiness.items) {
        if (item.level == ReadinessFindingLevel::Warning) {
            diags.push_back(item.diag);
        }
    }
    return project::PrepareOutcome::Planned;
}

// =====================================================================
// 两命令钩子（§9.1 命令清单表逐行——槽形状语义见头文件类注）
// =====================================================================

namespace {

/**
 * @brief Apply 模式共用装配体（两命令钩子的共用体——差异只在 import 的
 *        溯源断言；工作集＋ctx 足够，见文件头"实现决策登记"）。
 *
 * 装配规则（§9.7 根引用表/挂载语义）：
 *   - 根槽（恰一——基类已校验）：allocateNew→取号（基类已校验闭包无
 *     根）；显式→用槽身份。候选根＝载荷根字节解码值；
 *   - 集合槽 allocateNew：基线根引用表该槽须未挂载（引用稳定性——重
 *     挂载走根字节）→取号→候选根引用槽置新 oid→根字节重编码写入；
 *   - 集合槽显式：基线根引用表该槽须恰挂载该 oid（引用稳定——挂载关
 *     系变更走根对象字节，不在集合槽侧改挂）→集合字节原样写入；
 *   - 根对象写入＝候选根 canonical 重编码（根槽在场恒写根——挂载增量
 *     与根头编辑统一以候选为准；位级保真的逆放走 Restore 路径不经此）。
 */
RequirementDecodeOutcome decodeApplyCommon(project::HandlerContext& ctx,
                                           const RequirementCommandPayload& payload,
                                           const RequirementWorkingSet& baseline,
                                           project::CommandPlan& out,
                                           const RequirementCodec& codec)
{
    RequirementDecodeOutcome result;
    result.candidate = baseline;  // 增量装配底——未涉及集合保持基线态

    // ---- 第一遍：根槽先行（候选根＝载荷根字节解码值；挂载增量写在
    //      候选根上，与载荷槽序无关——两遍处理防根值覆盖挂载增量）----
    std::optional<core::ObjectId> rootOid;  // 根写入身份（显式或取号）
    for (const RequirementPayloadSlot& slot : payload.objects) {
        if (slot.objectTypeToken != kReqSetObjectType) { continue; }
        auto decoded = decodeSlotObject(codec, slot);
        if (!decoded.has_value()
            || !std::holds_alternative<RequirementSet>(*decoded)) {
            result.outcome = project::PrepareOutcome::RejectedInvalidInput;
            return result;
        }
        result.candidate.root = std::get<RequirementSet>(*decoded);
        rootOid = slot.allocateNew ? ctx.objectId()  // PA-1 取号（闭包无根已由基类校验）
                                   : slot.objectId;
    }
    if (!rootOid.has_value()) {
        // 根槽缺失＝载荷违约（applySlotsValid 已拦——防御面）。
        result.outcome = project::PrepareOutcome::RejectedInvalidInput;
        return result;
    }

    // ---- 第二遍：集合槽（解码＋挂载核对＋候选路由＋写入）----
    for (const RequirementPayloadSlot& slot : payload.objects) {
        if (slot.objectTypeToken == kReqSetObjectType) { continue; }

        auto decoded = decodeSlotObject(codec, slot);
        if (!decoded.has_value()) {
            result.outcome = project::PrepareOutcome::RejectedInvalidInput;
            return result;
        }
        // 挂载稳定性（基线根引用表——显式替换须恰挂载该 oid；allocateNew
        // 须未挂载——重挂载走根字节，§9.7"引用/条目移除"语义）。
        const std::optional<core::ObjectId>* mount =
            baselineMountOf(baseline, slot.objectTypeToken);
        std::optional<core::ObjectId>* candidateMount =
            candidateMountOf(result.candidate, slot.objectTypeToken);
        if (mount == nullptr || candidateMount == nullptr) {
            result.outcome = project::PrepareOutcome::RejectedInvalidInput;
            return result;
        }
        if (slot.allocateNew) {
            if (mount->has_value()) {
                // 该集合已在基线挂载——新对象须经根字节重挂载（不在集合
                // 槽侧换挂）。
                result.outcome = project::PrepareOutcome::RejectedInvalidInput;
                return result;
            }
            // 首挂载：取号＋候选根引用槽置新 oid（根重编码后持久化）。
            const core::ObjectId newOid = ctx.objectId();  // PA-1
            *candidateMount = newOid;
            project::ObjectWrite write;
            write.objectId = newOid;
            write.objectTypeToken = slot.objectTypeToken;
            write.payloadCanonical = slot.objectBytes;
            out.objectWrites.push_back(std::move(write));
            result.affectedOids.push_back(newOid);
        } else {
            if (!mount->has_value() || !(*mount == slot.objectId)) {
                // 挂载失配：基线该槽未挂载或挂载的不是本槽身份（字节替换
                // 只对已挂载对象成立）。
                result.outcome = project::PrepareOutcome::RejectedInvalidInput;
                return result;
            }
            project::ObjectWrite write;
            write.objectId = slot.objectId;
            write.objectTypeToken = slot.objectTypeToken;
            write.payloadCanonical = slot.objectBytes;
            out.objectWrites.push_back(std::move(write));
            result.affectedOids.push_back(slot.objectId);
        }
        // 候选路由（token/值类型一致性违约＝无效载荷）。
        if (!routeSetObject(result.candidate, slot.objectTypeToken, *decoded)) {
            result.outcome = project::PrepareOutcome::RejectedInvalidInput;
            return result;
        }
    }

    // ---- 根对象写入（恒写——根槽恰一；候选根重编码承载挂载增量）----
    auto encodedRoot = codec.encode(RequirementObjectVariant(result.candidate.root),
                                    kCurrentRequirementFormatVersion);
    if (!encodedRoot.ok()) {
        // 候选根重编码失败＝实现缺陷（解码门已过、根值无新增违例面）。
        throw std::logic_error("requirements: 候选根重编码失败（实现缺陷）: "
                               + encodedRoot.error().detail);
    }
    project::ObjectWrite rootWrite;
    rootWrite.objectId = *rootOid;
    rootWrite.objectTypeToken = std::string(kReqSetObjectType);
    rootWrite.payloadCanonical = encodedRoot.get();
    out.objectWrites.push_back(std::move(rootWrite));
    result.affectedOids.push_back(*rootOid);

    result.outcome = project::PrepareOutcome::Planned;
    return result;
}

/**
 * @brief Restore 模式共用装配体（快照逆放——§6.9 同源）：全部槽显式身
 *        份且存在于基线（基类已校验存在性；此处复核模式面）；写入＝载
 *        荷携带的前一版本 canonical 字节原样直写（位级保真，不重编码）。
 */
RequirementDecodeOutcome decodeRestoreCommon(const RequirementCommandPayload& payload,
                                             const RequirementWorkingSet& baseline,
                                             project::CommandPlan& out,
                                             const RequirementCodec& codec)
{
    RequirementDecodeOutcome result;
    result.candidate = baseline;  // 逆放底——未涉及对象保持基线态
    if (payload.objects.empty()) {
        result.outcome = project::PrepareOutcome::RejectedInvalidInput;
        return result;
    }
    for (const RequirementPayloadSlot& slot : payload.objects) {
        // 逆放槽契约：显式身份（allocateNew 在逆放语义无意义）。
        if (slot.allocateNew || !slot.objectId.isValid()) {
            result.outcome = project::PrepareOutcome::RejectedInvalidInput;
            return result;
        }
        auto decoded = decodeSlotObject(codec, slot);
        if (!decoded.has_value()) {
            result.outcome = project::PrepareOutcome::RejectedInvalidInput;
            return result;
        }
        // 候选路由（根/集合值就地替换；token/值类型违约＝无效载荷）。
        if (slot.objectTypeToken == kReqSetObjectType) {
            if (!std::holds_alternative<RequirementSet>(*decoded)) {
                result.outcome = project::PrepareOutcome::RejectedInvalidInput;
                return result;
            }
            result.candidate.root = std::get<RequirementSet>(*decoded);
        } else if (!routeSetObject(result.candidate, slot.objectTypeToken, *decoded)) {
            result.outcome = project::PrepareOutcome::RejectedInvalidInput;
            return result;
        }
        // 写入＝前一版本字节原样（(oid,cv) 快照——位级保真）。
        project::ObjectWrite write;
        write.objectId = slot.objectId;
        write.objectTypeToken = slot.objectTypeToken;
        write.payloadCanonical = slot.objectBytes;
        out.objectWrites.push_back(std::move(write));
        result.affectedOids.push_back(slot.objectId);
    }
    result.outcome = project::PrepareOutcome::Planned;
    return result;
}

}  // namespace

RequirementDecodeOutcome ApplyRequirementSetHandler::decodeAndPlan(
    project::HandlerContext& ctx,
    const RequirementCommandPayload& payload,
    const RequirementWorkingSet& baseline,
    project::CommandPlan& out)
{
    const RequirementCodec codec;
    if (payload.mode == RequirementCommandPayload::Mode::Restore) {
        return decodeRestoreCommon(payload, baseline, out, codec);
    }
    return decodeApplyCommon(ctx, payload, baseline, out, codec);
}

RequirementDecodeOutcome ApplyRequirementImportHandler::decodeAndPlan(
    project::HandlerContext& ctx,
    const RequirementCommandPayload& payload,
    const RequirementWorkingSet& baseline,
    project::CommandPlan& out)
{
    const RequirementCodec codec;
    const RequirementDecodeOutcome result =
        payload.mode == RequirementCommandPayload::Mode::Restore
            ? decodeRestoreCommon(payload, baseline, out, codec)
            : decodeApplyCommon(ctx, payload, baseline, out, codec);
    if (result.outcome != project::PrepareOutcome::Planned) {
        return result;
    }
    // 导入溯源完整性断言（§9.1 表行 2"同上＋导入溯源完整性"；I-REQ-8
    // 字面——摘要＋行号必登记；候选工作集整体核对＝导入批次的原子面）。
    if (!importProvenanceComplete(result.candidate)) {
        return RequirementDecodeOutcome{
            project::PrepareOutcome::RejectedInvalidInput, RequirementWorkingSet{}, {}};
    }
    return result;
}

void registerRequirementCommandHandlers(project::HandlerRegistry& registry)
{
    // L5 装配期一次性注册（project §6.5）；处理器无状态——族内共享安全。
    registry.registerHandler(std::make_unique<ApplyRequirementSetHandler>());
    registry.registerHandler(std::make_unique<ApplyRequirementImportHandler>());
}

}  // namespace sdurws::ird::requirements
