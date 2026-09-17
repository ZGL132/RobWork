/**
 * @file   AtomicFile.hpp
 * @brief  导出文件原子写出（IAtomicFileWriter）——单文件导出（CSV/JSON/
 *         HTML 工件、WorkCell XML、.rwpack 本体）统一的"暂存→替换"协议，
 *         失败/取消恢复先前输出。
 *
 * 设计依据：
 *   - units/io.md §4.6（导出文件原子写出协议：写 `<target>.<8hex>.tmp`
 *     〔同目录→同卷〕→ flush＋FlushFileBuffers → ReplaceFile → 删除 .tmp
 *     残留 → 报告；ReplacePolicy：NeverOverwrite 默认/OverwriteAtomic
 *     显式确认）、§9.10（IAtomicFileWriter 接口契约——prepare→[写入]→
 *     commit(替换)/abort(清理)，commit 前目标不变）、§3.1（公共头表
 *     AtomicFile.hpp 行）、§1.3 目标 4（失败不污染目标）
 *   - 需求 MDL-20（支撑：导出失败恢复先前输出——原子写出设施）、PM-05
 *     （V23：目标 .rwpack 不存在或为先前完整版本——本头是结构性保证的
 *     承载件）、NFR-MNT-01（零 Qt）
 *   - 任务契约 tasks/foundation/IO-T06.json acceptance 2（V23
 *     ExportStagingOnly）与 acceptance 5（测试目标登记）
 *
 * 背景说明（为什么原子性要求暂存文件与目标**同目录**）：Windows 的
 * ReplaceFile/MoveFileEx 替换是**同卷**原子操作——暂存文件放在目标同
 * 目录（而非临时区）保证同卷，替换瞬时完成、无跨卷拷贝窗口；替换前目
 * 标的任何既有内容（先前的完整输出）原样保留——"失败恢复先前输出"
 * （MDL-20）与"目标不变"（V23）由此成为结构性保证，不依赖事后清理。
 *
 * 失败语义分段（§4.6 原文）：
 *   - 失败/取消发生在 commit 前：先前输出完整保留（abort 只清理自己的
 *     暂存文件——它带本会话随机后缀，绝不触碰任何既有文件）；
 *   - 替换后失败（残留清理失败）：目标已正确，仅开发级诊断——本实现
 *     不产生该情形（无备份文件残留面，见 commit 注）。
 *
 * 与 ZipChannel 的分工：ZIP 容器编解码归 ZipChannel/Package（§7.1）；
 * 本头只管"一个文件的原子就位"——导出流程（IPackageExporter）在临时区
 * 组装好完整包后，经本设施分块搬运至目标旁暂存位并原子替换（大包不整
 * 体驻留内存）。
 *
 * 故障注入口径（§11.1）：契约/故障注入测试经 fake 适配层替换
 * IAtomicFileWriter（PackageIoFacilities 注入——Package.hpp），在
 * prepare/写入/commit 各点注入失败，验证 V23"目标不存在或为先前完整
 * 版本"；产品装配使用 makeAtomicFileWriter() 的真实实现。
 *
 * 线程约束：IAtomicFileWriter 实例与每个 AtomicTarget 会话型单线程
 * （§9.10 契约注"AtomicWriter 实例单线程"）；不同目标可各持会话并行。
 * AtomicTarget 为值语义句柄（pimpl）——拷贝共享同一底层会话（与
 * TempAreaSession 同款取舍：cleanup(session&) 幂等以句柄为参）。
 */

#ifndef SDURWS_IRD_IO_ATOMICFILE_HPP
#define SDURWS_IRD_IO_ATOMICFILE_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

#include <sdurws/ird/io/IoFwd.hpp>   // IoResult——接口返回容器

namespace sdurws::ird::io {

// =====================================================================
// ReplacePolicy（§4.6 替换策略——两值封闭枚举）
// =====================================================================

/**
 * @brief 目标已存在时的替换策略（§4.6 ReplacePolicy 行）。
 *
 * 枚举顺序＝卡面出现序（持久化契约面纪律，只允许表尾追加）。
 */
enum class ReplacePolicy : std::uint8_t {
    /// 默认：目标存在→IO-PACK-TARGET-EXISTS 同族码拒绝（不覆盖未经确认
    /// 的既有文件——§4.1 P-7 行；对目录不适用——§7.4 目标目录已存在行
    /// 由包导入器以固定 NeverOverwrite 语义承载）。
    NeverOverwrite,
    /// 显式确认后的原子覆盖（导出默认——用户已选定目标路径，§9.9
    /// PackageExportOptions::replace 缺省 OverwriteAtomic；仍是原子替换，
    /// 失败时先前输出保留）。
    OverwriteAtomic,
};

// =====================================================================
// AtomicTarget（§9.10 prepare 产物——一次原子写出的会话句柄）
// =====================================================================

/**
 * @brief 原子写出的会话句柄（§9.10 原文返回类型 AtomicTarget）。
 *
 * 生命周期与所有权：值语义 pimpl——拷贝共享同一底层会话（暂存文件与
 * 打开句柄只有一份）；commit/abort 之后会话进入终态，再次 write/commit/
 * abort＝调用方契约违约（防御性返回 IO-FORMAT-INTERNAL，不抛——§1.4）。
 * 底层句柄由实现 RAII 关闭（会话终态或最后一份句柄析构时）。
 *
 * 写入面（等价调整——DTB §5.4 增量修订，io.md §15.5 v0.9 登记）：§9.10
 * 卡面协议只写"prepare→[写入]→commit/abort"，未定义写入经何通道；本实
 * 现以 write(std::string_view) 承载（调用方分块驱动——大包导出按 1 MiB
 * 块从暂存区读入、写出，内存有界）。暂存文件名＝
 * `<target 文件名>.<8hex>.tmp`（§4.6 原文形态；同目录→同卷——类注）。
 */
class AtomicTarget {
public:
    AtomicTarget() = default;
    ~AtomicTarget();
    AtomicTarget(AtomicTarget&& other) noexcept;
    AtomicTarget& operator=(AtomicTarget&& other) noexcept;
    AtomicTarget(const AtomicTarget&) = default;
    AtomicTarget& operator=(const AtomicTarget&) = default;

