/**
 * @file   NameMap.cpp
 * @brief  RuntimeNameMap 实现——§7.2 生成规则（合法化/保留字/消歧）、双射
 *         索引、S8 交叉校验与⑥端口转发（规则出处逐段标注）。
 *
 * 设计依据：
 *   - units/runtime.md §7.1～§7.5（范围表/生成规则表/接口属性表/AT-18/迁移表）、
 *     §6.3（BaseMount 名经名称映射登记）、§15.4（RT-T05 实现层澄清登记）
 *   - 需求 ARC-04/MDL-14/CON-06；R-4 红线（本文件＝前缀拼装唯一合法位置，
 *     登记例外清单的唯一实现文件——RT-T13 提交 WP-01-T01 门禁）
 *   - 任务契约 tasks/foundation/RT-T05.json
 *
 * ★ R-4 红线落点（NFR-MNT-07/SA-05）：全产品唯一的"作用域前缀＋'.'＋局部名"
 *   拼装点＝本文件 detail::joinScopeLocal()；任何其他文件出现该动作即 R-4
 *   违例（NameMapTest 的源码扫描用例钉住——kPrefixJoinHelper 名字唯一性）。
 *
 * 线程安全：本文件全部函数为纯函数或只读查询（无共享可变状态），构建产物
 * 不可变——并发只读安全（§7.3 接口属性表）。
 * 确定性：排序键（scope 枚举序/localName 字节序/ObjectId 字节序＝规范文本序）、
 * 消歧分配序、警告收集序全部无环境依赖（NFR-COR-02；RT-NM-1/RT-ID-1 钉住）。
 */

#include <sdurws/ird/runtime/NameMap.hpp>

#include <algorithm>
#include <limits>
#include <utility>

#include <sdurws/ird/runtime/Codec.hpp>  // IRDNAME 内容身份（§7.6——同单元公共头）

