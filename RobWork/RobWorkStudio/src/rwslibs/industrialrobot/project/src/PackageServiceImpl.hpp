/**
 * @file   PackageServiceImpl.hpp
 * @brief  项目包服务实现（PackageServiceImpl）——§5.8 IPackageService
 *         实体与 §8.10 导出/导入协议的实现载体：导出＝快照捕获（D-17）
 *         →编解码执行（注入窄接口）；导入＝同卷临时位→编解码解包校验
 *         →打开协议校验→rename 发布→打开进入。私有实现头，不出
 *         include/——R-2 纪律。
 *
 * 设计依据：
 *   - units/project.md §5.8（IPackageService 服务实体）、§8.10（包导
 *     入导出存储侧：导出＝开始时快照一致数据视图＋后台进度可取消〔取
 *     消即清理临时输出〕；导入＝解包临时目录全量校验→打开协议校验→
 *     整体 rename 发布，失败/取消不留目标目录、清理临时区）、§8.9
 *     （选择性复制——导出快照的引用完整性过滤同源）、§8.7（打开协议
 *     ——导入校验/进入的协议出处）、§10.4（与 io 分工——O-09 注入式
 *     协作，零编译边）、D-17（快照一致视图）；
 *   - units/io.md §7（导出六步/导入九步的**只读语义参考**——编排序与
 *     责任切分：io＝编解码执行＋全量文件校验；project＝打开协议校验
 *     ＋rename 发布＋目标二次收口——"io 不发布"，N-7。零 io 头消费）；
 *   - 需求 PM-05（项目包存储侧）、NFR-SEC-01/NFR-SEC-02（包通道路径/
 *     预算防护——由编解码实现承担，见 IPackageCodec 注释）、AT-13；
 *   - 任务契约 tasks/foundation/PRJ-T18.json acceptance 3。
 *
 * 背景说明（导入与另存为的机制同源与差异）：两者共享"同卷临时位→
 *   校验→rename 发布→打开进入"的发布时序（PM-05 同口径），差异在
 *   校验来源与身份语义——另存为的校验对象是"自己复制出的副本"（闭包
 *   校验），导入的校验对象是"外部不可信包内容"（编解码实现的哈希/
 *   预算/穿越全量校验在前＋project 打开协议在后，两层版本各自判定，
 *   io.md §7.9 同口径）；另存为换新 projectId（D-16），导入**保持包
 *   内 projectId**（项目整体搬运，身份不变——IPackageService 类注释）。
 *
 * 错误语义（错误二分，AGENTS §3；结果轨口径见 PackageService.hpp
 *   PackageExportResult/PackageImportResult 注释——实现口径登记）：
 *   - 调用方错误 → std::invalid_argument fail-fast（codec 未注入/
 *     源未绑定〔导出〕/路径形态违约）；
 *   - 状态错误 → 返回值轨（源上下文已关闭＝ContextClosed 入 error）；
 *   - 编解码实现侧失败 → diagnostics 透传（io 域码——不解释不映射
 *     不私造 PRJ 码，CR-08/§5.7 透传纪律；取消＝canceled 位，非错误）；
 *   - 打开协议/发布环境失败 → StoreError 入 error（稳定码语义原样）。
 *
 * 线程模型：m_opMutex 服务级操作互斥（导出/导入串行——codec 消费面
 *   与临时位的单份语义；导出与源上下文命令提交可并存，D-17 保证包
 *   自洽）。
 *
 * 所有权与生命周期：createPackageService 经 unique_ptr 交付装配方；
 *   source/codec/fileOps/eventBus/sink 全部**非 owning**——生存期由
 *   装配方保证覆盖本对象。codec 为空＝装配违约（构造期 fail-fast）；
 *   fileOps 为空＝生产默认（真实 Win32FileOps——D-10 注入形态同
 *   SaveAsServiceImpl）。
 */

#ifndef SDURWS_IRD_PROJECT_SRC_PACKAGESERVICEIMPL_HPP
#define SDURWS_IRD_PROJECT_SRC_PACKAGESERVICEIMPL_HPP

