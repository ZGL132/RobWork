/**
 * @file   ObjectStore.hpp
 * @brief  对象库（ObjectStore）——objects/ 目录的内容编址、只增发布、
 *         摘要校验与 LRU 负载缓存（私有实现头，不入 include/——R-2 纪律）。
 *
 * 设计依据：
 *   - units/project.md §4.6（对象库与内容编址——本类是该节的实现载体：
 *     内容寻址 objects/<oid>/<cv>、只增发布"目标已存在→校验一致视为共享/
 *     不一致→store-corrupt"、读取侧首次 size＋SHA-256 校验、每存储上下文
 *     一个 LRU ObjectCache（默认 256 MiB 可配）、引用存在性语义）、§4.4.6
 *     （对象文件＝负载原样字节，无信封不自述类型——D-10）、§4.4.8（稳定
 *     错误码 token——store-corrupt/write-rejected/disk-full 等）、§7.1
 *     （第 2/4 步：暂存写持久化闸门→publishNew 只增发布；本类承接其中
 *     "对象"这一发布品类——事务编排与修订/清单发布归 PRJ-T07 TxEngine）、
 *     §3.4（源码目录布局 src/ObjectStore.{hpp,cpp}）；
 *   - 需求 CON-01（身份/版本包络的持久化编址——§2.2 CON-01 行承接部分）、
 *     PA-2（不可变历史：写入只增新内容、既有读者引用永不被覆盖）、D-10
 *     （对象负载不做业务解释）、N-9（证据对象边界：project 只存取不判断）；
 *   - 任务契约 tasks/foundation/PRJ-T05.json acceptance 1～4。
 *
 * 背景说明（为什么对象库独立成类）：
 *   objects/ 是整个项目格式中唯一"内容即身份"的条目——文件名由内容的
 *   SHA-256 决定，因此"写入不覆盖、篡改必可检、同内容自然去重"三件事
 *   不靠纪律靠结构：任何代码路径都不以写模式打开已发布对象（唯一写动作
 *   是 publishNew rename，目标已存在即失败），摘要不符在读取与发布两条
 *   路径上都会被拒。修订视图的不可变性（§4.6"查询快照与写入隔离"）建立
 *   在本类之上：读者持有的字节是深拷贝/不可变视图，后续提交只增新文件，
 *   永不改写旧文件（PA-2/PA-3）。
 *
 * 摘要路径纪律（CR-02 处置，acceptance 4）：
 *   本类**不持有任何哈希实现**——内容版本一律经 codec::contentVersionOf()
 *   （本单元唯一哈希入口，内部即 core::ContentDigester）计算；文件名 hex
 *   由 core::ContentVersion::toCanonical() 剥离 "cv-" tag 得到（与 core
 *   规范文本同源，防本地第二套格式化）；目录名由 core::ObjectId::
 *   toCanonical() 直接得到；读回解析经 ContentVersion/ObjectId 的
 *   tryFromCanonical。P-PR-1 处置：消费面仅 core.md v0.1 公共契约
 *   （Identity.hpp/Digest.hpp），冻结出 diff 后按影响面增量同步，不私改 core。
 *
 * 错误语义（错误二分，AGENTS §3/§5.0）：
 *   - 调用方错误 → std::invalid_argument fail-fast：无效 ObjectId/ContentVersion
 *     （全零保留值）、引用表中含无效键。
 *   - 数据侧错误（磁盘内容与编址不符/结构损坏）→ StoreError(StoreCorrupt)：
 *     读取侧摘要校验失败（篡改/位翻转）、发布时既有文件摘要不一致、
 *     object() 引用缺失（§5.2 表：引用缺失＝store-corrupt）。
 *   - 环境错误 → StoreError 携带对应稳定码：磁盘满＝DiskFull、拒绝访问＝
 *     AccessDenied、其余写路径失败＝WriteRejected（§7.1 第 4 步"rename
 *     失败→Failed(write-rejected)"口径）。
 *
 * 线程约束：读路径（tryObject/object/cacheStats/cacheLruOrder）并发安全
 * （缓存与"已校验集"由内部互斥保护——§4.6"缓存互斥内部实现"）；写路径
 * （publishObject/scanObjects 由调用方串行化——写权威唯一归事务引擎
 * （§9 写锁持有者），本类不做路径级互斥。同一实例读写并发安全：发布只
 * 追加新文件并插入缓存节点，读者持有的深拷贝不受逐出影响。
 *
 * 所有权与生命周期：IDiagnosticsSink* 为非 owning（可空——可空时丢弃
 * 开发诊断，存储行为不受影响，§5.0 sink 约定）；本类不持有任何磁盘句柄，
 * 析构不产生磁盘副作用。
 */