namespace sdurws::ird::runtime {

namespace {

// =====================================================================
// §7.2 规则表常量（每项出处见行尾注释；魔法数字一律禁用——命名常量＋出处）。
// =====================================================================

/// 合法字符集判定（§7.2"合法字符集"行：[A-Za-z0-9_.-]，不含 '/'、空格、
/// Unicode——RobWork Frame 名安全集合；大小写保留）。
bool isLegalNameChar(char c) noexcept
{
    const unsigned char u = static_cast<unsigned char>(c);
    return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9')
        || c == '_' || c == '.' || c == '-';
}

/// 非法字符的替身（§7.2"合法化"行：逐个替换 '_'）。
constexpr char kIllegalCharReplacement = '_';

/// 前导数字的前缀（§7.2"合法化"行：前导数字加前缀 "n_"）。
constexpr const char* kLeadingDigitPrefix = "n_";

/// 空名的合法化结果（§7.2"合法化"行：空名→"unnamed"＋警告）。
constexpr const char* kEmptyNameReplacement = "unnamed";

/// 保留字最小集（§7.2"保留字"行：世界帧名 WORLD——localName 命中即直接
/// 消歧加后缀；大小写敏感——"world" 不保留，精确等值口径）。
constexpr const char* kReservedWorld = "WORLD";

/// 消歧后缀起始序号（§7.2"唯一化"行：首个保留原名、后续追加 "_2"、"_3"…
/// ——即首个后缀为 2；格式与数字预算为 §7.2 设计默认，待产品确认，
/// 与 DTB O-12 同源登记，实现不私裁上限）。
constexpr std::uint64_t kDisambiguationSuffixBegin = 2;

/// 消歧后缀分隔（§7.2 原文形态 "_2"/"_3"——下划线＋十进制序号）。
constexpr const char* kDisambiguationSuffixSeparator = "_";

/// 场景作用域的世界系命名空间 token（§7.1 范围表：SceneObject 行
/// "Scene.<localName>"）。
constexpr const char* kSceneScopeToken = "Scene";

/// 几何视觉段后缀（§7.1 Geometry 行示例 ".visual"）。
constexpr const char* kGeometryVisualFacet = "visual";

/// 几何碰撞段后缀（§7.1 Geometry 行示例 ".collision"；工具/场景几何在
/// 碰撞评估消费——统一 collision 段，§15.4 RT-T05 登记项）。
constexpr const char* kGeometryCollisionFacet = "collision";

/// 动力学 Body 名后缀（§7.1 Body 行示例 "IRB6700.link_2.body"）。
constexpr const char* kBodyFacet = "body";

// =====================================================================
// 前缀拼装唯一合法点（R-4——见文件头）。除本函数外，任何代码不得以任何
// 形态拼装 "作用域.局部名"（NameMapTest 源码扫描钉住本名字的唯一出现）。
// =====================================================================

/**
 * @brief 拼装设备作用域全名（R-4 唯一合法位置——ARC-04/SA-05）。
 * @param scopeToken [in] 作用域 token（设备名或 "Scene"）
 * @param localName  [in] 消歧后局部名
 * @return "scopeToken.localName"（Scene 对象名 → "Scene.obstacle_1"）
 */
std::string joinScopeLocal(const std::string& scopeToken, const std::string& localName)
{
    return scopeToken + "." + localName;
}

// =====================================================================
// §7.2"合法化"行：非法字符替换 → 前导数字前缀 → 空名兜底（顺序即规则表
// 行内顺序；返回值第二分量＝是否触发空名警告）。
// =====================================================================

/**
 * @brief 合法化一个局部名（§7.2"合法化"行的逐步实现）。
 * @param raw [in] 规范侧权威局部名（CanonicalModel 原值；可为空）
 * @param isEmptyName [out] 置位表示触发空名兜底（调用方产 EmptyName 警告）
 * @return 合法化后的局部名（charset 内；不含前导数字；非空）
 */
std::string legalizeLocalName(const std::string& raw, bool& isEmptyName)
{
    isEmptyName = false;
    // 第一步：非法字符逐个替换 '_'（§7.2——替换而非拒绝：名称是产物标签，
    // 用户可读性优先；硬拒绝面在 builder 的空名/含'/'检查，S3/S5 已把关）。
    std::string legal;
    legal.reserve(raw.size());
    for (const char c : raw) {
        legal.push_back(isLegalNameChar(c) ? c : kIllegalCharReplacement);
    }
    // 第二步：前导数字加前缀 "n_"（RobWork Frame 名不允许数字开头——
    // 标识符安全集合的字符级约束）。
    if (!legal.empty() && legal.front() >= '0' && legal.front() <= '9') {
        legal.insert(0, kLeadingDigitPrefix);
    }
    // 第三步：空名兜底 "unnamed"＋警告（§7.2 原文；防御性——builder 已拒绝
    // 空名，本路径在直构 Entry 等场景保持规则完备）。
    if (legal.empty()) {
        isEmptyName = true;
        legal = kEmptyNameReplacement;
    }
    return legal;
}

/// 生成一条消歧警告（§7.2"唯一化"行：列出原名/消歧名/对象）。
RuntimeNameNotice makeDisambiguatedNotice(const core::ObjectId& id,
                                          const std::string& originalLocal,
                                          const std::string& runtimeLocal,
                                          const std::string& fullName)
{
    RuntimeNameNotice n;
    n.kind = RuntimeNameNotice::Kind::Disambiguated;
    n.objectId = id;
    n.originalLocalName = originalLocal;
    n.runtimeLocalName = runtimeLocal;
    n.fullName = fullName;
    return n;
}

/// 生成一条空名警告（§7.2"合法化"行：空名→"unnamed"＋警告）。
RuntimeNameNotice makeEmptyNameNotice(const core::ObjectId& id)
{
    RuntimeNameNotice n;
    n.kind = RuntimeNameNotice::Kind::EmptyName;
    n.objectId = id;
    n.originalLocalName.clear();
    n.runtimeLocalName = kEmptyNameReplacement;
    return n;
}

/// 条目排序键（§7.2"稳定排序"行： (scope, localName, ObjectId) 字典序；
/// ObjectId 字节序＝规范文本序——toCanonical 为字节的十六进制直出）。
bool entryLess(const RuntimeNameMap::Entry& a, const RuntimeNameMap::Entry& b)
{
    if (a.scope != b.scope) { return static_cast<int>(a.scope) < static_cast<int>(b.scope); }
    if (a.localName != b.localName) { return a.localName < b.localName; }
    return a.objectId.bytes < b.objectId.bytes;
}

}  // namespace

// =====================================================================
// 构建（§7.2 生成规则全流程——步骤号与 buildRuntimeNameMap 注释一致）。
// =====================================================================

RuntimeNameMap buildRuntimeNameMap(const CanonicalModel& model)
{
    using Entry = RuntimeNameMap::Entry;

    const RobotChain& chain = model.chain();

    // ---- 收集器：先收"原始条目"（objectId/scope/scopeToken/原始局部名），
    // 再统一合法化——scopeToken（设备名）自身也要先合法化，故分两阶段。----
    struct RawRequest {
        core::ObjectId objectId;   ///< 键对象
        NameScope scope;           ///< 语义范围
        bool deviceScoped;         ///< true＝设备作用域（scopeToken=设备名），false＝Scene
        std::string rawLocal;      ///< 规范侧权威局部名（消歧前）
        bool isDeviceEntry;        ///< true＝Device 条目（fullName=设备名本身，无前缀）
    };
    std::vector<RawRequest> raws;
    std::vector<RuntimeNameNotice> notices;

    // 步骤 1a：合法化设备名（§7.1 Device 行"设备名本身"）。设备名是其余
    // 设备作用域条目的 scopeToken，必须先于它们生成；保留字/消歧对设备名
    // 一视同仁（设备条目与其他条目同处一个 WC 命名空间）。
    bool deviceNameEmpty = false;
    const std::string deviceName = legalizeLocalName(chain.robotLocalName, deviceNameEmpty);
    if (deviceNameEmpty) {
        // 设备名空——builder 已拒（§4.3.3），防御性警告随 robot 对象登记。
        notices.push_back(makeEmptyNameNotice(chain.robotObjectId));
    }

    // 步骤 1b：逐范围收集条目请求（§7.1 范围表逐行；Sensor 不生成——R1 无实例）。
    {
        // Device 行：robot→设备名本身（无前缀——R-4 的"前缀"对设备名不适用）。
        raws.push_back({chain.robotObjectId, NameScope::Device, true, deviceName, true});
        // BaseMount 行：robot（派生节点——§6.3 唯一写入点的安装 FixedFrame）。
        raws.push_back({chain.robotObjectId, NameScope::BaseMount, true,
                        std::string{"BaseMount"}, false});
        // Joint 行：逐关节（链序）。
        for (const CanonicalJoint& j : chain.joints) {
            raws.push_back({j.objectId, NameScope::Joint, true, j.localName, false});
        }
        // LinkFrame 行：逐连杆；BaseFrame 行：link[0]；Flange 行：link[N]。
        for (std::size_t i = 0; i < chain.links.size(); ++i) {
            const CanonicalLink& l = chain.links[i];
            raws.push_back({l.objectId, NameScope::LinkFrame, true, l.localName, false});
            if (i == 0) {
                raws.push_back({l.objectId, NameScope::BaseFrame, true,
                                std::string{"Base"}, false});
            }
            if (i + 1 == chain.links.size()) {
                raws.push_back({l.objectId, NameScope::Flange, true,
                                std::string{"Flange"}, false});
            }
        }
        // Tcp 行：逐工具（TCP Frame 承载工具局部名——§7.1 示例 "tcp"/
        // "gripper_tcp" 即工具局部名形态）。
        for (const CanonicalTool& t : model.tools()) {
            raws.push_back({t.objectId, NameScope::Tcp, true, t.localName, false});
        }
        // Geometry 行：连杆 visual/collision＋工具 geometry＋场景 geometry
        //（键＝资源 resourceId——几何资源是对象，ARC-04 经其 id 解析）。
        auto pushGeometry = [&](const ResourceRef& ref, bool sceneScoped,
                                const std::string& ownerRawLocal, const char* facet) {
            // 局部名＝owner 合法化名＋"."＋段名（§7.1 示例形态
            // "joint_3.collision"）；owner 名复用其主条目的合法化结果——
            // 此处在收集期仅暂存原始组合，合法化在统一阶段执行（组合名
            // 字符已全在 charset 内，合法化幂等）。
            std::string ownerLegal;
            {
                bool emptyFlag = false;
                ownerLegal = legalizeLocalName(ownerRawLocal, emptyFlag);
            }
            RawRequest r;
            r.objectId = ref.resourceId;
            r.scope = NameScope::Geometry;
            r.deviceScoped = !sceneScoped;
            r.rawLocal = ownerLegal + "." + facet;
            r.isDeviceEntry = false;
            raws.push_back(std::move(r));
        };
        for (const CanonicalLink& l : chain.links) {
            if (l.visual.has_value()) {
                pushGeometry(*l.visual, false, l.localName, kGeometryVisualFacet);
            }
            if (l.collision.has_value()) {
                pushGeometry(*l.collision, false, l.localName, kGeometryCollisionFacet);
            }
        }
        for (const CanonicalTool& t : model.tools()) {
            if (t.geometry.has_value()) {
                pushGeometry(*t.geometry, false, t.localName, kGeometryCollisionFacet);
            }
        }
        for (const CanonicalSceneObject& s : model.scene()) {
            // SceneObject 行：世界系固连对象（scopeToken="Scene"）。
            raws.push_back({s.objectId, NameScope::SceneObject, false, s.localName, false});
            pushGeometry(s.geometry, true, s.localName, kGeometryCollisionFacet);
        }
        // Body 行：仅 capabilities.hasDynamicWorkCell（§7.1"DWC 存在时"——
        // 能力位由 builder 从内容派生，读取它保持纯函数性）。
        if (model.capabilities().hasDynamicWorkCell) {
            for (const CanonicalLink& l : chain.links) {
                bool emptyFlag = false;
                const std::string linkLegal = legalizeLocalName(l.localName, emptyFlag);
                RawRequest r;
                r.objectId = l.objectId;
                r.scope = NameScope::Body;
                r.deviceScoped = true;
                r.rawLocal = linkLegal + "." + kBodyFacet;
                r.isDeviceEntry = false;
                raws.push_back(std::move(r));
            }
        }
    }

    // ---- 步骤 2＋3：合法化＋保留字标记（§7.2 行 1/2/6）。----
    struct WorkingEntry {
        Entry entry;                 // 最终承载（localName 先放合法化名）
        std::string legalLocal;      // 合法化名（消歧前的"原名"——警告用）
        bool reserved;               // localName 命中保留字（直接消歧）
        bool keepOriginal;           // 组内首位且未被保留字/占用拦截时保留原名
        std::uint64_t suffix = 0;    // >0 表示需追加 "_<n>"
    };
    std::vector<WorkingEntry> working;
    working.reserve(raws.size());
    // 占用名集合：全部合法化名预先登记（§7.2——消歧候选必须跳过"已存在的
    // 其他对象名"，例如对象 A:"base"、B:"base_2"、C:"base" 时 C 不得取
    // "base_2"）。Device 条目的占用名＝设备名本身（fullName），其余＝全名。
    std::map<std::string, std::size_t, std::less<>> takenNames;
    for (RawRequest& r : raws) {
        bool emptyFlag = false;
        std::string legal = legalizeLocalName(r.rawLocal, emptyFlag);
        if (emptyFlag) {
            notices.push_back(makeEmptyNameNotice(r.objectId));
        }
        WorkingEntry w;
        w.entry.objectId = r.objectId;
        w.entry.scope = r.scope;
        w.entry.scopeToken = r.isDeviceEntry ? legal
            : (r.deviceScoped ? deviceName : std::string{kSceneScopeToken});
        w.entry.localName = legal;
        w.entry.authoritativeLocalName = r.rawLocal;
        w.legalLocal = std::move(legal);
        // 保留字（§7.2"保留字"行——大小写敏感精确比对）。
        w.reserved = (w.legalLocal == kReservedWorld);
        w.keepOriginal = !w.reserved;
        working.push_back(std::move(w));
    }
    for (WorkingEntry& w : working) {
        // 全名＝Device 条目取设备名本身，其余取拼装（唯一合法点调用）。
        const std::string full = (w.entry.scope == NameScope::Device)
            ? w.entry.localName
            : joinScopeLocal(w.entry.scopeToken, w.entry.localName);
        ++takenNames[full];
    }

    // ---- 步骤 4：消歧（§7.2"唯一化"行）——按全名分组，组内按 ObjectId
    // 规范文本（字节）字典序分配：首个（且非保留字、名未被占）保留原名，
    // 其余自 "_2" 起取首个未占用后缀；每例产警告。----
    {
        // 分组：全名 → 组成员下标（保持 working 序，组内再排序）。
        std::map<std::string, std::vector<std::size_t>, std::less<>> groups;
        for (std::size_t i = 0; i < working.size(); ++i) {
            const WorkingEntry& w = working[i];
            const std::string full = (w.entry.scope == NameScope::Device)
                ? w.entry.localName
                : joinScopeLocal(w.entry.scopeToken, w.entry.localName);
            groups[full].push_back(i);
        }
        for (auto& kv : groups) {
            std::vector<std::size_t>& members = kv.second;
            // 组内按 (scope, localName, ObjectId) 序排序（§7.2 的 ObjectId
            // 字典序在组内即 ObjectId 序——同组同 scope 同 localName）。
            std::sort(members.begin(), members.end(),
                      [&working](std::size_t a, std::size_t b) {
                          return entryLess(working[a].entry, working[b].entry);
                      });
            for (std::size_t rank = 0; rank < members.size(); ++rank) {
                WorkingEntry& w = working[members[rank]];
                // 消歧判定（§7.2"唯一化"行）：
                //   - 保留字→直接消歧（"localName 为 WORLD 时直接消歧加后缀"，
                //     即使组内首位也不保留原名）；
                //   - 组内非首位→同名冲突，追加后缀；组内首位保留原名。
                // 跨组安全性说明：后缀候选探测跳过"全部合法化名"（takenNames
                // 在分组前已整表登记），故探测永不占用其他条目的合法化名；
                // 组内首位保留的原名只属于本组（同名条目必在同组）——两个
                // 决定性来源合并后全名全局唯一（构造不变量）。
                const bool needsSuffix = w.reserved || (rank > 0);
                if (!needsSuffix) { continue; }
                // 后缀候选自 "_2" 起线性探测首个未占用名（§7.2 后缀形态
                // "_2"、"_3"…；无人工数字上限——uint64 溢出即防御性
                // NameConflict，"不可消歧"行的理论不可达通道）。
                for (std::uint64_t n = kDisambiguationSuffixBegin;; ++n) {
                    if (n == std::numeric_limits<std::uint64_t>::max()) {
                        // 防御性失败（理论不可达——ObjectId 唯一保证组有限、
                        // 自然数后缀空间耗尽需 2^64 量级同名对象）：fail-fast，
                        // 不返回半成品（§7.2"不可消歧"→NameConflict）。
                        throw RuntimeError{
                            RuntimeErrorCode::NameConflict,
                            "runtime/name-conflict: 消歧后缀空间耗尽（localName="
                                + w.legalLocal + "，scope=" + std::to_string(
                                    static_cast<int>(w.entry.scope)) + "）"};
                    }
                    const std::string candidate = w.legalLocal
                        + kDisambiguationSuffixSeparator + std::to_string(n);
                    std::string candFull = (w.entry.scope == NameScope::Device)
                        ? candidate
                        : joinScopeLocal(w.entry.scopeToken, candidate);
                    if (takenNames.find(candFull) == takenNames.end()) {
                        // 候选名可用：登记占用＋落位＋警告（原名/消歧名/对象）。
                        ++takenNames[candFull];
                        w.suffix = n;
                        notices.push_back(makeDisambiguatedNotice(
                            w.entry.objectId, w.legalLocal, candidate, candFull));
                        break;
                    }
                }
            }
        }
        // 后缀落位（探测循环内只记序号——此处统一改写 localName/fullName，
        // 避免探测期间半状态污染 takenNames 判定）。
        for (WorkingEntry& w : working) {
            if (w.suffix == 0) { continue; }
            w.entry.localName = w.legalLocal + kDisambiguationSuffixSeparator
                + std::to_string(w.suffix);
        }
    }

    // ---- 步骤 5：规范化存储（§7.2"稳定排序"）＋索引＋身份。----
    std::vector<Entry> entries;
    entries.reserve(working.size());
    for (WorkingEntry& w : working) {
        // 全名在消歧后重算（Device 条目＝localName；其余＝唯一合法点拼装）。
        w.entry.fullName = (w.entry.scope == NameScope::Device)
            ? w.entry.localName
            : joinScopeLocal(w.entry.scopeToken, w.entry.localName);
        entries.push_back(std::move(w.entry));
    }
    std::sort(entries.begin(), entries.end(), entryLess);

    // 索引：fullName→下标（透明比较器，string_view 零拷贝查询）；
    // ObjectId→身份条目（同 id 多条目时取存储序首条——确定性，§15.4 登记）。
    RuntimeNameMap map;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        map.m_byFullName.emplace(entries[i].fullName, i);
        map.m_identityIndex.emplace(entries[i].objectId, i);
    }
    map.m_entries = std::move(entries);
    map.m_notices = std::move(notices);
    map.m_ruleVersion = kNameMapRuleVersion;  // §7.2 规则表初始版本（单一权威常量——待产品确认冻结）
    map.m_identity = rtcodec::computeNameMapContentIdentity(map);  // §7.6（CR-02）
    return map;
}

