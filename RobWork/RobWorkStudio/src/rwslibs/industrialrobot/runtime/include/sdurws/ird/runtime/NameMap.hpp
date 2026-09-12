/**
 * @file   NameMap.hpp
 * @brief  RuntimeNameMap——ObjectId ↔ RobWork 设备作用域全名 的唯一双向解析器
 *         （⑥名称端口；MDL-14 编译期生成＋AT-18 双向往返的载体）。
 *
 * 设计依据：
 *   - units/runtime.md §7 全章：§7.1（命名模型与 NameScope 范围表）、§7.2
 *     （生成规则：合法字符集/合法化/消歧/保留字/稳定排序/生成时点）、§7.3
 *     （冻结接口——任务指定的四个入口＋接口属性表＋IRuntimeNameResolver
 *     ⑥端口形态）、§7.4（AT-18 双向往返验证——本头数据面的验收口径）、
 *     §7.5（不持久化/无别名/旧快照旧名仍可反解）、§7.6（IRDNAME 内容身份，
 *     编解码落位 Codec.hpp——CR-02 摘要边界同款）
 *   - §6.3（BaseMount 唯一写入点的运行时名"经名称映射登记"——派生节点名
 *     是映射输出的一部分，见下方"映射条目构成"）
 *   - 需求 ARC-04（稳定对象 ID＋统一名称解析器；R-4 前缀操作唯一合法位置）、
 *     MDL-14（完整映射：无遗漏/无重复前缀/无旧机械臂名）、CON-06（映射内容
 *     身份随快照保存，名称先反解为对象 ID 才接纳结果）
 *   - 任务契约 tasks/foundation/RT-T05.json（产物 NameMap.hpp/.cpp；acceptance
 *     RT-NM-1～7、R-4 唯一合法位置、MDL-14 全量生成双向校验）
 *
 * 背景说明（为什么需要唯一解析器——ARC-04 原文）：RobWork 设备作用域全名
 * 只由统一名称解析器生成/反解，禁止各单元自行拼接、剥离或猜测机械臂名称
 * 前缀。因此"RobotScope + '.' + localName"这一前缀拼装动作在全部产品代码中
 * 只允许出现在 NameMap.cpp 的 joinScopeLocal() 一处（R-4 例外登记的唯一
 * 位置，NFR-MNT-07/静态门禁归 WP-01-T01）；下游持有的一律是：
 *   - 对象引用 → core::ObjectId（跨修订稳定，重命名不破坏引用——§7.5）；
 *   - 显示/回显名称 → resolveObjectId 反解（ui.md §诊断定位同款消费）；
 *   - RobWork 侧名称 → resolveRuntimeName 整串精确匹配（不拆段、不猜前缀）。
 *
 * 映射条目构成（§7.1 范围表＋§6.3 派生节点登记的合成口径）：
 *   身份作用域条目（每对象恰一条，resolveObjectId 的返回值来源）：
 *     robot→Device（设备名本身）；joint→Joint；link→LinkFrame；tool→Tcp；
 *     scene→SceneObject；资源→Geometry（几何资源以 resourceId 为键）。
 *   派生作用域条目（共享所属对象的 ObjectId；§6.3/§7.1 明文登记）：
 *     BaseMount（robot）、BaseFrame（link[0]）、Flange（link[N]）、
 *     Body（link，仅 capabilities.hasDynamicWorkCell 时——"DWC 存在时"）。
 *   派生条目使 WC 交叉校验可覆盖编译器写入的全部 Frame 名（§7.2 生成时点：
 *   "编译器写入 WC 的名字必须与映射输出逐一相等"）；解析语义见
 *   resolveRuntimeName/resolveObjectId 各自注释与 units/runtime.md §15.4
 *   的 RT-T05 实现层澄清登记。
 *
 * 不变量（构造即成立，buildRuntimeNameMap 是唯一执行点）：
 *   - 名称唯一：映射内 fullName 两两精确不等（WC 单一 Frame 命名空间的
 *     前提——重复名会使 S6 编译失败）；比较大小写敏感（附录 D 第 12 项
 *     精确等值，无容差）。
 *   - (ObjectId, NameScope) 对唯一：同对象同作用域不重复登记。
 *   - 双射解析：resolveObjectId 对映射内每个 ObjectId 恰返回其身份作用域
 *     名称；resolveRuntimeName 对映射内每个名称恰返回唯一 ObjectRef。
 *   - 确定性：同 model 重复构建产出逐条目相等的映射与相等内容身份
 *     （§7.3 后置——排序/消歧/编码全链无环境依赖，RT-NM-1/RT-ID-1 钉住）。
 *
 * 生命周期与所有权：值语义（拷贝/移动）；随 RuntimeSnapshot 持有（RT-T09），
 * 不持久化（§7.5——唯一入持久化链路的是内容身份，写盘归 evidence/execution）。
 * 线程安全：构建完成后只读（全部访问器 const、无 setter），可并发共享只读；
 * buildRuntimeNameMap 为纯函数、可重入。
 */