    /// 会话是否处于可写/可提交状态（默认构造/已 commit/已 abort＝false）。
    bool isActive() const noexcept;

    /// 最终发布目标路径（prepare 传入值的规范化形式——只读访问）。
    const std::filesystem::path& targetPath() const noexcept;

    /// 暂存文件路径（`<target 文件名>.<8hex>.tmp`——与目标同目录同卷；
    /// commit 前一直存在，commit/abort 后消失）。
    const std::filesystem::path& tempPath() const noexcept;

    /**
     * @brief 追加写入一段字节到暂存文件（§9.10 协议"[写入]"步骤的通道）。
     *
     * @param bytes [in] 本块字节（可为空——空块 no-op，便于调用方循环驱动）
     * @return 成功＝已写入；失败＝IO-RES-*（写失败四分类——§4.2.5）、
     *         IO-FORMAT-INTERNAL（会话非活动态——调用方契约违约）
     */
    IoResult<void> write(std::string_view bytes);

    /// 实现承载（pimpl——Win32 句柄与暂存状态不出公共头）。
    struct Impl;

    /// pimpl 通道（TempAreaSession::adopt 同款形态）：写出器实现装配/
    /// 访问底层会话的唯一入口——m_impl 保持私有，接口实现经本通道读写。
    static AtomicTarget adopt(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl() const noexcept;

private:
    std::shared_ptr<Impl> m_impl;   ///< 会话承载（shared——值语义拷贝共享）
};

// =====================================================================
// IAtomicFileWriter（§9.10 接口契约——签名逐字承载）
// =====================================================================

/**
 * @brief 原子文件写出器接口（§9.10 原文三方法）。
 *
 * 行为契约（§9.10 契约注＋§4.6 协议，逐条冻结）：
 *   - prepare：父目录必须已存在（否则 IO-RES-NOT-FOUND——本设施不代建
 *     目录，导出目标的目录归属调用方）；NeverOverwrite 且目标存在→
 *     IO-PACK-TARGET-EXISTS；OverwriteAtomic 且目标存在→放行（替换在
 *     commit 才发生——prepare 阶段目标不变）。成功＝暂存文件已创建并
 *     打开（独占写），AtomicTarget 就绪。
 *   - commit：flush＋FlushFileBuffers（落盘到介质——断电语义，§4.6 协
 *     议原文）→ 目标存在则 ReplaceFileW 原子替换（否则 MoveFileExW 挪
 *     入）→ 暂存位清空。成功后目标＝本次内容，先前输出被原子顶替。
 *   - abort：关闭句柄＋删除暂存文件；目标路径零接触（V23 的结构性依据）。
 *   - 错误类型：IO-PACK-TARGET-EXISTS（NeverOverwrite 命中）、IO-RES-*
 *     （四分类）、IO-FORMAT-INTERNAL（会话状态违约）。
 *   - 线程：实例单线程（§9.10）；不同实例/不同目标并行安全。
 *   - 副作用：仅目标目录内的暂存文件与目标本体（commit 时）；无其他。
 */
class IAtomicFileWriter {
public:
    virtual ~IAtomicFileWriter() = default;

    /**
     * @brief 准备一次原子写出（§9.10 原文签名）。
     *
     * @param target [in] 发布目标路径（父目录必须已存在）
     * @param policy [in] 替换策略（NeverOverwrite＝目标存在即拒）
     * @return 成功＝活动会话句柄；失败＝IO-PACK-TARGET-EXISTS/IO-RES-*
     */
    virtual IoResult<AtomicTarget> prepare(const std::filesystem::path& target,
                                           ReplacePolicy policy) = 0;

    /**
     * @brief 提交：暂存内容原子替换到目标（§9.10；§4.6 替换步骤）。
     *
     * FlushFileBuffers 后替换——commit 返回成功即目标已在介质上完整
     * （非仅缓存）。替换后暂存位无残留（Windows 原子替换语义下暂存文件
     * 已顶替目标/被挪入目标位，无备份残留面——§4.6"删除 .tmp 残留"步骤
     * 在此语义下天然满足）。
     */
    virtual IoResult<void> commit(AtomicTarget& target) = 0;

    /**
     * @brief 放弃：清理暂存、目标零接触（§9.10；幂等——已终态会话
     *        abort 返回成功，便于失败路径统一调用）。
     */
    virtual IoResult<void> abort(AtomicTarget& target) = 0;
};

/// 写出器指针别名（PackageIoFacilities 注入形态——Package.hpp）。
using IAtomicFileWriterPtr = std::shared_ptr<IAtomicFileWriter>;

/**
 * @brief 创建产品真实写出器（§9.11 atomicWriter() 访问器的实现侧工厂；
 *        故障注入测试以 fake 实现替换——不经过本工厂）。
 */
IAtomicFileWriterPtr makeAtomicFileWriter();

} // namespace sdurws::ird::io

#endif // SDURWS_IRD_IO_ATOMICFILE_HPP
