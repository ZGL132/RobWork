/**
 * @file   SafePath.hpp
 * @brief  SafePath——路径角色模型、Windows 路径规范化与穿越/reparse point
 *         逃逸防护（含等价类键与会话互斥原语）。
 *
 * 设计依据：
 *   - units/io.md §4.1（七类路径角色）、§4.2（Windows 路径规范化：规范化
 *     算法/UNC 与 .. 处置/symlink·junction/等价类键/错误四分类）、§4.3
 *     （SafePath 规则总表 SP-1~SP-10、NormalizedPath、合法/非法示例表）、
 *     §9.1（ISafePathResolver 接口契约）、§4.2.4＋§9.10（TempArea 会话
 *     互斥**经等价键**——本头 EquivKeyMutex 为该互斥原语）、§3.1（公共头
 *     表 SafePath.hpp 行）
 *   - 需求 NFR-SEC-01（项目内持久化资源引用与项目包解包成员不得逃逸项目
 *     资源区；用户显式选择的外部源一次性读取**不在此限**）、NFR-MNT-01
 *     （零 Qt——本头纯标准库）
 *   - 任务契约 tasks/foundation/IO-T02.json（≙WP-11-T03）acceptance 1/4/5
 *
 * 背景说明（为什么路径安全必须集中在 io——SA-14 统一入口防护）：所有外部
 * 输入通道（CSV/JSON/资源/目录包/.rwpack）统一经 io 的 SafePath＋
 * BudgetGuard 防护，防护设施不得分散到各导入通道。管辖范围＝**P-4 包内
 * 成员＋P-5 项目内持久化资源引用**（SP-1，ARCH §6.6 原文口径）；P-1 用户
 * 显式选择的一次性读取为例外——仍受预算管辖，但不施加资源区逃逸检查
 * （NFR-SEC-01 明文例外，见 acceptance 4"例外不扩大化"）。路径**不作
 * 身份**（SP-5/§1.3 目标 3）：本头全部输出（规范化路径/等价键/相对键）
 * 仅用于寻址、去重与互斥，绝不充当对象身份——身份由 core/project 分配。
 *
 * 与 project 的关系（P-IO-1 处置锚点）：SafePath 对 project 的消费形态＝
 * IoRuntime 注入（§9.11）——本头仅以 std::filesystem::path 接收"解析基点"
 * （base），不引用任何 project 类型；P-IO-1 裁决结果（补登直连边或维持
 * 注入式）均不需要改动本头（契约 acceptance 5）。
 *
 * Windows 语义（§4.2，按 Microsoft Learn 公开口径）：NTFS/ReFS 大小写
 * 不敏感——等价类键做大小写折叠（A-Z→a-z，不做本地化折叠）；段尾点与
 * 空白被文件系统剥离——P-4/P-5 角色对含此形态的输入直接拒绝（防"写入名
 * ≠引用名"的等价分裂）；保留名（CON/PRN/AUX/NUL/COM1-9/LPT1-9）对
 * P-4/P-5 拒绝；符号链接/junction/其他 reparse point 经 Win32
 * FILE_ATTRIBUTE_REPARSE_POINT 逐段复核（std::filesystem 在 MSVC 侧不把
 * junction 呈现为 symlink——必须用 Win32 属性补检）。
 *
 * 线程安全：ISafePathResolver 实现为无状态并发只读安全（规则集构造后
 * 不可变——§9.1）；EquivKeyMutex 内部加锁，acquire/release 并发安全。
 * 确定性（NFR-COR-01/02）：同输入同结果；等价键仅依赖路径字符串与
 * canonical（canonical 失败时降级口径一致——§4.2.4 退化为折叠键）；集合
 * 输出按稳定序。取消行为：纯计算＋只读文件系统元数据访问，无取消点
 * （§9.1 契约表）。
 */

#ifndef SDURWS_IRD_IO_SAFEPATH_HPP
#define SDURWS_IRD_IO_SAFEPATH_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>

#include <sdurws/ird/io/IoFwd.hpp>   // IoResult/IoString——接口返回容器与 UTF-8 别名