// =====================================================================
// 查询实现（§7.3 接口属性表：并发只读、确定性、不抛）。
// =====================================================================

Expected<ObjectRef, RuntimeResolveError>
    RuntimeNameMap::resolveRuntimeName(RuntimeNameView name) const noexcept
{
    // 空名：§7.4 接口属性表"空串/非法句法同码"——UnknownObject（含回显），
    // 不抛、不默认命中（RT-NM-3）。
    RuntimeResolveError err;
    err.code = RuntimeErrorCode::UnknownObject;
    err.requestedName.assign(name);

    if (name.empty()) {
        err.detail = "runtime/unknown-object: 空名不解析（§7.4——空串同码不抛）";
        return Expected<ObjectRef, RuntimeResolveError>::err(std::move(err));
    }
    // 整串精确匹配（大小写敏感——附录 D 第 12 项；透明比较器避免构造
    // std::string 拷贝）。未命中＝不在本快照（跨快照名/旧名/拼错前缀
    // 一律同码——调用方无法也从不需要区分"为什么不在"，RT-NM-3/7）。
    const auto it = m_byFullName.find(name);
    if (it == m_byFullName.end()) {
        err.detail = "runtime/unknown-object: 名称不在本快照映射（整串精确匹配未命中）";
        return Expected<ObjectRef, RuntimeResolveError>::err(std::move(err));
    }
    const Entry& e = m_entries[it->second];
    ObjectRef ref;
    ref.objectId = e.objectId;
    ref.scope = e.scope;
    ref.localName = e.authoritativeLocalName;  // 消歧前权威局部名（§7.3）
    return Expected<ObjectRef, RuntimeResolveError>::ok(std::move(ref));
}

