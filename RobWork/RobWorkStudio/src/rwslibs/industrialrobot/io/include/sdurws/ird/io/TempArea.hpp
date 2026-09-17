/**
 * @file   TempArea.hpp
 * @brief  临时导入/导出/固化中转目录的创建、互斥、RAII 清理与失败残留
 *         报告（ITempAreaManager/TempAreaSession）。
 *
 * 设计依据：
 *   - units/io.md §7.5（临时区管理：创建/互斥/清理/残留报告/崩溃残留）、
 *     §7.6（清理状态图：CleanupFailed 终态可重试、残留不可被误识别为
 *     项目）、§4.1 P-3/P-6（导入临时根/中转路径的角色归属）、§4.2.4＋
 *     §9.10（会话互斥**经等价键**——EquivKeyMutex 原语，SafePath.hpp）、
 *     §9.10（ITempAreaManager 接口契约）、§3.1（公共头表 TempArea.hpp
 *     行）、IO-D08（临时区与发布目标同卷——rename 原子性前提）
 *   - 需求 PM-05（取消即清理临时区、失败不留可误识别半成品）、CON-03
 *     （固化中转位——P-6 由 project 授权）、NFR-MNT-01（零 Qt）
 *   - 任务契约 tasks/foundation/IO-T06.json acceptance 2（V19 取消清理/
 *     V20 磁盘满/V21 CleanupFailed）与 acceptance 4（失败不留目标目录）
 *
 * 背景说明（为什么临时区是"失败不留半成品"的结构性承载）：PM-05 的
 * 关键承诺是目标项目目录/正式包文件在任何失败下都不留可被误识别的半
 * 成品。实现机制＝**发布前一切写入只落在隐藏前缀临时区**（`.rwpack-
 * import-<8hex>/`、`.<name>.<8hex>.tmp/`——§7.5 创建行），目标路径只在
 * project 发布（同卷 rename/原子替换）那一刻被触碰；因此临时区能否被
 * 可靠创建与清理，直接决定失败路径的干净程度。清理失败不是可忽略的尾
 * 事件——§7.6 把它定为**终态 CleanupFailed**（残留登记＋可重试），残留
 * 均在隐藏前缀内、带 io-session.json 标记，与正式产物可区分。
 *
 * 崩溃残留回收（§7.5 两段机制的第二段）：进程内 EquivKeyMutex 管并发
 * 互斥；进程崩溃后租约自然消失，磁盘残留由"下次同前缀会话创建时按
 * io-session.json 标记识别回收"承接（create 时扫描）——**仅当**残留内
 * 含本 io 标记文件才清理，绝不误删用户目录（§7.5 互斥行原文）。
 *
 * 线程约束：管理器进程级并发安全（create/cleanup 内部互斥——§9.10
 * "进程级；并发安全（会话前缀互斥）"）；单个会话句柄单线程使用。
 * TempAreaSession 为值语义句柄（pimpl——拷贝共享同一底层会话）。
 *
 * 故障注入口径（§11.1/V20/V21）：磁盘可用空间探测与清理失败经 fake
 * ITempAreaManager 适配层注入（PackageIoFacilities 注入——Package.hpp），
 * 不依赖真实磁盘满/真实删除失败；产品装配使用 makeTempAreaManager()。
 */

#ifndef SDURWS_IRD_IO_TEMPAREA_HPP
#define SDURWS_IRD_IO_TEMPAREA_HPP

#include <cstdint>
#include <filesystem>
#include <memory>

#include <sdurws/ird/io/IoFwd.hpp>   // IoResult/IoCancelToken——接口返回容器

