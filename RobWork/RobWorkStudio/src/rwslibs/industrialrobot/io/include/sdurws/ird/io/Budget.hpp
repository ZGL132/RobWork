/**
 * @file   Budget.hpp
 * @brief  BudgetGuard——资源预算的维度表、规格（默认/硬上限/收紧放宽）、
 *         scope 树记账与可观测账本（BudgetLedger）。
 *
 * 设计依据：
 *   - units/io.md §4.4（SafePath 与 BudgetGuard 防护流程——②在③前、③在
 *     ④前；检查点即取消点）、§4.5.1（预算维度表 16 维——默认值/硬上限/
 *     超限码）、§4.5.2（语义细则：默认值来源/调用方覆盖/比较型三要素/
 *     溢出防护/多阶段累计/包导入四维强制）、§9.2（IBudgetGuard 接口契约
 *     ——签名建议与行为契约表）、§3.1（公共头表 Budget.hpp 行）
 *   - 需求 NFR-SEC-02（资源预算：单文件大小、递归深度、解压总量上限，
 *     超限拒绝＋诊断）、PM-05（解包防护维度——DTB WP-11-T03 需求列）
 *   - 任务契约 tasks/foundation/IO-T02.json acceptance 2/3/4/5
 *
 * 背景说明（预算为什么归 io 而不是 policy——§4.5.2/IO-D06）：预算是**资
 * 源安全设施**（防大文件打开成为攻击面、防 zip 炸弹），不是工程判定阈值
 * （EngineeringPolicySet＝碰撞/判定阈值等工程语义，ARCH §7.5）；SA-14
 * 归 io。默认值表为 io 常量（BudgetSpec::productDefault()），NFR-SEC-02
 * 只要求"上限存在"未定数值——本实现按 §4.5.1 建议默认（Draft 档位，
 * P-IO-4：架构评审确认前不私改数值；修订走单元卡 §15.5 变更记录）。
 *
 * 硬上限纪律：调用方只能收紧（tighten，≤当前值）或经显式
 * relaxToHardLimit 放宽**至硬上限**；硬上限在 charge 层强制（"超限即便
 * relax 也不可逾越"——§9.2 非法调用行）；包导入通道对
 * ArchiveExpandedBytes/ArchiveRatio/FileCount/DirDepth 四维禁用放宽
 * （§4.5.2，isHardLimited 为其判定源——§9.2）。
 *
 * 线程安全：单 scope 内操作非并发（单线程读取流程——§9.2 契约表）；
 * guard 对象整体（openScope/跨 scope 的 ledger）内部加锁，ledger() 可
 * 并发只读。确定性：饱和算法（§4.5.2）——同序列 charge 同判定。
 * 生命周期：guard 会话级（随导入/固化会话创建销毁——§9.2）；spec 值
 * 拷贝归调用方。
 */

#ifndef SDURWS_IRD_IO_BUDGET_HPP
#define SDURWS_IRD_IO_BUDGET_HPP

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <sdurws/ird/io/IoFwd.hpp>   // IoResult——接口返回容器

