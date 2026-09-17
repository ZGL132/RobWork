/**
 * @file   Budget.cpp
 * @brief  BudgetGuard 实现——§4.5.1 数值表落位、饱和加法记账、scope 树
 *         与父回收、压缩包双侧重账、比较型三要素诊断参数。
 *
 * 设计依据：
 *   - units/io.md §4.5.1（16 维默认值/硬上限/超限码表——P-IO-4 Draft 档
 *     位数值逐维落位）、§4.5.2（语义细则： tighten/relax 口径、饱和加
 *     法、双侧比较、包导入四维强制、多阶段累计）、§9.2（接口契约表）、
 *     §4.4（防护流程③⑤——预算开户先于打开、读取循环内检查点）
 *   - 需求 NFR-SEC-02（超限拒绝＋诊断）、PM-05（解包防护）
 *   - 任务契约 tasks/foundation/IO-T02.json acceptance 2/3（V06＋三要
 *     素诊断＋双保险＋账本可观测）
 *
 * 确定性要点（review 导览）：
 *   - 全部累加经饱和加法（不回绕——§4.5.2 溢出防护行）；超限判定在
 *     "先加后判"失败时**不提交**（后置"失败＝状态不变"§9.2）。
 *   - 比例比较用交叉相乘＋饱和乘法（expanded > ratioLimit × compressed
 *     等价变形，无除法——§4.5.2"不做除法上溢路径"）。
 *   - 诊断三要素 params 键固定 actual/limit/unit，数值以十进制定点文
 *     本化（不经 locale——IoError.hpp 确定性注释同源）。
 */

#include <sdurws/ird/io/Budget.hpp>

#include <sdurws/ird/io/IoDiagnostics.hpp>   // makeComparativeError——比较型三要素构造