#ifndef SDURWS_IRD_PROJECT_SRC_OBJECTSTORE_HPP
#define SDURWS_IRD_PROJECT_SRC_OBJECTSTORE_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

namespace sdurws::ird::project::objstore {

// =====================================================================
// 值类型：对象键 / 扫描报告 / 缓存观测
// =====================================================================

/**
 * @brief 对象键（ObjectId＋ContentVersion）——缓存键与引用存在性判定的
 *        二元组（§4.6"键＝(ObjectId, ContentVersion)"原文形态）。
 *
 * 背景说明：二者缺一不可——ObjectId 回答"哪个逻辑对象"（跨内容稳定），
 * ContentVersion 回答"内容哪一版"（内容寻址文件名）；同对象多版本并存时
 * 各版本是独立文件与独立缓存条目。作为哈希容器键使用（std::hash 特化由
 * core 提供，二者组合——组合式见 ObjectKeyHash）。
 *
 * 线程安全：纯值类型。
 */
struct ObjectKey {
    /// 对象身份（obj- 规范文本形态；全零＝无效，不作为键出现）。
    core::ObjectId oid{};
    /// 对象内容版本（cv- 规范文本形态；全零＝无效，不作为键出现）。
    core::ContentVersion cv{};

    bool operator==(const ObjectKey& o) const noexcept
    {
        return oid == o.oid && cv == o.cv;
    }
    bool operator!=(const ObjectKey& o) const noexcept { return !(*this == o); }
};

/// ObjectKey 的哈希子（std::unordered_set/map 键用）。
///
/// 组合式说明：两成员各取 core 提供的 std::hash（FNV-1a 128），按
/// boost::hash_combine 的黄金比例常数 0x9e3779b97f4a7c15（2^64/φ，散列
/// 组合的标准扰动常数）移位相乘后异或——两成员任一变化必改变结果，
/// 且避免同 oid 邻近键的碰撞聚集。该常数只影响散列分布，不影响语义。
struct ObjectKeyHash {
    std::size_t operator()(const ObjectKey& k) const noexcept
    {
        std::size_t h1 = std::hash<core::ObjectId>{}(k.oid);
        std::size_t h2 = std::hash<core::ContentVersion>{}(k.cv);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) + (h1 >> 2));
    }
};

/**
 * @brief 对象区扫描报告（§4.6 引用存在性语义的数据侧产出）。
 *
 * 背景说明（三个桶各自映射什么——映射决策权在上层，本类只如实分类）：
 *   - missingReferenced＝"有引用无对象"：调用方（打开协议 PRJ-T08/恢复
 *     扫描）映射为稳定码 store-corrupt（§4.6/§7.4 恢复第④步）；
 *   - corruptReferenced＝"有对象但 size＋摘要校验失败"：同映射
 *     store-corrupt（定位到具体对象——篡改/介质位翻转）；
 *   - danglingOnDisk＝"有对象无引用"：悬挂对象＝**开发诊断**（§4.6 原文；
 *     §4.1 objects 行"闭包外对象＝悬挂对象：开发诊断计数，不删除"——GC
 *     明确不做，本类绝不删除任何对象文件）；
 *   - malformedEntries＝对象区内不满足 §4.1 命名规则（obj-<32hex> 目录／
 *     64 小写 hex 文件名）的条目原文：数据侧异常的开发观测项，扫描不抛
 *     （抛会掩盖其余条目的完整扫描结果），交由开发诊断分流。
 *
 * 线程安全：纯值类型。
 */
struct ObjectScanReport {
    /// 有引用无对象（→ store-corrupt，调用方映射）。
    std::vector<ObjectKey> missingReferenced;
    /// 有对象但读取/摘要校验失败（→ store-corrupt，调用方映射）。
    std::vector<ObjectKey> corruptReferenced;
    /// 有对象无引用（悬挂对象——开发诊断，只读不删）。
    std::vector<ObjectKey> danglingOnDisk;
    /// 命名不合规条目的目录内相对路径原文（开发观测；扫描不抛）。
    std::vector<std::string> malformedEntries;
};

/**
 * @brief LRU 负载缓存的观测快照（开发诊断/测试观测面）。
 *
 * 背景说明：§4.6 缓存预算＝"负载字节总量"约束；本结构只暴露记账数字，
 * 不暴露缓存条目本身（条目不可变、无逐出回调语义——暴露条目会诱导调用方
 * 依赖缓存内容，违反"缓存是纯加速层"的定位：任何条目随时可被逐出）。
 *
 * 线程安全：纯值类型。
 */