namespace sdurws::ird::io {

// =====================================================================
// BudgetDimension（§4.5.1 预算维度表——16 维，枚举序＝表行序）
// =====================================================================

/**
 * @brief 预算维度（§4.5.1 表 16 维；枚举顺序＝表行序——持久化契约面，
 *        只允许表尾追加并走单元卡增量修订）。
 *
 * 计量单位分三类（诊断三要素的 unit 取值——§4.5.2/diagnostics §8.6）：
 * 字节类（*Bytes）、计数类（*Count/*Rows/顶点面数）、层级类（*Depth）；
 * ArchiveRatio 为比较型特殊维（压缩侧与展开侧分别记账后比较——不做
 * 除法上溢路径，§4.5.2 溢出防护行）。
 */
enum class BudgetDimension : std::uint8_t {
    SingleFileBytes,      ///< 单文件读取/展开大小（默认 256 MiB｜硬 2 GiB｜IO-SEC-BUDGET-FILE）
    TotalBytes,           ///< 单次导入/读取会话累计字节（默认 2 GiB｜硬 8 GiB｜IO-SEC-BUDGET-TOTAL）
    FileCount,            ///< 文件数（包/目录/依赖树）（默认 200,000｜硬 1,000,000｜IO-SEC-BUDGET-COUNT）
    DirDepth,             ///< 目录深度（默认 32｜硬 64｜IO-SEC-BUDGET-DEPTH）
    ArchiveExpandedBytes, ///< 压缩包展开总量（默认 4 GiB｜硬 16 GiB｜IO-SEC-BUDGET-EXPAND）
    ArchiveRatio,         ///< 压缩比 展开/压缩（默认 100:1｜硬 200:1｜IO-SEC-BOMB-RATIO）
    CsvRowCount,          ///< CSV 行数（默认 5,000,000｜硬 50,000,000｜IO-SEC-BUDGET-ROWS）
    CsvFieldChars,        ///< CSV 单字段字符数（默认 64 KiB｜硬 1 MiB｜IO-SEC-BUDGET-FIELD）
    JsonDocBytes,         ///< JSON 文档大小（默认 64 MiB｜硬 512 MiB｜IO-SEC-BUDGET-JSON）
    JsonDepth,            ///< JSON 嵌套深度（默认 64｜硬 128｜IO-SEC-BUDGET-JSON-DEPTH）
    JsonStringChars,      ///< JSON 单字符串长度（默认 16 MiB｜硬 64 MiB｜IO-SEC-BUDGET-JSON-STRING）
    MeshVertexCount,      ///< 网格顶点数（默认 20,000,000｜硬 100,000,000｜IO-SEC-BUDGET-MESH）
    MeshFaceCount,        ///< 网格面数（默认 40,000,000｜硬 200,000,000｜IO-SEC-BUDGET-MESH）
    IncludeDepth,         ///< XML include/Xacro 递归深度（默认 16｜硬 32｜IO-SEC-BUDGET-INCLUDE）
    RefGraphDepth,        ///< 资源引用图深度/循环检测上限（默认 64｜硬 128｜IO-SEC-BUDGET-REFDEPTH）
    TempAreaBytes,        ///< 临时区占用（默认 8 GiB｜硬＝min(展开预算×2, 可用磁盘−1 GiB)｜IO-SEC-BUDGET-TEMP）
};

/// 维度总数（数组尺寸锚点——遍历/账本容量）。
inline constexpr std::size_t kBudgetDimensionCount = 16;

// =====================================================================
// BudgetSpec（§9.2——§4.5 数值表的运行时形态）
// =====================================================================

/**
 * @brief 预算规格：16 维限额＋放宽状态＋放宽禁用标志（§9.2 原文成员
 *        productDefault/tighten/relaxToHardLimit/isHardLimited）。
 *
 * 值语义（§9.2 契约表"BudgetSpec 值拷贝归调用方"）；openScope 时整体
 * 拷贝入 scope，此后调用方对 spec 的修改不影响已开 scope。
 *
 * 调用方错误语义（AGENTS.md：调用方错误 fail-fast）：tighten 传入大于
 * 当前值、对放宽禁用维调用 relaxToHardLimit 属**调用方契约违约**——
 * 本类以 std::invalid_argument fail-fast（值类型方法无错误返回轨道；
 * io 的"对外接口一律非抛出"约束针对文件/解析操作——§1.4，规格准备是
 * 装配期纯计算，编程错误静默吞掉比抛出更危险）。经 openScope 还有一道
 * 防御校验（值>硬上限→IO-FORMAT-INTERNAL），两层互为纵深。
 */
struct BudgetSpec {
    /**
     * @brief 产品默认规格（§4.5.1 表"默认值"列逐维落位——P-IO-4 Draft
     *        档位；测试逐项钉住，架构评审确认前不私改）。
     */
    static BudgetSpec productDefault();

    /**
     * @brief 包导入强化规格（§4.5.2"包导入 vs 普通读取"行：默认值之上
     *        对 ArchiveExpandedBytes/ArchiveRatio/FileCount/DirDepth 四
     *        维禁用放宽——isHardLimited 四维全真，§9.2"合法调用"行）。
     */
    static BudgetSpec packImportHardened();

    /**
     * @brief 收紧某维限额（只收紧，永远合法——§4.5.2；v＞当前值＝契约
     *        违约，抛 std::invalid_argument）。
     * @param dim [in] 目标维度
     * @param v   [in] 新限额（单位随维度：字节/计数/层级——见
     *            BudgetDimension 注释）
     * @return *this（流式链式收紧多维）
     */
    BudgetSpec& tighten(BudgetDimension dim, std::uint64_t v);

    /**
     * @brief 显式放宽至硬上限（§4.5.2"显式标记，导入报告记录放宽项"）。
     *
     * 放宽只到硬上限（不是无限）；对 isHardLimited(dim)==true（放宽禁
     * 用——包导入四维）的维度调用＝契约违约，抛 std::invalid_argument。
     */
    BudgetSpec& relaxToHardLimit(BudgetDimension dim);

    /// 放宽禁用判定（§9.2"包导入通道禁用放宽的判定源"）。
    bool isHardLimited(BudgetDimension dim) const;

    /// 当前限额（tighten/relax 后的生效值）。
    std::uint64_t limit(BudgetDimension dim) const;

    /// 该维是否已被放宽至硬上限（导入报告"放宽项"清单的判定面——§4.5.2）。
    bool isRelaxed(BudgetDimension dim) const;