#ifndef SDURWS_IRD_RUNTIME_NAMEMAP_HPP
#define SDURWS_IRD_RUNTIME_NAMEMAP_HPP

#include <cstdint>
#include <functional>  // std::less<>（透明比较器——string_view 零拷贝查询）
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>     // ContentIdentity（CON-06 内容身份）
#include <sdurws/ird/core/Identity.hpp>   // ObjectId（唯一映射键——ARC-04）
#include <sdurws/ird/runtime/CanonicalModel.hpp>  // buildRuntimeNameMap 的输入
#include <sdurws/ird/runtime/Errors.hpp>  // Expected/RuntimeResolveError/RuntimeNameError

namespace sdurws::ird::runtime {

// =====================================================================
// kNameMapRuleVersion——名称生成规则版本单一权威常量（§7.1/§9.4 缓存键
// 分量 nameMapRuleVersion；RT-T10 增设——§15.4 v0.11 登记）。
// =====================================================================

/**
 * @brief 名称生成规则版本的单一权威值（§7.2 规则表初始版本＝1）。
 *
 * 背景：规则变化＝名称可能变＝新缓存键（§9.4 分量表 nameMapRuleVersion
 * 行）；升版走设计变更评审。消费方（buildRuntimeNameMap 的初始版本、
 * CacheKey 的键分量默认值）一律取本常量、禁止另写字面量（与
 * kBaseWorldRuleVersion 同款单一权威纪律——§15.4 v0.10 先例）。
 * 待产品确认冻结后仅替换此单点（§15.4 v0.5③ 同源登记）。
 */
inline constexpr std::uint32_t kNameMapRuleVersion = 1;

// =====================================================================
// NameScope——名称范围（§7.1 范围表行序即枚举声明序；一经交付不得改动/
// 插入——枚举数值进入 IRDNAME 编码契约面，稳定第一）。
// =====================================================================

/**
 * @brief 名称范围（§7.1 NameScope 表——运行时名称的语义分类轴）。
 *
 * 每个枚举值对应范围表一行；Sensor 为阶段 C+ 预留（R1 无实例、不预建
 * 生成规则——objectsInScope(Sensor) 恒空）。枚举值即 IRDNAME 编码中的
 * scope 字节（§7.6 排序键首位），顺序一经交付不得改动。
 */
enum class NameScope {
    Device,      ///< 机器人设备名本身（"IRB6700"——覆盖对象：robot）
    Joint,       ///< 关节 Frame（"IRB6700.joint_3"——覆盖对象：joint）
    LinkFrame,   ///< 连杆 FixedFrame（"IRB6700.link_2"——覆盖对象：link）
    BaseMount,   ///< 安装 FixedFrame（"IRB6700.BaseMount"——§6.3 唯一写入点）
    BaseFrame,   ///< Device base frame（"IRB6700.Base"——覆盖对象：link[0]）
    Flange,      ///< Device end frame（"IRB6700.Flange"——覆盖对象：link[N]）
    Tcp,         ///< TCP FixedFrame（"IRB6700.tcp"——覆盖对象：tool）
    Geometry,    ///< 几何资源挂接标识（"IRB6700.link_1.collision"/"Scene.obstacle_1.collision"）
    SceneObject, ///< 场景对象 FixedFrame（"Scene.<localName>"——世界系固连）
    Body,        ///< rwsim Body 名（"IRB6700.link_2.body"——DWC 存在时）
    Sensor,      ///< 传感器（阶段 C+ 预留；R1 无实例）
};

// =====================================================================
// RuntimeName——运行时名称值类型（§7.3 原文契约）。
// =====================================================================

/**
 * @brief 运行时名称（值类型）：全名及其分解（§7.3）。
 *
 * 字段语义（§7.3 原文注释）：
 *   - fullName："RobotScope.LocalName"（Geometry/Body 叠加 ".collision"
 *     等后缀段——即 scopeToken 与 localName 间恰一个分隔点，localName 内部
 *     可含 '.'）；Device 作用域例外：设备名本身，fullName==scopeToken==
 *     localName（设备名无前缀——R-4 的"前缀"对设备名不适用）。
 *   - scopeToken：机器人设备名或 "Scene"（场景作用域的世界系命名空间）。
 *   - localName：消歧后的局部名（合法化＋保留字＋消歧后缀全部生效后的
 *     最终形态——与 RobWork 侧 Frame/Device 名逐字节相等）。
 *   - scope：语义范围（诊断定位与下游按范围取用）。
 *
 * 等值：精确等值（附录 D 第 12 项——全字段逐一比较，无容差、大小写敏感）。
 * 值语义纯结构；线程安全。
 */
struct RuntimeName {
    std::string fullName;   ///< 全名（RobWork 侧逐字节名称；Device 作用域＝设备名本身）
    std::string scopeToken; ///< 机器人设备名或 "Scene"（Device 作用域＝设备名）
    std::string localName;  ///< 消歧后的局部名（Device 作用域＝设备名）
    NameScope scope = NameScope::Device; ///< 语义范围