struct ObjectCacheStats {
    /// 当前缓存条目数（无单位——纯计数）。
    std::size_t entryCount = 0;
    /// 当前缓存负载字节总量（单位：字节；记账口径＝各条目负载 vector 的
    /// size()，不含节点开销——预算语义按 §4.6"负载预算"从简执行）。
    std::size_t byteCount = 0;
    /// 预算上限（单位：字节；构造时固定）。
    std::size_t budgetBytes = 0;
};

// =====================================================================
// ObjectStore——对象库门面
// =====================================================================

/**
 * @brief 对象库——objects/ 目录的唯一读写实现（事务引擎/查询端口的内部
 *        部件；不进公共头——消费者在本单元内部，R-2）。
 *
 * 生命周期与所有权：由 ProjectStoreImpl（PRJ-T08）按存储上下文持有——
 * 每存储上下文一个实例（§4.6"每存储上下文一个 LRU ObjectCache"）；实例
 * 身份随存储上下文（规范化路径 §9.3），跨项目天然隔离（缓存键含 oid 但
 * 无路径——因实例隔离而无串扰，§4.6 原文口径）。
 */
class ObjectStore {
public:
    /// LRU 缓存默认预算＝256 MiB（§4.6"默认预算 256 MiB，可配"原文；
    /// 单位：字节）。独立常量＝生产默认与测试用小预算共用同一事实来源。
    static constexpr std::size_t kDefaultCacheBudgetBytes =
        static_cast<std::size_t>(256) * 1024 * 1024;

    /**
     * @brief 构造对象库（绑定对象区与暂存区目录，注入缓存预算与诊断 sink）。
     *
     * @param objectsDir      [in] 对象区根目录（项目内 objects/ 的绝对路径；
     *                             目录不必已存在——首次发布时创建）
     * @param stagingDir      [in] 暂存区目录（发布用临时文件的落点，§7.1
     *                             第 2 步；发布后临时文件即被 rename 移走，
     *                             失败残留由恢复扫描处置）。
     *                             与 objectsDir 必须同卷（rename 约束——
     *                             §7.1 第 4 步"同卷"，调用方保证）
     * @param cacheBudgetBytes [in] LRU 缓存预算（单位：字节；默认
     *                             kDefaultCacheBudgetBytes＝256 MiB）
     * @param sink            [in] 开发诊断 sink（非 owning，可空——空＝
     *                             丢弃开发诊断；§5.0 sink 约定）
     *
     * @throws 无（目录创建延迟到首次使用；构造不做 I/O）
     */
    ObjectStore(std::filesystem::path objectsDir,
                std::filesystem::path stagingDir,
                std::size_t cacheBudgetBytes = kDefaultCacheBudgetBytes,
                IDiagnosticsSink* sink = nullptr);

    // ---- 写路径（调用方＝事务引擎，串行化由写锁保证——§9） ----

    /**
     * @brief 只增发布一个对象：计算内容版本→暂存写→publishNew 就位。
     *
     * 执行序（§7.1 第 2/4 步的对象品类；每步失败语义见 @return/@throws）：
     *   1. cv＝contentVersionOf(payload)（CR-02 唯一哈希入口）；
     *   2. 确保 objects/<oid>/ 目录存在（含对象区根——首次发布自建）；
     *   3. writeThrough 写临时文件（持久性闸门——落盘后才允许进入可见性
     *      切换；失败残留留在暂存区，由恢复扫描处置，§7.1 第 2 步）；
     *   4. publishNew rename 到 objects/<oid>/<64hex>：
     *        - 目标不存在 → rename 成功，返回 cv（**不预热读取缓存**——
     *          §4.6 缓存＝读取侧惰性加载，首读校验不可跳过）；
     *        - 目标已存在（同内容已发布＝共享去重）→ 读回既有文件做
     *          size＋摘要校验：一致 → 共享（磁盘零写入——既有文件不被
     *          触碰；临时文件尽力清除；该次实算校验同步记入已校验集），
     *          不一致 → store-corrupt（磁盘内容与编址不符＝被外部篡改/
     *          损坏，§4.6 原文口径）；
     *   5. 既有文件在校验时消失（并发删除竞争）→ 有界重试一次第 4 步
     *      （重试仍失败按其失败语义传播——不做无限循环）。
     *
     * @param oid          [in] 对象身份（须非零有效——分配归 HandlerContext/
     *                     事务引擎，core §4.1"对象创建→project"）
     * @param payloadBytes [in] 对象负载原样字节（**收到的字节原样存储**，
     *                     D-10：本类不加信封、不解释内容、零格式前提；
                     长度 0 合法＝空负载对象）
     *
     * @return 内容版本（cv-<64hex>）；同时即目标文件名（剥离 tag 的 hex）
     *
     * @throws std::invalid_argument oid 为全零保留值（调用方契约违约）
     * @throws StoreError DiskFull/AccessDenied/WriteRejected（暂存写或
     *         rename 的环境失败——错误码映射见文件头"错误语义"）；
     *         StoreCorrupt（既有目标文件摘要与 cv 不符——数据侧）
     *
     * 复杂度：O(n)（一次 SHA-256＋一次顺序写＋一次 rename）。
     */
    core::ContentVersion publishObject(const core::ObjectId& oid,
                                       std::string_view payloadBytes);