#include <limits>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace sdurws::ird::io {
namespace {

// =====================================================================
// §4.5.1 数值表（P-IO-4 Draft 档位——修订走单元卡 §15.5，实现侧不私改）
// =====================================================================

/// 单维数值行（默认值＋硬上限——§4.5.1 表两列；超限码按维独立映射）。
struct DimSpec {
    std::uint64_t def;   ///< 产品默认值（单位随维——字节/计数/层级/比例分子）
    std::uint64_t hard;  ///< 硬上限（不可覆盖——charge 层强制）
};

/// §4.5.1 表逐维数值（数组下标＝BudgetDimension 枚举序——表行序；两序
/// 恒等的守卫见 productDefault 内 static_assert 之后的逐项注释）。
constexpr std::array<DimSpec, kBudgetDimensionCount> kDimTable = {{
    /* SingleFileBytes      */ {256ull * 1024 * 1024,      2ull * 1024 * 1024 * 1024},   // 256 MiB｜2 GiB
    /* TotalBytes           */ {2ull * 1024 * 1024 * 1024, 8ull * 1024 * 1024 * 1024},   // 2 GiB｜8 GiB
    /* FileCount            */ {200000,                    1000000},                     // 200,000｜1,000,000
    /* DirDepth             */ {32,                        64},                          // 32｜64
    /* ArchiveExpandedBytes */ {4ull * 1024 * 1024 * 1024, 16ull * 1024 * 1024 * 1024},  // 4 GiB｜16 GiB
    /* ArchiveRatio         */ {100,                       200},                         // 100:1｜200:1
    /* CsvRowCount          */ {5000000,                   50000000},                    // 5,000,000｜50,000,000
    /* CsvFieldChars        */ {64 * 1024,                 1024 * 1024},                 // 64 KiB｜1 MiB
    /* JsonDocBytes         */ {64ull * 1024 * 1024,       512ull * 1024 * 1024},        // 64 MiB｜512 MiB
    /* JsonDepth            */ {64,                        128},                         // 64｜128
    /* JsonStringChars      */ {16ull * 1024 * 1024,       64ull * 1024 * 1024},         // 16 MiB｜64 MiB
    /* MeshVertexCount      */ {20000000,                  100000000},                   // 20,000,000｜100,000,000
    /* MeshFaceCount        */ {40000000,                  200000000},                   // 40,000,000｜200,000,000
    /* IncludeDepth         */ {16,                        32},                          // 16｜32
    /* RefGraphDepth        */ {64,                        128},                         // 64｜128
    /* TempAreaBytes        */ {8ull * 1024 * 1024 * 1024, 32ull * 1024 * 1024 * 1024},  // 8 GiB｜硬=min(展开×2=32GiB, 磁盘−1GiB) 的编译期上界
}};

static_assert(kDimTable.size() == kBudgetDimensionCount, "§4.5.1 表 16 维——与枚举序恒等");
static_assert(static_cast<std::uint8_t>(BudgetDimension::TempAreaBytes) == 15,
              "枚举序＝§4.5.1 表行序（持久化契约面——IoErrorCode 同款纪律）");

/**
 * @brief 维度→超限稳定码映射（§4.5.1"超限码"列；诊断三要素经
 *        makeComparativeError 附着）。
 */
IoErrorCode overLimitCode(BudgetDimension dim)
{
    switch (dim) {
    case BudgetDimension::SingleFileBytes:      return IoErrorCode::SecBudgetFile;
    case BudgetDimension::TotalBytes:           return IoErrorCode::SecBudgetTotal;
    case BudgetDimension::FileCount:            return IoErrorCode::SecBudgetCount;
    case BudgetDimension::DirDepth:             return IoErrorCode::SecBudgetDepth;
    case BudgetDimension::ArchiveExpandedBytes: return IoErrorCode::SecBudgetExpand;
    case BudgetDimension::ArchiveRatio:         return IoErrorCode::SecBombRatio;
    case BudgetDimension::CsvRowCount:          return IoErrorCode::SecBudgetRows;
    case BudgetDimension::CsvFieldChars:        return IoErrorCode::SecBudgetField;
    case BudgetDimension::JsonDocBytes:         return IoErrorCode::SecBudgetJson;
    case BudgetDimension::JsonDepth:            return IoErrorCode::SecBudgetJsonDepth;
    case BudgetDimension::JsonStringChars:      return IoErrorCode::SecBudgetJsonString;
    case BudgetDimension::MeshVertexCount:      return IoErrorCode::SecBudgetMesh;
    case BudgetDimension::MeshFaceCount:        return IoErrorCode::SecBudgetMesh;
    case BudgetDimension::IncludeDepth:         return IoErrorCode::SecBudgetInclude;
    case BudgetDimension::RefGraphDepth:        return IoErrorCode::SecBudgetRefDepth;
    case BudgetDimension::TempAreaBytes:        return IoErrorCode::SecBudgetTemp;
    }
    return IoErrorCode::FormatInternal;    // 全枚举不可达——防御性返回
}

/// 维度索引（枚举→数组下标；内部承载表按此索引）。
std::size_t dimIndex(BudgetDimension dim)
{
    return static_cast<std::size_t>(dim);
}

/**
 * @brief 维度的诊断单位 token（三要素第三元——§4.5.2/diagnostics §8.6；
 *        稳定英文短语，参数是数据不是文案）。
 */
std::string_view dimUnit(BudgetDimension dim)
{
    switch (dim) {
    case BudgetDimension::SingleFileBytes:
    case BudgetDimension::TotalBytes:
    case BudgetDimension::ArchiveExpandedBytes:
    case BudgetDimension::JsonDocBytes:
    case BudgetDimension::TempAreaBytes:
        return "bytes";                 // 字节类
    case BudgetDimension::CsvFieldChars:
    case BudgetDimension::JsonStringChars:
        return "chars";                 // 字符数类
    case BudgetDimension::DirDepth:
    case BudgetDimension::JsonDepth:
    case BudgetDimension::IncludeDepth:
    case BudgetDimension::RefGraphDepth:
        return "levels";                // 层级类
    case BudgetDimension::ArchiveRatio:
        return "ratio";                 // 比较型：展开/压缩倍率
    default:
        return "count";                 // 计数类（FileCount/Rows/Mesh*/…）
    }
}

/// 饱和加法（§4.5.2"累计器为 uint64_t＋饱和加法"——永不回绕）。
std::uint64_t satAdd(std::uint64_t a, std::uint64_t b)
{
    return (a > std::numeric_limits<std::uint64_t>::max() - b) ? std::numeric_limits<std::uint64_t>::max()
                                                               : a + b;
}

/// 饱和乘法（比例比较的交叉相乘用——乘积超界时钳到 uint64 最大值）。
std::uint64_t satMul(std::uint64_t a, std::uint64_t b)
{
    if (a == 0 || b == 0) {
        return 0;
    }
    const std::uint64_t maxV = std::numeric_limits<std::uint64_t>::max();
    return (a > maxV / b) ? maxV : a * b;
}

// =====================================================================
// scope 与 guard 实现
// =====================================================================

/// scope 运行态（guard 内部承载——spec 拷贝＋各维累计＋树关系）。
/// 全部成员显式初始化（archiveCompressed 曾漏初始化——Release 堆垃圾使
/// 比例判定失效，测试实测暴露：不确定值不可作为判定输入，NFR-COR-01）。
struct ScopeState {
    BudgetSpec spec;                 ///< 开 scope 时的规格拷贝（此后与调用方无关）
    std::array<std::uint64_t, kBudgetDimensionCount> used{}; ///< 各维已入账（饱和；零初始化）
    std::uint64_t archiveCompressed = 0; ///< 压缩侧累计（ArchiveRatio 对侧——字节；零初始化）
    BudgetScopeId parent{};          ///< 父句柄（0＝根）
    bool open = true;                ///< 打开态（close 后 false；快照仍可取）
};

/**
 * @brief 预算守卫实现（§9.2 四方法＋chargeArchive）。
 *
 * 线程模型：单 scope 内 charge 非并发（§9.2 契约表）是**调用方约束**；
 * guard 内部仍以互斥锁保护映射与状态——使 ledger() 并发只读安全（同章
 * "ledger() 并发只读"）与跨 scope 的 open/close 并发安全。锁粒度＝整个
 * guard：预算检查点是调用方读取循环的每块/每行/每条目（§4.4⑤），锁开
 * 销相对文件 I/O 可忽略。
 */
class BudgetGuard final : public IBudgetGuard {
public:
    IoResult<BudgetScopeId> openScope(const BudgetSpec& spec, BudgetScopeId parent) override
    {
        IoResult<BudgetScopeId> out;
        std::lock_guard<std::mutex> lock(m_mutex);

        // ①规格整体校验：任一维限额超硬上限＝试图绕过安全上限——整体
        //   拒绝（不部分生效；§9.2"非法调用：charge 超过硬上限（即便
        //   relax 也不可逾越）"的 scope 侧防线）。
        for (std::size_t i = 0; i < kBudgetDimensionCount; ++i) {
            if (spec.m_limits[i] > kDimTable[i].hard) {
                out.error.code = IoErrorCode::FormatInternal;
                out.error.detail = "spec 限额超硬上限（维 " + std::to_string(i) + "）——硬上限不可逾越（§4.5.1）";
                return out;
            }
        }
        // ②父句柄校验：必须为打开态 scope（0＝开根 scope）。
        if (parent.value != 0) {
            const auto it = m_scopes.find(parent.value);
            if (it == m_scopes.end() || !it->second.open) {
                out.error.code = IoErrorCode::FormatInternal;
                out.error.detail = "parent scope 非法或已关闭（§9.2 前置：scope 处于打开态）";
                return out;
            }
        }
        // ③句柄顺序分配（1 起；0 保留为"无"）——回传前入库。
        const auto id = static_cast<std::uint64_t>(m_scopes.size() + 1);
        // 句柄耗尽防御：顺序分配复用已关闭槽位（erase 后 size+1 可能撞
        // 存活句柄）——线性探查下一个空闲号（实际会话规模远达不到）。
        std::uint64_t candidate = id;
        while (m_scopes.count(candidate) != 0) {
            ++candidate;
        }
        ScopeState st;
        st.spec = spec;                     // 值拷贝入库（§9.2"spec 值拷贝归调用方"的镜像义务）
        st.parent = parent;
        st.open = true;
        m_scopes.emplace(candidate, std::move(st));
        out.value.value = candidate;
        return out;
    }