    // -----------------------------------------------------------------
    // 内部承载（公共头内联值语义——三张 16 元素定长表，构造后经方法修改）
    // -----------------------------------------------------------------
    std::array<std::uint64_t, kBudgetDimensionCount> m_limits{};   ///< 各维生效限额
    std::array<bool, kBudgetDimensionCount> m_relaxed{};           ///< 各维是否已放宽至硬上限
    std::array<bool, kBudgetDimensionCount> m_relaxForbidden{};    ///< 各维是否禁用放宽（包导入四维）
};

// =====================================================================
// BudgetScopeId / BudgetLedgerSnapshot（§9.2 scope 句柄与账本快照）
// =====================================================================

/**
 * @brief scope 句柄（§9.2 openScope 返回值；值语义，调用方持有并在
 *        charge/ledger/closeScope 中回传）。
 *
 * 0 为"无 scope"保留值（树根父句柄用 0 表达）；有效句柄自 1 起由 guard
 * 顺序分配。句柄不携带 scope 存活期保证——对已关闭/非法句柄的操作返回
 * IO-FORMAT-INTERNAL（§9.2 错误类型行）。
 */
struct BudgetScopeId {
    std::uint64_t value = 0;    ///< 0＝无/非法；≥1＝guard 分配的有效句柄

    bool operator==(const BudgetScopeId& o) const noexcept { return value == o.value; }
    bool operator!=(const BudgetScopeId& o) const noexcept { return value != o.value; }
};

/// 单维账本状态（ledger 快照行）。
struct BudgetDimensionState {
    std::uint64_t used;    ///< 已入账累计（饱和加法——永不回退、永不回绕）
    std::uint64_t limit;   ///< 该 scope 生效限额（spec 拷贝）
    bool relaxed;          ///< 该维是否放宽至硬上限（导入报告面）
};

/**
 * @brief 账本快照（§9.2 ledger() 返回——诊断用可观测面，§4.4"BudgetLedger
 *        可观测"）。
 *
 * dimensions 按维度声明序排列（确定性——NFR-COR-02）；快照是取值瞬间
 * 的一致性拷贝，随后的 charge 不影响已取快照。archiveCompressedBytes
 * 为压缩侧累计（ArchiveRatio 比较型维的对侧——§4.5.2"压缩侧与展开侧
 * 分别记录后比较"）。
 */
struct BudgetLedgerSnapshot {
    bool open;                                                      ///< scope 是否处于打开态
    std::vector<std::pair<BudgetDimension, BudgetDimensionState>> dimensions; ///< 按声明序
    std::uint64_t archiveCompressedBytes;                           ///< 压缩侧累计（字节）
};

// =====================================================================
// IBudgetGuard（§9.2 接口契约——签名承载＋必要的等价调整）
// =====================================================================

/**
 * @brief 预算守卫接口（§9.2 原文四方法＋两处登记过的等价调整）。
 *
 * 等价调整（§9.0"签名均为实现建议……实现期允许等价调整，语义不变"）：
 *   1. openScope 增加带默认值的 parent 参数（§4.5.2"多阶段累计：父
 *      scope 关闭前子 scope 余额回收"要求 scope 成树；§9.2 原签名无父
 *      句柄无法成树——补默认参数保持原调用形态可用）。
 *   2. 增补 chargeArchive 方法承载 ArchiveRatio 双侧记账（§4.5.2"压缩
 *      侧与展开侧分别记录后比较"；单一 amount 的 charge 无法同时表达
 *      两侧——原签名的 charge 对 ArchiveRatio 维显式拒绝）。
 *
 * 行为契约（§9.2 契约表，逐行冻结）：
 *   - 前置：amount≥0（无符号类型天然满足）；scope 处于打开态；子 scope
 *     先于父关闭。
 *   - 后置：charge 成功＝该维累计入账；失败＝**状态不变**（本笔不入账，
 *     整体由调用方中止——调用方"超限即中止＋诊断"§4.4⑤）。
 *   - 错误：IO-SEC-BUDGET-*（各维超限码映射——§4.5.1）/IO-SEC-BOMB-RATIO
 *     （比较型三要素 params：actual/limit/unit——diagnostics §8.6）；
 *     IO-FORMAT-INTERNAL（非法 scope/ArchiveRatio 误走 charge）。
 *   - 无内建取消（检查点由读取流程在 charge 前后插入——§4.4③⑤）。
 *   - 副作用：无 I/O；超限诊断经调用方映射 sink 上报。
 */
class IBudgetGuard {
public:
    virtual ~IBudgetGuard() = default;