    // ---- 读路径（并发安全；查询端口/归档会话消费） ----

    /**
     * @brief try 轨读取对象负载（§5.2 tryObject 的底层实现）。
     *
     * 引用存在性语义（acceptance 3）：对象文件不存在（含请求的 (oid,cv)
     * 从未发布、清单无引用的请求）→ 返回空 optional，**不抛**——存在性
     * 判定与"引用缺失属于损坏"的升级决策归调用方（查询端口/扫描）。
     * 文件存在但摘要校验失败 → 仍抛 StoreCorrupt（数据侧错误不降级为
     * "不存在"——篡改与缺失是两种事实，静默合并会掩盖损坏，§4.6 读取
     * 侧校验语义）。
     *
     * 完整性语义：每存储上下文对每个 (oid,cv) 首次读盘执行 size＋SHA-256
     * 校验；通过后记入"已校验集"，同上下文后续读盘免复检（§4.6"摘要
     * 缓存后免复检"——对象文件不可变是免复检的结构前提）；缓存命中则
     * 完全不触盘。
     *
     * @param oid [in] 对象身份（须非零有效）
     * @param cv  [in] 内容版本（须非零有效）
     *
     * @return 负载字节深拷贝（值语义——调用方自由持有，PA-3：与缓存/磁盘
     *         后续变化隔离）；对象不存在＝nullopt
     *
     * @throws std::invalid_argument oid/cv 为全零保留值（调用方契约违约）
     * @throws StoreError StoreCorrupt（摘要/size 校验失败——数据侧）；
     *         AccessDenied 等（存在却不可读的环境残余——见实现映射表）
     */
    std::optional<std::vector<std::uint8_t>> tryObject(
        const core::ObjectId& oid, const core::ContentVersion& cv);

    /**
     * @brief 非抛不确定形态读取（§5.2 object 的底层实现）。
     *
     * 与 tryObject 的唯一区别：对象文件不存在 → 抛 StoreError(StoreCorrupt)
     * （§5.2 错误表"引用缺失＝store-corrupt"——调用方已断言引用存在，
     * 缺失即损坏而非"没有"）。
     *
     * @param oid [in] 对象身份（须非零有效）
     * @param cv  [in] 内容版本（须非零有效）
     *
     * @return 负载字节深拷贝（同 tryObject）
     *
     * @throws std::invalid_argument 同 tryObject
     * @throws StoreError StoreCorrupt（引用缺失或摘要校验失败）；环境码同 tryObject
     */
    std::vector<std::uint8_t> object(const core::ObjectId& oid,
                                     const core::ContentVersion& cv);

    // ---- 扫描（打开协议/恢复扫描的语义部件——§4.6 引用存在性、§7.4 ④⑤） ----

    /**
     * @brief 对象区引用存在性扫描：对照引用集报告缺失/损坏/悬挂。
     *
     * 两个扫描方向（acceptance 3）：
     *   - 引用→磁盘：引用集中每个 (oid,cv) 在 objects/ 找不到文件 →
     *     missingReferenced（调用方映射 store-corrupt）；verifyDigest 为
     *     true 时对存在者加做 size＋SHA-256 全量校验，失败 →
     *     corruptReferenced（§7.4 恢复第④步"校验闭包完整性"口径——打开
     *     协议用全量；existences-only 档供廉价计数场景）；
     *   - 磁盘→引用：遍历 objects/ 下全部 <oid>/<64hex>，不在引用集 →
     *     danglingOnDisk（悬挂对象——本类同时经 sink.reportDev 上报开发
     *     诊断，只读不删）；命名不合规条目 → malformedEntries（开发观测）。
     *
     * @param references   [in] 引用集（修订闭包/清单的全部对象引用——
     *                     含元数据对象；本类不解析清单，引用集由调用方
     *                     组装——权威唯一 PA-1：清单语义归 RevisionIndex）
     * @param verifyDigest [in] true＝对存在对象加做全量摘要校验（打开/恢复
     *                     完整性口径）；false＝只查存在性（廉价档）
     *
     * @return 三桶分类报告（永不抛数据侧错误——损坏进报告桶不中断扫描）
     *
     * @throws std::invalid_argument references 含全零键（调用方契约违约）
     *
     * 线程约束：遍历目录期间对象区不得有并发发布（由调用方串行化——
     * 扫描归打开/恢复时点，写锁语义覆盖）。
     */
    ObjectScanReport scanObjects(const std::vector<ObjectKey>& references,
                                 bool verifyDigest) const;