    IoResult<void> charge(BudgetScopeId id, BudgetDimension dim, std::uint64_t amount) override
    {
        IoResult<void> out;
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_scopes.find(id.value);
        // ①句柄/状态检查（§9.2 前置：scope 打开态；非法→IO-FORMAT-INTERNAL）。
        if (it == m_scopes.end() || !it->second.open) {
            out.error.code = IoErrorCode::FormatInternal;
            out.error.detail = "scope 非法或已关闭（§9.2 前置）";
            return out;
        }
        // ②比较型维不可走单笔 charge（双侧语义——走 chargeArchive）。
        if (dim == BudgetDimension::ArchiveRatio) {
            out.error.code = IoErrorCode::FormatInternal;
            out.error.detail = "ArchiveRatio 为压缩/展开双侧比较维——须走 chargeArchive（§4.5.2）";
            return out;
        }
        ScopeState& sc = it->second;
        const std::size_t i = dimIndex(dim);
        // ③预检：饱和入账后是否超该维限额——超限即拒且**不提交**（后
        //   置"失败＝状态不变，本笔不入账"§9.2；比较型三要素诊断——
        //   actual=潜在累计值/limit=生效限额/unit=维度单位，§4.5.2）。
        const std::uint64_t projected = satAdd(sc.used[i], amount);
        if (projected > sc.spec.m_limits[i]) {
            out.error = makeComparativeError(overLimitCode(dim), projected, sc.spec.m_limits[i], dimUnit(dim),
                                             "预算超限（维 " + std::to_string(i) + "）——§4.5.2 三要素");
            return out;
        }
        sc.used[i] = projected;             // 成功＝入账
        return out;
    }