namespace sdurws::ird::io {

// =====================================================================
// PathRole（§4.1 路径角色模型——七类）
// =====================================================================

/**
 * @brief 路径角色：同一条物理路径在不同角色下适用不同校验强度（§4.1）。
 *
 * 角色由调用方在获取 resolver 时声明，io **不从路径形态推断角色**（§4.1
 * 原文）。枚举顺序＝§4.1 表行序（持久化契约面纪律，只允许表尾追加——
 * IoErrorCode 同款）。
 */
enum class PathRole : std::uint8_t {
    UserSource,         ///< P-1 用户提供的源路径：准入校验（存在性等），不施加资源区逃逸检查（NFR-SEC-01 例外）
    ProjectRoot,        ///< P-2 项目根路径：规范化＋等价类归一（所有权归 project，io 仅接收）
    ImportStaging,      ///< P-3 导入临时根：io 全权管理（TempArea，IO-T06 落位；发布＝同卷 rename）
    PackEntry,          ///< P-4 包内相对路径：最严校验（纯相对/正斜杠/无根成分/无 ..、./非法字符）
    ProjectResourceRef, ///< P-5 项目内持久化资源引用：不得逃逸资源区（objects/、catalog/），逐段无 reparse point
    StagingTmp,         ///< P-6 固化/缓存中转路径：仅经 project 授权的 .staging/tmp/ 中转位
    ExportTarget,       ///< P-7 导出目标路径：可写性预检＋原子替换归 IAtomicFileWriter（IO-T06）；此处仅规范化
};

// =====================================================================
// NormalizedPath（§4.3.2 解析结果——字段与语义逐字承载）
// =====================================================================

/**
 * @brief 规范化结果（§4.3.2 原文字段：native/display/relKey/equivKey/
 *        isUnc/fileSize）。
 *
 * 生命周期/所有权：纯值类型，随 IoResult 按值返回，调用方所有。
 * 敏感性（NFR-SEC-07/§10.3）：display 为脱敏呈现形式（UTF-8、去除
 * \\?\ 前缀），可进入诊断构造链；native 为内部处理形式（含 \\?\ 扩展
 * 前缀），**不得**直接进入用户可见文案。SP-5：本结构不产出任何对象 ID
 * ——路径不作身份。
 */
struct NormalizedPath {
    std::wstring native;    ///< 规范化原生形式（内部处理统一 \\?\ 扩展前缀——§4.2.1 步骤 5）
    std::string  display;   ///< 脱敏呈现形式（UTF-8；去除 \\?\——诊断用）
    std::string  relKey;    ///< P-4/P-5：管辖根内的相对键（正斜杠、小写折叠——重复条目检测/缓存键）
    std::string  equivKey;  ///< 等价类键（大小写折叠＋词法规范化＋weakly_canonical 实路径——§4.2.4）
    bool         isUnc;     ///< 是否 UNC 路径（\\server\share\… 或 \\?\UNC\… 形态）
    /// 目标存在时的文件大小（提示性，单位字节；不作身份；不可得/非常规
    /// 文件为 nullopt——§4.3.2 "若目标存在（提示性；不作身份）"）。
    std::optional<std::uint64_t> fileSize;
};

// =====================================================================
// SafePathRuleSet（§3.1 公共头表 SafePath.hpp 行登记的规则集实体；
// §9.1 "规则集构造后不可变"的运行时形态）
// =====================================================================

/**
 * @brief SafePath 规则集：长度上限等可参数化规则的不可变载体（§9.1
 *        "并发只读安全；规则集构造后不可变"）。
 *
 * 为什么把规则数值收拢为值对象：SP-9 长度上限（总长 4096、单段 255）
 * 是 io 自设上限（低于 OS 上限——§4.2.1 步骤 5，P-IO-4 数值表同族的
 * "Draft 档位"数值）；收拢后测试可注入边界值验证规则本身，产品路径固定
 * 用 productDefault()。数值修订走单元卡增量修订（§15.5），实现侧不私改。
 *
 * 线程安全：纯值类型；构造后不可变（无 setter——类型层面杜绝）。
 */
struct SafePathRuleSet {
    std::size_t maxTotalLength;   ///< 路径总长上限（UTF-16 码元数）——SP-9：4096（低于 OS 的 32,767）
    std::size_t maxSegmentLength; ///< 单段长度上限（UTF-16 码元数）——SP-9：255（NTFS 段上限口径）