namespace sdurws::ird::io {

// =====================================================================
// TempAreaRole / TempAreaSpec（§7.5 创建行——三类临时区的角色区分）
// =====================================================================

/**
 * @brief 临时区角色（§7.5 创建行的三类；枚举序＝卡面行序——持久化契约
 *        面纪律，只允许表尾追加）。
 *
 * 角色决定命名前缀与标记文件语义，由调用方在 spec 中声明——io 不从
 * 路径形态推断（§4.1 角色声明同款纪律）。
 */
enum class TempAreaRole : std::uint8_t {
    /// 包导入工作区：`<发布目标父目录>\.rwpack-import-<8hex>\`（§4.1 P-3；
    /// 同卷要求＝与发布目标同卷——发布＝project 同卷 rename，IO-D08）。
    PackImport,
    /// 包导出暂存区：`<目标目录>\.<name>.<8hex>.tmp\`（§7.5 创建行；
    /// 同目录→同卷——与目标文件的原子替换衔接，§4.6）。
    PackExport,
    /// 固化中转区：`.staging/tmp/solidify-<id>/`（P-6——**project 授权
    /// 位**；本管理器仅在调用方传入授权 baseDir 时在其下建会话，不越界
    /// 写其他 .rwdesign 条目——§2.1 N-1）。
    Solidify,
};

/**
 * @brief 临时区规格（§9.10 create 参数 TempAreaSpec{role,baseDir,name}）。
 *
 * 值语义。baseDir 语义随角色：PackImport＝发布目标父目录（其必须已存
 * 在可写）；PackExport＝导出目标文件目录；Solidify＝project 授权的
 * `.staging/tmp/`。nameHint 仅 PackExport 使用（目标文件名去扩展名的
 * 命名提示——`.<name>.<8hex>.tmp` 前缀）；其余角色忽略。
 */
struct TempAreaSpec {
    TempAreaRole role = TempAreaRole::PackImport;  ///< 角色（决定命名前缀）
    std::filesystem::path baseDir;                 ///< 会话根的父目录（同卷锚点）
    std::string nameHint;                          ///< 命名提示（PackExport 有效；UTF-8）
};

// =====================================================================
// TempAreaSession（§9.10 create 产物——会话句柄）
// =====================================================================

/**
 * @brief 临时区会话句柄（值语义 pimpl；拷贝共享同一底层会话——cleanup
 *        以句柄为参的幂等语义与 AtomicTarget 同款）。
 *
 * active()：create 成功→cleanup 成功前为真。rootPath()：会话根（隐藏
 * 前缀目录的绝对路径）——包导入的"已验证临时目录"即其下 payload/
 * （§7.3 步骤⑧ project 对其执行 rename 发布）；包导出的组装区即根本身。
 * role()：create 时声明的角色回显（project 选择发布方式/诊断归类用）。
 */
class TempAreaSession {
public:
    TempAreaSession() = default;
    ~TempAreaSession();
    TempAreaSession(TempAreaSession&& other) noexcept;
    TempAreaSession& operator=(TempAreaSession&& other) noexcept;
    TempAreaSession(const TempAreaSession&) = default;
    TempAreaSession& operator=(const TempAreaSession&) = default;

    /// 会话是否处于活动态（未清理；默认构造/已清理＝false）。
    bool isActive() const noexcept;

    /// 会话根绝对路径（隐藏前缀目录；非活动态返回空路径）。
    const std::filesystem::path& rootPath() const noexcept;

    /// 角色回显（非活动态仍返回创建时角色）。
    TempAreaRole role() const noexcept;

    /// 实现承载（pimpl——互斥租约/标记文件状态不出公共头）。
    struct Impl;