    /// 精确等值（附录 D 第 12 项；确定性纯函数、不抛）。
    bool operator==(const RuntimeName& o) const noexcept
    {
        return fullName == o.fullName && scopeToken == o.scopeToken
            && localName == o.localName && scope == o.scope;
    }
    bool operator!=(const RuntimeName& o) const noexcept { return !(*this == o); }
};

/// 解析入口的完整名字符串视图（§7.3：using RuntimeNameView = std::string_view）。
using RuntimeNameView = std::string_view;

// =====================================================================
// ObjectRef——解析产物（§7.3 原文契约）。
// =====================================================================

/**
 * @brief 名称解析产物：对象身份＋范围语义（§7.3）。
 *
 * localName 为**规范侧权威 localName（消歧前）**——即 CanonicalModel 中
 * 对象的原局部名（几何/Body 等叠加段条目为其组合形态）；消歧后缀只改变
 * 运行时名，不改变权威局部名——下游据此回显用户可读名（ui.md：诊断定位
 * 经 resolveObjectId 取 localName）。业务侧持久引用一律取 objectId（ARC-04）。
 * 值语义纯结构；线程安全。
 */
struct ObjectRef {
    core::ObjectId objectId; ///< 对象稳定身份（跨修订不变——持久引用唯一形态）
    NameScope scope = NameScope::Device; ///< 命中条目的语义范围
    std::string localName;   ///< 规范侧权威 localName（消歧前——见结构注释）
};

// =====================================================================
// RuntimeNameNotice——生成期警告记录（§7.2 消歧/空名警告的数据承载）。
// =====================================================================

/**
 * @brief 名称生成期的警告级记录（§7.2："每例产警告级诊断（列出原名/消歧名/
 *        对象）"与"空名→unnamed＋警告"）。
 *
 * ★ 为什么不是 core::DiagnosticRecord（PA-1 登记项）：稳定诊断码的码值
 *   权威归 diagnostics 单元 StableCodeRegistry；§10.11 v0.3 冻结清单中
 *   尚无名称消歧警告码（该清单只含错误码/事件码），而本任务的契约
 *   allowedFiles 不含 units/diagnostics.md——故本类型只承载**数据三元组**
 *   （原名/消歧名/对象，§7.2 原文要求的三项），警告码的分配与
 *   CompileOutcome.diagnostics 的落位归编译器任务（RT-T11）随 diagnostics
 *   侧登记后转译。本单元不私裁码值。
 *
 * 值语义纯结构；线程安全。
 */
struct RuntimeNameNotice {
    /// 警告种类（§7.2 两处警告来源）。
    enum class Kind {
        Disambiguated, ///< 同范围内合法化后同名——按 ObjectId 序消歧加后缀
        EmptyName,     ///< 空名（防御性——CanonicalModelBuilder 已拒绝；规则面保留）
    };

