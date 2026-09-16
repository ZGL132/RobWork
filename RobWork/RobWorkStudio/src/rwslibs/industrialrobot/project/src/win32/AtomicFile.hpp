/**
 * @file   AtomicFile.hpp
 * @brief  存储原语（AtomicFile）——"暂存写/只增发布/原子替换"三个单文件
 *         原语的门面。
 *
 * 设计依据：
 *   - units/project.md §7.1（七步协议中本原语承担的动作：第 2 步暂存写
 *     "FILE_FLAG_WRITE_THROUGH 写＋FlushFileBuffers 后关闭"、第 4 步
 *     "publishNew（目标不存在→rename；已存在→摘要一致视为共享…）"、
 *     第 5 步 "MoveFileExW(HEAD.new→HEAD, REPLACE_EXISTING|WRITE_THROUGH)
 *     ——唯一提交点"）、§7.2（Windows 文件操作保证逐行口径——本类是
 *     该表行 1/2/3 的实现载体；行 6"不使用 ReplaceFileW"同样落实：
 *     HEAD/草稿无需保留目标属性，少一个依赖面）；
 *   - 需求 NFR-REL-01 的单文件原语层（"单文件'暂存＋原子替换'只用于版本
 *     目录内资源写入"——REQUIREMENTS §NFR-REL-01）；WP-04-T02 卡行
 *     （DTB §2.5）与任务契约 PRJ-T02.json；
 *   - units/project.md §3.4（win32/AtomicFile.{hpp,cpp} 布局位）。
 *
 * 背景说明（两种保证的分离——§7.2 任务约束§五.5 明确项，本类 API 按
 * 保证分面而不是按"功能"分面，是有意为之的语义表达）：
 *   - **写入持久性**（崩溃后内容仍在）＝writeThrough()：写穿透＋flush
 *     闸门，覆盖暂存写；
 *   - **原子替换**（并发可见性：读者见旧或新，无半文件）＝publishNew()/
 *     replaceFile()：同卷 rename 元数据操作，覆盖只增发布与 HEAD 切换。
 *   持久性闸门失败时绝不进入可见性切换（调用方在 writeThrough 失败后
 *   必须放弃发布——失败传播见各方法注释），这是"失败不留半写目标"
 *   （NFR-REL-01）在单文件层的结构性保证。
 *
 * 不超诺口径（§7.2）：本类只承诺 Windows API 文档声明的保证——同卷
 * rename 的原子可见性、MOVEFILE_WRITE_THROUGH 的返回前刷盘、写穿透＋
 * FlushFileBuffers 的落盘；**不断言超出文档的崩溃一致性**（如"断电后
 * HEAD 必为新值"），残余窗口由 §7 恢复协议兜底（PM-08）。
 *
 * 失败残留语义（§7.1 第 2 步）：writeThrough 中途失败时，**已写入的部分
 * 文件留在原地不清除**——这是设计行为而非疏漏：暂存路径属于 .staging/
 * 事务目录（调用方决定路径），残留由启动恢复扫描忽略＋报告（PM-08），
 * 本原语不擅自删除（删除失败分支会引入新的失败语义）。
 *
 * 线程约束：本类无共享可变状态（仅持有 IFileOps 指针），同一实例的
 * 多个方法调用相互独立，可并发——但**对同一路径的并发操作由调用方
 * 串行化**（写锁归 §9.1 StoreLock，PRJ-T03；本原语不做路径级互斥）。
 * IFileOps 指针指向的对象生存期必须覆盖本对象（构造注入，非 owning）。
 */

#ifndef RWS_IRD_PROJECT_SRC_WIN32_ATOMICFILE_HPP
#define RWS_IRD_PROJECT_SRC_WIN32_ATOMICFILE_HPP

#include "IFileOps.hpp"

#include <cstddef>
#include <string>

namespace sdurws::ird::project::win32 {

/**
 * @brief writeThrough 的分块写入尺寸（故障注入观测粒度，单位：字节）。
 *
 * 0.0625 MB＝64 KiB，是常规文件 I/O 的缓冲量级选择；**不构成任何持久性
 * 语义承诺**——持久性闸门是 flush（§7.2 行 1），分块只为把 F2"第 N 字节
 * 写失败"的注入边界落到 writeChunk 的 occurrence 计数上（整文件单次写
 * 会让注入点只能落在文件边界，覆盖不了写中途失败）。公开常量＝测试与
 * 实现共用同一事实来源（测试据它构造跨块数据并断言残留字节数）。
 */
inline constexpr std::size_t kWriteChunkSize = 64 * 1024;

/**
 * @brief 真实 Win32 实现——IFileOps 的生产实现体（无状态）。
 *
 * 每个方法即对应 Win32 API 的薄封装（错误码经 GetLastError 如实返回）；
 * 无成员状态，多线程共享单个实例安全（§9.8 线程模型中本类不属于任何
 * 会话上下文）。定义于 AtomicFile.cpp；生产代码经 AtomicFile 默认构造
 * 间接使用，测试可显式构造以驱动真实文件系统断言。
 */
class Win32FileOps : public IFileOps {
public:
    FileResult openWriteThrough(const std::wstring& path,
                                HANDLE* handle) override;
    FileResult writeChunk(HANDLE handle, const char* data,
                          std::size_t length) override;
    FileResult flush(HANDLE handle) override;
    FileResult closeHandle(HANDLE handle) override;
    FileResult publishNew(const std::wstring& tempPath,
                          const std::wstring& targetPath) override;
    FileResult replaceExisting(const std::wstring& tempPath,
                               const std::wstring& targetPath) override;
    // 目录操作两方法（PRJ-T07 增补——故障点登记见 IFileOps.hpp
    // faultpoint::kCreateDirectories/kRemoveTree）：std::filesystem 薄封装，
    // 幂等语义（已存在＝成功 / 不存在＝删除成功）在接口契约中冻结。
    FileResult createDirectories(const std::wstring& path) override;
    FileResult removeTree(const std::wstring& path) override;
};

/**
 * @brief 存储原语门面——三个单文件原语的统一入口（IFileOps 注入接缝）。
 *
 * 生命周期与所有权：IFileOps* 为**非 owning**——默认构造时指向进程级
 * 共享的无状态 Win32FileOps（函数级静态，C++11 线程安全初始化）；注入
 * 构造（测试/未来事务引擎）时由注入方持有实现对象。拷贝/移动无意义
 * （薄门面），不提供。
 */
class AtomicFile {
public:
    /**
     * @brief 生产构造：绑定真实 Win32 实现。
     *
     * 函数级静态 Win32FileOps 实例＝无状态对象的进程级共享（避免每个
     * AtomicFile 各带一个空实现对象；线程安全由 C++11 magic static 保证）。
     */
    AtomicFile();