Expected<RuntimeName, RuntimeNameError>
    RuntimeNameMap::resolveObjectId(core::ObjectId id) const noexcept
{
    RuntimeNameError err;
    err.code = RuntimeErrorCode::UnknownObject;
    // detail 携 id 规范文本（§7.4 接口属性表——含全零占位 id，调用方可定位）。
    err.detail = "runtime/unknown-object: 对象不在本快照映射（id="
        + id.toCanonical() + "）";
    err.requestedId = id;

    // 非法 id（全零保留值）＝查询前置违约的查询轨表达：与未命中同码回执，
    // 不抛（§7.3"查询不抛"总则；fail-fast 轨保留给 Expected 前置违约）。
    if (!id.isValid()) {
        err.detail = "runtime/unknown-object: 全零 id 不解析（保留值纪律，id="
            + id.toCanonical() + "）";
        return Expected<RuntimeName, RuntimeNameError>::err(std::move(err));
    }
    const auto it = m_identityIndex.find(id);
    if (it == m_identityIndex.end()) {
        return Expected<RuntimeName, RuntimeNameError>::err(std::move(err));
    }
    const Entry& e = m_entries[it->second];
    RuntimeName n;
    n.fullName = e.fullName;
    n.scopeToken = e.scopeToken;
    n.localName = e.localName;
    n.scope = e.scope;
    return Expected<RuntimeName, RuntimeNameError>::ok(std::move(n));
}