    Kind kind = Kind::Disambiguated; ///< 警告种类
    core::ObjectId objectId;         ///< 对象（"对象"项——警告定位主体）
    std::string originalLocalName;   ///< 原名（消歧前的合法化局部名；空名警告为空串）
    std::string runtimeLocalName;    ///< 消歧名（最终局部名；空名警告为 "unnamed"）
    std::string fullName;            ///< 消歧后全名（便于直接定位 WC 对象）
};

// =====================================================================
// RuntimeNameMap——不可变映射本体（§7.3 冻结接口：四个查询入口＋构建纯函数）。
// =====================================================================

/**
 * @brief ObjectId ↔ RuntimeName 双向映射（§7.3——查询全部 const、并发只读
 *        安全、确定性、无副作用；查询轨不抛，一律 Expected 返回）。
 *
 * 构造：唯一入口＝buildRuntimeNameMap(model)（或 rtcodec::parseNameMap 重建，
 * worker 物化同款——§7.6）；默认构造仅产生空映射（供容器占位与 Expected
 * 载体，查询一律返回 UnknownObject——不抛、不默认命中，RT-NM-3 口径）。
 *
 * 存储与排序（§7.2"稳定排序"行）：条目按 (scope, localName, ObjectId 规范
 * 文本) 字典序存储——遍历与序列化确定；另持 fullName 索引（透明比较器，
 * 解析零拷贝）与 ObjectId→身份条目索引。
 *
 * 身份条目规则：一个 ObjectId 可携带多条派生条目（§6.3/§7.1），其"身份
 * 作用域"条目＝resolveObjectId 的返回值来源：robot→Device、joint→Joint、
 * link→LinkFrame、tool→Tcp、scene→SceneObject、资源→Geometry；资源被多处
 * 引用（多条 Geometry 条目）时取映射序首条（(scope, localName, ObjectId)
 * 序最小——确定性，§15.4 RT-T05 登记项）。
 *
 * 线程安全：构建后只读；拷贝/移动值语义。
 */
class RuntimeNameMap {
public:
    /// 空映射（仅供容器占位——查询全部 UnknownObject；无诊断、零身份）。
    RuntimeNameMap() = default;

    // ---- 查询入口 1/4：名称 → 对象（§7.3 接口属性表第 2 行）----

    /**
     * @brief 按完整名精确解析对象（整串匹配——不拆段、不做前缀/后缀猜测）。
     *
     * @param name [in] 完整名（"RobotScope.LocalName"形态；大小写敏感——
     *             附录 D 第 12 项精确等值，与 RobWork findFrame 行为一致）
     * @return ok＝ObjectRef（单射唯一：命中条目的对象/范围/权威局部名）；
     *         err＝UnknownObject（requestedName 回显原名；空串/未命中/
     *         大小写不符/非法句法同码——§7.4 接口属性表），不抛、不默认命中
     *
     * 确定性：同映射同名同结果（只读哈希/有序表查询）；并发只读安全。
     * 复杂度：O(log n)（fullName 有序索引）。
     */
    Expected<ObjectRef, RuntimeResolveError>
        resolveRuntimeName(RuntimeNameView name) const noexcept;