    /**
     * @brief 产品默认规则集（SP-9 数值——§4.2.1 步骤 5/§4.3.1 SP-9 行）。
     *
     * Draft 档位（P-IO-4 同族纪律）：数值为卡面建议默认，架构评审确认前
     * 不私改；测试对产品默认值逐项钉住（IoSec/BudgetDefaults 同款口径）。
     */
    static SafePathRuleSet productDefault()
    {
        return SafePathRuleSet{4096, 255};
    }
};

// =====================================================================
// ISafePathResolver（§9.1 接口契约——签名逐字承载）
// =====================================================================

/**
 * @brief SafePath 解析器接口（§9.1 原文两方法）。
 *
 * 行为契约（§9.1 契约表，逐行冻结）：
 *   - 前置：rawPath 非 null；P-5/P-6 角色时 base 必须提供（P-5＝项目根，
 *     资源区＝根下 objects/ 与 catalog/——§4.3.3 示例表"基＝项目根"；
 *     P-6＝.staging/tmp 授权中转位）。P-4/P-5 未传 base 属调用方契约
 *     违约→防御性拒绝 IO-FORMAT-INTERNAL（§9.1 "非法调用"行）。
 *   - 后置：成功＝NormalizedPath 各字段填充；失败＝无部分产物
 *     （IoResult 值轨道为默认构造状态）。
 *   - 错误：IO-SEC-PATH-ESCAPE/-SYMLINK/-RESERVED/-TOO-LONG、
 *     IO-FORMAT-PACK-ENTRY（P-4 非法字符）、IO-PACK-DUPLICATE-ENTRY
 *     （批量查重）、IO-RES-NOT-FOUND（P-1 实体校验）、
 *     IO-FORMAT-INTERNAL（调用方契约违约/非法角色）。
 *   - 副作用：仅访问文件系统元数据（attributes/canonical）——只读。
 *   - 生命周期：进程级单例（IoRuntime 创建——§9.11）；无状态，结果值
 *     归调用方。
 */
class ISafePathResolver {
public:
    virtual ~ISafePathResolver() = default;

    /**
     * @brief 按角色规范化一条路径（§9.1 原文签名）。
     *
     * 校验序（§4.4 防护流程步骤②；实现按"结构性检查→词法规范化→角色
     * 规则 SP-2~SP-9→实体检查"的固定次序，保证同输入同错误）：
     * ① 长度上限（>32,767 即 TOO-LONG；SP-9 总长/段长）；② 输入分类
     * （空/相对/绝对/UNC/\\?\ 设备前缀）；③ 统一分隔符＋词法消解（.／..
     * ——抵穿根＝穿越尝试 IO-SEC-PATH-ESCAPE，§4.2.1 步骤 3）；④ 角色规
     * 则（P-4 最严：纯相对/无 .. ./非法字符；P-5：解析结果必须落在
     * objects|catalog 资源区内＋逐段无 reparse point；保留名/尾随点空白
     * 仅 P-4/P-5 拒绝；P-1/P-7 放行——NFR-SEC-01 例外不扩大化）；⑤ 等价
     * 键（折叠＋canonical，失败退化为折叠键）；⑥ P-1 存在性（不在→
     * IO-RES-NOT-FOUND，§4.2.5 四分类）。
     *
     * @param role    [in] 路径角色（调用方声明——§4.1）
     * @param rawPath [in] 原始路径（UTF-16 宽字符；Windows 本机形态）
     * @param base    [in] 解析基点；仅 P-5（项目根）/P-6（中转根）使用，
     *                其余角色必须为空（传入非空＝调用方违约→
     *                IO-FORMAT-INTERNAL，防角色语义混用）
     *
     * @return 成功＝NormalizedPath；失败＝IoError（code/params/detail，
     *         path 类参数以脱敏 display 形态携带）
     */
    virtual IoResult<NormalizedPath>
        normalize(PathRole role, const std::wstring& rawPath,
                  const std::filesystem::path& base = {}) const = 0;