std::vector<ObjectRef> RuntimeNameMap::objectsInScope(NameScope scope) const noexcept
{
    // 存储序＝(scope, localName, ObjectId) 字典序——同 scope 条目连续且
    // 有序，顺序过滤即稳定排序输出（§7.3"稳定排序"）。
    std::vector<ObjectRef> out;
    for (const Entry& e : m_entries) {
        if (e.scope != scope) { continue; }
        ObjectRef ref;
        ref.objectId = e.objectId;
        ref.scope = e.scope;
        ref.localName = e.authoritativeLocalName;
        out.push_back(std::move(ref));
    }
    return out;
}

bool RuntimeNameMap::operator==(const RuntimeNameMap& o) const noexcept
{
    // 条目集＋规则版本＋内容身份全等（§7.3"逐字节相等映射与相等内容身份"
    // 的值面投影——条目已规范化，逐字段比较即字节等价的充要判据）。
    if (m_ruleVersion != o.m_ruleVersion || m_entries.size() != o.m_entries.size()) {
        return false;
    }
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        const Entry& a = m_entries[i];
        const Entry& b = o.m_entries[i];
        if (a.scope != b.scope || a.objectId != b.objectId || a.scopeToken != b.scopeToken
            || a.localName != b.localName || a.fullName != b.fullName
            || a.authoritativeLocalName != b.authoritativeLocalName) {
            return false;
        }
    }
    return m_identity == o.m_identity;
}