    // ---- 查询入口 2/4：对象 → 名称（§7.3 接口属性表第 3 行）----

    /**
     * @brief 按对象身份反解运行时名（返回其身份作用域条目——见类注释）。
     *
     * @param id [in] 对象稳定身份（须为构建输入闭包内的 ObjectId）
     * @return ok＝RuntimeName（身份作用域条目的全名/分解/范围）；err＝
     *         UnknownObject（detail 含 id 规范文本——§7.4 接口属性表），
     *         不抛
     *
     * 用法约束（RT-NM-7 类型层纪律）：本入口只接受 core::ObjectId——把
     * RuntimeName 当 ObjectId 传入在编译期即不可达（两类型无转换）；全名
     * 只经 resolveRuntimeName 整串匹配。
     */
    Expected<RuntimeName, RuntimeNameError> resolveObjectId(core::ObjectId id) const noexcept;

    // ---- 查询入口 3/4：按范围枚举（§7.3：稳定排序）----

    /**
     * @brief 枚举某范围的全部条目（§7.3——稳定排序）。
     *
     * @param scope [in] 目标范围（Sensor 恒空——R1 无实例，§7.1 范围表）
     * @return 该范围全部条目（按映射存储序＝(scope, localName, ObjectId)
     *         字典序输出；每次调用独立构造返回值——确定性、并发只读安全）
     */
    std::vector<ObjectRef> objectsInScope(NameScope scope) const noexcept;

    // ---- 查询入口 4/4：内容身份与规则版本（CON-06/§7.6）----

    /**
     * @brief 映射内容身份（§7.6：SHA-256 over IRDNAME 编码——含规则版本
     *        编码头；core::ContentDigester 单一摘要路径，CR-02）。
     *
     * CON-06 消费点：内容身份随快照进入 evidence 切片与结果接纳核对
     * （execution 经 IRuntimeNameResolver::nameMapIdentity 比对）。空映射
     * 返回全零（保留值——"无映射"与"空集映射"在本类型层同态，空映射仅
     * 占位用途）。确定性：同条目集同身份（跨进程一致，NFR-COR-02）。
     */
    core::ContentIdentity contentIdentity() const noexcept { return m_identity; }

    /**
     * @brief 生成规则版本（nameMapRuleVersion，§7.1/§9.4 缓存键分量）。
     *
     * 当前值 1＝§7.2 规则表的初始版本（设计默认，待产品确认后冻结——
     * 规则变化＝名称可能变＝新缓存键，§9.4；升版走设计变更评审）。
     */
    std::uint32_t ruleVersion() const noexcept { return m_ruleVersion; }

    // ---- 生成期警告（§7.2 消歧/空名警告——数据面见 RuntimeNameNotice）----

    /**
     * @brief 构建期警告记录（消歧/空名——原名/消歧名/对象三元组）。
     *
     * 顺序＝生成序（条目收集序，确定性）；诊断码分配与 diagnostics 落位
     * 归 RT-T11（PA-1——见 RuntimeNameNotice 注释）。
     */
    const std::vector<RuntimeNameNotice>& notices() const noexcept { return m_notices; }

    // ---- 映射级等值与规模（测试/worker 核对辅助）----

    /**
     * @brief 映射级精确等值（条目集＋规则版本＋内容身份全等）。
     *
     * "同 model 重复调用产出逐字节相等映射"（§7.3 后置）的值面判据——
     * RT-NM-1 以本判据＋编码逐字节相等双重钉住。确定性纯函数、不抛。
     */
    bool operator==(const RuntimeNameMap& o) const noexcept;
    bool operator!=(const RuntimeNameMap& o) const noexcept { return !(*this == o); }