    /**
     * @brief 注入构造：测试 fake 或故障包装经此接缝替换文件操作实现
     *        （testkit.md D-10 形态；PRJ-T15 契约测试的消费面）。
     *
     * @param ops [in] 非 owning；不得为空（空接缝＝调用方契约违约，
     *            fail-fast 而不是静默崩溃）
     */
    explicit AtomicFile(IFileOps* ops);

    /**
     * @brief 暂存写：写入持久性保证面（§7.1 第 2 步；§7.2 行 1）。
     *
     * 执行序：打开（FILE_FLAG_WRITE_THROUGH）→ 按 kWriteChunkSize 分块
     * 写出全部字节 → FlushFileBuffers → 关闭。flush 成功＝数据落盘承诺
     * 成立；**任何一步失败即整体失败**，失败时已写入部分留作暂存残留
     * （§7.1 第 2 步"残留部分文件于 .staging"——由恢复扫描处置，见类头）。
     *
     * @param path   [in] 目标文件路径（暂存路径由调用方生成；父目录须已
     *               存在——目录创建归事务引擎第 1 步，本原语不建目录）
     * @param data   [in] 待写字节缓冲；可为 nullptr 当且仅当 length==0
     *               （空文件暂存是合法操作）
     * @param length [in] 待写字节数（单位：字节）
     * @return ok==true＝全部字节已写出且 flush 闸门通过；
     *         ok==false＝osError 为失败步骤的 Win32 原始错误码
     *         （打开/写/flush 各步骤错误码透传，失败步骤标识已由注入
     *         接缝/调用序列承载，不额外编码）
     *
     * 复杂度：O(n)，n 为字节数；每 64 KiB 一次接口调用。
     */
    FileResult writeThrough(const std::wstring& path, const char* data,
                            std::size_t length);

    /**
     * @brief 只增发布：原子可见性面——目标存在即失败，绝不覆盖
     *        （§7.1 第 4 步；§7.2 行 2）。
     *
     * "目标已存在→摘要一致视为共享"的共享判定归上层对象库（PRJ-T05）；
     * 本原语层遇到已存在目标一律失败（ERROR_ALREADY_EXISTS），把判定权
     * 留给持有摘要信息的层（权威唯一 PA-1）。
     *
     * @param tempPath   [in] 暂存文件路径（应已过 writeThrough 持久性
     *                   闸门——本方法不校验，持久性时序由调用方协议保证）
     * @param targetPath [in] 正式路径；须与 tempPath 同卷（§7.1 第 4 步
     *                   发布发生在项目根内）
     * @return ok==true＝tempPath 已改名为 targetPath（temp 不复存在）；
     *         ok==false＝osError 透传（目标已存在＝ERROR_ALREADY_EXISTS，
     *         此时两路径内容均保持原状）
     */
    FileResult publishNew(const std::wstring& tempPath,
                          const std::wstring& targetPath);

    /**
     * @brief 原子替换：提交点面——读者见旧或新完整内容之一
     *        （§7.1 第 5 步 HEAD 切换；§7.2 行 3）。
     *
     * 调用方协议（§7.1 第 5 步）：tempPath 须先过 writeThrough 闸门；
     * 本方法成功即"唯一提交点"成立（此后本次修订视为已提交）。
     *
     * @param tempPath   [in] 已持久化的新文件路径（同卷约束同 publishNew）
     * @param targetPath [in] 被替换路径；不存在时＝首次创建（仍原子可见）
     * @return ok==true＝targetPath 内容已整体切换且 WRITE_THROUGH 返回前
     *         刷盘；ok==false＝osError 透传，targetPath 保持旧内容
     *         （替换失败不留半写——MoveFileExW 的元数据原子性）
     */
    FileResult replaceFile(const std::wstring& tempPath,
                           const std::wstring& targetPath);

private:
    IFileOps* m_ops;  ///< 文件操作实现（非 owning；生存期由注入方保证）
};

}  // namespace sdurws::ird::project::win32

#endif  // RWS_IRD_PROJECT_SRC_WIN32_ATOMICFILE_HPP