    /**
     * @brief 开立预算 scope（§9.2；spec 整体校验后拷贝——值非法即拒，
     *        不部分生效）。
     *
     * 校验：各维限额 ≤ 硬上限（超过＝调用方试图绕过硬上限→
     * IO-FORMAT-INTERNAL）；parent 必须为打开态 scope（否则
     * IO-FORMAT-INTERNAL）。
     *
     * @param spec   [in] 预算规格（值拷贝入库，此后与调用方无关）
     * @param parent [in] 父 scope（缺省 0＝根 scope；多阶段累计时子
     *               scope 挂父——closeScope 时子账回收至父）
     * @return 成功＝scope 句柄；失败＝IO-FORMAT-INTERNAL（spec/parent 非法）
     */
    virtual IoResult<BudgetScopeId> openScope(const BudgetSpec& spec, BudgetScopeId parent = {}) = 0;

    /**
     * @brief 入账一笔用量（§9.2 原文签名；饱和加法，超限即拒且状态不变）。
     *
     * @param id     [in] scope 句柄（必须处于打开态）
     * @param dim    [in] 维度（ArchiveRatio 不可走此方法——比较型双侧
     *               维，走 chargeArchive；误用→IO-FORMAT-INTERNAL）
     * @param amount [in] 本笔用量（单位随维度；一次 charge 一笔，检查
     *               点粒度由调用方定——§4.4⑤每块/每行/每条目）
     * @return 成功＝已入账；失败＝对应 IO-SEC-BUDGET-*（含三要素
     *         params）或 IO-FORMAT-INTERNAL
     */
    virtual IoResult<void> charge(BudgetScopeId id, BudgetDimension dim, std::uint64_t amount) = 0;

    /**
     * @brief 压缩包双侧重账（等价调整——§4.5.2 ArchiveRatio 行）。
     *
     * 语义：压缩侧累计 compressedDelta；展开侧先按 ArchiveExpandedBytes
     * 检查（超→IO-SEC-BUDGET-EXPAND，状态不变），再按比例检查
     * （展开总量 > 比例限额 × 压缩总量——饱和乘法比较，无除法→
     * IO-SEC-BOMB-RATIO，状态不变），全部通过才双侧入账。压缩总量为 0
     * 且展开量＞0＝比例无穷大→IO-SEC-BOMB-RATIO（防除零式逃逸）。
     *
     * @param id              [in] scope 句柄（打开态）
     * @param compressedDelta [in] 本笔压缩输入字节（zip 条目压缩后字节）
     * @param expandedDelta   [in] 本笔展开字节（声明大小与实际展开双重
     *                        计数以较大者入账的口径由调用方分两笔实现：
     *                        预检笔＋展开补差笔——§4.5.2/§4.4 双保险）
     * @return 成功＝双侧入账；失败＝IO-SEC-BUDGET-EXPAND/-RATIO（状态不变）
     */
    virtual IoResult<void> chargeArchive(BudgetScopeId id, std::uint64_t compressedDelta,
                                         std::uint64_t expandedDelta) = 0;

    /**
     * @brief 关闭 scope 并把本 scope 各维累计回收至父（§9.2"余额回收至
     *        父"；§4.5.2"父 scope 关闭前子 scope 余额回收"）。
     *
     * 回收语义：子的**已用量**（非余额）逐维 charge 到父（父限额按父
     * spec 判定——多阶段累计共享额度：子 scope 消耗必须反映到父会话总
     * 量，否则子 scope 各自开满即绕过会话总量上限）。父因此超限时
     * closeScope 返回该维预算错误且**父子状态均不变**（子仍打开——
     * 调用方按失败处理整个读取会话，§4.4"超限即中止"）。回收后子 scope
     * 置关闭态；重复 close＝非法 scope（IO-FORMAT-INTERNAL）。
     *
     * @param id [in] 待关闭 scope（必须打开；根/子皆可）
     * @return 成功＝已回收并关闭；失败＝IO-SEC-BUDGET-*（父超限，状态
     *         不变）或 IO-FORMAT-INTERNAL（句柄非法/已关闭）
     */
    virtual IoResult<void> closeScope(BudgetScopeId id) = 0;

    /**
     * @brief 账本快照（§9.2 原文签名；诊断用可观测面，并发只读安全）。
     *
     * @param id [in] scope 句柄（关闭后的 scope 仍可取快照——终值观
     *             测/诊断定位；非法句柄→空快照 open=false）
     */
    virtual BudgetLedgerSnapshot ledger(BudgetScopeId id) const = 0;
};

/// 守卫指针别名（§9.11 访问器 budgetFactory() 的产物形态；guard 实例
/// 会话级——随导入/固化会话创建销毁，§9.2 契约表）。
using IBudgetGuardPtr = std::shared_ptr<IBudgetGuard>;

/**
 * @brief 创建预算守卫（IoRuntime 装配入口的实现侧工厂——§9.11
 *        budgetFactory() 访问器产物；进程内可创建多个会话级实例）。
 */
IBudgetGuardPtr makeBudgetGuard();

} // namespace sdurws::ird::io

#endif // SDURWS_IRD_IO_BUDGET_HPP