    /// 条目总数（身份作用域条目＋派生条目；测试/规模核对用）。
    std::size_t size() const noexcept { return m_entries.size(); }

    // ---- 编解码与构建的协作面（同单元内部——R-2 不跨单元）----

    /// 映射条目（IRDNAME 编码/解码与遍历的最小承载；字段见名称注释）。
    struct Entry {
        core::ObjectId objectId;              ///< 键：对象稳定身份
        NameScope scope = NameScope::Device;  ///< 语义范围（编码 scope 字节）
        std::string scopeToken;               ///< 设备名或 "Scene"
        std::string localName;                ///< 消歧后局部名
        std::string fullName;                 ///< 全名（全局唯一）
        std::string authoritativeLocalName;   ///< 规范侧权威局部名（消歧前）

        /**
         * @brief 全名形态自检（IRDNAME 解析侧结构复核——§7.3 名称分解契约）。
         *
         * Device 条目：fullName==scopeToken==localName（设备名本身，无前缀）；
         * 其余条目：fullName ＝ scopeToken · kScopeSeparator · localName
         *（逐字节比对——本方法只做形态判定，不构造任何新名称）。
         * @return 形态合法＝true（noexcept 纯比对）
         */
        bool wellFormedFullName() const noexcept
        {
            if (scope == NameScope::Device) {
                return fullName == scopeToken && fullName == localName;
            }
            const std::size_t sep = scopeToken.size();
            return fullName.size() == sep + 1 + localName.size()
                && fullName.compare(0, sep, scopeToken) == 0
                && fullName[sep] == kScopeSeparator
                && fullName.compare(sep + 1, localName.size(), localName) == 0;
        }
    };

    /// 存储序条目视图（IRDNAME 编码序＝存储序——§7.6/§7.2 同键）。
    const std::vector<Entry>& entries() const noexcept { return m_entries; }

    /// 条目全名形态常量（作用域 token 与局部名间唯一分隔——仅用于形态
    /// 自检的字节比对，不参与任何名称构造；R-4 单点仍是 NameMap.cpp）。
    static constexpr char kScopeSeparator = '.';

    /**
     * @brief 由已验证条目重建映射（rtcodec::parseNameMap 专用恢复路径）。
     *
     * 前置（调用方保证——parse 已校验）：entries 已按 (scope, localName,
     * ObjectId) 序、fullName 全局唯一、(objectId, scope) 唯一、Device 条目
     * fullName==localName、其余 fullName==scopeToken+"."+localName。
     * @param entries [in] 已排序去重的条目集（移动入）
     * @param identity [in] 该条目集的 IRDNAME 内容身份（parse 侧重算值）
     * @param ruleVersion [in] 编码头携带的生成规则版本（透传恢复）
     * @return 重建完成的只读映射
     */
    static RuntimeNameMap fromValidatedEntries(std::vector<Entry> entries,
                                               core::ContentIdentity identity,
                                               std::uint32_t ruleVersion);

private:
    // buildRuntimeNameMap 是唯一构建路径（friend 直填私有成员——构建完成后
    // 该路径即关闭，"构建后只读"由此成立）。
    friend RuntimeNameMap buildRuntimeNameMap(const CanonicalModel& model);