    /// pimpl 通道（ResourceStreamHandle::adopt 同款形态）：管理器实现
    /// 装配/访问底层会话的唯一入口——m_impl 保持私有，接口实现经本通道
    /// 读写，公共消费者拿不到裸 pimpl（封装与可实现性两全）。
    static TempAreaSession adopt(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl() const noexcept;

private:
    std::shared_ptr<Impl> m_impl;   ///< 会话承载（shared——值语义拷贝共享）
};

// =====================================================================
// ITempAreaManager（§9.10 接口契约＋一处登记等价增补）
// =====================================================================

/**
 * @brief 临时区管理器接口（§9.10 原文两方法＋一处等价增补）。
 *
 * 等价增补（§9.0"签名均为实现建议……实现期允许等价调整，语义不变"；
 * DTB §5.4 增量修订，io.md §15.5 v0.9 登记）：§9.10 卡面无"会话所在卷
 * 可用空间"的探测口，而 §7.3 步骤④要求"磁盘可用空间检查（不足→
 * IO-PACK-DISK-FULL）"且 §11.1 规定故障注入"经 ITempAreaManager 的
 * fake 适配层注入"——故增补 availableBytes(session) 虚方法：真实实现
 * ＝std::filesystem::space(root).available；契约测试以 fake 覆写注入
 * 确定值（V20 的注入面）。语义为**提示性探测**（探测与写入之间无原子
 * 保证，写入失败兜底仍是防线之二——§7.4 磁盘不足行）。
 *
 * 行为契约（§7.5/§9.10，逐条冻结）：
 *   - create：① 等价键互斥（同一 baseDir 角色前缀至多一个活动会话——
 *     等价键＝baseDir 规范化＋大小写折叠；同一项目经不同拼写打开不产
 *     生双份会话，§4.2.4）；占用即 IO-RES-LOCK-CONFLICT（调用方决定
 *     等待/放弃——§4.2.5 分类四）。② 崩溃残留回收：扫描 baseDir 下同
 *     前缀目录，含 io-session.json 标记者清理（仅此！无标记者不触碰
 *     ——§7.5"绝不误删用户目录"），清理失败不阻断新会话（新 8hex 后
 *     缀无碰撞）。③ 创建 `<前缀><8hex>` 会话根＋写入 io-session.json
 *     标记（pid＋UTC 时间戳——崩溃残留的识别依据）。
 *   - cleanup：递归删除会话根（**仅本会话根**——绝不删除非本会话文件，
 *     §7.6）；幂等（已清理再次调用＝成功）；失败→IO-PACK-CLEANUP-
 *     FAILED（残留路径清单以脱敏 display 形态入 params/detail——§7.5
 *     残留报告行），会话保持活动可重试（CleanupFailed 终态可重试，
 *     §7.6）。
 *   - 错误类型：IO-RES-LOCK-CONFLICT（互斥占用）、IO-RES-*（baseDir
 *     缺失/不可写等四分类）、IO-PACK-CLEANUP-FAILED（清理残留）、
 *     IO-CANCELLED（create 的检查点——§9.10 原文签名带令牌）。
 */
class ITempAreaManager {
public:
    virtual ~ITempAreaManager() = default;

    /**
     * @brief 创建临时区会话（§9.10 原文签名）。
     *
     * @param spec   [in] 角色＋baseDir（必须已存在可写）＋命名提示
     * @param cancel [in] 取消令牌（null＝不可取消；检查点＝残留扫描/标记
     *               写入前后——创建耗时短，检查点密度满足协作语义即可）
     * @return 成功＝活动会话（rootPath 就绪）；失败＝见类注错误类型
     */
    virtual IoResult<TempAreaSession> create(const TempAreaSpec& spec,
                                             IoCancelToken* cancel) = 0;

    /**
     * @brief 清理会话根（§9.10 原文签名；幂等；仅删本会话根）。
     *
     * @param session [in,out] 会话句柄（成功后转非活动态；失败保持活动
     *                ——CleanupFailed 可重试语义）
     * @return 成功＝已清理；失败＝IO-PACK-CLEANUP-FAILED（params 携带
     *         residual0..N 脱敏残留清单，至多 16 条——防 params 膨胀）
     */
    virtual IoResult<void> cleanup(TempAreaSession& session) = 0;

    /**
     * @brief 会话根所在卷的可用字节数（等价增补——见类注；提示性探测）。
     *
     * @param session [in] 活动会话（非活动/非法→IO-FORMAT-INTERNAL）
     * @return 成功＝可用字节数；失败＝IO-RES-*（探测失败——调用方按
     *         "不可知"处理，不阻断流程：探测是优化，写入兜底是防线）
     */
    virtual IoResult<std::uint64_t> availableBytes(const TempAreaSession& session) const = 0;
};

/// 管理器指针别名（PackageIoFacilities 注入形态——Package.hpp）。
using ITempAreaManagerPtr = std::shared_ptr<ITempAreaManager>;

/**
 * @brief 创建产品真实管理器（§9.11 tempArea() 访问器的实现侧工厂；
 *        故障注入测试以 fake 实现替换——不经过本工厂）。
 */
ITempAreaManagerPtr makeTempAreaManager();

} // namespace sdurws::ird::io

#endif // SDURWS_IRD_IO_TEMPAREA_HPP