    // ---- 观测（开发诊断/测试面；不承诺任何缓存内容语义） ----

    /**
     * @brief 缓存记账快照（并发安全）。
     *
     * @return 当前条目数/字节量/预算（单位见 ObjectCacheStats 注释）
     */
    ObjectCacheStats cacheStats() const;

    /**
     * @brief 缓存条目的 LRU 序快照（最旧→最新；开发诊断/测试观测面）。
     *
     * 背景说明：暴露键序而非条目内容——逐出顺序是预算语义的可观测面
     * （验收"缓存预算用例"需要断言"谁被逐出"），条目字节仍不可经此获取
     * （读负载必须走 tryObject/object 的校验通道）。
     *
     * @return 键列表（拷贝；最旧在前）
     */
    std::vector<ObjectKey> cacheLruOrder() const;

private:
    /// 对象目录：objectsDir/<oid 规范文本>（"obj-<32hex>"——§4.1 命名规则）。
    std::filesystem::path objectDir(const core::ObjectId& oid) const;
    /// 对象文件：objectDir/<cv hex 64>（cv 规范文本剥离 "cv-" tag——§4.1
    /// 命名规则"<cv>＝内容版本 64 hex（无 tag）"）。
    std::filesystem::path objectFile(const core::ObjectId& oid,
                                     const core::ContentVersion& cv) const;

    /// 读盘＋完整性校验公共体：文件存在时执行 size＋SHA-256（受已校验集
    /// 免复检约束），返回 nullopt 表示文件不存在。
    /// 抛 StoreCorrupt（校验失败）/环境码（存在却读不了）。
    std::optional<std::vector<std::uint8_t>> readVerified(
        const core::ObjectId& oid, const core::ContentVersion& cv);

    /// 缓存插入（超预算逐出 LRU；单条目超预算整条不缓存——预算语义）。
    void cacheInsert(const ObjectKey& key,
                     std::shared_ptr<const std::vector<std::uint8_t>> bytes);

    std::filesystem::path m_objectsDir;  ///< 对象区根（objects/；构造固定）
    std::filesystem::path m_stagingDir;  ///< 暂存区根（发布临时文件落点）
    std::size_t m_budgetBytes;           ///< LRU 预算（单位：字节；构造固定）
    IDiagnosticsSink* m_sink;            ///< 开发诊断 sink（非 owning，可空）

    /// 内部互斥（保护缓存与已校验集；读路径并发安全、写路径调用方串行化
    /// ＋本锁保证记账一致——§4.6"缓存互斥内部实现"）。
    mutable std::mutex m_mutex;
    /// LRU 双向链表（front＝最旧，back＝最新；节点值＝不可变负载视图）。
    std::list<std::pair<ObjectKey, std::shared_ptr<const std::vector<std::uint8_t>>>> m_lru;
    /// 键→链表节点索引（unordered_map＋list＝经典 LRU O(1) 形态）。
    std::unordered_map<ObjectKey, std::list<std::pair<ObjectKey,
        std::shared_ptr<const std::vector<std::uint8_t>>>>::iterator, ObjectKeyHash> m_index;
    /// 当前缓存负载字节总量（单位：字节；与 m_lru 记账同步维护）。
    std::size_t m_cachedBytes = 0;
    /// 已通过首次校验的 (oid,cv) 集（§4.6"摘要缓存后免复检"；不随负载
    /// 逐出而清除——校验对象是不可变文件，逐出只影响字节缓存不影响校验结论）。
    std::unordered_set<ObjectKey, ObjectKeyHash> m_verified;
    /// 暂存临时文件名序号（进程内单调；防同批次临时名冲突——原子递增）。
    std::atomic<std::uint64_t> m_tempSeq{0};
};

}  // namespace sdurws::ird::project::objstore

#endif  // SDURWS_IRD_PROJECT_SRC_OBJECTSTORE_HPP