    std::vector<Entry> m_entries;   ///< 存储序条目（(scope, localName, ObjectId) 字典序）
    std::map<std::string, std::size_t, std::less<>> m_byFullName; ///< fullName→条目下标
    std::map<core::ObjectId, std::size_t> m_identityIndex;        ///< ObjectId→身份条目下标
    core::ContentIdentity m_identity{}; ///< 内容身份（§7.6——构建/解析时计算）
    std::uint32_t m_ruleVersion = kNameMapRuleVersion; ///< 生成规则版本（单一权威常量——见 ruleVersion() 注释）
    std::vector<RuntimeNameNotice> m_notices; ///< 生成期警告（构建路径填充）
};

// =====================================================================
// buildRuntimeNameMap——S8 生成纯函数（§7.3：只依赖 model 内容，不读 WC——
// 映射规则可独立测试）。
// =====================================================================

/**
 * @brief 从 CanonicalModel 确定性生成完整名称映射（§7.2 规则；纯函数）。
 *
 * 生成步骤（对 §7.2 规则表的逐步实现；同 model 重复调用产出逐条目相等
 * 映射与相等内容身份——§7.3 后置）：
 *   1. 条目收集（§7.1 范围表）：robot→Device；逐关节→Joint；逐连杆→
 *      LinkFrame；link[0]→BaseFrame、link[N]→Flange；robot→BaseMount
 *      （§6.3 派生节点）；逐工具→Tcp；逐几何引用→Geometry（键＝资源
 *      resourceId；owner 局部名+"."+facet〔visual/collision；工具/场景
 *      几何统一 collision——几何在碰撞评估消费〕）；逐场景对象→
 *      SceneObject（scopeToken="Scene"）；capabilities.hasDynamicWorkCell
 *      时逐连杆→Body（"DWC 存在时"）。Sensor 不生成（R1 无实例）。
 *   2. 合法化（§7.2）：非法字符（charset 外）逐个替换 '_'；前导数字加
 *      前缀 "n_"；空名→"unnamed"＋EmptyName 警告（防御性——builder 已拒）。
 *   3. 保留字（§7.2）：localName=="WORLD"（大小写敏感）直接消歧加后缀。
 *   4. 消歧（§7.2）：全名相同（WC 单一命名空间的冲突域）的条目按 ObjectId
 *      规范文本字典序排序，首个保留原名、后续追加 "_2"、"_3"…（后缀格式
 *      与数字预算＝§7.2 设计默认：无人工上限的十进制递增，溢出即防御性
 *      失败——待产品确认，不私裁，登记 DTB O-12 同源待确认项）；后缀候选
 *      跳过全部已占用名；每例产 Disambiguated 警告。
 *   5. 内容身份（§7.6）：IRDNAME 编码＋SHA-256（经 rtcodec，CR-02）。
 *
 * @param model [in] 规范模型（应为 CanonicalModelBuilder 产物——身份有效，
 *              §7.3 前置；只读，本函数不修改）
 * @return 完整映射（含生成期警告与内容身份；值语义独立）
 *
 * @throws RuntimeError NameConflict 消歧后仍冲突（防御性——ObjectId 唯一
 *         保证理论不可达；实现为后缀计数 uint64 溢出才可达，§7.2"不可消歧"
 *         行的构造期失败通道，fail-fast 不返回半成品）
 *
 * 线程/确定性：纯函数、可重入、无 I/O、无隐藏状态（§7.3 接口属性表）。
 */
RuntimeNameMap buildRuntimeNameMap(const CanonicalModel& model);

// =====================================================================
// crossCheckRuntimeNames——S8 交叉校验（§7.2 生成时点/§7.4 无漏/旧/双前缀）。
// =====================================================================

/**
 * @brief 对 WC/DWC 实际对象名逐项交叉校验（§7.2："编译器写入 WC 的名字
 *        必须与映射输出逐一相等"）。
 *
 * 校验方向（§7.4）：WC 实际对象名集合 ⊆ 映射名集合——任一实际名未命中
 * 映射即失败。检测能力（MDL-14 验收口径）：
 *   - 双前缀（"Robot.Robot.joint_1" 形态——映射无此名，RT-NM-6）；
 *   - 旧机械臂名残留（重编译后 WC 残留旧名——映射已重建，RT-NM-5）；
 *   - 遗漏的反向面（编译器写出了映射外的名字＝映射与 WC 不一致）。
 *
 * @param map [in] 本快照映射（只读）
 * @param actualRuntimeNames [in] WC/DWC 实际对象名集合（编译器收集；顺序
 *        不敏感——逐项独立核对；空集平凡通过，是否空集合法归编译器决策）
 *
 * @throws RuntimeError NameConflict 存在未命中映射的实际名（detail 逐条
 *         列出——S8 硬失败通道，与 buildRuntimeNameMap 构造期失败同码；
 *         调用方＝编译器 S8 段，fail-fast 不产出快照，MDL-06 原子性）
 *
 * 确定性：同输入同结论同 detail（纯函数；异常轨为 S8 失败语义的载体，
 * 与 §3.4"构造期硬失败"通道一致）。
 */
void crossCheckRuntimeNames(const RuntimeNameMap& map,
                            const std::vector<std::string>& actualRuntimeNames);

// =====================================================================
// IRuntimeNameResolver——⑥名称端口（§7.3：供不持快照的调用方）。
// =====================================================================

/**
 * @brief 名称端口抽象（⑥端口形态——execution 接纳/诊断定位/ui 显示消费）。
 *
 * 语义：实现＝绑定某快照的映射（nameMapIdentity 供接纳前核对结果绑定的
 * 映射身份——CON-06："运行时名称必须先反解为对象 ID 才能接纳结果"）。
 * 实现方约束：并发只读安全；查询语义与 RuntimeNameMap 同名入口逐字一致
 * （本头两 resolve 的契约即端口契约——不在此复制）。
 * 生命周期：实现由快照侧（RT-T09）提供并随快照存活；消费方不得缓存超出
 * 快照生命周期的引用（§9.3 迟到语义由绑定快照的映射保证）。
 */
class IRuntimeNameResolver {
public:
    virtual ~IRuntimeNameResolver() = default;