    IoResult<void> chargeArchive(BudgetScopeId id, std::uint64_t compressedDelta,
                                 std::uint64_t expandedDelta) override
    {
        IoResult<void> out;
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_scopes.find(id.value);
        if (it == m_scopes.end() || !it->second.open) {
            out.error.code = IoErrorCode::FormatInternal;
            out.error.detail = "scope 非法或已关闭（§9.2 前置）";
            return out;
        }
        ScopeState& sc = it->second;
        const std::size_t expIdx = dimIndex(BudgetDimension::ArchiveExpandedBytes);

        // ①展开总量检查（ArchiveExpandedBytes 维——超→IO-SEC-BUDGET-EXPAND，
        //   状态不变：压缩侧同样不入账，调用方整体中止）。
        const std::uint64_t newExpanded = satAdd(sc.used[expIdx], expandedDelta);
        if (newExpanded > sc.spec.m_limits[expIdx]) {
            out.error = makeComparativeError(IoErrorCode::SecBudgetExpand, newExpanded,
                                             sc.spec.m_limits[expIdx], dimUnit(BudgetDimension::ArchiveExpandedBytes),
                                             "展开总量超限——§4.5.1 ArchiveExpandedBytes");
            return out;
        }
        // ②比例检查（交叉相乘比较，无除法——§4.5.2）：
        //   newExpanded > ratioLimit × newCompressed → 炸弹。
        //   ratioLimit＝spec（默认 100，relax 后 200）；newCompressed＝0
        //   且 newExpanded>0＝比例无穷大（防除零式逃逸——zip 条目声明
        //   压缩大小为 0 的欺骗形态）。
        const std::uint64_t newCompressed = satAdd(sc.archiveCompressed, compressedDelta);
        const std::uint64_t ratioLimit = sc.spec.m_limits[dimIndex(BudgetDimension::ArchiveRatio)];
        const std::uint64_t expandedBound = satMul(ratioLimit, newCompressed);
        if (newExpanded > expandedBound || (newCompressed == 0 && newExpanded > 0)) {
            // 三要素：actual＝展开/压缩比值不可整除时以两侧原值入 params
            // ——actual 取展开量、limit 取比例上限×压缩量的展开当量（比
            // 较型同量纲），unit=ratio；两侧原值进 detail 供开发级定位。
            out.error = makeComparativeError(IoErrorCode::SecBombRatio, newExpanded, expandedBound, "bytes",
                                             "压缩比超限：expanded=" + std::to_string(newExpanded)
                                                 + " compressed=" + std::to_string(newCompressed)
                                                 + " ratioLimit=" + std::to_string(ratioLimit) + ":1——§4.5.1");
            return out;
        }
        // ③双侧通过→同时入账（成功路径唯一提交点）。
        sc.used[expIdx] = newExpanded;
        sc.archiveCompressed = newCompressed;
        return out;
    }