    /**
     * @brief P-4 批量预检：包条目/manifest 全量校验＋折叠键查重（§9.1
     *        原文签名；"任一非法即整批拒绝（errors 全量列出）"）。
     *
     * 逐条目按 P-4 规则校验（与 normalize(PackEntry) 完全同一规则核——
     * 单条与批量永不分歧），全部合法再按折叠 relKey 查重（§4.2.4：包条
     * 目重复检测；§4.3.3 P-4 表：仅大小写异的两条目＝重复）。errors 全量
     * 列出的承载：IoError.code 取**最小条目序号**处的错误（确定性），
     * detail 按条目序号升序逐行列出全部违规（"index=N code=… "行）——
     * 调用方（IO-T04 zip 通道）据此产出整体拒绝报告。
     *
     * @param entryCount [in] 条目总数（0＝空包：合法，返回成功）
     * @param entryAt    [in] 条目访问器（按序号取条目名；由调用方提供
     *                   zip 中心目录/manifest 的读取——本接口不做 I/O）
     *
     * @return 全部合法＝成功；任一违规＝失败（code 见 normalize；重复
     *         条目＝IO-PACK-DUPLICATE-ENTRY——§4.3.3 P-4 表末行）
     */
    virtual IoResult<void>
        normalizePackEntries(std::size_t entryCount,
                             const std::function<IoString(std::size_t)>& entryAt) const = 0;
};

/// 解析器指针别名（§9.11 IoRuntime::Injection 以共享指针装配；指向
/// 不可变无状态实现，并发共享安全）。
using ISafePathResolverPtr = std::shared_ptr<ISafePathResolver>;

/**
 * @brief 创建产品规则集下的解析器（IoRuntime 装配入口的实现侧工厂——
 *        §9.11 访问器 safePath() 的产物）。
 *
 * @param rules [in] 规则集（默认＝productDefault()；测试可注入边界规则）
 * @return 无状态并发安全解析器（进程级共享由调用方持有）
 */
ISafePathResolverPtr makeSafePathResolver(const SafePathRuleSet& rules = SafePathRuleSet::productDefault());

// =====================================================================
// EquivKeyMutex（§4.2.4/§9.10——TempArea 会话互斥经等价键的原语）
// =====================================================================

/**
 * @brief 等价键互斥表：同一等价类（同一项目实体的不同路径拼写）至多持有
 *        一个会话租约（§4.2.4 "io 侧缓存/临时键按等价类归一，不产生双份
 *        会话"；§9.10 契约注 "TempArea 会话互斥经等价键"）。
 *
 * 背景说明：同一项目经 D:\P、d:\p\、D:/P 打开时，等价键相同（V10 断言
 * 面）；若不互斥，两个拼写会各建一份 .rwpack-import-* 临时区并在发布期
 * 竞争同一目标。本表以等价键为粒度发租约：TempAreaManager（IO-T06）在
 * create 会话前 acquire(equivKey)，cleanup 后 release——同一项目的第二
 * 个导入会话 acquire 返回 false，实现"单会话；无重复临时区"（V10 观测
 * 点"临时区计数"）。资源摘要缓存键/包条目查重同用等价键（§4.2.4），但
 * 各自是无状态计算，不经本表。
 *
 * 键的来源：调用方传入 NormalizedPath::equivKey（本项目内计算所得）；
 * 本表不解释键内容、不做规范化——等价性判定权威在 SafePath 解析核。
 *
 * 线程安全：内部 std::shared_mutex 保护；acquire/release/观测方法并发
 * 安全（§9.10 "进程级；并发安全"）。进程级生命周期（随 IoRuntime）。
 * 崩溃恢复：本表是进程内存态——进程崩溃后租约自然消失，磁盘残留临时区
 * 由 §7.5 的标记文件机制（io-session.json）在下次同前缀会话回收，两者
 * 互补（本表管并发，标记文件管崩溃残留）。
 */
class EquivKeyMutex final {
public:
    EquivKeyMutex() = default;

    // 禁拷贝/禁移动：租约表的身份即"进程内唯一互斥面"——复制出一个副本
    // 会让同一等价键在两份表里各持租约，互斥语义失效。
    EquivKeyMutex(const EquivKeyMutex&) = delete;
    EquivKeyMutex& operator=(const EquivKeyMutex&) = delete;

    /**
     * @brief 尝试获取等价键的会话租约（非阻塞——§9.10 互斥是"是否允许
     *        开会话"的判定，不做等待排队；等待/重试策略归调用方）。
     *
     * @param equivKey [in] 等价类键（NormalizedPath::equivKey）
     * @return true＝租约获取成功（键被本调用持有至 release）；
     *         false＝键已被持有（同一项目的另一会话在场——调用方不得
     *         创建新临时区/新会话）
     */
    bool tryAcquire(const std::string& equivKey);

    /**
     * @brief 释放租约（幂等语义：未持有的键释放为 no-op——cleanup 失败
     *        重试路径下调用方可能重复 release，不视为错误）。
     *
     * @param equivKey [in] 待释放的等价类键
     */
    void release(const std::string& equivKey);

    /// 键当前是否被持有（观测面——V10"临时区计数"断言用）。
    bool isHeld(const std::string& equivKey) const;

    /// 当前持有租约数（观测面——"单会话"断言：同一项目两种拼写打开后
    /// 持有数仍为 1）。
    std::size_t heldCount() const;

private:
    /// 持有的等价键集合（std::set＋透明比较器：支持 string_view 异构查找
    /// 且字典序稳定——确定性观测面；节点式容器保证迭代稳定性）。
    /// mutable：const 观测路径加锁所需。
    std::set<std::string, std::less<>> m_held;
    /// 读写锁：acquire/release 写、isHeld/heldCount 读——观测面并发只读
    /// 安全（§9.2 ledger 同款口径）。
    mutable std::shared_mutex m_mutex;
};

} // namespace sdurws::ird::io

#endif // SDURWS_IRD_IO_SAFEPATH_HPP