    /// 名称→对象（语义＝RuntimeNameMap::resolveRuntimeName——查询轨不抛）。
    virtual Expected<ObjectRef, RuntimeResolveError>
        resolveRuntimeName(RuntimeNameView name) const = 0;

    /// 对象→名称（语义＝RuntimeNameMap::resolveObjectId——查询轨不抛）。
    virtual Expected<RuntimeName, RuntimeNameError>
        resolveObjectId(core::ObjectId id) const = 0;

    /// 绑定映射的内容身份（CON-06 接纳核对凭据——相等才可接纳结果）。
    virtual core::ContentIdentity nameMapIdentity() const = 0;
};

/**
 * @brief 端口最小适配器：绑定一个 RuntimeNameMap 的只读解析器。
 *
 * 用途：⑥端口的参考实现（测试/无快照消费方）；RT-T09 快照可复用或以
 * 成员实现替换（端口契约不变）。非线程安全面：构造/析构（绑定关系建立
 * 后全部查询并发只读安全——被绑映射自身只读）。
 * 所有权：不拥有被绑映射——调用方保证其生存期覆盖本适配器的使用期。
 */
class BoundRuntimeNameResolver : public IRuntimeNameResolver {
public:
    /**
     * @brief 绑定映射构造。
     * @param map [in] 被绑映射（调用方持有；本适配器仅存只读指针）
     */
    explicit BoundRuntimeNameResolver(const RuntimeNameMap& map) noexcept : m_map(&map) {}

    /// 转发名称解析（契约同 RuntimeNameMap::resolveRuntimeName）。
    Expected<ObjectRef, RuntimeResolveError>
        resolveRuntimeName(RuntimeNameView name) const override
    {
        return m_map->resolveRuntimeName(name);
    }

    /// 转发对象反解（契约同 RuntimeNameMap::resolveObjectId）。
    Expected<RuntimeName, RuntimeNameError> resolveObjectId(core::ObjectId id) const override
    {
        return m_map->resolveObjectId(id);
    }

    /// 转发内容身份（CON-06 接纳核对凭据）。
    core::ContentIdentity nameMapIdentity() const override { return m_map->contentIdentity(); }

private:
    const RuntimeNameMap* m_map; ///< 被绑映射（不拥有——调用方保证生存期）
};

}  // namespace sdurws::ird::runtime

#endif  // SDURWS_IRD_RUNTIME_NAMEMAP_HPP