    IoResult<void> closeScope(BudgetScopeId id) override
    {
        IoResult<void> out;
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_scopes.find(id.value);
        if (it == m_scopes.end() || !it->second.open) {
            out.error.code = IoErrorCode::FormatInternal;
            out.error.detail = "scope 非法或已关闭（§9.2 前置：子 scope 先于父关闭）";
            return out;
        }
        ScopeState& sc = it->second;

        // 余额回收至父（§9.2 closeScope 注释；§4.5.2 多阶段累计行）：
        // 语义取"子的已用量逐维 charge 到父"——多阶段累计共享额度：若
        // 子消耗不反映到父，N 个子 scope 各自开满即绕过会话总量上限
        //（父限额按父 spec 判定，父也因此获得三要素诊断的定位点）。
        // ArchiveRatio 不向上回收（比较型维是 per-archive 事实，不是
        // 累计量——父的比例判定由父自己 chargeArchive 承载）。
        const auto parentIt = sc.parent.value == 0 ? m_scopes.end() : m_scopes.find(sc.parent.value);
        if (sc.parent.value != 0 && (parentIt == m_scopes.end() || !parentIt->second.open)) {
            out.error.code = IoErrorCode::FormatInternal;
            out.error.detail = "父 scope 已关闭/非法——须先关子后关父（§9.2 前置）";
            return out;
        }

        // ①预演父回收：任一维超父限额→整笔回收失败、父子状态均不变
        //   （子仍打开——调用方按预算失败中止会话；确定性：预演与真实
        //   提交同一投影计算）。
        if (parentIt != m_scopes.end()) {
            ScopeState& parent = parentIt->second;
            for (std::size_t i = 0; i < kBudgetDimensionCount; ++i) {
                if (i == dimIndex(BudgetDimension::ArchiveRatio)) {
                    continue;           // 比较型维不回收（见上）
                }
                const std::uint64_t projected = satAdd(parent.used[i], sc.used[i]);
                if (projected > parent.spec.m_limits[i]) {
                    const auto dim = static_cast<BudgetDimension>(i);
                    out.error = makeComparativeError(overLimitCode(dim), projected, parent.spec.m_limits[i],
                                                     dimUnit(dim),
                                                     "子 scope 回收至父触发父超限——§4.5.2 多阶段累计");
                    return out;         // 状态不变（未提交任何回写）
                }
            }
            // ②真实提交（预演已保证不超限）。
            for (std::size_t i = 0; i < kBudgetDimensionCount; ++i) {
                if (i == dimIndex(BudgetDimension::ArchiveRatio)) {
                    continue;
                }
                parent.used[i] = satAdd(parent.used[i], sc.used[i]);
            }
        }
        // ③关闭子 scope（关闭后句柄仍可 ledger 取终值快照——诊断定位）。
        sc.open = false;
        return out;
    }