RuntimeNameMap RuntimeNameMap::fromValidatedEntries(std::vector<Entry> entries,
                                                    core::ContentIdentity identity,
                                                    std::uint32_t ruleVersion)
{
    // parseNameMap 专用恢复路径（前置校验在 parse 侧完成——本函数只装配
    // 索引；校验与装配分离保证解析失败不产出半成品，NFR-COR-03）。
    RuntimeNameMap map;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        map.m_byFullName.emplace(entries[i].fullName, i);
        map.m_identityIndex.emplace(entries[i].objectId, i);
    }
    map.m_entries = std::move(entries);
    map.m_identity = identity;
    map.m_ruleVersion = ruleVersion;  // 编码头透传（worker 侧与主进程同版核对）
    return map;
}

// =====================================================================
// S8 交叉校验（§7.2 生成时点/§7.4 无漏/旧/双前缀——MDL-14 验收口径）。
// =====================================================================

void crossCheckRuntimeNames(const RuntimeNameMap& map,
                            const std::vector<std::string>& actualRuntimeNames)
{
    // 逐项独立核对（方向＝WC 实际名 ⊆ 映射名，§7.4；未命中即失败并回显
    // 名单——双前缀/旧机械臂名/编译器越权写入在此一并拦截）。
    std::string offenders;
    for (const std::string& name : actualRuntimeNames) {
        if (!map.resolveRuntimeName(name).ok()) {
            offenders += "\n  - \"" + name + "\"";
        }
    }
    if (!offenders.empty()) {
        // S8 硬失败通道（§5.2 S8 行：WC 对象名与映射不一致→NameConflict）；
        // detail 逐条列出未命中名（确定性＝输入序），fail-fast 不产出快照。
        throw RuntimeError{RuntimeErrorCode::NameConflict,
                           "runtime/name-conflict: WC/DWC 实际对象名未命中映射"
                           "（MDL-14：双前缀/旧机械臂名/越权写入检测）"
                           + offenders};
    }
}

}  // namespace sdurws::ird::runtime