#include <mutex>

#include <sdurws/ird/core/Events.hpp>
#include <sdurws/ird/project/PackageService.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

#include "StoreClone.hpp"

namespace sdurws::ird::project {

/**
 * @brief §5.8 IPackageService 契约的实现（final——不再派生；消费者只
 *        面向公共抽象与工厂，实现细节不外泄——R-2）。
 */
class PackageServiceImpl final : public IPackageService {
public:
    /**
     * @brief 构造项目包服务（工厂与测试可达）。
     *
     * @param source      [in] 源存储上下文（非 owning；可空＝本实例只
     *                    用于导入——exportPackage 届时 fail-fast）
     * @param codec       [in] 包编解码实现（非 owning；不得为空——装配
     *                    违约 fail-fast）
     * @param fileOps     [in] 文件操作通道（非 owning；可空＝生产默认
     *                    ——真实 Win32FileOps；测试注入面）
     * @param eventBus    [in] 事件总线（非 owning，可空——导入项目上
     *                    下文打开注入）
     * @param diagnostics [in] 诊断 sink（非 owning，可空＝丢弃）
     *
     * @throws std::invalid_argument codec 为空（装配违约尽早暴露）
     */
    PackageServiceImpl(ProjectStore* source,
                       IPackageCodec* codec,
                       win32::IFileOps* fileOps,
                       core::IDomainEventBus* eventBus,
                       IDiagnosticsSink* diagnostics);

    /**
     * @brief 导出项目包（§8.10/D-17——执行序与错误语义见公共头注释）。
     *
     * 执行序：前置校验 → 快照捕获（D-17，§8.9 过滤同源）→ 编解码导出
     *   （IPackageCodec::exportPack——进度/取消逐文件；零写源项目）。
     *
     * @param options [in] 导出选项
     * @return 导出结果（成败/取消/透传诊断）
     *
     * @throws std::invalid_argument codec 未注入／源未绑定／targetFile
     *         为空/父目录不存在/扩展名非 .rwpack
     */
    [[nodiscard]] PackageExportResult exportPackage(
        const PackageExportOptions& options) override;

    /**
     * @brief 导入项目包（§8.10/PM-05——执行序与错误语义见公共头注释；
     *        失败/取消不留目标目录、清理临时区）。
     *
     * 执行序：前置校验 → 建同卷临时位（".rwpack-import-<8hex>.tmp"）→
     *   编解码解包＋全量校验（NFR-SEC-01/02 由实现承担）→ 打开协议
     *   全量校验（临时目录 ReadOnly——PM-02②③⑤）→ rename 发布 →
     *   打开进入（Writable）。清理护栏覆盖全部失败/取消路径。
     *
     * @param packFile   [in] 包文件
     * @param targetDir  [in] 发布目标（必须不存在）
     * @param onProgress [in] 进度回调（可空）
     * @param onCancel   [in] 取消回调（可空）
     * @return 导入结果
     *
     * @throws std::invalid_argument codec 未注入／packFile 为空/不存在/
     *         非普通文件／targetDir 为空/已存在/父目录不存在
     */
    [[nodiscard]] PackageImportResult importPackage(
        const std::filesystem::path& packFile,
        const std::filesystem::path& targetDir,
        const ProgressCallback& onProgress,
        const CancelCallback& onCancel) override;

private:
    ProjectStore* m_source;              ///< 源上下文（非 owning；可空＝仅导入）
    IPackageCodec* m_codec;              ///< 编解码实现（非 owning；恒非空）
    win32::IFileOps* m_ops;              ///< 文件操作通道（非 owning；恒非空）
    core::IDomainEventBus* m_eventBus;   ///< 事件总线（非 owning，可空）
    IDiagnosticsSink* m_sink;            ///< 诊断 sink（非 owning，可空）
    std::mutex m_opMutex;                ///< 服务级操作互斥
};

}  // namespace sdurws::ird::project

#endif  // SDURWS_IRD_PROJECT_SRC_PACKAGESERVICEIMPL_HPP