    BudgetLedgerSnapshot ledger(BudgetScopeId id) const override
    {
        // 并发只读安全（§9.2 契约表）——与 charge/close 共锁保证快照
        // 一致性；快照是取值瞬间拷贝，后续 charge 不影响已取快照。
        std::lock_guard<std::mutex> lock(m_mutex);
        BudgetLedgerSnapshot snap;
        snap.open = false;
        snap.archiveCompressedBytes = 0;
        const auto it = m_scopes.find(id.value);
        if (it == m_scopes.end()) {
            return snap;                // 非法句柄：open=false 空快照（查询非抛）
        }
        const ScopeState& sc = it->second;
        snap.open = sc.open;
        for (std::size_t i = 0; i < kBudgetDimensionCount; ++i) {
            const auto dim = static_cast<BudgetDimension>(i);
            BudgetDimensionState ds;
            ds.used = sc.used[i];
            ds.limit = sc.spec.m_limits[i];
            ds.relaxed = sc.spec.isRelaxed(dim);
            snap.dimensions.emplace_back(dim, ds);
        }
        snap.archiveCompressedBytes = sc.archiveCompressed;
        return snap;
    }

private:
    /// 全部 scope（句柄→状态；unordered_map——句柄是无语义键，观测面
    /// 的稳定序由 ledger 的声明序遍历承载，不经容器序）。
    std::unordered_map<std::uint64_t, ScopeState> m_scopes;
    /// 守卫级互斥（见类注释线程模型）。mutable：ledger() 为 const 观测
    /// 路径，加锁所需（快照一致性——与 charge/close 共锁）。
    mutable std::mutex m_mutex;
};

} // namespace

// =====================================================================
// BudgetSpec 成员（数值表定稿落位——头内联声明、此处定义）
// =====================================================================

BudgetSpec BudgetSpec::productDefault()
{
    BudgetSpec s;
    for (std::size_t i = 0; i < kBudgetDimensionCount; ++i) {
        s.m_limits[i] = kDimTable[i].def;
        s.m_relaxed[i] = false;
        s.m_relaxForbidden[i] = false;
    }
    return s;
}

BudgetSpec BudgetSpec::packImportHardened()
{
    // §4.5.2"包导入 vs 普通读取"行：ArchiveExpandedBytes/ArchiveRatio/
    // FileCount/DirDepth 四维强制全开且不可放宽（zip 炸弹主战场）。
    BudgetSpec s = productDefault();
    s.m_relaxForbidden[dimIndex(BudgetDimension::ArchiveExpandedBytes)] = true;
    s.m_relaxForbidden[dimIndex(BudgetDimension::ArchiveRatio)] = true;
    s.m_relaxForbidden[dimIndex(BudgetDimension::FileCount)] = true;
    s.m_relaxForbidden[dimIndex(BudgetDimension::DirDepth)] = true;
    return s;
}

BudgetSpec& BudgetSpec::tighten(BudgetDimension dim, std::uint64_t v)
{
    const std::size_t i = dimIndex(dim);
    if (v > m_limits[i]) {
        // 调用方契约违约：tighten 只收紧（§4.5.2）——放宽语义必须走显
        // 式 relaxToHardLimit（导入报告留痕）。静默钳位会掩盖调用方
        // bug（AGENTS.md：调用方错误 fail-fast；头注释同源说明）。
        throw std::invalid_argument("BudgetSpec::tighten 收到大于当前限额的值——收紧语义违约（§4.5.2）");
    }
    m_limits[i] = v;
    return *this;
}

BudgetSpec& BudgetSpec::relaxToHardLimit(BudgetDimension dim)
{
    const std::size_t i = dimIndex(dim);
    if (m_relaxForbidden[i]) {
        // 包导入通道禁用放宽（§4.5.2；isHardLimited 判定源 §9.2）——
        // 对四维调用放宽＝契约违约，fail-fast 而非静默不变。
        throw std::invalid_argument("BudgetSpec::relaxToHardLimit 对放宽禁用维（包导入四维）调用——§4.5.2 违约");
    }
    m_limits[i] = kDimTable[i].hard;    // 放宽只到硬上限（§4.5.2）
    m_relaxed[i] = true;                // 显式标记——导入报告记录放宽项
    return *this;
}

bool BudgetSpec::isHardLimited(BudgetDimension dim) const
{
    return m_relaxForbidden[dimIndex(dim)];
}

std::uint64_t BudgetSpec::limit(BudgetDimension dim) const
{
    return m_limits[dimIndex(dim)];
}

bool BudgetSpec::isRelaxed(BudgetDimension dim) const
{
    return m_relaxed[dimIndex(dim)];
}

IBudgetGuardPtr makeBudgetGuard()
{
    return std::make_shared<BudgetGuard>();
}

} // namespace sdurws::ird::io
